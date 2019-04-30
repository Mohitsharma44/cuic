import os
import time
import shutil
import json
import pyinotify
import logger

logger = logger.logger(tofile=True)

class FsEventHandler(pyinotify.ProcessEvent):
    def __init__(self, ftimestamp, status_dict, *args, **kwargs):
        self.ftimestamp = ftimestamp
        self.status_dict = status_dict
        
    def process_IN_CLOSE_WRITE(self, event):
        try:
            path = os.path.dirname(event.pathname)
            if event.pathname.endswith("raw"):
                meta_fname = os.path.basename(event.pathname)[:-3]+"meta"
                logger.critical("Creating "+str(meta_fname))
                with open(os.path.join(path, meta_fname), "w") as fh:
                    json.dump(self.status_dict.copy(), fh)
                self.ftimestamp.set(os.path.getmtime(os.path.abspath(event.pathname)))
                #shutil.copy(os.path.join(current_path, "current_status.meta"),
                #            os.path.join(path, meta_fname))
        except Exception as ex:
            logger.warning("Exception in processing IN_CLOSE_WRITE event: "+str(ex))
