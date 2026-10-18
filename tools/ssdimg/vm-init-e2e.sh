#!/bin/sh
# rdinit= of vm-e2e.sh's boot test (appended to the amd64 initramfs as /init.e2e): the KVM guest's
# virtual disks get the PS3's names, vda as /dev/ps3db, vdb as /dev/ps3da, then the initramfs's own
# /init runs unchanged
export PATH=/bin:/sbin
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev
n=0
while [ ! -e /sys/class/block/vdb/dev ] && [ $n -lt 100 ]; do usleep 100000; n=$((n + 1)); done
mknod /dev/ps3db b $(tr : ' ' < /sys/class/block/vda/dev)
mknod /dev/ps3da b $(tr : ' ' < /sys/class/block/vdb/dev)
umount /sys
umount /proc
exec /init
