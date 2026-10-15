#!/bin/sh
# Stages one of our packages' files under <root> (a fresh directory), in their installed paths:
#   tools/stage-package.sh <package> <root>
# packages: ps3-kernel ps3rsx ps3-boot ps3-gpu-tools ps3-mesa classicube ps3-spu
# tools/build-debs.sh turns each staged tree into a .deb (docs/PLAN.md item 7).
# Env: BUILD (build dir), MESA_PREFIX (where Mesa was built to live), KERNEL_RELEASE, KERNEL_ARGS.
set -eu
P="$1"; R="$2"
B="${BUILD:-build}"
K="$B/kernel/stage"

# the package's commands on PATH: /usr/bin links to /usr/lib/ps3/bin
link_commands() {
	install -d "$R/usr/bin"
	for f in "$R"/usr/lib/ps3/bin/*; do
		ln -s "../lib/ps3/bin/${f##*/}" "$R/usr/bin/${f##*/}"
	done
}

case "$P" in
ps3-kernel)	# the kernel and its modules; Petitboot boots /boot/vmlinux-* (kboot.conf: image)
	install -d "$R/boot" "$R/lib/modules"
	install -m 644 "$K/boot/vmlinux-$KERNEL_RELEASE" "$K/boot/config-$KERNEL_RELEASE" \
		"$K/boot/System.map-$KERNEL_RELEASE" "$R/boot/"
	cp -a "$K/lib/modules/$KERNEL_RELEASE" "$R/lib/modules/"
	# depmod output comes from the postinst, not the package
	rm -f "$R/lib/modules/$KERNEL_RELEASE"/modules.*.bin "$R/lib/modules/$KERNEL_RELEASE"/modules.dep \
		"$R/lib/modules/$KERNEL_RELEASE"/modules.alias "$R/lib/modules/$KERNEL_RELEASE"/modules.symbols \
		"$R/lib/modules/$KERNEL_RELEASE"/modules.devname "$R/lib/modules/$KERNEL_RELEASE"/modules.softdep \
		"$R/lib/modules/$KERNEL_RELEASE"/modules.weakdep
	rm -f "$R/lib/modules/$KERNEL_RELEASE/build" "$R/lib/modules/$KERNEL_RELEASE/source"
	# its own boot arguments, for ps3-update-kboot (the boot menu entry)
	install -d "$R/usr/lib/ps3/boot"
	echo "$KERNEL_ARGS" > "$R/usr/lib/ps3/boot/$KERNEL_RELEASE.args" ;;
ps3-boot)	# writes Petitboot's boot menu from the installed kernels
	install -d "$R/usr/sbin"
	install -m 755 ps3-boot/ps3-update-kboot "$R/usr/sbin/" ;;
ps3rsx)		# the RSX DRM driver, loaded at boot (before the desktop starts X)
	install -d "$R/lib/modules/$KERNEL_RELEASE/updates" "$R/usr/lib/modules-load.d"
	install -m 644 "$B/gpu-ps3rsx/ps3rsx.ko" "$R/lib/modules/$KERNEL_RELEASE/updates/"
	# per release, so the packages for several kernels install side by side
	echo ps3rsx > "$R/usr/lib/modules-load.d/ps3rsx-$KERNEL_RELEASE.conf" ;;
ps3-gpu-tools)	# RSX probes and tests (ps3-gpu-tools), the Phase 0/1 probe modules
	install -d "$R/usr/lib/ps3/bin" "$R/usr/lib/ps3/kmod"
	install -m 755 "$B"/gpu/* ps3-gpu-tools/scripts/*.sh "$R/usr/lib/ps3/bin/"
	install -m 644 "$B"/gpu-kmod/*.ko "$R/usr/lib/ps3/kmod/"
	link_commands ;;
ps3-mesa)	# our Mesa fork and its GL tests; ld.so.conf.d makes everything (setuid Xorg too) use it
	install -d "$R$MESA_PREFIX/bin" "$R/etc/ld.so.conf.d"
	cp -a "$B/mesa/install$MESA_PREFIX/." "$R$MESA_PREFIX/"
	install -m 755 "$B"/mesa/tests/* "$R$MESA_PREFIX/bin/"
	echo "$MESA_PREFIX/lib" > "$R/etc/ld.so.conf.d/00-ps3mesa.conf" ;;
classicube)	# the game, its sounds and music, default options; /usr/games/classicube runs it
	# from a per-user directory (it keeps options, maps and screenshots next to itself)
	install -d "$R/usr/lib/classicube/texpacks" "$R/usr/lib/classicube/audio" "$R/usr/games" \
		"$R/usr/share/applications"
	install -m 755 "$B/classicube/install/ClassiCube" "$R/usr/lib/classicube/"
	install -m 644 "$B/classicube/install/texpacks/classicube.zip" "$R/usr/lib/classicube/texpacks/"
	install -m 644 "$B"/classicube/audio/* "$R/usr/lib/classicube/audio/"
	install -m 644 classicube/options.txt "$R/usr/lib/classicube/"
	install -m 755 classicube/classicube.sh "$R/usr/games/classicube"
	install -m 644 classicube/classicube.desktop "$R/usr/share/applications/" ;;
ps3-spu)	# SPU runner and tests; spufs loaded and mounted at /spu at boot
	install -d "$R/usr/lib/ps3/bin" "$R/usr/lib/ps3/spu" "$R/usr/lib/modules-load.d" \
		"$R/usr/lib/systemd/system/local-fs.target.wants"
	install -m 755 "$B/spu-tests/spurun" "$R/usr/lib/ps3/bin/"
	link_commands
	install -m 644 "$B"/spu-tests/*.spu "$R/usr/lib/ps3/spu/"
	echo spufs > "$R/usr/lib/modules-load.d/spufs.conf"
	printf '%s\n' '[Unit]' 'Description=SPU file system (the Cell SPEs, spufs)' \
		'After=systemd-modules-load.service' '' '[Mount]' 'What=spufs' 'Where=/spu' 'Type=spufs' \
		'Options=mode=1777' > "$R/usr/lib/systemd/system/spu.mount"
	ln -s ../spu.mount "$R/usr/lib/systemd/system/local-fs.target.wants/spu.mount" ;;
*)
	echo "unknown package: $P" >&2; exit 1 ;;
esac
