import argparse
import atexit
import json
import time
import ast
import os
import sys
import pika
import psutil
import shutil
import logger
import asyncio
import pyinotify
import threading
import multiprocessing
from datetime import datetime
from rpc_client import UOControllerRpcClient
from xmlrpc.client import ServerProxy

PROCNAME = "watchdog.py"
### --- relative import for pybirger
logger = logger.logger(tofile=True)
try:
    current_path = os.path.split(os.path.abspath(os.path.realpath(sys.argv[0])))[0]
    #current_path = os.path.dirname(os.path.abspath(__file__))
    #print("argv[0] "+str(sys.argv[0]))
    #print("This will be appended: "+str(os.path.join(current_path, os.path.abspath('..'))))
    #sys.path.append(os.path.join(current_path, os.path.abspath('..')))
    #sys.path.append(os.path.join(current_path, os.path.abspath('..')))
    sys.path.append(os.path.abspath(os.path.join(current_path, os.path.pardir)))
    from pybirger.pybirger import api
    birger = api.Birger(os.getenv("birger_uds_host"), os.getenv("birger_uds_port"))
except Exception as ex:
    logger.critical("Cannot import pybirger for lens control. Make sure its in the parent directory '../'")
### ---

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

mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE | pyinotify.IN_MOVED_TO | pyinotify.IN_CLOSE_WRITE # watched events
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

expvals=[{"start": "05:00:00", "end": "21:00:00", "expval": 500},
         {"start": "21:00:00", "end": "23:59:59", "expval": 2000000},
         {"start": "00:00:00", "end": "05:00:00", "expval": 2000000}]

class FsEventHandler(pyinotify.ProcessEvent):
    def __init__(self, cam_commands):
        self._current_timestamp = 0
        self._internal_cntr = 0
        self.exp_timer = 0
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
            # Check for time and set exposure for the camera every 1 minute
            #self.exp_timer += 1
            #if self.exp_timer % 30 == 0:
            #    required_exposure = _custom_exposure(expvals)
            #    logger.info("Setting Exposure to {}".format(required_exposure))
            #    set_exposure(required_exposure)
            #    self.exp_timer = 0

            # Is the camera supposed to capture image continuously?
            if self.cam_commands.get("interval", 0) != 0:
                # Is the camera capturing finite, X, frames?
                if (self.cam_commands.get("capture", 0) > 0 and \
                    self._internal_cntr < self.cam_commands.get("capture", 0) and \
                    self.BUSY == False):
                    if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
                        self._restart_camera()
                        # Make sure that camera has captured atleast 1 image in
                        # (interval + 60) seconds
                        self._current_timestamp = time.time()
                    elif time.time() - self._current_timestamp > (60 + (self.cam_commands["interval"])):
                        logger.warning("No Images received in last {} seconds".\
                                       format(60 + (self.cam_commands["interval"])))
                        self._restart_camera()
                    # Stop monitoring files once the X frames have been captured
                    elif self._internal_cntr >= self.cam_commands["capture"]:
                        logger.info("All frames captured, Stopping fsmonitoring")
                        _temp_cam_commands = self.cam_commands
                        _temp_cam_commands["capture"] = 0
                        self.cam_commands = _temp_cam_commands
                        self._internal_cntr = 0
                    else:
                        self._internal_cntr += 1
                # Is the camera capturing infinite frames?
                elif self.cam_commands.get("capture", 0) == -1 and self.BUSY == False:
                    if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
                        self._restart_camera()
                        # Make sure that camera has captured atleast 1 image in
                        # (interval + 60) seconds
                        self._current_timestamp = time.time()
                    elif time.time() - self._current_timestamp > (60 + (self.cam_commands["interval"])):
                        logger.warning("No Images received in last {} seconds".\
                                       format(60 + (self.cam_commands["interval"])))
                        self._restart_camera()

                # Camera is not capturing anything
                else:
                    # watchdog shouldn't restart the code next time the camera is instructed
                    # to capture, so update the _current_timestamp
                    # to current time
                    self._current_timestamp = time.time()
            if self.cam_commands.get("capture", 0) != 0 and self.BUSY == False:
                if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
                    self._restart_camera()
                    # Give camera a little time to start capturing
                    self._current_timestamp = time.time()
                if time.time() - self._current_timestamp > 6*(cam_commands["interval"]):
                    logger.warning("No Images received in last {} seconds".\
                                   format(6*(cam_commands["interval"])))
                    self._restart_camera()
            else:
                # watchdog shouldn't restart the code next time the camear is instructed
                # to capture, so update the _current_timestamp
                # to current time
                self._current_timestamp = time.time()
        except Exception as ex:
            logger.error("Error in check_timestamp: "+str(ex))
            logger.warning("Since I cannot recover from this error, I will die. Hopefully someone will resuscitate me")
            _quit()
        finally:
            threading.Timer(2, self.check_timestamp).start()
            
    def _restart_camera(self):
        try:
            self.BUSY = True
            # After restarting camera, only capture remaining frames:
            # -- this is the way to propagate the changes to mp.manager.dict
            _temp_cam_commands = self.cam_commands
            _temp_cam_commands["capture"] = _temp_cam_commands["capture"] - self._internal_cntr
            self.cam_commands = _temp_cam_commands
            # --
            self._internal_cntr = 0
            restart_camera(self.cam_commands)
        except Exception as ex:
            logger.error("Exception in _restart_camera: "+str(ex))
        finally:
            # Set the _current_timestamp to current time
            self._current_timestamp = time.time()
            self.BUSY = False
            
    def process_IN_CLOSE_WRITE(self, event):
        try:
            path = os.path.dirname(event.pathname)
            if event.pathname.endswith("raw"):
                meta_fname = os.path.basename(event.pathname)[:-3]+"meta"
                logger.info("Creating "+str(meta_fname))
                self._current_timestamp = os.path.getmtime(event.pathname)
                shutil.copy(os.path.join(current_path, "current_status.meta"), os.path.join(path, meta_fname))
                # Check if exposure is what we want it to be
                required_exposure = _custom_exposure(expvals)
                with open(os.path.join(current_path, "current_status.meta"), 'r') as fh:
                    current_status = json.load(fh)
                if required_exposure != current_status['exposure']:
                    logger.info("Setting Exposure to {}".format(required_exposure))
                    set_exposure(required_exposure)
                #os.system("cp {} {}".format("current_status.meta", os.path.join(path, meta_fname)))
        except Exception as ex:
            logger.warning("Exception in processing IN_CREATE event: "+str(ex))

        
    def process_IN_DELETE(self, event):
        print("Removing:", event.pathname)
        
    def process_IN_MOVED_TO(self, event):
        print("Moved: ", event.pathname)

