import os
import sys
import json
import pika
import logger
import asyncio
import argparse
import pyinotify
import multiprocessing as mp
from fshandler import FsEventHandler
from orchestrator import Orchestrator

logger = logger.logger(tofile=True)
status_path = "/var/log/cuic/statuses/"

parser = argparse.ArgumentParser(description='WatchDog -- sidekick to the cuic capture'+ \
                                 '(currently only supports teledyne cameras)')
parser.add_argument('--loc', metavar='L', type=str, nargs='+',
                    dest='location', required=True,
                    help='Location where the camera is running. Queue name will be generated from this')
args = parser.parse_args()
mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE | pyinotify.IN_MOVED_TO | pyinotify.IN_CLOSE_WRITE # watched events

RPC_VHOST = "/vis"
RPC_QUEUE_NAME = args.location[0] + "_watchdog"
RPC_SERVER = os.getenv("rpc_server")
RPC_PORT = os.getenv("rpc_port")
RPC_USER = os.getenv("rpc_user")
RPC_PASS = os.getenv("rpc_pass")
CA_CERT = os.getenv("ca_cert")
IMG_DIR = os.path.join(os.getenv("mtc_vis_dir"), args.location[0], "live")

def monitor_fs(directory, ftimestamp, status_dict):
    """
    Monitoring file system for new files every `interval`.
    Parameters
    ----------
    directory: str
        absolute path to the directory to be monitored
    ftimestamp: float
        (preferably) mp.Manager().Value object with float dtype
    status_dict: dict
        mp.Manager().dict() object for storing status information
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
        logger.info("Got: "+str(command_dict))
        orchestrator._initialize(command_dict["location"])
        response = "Done"
        if command_dict["exposure"] != -99:
            response = orchestrator.set_exposure(command_dict["exposure"])
        if command_dict["focus"] != -99:
            response = orchestrator.set_focus(command_dict["focus"])
        if command_dict["aperture"] != -99:
            response = orchestrator.set_aperture(command_dict["aperture"])
        if command_dict["status"]:
            response = orchestrator.status()
        elif command_dict["stop"]:
            logger.info("Requesting to stop")
            orchestrator.stop()
            response = "Stopped"
        else:
            # Maybe we just need to pan and or tilt to a location
            if command_dict["capture"] == 0:
                # Just check if pt value is not (-999999, -999999).
                # If either pan or tilt is -999999, its OK.
                if command_dict["pt_locs"][0] != [-999999, -999999]:
                    orchestrator.pantilt(command_dict["pt_locs"][0])
                    response = "Panned and/or Tilted"
            else:
                command.update(command_dict)
                orchestrator.start()
                response = "started"
        ch.basic_publish(exchange='',
                         routing_key=props.reply_to,
                         properties=pika.BasicProperties(correlation_id=props.correlation_id),
                          body=str(response))
        ch.basic_ack(delivery_tag=method.delivery_tag)
    except Exception as ex:
        logger.error("Error in on_rpc_request: "+str(ex))

        
if __name__ == "__main__":
    # exposure values
    expvals=[{"start": "06:00:00", "end": "20:00:00", "expval": 500},
             {"start": "20:00:00", "end": "23:59:59", "expval": 2000000},
             {"start": "00:00:00", "end": "06:00:00", "expval": 2000000}]
    mngr = mp.Manager()
    ftimestamp = mngr.Value(float, 0)
    status_dict = mngr.dict({})
    command = mngr.dict({})
    logger.info("Starting fs monitoring")
    fs_process = mp.Process(target=monitor_fs, args=(IMG_DIR, ftimestamp, status_dict))
    fs_process.daemon=True
    fs_process.start()
    orchestrator = Orchestrator(ftimestamp, command, expvals)
    credentials = pika.PlainCredentials(os.getenv("rpc_user"), os.getenv("rpc_pass"))
    connection = pika.BlockingConnection(
        pika.ConnectionParameters(host=os.getenv("rpc_server"), port=os.getenv("rpc_port"),
                                  virtual_host=RPC_VHOST, credentials=credentials, heartbeat=0))
    
    channel = connection.channel()
    channel.queue_declare(queue=RPC_QUEUE_NAME)
    try:
        channel.basic_qos(prefetch_count=1)
        channel.basic_consume(on_rpc_request, queue=RPC_QUEUE_NAME)
        logger.info("Listening for RPC messages")
        channel.start_consuming()
    except KeyboardInterrupt as ki:
        logger.info("Exiting now")
        sys.exit(1)
    except Exception as ex:
        logger.critical("Critical Exception in main: "+str(ex))
