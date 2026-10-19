#!/bin/bash
# vm-guest-b.sh: Phase B's preparation inside vm-e2e.sh's KVM guest (the host's / read-only, chrooted;
# /tmp tmpfs; /tmp/out the host's $W/out, writable). Disks: vda the UFS2 image, vdb the whole-disk
# stand-in, given the PS3's names here as /dev/ps3db and /dev/ps3da nodes, so ps3-imgmap and
# ps3-imgmap-boot run without any test seam for the devices. Checks that the C tool makes exactly the
# devices ps3-imgmap create makes (the kernel's tables compared) and its refusals; leaves the boot map
# in /tmp/out for the initramfs. Then the tool's guard model build (-DPS3_GUARD_FAKE, the kernel
# guard's side modelled in the program) on the real device-mapper: it registers exactly the kernel's
# tables, and a registration that fails removes the devices again. Reads the disk only. Prints
# "E2E B PASS" when everything held.
set -u
arg() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" /proc/cmdline; }
ROOT=$(arg e2e_root); W=$(arg e2e_work)
T=$ROOT/tools/ssdimg
OUT=/tmp/out
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PS3_IMGMAP_STATE=/tmp/state PS3_IMGMAP_SYSBLOCK=/tmp/sysb
export PS3_IMGMAP_DMSETUP="dmsetup --noudevsync" DM_DISABLE_UDEV=1 PYTHONDONTWRITEBYTECODE=1
fails=0
ok() { echo "ok - $*"; }
bad() { echo "FAIL - $*"; fails=$((fails + 1)); }
expect() {	# expect RC WHAT CMD...: CMD's status must be RC
	local want=$1 what=$2; shift 2
	"$@" > /tmp/o 2>&1; local rc=$?
	sed 's/^/     /' /tmp/o
	[ $rc = "$want" ] && ok "$what" || bad "$what (status $rc, wanted $want)"
}
IM="python3 -I $T/ps3-imgmap"
B=$W/pib-amd64		# ps3-imgmap-boot, the static amd64 build of build-initrd.sh
MNT=/tmp/mnt
MAP=/tmp/state/ssdtest.map.json
BM=$OUT/ssdtest.map

for p in "vda ps3db" "vdb ps3da"; do
	set -- $p
	mknod /dev/$2 b $(tr : ' ' < /sys/class/block/$1/dev) || { echo "E2E B FAIL: mknod $2"; exit 1; }
done
mkdir -p /tmp/sysb /tmp/run && ln -s /sys/class/block/vdb /tmp/sysb/ps3da

mount -t ufs -o ro,ufstype=ufs2 /dev/ps3db $MNT || { echo "E2E B FAIL: mount"; exit 1; }
expect 0 "ps3-imgmap map (as on the PS3: /dev/ps3db)" $IM map $MNT/linux/ssdtest.img
expect 0 "verify" $IM verify $MAP
expect 0 "recheck" $IM recheck $MAP
umount $MNT
expect 0 "ps3-imgmap create ps3ssd --swap-mib 2" $IM create $MAP ps3ssd --yes --swap-mib 2
dmsetup table ps3ssd > $OUT/py-root.table && dmsetup table ps3ssd-swap > $OUT/py-swap.table
sz_root=$(blockdev --getsz /dev/mapper/ps3ssd); sz_swap=$(blockdev --getsz /dev/mapper/ps3ssd-swap)
expect 0 "export-boot (swap size from the created record)" $IM export-boot $MAP --out $BM
cp $MAP $OUT/
expect 0 "ps3-imgmap-boot check" $B check $BM
$B tables $BM root > /tmp/c-root && $B tables $BM swap > /tmp/c-swap
cmp -s /tmp/c-root /tmp/state/ps3ssd.table && cmp -s /tmp/c-swap /tmp/state/ps3ssd-swap.table &&
	ok "the C tables are ps3-imgmap create's, line for line" || bad "tables differ"
expect 0 "ps3-imgmap remove" $IM remove ps3ssd

