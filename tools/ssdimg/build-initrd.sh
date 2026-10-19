#!/bin/bash
# build-initrd.sh: the SSD root's initramfs (docs/GAMEOS-HDD.md "Phase B"), in the builder container
# (make initrd-ssd; tools/ssdimg/vm-e2e.sh builds an amd64 one for its KVM guest the same way).
# Contents: /init (tools/ssdimg/initrd/init), Debian's busybox-static with links for the applets the
# init and a rescue shell use (the destructive ones, mke2fs, mkswap, fdisk, ..., only as
# "busybox <applet>"), /sbin/ps3-imgmap-boot (static), the ufs and dm-mod modules of the kernel
# release, the boot maps in /ps3ssd/maps, /etc/ps3ssd-initrd (release, build, maps). A reproducible
# newc cpio (tools/ssdimg/mkcpio.py), xz with CRC32 and a 1 MiB dictionary as the kernel wants.
# Env:
#   ARCH          ppc64 (the PS3, default) or amd64 (the KVM test)
#   RELEASE       the kernel release (default: the newest release kernel in build/debs/linux)
#   KDEB          the linux-image deb the modules come from (default: RELEASE's in build/debs/linux)
#   MODDIR        instead of KDEB: a directory with ufs.ko and dm-mod.ko (the KVM test: the host's)
#   BUSYBOX_DEB   busybox-static's deb for ARCH (make downloads it, sha256 in config.mk)
#   IMGMAP        a copy of the PS3's /var/lib/ps3-imgmap/<name>.map.json, with its verified and created
#                 records next to it: ps3-imgmap export-boot makes the boot map from it
#   BOOTMAP       instead of IMGMAP: boot maps already made (space separated)
#   OUT           default build/initrd/initrd-ssd-$RELEASE.img (amd64: ...-amd64.img)
#   GUARD         required or auto: whether the boot needs ps3db's write guard (local/0045). Default:
#                 required when the release's System.map in KDEB has the guard, auto otherwise and with
#                 MODDIR (auto: a kernel without the guard boots with a log line saying so)
#   INITRD_BUILT  the build's name for /etc/ps3ssd-initrd (make passes git describe)
# Also writes $OUT.manifest (sha256 of every file and of the kernel package's modules, the sizes), the
# boot maps into maps/ next to $OUT, and ps3-imgmap-boot.$ARCH next to it: what the PS3 side
# (tools/ssdimg/ssd-install-image.sh) checks against the running system.
set -euo pipefail
{
cd "$(dirname "$0")/../.."
ARCH=${ARCH:-ppc64}
case $ARCH in
ppc64) CC=powerpc64-linux-gnu-gcc STRIP=powerpc64-linux-gnu-strip ;;
amd64) CC=gcc STRIP=strip ;;
*) echo "ARCH: ppc64 or amd64" >&2; exit 2 ;;
esac
if [ -z "${RELEASE:-}" ]; then
	RELEASE=$(ls build/debs/linux/ | sed -n 's/^linux-image-\(6[^_]*\)_.*_ppc64\.deb$/\1/p' | grep -v -- -debug | sort -V | tail -n 1)
fi
[ -n "$RELEASE" ] || { echo "no RELEASE and no release kernel in build/debs/linux" >&2; exit 2; }
: "${BUSYBOX_DEB:?}"
[ "$ARCH" = ppc64 ] && SUFFIX= || SUFFIX=-$ARCH
OUT=${OUT:-build/initrd/initrd-ssd-$RELEASE$SUFFIX.img}
mkdir -p build/tmp "$(dirname "$OUT")"
W=$(mktemp -d build/tmp/initrd.XXXXXX)
trap 'rm -rf "$W"' EXIT
T=$W/tree
mkdir -p "$T"/{bin,sbin,dev,proc,sys,run,root,gameos,etc,ps3ssd/maps,lib/modules/"$RELEASE"}

