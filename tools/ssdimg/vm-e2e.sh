#!/bin/bash
# vm-e2e.sh: ps3-imgmap against the real Linux ufs driver, on the PC, without root: a big-endian UFS2
# image made with NetBSD's makefs (Debian's source, rebuilt in the builder container with the UFS2
# super block where Linux and GameOS have it; the image made unprivileged) holding a stamped
# file, mounted read-only in a throwaway KVM guest that boots the host's own kernel (its ufs and 9p
# modules in a busybox initramfs) with the host's / shared read-only over 9p for python3, dmsetup,
# blockdev and mkfs.ext4. In the guest (tools/ssdimg/vm-guest.sh): map, verify, recheck, the refusals,
# dm-linear's EBUSY on a mounted disk, create, mkfs.ext4 and writes on the mapped devices, then the
# proof that every sector outside the file's blocks (UFS metadata, the other files, the header area)
# is byte for byte the original and the UFS still mounts with the same file contents.
# Two geometries: block 16384 / fragment 2048 and block 32768 / fragment 4096 (the PS3's is not known
# yet: ps3-imgmap map prints it).
# Needs docker (the builder image), qemu-system-x86_64, /dev/kvm, busybox from the alpine image,
# the host kernel's modules. Exit 77 when one is missing. Scratch in build/tmp/ssdimg/vm, removed after.
set -u
cd "$(dirname "$0")/../.."
ROOT=$PWD
W=$ROOT/build/tmp/ssdimg/vm
KVER=${KVER:-$(uname -r)}
KERNEL=/boot/vmlinuz-$KVER
MODS=/lib/modules/$KVER
IMG_BUILDER=${IMG_BUILDER:-$(docker images --format '{{.Repository}}:{{.Tag}}' 2>/dev/null | grep '^ps3-deb-builder:' | head -1)}
skip() { echo "skip: $*"; exit 77; }
command -v qemu-system-x86_64 >/dev/null || skip "no qemu-system-x86_64"
[ -r /dev/kvm ] && [ -w /dev/kvm ] || skip "no /dev/kvm"
[ -r "$KERNEL" ] || skip "no readable $KERNEL"
[ -n "$IMG_BUILDER" ] || skip "no ps3-deb-builder image (makefs)"
docker image inspect alpine:3.20 >/dev/null 2>&1 || skip "no alpine:3.20 image (busybox)"
for m in fs/netfs/netfs net/9p/9pnet net/9p/9pnet_virtio fs/9p/9p fs/ufs/ufs; do
	ls "$MODS/kernel/$m.ko"* >/dev/null 2>&1 || grep -q "kernel/$m.ko" "$MODS/modules.builtin" || skip "no $m module"
done
grep -q 'kernel/drivers/md/dm-mod.ko' "$MODS/modules.builtin" || ls "$MODS/kernel/drivers/md/dm-mod.ko"* >/dev/null 2>&1 ||
	skip "no dm-mod"

rm -rf "$W"; mkdir -p "$W/initrd/bin" "$W/initrd/lib" "$W/initrd/mods" "$W/src/linux"
trap 'rm -rf "$W"' EXIT

# the initramfs: busybox (alpine's, with its musl loader) and the modules, uncompressed
c=$(docker create alpine:3.20) || exit 1
docker cp "$c:/bin/busybox" "$W/initrd/bin/busybox" >/dev/null && docker cp -L "$c:/lib/ld-musl-x86_64.so.1" "$W/initrd/lib/" >/dev/null
docker rm "$c" >/dev/null
for m in fs/netfs/netfs net/9p/9pnet net/9p/9pnet_virtio fs/9p/9p fs/ufs/ufs drivers/md/dm-mod; do
	f=$(ls "$MODS/kernel/$m.ko"* 2>/dev/null | head -1)
	[ -n "$f" ] || continue
	case $f in
	*.xz) xz -dc "$f" > "$W/initrd/mods/${m##*/}.ko" ;;
	*.zst) zstd -qdc "$f" > "$W/initrd/mods/${m##*/}.ko" ;;
	*) cp "$f" "$W/initrd/mods/" ;;
	esac
