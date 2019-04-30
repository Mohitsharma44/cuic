import os
import sys
import ast
import glob
import json
import time
import sched
import logger
import threading
import sched
from itertools import cycle
from xmlrpc.client import ServerProxy
from rpc_client import UOControllerRpcClient
from apscheduler.schedulers.background import BackgroundScheduler
from apscheduler import job

logger = logger.logger(tofile=True)

try:
    # relative import for pyflirpt
    current_path = os.path.split(os.path.abspath(os.path.realpath(sys.argv[0])))[0]
    sys.path.append(os.path.abspath(os.path.join(current_path, os.path.pardir)))
    from pybirger.pybirger import api
    from pyflirpt.keyboard import keyboard
except Exception as ex:
    logger.critical("Cannot import certain modules "+str(ex))
RPC_VHOST = "/vis"
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

scheduler = BackgroundScheduler()
scheduler.start()

class PTCcontroller(object):
    """
    Even though the name says ptcontroller, this class
    is responsible for controlling pan and tilt movement
    as well as taking images.
    If you just need to move the pan and tilt and not take
    the images, you must pass `noimage=True`
    """
    def __init__(self, flags=None, status_dict=None, interval=None,
                  command=None, noimage=False, loop_cntr=None,
                 ftimestamp=None, *args, **kwargs):
        self.flags = flags
        self.status_dict = status_dict
        self.interval = interval
        self.command = command
        self.noimage = noimage
        self.loop_cntr = loop_cntr
        self._old_ftime = 0
        self.birger = api.Birger(os.getenv("birger_uds_host"), os.getenv("birger_uds_port"))
        self.pt = keyboard.KeyboardController(pt_ip=os.getenv("north_pt_ip").encode(), pt_port=os.getenv("north_pt_port").encode())
        #self.ptsched = None
        self.next_pos = 0
        self.pt_positions = None
        # dummy apscheduler.job variable
        self.ptjob = job.Job
        self.ftimestamp = ftimestamp
        
    def start(self):
        """
        This function will start the ptcontroller
        scheduler.
        """
        try:
            self.pt_positions = cycle(self.command["pt_locs"])
            self._old_ftime = 0
            self.flags["ready_flag"] = True
            self.flags["resend_flag"] = False
            # If there is already a job running, remove it
            if not self.ptjob.id == "ptcontroller":
                self.ptjob = scheduler.add_job(self.ptcontrol, 'interval',
                                               seconds=self.interval.value, id="ptcontroller")
            scheduler.reschedule_job(self.ptjob.id, trigger='interval', seconds=self.interval.value)
            self.ptjob.resume()
        except Exception as ex:
            logger.error("Error scheduling ptcontroller: "+str(ex))

    def stop(self):
        """
        This function will stop the scheduled ptcontroller
        """
        try:
            if self.ptjob.id == "ptcontroller":
                self.ptjob.pause()
            #scheduler.cancel(self.ptsched)
        except ValueError as ve:
            logger.info("ptcontroller is not running")
        except Exception as ex:
            logger.error("Error stopping ptscheduler: "+str(ex))

    def shutdown(self):
        """
        This will shutdown the scheduler once and for all
        """
        try:
            scheduler.shutdown()
        except Exception as ex:
            # who cares at this stage
            logger.warning("Error shutting down.. but killed forcibly "+str(ex))
            
    def restart_camera(self):
        try:
            self.flags["ready_flag"] = False
            logger.warning("Restarting cuicucapture code")
            if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in STOPPED_STATENAME:
                proxy.supervisor.stopProcess("cuiccapture")
                time.sleep(10)
            if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
                proxy.supervisor.startProcess("cuiccapture")
                time.sleep(10)
            """
            logger.info("I will restart the camera")
            time.sleep(5)
            """
            self.flags["resend_flag"] = True
            self.flags["ready_flag"] = True
            return True
        except Exception as ex:
            logger.error("Exception in restart_camera: "+str(ex))
            self.flags["ready_flag"] = True

    def set_exposure(self, location, value):
        """
        Set the exposure of the camera to `value`
        """
        if value != -99:
            cam_command = {
                "interval": 0,
                "focus": -99,
                "location": location,
                "status": False,
                "aperture": -99,
                "exposure": int(value),
                "stop": False,
                "capture": 0,
                "kill": ""
            }
            self._send_to_camera(cam_command)

    def set_focus(self, value):
        if value != -99:
            self.birger.set_focus(value)

    def set_aperture(self, value):
        if value != -99:
            self.birger.set_aperture(value)
    
    def _cam_status(self, location):
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
            "location": location,
            "status": True,
            "aperture": -99,
            "exposure": -99,
            "stop": False,
            "capture": 0,
            "kill": ""
        }
        vis_cam_rpc_client = UOControllerRpcClient(vhost=RPC_VHOST,
                                                   queue_name=known_vis_queues[location]["cam"])
        cam_response = vis_cam_rpc_client.call(json.dumps(cam_status_cmd))
        status_message = None
        try:
            status_message = ast.literal_eval(cam_response.strip("b'"))
        except Exception as ex:
            # Cannot communicate with the camera, restart it?
            #cam_response = self.restart_camera()
            logger.error("Error in _cam_status: "+str(ex))
            #status_message = ast.literal_eval(cam_response.strip("b'"))
        # hacky way to clear the object
        vis_cam_rpc_client = None
        del vis_cam_rpc_client
        return status_message

    def _lens_status(self):
        """
        Obtain lens related information
        Parameter
        ---------
        birger: `pybirger` object
        """
        return {
            "focus": self.birger.get_focus().decode('ascii'),
            "aperture": self.birger.get_aperture().decode('ascii')
        } 
        
    def status(self, cam_location, tofile=True):
        try:
            self.flags["ready_flag"] = False
            cam_status = self._cam_status(cam_location)
            lens_status = self._lens_status()
            pt_status = self.next_pos
            _status = {"camera": cam_status, "lens": lens_status, "pt_locs": self.pt.current_pos()}
            if tofile:
                with open(os.path.join(current_path, "current_status.meta"), "w") as fh:
                    json.dump(_status, fh)
            self.status_dict.update(_status)
            return _status
        except Exception as ex:
            logger.critical("Error obtaining status: "+str(ex))
        finally:
            self.flags["ready_flag"] = True

    def _send_to_camera(self, command=None):
        while not self.flags["ready_flag"]:
            time.sleep(1)
        try:
            self.flags["ready_flag"] = False
            vis_cam_rpc_client = UOControllerRpcClient(vhost=RPC_VHOST,
                                                       queue_name=known_vis_queues[command["location"]]["cam"])
            cam_response = vis_cam_rpc_client.call(json.dumps(command.copy()))
            if not cam_response:
                self.restart_camera()
            vis_cam_rpc_client = None
            del vis_cam_rpc_client
            # write the status information for camera and lens as metadata
            self.status(cam_location=command["location"], tofile=True)
            return True
        except Exception as ex:
            logger.critical("Error sending command to camera: "+str(ex))
            return False
        finally:
            self.flags["ready_flag"] = True

    def _get_last_file_modified_time(self, directory):
        files = sorted(glob.glob(os.path.join(directory, "*.raw")), key=os.path.getmtime)
        _new_ftime = os.path.getmtime(files[-1])
        logger.info("latest: {} @ {}".format(files[-1], _new_ftime))
        return _new_ftime
        """
        if _new_ftime - self._old_ftime > self.interval.value:
        # No new files, should restart the camera!!
            self.restart_camera()
            self._old_ftime = time.time()
            return self._old_ftime
        else:
            return _new_ftime
        """

    def pantilt(self, pan_tilt):
        """
        pan and tilt to a particular position.
        Return when pt reaches the requested position
        Parameters
        ----------
        pan_tilt: tuple or list
            (pan, tilt) or [pan, tilt]
        """
        try:
            self.pt.pan(int(pan_tilt[0]))
            self.pt.tilt(int(pan_tilt[1]))
            while not self.pt.ready():
                time.sleep(1)
            # wait for PT to settle down:
            time.sleep(2)
            return True
        except Exception as ex:
            logger.error("Error with panning or tilting: "+str(ex))

        
    def ptcontrol(self):
        logger.info("command in pt: "+str(self.command))
        """
        if self.command.get("pt_locs", None):
            pt_positions = iter(self.command["pt_locs"])
            # send command to capture single image
        """
        def _new_file_captured():
            self.flags["ready_flag"] = False
            trial = 0
            while trial < 4:
                # lets wait some 20 seconds before giving up
                if self.ftimestamp.value > self._old_ftime:
                    self.flags["ready_flag"] = True
                    return self.ftimestamp.value
                trial += 1
                time.sleep(5)
            else:
                return self._old_ftime
                
        cam_command = {
            "location": str(self.command["location"]),
            "status": bool(self.command["status"]),
            "kill": str(self.command["kill"]),
            "capture": 1,
            "exposure": float(self.command["exposure"]),
            "focus": int(self.command["focus"]),
            "interval": int(self.command["interval"]),
            "stop": bool(self.command["stop"]),
            "aperture": int(self.command["aperture"]),
        }
        try:
            logger.info("self.loop_cntr = {}".format(self.loop_cntr.value))
            if (self.loop_cntr.value > 0 or self.loop_cntr.value <= -1):
                # This means, just loop over positions X number of times
                if not self.noimage.value:
                    logger.info("Checking _new_ftime")
                    _new_ftime = _new_file_captured()
                    logger.warning("NEW FTIME: {}".format(_new_ftime))
                    if _new_ftime == self._old_ftime:
                        logger.info("no new files were captured, restart the camera")
                        self.restart_camera()
                        while not self.flags["ready_flag"]:
                            # wait for camera to finish restarting
                            time.sleep(1)
                        logger.info("now setting old time = new time")
                    self._old_ftime = _new_ftime
                # If allowed to run?
                while not self.flags["ready_flag"]:
                    time.sleep(1)
                logger.info("Allowed to run pt or replay")
                # resend the command? then don't move pan and tilt
                try:
                    if self.flags["resend_flag"]:
                        logger.warning("Resending command!!")
                        if self._send_to_camera(command=cam_command):
                            self.flags["resend_flag"] = False
                            # so that finally block doesn't decrement counter
                            self.loop_cntr.set(self.loop_cntr.value + 1)
                            return True
                    logger.info("No replay required, pan and/or tilt")
                    # pan and tilt
                    self.next_pos = next(self.pt_positions)
                    # update the status
                    logger.info("I will move PT : {}".format(self.noimage.value))
                    #time.sleep(5)
                    self.pantilt(self.next_pos)
                    while not self.pt.ready():
                        time.sleep(1)
                    # need to give a settle down period
                    time.sleep(2)
                    # capture image if requested
                    if not self.noimage.value:
                        #logger.info("I will send the capure command to camera")
                        if not self._send_to_camera(command=cam_command):
                            self.flags["resend_flag"] = True
                        # need to give a settle down period
                        time.sleep(2)
                        return True
                except Exception as ex:
                    logger.info("Exception in panning, tilting or capturing: "+str(ex))
                finally:
                    if self.loop_cntr.value > 0 and not self.flags["resend_flag"]:
                        logger.info("decrementing self.loop_cntr")
                        self.loop_cntr.set(self.loop_cntr.value - 1)
                        logger.info("decremented loop cntr = {}".format(self.loop_cntr.value))                            
        except Exception as ex:
                logger.error("Error in ptcontrol loop: "+str(ex))
        finally:
            time.sleep(3)

        
