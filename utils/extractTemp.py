import pyinotify
import os
from w1thermsensor import W1ThermSensor
from datetime import datetime

# path = "/nfs-volume/volume5/HTI/1mtcNorth"
path = '/home/pi/temp'
dest = '/home/pi/temp'
wm = pyinotify.WatchManager()  # Watch Manager
mask = pyinotify.IN_CREATE  # watched events

class EventHandler(pyinotify.ProcessEvent):
    # def gettemp(self,f):
    #     # file = open(f,'r')
    #     # return file.read()
    #     for sensor in W1ThermSensor.get_available_sensors():
    #         print("Sensor %s has temperature %.2f" % (sensor.id, sensor.get_temperature()))

    def process_IN_CREATE(self, event):
        if event.pathname.split('.')[-1] == 'MOV':
            nf = event.pathname.split('/')[-1].split('.')[0]+'.temp'
            print("Creating: {}".format(event.pathname))
            t = open(os.path.join(des, nf), 'w')
            for sensor in W1ThermSensor.get_available_sensors():
                t.write("{},{},{}".format(datetime.now().isoformat(),sensor.id,sensor.get_temperature()))
            t.close()

handler = EventHandler()
notifier = pyinotify.Notifier(wm, handler)
wdd = wm.add_watch(path, mask, rec=True)

notifier.loop()
