import os
import time

path = os.path.join(os.getenv("mtc_vis_dir"), "1mtcSouth", "live")

while True:
    allfiles = sorted(os.listdir(path))
    if len(allfiles) > 20:
        for f in allfiles[:5]:
            if f.endswith('raw'):
                print "Removing: "+str(f)
                os.remove(os.path.join(path, f))
    time.sleep(1)
