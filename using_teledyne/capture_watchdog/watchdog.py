import argparse
import atexit
import json
import time
import os
import pika
import logger
import asyncio
import pyinotify
import threading
import multiprocessing
from rpc_client import UOControllerRpcClient
from xmlrpc.client import ServerProxy

logger = logger.logger(tofile=True)

parser = argparse.ArgumentParser(description='WatchDog -- sidekick to the cuic capture'+ \
                                 '(currently only supports teledyne cameras)')
parser.add_argument('--loc', metavar='L', type=str, nargs='+', dest='location', required=True,
                    help='Location where the camera is running. Queue name will be generated from this')
args = parser.parse_args()
RPC_VHOST = "/vis"
RPC_QUEUE_NAME = args.location[0] + "_watchdog"

RPC_SERVER = os.getenv("rpc_server")
RPC_PORT = os.getenv("rpc_port")
RPC_USER = os.getenv("rpc_user")
RPC_PASS = os.getenv("rpc_pass")
IMG_DIR = os.path.join(os.getenv("mtc_vis_dir"), args.location[0], "live")

mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE | pyinotify.IN_MOVED_TO # watched events
known_vis_queues = {
    "1mtcNorth": {"cam": "1mtcNorth", "watchdog": "1mtcNorth_watchdog"},
    "1mtcSouth": {"cam": "1mtcSouth", "watchdog": "1mtcSouth_watchdog"},
    "370Roof"  : {"cam": "370Roof", "watchdog": "370Roof_watchdog"},
    "test"     : {"cam": "test", "watchdog": "test_watchdog"},
}
proxy = ServerProxy('http://127.0.0.1:9001/RPC2')
RUNNING_STATENAME     = ["RUNNING"]
NOT_RUNNING_STATENAME = ["STARTING", "BACKOFF",
                         "FATAL", "UNKNOWN"]
STOPPED_STATENAME     = ["STOPPING", "STOPPED", "EXITED"]

class FsEventHandler(pyinotify.ProcessEvent):
    def __init__(self, cam_commands):
        self._current_timestamp = 0
        self.cam_commands = cam_commands
        self.check_timestamp()
        self.BUSY = False
        
    def check_timestamp(self):
        """
        Function to check the timestamp and if a drift of 
        > 3*interval requested is observed, it makes a call
        to restart the cuiccapture code.
        .. Note: Its a super Ugly way for checking timestamp of the files
        and comparing them. This method creates new thread object
        everytime its called. Hate this implementation
        but cannot find a better alternative. Maybe for some othertime or ... decade 
        """
        try:
            if self.cam_commands.get("capture", 0) != 0 and self.BUSY == False:
                if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
                    self.restart_camera()
                    # Give camera a little time to start capturing
                    self._current_timestamp = time.time()
                if time.time() - self._current_timestamp > 3*(cam_commands["interval"]):
                    logger.warning("No Images received in last {} seconds".\
                                   format(3*(cam_commands["interval"])))
                    self.restart_camera()
            else:
                # watchdog shouldn't restart the code next time the camear is instructed
                # to capture, so update the _current_timestamp
                # to current time
                self._current_timestamp = time.time()
        except Exception as ex:
            logger.error("Error in check_timestamp: "+str(ex))
        finally:
            threading.Timer(2, self.check_timestamp).start()
            
    def restart_camera(self):
        try:
            self.BUSY = True
            logger.warning("Restarting cuicucapture code")
            if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in STOPPED_STATENAME:
                proxy.supervisor.stopProcess("cuiccapture")
                time.sleep(10)
            if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
                proxy.supervisor.startProcess("cuiccapture")
                time.sleep(10)
            vis_cam_rpc_client = UOControllerRpcClient(vhost=RPC_VHOST,
                                                       queue_name=known_vis_queues[self.cam_commands["location"]]["cam"])
            vis_cam_response = vis_cam_rpc_client.call(json.dumps(dict(self.cam_commands)))
        except Exception as ex:
            logger.error("Exception in restart_camera: "+str(ex))
        finally:
            self.BUSY = False
            
    def process_IN_CREATE(self, event):
        try:
            print("Creating:", event.pathname)
            self._current_timestamp = os.path.getmtime(event.pathname)
        except Exception as ex:
            logger.warning("Exception in processing IN_CREATE event: "+str(ex))
        
    def process_IN_DELETE(self, event):
        print("Removing:", event.pathname)
        
    def process_IN_MOVED_TO(self, event):
        print("Moved: ", event.pathname)

