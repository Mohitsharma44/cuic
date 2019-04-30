from paramiko import SSHClient
from scp import SCPClient
import os
import sys
import time

def progress(filename, size, sent):
    sys.stdout.write("\r {} progress: {:.2f}".format(filename,
                                                     float(sent)/float(size)*100))


def transfer(flist):
    try:
        ssh = SSHClient()
        ssh.load_system_host_keys()
        ssh.connect('1mtcNorthServer.cuspuo.internal', username="mohitsharma44")
        # SCPCLient takes a paramiko transport and progress callback as its arguments.
        with SCPClient(ssh.get_transport(),
                        progress=progress,
                        socket_timeout=120) as scp:
        
            for f in flist:
                try:
                    scp.put(files=f,
                            remote_path='/nfs-volume/volume6/BB/1mtcNorth/live/',
                            recursive=False,
                            preserve_times=True)
                    print()
                    print("[" + time.strftime("%D - %H:%M:%S") + "] Removing "+str(f))
                    os.remove(f)
                except Exception as ex:
                    print("[" + time.strftime("%D - %H:%M:%S") + "] Exception transferring the file: " + str(f) + " -- " + str(ex))
    except Exception as ex:
        print("[" + time.strftime("%D - %H:%M:%S") + "] Exception in transfer: "+str(ex))

    
if __name__ == "__main__":    
    while True:
        try:
            # generate file list
            dirpath, dirnames, filenames = list(os.walk('/mnt/ramdisk/1mtcNorth/live'))[0]
            files = sorted(map(lambda x: os.path.join(dirpath, x), filenames), key=os.path.getmtime)
            if len(files) > 4:
                transfer(files[:len(files)-2])
        except Exception as ex:
            print("[" + time.strftime("%D - %H:%M:%S") + "] Exception in obtaining filelist: "+str(ex))
        finally:
            print("[" + time.strftime("%D - %H:%M:%S") + "] Waiting for more files...", end='\r')
            time.sleep(10)


