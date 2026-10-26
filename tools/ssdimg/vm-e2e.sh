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
# Two geometries: block 16384 / fragment 2048 and block 32768 / fragment 4096 (the PS3's is block
# 16384, fragment 4096).
# Phase B (docs/GAMEOS-HDD.md), per geometry, on a fresh copy of the image: a second guest
# (tools/ssdimg/vm-guest-b.sh) compares ps3-imgmap-boot's devices with ps3-imgmap create's and puts a
# test root on ext4 on /dev/mapper/ps3root; then the real initramfs (tools/ssdimg/build-initrd.sh,
# amd64: Debian's busybox-static, the static tool, the host's ufs and dm-mod) boots the guest with
# root=/dev/mapper/ps3root and the test root checks what it finds (and writes); every sector outside
# the image's mapped blocks must still be the original's. For the first geometry also three refused
# boots (no map for the image, a broken header area, the file moved by a rebuilt UFS): each must stop
# in the rescue shell with its reason, the disk image unchanged.
# The kernel guard (local/0045, docs/GAMEOS-HDD.md "The kernel guard"): ps3guard_sim
# (tools/ssdimg/guard-sim, built here against the host kernel) gives the guests disks named ps3da and
# ps3db over vdb and vda with the patch's own guard code and sysfs interface. The second Phase B
# geometry runs on them: the install guest (ps3-imgmap create registers and seals, the install's
# ext4 behind the guard), the initramfs (built with guard=required) registering and sealing what it
# made, the test root's stray writes refused, a boot refused without the guard; then two guard
# guests (tools/ssdimg/vm-guest-guard.sh): Phase A's create behind the guard and the C tool refusing
# a sealed guard, and sysfs's own edges (a write cut at 4 KiB, malformed lines, pending ranges, the
# seal's merge, writes on both sides of a range). vm-guest-b.sh also runs the guard model build
# (-DPS3_GUARD_FAKE) against the real device-mapper: a registration failure removes the devices.
# Without the host kernel's headers (kernel-devel) the guard parts are skipped (said so).
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
for m in fs/netfs/netfs net/9p/9pnet net/9p/9pnet_virtio fs/9p/9p fs/ufs/ufs drivers/md/dm-mod fs/fat/fat fs/fat/vfat \
	fs/nls/nls_cp437 fs/nls/nls_iso8859-1; do
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
if grep -q e2e_out=1 /proc/cmdline; then
	mkdir -p /host/tmp/out; mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 out /host/tmp/out
fi
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

