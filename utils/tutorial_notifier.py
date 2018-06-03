import pyinotify

# path = "/nfs-volume/volume5/HTI/1mtcNorth"
path = './mov'
des = './mov/temp'
wm = pyinotify.WatchManager()  # Watch Manager
mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE  # watched events

class EventHandler(pyinotify.ProcessEvent):
    def gettemp(self,f):
        file = open(f,'r')
        return file.read()

    def process_IN_CREATE(self, event):
        nf = event.pathname.split('/')[-1].split('.')[0]+'.temp'
        print("Creating: {}".format(os.path.join(des, nf)))
        t = open(os.path.join(des, nf), 'w')
        t.write(self.gettemp(event.pathname))
        t.close()

    def process_IN_DELETE(self, event):
        print("Removing: {}".format(event.pathname))

handler = EventHandler()
notifier = pyinotify.Notifier(wm, handler)
wdd = wm.add_watch(path, mask, rec=True)

notifier.loop()
