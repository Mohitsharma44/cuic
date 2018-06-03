# Notifier example from tutorial
#
# See: http://github.com/seb-m/pyinotify/wiki/Tutorial
#
import pyinotify

# path = "/nfs-volume/volume5/HTI/1mtcNorth"
path = './mov'
wm = pyinotify.WatchManager()  # Watch Manager
mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE  # watched events

class EventHandler(pyinotify.ProcessEvent):
    def gettemp(self,f):
        file = open(f,'r')
        return file.read()

    def process_IN_CREATE(self, event):
        nf = event.pathname.split('.')[0]+'.temp'
        print("Creating: {}".format(nf))
        t = open(nf, 'w')
        t.write(self.gettemp(event.pathname))
        t.close()

    def process_IN_DELETE(self, event):
        print("Removing: {}".format(event.pathname))

handler = EventHandler()
notifier = pyinotify.Notifier(wm, handler)
wdd = wm.add_watch(path, mask, rec=True)

notifier.loop()