def set_exposure(value):
    if value != -99:
        cam_command = {
            "interval": 0,
            "focus": -99,
            "location": args.location[0],
            "status": False,
            "aperture": -99,
            "exposure": int(value),
            "stop": False,
            "capture": 0,
            "kill": ""
        }
        return _process_commands(cam_command)

def restart_camera(cam_commands):
    try:
        logger.warning("Restarting cuicucapture code")
        if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in STOPPED_STATENAME:
            proxy.supervisor.stopProcess("cuiccapture")
            time.sleep(10)
        if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
            proxy.supervisor.startProcess("cuiccapture")
            time.sleep(10)
        vis_cam_rpc_client = UOControllerRpcClient(vhost=RPC_VHOST,
                                                   queue_name=known_vis_queues[cam_commands["location"]]["cam"])
        vis_cam_response = vis_cam_rpc_client.call(json.dumps(dict(cam_commands)))
        return vis_cam_response
    except Exception as ex:
        logger.error("Exception in restart_camera: "+str(ex))
            
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

def _status(loc):
    """
    Function to obtain the status from the camera 
    and the birger adapter
    Parameters
    ----------
    loc: str
        location of the camera to fetch the information of
    """
    cam_status_cmd = {
            "interval": 0,
            "focus": -99,
            "location": loc,
            "status": True,
            "aperture": -99,
            "exposure": -99,
            "stop": False,
            "capture": 0,
            "kill": ""
    }
    try:
        vis_cam_rpc_client = UOControllerRpcClient(vhost=RPC_VHOST,
                                                   queue_name=known_vis_queues[cam_status_cmd["location"]]["cam"])
        cam_response = vis_cam_rpc_client.call(json.dumps(cam_status_cmd))
        #if not cam_response:
        # Cannot communicate with the camera, restart it?
        #    cam_response = restart_camera(cam_status_cmd)
        status_message = ast.literal_eval(cam_response.strip("b'"))
        # relevant information from the lens
        lens_status = {
            "focus": birger.get_focus().decode('ascii'),
            "aperture": birger.get_aperture().decode('ascii')
        }
        status_message.update(lens_status)
        # hacky way to clear the object
        vis_cam_rpc_client = None
        del vis_cam_rpc_client
        return status_message
    except Exception as ex:
        logger.warning("Error in obtaining status: {}".format(ex))
        _quit()
        return False
    
