#!/bin/bash
# sixaxis-idle-axes.sh: linux/patches/local/0050 (hid-sony starts the Sixaxis sticks centered) in a
# QEMU guest. An x86_64 defconfig kernel from our Linux source tree (make linux-src) with uhid,
# joydev and hid-sony as a module; hid-sony.ko built twice from that tree's hid-sony.c, without the
# patch and with it. The guest's init (sixaxis-uhid-init.c, no libc) loads the module, makes a
# DS3 over uhid (054c:0268 on BUS_USB, the real descriptor) that sends no input report, like a pad
# whose PS button was not pressed, and prints the stick axes (EVIOCGABS, joydev's init events),
# then sends two reports. Expected: stock 0 on the sticks (RESULT idle FAIL), patched 128 (PASS),
# reports followed in both. Needs gcc, cpio, qemu-system-x86_64 (KVM if there).
# WORK (default build/tmp/tests/sixaxis-idle-axes, ~700 MB) holds the kernel build.
set -euo pipefail
cd "$(dirname "$0")/../.."
P=$PWD/linux/patches/local/0050-hid-sony-sixaxis-center-sticks.patch
WORK=$(realpath -m "${WORK:-build/tmp/tests/sixaxis-idle-axes}")
SRC=$(ls -d build/src/linux/linux-*/ 2>/dev/null | head -1)
[ -n "$SRC" ] || { echo "skip: no kernel source tree (make linux-src)"; exit 77; }
SRC=$(realpath "$SRC")
for c in gcc cpio qemu-system-x86_64 patch; do command -v $c >/dev/null || { echo "skip: no $c"; exit 77; }; done
O=$WORK/obj
mkdir -p "$O"

if [ ! -e "$O/Module.symvers" ]; then
	make -s -C "$SRC" O="$O" x86_64_defconfig >/dev/null
	cat > "$WORK/frag.config" <<-'EOF'
	CONFIG_UHID=y
	CONFIG_HID_SONY=m
	CONFIG_SONY_FF=y
	CONFIG_INPUT_JOYDEV=y
	CONFIG_INPUT_EVDEV=y
	CONFIG_NEW_LEDS=y
	CONFIG_LEDS_CLASS=y
	CONFIG_MODULES=y
	CONFIG_MODULE_SIG=n
	CONFIG_DEVTMPFS=y
	CONFIG_BLK_DEV_INITRD=y
	CONFIG_DRM=n
	CONFIG_SOUND=n
	CONFIG_WLAN=n
	CONFIG_WIRELESS=n
	CONFIG_NETDEVICES=n
	CONFIG_DEBUG_INFO_NONE=y
	EOF
	"$SRC/scripts/kconfig/merge_config.sh" -m -O "$O" "$O/.config" "$WORK/frag.config" >/dev/null
	make -s -C "$SRC" O="$O" olddefconfig >/dev/null
	echo "building the test kernel in $O"
	make -s -C "$SRC" O="$O" -j"$(nproc)" bzImage modules
fi

gcc -Wall -Werror -O2 -nostdlib -static -ffreestanding -fno-stack-protector -fno-pie -no-pie -fno-builtin \
	-o "$WORK/init" linux/tests/sixaxis-uhid-init.c

# the tree's hid-sony.c may or may not carry the patch already (a source package made after it)
for v in stock patched; do
	M=$WORK/mod-$v
	rm -rf "$M"
	mkdir -p "$M/drivers/hid"
	cp "$SRC/drivers/hid/hid-sony.c" "$SRC/drivers/hid/hid-ids.h" "$M/drivers/hid/"
	if grep -q sixaxis_center_sticks "$M/drivers/hid/hid-sony.c"; then
		[ $v = patched ] || patch -s -R -p1 -d "$M" < "$P"
	else
		[ $v = stock ] || patch -s -p1 -d "$M" < "$P"
	fi
	mv "$M"/drivers/hid/* "$M/"
	echo 'obj-m := hid-sony.o' > "$M/Kbuild"
	make -s -C "$O" M="$M" W=1 modules 2>&1 | { grep -v 'kernel-doc line' || true; }
	R=$WORK/root-$v
	rm -rf "$R"
	mkdir -p "$R"
	cp "$WORK/init" "$R/init"
	cp "$M/hid-sony.ko" "$R/"
	(cd "$R" && find . | cpio -o -H newc --quiet) > "$WORK/initrd-$v.cpio"
done

kvm=()
[ -w /dev/kvm ] && kvm=(-enable-kvm)
fail=0
for v in stock patched; do
	timeout 120 qemu-system-x86_64 "${kvm[@]}" -m 256 -nographic -no-reboot -kernel "$O/arch/x86/boot/bzImage" \
		-initrd "$WORK/initrd-$v.cpio" -append "console=ttyS0 quiet loglevel=4 panic=-1" 2>&1 |
		tr -d '\r' | sed -n 's/.*\(ds3init: \)/\1/; /ds3init:/,$p' | grep -v 'reboot: Power down' > "$WORK/run-$v.log" || true
	echo "== $v"
	cat "$WORK/run-$v.log"
	idle=$(sed -n 's/^RESULT idle: .*(\(PASS\|FAIL\))$/\1/p' "$WORK/run-$v.log")
	reports=$(sed -n 's/^RESULT reports: .*(\(PASS\|FAIL\))$/\1/p' "$WORK/run-$v.log")
	want=PASS
	[ $v = stock ] && want=FAIL
	[ "$idle" = $want ] && [ "$reports" = PASS ] || { echo "!! $v: idle '$idle' (want $want), reports '$reports'"; fail=1; }
done
[ $fail = 0 ] && echo "ok: stock pad idles at 0 (fully up/left), patched at the center; reports unchanged"
exit $fail
