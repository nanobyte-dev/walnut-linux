#!/bin/sh
# rdinit= of vm-e2e.sh's boot test (appended to the amd64 initramfs as /init.e2e): the KVM guest's
# virtual disks get the PS3's names, vda as /dev/ps3db, vdb as /dev/ps3da, then the initramfs's own
# /init runs unchanged. With e2e_guard=1 on the command line the names come from ps3guard_sim
# (/ps3guard_sim.ko, appended too): disks ps3da and ps3db over vdb and vda with the kernel guard of
# local/0045 and its /sys/block/ps3db/write_guard; otherwise from device nodes (no guard). A third disk,
# vdc, becomes /dev/ps3dc (dev_hdd1).
export PATH=/bin:/sbin
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev
n=0
while [ ! -e /sys/class/block/vdb/dev ] && [ $n -lt 100 ]; do usleep 100000; n=$((n + 1)); done
if grep -q 'e2e_guard=1' /proc/cmdline; then
	insmod /ps3guard_sim.ko db=/dev/vda da=/dev/vdb || echo "E2E BOOT FAIL: insmod ps3guard_sim"
else
	mknod /dev/ps3db b $(tr : ' ' < /sys/class/block/vda/dev)
	mknod /dev/ps3da b $(tr : ' ' < /sys/class/block/vdb/dev)
fi
# a third disk is dev_hdd1 (the internal install's tests): /dev/ps3dc
[ -e /sys/class/block/vdc/dev ] && mknod /dev/ps3dc b $(tr : ' ' < /sys/class/block/vdc/dev)
umount /sys
umount /proc
exec /init
