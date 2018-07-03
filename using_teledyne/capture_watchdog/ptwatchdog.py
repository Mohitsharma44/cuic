import os
import json
import time
import pika
import logger
import asyncio
import argparse
import pyinotify
from ctypes import c_bool
import multiprocessing as mp
from pthandler import PTCcontroller
from fshandler import FsEventHandler
from rpc_client import UOControllerRpcClient

MIN_INTERVAL = 5
logger = logger.logger(tofile=True)

parser = argparse.ArgumentParser(description='WatchDog -- sidekick to the cuic capture'+ \
                                 '(currently only supports teledyne cameras)')
parser.add_argument('--loc', metavar='L', type=str, nargs='+', 
dest='location', required=True,
                    help='Location where the camera is running. Queue name will be generated from this')

args = parser.parse_args()

RPC_VHOST = "/vis"
RPC_QUEUE_NAME = args.location[0] + "_watchdog"

RPC_SERVER = os.getenv("rpc_server")
RPC_PORT = os.getenv("rpc_port")
RPC_USER = os.getenv("rpc_user")
RPC_PASS = os.getenv("rpc_pass")
IMG_DIR = os.path.join(os.getenv("mtc_vis_dir"), args.location[0], "live")

mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE | pyinotify.IN_MOVED_TO | pyinotify.IN_CLOSE_WRITE # watched events

known_vis_queues = {
    "1mtcNorth": {"cam": "1mtcNorth", "watchdog": "1mtcNorth_watchdog"},
    "1mtcSouth": {"cam": "1mtcSouth", "watchdog": "1mtcSouth_watchdog"},
    "370Roof" : {"cam": "370Roof", "watchdog": "370Roof_watchdog"},
    "test" : {"cam": "test", "watchdog": "test_watchdog"},
}

def monitor_fs(directory, ftimestamp, status_dict):
    """
    Monitoring file system for new files every `interval`.
    If no new files are created between x and x+1.5*(`interval`)
    then a callback request will be made
    Parameters
    ----------
    directory: str
        absolute path to the directory to be monitored
    ftimestamp: float
        (preferably) mp.Manager().Value object with float dtype
    """
    wm = pyinotify.WatchManager()
    loop = asyncio.get_event_loop()
    event_handler = FsEventHandler(ftimestamp, status_dict)
    notifier = pyinotify.AsyncioNotifier(wm, loop,
                                         default_proc_fun=event_handler)
    wm.add_watch(directory, mask)
    loop.run_forever()
    notifier.stop()

def on_rpc_request(ch, method, props, body):
    """
    Blocking Function for handling the incoming data
    Refer "http://pika.readthedocs.io/en/0.11.2/modules/adapters/blocking.html"
    """
    try:
        command_dict = json.loads(body.decode("utf-8"))
        logger.info("Correlation id: " + str(props.correlation_id))
        response = "ERR"

        # ----- Set Camera / Lens parameters?
        if command_dict["exposure"] != -99:
            ptc.set_exposure(command_dict["location"], command_dict["exposure"])
            reponse = "OK"
        if command_dict["focus"] != -99:
            ptc.set_focus(command_dict["focus"])
            response = "OK"
        if command_dict["aperture"] != -99:
            ptc.set_aperture(command_dict["aperture"])
            response = "OK"
            
        # ----- Status of the camera?
        if command_dict["status"]:
            logger.info("Requesting for status")
            ptc.status(command_dict["location"], False)
            response = status_dict.copy()
            logger.info("Reponse of status: "+str(response))

        # ----- Stop all tasks?
        elif command_dict["stop"]:
            logger.info("Requesting to stop")
            ptc.stop()
            response = "Stopped"

        # ----- PTCC tasks?
        else:
            # total number of scans / captures            
            loop_cntr.set(int(command_dict["capture"]*len(command_dict["pt_locs"])))
            noimage.set(False)
            if not loop_cntr.value:
                logger.info("loop_cntr is 0 so setting noimage as True and loop_cntr to 1")
                # if capture = 0, and we still reach here,
                # it means we just need to pan and/or tilt
                # but not capture images
                noimage.set(True)
                loop_cntr.set(1)
            logger.info("updating command_dict")
            command.update(command_dict)
            logger.info("checking if interval < MIN_INTERVAL")
            interval.set(command_dict["interval"])
            if interval.value < MIN_INTERVAL:
                logger.info("Increasing interval")
                # wait additional MIN_INTERVAL between captures
                interval.set(int(command_dict["interval"]) + MIN_INTERVAL)
            logger.info("starting ptc")
            ptc.start()
            response = "Started"
        ch.basic_publish(exchange='',
                         routing_key=props.reply_to,
                         properties=pika.BasicProperties(correlation_id=props.correlation_id),
                         body=str(response))
        ch.basic_ack(delivery_tag=method.delivery_tag)
    except Exception as ex:
        logger.error("Error in on_rpc_request: "+str(ex))


if __name__ == "__main__":
    mngr = mp.Manager()
    # shared dictionary containing flags
    flags = mngr.dict({'capture': 0, 'ready_flag': True, 'resend_flag': False})
    # shared dictionary for all processes to update their status
    status_dict = mngr.dict()
    # interval between capture (default = 10)
    interval = mngr.Value('i', 10)
    noimage = mngr.Value(c_bool, False)
    command = mngr.dict({})
    loop_cntr = mngr.Value('i', 0)
    # new file creation time, to be updated by fshandler
    ftimestamp = mngr.Value(float, 1)
    
    # Initialize the ptccontroller
    ptc = PTCcontroller(flags, status_dict, interval, command, noimage, loop_cntr, ftimestamp)

    credentials = pika.PlainCredentials(os.getenv("rpc_user"), os.getenv("rpc_pass"))
    connection = pika.BlockingConnection(
        pika.ConnectionParameters(os.getenv("rpc_server"), os.getenv("rpc_port"),
                                  RPC_VHOST, credentials))
    channel = connection.channel()
    channel.queue_declare(queue=RPC_QUEUE_NAME)

    # Initialize fsmonitoring
    logger.info("Starting fs monitoring")
    fs_process = mp.Process(target=monitor_fs, args=(IMG_DIR, ftimestamp, status_dict))
    fs_process.daemon=True
    fs_process.start()
    
    try:
        channel.basic_qos(prefetch_count=1)
        channel.basic_consume(on_rpc_request, queue=RPC_QUEUE_NAME)
        logger.info("Listening for RPC messages")
        channel.start_consuming()
    except KeyboardInterrupt as ki:
        print()
        ptc.stop()
        ptc.shutdown()
        logger.info("Exiting now")
    except Exception as ex:
        logger.critical("Critical Exception in main: "+str(ex))