done
cat > "$W/initrd/init" <<'EOF'
#!/bin/busybox sh
/bin/busybox --install -s /bin
export PATH=/bin
mkdir -p /proc /sys /dev /host /tmp
mount -t proc proc /proc; mount -t sysfs sys /sys; mount -t devtmpfs dev /dev
for m in netfs 9pnet 9pnet_virtio 9p ufs dm-mod; do [ -e /mods/$m.ko ] && insmod /mods/$m.ko; done
mount -t 9p -o trans=virtio,version=9p2000.L,ro,msize=262144 hostroot /host || { echo "E2E FAIL: no 9p"; poweroff -f; }
for d in proc sys dev; do mount --bind /$d /host/$d; done
mount -t tmpfs tmp /host/tmp; mount -t tmpfs run /host/run
mkdir -p /host/tmp/mnt
G=$(sed -n 's/.*e2e_guest=\([^ ]*\).*/\1/p' /proc/cmdline)
chroot /host /bin/bash "$G"
echo "E2E guest exit $?"
poweroff -f
EOF
chmod 755 "$W/initrd/init"
(cd "$W/initrd" && find . | cpio -o -H newc --quiet | gzip -1) > "$W/initrd.gz" || exit 1

# NetBSD's makefs (Debian's package source), rebuilt with the UFS2 super block at 64 KiB: as
# packaged it puts it at 8 KiB ("#if 0 /* XXX makefs is used for small filesystems. */" in
# ffs/mkfs.c), where Linux's ufstype=ufs2 doesn't look (GameOS's dev_hdd0 has it at 64 KiB)
echo "== makefs from Debian's source, UFS2 super block at 64 KiB"
mkdir -p "$W/msrc"
docker run --rm -v "$W/msrc:/s" "$IMG_BUILDER" bash -c "cd /s && apt-get update -qq >/dev/null &&
	apt-get source -qq makefs >/dev/null 2>&1 && cd makefs-*/ &&
	sed -i 's|^#if 0 /\\* XXX makefs is used for small filesystems. \\*/|#if 1|' src/usr.sbin/makefs/ffs/mkfs.c &&
	! grep -q 'XXX makefs is used for small' src/usr.sbin/makefs/ffs/mkfs.c && apt-get build-dep -y -qq . >/dev/null 2>&1 &&
	dpkg-buildpackage -b -uc -us -nc > /s/build.log 2>&1; rc=\$?; chown -R $(id -u):$(id -g) /s; exit \$rc" ||
	{ tail -20 "$W/msrc/build.log" 2>/dev/null; echo "makefs build failed"; exit 1; }
cp "$W"/msrc/makefs-*/builddir/usr.sbin/makefs/makefs "$W/makefs" || exit 1
rm -rf "$W/msrc"

rc=0
for geo in 16384:2048 32768:4096; do
	b=${geo%:*}; f=${geo#*:}
	echo "== UFS2 big-endian, block $b, fragment $f"
	rm -rf "$W/src"; mkdir -p "$W/src/linux" "$W/src/home/00000001"
	python3 tools/ssdimg/stamp.py dry-run --mib 24 --uuid 0ddba11c-0ffe-4ea5-b0b0-123456789abc \
		--out "$W/src/linux/ssdtest.img" 2>/dev/null || exit 1
	head -c 3000000 /dev/urandom > "$W/src/home/00000001/save.dat"   # GameOS's files around it
	head -c 700000 /dev/urandom > "$W/src/zz-after.dat"
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$W:/w" "$IMG_BUILDER" \
		/w/makefs -t ffs -B be -s 64m -o version=2,bsize=$b,fsize=$f /w/ufs.img /w/src > "$W/makefs.log" 2>&1 ||
		{ cat "$W/makefs.log"; exit 1; }
	cp "$W/ufs.img" "$W/orig.img"
	truncate -s 8m "$W/whole.img"     # stands in for ps3da (setro)
	sha256sum "$W/src/home/00000001/save.dat" "$W/src/zz-after.dat" | awk '{print $1}' > "$W/sums"
	timeout 600 qemu-system-x86_64 -enable-kvm -cpu host -m 1024 -smp 2 -nographic -no-reboot \
		-kernel "$KERNEL" -initrd "$W/initrd.gz" \
		-append "console=ttyS0 panic=-1 loglevel=4 e2e_guest=$ROOT/tools/ssdimg/vm-guest.sh e2e_root=$ROOT e2e_work=$W e2e_fsize=$f e2e_bsize=$b" \
		-drive file="$W/ufs.img",if=virtio,format=raw,cache=none \
		-drive file="$W/whole.img",if=virtio,format=raw \
		-drive file="$W/orig.img",if=virtio,format=raw,readonly=on \
		-virtfs local,path=/,mount_tag=hostroot,security_model=none,readonly=on \
		> "$W/console.log" 2>&1
	tr -d '\r' < "$W/console.log" | grep -E '^(E2E|>>|!!|ok|FAIL)' | sed 's/^/   /'
	if grep -q '^E2E PASS' <(tr -d '\r' < "$W/console.log"); then echo "== pass ($b/$f)"
	else echo "== FAILED ($b/$f): console log tail:"; tr -d '\r' < "$W/console.log" | tail -40; rc=1; fi
done
exit $rc
