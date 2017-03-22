import os
import time

path = '/mnt/ramdisk/'

while True:
    allfiles = sorted(os.listdir(path))
    if len(allfiles) > 20:
        for f in allfiles[:10]:
            if f.endswith('raw'):
                print "Removing: "+str(f)
                os.remove(os.path.join(path, f))
            elif f.endswith('hdr'):
                print "Removing: "+str(f)
                os.remove(os.path.join(path, f))
        time.sleep(1)
    time.sleep(1)
