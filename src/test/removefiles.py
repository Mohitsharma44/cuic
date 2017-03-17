import os
import time

path = '/mnt/ramdisk'

while True:
    for f in os.listdir(path):
        print "Removing: "+str(f)
        os.remove(os.path.join(path, f))
    time.sleep(0.1)
