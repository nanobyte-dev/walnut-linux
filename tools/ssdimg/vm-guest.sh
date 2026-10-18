#!/bin/bash
# vm-guest.sh: vm-e2e.sh's part inside the KVM guest (the host's / read-only, chrooted; /tmp tmpfs).
# Disks: vda the UFS2 image (stands in for ps3db), vdb a scratch disk (for ps3da), vdc vda's
# original, read-only. Prints "E2E PASS" when everything held.
set -u
arg() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" /proc/cmdline; }
ROOT=$(arg e2e_root); W=$(arg e2e_work); FSIZE=$(arg e2e_fsize); BSIZE=$(arg e2e_bsize)
T=$ROOT/tools/ssdimg
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PS3_IMGMAP_TEST_DEVICE=/dev/vda PS3_IMGMAP_TEST_WHOLE=/dev/vdb PS3_IMGMAP_STATE=/tmp/state
export PS3_IMGMAP_DMSETUP="dmsetup --noudevsync" DM_DISABLE_UDEV=1 PYTHONDONTWRITEBYTECODE=1
fails=0
ok() { echo "ok - $*"; }
bad() { echo "FAIL - $*"; fails=$((fails + 1)); }
expect() {	# expect RC WHAT CMD...: CMD's status must be RC
	local want=$1 what=$2; shift 2
	"$@" > /tmp/out 2>&1; local rc=$?
	sed 's/^/     /' /tmp/out
	[ $rc = "$want" ] && ok "$what" || bad "$what (status $rc, wanted $want)"
}
IM="python3 -I $T/ps3-imgmap"
MNT=/tmp/mnt
MAP=/tmp/state/ssdtest.map.json

mount -t ufs -o ro,ufstype=ufs2 /dev/vda $MNT || { echo "E2E FAIL: mount"; exit 1; }
expect 0 "ufs mounted read-only, mountinfo has ro and ufstype=ufs2" grep -E ' / /tmp/mnt ro.* - ufs /dev/vda ro,.*ufstype=ufs2' /proc/self/mountinfo
expect 0 "map" $IM map $MNT/linux/ssdtest.img
grep -q "fragment $FSIZE" /tmp/out && ok "map: UFS2 big-endian, fragment $FSIZE from the super block" || bad "fragment size"
python3 - "$MAP" "$FSIZE" <<'EOF' && ok "map: FIGETBSZ = fs_fsize, extents cover the file, more than one (indirect blocks between)" || bad "map contents"
import json, sys
m = json.load(open(sys.argv[1]))
assert m["fragment_size"] == int(sys.argv[2]), m["fragment_size"]
assert m["byte_order"] == "big-endian"
assert sum(e[2] for e in m["extents"]) * 512 == m["file_size"] == 24 << 20
assert len(m["extents"]) > 1, m["extents"]
print("     extents:", m["extents"][:6], "...")
EOF
# the unit itself: the first data sector the map names holds the file's first block, read raw
python3 - "$MAP" <<'EOF' && ok "map: the device sector of image byte 0 and of the last block hold those stamps" || bad "unit check"
import json, os, struct, sys
m = json.load(open(sys.argv[1]))
fd = os.open("/dev/vda", os.O_RDONLY)
l, p, n = m["extents"][0]
b = os.pread(fd, 4096, p * 512)
assert b[:8] == b"PS3IMGv1" and struct.unpack(">Q", b[24:32])[0] == 0
l, p, n = m["extents"][-1]
b = os.pread(fd, 4096, (p + n) * 512 - 4096)
assert struct.unpack(">Q", b[24:32])[0] == m["file_size"] - 4096
EOF
expect 0 "map --every-fragment gives the same extents" $IM map $MNT/linux/ssdtest.img --every-fragment --out /tmp/state/every.map.json
python3 -c "import json,sys; a,b=(json.load(open(f)) for f in sys.argv[1:]); sys.exit(a['extents']!=b['extents'])" $MAP /tmp/state/every.map.json &&
	ok "every-fragment extents equal" || bad "every-fragment extents differ"
