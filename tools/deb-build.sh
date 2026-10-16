#!/bin/bash
# deb-build.sh <pkg>: builds our source package (build/src/<pkg>/*.dsc, from deb-prepare.sh) natively
# in a fresh ppc64 chroot under qemu-user (inside the deb builder, privileged, as root).
# The chroot with the package's build dependencies installed is cached per debian/control
# (build/chroot/<pkg>-deps-<hash>.tar); ccache persists in build/ccache (5 GB max).
# Output: build/debs/<pkg>/ (replaced), plus the build log.
# Env: OWNER JOBS DEB_BUILD_EXTRA (e.g. noautodbgsym)
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
PKG=$1
. "$PKG/package.conf"
DSC=$(ls build/src/"$PKG"/*.dsc | head -1)
if [ "$TYPE" = native ]; then
	UPVER=$(basename "$DSC" .dsc); UPVER=${UPVER#*_}
elif [ "$TYPE" = debsrc ]; then
	UPVER=${VERSION%-*}; UPVER=${UPVER#*:}
fi	# TYPE=upstream: UPVER from package.conf
OUT=build/debs/$PKG
rm -rf "$OUT"; mkdir -p "$OUT"
# BUILD=indep: everything builds right here (amd64 deb builder), no qemu. BUILD=split: the
# Architecture: all packages here (-A, with INDEP_PATH on PATH, e.g. our spu-elf toolchain),
# the ppc64 ones in the chroot (-B). BUILD=native (default): all in the chroot.
# BUILD=cross: cross-built here for ppc64 (kernel modules against our kernel tree; the build
# dependencies are this builder's own, not checked).
if [ "${BUILD:-native}" = indep ] || [ "${BUILD:-native}" = split ] || [ "${BUILD:-native}" = cross ]; then
	W=build/chroot/indep-$PKG
	what=-b; [ "$BUILD" = split ] && what=-A; [ "$BUILD" = cross ] && what="-a ppc64 -b -d"
	rm -rf "$W"; mkdir -p "$W"
	(cd "$W" && dpkg-source -x "../../../$DSC" src >/dev/null && cd src && \
		PATH="${INDEP_PATH:+$INDEP_PATH:}$PATH" DEB_BUILD_OPTIONS=nocheck \
		dpkg-buildpackage $what -uc -us > ../build.log 2>&1) \
		|| { tail -30 "$W/build.log"; exit 1; }
	mv "$W"/*.deb "$W"/*.buildinfo "$W"/*.changes "$OUT/" 2>/dev/null || true
	mv "$W/build.log" "$OUT/build-indep.log"
	rm -rf "$W"
	chown -R "$OWNER" "$OUT"
	if [ "$BUILD" != split ]; then ls "$OUT"/*.deb; exit 0; fi
fi
ARCHBUILD=-b; [ "${BUILD:-native}" = split ] && ARCHBUILD=-B
OUT=build/debs/$PKG
R=$PWD/build/chroot/run-$PKG
mkdir -p build/ccache build/chroot

# Unmount everything under the chroot. Nothing may walk the tree (chown -R, rm -r) while /sys or
# /dev are bound into it: in this privileged container they are the host's.
unmount_all() {
	for m in build/ccache dev proc sys; do umount -R -l "$R/$m" 2>/dev/null || true; done
	if grep -q " $R/" /proc/mounts; then echo "!! still mounted under $R" >&2; return 1; fi
}
cleanup() {
	unmount_all && rm -rf --one-file-system "$R"
}
trap cleanup EXIT
cleanup

hash=$(sha256sum "build/src/$PKG/$SOURCE-$UPVER/debian/control" | cut -c1-12)
DEPS=build/chroot/$PKG-deps-$hash.tar
mkdir -p "$R"
if [ -e "$DEPS" ]; then
	tar -xf "$DEPS" -C "$R"
else
	tar -xf build/chroot/sid-ppc64.tar -C "$R"
fi
mkdir -p "$R/proc" "$R/sys" "$R/dev"
mount -t proc proc "$R/proc"; mount -t sysfs -o ro sysfs "$R/sys"; mount --rbind /dev "$R/dev"
mkdir -p "$R/build/ccache" "$R/build/reproducible-path"
mount --bind build/ccache "$R/build/ccache"
cp /etc/resolv.conf "$R/etc/resolv.conf"
cp build/src/"$PKG"/*.dsc build/src/"$PKG"/*.tar.* "$R/build/" 2>/dev/null || true
for f in build/src/"$PKG"/*.orig*.tar.*; do [ ! -e "$f" ] || cp -L "$f" "$R/build/"; done

in_chroot() { chroot "$R" /usr/bin/env -i PATH=/usr/lib/ccache:/usr/sbin:/usr/bin:/sbin:/bin \
	HOME=/root LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive "$@"; }

T0=$(date +%s)
in_chroot sh -c "cd /build/reproducible-path && dpkg-source -x ../$(basename "$DSC") $SOURCE-$UPVER >/dev/null"
if [ ! -e "$DEPS" ]; then
	in_chroot apt-get update -qq
	in_chroot sh -c "cd /build/reproducible-path/$SOURCE-$UPVER && eatmydata apt-get build-dep -y -q ./" \
		| grep -E "^(Need to get|After this|E:)" || true
	in_chroot apt-get clean
	rm -rf "$R/build/reproducible-path/$SOURCE-$UPVER.deps-cache"
	# cache the chroot with the build dependencies, without the source tree
	rm -f build/chroot/"$PKG"-deps-*.tar
	tar --exclude=./build --exclude=./proc --exclude=./sys --exclude=./dev -cf "$DEPS.tmp" -C "$R" . && mv "$DEPS.tmp" "$DEPS"
	in_chroot sh -c "cd /build/reproducible-path && rm -rf $SOURCE-$UPVER && dpkg-source -x ../$(basename "$DSC") $SOURCE-$UPVER >/dev/null"
fi
T1=$(date +%s)
echo ">> build dependencies ready in $((T1 - T0)) s"

in_chroot sh -c "ccache -M 5G >/dev/null; cd /build/reproducible-path/$SOURCE-$UPVER && \
	CCACHE_DIR=/build/ccache CCACHE_BASEDIR=/build/reproducible-path CCACHE_NOHASHDIR=1 \
	DEB_BUILD_OPTIONS='nocheck parallel=${JOBS:-16} ${DEB_BUILD_EXTRA:-}' \
	eatmydata dpkg-buildpackage $ARCHBUILD -uc -us" > "build/chroot/$PKG-build.log" 2>&1 \
	|| { tail -40 "build/chroot/$PKG-build.log"; exit 1; }
T2=$(date +%s)
echo ">> built in $((T2 - T1)) s"

in_chroot sh -c "CCACHE_DIR=/build/ccache ccache -s" | grep -E "Hits|Misses|Cache size" || true
unmount_all
cp "$R"/build/reproducible-path/*.deb "$R"/build/reproducible-path/*.ddeb "$R"/build/reproducible-path/*.buildinfo \
	"$R"/build/reproducible-path/*.changes "$OUT/" 2>/dev/null || true
mv "build/chroot/$PKG-build.log" "$OUT/build.log"
# only our outputs (never the chroot tree: see unmount_all)
chown -R "$OWNER" "$OUT" build/ccache
chown "$OWNER" build/chroot build/chroot/*.tar build/chroot/*.log 2>/dev/null || true
ls -la "$OUT"
exit
}
