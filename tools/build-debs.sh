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
	ps3-mesa)
		# above every old build (they used the global commit count) and below nothing we publish
		version="$MESA_VERSION+ps3.99999"
		arch=all
		depends="mesa-libgallium (>= 26.2.4-1+ps3), libgl1-mesa-dri (>= 26.2.4-1+ps3), libegl-mesa0 (>= 26.2.4-1+ps3), libglx-mesa0 (>= 26.2.4-1+ps3), libgbm1 (>= 26.2.4-1+ps3), ps3-gpu-tools (>= 0.146~)"
		desc="transitional package: our Mesa is now Debian's mesa rebuilt for the PS3; can be removed"
		triggers="activate-noawait ldconfig" ;;
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
