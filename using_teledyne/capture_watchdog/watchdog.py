import argparse
import atexit
import json
import time
import os
import pika
import logger
import asyncio
import pyinotify
import multiprocessing

logger = logger.logger(tofile=False)

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


class FsEventHandler(pyinotify.ProcessEvent):
    def __init__(self, cam_commands):
        self.cam_commands = cam_commands
        
    def process_IN_CREATE(self, event):
        print("Creating:", event.pathname)
        print(self.cam_commands)
        
    def process_IN_DELETE(self, event):
        print("Removing:", event.pathname)
        print(self.cam_commands)
        
    def process_IN_MOVED_TO(self, event):
        print("Moved: ", event.pathname)
        print(self.cam_commands)

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
    notifier = pyinotify.AsyncioNotifier(wm, loop,
                                         default_proc_fun=FsEventHandler(cam_commands))
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