# Phase B's pieces: the amd64 busybox-static (the PS3's version), ps3-imgmap-boot static for amd64,
# the rdinit wrapper that gives the virtual disks the PS3's names before the initramfs's /init runs
PHASEB=1
make --no-print-directory -s initrd-busybox >/dev/null 2>&1 || { echo "(Phase B skipped: no busybox-static debs)"; PHASEB=0; }
BB_AMD64=$(ls build/downloads/initrd/busybox-static_*_amd64.deb 2>/dev/null | head -1)
REL_W=${W#"$ROOT"/}
if [ $PHASEB = 1 ]; then
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$ROOT:/work" -w /work "$IMG_BUILDER" sh -c "
		gcc -static -O2 -Wall -Wextra -Werror -o $REL_W/pib-amd64 tools/ssdimg/ps3-imgmap-boot.c &&
		dpkg-deb --fsys-tarfile $BB_AMD64 | tar -x -O ./usr/bin/busybox > $REL_W/busybox && chmod 755 $REL_W/busybox" ||
		{ echo "Phase B tools build failed"; exit 1; }
	mkdir -p "$W/guard-inc"
	packages/patched/kernel/linux/tests/guard-header.sh > "$W/guard-inc/ps3disk_guard.h" || exit 1
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$ROOT:/work" -w /work "$IMG_BUILDER" sh -c "
		gcc -static -O2 -Wall -Wextra -Werror -DPS3_GUARD_FAKE -I $REL_W/guard-inc -I tools/ssdimg \
			-o $REL_W/pib-amd64-fake tools/ssdimg/ps3-imgmap-boot.c" || { echo "guard model build failed"; exit 1; }
	mkdir -p "$W/e2e-wrap"
	cp tools/ssdimg/vm-init-e2e.sh "$W/e2e-wrap/init.e2e" && chmod 755 "$W/e2e-wrap/init.e2e"
fi
# the guard simulation for the host kernel (the guests run it)
GUARDSIM=0
if [ $PHASEB = 1 ]; then
	if [ -e "$MODS/build/Makefile" ] && tools/ssdimg/guard-sim/extract.sh "$W/guardsim" &&
	   make -s -C "$MODS/build" M="$W/guardsim" modules > "$W/guardsim.log" 2>&1 && [ -e "$W/guardsim/ps3guard_sim.ko" ]; then
		GUARDSIM=1
		cp "$W/guardsim/ps3guard_sim.ko" "$W/e2e-wrap/"
	else
		echo "(the kernel guard's parts skipped: no ps3guard_sim for $KVER, kernel headers in $MODS/build?)"
		tail -5 "$W/guardsim.log" 2>/dev/null
	fi
	python3 -I tools/ssdimg/mkcpio.py "$W/e2e-wrap" "$W/e2e-wrap.cpio" || exit 1
fi

# boot_until LOG PATTERN SECONDS QEMU-ARGS...: a guest in the background, until PATTERN is in its
# console log, it powers off, or SECONDS pass; then it is killed (a guest in the rescue shell waits forever)
boot_until() {
	local log=$1 pat=$2 secs=$3; shift 3
	rm -f "$log"
	qemu-system-x86_64 -enable-kvm -cpu host -m 1024 -smp 2 -display none -no-reboot -monitor none \
		-serial file:"$log" "$@" > "$log.qemu" 2>&1 < /dev/null &
	local pid=$! n=0
	while kill -0 $pid 2>/dev/null && [ $n -lt $((secs * 2)) ]; do
		grep -q "$pat" "$log" 2>/dev/null && { sleep 2; break; }
		sleep 0.5; n=$((n + 1))
	done
	kill $pid 2>/dev/null; wait $pid 2>/dev/null
	tr -d '\r' < "$log" > "$log.txt"
}
# outside_same MAP A B: every device sector outside the image's mapped part is the same in A and B;
# prints how many mapped ranges differ
outside_same() { python3 -I tools/ssdimg/vm-outside.py "$@"; }

refused() {	# refused WHAT IMAGEFILE PATTERN KERNEL-ARGS...
	local what=$1 img=$2 pat=$3; shift 3
	cp "$img" "$W/neg.before.img"
	boot_until "$W/console-neg.log" "reboot -f" 60 -kernel "$KERNEL" -initrd "$W/initrd-test.img" \
		-append "$(echo "$CMD" | sed "s|ps3ssd.image=[^ ]*|$1|")" -drive file="$img",if=virtio,format=raw,cache=none \
		-drive file="$W/whole.img",if=virtio,format=raw
	if grep -q "$pat" "$W/console-neg.log.txt" && grep -q 'the SSD root was NOT started' "$W/console-neg.log.txt" &&
	   ! grep -q 'E2E BOOT' "$W/console-neg.log.txt" && cmp -s "$img" "$W/neg.before.img"; then
		echo "   ok - refused: $what ($(grep -m1 'FAILED at' "$W/console-neg.log.txt" | sed 's/.*FAILED at //' | cut -c1-100))"
	else echo "   FAIL - $what: console tail:"; tail -30 "$W/console-neg.log.txt"; rc=1; fi
}

rc=0
first=1
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

# Phase B: the PS3's geometry (block 16384, fragment 4096: the 24 MiB file is one extent after the
# header, its single indirect block covers 32 MiB) and block 4096 / fragment 512, where every 2 MiB of
# the file has an indirect block between, so both tables have several dm-linear targets (the PS3's
# 16 GiB file has 513 extents)
for geo in 16384:4096 4096:512; do
	[ $PHASEB = 1 ] || break
	b=${geo%:*}; f=${geo#*:}
	SIM=0 GUARD=auto		# the second geometry behind the guard simulation
	[ "$geo" = 4096:512 ] && [ $GUARDSIM = 1 ] && SIM=1 GUARD=required
	echo "== Phase B, block $b, fragment $f: ps3-imgmap-boot against ps3-imgmap create"
	rm -rf "$W/src"; mkdir -p "$W/src/linux" "$W/src/home/00000001"
	python3 tools/ssdimg/stamp.py dry-run --mib 24 --uuid 0ddba11c-0ffe-4ea5-b0b0-123456789abc \
		--out "$W/src/linux/ssdtest.img" 2>/dev/null || exit 1
	head -c 3000000 /dev/urandom > "$W/src/home/00000001/save.dat"
	head -c 700000 /dev/urandom > "$W/src/zz-after.dat"
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$W:/w" "$IMG_BUILDER" \
		/w/makefs -t ffs -B be -s 64m -o version=2,bsize=$b,fsize=$f /w/ufs.img /w/src > "$W/makefs.log" 2>&1 ||
		{ cat "$W/makefs.log"; exit 1; }
	cp "$W/ufs.img" "$W/orig.img"
	rm -f "$W/whole.img"; truncate -s 8m "$W/whole.img"
	rm -rf "$W/out"; mkdir -p "$W/out"
	timeout 600 qemu-system-x86_64 -enable-kvm -cpu host -m 1024 -smp 2 -nographic -no-reboot \
		-kernel "$KERNEL" -initrd "$W/initrd.gz" \
		-append "console=ttyS0 panic=-1 loglevel=4 e2e_guest=$ROOT/tools/ssdimg/vm-guest-b.sh e2e_root=$ROOT e2e_work=$W e2e_out=1" \
		-drive file="$W/ufs.img",if=virtio,format=raw,cache=none \
		-drive file="$W/whole.img",if=virtio,format=raw \
		-virtfs local,path=/,mount_tag=hostroot,security_model=none,readonly=on \
		-virtfs local,path="$W/out",mount_tag=out,security_model=none \
		> "$W/console-b.log" 2>&1
	tr -d '\r' < "$W/console-b.log" | grep -E '^(E2E|>>|!!|ok|FAIL)' | sed 's/^/   /'
	if ! grep -q '^E2E B PASS' <(tr -d '\r' < "$W/console-b.log"); then
		echo "== FAILED (Phase B guest, $b/$f): console log tail:"; tr -d '\r' < "$W/console-b.log" | tail -40; rc=1; continue
	fi

	echo "== Phase B, block $b, fragment $f: ssd-install-image.sh check, install, boot$([ $SIM = 1 ] && echo ", behind the guard")"
	rm -rf "$W/from"
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$ROOT:/work" -w /work \
		-e ARCH=amd64 -e RELEASE="$KVER" -e MODDIR="$REL_W/initrd/mods" -e BUSYBOX_DEB="$BB_AMD64" -e GUARD=$GUARD \
		-e BOOTMAP="$REL_W/out/ssdtest.map" -e OUT="$REL_W/from/initrd-ssd-$KVER-amd64.img" -e INITRD_BUILT=vm-e2e \
		-e SOURCE_DATE_EPOCH=0 "$IMG_BUILDER" tools/ssdimg/build-initrd.sh > "$W/build-initrd.log" 2>&1 ||
		{ cat "$W/build-initrd.log"; echo "initramfs build failed"; rc=1; continue; }
	grep '^>>' "$W/build-initrd.log" | sed 's/^/   /'
	# the image to install: busybox and the checking init (tools/ssdimg/vm-testroot-init.sh)
	rm -rf "$W/testroot"; mkdir -p "$W/testroot"/{bin,sbin,dev,proc,sys,run,tmp,etc,mnt,usr/lib/modules/"$KVER"}
	cp "$W/busybox" "$W/testroot/bin/"
	for a in sh cat grep awk test blockdev mount umount sync poweroff head dd stat tr echo sleep ls; do ln -s busybox "$W/testroot/bin/$a"; done
	install -m 755 tools/ssdimg/vm-testroot-init.sh "$W/testroot/sbin/init"
	echo "tmpfs /tmp tmpfs defaults 0 0" > "$W/testroot/etc/fstab"
	tar --owner=0 --group=0 -C "$W/testroot" -cf "$W/testroot.tar" . || exit 1
	# the USB root's side: kernel, arguments, boot settings, the machine's state, home, the bench
	U=$W/out/usbroot
	mkdir -p "$U"/{boot,usr/lib/ps3/boot,etc/default,etc/ssh,home/tibi/.config,var/tmp/bench,var/tmp/ps3-bench/shadercache,var/tmp/ps3-bench/20261019-0100-sauerbraten-base}
	echo k > "$U/boot/vmlinux-$KVER"
	echo "console=ttyS0 panic=-1 loglevel=4" > "$U/usr/lib/ps3/boot/$KVER.args"
	printf 'PS3_ROOT_ARGS="root=PARTUUID=50533301-02 rootfstype=ext4 ro rootwait"\nPS3_EXTRA_ARGS=""\nPS3_DEFAULT_FLAVOUR=release\n' \
		> "$U/etc/default/ps3-boot"
	echo ps3-e2e > "$U/etc/hostname"; printf '127.0.0.1 localhost\n' > "$U/etc/hosts"
	echo key > "$U/etc/ssh/ssh_host_e2e_key"; echo e2e > "$U/home/tibi/.config/e2e"
	echo run > "$U/var/tmp/bench/run.sh"; echo x > "$U/var/tmp/ps3-bench/shadercache/x"
	echo y > "$U/var/tmp/ps3-bench/20261019-0100-sauerbraten-base/y"
	timeout 600 qemu-system-x86_64 -enable-kvm -cpu host -m 1024 -smp 2 -nographic -no-reboot \
		-kernel "$KERNEL" -initrd "$W/initrd.gz" \
		-append "console=ttyS0 panic=-1 loglevel=4 e2e_guest=$ROOT/tools/ssdimg/vm-guest-install.sh e2e_root=$ROOT e2e_work=$W e2e_out=1 e2e_guard=$SIM" \
		-drive file="$W/ufs.img",if=virtio,format=raw,cache=none \
		-drive file="$W/whole.img",if=virtio,format=raw \
		-virtfs local,path=/,mount_tag=hostroot,security_model=none,readonly=on \
		-virtfs local,path="$W/out",mount_tag=out,security_model=none \
		> "$W/console-i.log" 2>&1
	tr -d '\r' < "$W/console-i.log" | grep -E '^(E2E|ok|FAIL)' | sed 's/^/   /'
	if ! grep -q '^E2E I PASS' <(tr -d '\r' < "$W/console-i.log"); then
		echo "== FAILED (install guest, $b/$f): console log tail:"; tr -d '\r' < "$W/console-i.log" | tail -60; rc=1; continue
	fi

	echo "== Phase B, block $b, fragment $f: the boot entry's kernel line and initramfs boot root=/dev/mapper/ps3root"
	# as Petitboot: root= first, initrd= taken out, the image is the kernel (the host's here)
	entry=$(sed -n "s/^ssd-$KVER='\(.*\)'\$/\1/p" "$U/etc/kboot.conf")
	[ -n "$entry" ] || { echo "   FAIL - no ssd entry in kboot.conf"; rc=1; continue; }
	CMD="rdinit=/init.e2e $(echo "$entry" | tr ' ' '\n' | sed '1d; /^initrd=/d' | xargs)"
	[ $SIM = 1 ] && CMD="$CMD e2e_guard=1"	# the wrapper loads ps3guard_sim instead of naming the disks
	echo "   kernel line: $CMD"
	cat "$U/boot/initrd-ssd-$KVER.img" "$W/e2e-wrap.cpio" > "$W/initrd-test.img"
	cp "$W/ufs.img" "$W/ufs.before.img"
	boot_until "$W/console-boot.log" 'E2E BOOT' 120 -kernel "$KERNEL" -initrd "$W/initrd-test.img" -append "$CMD" \
		-drive file="$W/ufs.img",if=virtio,format=raw,cache=none -drive file="$W/whole.img",if=virtio,format=raw
	grep -E '^(ps3ssd|ok|FAIL|E2E)' "$W/console-boot.log.txt" | sed 's/^/   /'
	if grep -q '^E2E BOOT PASS' "$W/console-boot.log.txt"; then echo "   ok - booted from the image"
	else echo "== FAILED (boot, $b/$f): console tail:"; tail -40 "$W/console-boot.log.txt"; rc=1; fi
	for d in ps3root ps3swap; do
		n=$(sed -n "s|^ps3ssd: /dev/mapper/$d: .*, \([0-9]*\) extents).*|\1|p" "$W/console-boot.log.txt")
		if [ "$b" = 4096 ] && [ "${n:-0}" -le 1 ]; then echo "   FAIL - $d has ${n:-no} targets, wanted several"; rc=1
		else echo "   ok - $d is ${n:-?} dm-linear target(s)"; fi
	done
	if n=$(outside_same "$W/out/ssdtest.map.json" "$W/ufs.img" "$W/orig.img"); then
		echo "   ok - outside the image's mapped blocks the disk is the original ($n mapped ranges written)"
	else echo "   FAIL - $n"; rc=1; fi
	if n=$(outside_same "$W/out/ssdtest.map.json" "$W/ufs.img" "$W/ufs.before.img") && [ "$n" -gt 0 ]; then
		echo "   ok - the booted system's writes landed inside the image ($n ranges)"
	else echo "   FAIL - writes of the booted system: $n"; rc=1; fi

	if [ $SIM = 1 ]; then
		echo "== Phase B, block $b, fragment $f: the kernel guard"
		grep -q '^ps3ssd: write guard sealed: ' "$W/console-boot.log.txt" &&
			echo "   ok - the initramfs registered and sealed ps3db's ranges" || { echo "   FAIL - no seal in the boot"; rc=1; }
		CMD0=$CMD; CMD=${CMD% e2e_guard=1}
		refused "guard=required, the kernel has none" "$W/ufs.img" "for a kernel with it" ps3ssd.image=/linux/ssdtest.img
		CMD=$CMD0
		for part in 1 2; do
			cp "$W/orig.img" "$W/ufs.img"
			rm -f "$W/whole.img"; truncate -s 8m "$W/whole.img"
			timeout 600 qemu-system-x86_64 -enable-kvm -cpu host -m 1024 -smp 2 -nographic -no-reboot \
				-kernel "$KERNEL" -initrd "$W/initrd.gz" \
				-append "console=ttyS0 panic=-1 loglevel=4 e2e_guest=$ROOT/tools/ssdimg/vm-guest-guard.sh e2e_root=$ROOT e2e_work=$W e2e_out=1 e2e_part=$part" \
				-drive file="$W/ufs.img",if=virtio,format=raw,cache=none \
				-drive file="$W/whole.img",if=virtio,format=raw \
				-virtfs local,path=/,mount_tag=hostroot,security_model=none,readonly=on \
				-virtfs local,path="$W/out",mount_tag=out,security_model=none \
				> "$W/console-g$part.log" 2>&1
			tr -d '\r' < "$W/console-g$part.log" | grep -E '^(E2E|ok|FAIL)' | sed 's/^/   /'
			grep -q "^E2E G$part PASS" <(tr -d '\r' < "$W/console-g$part.log") ||
				{ echo "== FAILED (guard guest $part): console tail:"; tr -d '\r' < "$W/console-g$part.log" | tail -40; rc=1; }
			if [ $part = 1 ]; then
				if n=$(outside_same "$W/out/ssdtest.map.json" "$W/ufs.img" "$W/orig.img") && [ "$n" -gt 0 ]; then
					echo "   ok - outside the image's mapped blocks the disk is the original ($n mapped ranges written)"
				else echo "   FAIL - $n"; rc=1; fi
			elif cmp -s "$W/ufs.img" "$W/orig.img"; then echo "   ok - the disk is the original, byte for byte"
			else echo "   FAIL - the sysfs guest changed the disk"; rc=1; fi
		done
	fi

	[ $first = 1 ] || continue
	first=0
	# refused boots: the reason on the console, the rescue shell, the disk image unchanged
	echo "== Phase B: refused boots"
	refused "no boot map for the image" "$W/ufs.img" "no boot map for ps3ssd.image=/linux/other.img" ps3ssd.image=/linux/other.img
	cp "$W/ufs.img" "$W/hdr.img"
	python3 -I tools/ssdimg/vm-hdrbreak.py "$W/out/ssdtest.map.json" "$W/hdr.img" || rc=1
	refused "a header block no longer stamped" "$W/hdr.img" "header area's stamps don't read back" ps3ssd.image=/linux/ssdtest.img
	head -c 2000000 /dev/urandom > "$W/src/linux/aaa-before.dat"   # the image's blocks move
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$W:/w" "$IMG_BUILDER" \
		/w/makefs -t ffs -B be -s 64m -o version=2,bsize=$b,fsize=$f /w/moved.img /w/src > "$W/makefs.log" 2>&1 ||
		{ cat "$W/makefs.log"; exit 1; }
	refused "the file moved (UFS rebuilt)" "$W/moved.img" "CHANGED" ps3ssd.image=/linux/ssdtest.img
	rm -f "$W/hdr.img" "$W/moved.img" "$W/neg.before.img" "$W/ufs.before.img"
done

# The internal install (docs/release/ALPHA.md M1): the generic initramfs (no map, no kernel release,
# guard required) with a third disk as dev_hdd1 (FAT) holding the PC's upload: the rootfs tarball and
# the install manifest. The first boot maps and verifies the stamped image, saves the map to dev_hdd1,
# makes ext4 and unpacks; the second boots as Phase B from the saved map. Refused: dev_hdd1 not FAT,
# a manifest whose image size isn't the file's. The PS3's geometry, behind the guard.
if [ $PHASEB = 1 ] && [ $GUARDSIM = 1 ] && command -v mkfs.vfat >/dev/null && command -v mcopy >/dev/null; then
	echo "== the internal install: generic initramfs, the first boot installs, the second boots"
	IU=4f5a1b2c-3d4e-4f50-8a6b-7c8d9e0f1a2b
	rm -rf "$W/src"; mkdir -p "$W/src/linux" "$W/src/home/00000001"
	python3 tools/ssdimg/stamp.py dry-run --mib 300 --uuid $IU --out "$W/src/linux/walnut.img" 2>/dev/null || exit 1
	head -c 3000000 /dev/urandom > "$W/src/home/00000001/save.dat"
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$W:/w" "$IMG_BUILDER" \
		/w/makefs -t ffs -B be -s 400m -o version=2,bsize=16384,fsize=4096 /w/ufs.img /w/src > "$W/makefs.log" 2>&1 ||
		{ cat "$W/makefs.log"; exit 1; }
	cp "$W/ufs.img" "$W/orig.img"
	rm -f "$W/whole.img"; truncate -s 8m "$W/whole.img"
	rm -rf "$W/testroot"; mkdir -p "$W/testroot"/{bin,sbin,dev,proc,sys,run,tmp,etc/default,mnt}
	cp "$W/busybox" "$W/testroot/bin/"
	for a in sh cat grep awk blockdev mount umount sync poweroff dd stat tr echo test; do ln -s busybox "$W/testroot/bin/$a"; done
	install -m 755 tools/ssdimg/vm-testroot-walnut.sh "$W/testroot/sbin/init"
	echo "tmpfs /tmp tmpfs defaults 0 0" > "$W/testroot/etc/fstab"
	echo walnut > "$W/testroot/etc/walnut-testroot"
	tar --owner=0 --group=0 -C "$W/testroot" -cf - . | xz -1 > "$W/rootfs.tar.xz" || exit 1
	python3 -I -c '
import sys, hashlib, uuid
sys.path.insert(0, "tools/ssdimg")
import stamp
w, iu, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
r = w + "/rootfs.tar.xz"
sys.stdout.write(stamp.manifest("/linux/walnut.img", uuid.UUID(iu), size, 16, "/linux/install/rootfs.tar.xz",
    len(open(r, "rb").read()), stamp.file_sha(r), stamp.unpacked_size(r)))' "$W" $IU $((300 << 20)) > "$W/install.conf" || exit 1
	mkhdd1() {	# mkhdd1 FILE MANIFEST: dev_hdd1 as GameOS leaves it plus the PC's upload
		rm -f "$1"; truncate -s 64m "$1"
		mkfs.vfat -F 32 -n PS3_HDD1 "$1" > /dev/null || exit 1
		mmd -i "$1" ::/linux ::/linux/install ::/cache || exit 1
		mcopy -i "$1" "$W/rootfs.tar.xz" ::/linux/install/rootfs.tar.xz || exit 1
		mcopy -i "$1" "$2" ::/linux/install/install.conf || exit 1
	}
	mkhdd1 "$W/hdd1.img" "$W/install.conf"
	rm -rf "$W/from"
	docker run --rm --network none -u "$(id -u):$(id -g)" -v "$ROOT:/work" -w /work \
		-e ARCH=amd64 -e RELEASE="$KVER" -e MODDIR="$REL_W/initrd/mods" -e BUSYBOX_DEB="$BB_AMD64" -e GUARD=required \
		-e E2FSPROGS_TAR="$(ls build/downloads/initrd/e2fsprogs-*.tar.xz | tail -n 1)" \
		-e GNUTAR_TAR="$(ls build/downloads/initrd/tar-*.tar.xz | tail -n 1)" \
		-e OUT="$REL_W/from/initrd-walnut-amd64.img" -e INITRD_BUILT=vm-e2e -e SOURCE_DATE_EPOCH=0 \
		"$IMG_BUILDER" tools/ssdimg/build-initrd.sh > "$W/build-initrd.log" 2>&1 ||
		{ cat "$W/build-initrd.log"; echo "generic initramfs build failed"; exit 1; }
	grep '^>>' "$W/build-initrd.log" | sed 's/^/   /'
	cat "$W/from/initrd-walnut-amd64.img" "$W/e2e-wrap.cpio" > "$W/initrd-test.img"
	WCMD="rdinit=/init.e2e root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=/linux/walnut.img console=ttyS0 panic=-1 loglevel=4 e2e_guard=1"
	wboot() {	# wboot LOG UFS HDD1 PATTERN SECONDS
		boot_until "$1" "$4" "$5" -kernel "$KERNEL" -initrd "$W/initrd-test.img" -append "$WCMD" \
			-drive file="$2",if=virtio,format=raw,cache=none -drive file="$W/whole.img",if=virtio,format=raw \
			-drive file="$3",if=virtio,format=raw
	}
	hdd1_has() { mdir -b -i "$W/hdd1.img" "$1" > /dev/null 2>&1; }
	wboot "$W/console-w1.log" "$W/ufs.img" "$W/hdd1.img" 'E2E BOOT' 300
	grep -E '^(ps3ssd|ok|FAIL|E2E)' "$W/console-w1.log.txt" | sed 's/^/   /'
	if grep -q '^E2E BOOT PASS' "$W/console-w1.log.txt"; then echo "   ok - the first boot installed and booted"
	else echo "== FAILED (install boot): console tail:"; tail -50 "$W/console-w1.log.txt"; rc=1; fi
	hdd1_has ::/linux/walnut/boot.map && hdd1_has ::/linux/walnut/boot.map.sha256 &&
		echo "   ok - dev_hdd1: the map saved in /linux/walnut" || { echo "   FAIL - no map on dev_hdd1"; rc=1; }
	st=$(mtype -i "$W/hdd1.img" ::/linux/walnut/state 2>/dev/null)
	case $st in "installed $IU "*) echo "   ok - dev_hdd1: state installed" ;; *) echo "   FAIL - state: $st"; rc=1 ;; esac
	hdd1_has ::/linux/install/install.done && ! hdd1_has ::/linux/install/install.conf &&
		echo "   ok - dev_hdd1: the manifest renamed install.done" || { echo "   FAIL - manifest not renamed"; rc=1; }
	hdd1_has ::/linux/install/rootfs.tar.xz && { echo "   FAIL - the rootfs tarball is still there"; rc=1; } ||
		echo "   ok - dev_hdd1: the rootfs tarball deleted"
	hdd1_has ::/cache && echo "   ok - dev_hdd1: GameOS's own directory untouched" || { echo "   FAIL - /cache gone"; rc=1; }
	wboot "$W/console-w2.log" "$W/ufs.img" "$W/hdd1.img" 'E2E BOOT' 120
	if grep -q '^E2E BOOT PASS' "$W/console-w2.log.txt" && grep -q 'rechecking' "$W/console-w2.log.txt" &&
	   ! grep -q 'installing' "$W/console-w2.log.txt"; then
		echo "   ok - the second boot: recheck against the saved map, no install"
	else echo "== FAILED (second boot): console tail:"; tail -40 "$W/console-w2.log.txt"; rc=1; fi
	wrefused() {	# wrefused WHAT HDD1 PATTERN: a fresh UFS, the rescue shell, the UFS unchanged
		cp "$W/orig.img" "$W/neg.img"
		wboot "$W/console-wn.log" "$W/neg.img" "$2" "reboot -f" 90
		if grep -q "$3" "$W/console-wn.log.txt" && grep -q 'NOT started' "$W/console-wn.log.txt" &&
		   ! grep -q 'E2E BOOT' "$W/console-wn.log.txt" && cmp -s "$W/neg.img" "$W/orig.img"; then
			echo "   ok - refused: $1"
		else echo "   FAIL - $1: console tail:"; tail -30 "$W/console-wn.log.txt"; rc=1; fi
	}
	rm -f "$W/notfat.img"; truncate -s 64m "$W/notfat.img"
	wrefused "dev_hdd1 is not FAT" "$W/notfat.img" "not GameOS's dev_hdd1"
	cmp -s "$W/notfat.img" <(head -c $((64 << 20)) /dev/zero) && echo "   ok - the non-FAT dev_hdd1 not written" ||
		{ echo "   FAIL - the non-FAT dev_hdd1 was written"; rc=1; }
	sed "s/^image_size .*/image_size $((400 << 20))/" "$W/install.conf" | head -n -1 > "$W/bad.body"
	{ cat "$W/bad.body"; echo "sha256 $(sha256sum < "$W/bad.body" | cut -d' ' -f1)"; } > "$W/bad.conf"
	mkhdd1 "$W/hdd1-bad.img" "$W/bad.conf"
	wrefused "the manifest's image size isn't the file's" "$W/hdd1-bad.img" "the install says"
	rm -f "$W/neg.img" "$W/notfat.img" "$W/hdd1-bad.img" "$W/bad.body" "$W/bad.conf"
else
	echo "(the internal install's test skipped: needs Phase B, ps3guard_sim, mkfs.vfat and mtools)"
fi
exit $rc