expect 2 "map refuses a file that isn't a stamped image" $IM map $MNT/zz-after.dat
expect 0 "verify (O_DIRECT through the extents)" $IM verify $MAP
expect 0 "recheck" $IM recheck $MAP
expect 2 "create refused while ps3db is mounted" $IM create $MAP ps3ssd --yes
# the kernel side of that: device-mapper can't claim a mounted disk
P=$(python3 -c "import json; print(json.load(open('$MAP'))['extents'][0][1])")
expect 1 "dmsetup create over a mounted disk fails (EBUSY: dm's exclusive claim)" dmsetup --noudevsync create probe --table "0 8 linear /dev/vda $P"
dmsetup --noudevsync remove probe >/dev/null 2>&1
umount $MNT
expect 0 "create after unmount (setro, dm-linear, with swap)" $IM create $MAP ps3ssd --yes --swap-mib 2
[ "$(cat /sys/class/block/vdb/ro)" = 1 ] && ok "the whole-disk stand-in is read-only" || bad "setro"
[ -b /dev/mapper/ps3ssd ] && [ -b /dev/mapper/ps3ssd-swap ] && ok "both mapped devices" || bad "mapped devices"
want=$(( (24 << 20) / 512 - 2048 - 4096 ))
[ "$(blockdev --getsz /dev/mapper/ps3ssd)" = $want ] && ok "ps3ssd is the image less header and swap ($want sectors)" || bad "ps3ssd size $(blockdev --getsz /dev/mapper/ps3ssd)"
expect 0 "mkfs.ext4 -L ps3ssd on the mapped device" mkfs.ext4 -q -F -L ps3ssd /dev/mapper/ps3ssd
mkdir -p /tmp/ext; mount /dev/mapper/ps3ssd /tmp/ext && head -c 9000000 /dev/urandom > /tmp/ext/fill && sync && umount /tmp/ext &&
	ok "ext4 mounted, 9 MB written" || bad "ext4 use"
expect 0 "mkswap and swapon on the swap device" sh -c 'mkswap /dev/mapper/ps3ssd-swap >/dev/null && swapon /dev/mapper/ps3ssd-swap && swapoff /dev/mapper/ps3ssd-swap'
dd if=/dev/urandom of=/dev/mapper/ps3ssd-swap bs=1M count=2 oflag=direct status=none && ok "swap device overwritten" || bad "swap write"
expect 0 "remove" $IM remove ps3ssd
expect 2 "verify of a map in use is refused (pass kept)" $IM verify $MAP
expect 1 "verify --force fails now (the stamps are overwritten, as they should be)" $IM verify $MAP --force
# nothing outside the file's blocks changed, the header area neither; the data part did
python3 - "$MAP" <<'EOF' && ok "every sector outside the image's data blocks equals the original (UFS metadata, other files, header)" || bad "writes escaped the image"
import json, sys
m = json.load(open(sys.argv[1]))
inside = []   # device sector ranges of image sectors >= 2048 (the mapped part)
for l, p, n in m["extents"]:
    a = max(l, 2048)
    if a < l + n:
        inside.append((p + a - l, p + n))
inside.sort()
cur, orig = open("/dev/vda", "rb"), open("/dev/vdc", "rb")
pos, changed_in = 0, 0
end = cur.seek(0, 2)
cur.seek(0)
def same(a, b):
    cur.seek(a * 512); orig.seek(a * 512)
    while a < b:
        k = min(2048, b - a)
        if cur.read(k * 512) != orig.read(k * 512):
            raise SystemExit("sector %d..%d differs outside the image" % (a, a + k))
        a += k
for a, b in inside:
    same(pos, a)
    cur.seek(a * 512); orig.seek(a * 512)
    if cur.read((b - a) * 512) != orig.read((b - a) * 512):
        changed_in += 1
    pos = b
same(pos, end // 512)
assert changed_in, "nothing changed inside the image: the writes went nowhere"
print("     %d of %d mapped ranges written, everything else identical" % (changed_in, len(inside)))
EOF
mount -t ufs -o ro,ufstype=ufs2 /dev/vda $MNT && ok "UFS still mounts" || bad "UFS mount after"
sha256sum $MNT/home/00000001/save.dat $MNT/zz-after.dat | awk '{print $1}' | cmp -s - "$W/sums" &&
	ok "the other files' contents unchanged" || bad "other files changed"
expect 0 "recheck after use: the block list unchanged" $IM recheck $MAP
umount $MNT
[ $fails = 0 ] && echo "E2E PASS" || echo "E2E FAIL ($fails)"
