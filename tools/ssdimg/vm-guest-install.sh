#!/bin/bash
# vm-guest-install.sh: ssd-install-image.sh in vm-e2e.sh's KVM guest (the host's / read-only, chrooted;
# /tmp tmpfs; /tmp/out the host's $W/out, writable, holding the fake USB root usbroot/). As on the PS3:
# ps3-imgmap map, verify, recheck, create ps3ssd --swap-mib 2 (the disks under the PS3's names), then
# check, install (a dry run first, the refusals, a real one, a --reuse one) and boot, which writes the
# initramfs and /etc/kboot.d/ssd.conf into the fake USB root and runs ps3-update-kboot there.
# With e2e_guard=1 the disks are ps3guard_sim's (the kernel guard of local/0045): create registers and
# seals, check reports the seal, and the install's mkfs and copy go through the guard.
# Prints "E2E I PASS" when everything held.
set -u
arg() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" /proc/cmdline; }
ROOT=$(arg e2e_root); W=$(arg e2e_work)
T=$ROOT/tools/ssdimg
OUT=/tmp/out
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PS3_IMGMAP_STATE=/tmp/state PS3_IMGMAP_SYSBLOCK=/tmp/sysb
export PS3_IMGMAP_DMSETUP="dmsetup --noudevsync" DM_DISABLE_UDEV=1 PYTHONDONTWRITEBYTECODE=1
export PS3SSD_TEST_ROOT=$OUT/usbroot PS3SSD_STATE=/tmp/state PS3SSD_UPDATE_KBOOT=$ROOT/ps3-boot/ps3-update-kboot
fails=0
ok() { echo "ok - $*"; }
bad() { echo "FAIL - $*"; fails=$((fails + 1)); }
expect() {	# expect RC WHAT CMD...: CMD's status must be RC
	local want=$1 what=$2; shift 2
	"$@" > /tmp/o 2>&1 < /dev/null; local rc=$?
	sed 's/^/     /' /tmp/o
	[ $rc = "$want" ] && ok "$what" || bad "$what (status $rc, wanted $want)"
}
IM="python3 -I $T/ps3-imgmap"
SI="bash $T/ssd-install-image.sh"
MAP=/tmp/state/ssdtest.map.json
TAR=$W/testroot.tar
FROM=$W/from

GUARD=$(arg e2e_guard)
if [ "$GUARD" = 1 ]; then
	insmod $W/guardsim/ps3guard_sim.ko db=/dev/vda da=/dev/vdb || { echo "E2E I FAIL: insmod ps3guard_sim"; exit 1; }
	mkdir -p /tmp/sysb && ln -s /sys/class/block/ps3da /tmp/sysb/ps3da && ln -s /sys/class/block/ps3db /tmp/sysb/ps3db
else
	for p in "vda ps3db" "vdb ps3da"; do
		set -- $p
		mknod /dev/$2 b $(tr : ' ' < /sys/class/block/$1/dev) || { echo "E2E I FAIL: mknod $2"; exit 1; }
	done
	mkdir -p /tmp/sysb && ln -s /sys/class/block/vdb /tmp/sysb/ps3da
fi
mount -t ufs -o ro,ufstype=ufs2 /dev/ps3db /tmp/mnt || { echo "E2E I FAIL: mount"; exit 1; }
expect 0 "map, verify, recheck" sh -c "$IM map /tmp/mnt/linux/ssdtest.img && $IM verify $MAP && $IM recheck $MAP"
umount /tmp/mnt
expect 1 "check refuses before create" $SI check --from $FROM
expect 0 "ps3-imgmap create ps3ssd --swap-mib 2" $IM create $MAP ps3ssd --yes --swap-mib 2
if [ "$GUARD" = 1 ]; then
	grep -q "write guard sealed" /tmp/o && ok "  (it registered and sealed ps3db's ranges)" || bad "no seal"
else
	grep -q "kernel has no write guard" /tmp/o && ok "  (it said the kernel has no guard)" || bad "no guard line"
fi
expect 0 "ssd-install-image.sh check" $SI check --from $FROM
grep -q "makes the same two devices" /tmp/o && ok "  (the boot map's tables are create's)" || bad "check output"
if [ "$GUARD" = 1 ]; then
	grep -q "write guard sealed: .* exactly ps3ssd and ps3ssd-swap's" /tmp/o && ok "  (and the sealed guard is theirs)" || bad "check: guard"
else
	grep -q "kernel without the write guard" /tmp/o && ok "  (and said there is no guard)" || bad "check: no guard line"
fi

expect 0 "install --dry-run" $SI install --from $FROM --tar $TAR --no-home --dry-run
[ -z "$(blkid -o value -s TYPE /dev/mapper/ps3ssd)" ] && ok "  (the dry run wrote nothing)" || bad "dry run wrote"
mkfs.ext4 -q -F -L ps3ssd /dev/mapper/ps3ssd	# Phase A's game copies stand in
expect 1 "install asks before erasing Phase A's file system (no terminal: refused)" $SI install --from $FROM --tar $TAR --no-home
grep -q "Phase A's game copies" /tmp/o && ok "  (it said what is there)" || bad "question"
mkdir -p /tmp/m && mount /dev/mapper/ps3ssd /tmp/m
expect 1 "install refuses while ps3ssd is mounted" $SI install --from $FROM --tar $TAR --no-home --yes
umount /tmp/m
expect 1 "--reuse refuses a file system that isn't ps3root" $SI install --from $FROM --tar $TAR --no-home --reuse --yes
expect 0 "install --yes (mkfs, extract, fix up)" $SI install --from $FROM --tar $TAR --yes
[ "$(blkid -o value -s LABEL /dev/mapper/ps3ssd)" = ps3root ] && ok "  ext4 labelled ps3root" || bad "label"
expect 0 "install --reuse --yes (the old root moved aside)" $SI install --from $FROM --tar $TAR --reuse --yes
mount -o ro /dev/mapper/ps3ssd /tmp/m
ls -d /tmp/m/.old-* >/dev/null 2>&1 && [ -e /tmp/m/sbin/init ] && ok "  .old-* and the new root" || bad "reuse layout"
[ -e /tmp/m/home/tibi/.config/e2e ] && ok "  /home/tibi copied" || bad "home"
umount /tmp/m

expect 0 "boot (initramfs and the entry on the USB root, ps3-update-kboot)" $SI boot --from $FROM
K=$OUT/usbroot/etc/kboot.conf
grep -q "^ssd-$(uname -r)='/boot/vmlinux-$(uname -r) initrd=/boot/initrd-ssd-$(uname -r).img root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=/linux/ssdtest.img" $K &&
	ok "the ssd entry in kboot.conf" || { bad "kboot.conf"; cat $K; }
grep -q "^$(uname -r)='/boot/vmlinux-$(uname -r) root=PARTUUID=50533301-02" $K && ok "the USB entry unchanged" || bad "USB entry"
cmp -s $OUT/usbroot/boot/initrd-ssd-$(uname -r).img $FROM/initrd-ssd-$(uname -r)-amd64.img && ok "the initramfs in /boot" || bad "initrd copy"
if [ "$GUARD" = 1 ]; then
	[ "$(cat /sys/block/ps3db/write_guard/refused)" = 0 ] && ok "nothing refused by the guard (mkfs, tar, the fix-ups)" ||
		bad "the guard refused $(cat /sys/block/ps3db/write_guard/refused) writes"
fi
expect 0 "remove" $IM remove ps3ssd
[ $fails = 0 ] && echo "E2E I PASS" || echo "E2E I FAIL ($fails)"
