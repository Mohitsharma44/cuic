settings {
logfile = "/var/log/lsyncd/lsyncd.log",
statusFile = "/var/log/lsyncd/lsyncd-status.log",
}
sync {
default.rsync,
delete = false,
source="/mnt/ramdisk/",
target="mohitsharma44@10.8.0.1:/home/mohitsharma44/audubon_imgs_live/",
rsync = {
compress = true,
acls = true,
verbose = true,
_extra = { "-a" },
},
delay = 5,
maxProcesses = 4,
}