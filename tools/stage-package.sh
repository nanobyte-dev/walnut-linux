#!/bin/sh
# Stages one of our packages' files under <root> (a fresh directory), in their installed paths:
#   tools/stage-package.sh <package> <root>
# packages: ps3-kernel ps3-mesa (transitional) (the others: <pkg>/debian)
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
ps3-mesa)	# transitional and empty: our Mesa is now Debian's packages rebuilt (mesa/), the GL tests
	# are in ps3-gpu-tools; upgrading to it removes /usr/lib/ps3-mesa and its ld.so.conf.d entry
	install -d "$R/usr/share/doc/ps3-mesa" ;;
*)
	echo "unknown package: $P" >&2; exit 1 ;;
esac
