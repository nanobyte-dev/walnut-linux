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
	depends=""; postinst=""; triggers=""
	case "$P" in
	ps3-kernel)
		version="$KERNEL_VERSION+ps3.$PKG_N"
		desc="Linux kernel for the PS3 (OtherOS++), with the RSX sharing patches"
		postinst="depmod -a $KERNEL_RELEASE" ;;
	ps3rsx)
		version="0.$PKG_N"
		# built against this kernel version (any build of it)
		depends="ps3-kernel (>= $KERNEL_VERSION), ps3-kernel (<< $KERNEL_VERSION.0~)"
		desc="DRM driver for the PS3's RSX GPU (registers as nouveau)"
		postinst="depmod -a $KERNEL_RELEASE" ;;
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
		echo "Package: $P"
		echo "Version: $version"
		echo "Architecture: ppc64"
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
	[ -z "$triggers" ] || echo "$triggers" > "$R/DEBIAN/triggers"
	# the previous version of this package is moved aside, never overwritten
	for old in "$REPO/${P}_"*.deb; do
		[ -e "$old" ] || continue
		mkdir -p "$B/old/repo" && mv "$old" "$B/old/repo/"
	done
	dpkg-deb --root-owner-group -Zxz --build "$R" "$REPO/${P}_${version}_ppc64.deb" > /dev/null
	echo ">> $REPO/${P}_${version}_ppc64.deb"
done

# The repository index (apt-get with "deb [trusted=yes] file:<repo> ./")
cd "$REPO"
apt-ftparchive packages . > Packages
apt-ftparchive release . > Release
