import os, sys
from datetime import datetime,timedelta
import shutil
from filecmp import cmp
import sched
import time
import hashlib

#File compare
def hash_bytestr_iter(bytesiter, hasher, ashexstr=False):
    for block in bytesiter:
        hasher.update(block)
    # print('here')
    return (hasher.hexdigest() if ashexstr else hasher.digest())

def file_as_blockiter(afile, blocksize=65536):
    with afile:
        block = afile.read(blocksize)
        while len(block) > 0:
            yield block
            block = afile.read(blocksize)

def copyfile():
    base = "/nfs-volume/volume6/BB/1mtcNorth"
    path = base+"/live"

    #Begin logging
    tlog = datetime.now().isoformat()
    log = open('copylog_'+'_'.join(base.split('/')[-2:])+'_'+tlog+'.log', 'w')

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
            log.write('Copy to '+npath+'\n')
            #Compare the file
            hashfile = hash_bytestr_iter(file_as_blockiter(open(file, 'rb')), hashlib.sha256())
            hashnfile = hash_bytestr_iter(file_as_blockiter(open(nfile, 'rb')), hashlib.sha256())
            log.write('Compare result: '+str(hashfile==hashnfile)+'\n')
        else:
            log.write('Already exists: '+nfile+'\n')

    log.close()

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