def _process_commands(command_dict, cam_commands=None):
    """
    Function to process the controller command
    and to save to the `mp.Manager.dict`.
    .. Note: Do not call it separately. This 
    function *overwrites* the previously
    stored commands. This should only be called when
    a new command is sent by controller.
    """
    try:
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
            # if the command has aperture or focus information,
            # send that to the birger adapter
            focus = new_command["focus"]
            if focus != -99:
                birger.set_focus(new_command["focus"])
            aperture = new_command["aperture"]
            if aperture != -99:
                birger.set_aperture(aperture)
            if cam_commands:
                cam_commands.update(new_command)
                # let's also save this camera command to file
                with open(os.path.join(current_path, "last_command.cmd"), "w") as fh:
                    json.dump(cam_commands.copy(), fh)
            else:
                cam_commands = new_command
            vis_cam_rpc_client = UOControllerRpcClient(vhost=RPC_VHOST,
                                                       queue_name=known_vis_queues[cam_commands["location"]]["cam"])
            cam_response = vis_cam_rpc_client.call(json.dumps(cam_commands.copy()))
            print("Stored ==> ", cam_commands)
            # hacky way to clear the object
            vis_cam_rpc_client = None
            del vis_cam_rpc_client
            # Obtain the status to write to file as metadata
            metadata = _status(new_command["location"])
            if not metadata:
                # Probably camera capture code is not running,
                # restart the capture code and obtain metadata again
                restart_camera(new_command)
                metadata = _status(new_command["location"])
            with open(os.path.join(current_path, "current_status.meta"), "w") as fh:
                json.dump(_status(new_command['location']), fh)
            return "OK"
        else:
            # obtain the status from camera
            return _status(new_command['location'])
    except Exception as ex:
        logger.error("Error processing command: "+str(ex))
        # exit now!
        sys.exit(0)
    
def on_rpc_request(ch, method, props, body):
    """
    Blocking Function for handling the incoming data
    Refer "http://pika.readthedocs.io/en/0.11.2/modules/adapters/blocking.html"
    """
    command_dict = json.loads(body.decode("utf-8"))
    logger.info("Correlation id: " + str(props.correlation_id))
    response = _process_commands(command_dict, cam_commands)
    ch.basic_publish(exchange='',
                     routing_key=props.reply_to,
                     properties=pika.BasicProperties(correlation_id=props.correlation_id),
                     body=str(response))
    ch.basic_ack(delivery_tag=method.delivery_tag)

def _replay_prev_command(cam_commands):
    """
    check if `last_command.cmd` file exists and
    replay it to the cmaera and/or to the lens.
    Parameters
    ----------
    cam_commands: mulitprocessing manager dict object
        that contains current camera commands shared
        amongst different processes and functions
    """
    try:
        logger.info("Replaying previous command")
        prev_command_file = os.path.join(current_path, "last_command.cmd")
        if os.path.exists(prev_command_file):
            with open(prev_command_file, "r") as fh:
                prev_command = json.load(fh)
            #restart_camera(prev_command)
            restart_camera(prev_command)
            _process_commands(prev_command, cam_commands)
    except Exception as ex:
        logger.warning("Error reading last command file: "+str(ex))

def _quit():
    sys.exit(2)

def _custom_exposure(dt_exp_list=None):
    """
    This function can be used to set a custom
    mapping of exposure during different time
    of day. Expected format
    [{start=datetime.time, end=datetime.time, exposusre=expval}],...
    """
    def _time_in_range(exp_dict):
        """Return exposure value if current time is in timerange"""
        if exp_dict['start'] <= exp_dict['end']:
            if exp_dict['start'] <= datetime.now() <= exp_dict['end']:
                return exp_dict['expval']
        else:
            if exp_dict['start'] <= datetime.now() or datetime.now() <= exp_dict['end']:
                return exp_dict['expval']

    if dt_exp_list == None:
        return True
    else:
        explist = list(map(lambda x: {"start": datetime.strptime(x['start'],
                                                                 "%H:%M:%S").replace(datetime.now().year,
                                                                                     datetime.now().month,
                                                                                     datetime.now().day),
                                      "end": datetime.strptime(x['end'],
                                                               "%H:%M:%S").replace(datetime.now().year,
                                                                                   datetime.now().month,
                                                                                   datetime.now().day),
                                      "expval": x['expval']}, dt_exp_list))
        exposure_reqd = list(filter(lambda x: x if x else None,
                                    map(_time_in_range, explist)))
        return exposure_reqd[0]

if __name__ == "__main__":
    for proc in psutil.process_iter():
        if PROCNAME in proc.name():
            if "watchdog.py" in [x for y in list(map(lambda x: x.split('/'), proc.cmdline())) for x in y]:
                proc.terminate()

    mp_manager = multiprocessing.Manager()
    cam_commands = mp_manager.dict()

    logger.info("Starting fs monitoring")
    process = multiprocessing.Process(target=monitor_fs, args=(IMG_DIR, cam_commands))
    process.daemon=True
    process.start()
    logger.info("Opening Connection with RabbitMQ")
    credentials = pika.PlainCredentials(os.getenv("rpc_user"), os.getenv("rpc_pass"))
    connection = pika.BlockingConnection(
        pika.ConnectionParameters(os.getenv("rpc_server"), os.getenv("rpc_port"),
                                  RPC_VHOST, credentials))
    channel = connection.channel()
    channel.queue_declare(queue=RPC_QUEUE_NAME)

    try:
        # replay previous command
        _replay_prev_command(cam_commands)
        channel.basic_qos(prefetch_count=1)
        channel.basic_consume(on_rpc_request, queue=RPC_QUEUE_NAME)
        logger.info("Listening for RPC messages")
        channel.start_consuming()
    except KeyboardInterrupt as ki:
        print()
        logger.info("Exiting now")
    except Exception as ex:
        logger.critical("Critical Exception in main: "+str(ex))
