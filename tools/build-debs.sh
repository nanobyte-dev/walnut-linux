#!/bin/bash
# Builds our packages (.debs) from the build outputs and indexes them as an apt repository:
#   tools/build-debs.sh <package>...      -> build/repo/<package>_<version>_ppc64.deb, Packages
# Runs as the user in the repo builder container. Files are root-owned in the packages; builds
# are reproducible (dpkg-deb honours SOURCE_DATE_EPOCH). docs/PLAN.md item 7.
# Env: BUILD MESA_PREFIX KERNEL_VERSION KERNEL_RELEASE MESA_VERSION CLASSICUBE_COMMIT PKG_N
set -euo pipefail
B="${BUILD:-build}"
REPO="$B/repo"
STAGE="$B/debs"
mkdir -p "$REPO"
export SOURCE_DATE_EPOCH
MAINT="PS3 Linux project <ps3@localhost>"

for P in "$@"; do
	R="$STAGE/$P"
	rm -rf "$R"; mkdir -p "$R/DEBIAN"
	BUILD="$B" tools/stage-package.sh "$P" "$R"
	depends=""; postinst=""; postrm=""; triggers=""; name="$P"; arch=ppc64
	case "$P" in
	ps3-kernel)	# one package per release: kernels install side by side, like linux-image-*
		name="ps3-kernel-$KERNEL_RELEASE"
		version="$KERNEL_VERSION+ps3.$PKG_N"
		depends="ps3-boot"
		desc="Linux kernel $KERNEL_RELEASE for the PS3 (OtherOS++), with the RSX sharing patches"
		postinst="depmod -a $KERNEL_RELEASE; ps3-update-kboot"
		postrm="if [ \"\$1\" = remove ] || [ \"\$1\" = purge ]; then rm -f /lib/modules/$KERNEL_RELEASE/modules.*; command -v ps3-update-kboot >/dev/null && ps3-update-kboot; fi; true" ;;
	ps3rsx)		# built against one kernel release
		name="ps3rsx-$KERNEL_RELEASE"
		version="0.$PKG_N"
		depends="ps3-kernel-$KERNEL_RELEASE"
		desc="DRM driver for the PS3's RSX GPU (registers as nouveau), for $KERNEL_RELEASE"
		postinst="depmod -a $KERNEL_RELEASE" ;;
	ps3-boot)
		version="0.$PKG_N"
		arch=all
		desc="Petitboot boot menu (/etc/kboot.conf) from the installed kernels" ;;
	ps3-gpu-tools)
		version="0.$PKG_N"
		desc="RSX probes, driver tests and test scripts" ;;
	ps3-mesa)
		version="$MESA_VERSION+ps3.$PKG_N"
		desc="Mesa with the nv30 fixes for the RSX (fork of $MESA_VERSION)"
		triggers="activate-noawait ldconfig" ;;
	classicube)
		version="0+git.${CLASSICUBE_COMMIT:0:7}+ps3.$PKG_N"
		depends="libsdl3-0, libopenal1"
		desc="ClassiCube block building game, with sounds and music" ;;
	ps3-spu)
		version="0.$PKG_N"
		desc="SPU program runner (spufs) and SPU tests; mounts spufs at /spu" ;;
	esac
	{
		echo "Package: $name"
		echo "Version: $version"
		echo "Architecture: $arch"
		echo "Maintainer: $MAINT"
		echo "Installed-Size: $(du -sk --exclude=DEBIAN "$R" | cut -f1)"
		[ -z "$depends" ] || echo "Depends: $depends"
		echo "Section: misc"
		echo "Priority: optional"
		echo "Description: $desc"
	} > "$R/DEBIAN/control"
	if [ -n "$postinst" ]; then
		printf '#!/bin/sh\nset -e\n%s\n' "$postinst" > "$R/DEBIAN/postinst"
		chmod 755 "$R/DEBIAN/postinst"
	fi
	if [ -n "$postrm" ]; then
		printf '#!/bin/sh\nset -e\n%s\n' "$postrm" > "$R/DEBIAN/postrm"
		chmod 755 "$R/DEBIAN/postrm"
	fi
	[ -z "$triggers" ] || echo "$triggers" > "$R/DEBIAN/triggers"
	# the previous version of this package is moved aside, never overwritten
	for old in "$REPO/${P}_"*.deb; do
		[ -e "$old" ] || continue
		mkdir -p "$B/old/repo" && mv "$old" "$B/old/repo/"
	done
	dpkg-deb --root-owner-group -Zxz --build "$R" "$REPO/${P}_${version}_$arch.deb" > /dev/null
	echo ">> $REPO/${P}_${version}_$arch.deb ($name)"
done

# The repository index (apt-get with "deb [trusted=yes] file:<repo> ./")
cd "$REPO"
apt-ftparchive packages . > Packages
apt-ftparchive release . > Release
