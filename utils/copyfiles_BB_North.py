import os, sys
from datetime import datetime,timedelta
import shutil
from filecmp import cmp
import sched
import time

def copyfile():
    base = "/nfs-volume/volume6/BB/1mtcNorth"
    path = base+"/live"

    #Begin logging
    tlog = datetime.now().isoformat()
    log = open('copylog_'+tlog+'.log', 'w')

    for f in os.listdir(path):
        #Generate new directory path
        file = os.path.join(path, f)
        #Get Modification time
        time = datetime.fromtimestamp(os.stat(file).st_mtime)
        npath = base + "/%d/%02d/%02d/%02d"%(time.year,time.month,time.day,time.hour)
        nfile = os.path.join(npath, f)
        log.write('check '+file+'\n')

        #Create the directory if not exist
        if not os.path.exists(npath):
            log.write('create dir: '+npath+'\n')
            os.makedirs(npath)

        if not os.path.exists(nfile):
            #Copy the file
            shutil.copy2(file, npath)
            log.write('copy to '+npath+'\n')
            #Compare the file
            log.write('compare result: '+str(cmp(file,nfile))+'\n')
        else:
            log.write('already exist: '+nfile+'\n')

    log.close()
#     sys.exit()

class PeriodicScheduler(object):
    def __init__(self):
        self.scheduler = sched.scheduler(time.time, time.sleep)

    def setup(self, interval, action, actionargs=()):
        action(*actionargs)
        self.scheduler.enter(interval, 1, self.setup,
                        (interval, action, actionargs))

    def run(self):
        self.scheduler.run()

try:
    INTERVAL = 60*15 # every 15 minutes
    periodic_scheduler = PeriodicScheduler()
    periodic_scheduler.setup(INTERVAL, copyfile) # it executes the event just once
    periodic_scheduler.run() # it starts the scheduler
except KeyboardInterrupt:
    sys.exit()
