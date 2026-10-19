#!/bin/bash
# vm-guest-guard.sh: the kernel guard (local/0045) in vm-e2e.sh's KVM guest (the host's / read-only,
# chrooted; /tmp tmpfs; /tmp/out the host's $W/out, with vm-guest-b.sh's boot map). ps3guard_sim gives
# the disks the PS3's names, ps3da over vdb and ps3db over vda, with the patch's own guard code and
# /sys/block/ps3dX/write_guard. The guard is sealed once per boot (the module stays pinned), so:
#   e2e_part=1  Phase A behind the guard: the files and policies before anything, a write refused
#               before the seal, ps3-imgmap map/verify/recheck/create (registers and seals), ext4
#               and swap on its devices, stray writes refused after the seal (the data already there,
#               written back), sysfs refusing ranges and sealed, then ps3-imgmap-boot create refused
#               (sealed earlier in this boot) without making anything
#   e2e_part=2  sysfs's edges with no image involved: malformed writes, a write cut at 4 KiB (the
#               short count), ps3-imgmap-boot create refused while ranges are pending, the seal's
#               merge, a write just inside and just outside the sealed range (data written back)
# Prints "E2E G1 PASS" / "E2E G2 PASS" when everything held.
set -u
arg() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" /proc/cmdline; }
ROOT=$(arg e2e_root); W=$(arg e2e_work); PART=$(arg e2e_part)
T=$ROOT/tools/ssdimg
OUT=/tmp/out
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PS3_IMGMAP_STATE=/tmp/state DM_DISABLE_UDEV=1 PYTHONDONTWRITEBYTECODE=1
export PS3_IMGMAP_DMSETUP="dmsetup --noudevsync"
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
B=$W/pib-amd64
BM=$OUT/ssdtest.map
MAP=/tmp/state/ssdtest.map.json
G=/sys/block/ps3db/write_guard
MNT=/tmp/mnt
mkdir -p $MNT /tmp/run
refused() { cat $G/refused; }
# rewrite SECTOR [COUNT]: the 512-byte sectors there (default 8) read and written back with O_DIRECT,
# one bio of exactly them (status of the write)
rewrite() {
	dd if=/dev/ps3db of=/tmp/blk bs=512 skip=$1 count=${2:-8} iflag=direct status=none &&
		dd if=/tmp/blk of=/dev/ps3db bs=512 seek=$1 count=${2:-8} oflag=direct conv=notrunc status=none 2>/dev/null
}
# put TEXT FILE: TEXT written with a single write(2); prints the count or the error
put() { python3 -I -c 'import os, sys
fd = os.open(sys.argv[2], os.O_WRONLY)
try:
    print(os.write(fd, sys.argv[1].encode()))
except OSError as e:
    print(e.strerror)' "$1" "$2"; }

insmod $W/guardsim/ps3guard_sim.ko db=/dev/vda da=/dev/vdb || { echo "E2E G$PART FAIL: insmod"; exit 1; }
[ -b /dev/ps3da ] && [ -b /dev/ps3db ] && ok "ps3guard_sim: /dev/ps3da and /dev/ps3db" || bad "no disks"

if [ "$PART" = 1 ]; then
	[ "$(ls $G | xargs)" = "nr_ranges nr_sectors policy ranges refused sealed" ] && ok "ps3db's write_guard/ files" || bad "files: $(ls $G | xargs)"
	[ "$(ls /sys/block/ps3da/write_guard | xargs)" = "policy refused" ] && ok "ps3da's: policy, refused" || bad "ps3da's files"
	[ "$(cat $G/policy) $(cat $G/sealed) $(cat $G/nr_ranges) $(cat $G/nr_sectors) $(refused)" = "whitelist 0 0 0 0" ] &&
		ok "ps3db: whitelist, unsealed, empty" || bad "ps3db state"
	[ "$(cat /sys/block/ps3da/write_guard/policy) $(cat /sys/block/ps3da/ro)" = "read-only 1" ] && ok "ps3da: read-only from the start" || bad "ps3da"
	blockdev --setrw /dev/ps3da 2>/dev/null && bad "BLKROSET 0 on ps3da went through" || ok "BLKROSET 0 on ps3da refused"
	blockdev --setro /dev/ps3da && ok "BLKROSET 1 on ps3da still fine" || bad "setro"
	rewrite 0 && bad "a write to ps3db before the seal went through" || ok "a write to ps3db before the seal: refused (EIO)"
	[ "$(refused)" = 1 ] && ok "  (refused 1)" || bad "refused $(refused), want 1"
	r0=$(refused)

	mount -t ufs -o ro,ufstype=ufs2 /dev/ps3db $MNT || { echo "E2E G1 FAIL: mount"; exit 1; }
	expect 0 "map, verify, recheck (Python) on the guarded ps3db" sh -c "$IM map $MNT/linux/ssdtest.img && $IM verify $MAP && $IM recheck $MAP"
	expect 0 "ps3-imgmap-boot recheck" $B recheck $BM $MNT --run /tmp/run
	umount $MNT
	expect 0 "ps3-imgmap create ps3ssd --swap-mib 2: registers and seals" $IM create $MAP ps3ssd --yes --swap-mib 2
	grep -q "write guard sealed" /tmp/o && ok "  (sealed)" || bad "no seal"
	want=$(python3 -I -c 'import sys
r = sorted((int(l.split()[4]), int(l.split()[1])) for f in sys.argv[1:] for l in open(f) if l.strip())
m = []
for a, n in r:
    if m and a <= m[-1][1]: m[-1][1] = max(m[-1][1], a + n)
    else: m.append([a, a + n])
print(len(m), sum(b - a for a, b in m))' /tmp/state/ps3ssd.table /tmp/state/ps3ssd-swap.table)
	[ "$(cat $G/sealed) $(cat $G/nr_ranges) $(cat $G/nr_sectors)" = "1 $want" ] && ok "sealed: $want (ranges, sectors) = the two tables, merged" ||
		bad "sealed state $(cat $G/sealed) $(cat $G/nr_ranges) $(cat $G/nr_sectors), want 1 $want"
	expect 0 "ext4 on /dev/mapper/ps3ssd behind the guard: mkfs, mount, 4 MiB written, unmount" sh -c "
		mkfs.ext4 -q -F -E lazy_itable_init=0,lazy_journal_init=0 /dev/mapper/ps3ssd && mkdir -p /tmp/e && mount /dev/mapper/ps3ssd /tmp/e &&
		dd if=/dev/urandom of=/tmp/e/x bs=1M count=4 status=none && sync && umount /tmp/e && fsck.ext4 -fn /dev/mapper/ps3ssd"
	expect 0 "swap on /dev/mapper/ps3ssd-swap: mkswap, 1 MiB written through it" sh -c "
		mkswap /dev/mapper/ps3ssd-swap && dd if=/dev/urandom of=/dev/mapper/ps3ssd-swap bs=1M count=1 oflag=direct status=none"
	[ "$(refused)" = "$r0" ] && ok "  (nothing refused: every write inside)" || bad "refused $(refused), was $r0"
	hdr=$(python3 -I -c 'import json, sys
m = json.load(open(sys.argv[1]))
print(next(p + 40 - l for l, p, n in m["extents"] if l <= 40 < l + n))' $MAP)
	for sec in 0 $hdr; do
		rewrite $sec && bad "a write to ps3db sector $sec went through" || ok "a write to ps3db sector $sec (outside the ranges) refused"
	done
	rewrite $(awk 'NR == 1 { print $5 }' /tmp/state/ps3ssd.table) && ok "a write to the first registered sector (straight to ps3db) went through" ||
		bad "a write inside the ranges refused"
	[ "$(refused)" = $((r0 + 2)) ] && ok "  (refused counted the two: $(refused))" || bad "refused $(refused), want $((r0 + 2))"
	[ "$(put '0 8
' $G/ranges)" = "Operation not permitted" ] && ok "ranges after the seal: EPERM" || bad "ranges after the seal"
	[ "$(put '1' $G/sealed)" = "Operation not permitted" ] && ok "sealed again: EPERM" || bad "sealed again"
	[ "$(put '0' $G/sealed)" = "Invalid argument" ] && ok "sealed 0: EINVAL" || bad "sealed 0"
	expect 2 "ps3-imgmap-boot create refused: sealed earlier in this boot" $B create $BM --run /tmp/run
	grep -q "sealed earlier in this boot" /tmp/o && ok "  (the reason)" || bad "reason"
	[ ! -e /dev/mapper/ps3root ] && ! dmsetup info ps3root >/dev/null 2>&1 && ok "  (nothing made)" || bad "devices made"
	expect 0 "ps3-imgmap remove" $IM remove ps3ssd
	rmmod ps3guard_sim 2>/dev/null && bad "the sealed module could be unloaded" || ok "the sealed module stays loaded (nothing else holds it)"
fi

if [ "$PART" = 2 ]; then
	n() { cat $G/nr_ranges; }
	for bad_line in "100 8" "abc
" "0 0
" "100 -8
" "99999999999999 8
" "0x10 8
"; do
		r=$(put "$bad_line" $G/ranges)
		[ "$r" = "Invalid argument" ] && [ "$(n)" = 0 ] && ok "ranges refuses $(printf %q "$bad_line"): EINVAL, nothing taken" ||
			bad "ranges $(printf %q "$bad_line"): $r, nr_ranges $(n)"
	done
	# 512 lines of 8 bytes are 4096 bytes; one more line: sysfs passes the first 4096, a short count
	text=$(python3 -I -c 'import sys; sys.stdout.write("10000 8\n" * 512 + "1 8\n")')
	r=$(put "$text
" $G/ranges)
	[ "$r" = 4096 ] && [ "$(n)" = 512 ] && ok "a 4100-byte write: 4096 taken (a short count), 512 ranges" || bad "4100-byte write: $r, nr_ranges $(n)"
	# cut inside a line: 511 lines and one of 10 bytes, 4098 in all
	text=$(python3 -I -c 'import sys; sys.stdout.write("10000 8\n" * 511 + "123456789")')
	r=$(put "$text
" $G/ranges)
	[ "$r" = "Invalid argument" ] && [ "$(n)" = 512 ] && ok "a write cut inside a line: EINVAL, nothing taken" || bad "cut write: $r, nr_ranges $(n)"
	mount -t ufs -o ro,ufstype=ufs2 /dev/ps3db $MNT && expect 0 "ps3-imgmap-boot recheck" $B recheck $BM $MNT --run /tmp/run
	umount $MNT
	expect 2 "ps3-imgmap-boot create refused: ranges pending" $B create $BM --run /tmp/run
	grep -q "already registered" /tmp/o && ok "  (the reason)" || bad "reason"
	[ ! -e /dev/mapper/ps3root ] && ok "  (nothing made)" || bad "devices made"
	r=$(put "1" $G/sealed)
	[ "$r" = 1 ] && [ "$(cat $G/sealed) $(n) $(cat $G/nr_sectors)" = "1 1 8" ] && ok "sealed: the 512 equal ranges merge into one of 8 sectors" ||
		bad "seal: $r, $(cat $G/sealed) $(n) $(cat $G/nr_sectors)"
	rewrite 10000 && ok "a write of sectors 10000-10007 (the range) went through" || bad "write inside refused"
	r0=$(refused)
	rewrite 10001 && bad "sectors 10001-10008 (one past the range) went through" || ok "sectors 10001-10008 (one past): refused"
	rewrite 9999 1 && bad "sector 9999 went through" || ok "sector 9999 (one before): refused"
	[ "$(refused)" = $((r0 + 2)) ] && ok "  (refused counted them)" || bad "refused $(refused)"
fi
[ $fails = 0 ] && echo "E2E G$PART PASS" || echo "E2E G$PART FAIL ($fails)"