# the C tool's own path: refusals first
mkdir -p /tmp/fake/linux
mount -t ufs -o ro,ufstype=ufs2 /dev/ps3db $MNT
cp $MNT/linux/ssdtest.img /tmp/fake/linux/ssdtest.img
expect 2 "recheck refuses the file on another file system (tmpfs)" $B recheck $BM /tmp/fake --run /tmp/run
grep -q "not GameOS's UFS2" /tmp/o && ok "  (because it isn't UFS2)" || bad "reason"
rm -rf /tmp/fake
expect 2 "create refuses without a recheck in this boot" $B create $BM --run /tmp/run
grep -q "no recheck in this boot" /tmp/o && ok "  (no recheck)" || bad "reason"
t0=$(date +%s.%N)
expect 0 "ps3-imgmap-boot recheck (FIBMAP, the real ufs driver)" $B recheck $BM $MNT --run /tmp/run
expect 2 "create refuses while ps3db is mounted" $B create $BM --run /tmp/run
grep -q "is mounted" /tmp/o && ok "  (mounted)" || bad "reason"
umount $MNT
cp /tmp/run/rechecked /tmp/rechecked.good
echo "$(cut -d' ' -f1 /tmp/rechecked.good) 00000000-0000-0000-0000-000000000000" > /tmp/run/rechecked
expect 2 "create refuses a recheck of another boot" $B create $BM --run /tmp/run
grep -q "another map or boot" /tmp/o && ok "  (another boot)" || bad "reason"
cp /tmp/rechecked.good /tmp/run/rechecked
blockdev --setrw /dev/ps3da	# ps3-imgmap create set it: the C tool must set it itself
expect 0 "ps3-imgmap-boot create (dm ioctls, BLKROSET, header stamps read back)" $B create $BM --run /tmp/run
[ "$(cat /sys/class/block/vdb/ro)" = 1 ] && ok "the whole-disk stand-in is read-only" || bad "setro"
[ -b /dev/mapper/ps3root ] && [ -b /dev/mapper/ps3swap ] && ok "/dev/mapper/ps3root and ps3swap" || bad "nodes"
dmsetup table ps3root | cmp -s - $OUT/py-root.table && dmsetup table ps3swap | cmp -s - $OUT/py-swap.table &&
	ok "the kernel's tables equal those of ps3-imgmap create (dmsetup table)" || { bad "kernel tables differ"; dmsetup table ps3root | head -3; head -3 $OUT/py-root.table; }
[ "$(blockdev --getsz /dev/mapper/ps3root)" = "$sz_root" ] && [ "$(blockdev --getsz /dev/mapper/ps3swap)" = "$sz_swap" ] &&
	ok "the same sizes ($sz_root, $sz_swap sectors)" || bad "sizes"
cmp -s /tmp/run/ps3root.table /tmp/state/ps3ssd.table && ok "its table record is create's" || bad "table record"
expect 2 "create refuses when the devices exist" $B create $BM --run /tmp/run

dmsetup --noudevsync remove ps3swap && dmsetup --noudevsync remove ps3root && ok "removed" || bad "remove"

F=$W/pib-amd64-fake	# the guard model build (vm-e2e.sh)
expect 0 "guard model: create registers and seals" env PS3_GUARD_FAKE= PS3_GUARD_FAKE_LOG=/tmp/glog $F create $BM --run /tmp/run --guard required
grep -q "write guard sealed" /tmp/o && ok "  (sealed)" || bad "no seal"
grep -v -e '^ranges ' -e '^sealed ' /tmp/glog > /tmp/glog.ranges
dmsetup table ps3root | awk '{print $5, $2}' > /tmp/live.ranges && dmsetup table ps3swap | awk '{print $5, $2}' >> /tmp/live.ranges
cmp -s /tmp/glog.ranges /tmp/live.ranges && ok "  it registered the device side of the kernel's tables ($(wc -l < /tmp/live.ranges) ranges), nothing else" ||
	{ bad "registered ranges differ"; head -3 /tmp/glog.ranges /tmp/live.ranges; }
[ "$(tail -n 1 /tmp/glog)" = "sealed 1" ] && ok "  then the seal" || bad "no seal write"
dmsetup --noudevsync remove ps3swap && dmsetup --noudevsync remove ps3root || bad "remove"
for fault in "short=1:a short count" "sealfail:writing 1 got" "nr_sectors+=8:our targets" "sealed:sealed earlier in this boot" "policy=writable:not whitelist"; do
	expect 2 "guard model: create refused (${fault%%:*})" env PS3_GUARD_FAKE="${fault%%:*}" $F create $BM --run /tmp/run
	grep -q "${fault#*:}" /tmp/o && ok "  (${fault#*:})" || bad "reason"
	[ ! -e /dev/mapper/ps3root ] && [ ! -e /dev/mapper/ps3swap ] && ! dmsetup info ps3root >/dev/null 2>&1 &&
		! dmsetup info ps3swap >/dev/null 2>&1 && ok "  no device left" || bad "devices left"
done
[ $fails = 0 ] && echo "E2E B PASS" || echo "E2E B FAIL ($fails)"
