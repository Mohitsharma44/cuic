import os
import time
import shutil
import json
import pyinotify
import logger

logger = logger.logger(tofile=True)
status_path = "/var/log/cuic/statuses/"

class FsEventHandler(pyinotify.ProcessEvent):
    def __init__(self, ftimestamp, status_dict, *args, **kwargs):
        self.ftimestamp = ftimestamp
        self.status_files = ["current.status"]
        self.status = {"cam":{}, "pt":{}, "lens": {}}
        
    def process_IN_CLOSE_WRITE(self, event):
        try:
            path = os.path.dirname(event.pathname)
            if event.pathname.endswith("raw"):
                meta_fname = os.path.basename(event.pathname)[:-3]+"meta"
                logger.info("Creating "+str(meta_fname))
                shutil.copy(os.path.join(status_path, "current.status"),
                            os.path.join(path, meta_fname))
                #with open(os.path.join(path, meta_fname), "w") as fh:
                #    json.dump(self.status.copy(), fh)
                # Update the shared dict:
                #status_dict.update(self.status.copy())
                self.ftimestamp.set(os.path.getmtime(os.path.abspath(event.pathname)))
        except Exception as ex:
            logger.warning("Exception in processing IN_CLOSE_WRITE event: "+str(ex))
