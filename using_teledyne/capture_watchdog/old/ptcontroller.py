import os
import json
import time
import logger

logger = logger.logger(tofile=True)

try:
    # relative import for pyflirpt
    current_path = os.path.split(os.path.abspath(os.path.realpath(sys.argv[0])))[0]
    sys.path.append(os.path.abspath(os.path.join(current_path, os.path.pardir)))
    from pyflirpt.keyboard import keyboard
except Exception as ex:
    logger.critical("Cannot import pyflirpt for pt control. Make sure its in the parent directory '../'")

def pt_controller(ptobj=None, pt_locations=None,
                  cam_commands=None, lock=None):
    """
    Responsible for capturing images via a camera
    on pan and tilt mechanism.
    This will move pan and tilt to a location, capture an
    image and then proceed to next location
    Parameters
    ----------
    pt_obj: pyflirpt obj
    pt_locations: dict
        containing (p,t) positions as a list or tuple
    cam_commands: dict
        containing camera command dict
    lock: multiprocessing / multithreading.Lock
    """
    while True:
        try:
            pt_pos = None
            # Keep trying until we get the locations
            while True:
                if pt_locations.get("locations", None):
                    pt_pos = iter(pt_locations["locations"])
                    break
                time.sleep(1)
            new_command = {
                "location": str(cam_commands["location"]),
                "status": bool(cam_commands["status"]),
                "kill": str(cam_commands["kill"]),
                "capture": 1,
                "exposure": float(cam_commands["exposure"]),
                "focus": int(cam_commands["focus"]),
                "interval": int(cam_commands["interval"]),
                "stop": bool(cam_commands["stop"]),
                "aperture": int(cam_commands["aperture"]),
            }
            # number of times to run the loop
            loop_cntr = cam_commands["capture"]
            # seconds_counter .. to make sure the following loop runs, at max, once every `interval`
            sec_cntr = time.time() + cam_commands["interval"]
            while True:
                if loop_cntr == 0:
                    # set capture modes to stop
                    cam_commands.update({"stop": True})
                if cam_commands["stop"]:
                    break
                # Run either `capture` times or indefinitely
                elif (loop_cntr > 0 or loop_cntr == -1):
                    try:
                        with lock:
                            try:
                                next_pos = next(pt_pos)
                                logger.info("moving to: " + str(next_pos))
                                time.sleep(5)
                            except StopIteration as si:
                                pt_pos = iter(pt_locations["locations"])
                                if sec_cntr > 0:
                                    time.sleep(sec_cntr)
                                sec_cntr = time.time() + new_command["interval"]
                                if loop_cntr > 0:
                                    loop_cntr -= 1
                                if loop_cntr == 0:
                                    cam_commands.update({"stop": True})
                                    break
                        #_process_commands(new_command, lock)
                        with open(os.path.join(current_path, "current_status.meta"), "r+") as fh:
                            status = json.load(fh)
                            status.update({"pt":next_pos})
                            fh.seek(0)
                            json.dump(status, fh)
                            fh.truncate()
                            sec_cntr -= time.time()
                    except Exception as ex:
                        pass
        except Exception as ex:
            logger.info("Exception in ptcontroller: "+str(ex))
        time.sleep(1)
    logger.critical("Last message from ptcontroller")

def pan(ptobj, pan):
    """
    Pan to a particular position
    Parameters
    ----------
    ptobj: `pyflirpt` object
    pan: int
        position to pan to
    """
    ptobj.pan(pan)

def pan(ptobj, tilt):
    """
    Tilt to a particular position
    Parameters
    ----------
    ptobj: `pyflirpt` object
    pan: int
        position to tilt to
    """
    ptobj.tilt(tilt)
