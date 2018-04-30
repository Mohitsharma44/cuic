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
#logger.info(args.location)

RPC_SERVER = os.getenv("rpc_server")
RPC_PORT = os.getenv("rpc_port")
RPC_USER = os.getenv("rpc_user")
RPC_PASS = os.getenv("rpc_pass")
IMG_DIR = os.getenv("mtc_vis_dir")

mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE | pyinotify.IN_MOVED_TO # watched events


class FsEventHandler(pyinotify.ProcessEvent):
    def process_IN_CREATE(self, event):
        print("Creating:", event.pathname)

    def process_IN_DELETE(self, event):
        print("Removing:", event.pathname)

    def process_IN_MOVED_TO(self, event):
        print("Moved: ", event.pathname)


def monitor_fs(directory):
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
                                         default_proc_fun=FsEventHandler())
    wm.add_watch(directory, mask)
    loop.run_forever()
    notifier.stop()

if __name__ == "__main__":
    """
    {
    "location": self.app.pargs.loc,
    "capture": int(self.app.pargs.capture),
    "interval": int(self.app.pargs.every),
    "focus": int(self.app.pargs.focus),
    "aperture": int(self.app.pargs.aper),
    "exposure": float(self.app.pargs.exp),
    "stop": bool(self.app.pargs.stop),
    "status": bool(self.app.pargs.stat),
    "kill": str(self.app.pargs.kill)
    }
    """
    mp_manager = multiprocessing.Manager()
    ## RPC from controller
    location = mp_manager.Value('location', "")
    capture = mp_manager.Value('capture', 0)
    interval = mp_manager.Value('interval', 0)
    focus = mp_manager.Value('focus', 0)
    aperture = mp_manager.Value('aperture', 0)
    exposure = mp_manager.Value('exposure', 0)
    stop = mp_manager.Value('stop', 0)
    status = mp_manager.Value('status', 0)
    kill = mp_manager.Value('kill', "")

    #logger.info("Opening Connection with RabbitMQ")
    logger.info("Starting fs monitoring")
    process = multiprocessing.Process(target=monitor_fs, args=("/home/mohitsharma44/Downloads",))
    process.start()
