import os
# import time
from datetime import datetime
import shutil
from filecmp import cmp

base = "/nfs-volume/volume5/HTI/1mtcNorth"
path = base+"/live"

log = open('copy_log.log', 'w')
for f in os.listdir(path):
    #Generate new directory path
    file = os.path.join(path, f)
    time = datetime.fromtimestamp(os.stat(file).st_birthtime)
    npath = base + "/%d/%02d/%02d/%02d"%(time.year,time.month,time.day,time.hour)
    nfile = os.path.join(npath, f)
    log.write('check '+nfile+'\n')

    #Create the directory if not exist
    if not os.path.exists(npath):
        log.write('create dir: '+npath+'\n')
        os.makedirs(npath)

    #Copy the file
    shutil.copy2(file, npath)
    log.write('copy to '+npath+'\n')

    #Compare the file
    log.write('compare result: '+str(cmp(file,nfile))+'\n')

log.close()
