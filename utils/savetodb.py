import pyinotify
import os
# from w1thermsensor import W1ThermSensor
from datetime import datetime
from keys import usrid, password

from couchbase.cluster import Cluster, PasswordAuthenticator
cluster = Cluster('couchbase://localhost')
cluster.authenticate(PasswordAuthenticator(usrid, password))
bucket = cluster.open_bucket('test-bucket')

# path = '/home/pi/temp'
path = '/home/yw3447'
wm = pyinotify.WatchManager()  # Watch Manager
mask = pyinotify.IN_CLOSE_WRITE  # watched events

class EventHandler(pyinotify.ProcessEvent):
    def savetodb(self,event):
        if event.pathname.split('.')[-1] == 'MOV':
            seq = bucket.get('nextseq').value
            fname = event.pathname.split('/')[-1]
            fpath = event.pathname
            time = os.stat(fpath).st_mtime

            print("Creating: {}".format(event.pathname))
            # for sensor in W1ThermSensor.get_available_sensors():
            #     temp = sensor.get_temperature()
            bucket.upsert(seq,{'fname':fname,'fpath':fpath,'time':time,
            # 'temp':temp
            })
            bucket.replace('nextseq','{:08d}'.format(int(seq)+1))

    def process_IN_CLOSE_WRITE(self, event):
        self.savetodb(event)

handler = EventHandler()
notifier = pyinotify.Notifier(wm, handler)
wdd = wm.add_watch(path, mask, rec=True)

notifier.loop()
