import os
import ast
import time
import json
import logger
from datetime import datetime
from itertools import cycle
from apscheduler import job
from xmlrpc.client import ServerProxy
from rpc_client import UOControllerRpcClient
from apscheduler.schedulers.background import BackgroundScheduler


logger = logger.logger(tofile=True)
status_path = "/var/log/cuic/statuses/"
RPC_VHOST = "/vis"
known_queues = {
    "1mtcNorth": {"cam": "1mtcNorth", "watchdog": "1mtcNorth_watchdog", "pt": "1mtcNorth_pt", "lens": "1mtcNorth_lens"},
    "1mtcSouth": {"cam": "1mtcSouth", "watchdog": "1mtcSouth_watchdog", "pt": "1mtcSouth_pt", "lens": "1mtcSouth_lens"},
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

class Orchestrator(object):
    def __init__(self, ftimestamp, command, dt_exp_list=None):
        """
        Parameters
        ----------
        ftimestamp: mp.Manager().Value()
            shared value object containing timestamp of most recent file
        command: mp.Manager().dict()
            shared dict object containing the commmand to be performed
        dt_exp_list: list
            list containing list of  start datettime.time, end datetime.time and
        exposure value to adjust the camera to different exposure values
        throughout the day
        """
        self.ftimestamp = ftimestamp
        self.command = command
        self.dt_exp_list = dt_exp_list
        self.location = ""
        self.interval = 10
        self._old_ftime = 0
        self.loop_cntr = 0
        self.pt_locs = []
        self.next_pos = (0,0)
        #self.viscam = self.lens = self.pt = None
        self.replay_flags = {"cam": False, "pt": False, "lens": False}
        self.orchestrator_job = job.Job
        
    def _initialize(self, location):
        """
        Initialize the viscam, lens and pt objects.
        This is supposed to be done only once.
        """
        self.location = location
        """
        try:
            logger.info("Orchestrator initialize got: "+str(location))
            if not self.viscam:
                self.viscam = UOControllerRpcClient(vhost=RPC_VHOST,
                                                    queue_name=known_queues[location]["cam"])
            if not self.lens:
                self.lens = UOControllerRpcClient(vhost=RPC_VHOST,
                                                  queue_name=known_queues[location]["lens"])
            if not self.pt:
                self.pt = UOControllerRpcClient(vhost=RPC_VHOST,
                                                queue_name=known_queues[location]["pt"])
        except Exception as ex:
            logger.critical("Error initializing: "+str(ex))
        """
        
    def start(self):
        if self.command["interval"] > self.interval:
            self.interval = self.command["interval"]
        self.loop_cntr = int(self.command["capture"]*len(self.command["pt_locs"]))
        self.pt_locs = cycle(self.command["pt_locs"])
        if not self.orchestrator_job.id == "orchestrator":
            self.orchestrator_job = scheduler.add_job(self.orchestrate, 'interval',
                                           seconds=self.interval, id="orchestrator")
        scheduler.reschedule_job(self.orchestrator_job.id, trigger='interval', seconds=self.interval)
        self.orchestrator_job.resume()
        
    def stop(self):
        try:
            if self.orchestrator_job.id == "orchestrator":
                self.orchestrator_job.pause()
                #scheduler.cancel(self.ptsched)
        except ValueError as ve:
            logger.info("orchestrator_job is not running")
        except Exception as ex:
            logger.error("Error stopping orchestrator_job: "+str(ex))
            
    def shutdown(self):
        try:
            scheduler.shutdown()
        except Exception as ex:
            # who cares at this stage
            logger.warning("Error shutting down.. but killed forcibly "+str(ex))

    def restart_camera(self):
        try:
            logger.warning("Restarting cuiccapture code")
            if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in STOPPED_STATENAME:
                proxy.supervisor.stopProcess("cuiccapture")
                time.sleep(10)
            if proxy.supervisor.getProcessInfo("cuiccapture")['statename'] not in RUNNING_STATENAME:
                proxy.supervisor.startProcess("cuiccapture")
                time.sleep(10)
            self.replay_flags["cam"] = True
            return True
        except Exception as ex:
            logger.error("Exception in restart_camera: "+str(ex))

    def restart_ptcontroller(self):
        try:
            logger.warning("Restarting ptcontroller")
            if proxy.supervisor.getProcessInfo("ptcontroller")['statename'] not in STOPPED_STATENAME:
                proxy.supervisor.stopProcess("ptcontroller")
                time.sleep(10)
            if proxy.supervisor.getProcessInfo("ptcontroller")['statename'] not in RUNNING_STATENAME:
                proxy.supervisor.startProcess("ptcontroller")
                time.sleep(10)
            return True
        except Exception as ex:
            logger.error("Exception in restart_ptcontroller: "+str(ex))

    def restart_lenscontroller(self):
        try:
            logger.warning("Restarting lenscontroller")
            if proxy.supervisor.getProcessInfo("lenscontroller")['statename'] not in STOPPED_STATENAME:
                proxy.supervisor.stopProcess("lenscontroller")
                time.sleep(10)
            if proxy.supervisor.getProcessInfo("lenscontroller")['statename'] not in RUNNING_STATENAME:
                proxy.supervisor.startProcess("lenscontroller")
                time.sleep(10)
            return True
        except Exception as ex:
            logger.error("Exception in restart_lenscontroller: "+str(ex))
            
    def command_tocamera(self, command):
        viscam = UOControllerRpcClient(vhost=RPC_VHOST,
                                       queue_name=known_queues[self.location]["cam"])
        cam_response = viscam.call(json.dumps(command))
        #logger.info("cam_response: "+str(cam_response))
        if not cam_response:
            logger.error("No response from cuiccapture ")
            self.restart_camera()
            #self.command_tocamera(command)
        return cam_response
    
    def command_tolens(self, command):
        lens = UOControllerRpcClient(vhost=RPC_VHOST,
                                     queue_name=known_queues[self.location]["lens"])
        lens_response = lens.call(json.dumps(command))
        #logger.info("lens_response: "+str(lens_response))
        if not lens_response:
            logger.error("No response from lens controller ")
            self.restart_lenscontroller()
            self.command_tolens(command)
        return lens_response

    def command_topt(self, command):
        pt = UOControllerRpcClient(vhost=RPC_VHOST,
                                   queue_name=known_queues[self.location]["pt"])
        pt_response = pt.call(json.dumps(command))
        #logger.info("pt_response: "+str(pt_response))
        if not pt_response:
            logger.error("No response from pt controller ")
            self.restart_ptcontroller()
            self.command_topt(command)
        return pt_response

    def set_exposure(self, value):
        if value != -99:
            cam_command = {
                "interval": 0,
                "focus": -99,
                "location": self.location,
                "status": False,
                "aperture": -99,
                "exposure": int(value),
                "stop": False,
                "capture": 0,
                "kill": ""
            }
            return self.command_tocamera(cam_command)

    def set_focus(self, value):
        if value != -99:
            lens_command = {
                "focus": int(value)
            }
            return self.command_tolens(lens_command)

    def set_aperture(self, value):
        if value != -99:
            lens_command = {
                "aperture": int(value)
            }
            return self.command_tolens(lens_command)

    def pantilt(self, value):
        _pt_trial = 0
        if value != [-999999, -999999]:
            pt_command = {
                "pan": value[0],
                "tilt": value[1]
            }
            return self.command_topt(pt_command)
        
    def cam_status(self):
        try:
            cam_command = {
                "interval": 0,
                "focus": -99,
                "location": self.location,
                "status": True,
                "aperture": -99,
                "exposure": -99,
                "stop": False,
                "capture": 0,
                "kill": ""
            }
            cam_response = ast.literal_eval(self.command_tocamera(cam_command).strip("b'"))
            return cam_response
        except Exception as ex:
            logger.error("Error obtaining camera status: "+str(ex))
            return False

    def lens_status(self):
        try:
            lens_command = {
                "status": True
            }
            lens_response = ast.literal_eval(self.command_tolens(lens_command).strip("b'"))
            return lens_response
        except Exception as ex:
            logger.error("Error obtaining lens status: "+str(ex))
            return False
            
    def pt_status(self):
        try:
            pt_command = {
                "status": True
            }
            pt_response = ast.literal_eval(self.command_topt(pt_command).strip("b'"))
            return pt_response
        except Exception as ex:
            logger.error("Error obtaining pt status: "+str(ex))
            return False
            
    def status(self):
        return {"cam": self.cam_status(),
                "lens": self.lens_status(),
                "pt": self.pt_status()}

    def _custom_exposure(self):
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

        if self.dt_exp_list == None:
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
                                          "expval": x['expval']}, self.dt_exp_list))
            exposure_reqd = list(filter(lambda x: x if x else None,
                                        map(_time_in_range, explist)))
            return exposure_reqd[0]
            
    def orchestrate(self):
        try:
            def _newfilecaptured():
                trial = 0
                while trial < 10:
                    # lets wait some 50 seconds before giving up
                    if self.ftimestamp.value > self._old_ftime:
                        self._old_ftime = self.ftimestamp.value
                        return self.ftimestamp.value
                    trial += 1
                    time.sleep(5)
                else:
                    return False
            cam_command = {
                "location": str(self.command["location"]),
                "status": bool(self.command["status"]),
                "kill": str(self.command["kill"]),
                "capture": 1,
                "exposure": float(self.command["exposure"]),
                "focus": int(self.command["focus"]),
                #"interval": int(self.command["interval"]),
                "interval": 1,
                "stop": bool(self.command["stop"]),
                "aperture": int(self.command["aperture"]),
            }
            lens_command = {
                "focus": str(self.command["focus"]),
                "aperture": str(self.command["aperture"])
            }
            if self.loop_cntr > 0 or self.loop_cntr <= -1:
                logger.info("will run")
                try:
                    # replay previous commands ?
                    if self.replay_flags["pt"]:
                        logger.info("will replay previous pt command")
                        pt_response = ast.literal_eval(self.pantilt(self.next_pos).strip("b'"))
                        
                    # -- capture and make sure image is captured --
                    if self.replay_flags["cam"]:
                        logger.info("will replay previous command")
                        if self.command_tocamera(cam_command):
                            if not _newfilecaptured():
                                self.restart_camera()
                                self.replay_flags["cam"] = True
                                return False
                            self.replay_flags["cam"] = False
                            return True
                        else:
                            return False
                    # --
                    # pan to the next position
                    logger.info("will pan/tilt to new position")
                    self.next_pos = next(self.pt_locs)
                    _pt_trial = 0
                    if self.next_pos != [-999999, -999999]:
                        pt_response = ast.literal_eval(self.pantilt(self.next_pos).strip("b'"))
                        logger.info("pt_response: "+str(pt_response))
                        # wait until pan and tilt actually moves to correct position
                        while _pt_trial < 5:
                            if self.next_pos[0] != -999999:
                                _pan_ok = pt_response['pan'] == int(self.next_pos[0])
                            else:
                                _pan_ok = True
                            if self.next_pos[1] != -999999:
                                _tilt_ok = pt_response['tilt'] == int(self.next_pos[1])
                            else:
                                _tilt_ok = True
                            if _pan_ok and _tilt_ok:
                                self.replay_flags["pt"] = False
                                break
                            self.restart_ptcontroller()
                            _pt_trial += 1
                            pt_response = ast.literal_eval(self.pantilt(self.next_pos).strip("b'"))
                            logger.info("new pan/tilt positions do not match with what was requested. Trying again: {}".format(_pt_trial))
                            time.sleep(1)
                        else:
                            self.replay_flags["pt"] = True
                            logger.critical("Error with pan and tilt!! Will Abort Future captures")
                            return False
                    # write status info to file first:
                    logger.info("dump current status")
                    current_status = self.status()
                    with open(os.path.join(status_path, "current.status"), "w") as fh:
                        json.dump(current_status, fh)
                    # ready to capture an image
                    logger.info("sending capture command to camera")
                    # -- capture and make sure image is captured -- 
                    if not self.command_tocamera(cam_command):
                        logger.info("no response from camera, set replay flag for cam")
                        self.replay_flags["cam"] = True
                        return False
                    if not _newfilecaptured():
                        logger.info("No new file captured, will restart camera")
                        self.restart_camera()
                        self.replay_flags["cam"] = True
                        return False
                    # --
                    # Adjust exposure if needed for next capture
                    required_exposure = self._custom_exposure()
                    if current_status['cam']['exposure'] != required_exposure:
                        logger.info("Setting exposure: "+str(required_exposure))
                        self.set_exposure(required_exposure)
                except Exception as ex:
                    logger.error("Error in inner orchestrate condition: "+str(ex))
                    # Better to restart all services
                    self.restart_camera()
                    self.restart_ptcontroller()
                    self.restart_lenscontroller()
                finally:
                    if self.loop_cntr > 0 and \
                       not self.replay_flags["cam"] and \
                       not self.replay_flags["pt"]:
                        logger.info("decrementing self.loop_cntr")
                        self.loop_cntr = self.loop_cntr - 1
                        logger.info("decremented loop cntr = {}".format(self.loop_cntr))
        except Exception as ex:
            logger.critical("Error in orchestrate: "+str(ex))