def monitor_fs(directory, cam_commands):
    """
    Monitoring file system for new files every `interval`.
    If no new files are created between x and x+1.5*(`interval`)
    then a callback request will be made
    Parameters
    ----------
    directory: str
        absolute path to the directory to be monitored
    """
    wm = pyinotify.WatchManager()
    loop = asyncio.get_event_loop()
    event_handler = FsEventHandler(cam_commands)
    notifier = pyinotify.AsyncioNotifier(wm, loop,
                                         default_proc_fun=event_handler)
    #notifier = pyinotify.AsyncioNotifier(wm, loop,
    #                                     default_proc_fun=FsEventHandler(cam_commands))
    wm.add_watch(directory, mask)
    loop.run_forever()
    notifier.stop()

def _store_commands(command_dict, cam_commands):
    """
    Function to store the controller command
    to the `mp.Manager` datatypes.
    .. Note: Do not call it separately. This 
    function *overwrites* the previously
    stored commands. This should only be called when
    a new command is sent by controller.
    """
    new_command = {
        "location": str(command_dict["location"]),
        "status": bool(command_dict["status"]),
        "kill": str(command_dict["kill"]),
        "capture": int(command_dict["capture"]),
        "exposure": float(command_dict["exposure"]),
        "focus": int(command_dict["focus"]),
        "interval": int(command_dict["interval"]),
        "stop": bool(command_dict["stop"]),
        "aperture": int(command_dict["aperture"]),
    }
    # Only update the command when status != True. i.e
    # we don't want to store the command if the controller
    # has sent request to just get the status of the camera
    if not new_command.get("status", False):
        cam_commands.update(new_command)
        print("Stored ==> ", cam_commands)
    
def on_rpc_request(ch, method, props, body):
    """
    Blocking Function for handling the incoming data
    Refer "http://pika.readthedocs.io/en/0.11.2/modules/adapters/blocking.html"
    """
    command_dict = json.loads(body.decode("utf-8"))
    logger.info("Correlation id: " + str(props.correlation_id))
    response = "OK"
    _store_commands(command_dict, cam_commands)
    ch.basic_publish(exchange='',
                     routing_key=props.reply_to,
                     properties=pika.BasicProperties(correlation_id=props.correlation_id),
                     body=str(response))
    ch.basic_ack(delivery_tag=method.delivery_tag)
    
if __name__ == "__main__":
    mp_manager = multiprocessing.Manager()
    cam_commands = mp_manager.dict()

    logger.info("Starting fs monitoring")
    process = multiprocessing.Process(target=monitor_fs, args=(IMG_DIR, cam_commands))
    process.start()
    logger.info("Opening Connection with RabbitMQ")
    credentials = pika.PlainCredentials(os.getenv("rpc_user"), os.getenv("rpc_pass"))
    connection = pika.BlockingConnection(
        pika.ConnectionParameters(os.getenv("rpc_server"), os.getenv("rpc_port"),
                                  RPC_VHOST, credentials))
    channel = connection.channel()
    channel.queue_declare(queue=RPC_QUEUE_NAME)

    try:
        channel.basic_qos(prefetch_count=1)
        channel.basic_consume(on_rpc_request, queue=RPC_QUEUE_NAME)
        logger.info("Listening for RPC messages")
        channel.start_consuming()
    except KeyboardInterrupt as ki:
        print()
        logger.info("Exiting now")
    except Exception as ex:
        logger.critical("Critical Exception in main: "+str(ex))

