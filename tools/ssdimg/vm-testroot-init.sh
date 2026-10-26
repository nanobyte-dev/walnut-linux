#!/bin/sh
# /sbin/init of vm-e2e.sh's test root (the image ssd-install-image.sh installs on /dev/mapper/ps3ssd in
# the KVM guest): what the initramfs and the install left behind, then a write on the root; with the
# kernel guard (ps3guard_sim's /sys/block/ps3db/write_guard) also its seal, the root's writes all
# inside it, and stray writes to ps3db refused (the data already there written back, so even a
# broken guard changes nothing). Prints "E2E BOOT PASS" and powers off.
export PATH=/bin
f=0
ok() { echo "ok - boot: $*"; }
no() { echo "FAIL - boot: $*"; f=$((f + 1)); }
r=$(awk '$2 == "/" { print $1, $3, substr($4, 1, 2) }' /proc/mounts | tail -n 1)
[ "$r" = "/dev/mapper/ps3root ext4 ro" ] && ok "root is /dev/mapper/ps3root, ext4, read-only" || no "root: $r"
[ "$(blockdev --getsz /dev/mapper/ps3swap)" = 4096 ] && ok "/dev/mapper/ps3swap, 2 MiB" || no "swap"
set -- $(stat -L -c '%t %T' /dev/ps3da)
[ "$(cat /sys/dev/block/$((0x$1)):$((0x$2))/ro)" = 1 ] && ok "ps3da read-only" || no "ps3da ro"
G=/sys/block/ps3db/write_guard
if [ -d $G ]; then
	grep -q 'write guard sealed' /run/ps3ssd/init.log && ok "guard: the initramfs sealed it" || no "guard: no seal in the log"
	[ "$(cat $G/policy) $(cat $G/sealed)" = "whitelist 1" ] && ok "guard: ps3db whitelist, sealed" || no "guard: $(cat $G/policy) $(cat $G/sealed)"
	want=$(( $(blockdev --getsz /dev/mapper/ps3root) + $(blockdev --getsz /dev/mapper/ps3swap) ))
	[ "$(cat $G/nr_sectors)" = $want ] && ok "guard: $(cat $G/nr_ranges) ranges, $want sectors: ps3root and ps3swap" ||
		no "guard: nr_sectors $(cat $G/nr_sectors), the devices have $want"
	[ "$(cat $G/refused)" = 0 ] && ok "guard: nothing refused so far" || no "guard: refused $(cat $G/refused) before the test"
fi
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
[ -e /var/tmp/bench/harness/run.sh ] && [ -e /var/tmp/ps3-bench/shadercache/x ] && ok "bench tree and state copied" || no "bench"
[ -e /var/tmp/ps3-bench/20261019-0100-sauerbraten-base ] && no "a bench run directory was copied" || ok "no run directories"
[ -e /var/lib/ps3-imgmap ] && ok "ps3-imgmap state copied" || no "state"
[ -e /etc/systemd/system/ps3ssd-udev.service ] && [ -L /etc/systemd/system/sysinit.target.wants/ps3ssd-udev.service ] &&
	ok "ps3ssd-udev.service enabled" || no "udev unit"
[ -d /mnt/usbroot ] && ok "/mnt/usbroot" || no "/mnt/usbroot"
mount -o remount,rw / && dd if=/dev/urandom of=/written bs=1M count=3 2>/dev/null && sync && mount -o remount,ro / &&
	ok "3 MiB written on the root" || no "writes"
if [ -d $G ]; then
	[ "$(cat $G/refused)" = 0 ] && ok "guard: the root's writes were all inside (refused 0)" || no "guard: refused $(cat $G/refused)"
	# sector 0 (UFS, outside the image) and the header area's last block before the root's first target
	off=$(awk 'NR == 1 { print $5 }' /run/ps3ssd/ps3root.table)
	inside=$(awk -v s=$((off - 8)) '$5 <= s && s < $5 + $2 { print "yes" }' /run/ps3ssd/ps3root.table /run/ps3ssd/ps3swap.table)
	n=0
	mount -t tmpfs tmpfs /tmp	# the root is read-only and this init isn't systemd: no fstab
	for sec in 0 $((off - 8)); do
		[ "$sec" = 0 ] || [ -z "$inside" ] || continue
		# the data that is there, written back; busybox's dd falls back from O_DIRECT to the page cache
		# (its buffer isn't aligned), so fsync: the writeback's EIO
		if ! dd if=/dev/ps3db of=/tmp/blk bs=512 skip=$sec count=8 2>/dev/null; then
			no "guard: can't read ps3db sector $sec"
		elif dd if=/tmp/blk of=/dev/ps3db bs=512 seek=$sec count=8 conv=notrunc,fsync 2>/dev/null; then
			no "guard: a write to ps3db sector $sec went through"
		else ok "guard: a write to ps3db sector $sec refused"; n=$((n + 1)); fi
	done
	# the page cache writes 512-byte or 4 KiB blocks, so one refused dd is one or more refused bios
	[ $n -ge 1 ] && [ "$(cat $G/refused)" -ge $n ] && ok "guard: refused counts them ($(cat $G/refused) bios)" ||
		no "guard: refused $(cat $G/refused) after $n refused writes"
	echo "0 8" > $G/ranges 2>/dev/null && no "guard: ranges taken after the seal" || ok "guard: ranges refused after the seal"
	blockdev --setrw /dev/ps3da 2>/dev/null && no "guard: ps3da made writable" || ok "guard: ps3da stays read-only (BLKROSET 0 refused)"
fi
[ $f = 0 ] && echo "E2E BOOT PASS" || echo "E2E BOOT FAIL ($f)"
sync
poweroff -f