echo "== busybox ($(basename "$BUSYBOX_DEB"))"
dpkg-deb --fsys-tarfile "$BUSYBOX_DEB" | tar -x -C "$W" ./usr/bin/busybox
install -m 755 "$W/usr/bin/busybox" "$T/bin/busybox"
for a in sh ash cat chroot cp cut dd df dmesg du echo env false find free grep head hexdump insmod less \
	ln ls lsblk lsmod md5sum mkdir mknod modinfo more mount mv poweroff printf ps readlink reboot rm \
	rmmod sed setsid sha256sum sleep sort stat strings switch_root sync tail tee test timeout tr true \
	umount uname uniq usleep vi wc xargs blockdev cttyhack awk [; do
	ln -s busybox "$T/bin/$a"
done

echo "== ps3-imgmap-boot ($ARCH, static)"
$CC -static -O2 -Wall -Wextra -Werror -o "$T/sbin/ps3-imgmap-boot" tools/ssdimg/ps3-imgmap-boot.c
$STRIP "$T/sbin/ps3-imgmap-boot"
cp "$T/sbin/ps3-imgmap-boot" "$(dirname "$OUT")/ps3-imgmap-boot.$ARCH"
gcc -O2 -Wall -Wextra -Werror -o "$W/pib-host" tools/ssdimg/ps3-imgmap-boot.c	# for the map checks here

echo "== modules for $RELEASE"
M=$T/lib/modules/$RELEASE
if [ -n "${MODDIR:-}" ]; then
	for m in ufs dm-mod; do [ -e "$MODDIR/$m.ko" ] && install -m 644 "$MODDIR/$m.ko" "$M/"; done
else
	KDEB=${KDEB:-$(ls build/debs/linux/linux-image-"$RELEASE"_*_ppc64.deb)}
	dpkg-deb --fsys-tarfile "$KDEB" | tar -x -C "$W" --wildcards "*/modules/$RELEASE/kernel/drivers/md/dm-mod.ko" \
		"*/modules/$RELEASE/kernel/fs/ufs/ufs.ko"
	for f in $(find "$W" -path "$W/tree" -prune -o -name '*.ko' -print); do
		v=$(modinfo -F vermagic "$f")
		case "$v" in "$RELEASE "*) ;; *) echo "$f: vermagic '$v' is not $RELEASE" >&2; exit 1 ;; esac
		# the package's file, for ssd-install-image.sh to compare with the installed kernel's
		echo "module_src ${f#"$W"/} $(sha256sum "$f" | cut -d' ' -f1)" >> "$W/modsrc"
		install -m 644 "$f" "$M/"
		$STRIP --strip-debug "$M/$(basename "$f")"
	done
	[ -e "$M/ufs.ko" ] && [ -e "$M/dm-mod.ko" ] || { echo "ufs.ko or dm-mod.ko missing in $KDEB" >&2; exit 1; }
fi

echo "== the write guard"
if [ -z "${GUARD:-}" ]; then
	GUARD=auto
	if [ -z "${MODDIR:-}" ]; then
		dpkg-deb --fsys-tarfile "$KDEB" | tar -x -C "$W" "./boot/System.map-$RELEASE"
		# ps3disk's sysfs group and BLKROSET hook exist only with local/0045
		grep -Eq ' (ps3disk_guard_group|ps3disk_set_read_only)$' "$W/boot/System.map-$RELEASE" && GUARD=required
	fi
fi
case $GUARD in required|auto) ;; *) echo "GUARD: required or auto" >&2; exit 2 ;; esac
echo "guard=$GUARD"

echo "== boot maps"
maps=()
if [ -n "${IMGMAP:-}" ]; then
	n=$(basename "$IMGMAP" .map.json)
	python3 -I tools/ssdimg/ps3-imgmap export-boot "$IMGMAP" --state "$(dirname "$IMGMAP")" --out "$W/$n.map"
	maps+=("$W/$n.map")
fi
for b in ${BOOTMAP:-}; do maps+=("$b"); done
[ ${#maps[@]} -gt 0 ] || { echo "no map: IMGMAP=<map.json> or BOOTMAP=<boot map>" >&2; exit 2; }
names=
for b in "${maps[@]}"; do
	"$W/pib-host" check "$b"
	n=$(basename "$b" .map)
	[ ! -e "$T/ps3ssd/maps/$n.map" ] || { echo "two maps named $n" >&2; exit 1; }
	install -m 644 "$b" "$T/ps3ssd/maps/$n.map"
	mkdir -p "$(dirname "$OUT")/maps" && install -m 644 "$b" "$(dirname "$OUT")/maps/$n.map"
	names="$names $n:$(sed -n 's/^image //p' "$b")"
done

install -m 755 tools/ssdimg/initrd/init "$T/init"
printf 'release=%s\nbuilt=%s\narch=%s\nmaps=%s\nguard=%s\n' "$RELEASE" "${INITRD_BUILT:-unknown}" "$ARCH" "${names# }" "$GUARD" \
	> "$T/etc/ps3ssd-initrd"

echo "== cpio, xz"
python3 -I tools/ssdimg/mkcpio.py "$T" "$W/initrd.cpio" --mtime "${SOURCE_DATE_EPOCH:-0}" --node /dev/console:600:c:5:1
xz -9 --check=crc32 --lzma2=preset=9,dict=1MiB -T1 -c "$W/initrd.cpio" > "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
{
	cat "$T/etc/ps3ssd-initrd"
	echo "cpio_bytes=$(stat -c %s "$W/initrd.cpio")"
	echo "image_bytes=$(stat -c %s "$OUT")"
	echo "image_sha256=$(sha256sum "$OUT" | cut -d' ' -f1)"
	cat "$W/modsrc" 2>/dev/null || true
	(cd "$T" && find . -type f | sort | xargs sha256sum)
} > "$OUT.manifest"
echo ">> $OUT: $(stat -c %s "$OUT") bytes ($(stat -c %s "$W/initrd.cpio") unpacked), maps:$names"
exit 0
}
