#!/bin/sh
# /sbin/init of vm-e2e.sh's test root (the image ssd-install-image.sh installs on /dev/mapper/ps3ssd in
# the KVM guest): what the initramfs and the install left behind, then a write on the root.
# Prints "E2E BOOT PASS" and powers off.
export PATH=/bin
f=0
ok() { echo "ok - boot: $*"; }
no() { echo "FAIL - boot: $*"; f=$((f + 1)); }
r=$(awk '$2 == "/" { print $1, $3, substr($4, 1, 2) }' /proc/mounts | tail -n 1)
[ "$r" = "/dev/mapper/ps3root ext4 ro" ] && ok "root is /dev/mapper/ps3root, ext4, read-only" || no "root: $r"
[ "$(blockdev --getsz /dev/mapper/ps3swap)" = 4096 ] && ok "/dev/mapper/ps3swap, 2 MiB" || no "swap"
[ "$(cat /sys/class/block/vdb/ro)" = 1 ] && ok "ps3da read-only" || no "ps3da ro"
grep -q ' ufs ' /proc/mounts && no "dev_hdd0 still mounted" || ok "dev_hdd0 not mounted"
grep -q 'starting /sbin/init' /run/ps3ssd/init.log && ok "the initramfs's log in /run/ps3ssd" || no "log"
[ -e /run/ps3ssd/ps3root.table ] && [ -e /run/ps3ssd/ps3swap.table ] && ok "the table records in /run/ps3ssd" || no "table records"
[ -c /dev/console ] && [ -b /dev/ps3db ] && ok "/dev moved" || no "/dev"
# the install's fixups
grep -q '^/dev/mapper/ps3root  *\/  *ext4  *defaults,noatime  *0 1$' /etc/fstab && ok "fstab: root" || no "fstab root"
grep -q '^/dev/mapper/ps3swap  *none  *swap  *sw,pri=200,noauto' /etc/fstab && ok "fstab: the VRAM swap's backing" || no "fstab swap"
grep -q 'PARTUUID=50533301-01' /etc/fstab && no "fstab has the USB swap" || ok "fstab: no USB swap"
[ "$(cat /etc/hostname)" = ps3-e2e ] && ok "hostname copied" || no "hostname"
[ -e /etc/ssh/ssh_host_e2e_key ] && ok "SSH host key copied" || no "host key"
[ -e /var/tmp/bench/run.sh ] && [ -e /var/tmp/ps3-bench/shadercache/x ] && ok "bench tree and state copied" || no "bench"
[ -e /var/tmp/ps3-bench/20261019-0100-sauerbraten-base ] && no "a bench run directory was copied" || ok "no run directories"
[ -e /var/lib/ps3-imgmap ] && ok "ps3-imgmap state copied" || no "state"
[ -e /etc/systemd/system/ps3ssd-udev.service ] && [ -L /etc/systemd/system/sysinit.target.wants/ps3ssd-udev.service ] &&
	ok "ps3ssd-udev.service enabled" || no "udev unit"
[ -d /mnt/usbroot ] && ok "/mnt/usbroot" || no "/mnt/usbroot"
mount -o remount,rw / && dd if=/dev/urandom of=/written bs=1M count=3 2>/dev/null && sync && mount -o remount,ro / &&
	ok "3 MiB written on the root" || no "writes"
[ $f = 0 ] && echo "E2E BOOT PASS" || echo "E2E BOOT FAIL ($f)"
sync
poweroff -f
