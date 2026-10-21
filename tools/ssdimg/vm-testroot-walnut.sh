#!/bin/sh
# /sbin/init of vm-e2e.sh's internal-install test root (the rootfs tarball the generic initramfs
# unpacks on its first boot, docs/release/ALPHA.md M1): what the install left behind, the guard's
# seal, a write on the root and stray writes to ps3db refused. Prints "E2E BOOT PASS" and powers off.
export PATH=/bin
f=0
ok() { echo "ok - boot: $*"; }
no() { echo "FAIL - boot: $*"; f=$((f + 1)); }
r=$(awk '$2 == "/" { print $1, $3, substr($4, 1, 2) }' /proc/mounts | tail -n 1)
[ "$r" = "/dev/mapper/ps3root ext4 ro" ] && ok "root is /dev/mapper/ps3root, ext4, read-only" || no "root: $r"
[ "$(blockdev --getsz /dev/mapper/ps3swap)" = 32768 ] && ok "/dev/mapper/ps3swap, 16 MiB (the manifest's)" || no "swap"
G=/sys/block/ps3db/write_guard
[ -d $G ] && [ "$(cat $G/policy) $(cat $G/sealed)" = "whitelist 1" ] && ok "guard: ps3db whitelist, sealed" ||
	no "guard: not sealed"
want=$(( $(blockdev --getsz /dev/mapper/ps3root) + $(blockdev --getsz /dev/mapper/ps3swap) ))
[ "$(cat $G/nr_sectors)" = $want ] && ok "guard: $want sectors, ps3root and ps3swap" || no "guard: nr_sectors $(cat $G/nr_sectors)"
grep -q ' ufs ' /proc/mounts && no "dev_hdd0 still mounted" || ok "dev_hdd0 not mounted"
grep -q ' vfat ' /proc/mounts && no "dev_hdd1 still mounted" || ok "dev_hdd1 not mounted"
set -- $(stat -L -c '%t %T' /dev/ps3dc)
[ "$(cat /sys/dev/block/$((0x$1)):$((0x$2))/ro)" = 1 ] && ok "dev_hdd1 read-only again" || no "dev_hdd1 left writable"
grep -q '^/dev/mapper/ps3root  *\/  *ext4' /etc/fstab && ok "fstab: the install's root" || no "fstab"
grep -q '^PS3_BOOT_TARGET=hdd1$' /etc/default/ps3-boot && ok "ps3-boot: PS3_BOOT_TARGET=hdd1" || no "ps3-boot target"
grep -q 'ps3ssd.image=/linux/walnut.img' /etc/default/ps3-boot && ok "ps3-boot: the image's root args" || no "ps3-boot args"
[ -e /etc/fstab.image ] && ok "the image's own fstab kept as fstab.image" || no "fstab.image"
[ -L /etc/systemd/system/sysinit.target.wants/ps3ssd-udev.service ] && ok "ps3ssd-udev.service enabled" || no "udev unit"
[ -e /etc/walnut-testroot ] && ok "the tarball's files are there" || no "tarball contents"
[ -e /root-was-installed ] && no "a file from before the install survived" || ok "a fresh file system"
mount -o remount,rw / && dd if=/dev/urandom of=/written bs=1M count=3 2>/dev/null && sync && mount -o remount,ro / &&
	ok "3 MiB written on the root" || no "writes"
[ "$(cat $G/refused)" = 0 ] && ok "guard: the root's writes all inside" || no "guard: refused $(cat $G/refused)"
mount -t tmpfs tmpfs /tmp
if dd if=/dev/ps3db of=/tmp/blk bs=512 count=8 2>/dev/null && dd if=/tmp/blk of=/dev/ps3db bs=512 count=8 conv=notrunc,fsync 2>/dev/null; then
	no "guard: a write to ps3db sector 0 went through"
else ok "guard: a write to ps3db sector 0 refused"; fi
[ $f = 0 ] && echo "E2E BOOT PASS" || echo "E2E BOOT FAIL ($f)"
sync
poweroff -f
