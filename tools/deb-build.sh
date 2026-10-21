#!/bin/bash
# deb-build.sh <pkg>: builds our source package (build/src/<pkg>/*.dsc, from deb-prepare.sh) natively
# in a fresh ppc64 chroot under qemu-user (inside the deb builder, privileged, as root).
# The chroot with the package's build dependencies installed is cached per debian/control
# (build/chroot/<pkg>-deps-<hash>.tar); ccache persists in build/ccache (5 GB max).
# Output: build/debs/<pkg>/ (replaced), plus the build log.
# Env: OWNER JOBS DEB_BUILD_EXTRA (e.g. noautodbgsym) KEEP_TREE (BUILD=cross-linux: keep the build tree)
# package.conf: NATIVE_PROFILES (build profiles for the chroot build), CROSS_PROFILES (cross-linux)
# package.conf CHECK=1: the package's own tests run (DEB_BUILD_OPTIONS without nocheck; luanti's
# --run-unittests, under qemu-user); every other package builds with nocheck
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
PKG=$1
. "$PKG/package.conf"
NOCHECK=nocheck; [ "${CHECK:-0}" = 1 ] && NOCHECK=
DSC=$(ls build/src/"$PKG"/*.dsc | head -1)
if [ "$TYPE" = native ]; then
	UPVER=$(basename "$DSC" .dsc); UPVER=${UPVER#*_}
elif [ "$TYPE" = debsrc ]; then
	UPVER=${VERSION%-*}; UPVER=${UPVER#*:}
fi	# TYPE=upstream: UPVER from package.conf
OUT=build/debs/$PKG${KERNEL_FLAVOUR:+-$KERNEL_FLAVOUR}
rm -rf "$OUT"; mkdir -p "$OUT"
# BUILD=indep: everything builds right here (amd64 deb builder), no qemu. BUILD=split: the
# Architecture: all packages here (-A, with INDEP_PATH on PATH, e.g. our spu-elf toolchain),
# the ppc64 ones in the chroot (-B). BUILD=native (default): all in the chroot.
# BUILD=cross: cross-built here for ppc64 (kernel modules against our kernel tree; the build
# dependencies are this builder's own, not checked).
# BUILD=cross-linux (the kernel, linux/): the kernel packages cross-built here for ppc64 with
# CROSS_PROFILES (build dependencies: this builder's own, not checked; ccache in build/ccache) in
# build/chroot/cross-<pkg> (kept with KEEP_TREE=1), then linux-kbuild (the ppc64 host tools
# linux-headers needs) natively in the chroot below: -B with NATIVE_PROFILES, its .changes,
# .buildinfo and log in build/debs/<pkg>/native/.
if [ "${BUILD:-native}" = cross-linux ]; then
	W=build/chroot/cross-$PKG
	CC_DIR=$PWD/build/ccache
	rm -rf "$W"; mkdir -p "$W" "$CC_DIR"
	T0=$(date +%s)
	(cd "$W" && dpkg-source -x "../../../$DSC" src >/dev/null && cd src && \
		DEB_BUILD_OPTIONS="${NOCHECK:+$NOCHECK }parallel=${JOBS:-16}" DEBIAN_KERNEL_USE_CCACHE=1 \
		CCACHE_DIR="$CC_DIR" CCACHE_BASEDIR="$PWD" CCACHE_NOHASHDIR=1 CCACHE_MAXSIZE=5G \
		dpkg-buildpackage -a ppc64 -b -d -uc -us ${CROSS_PROFILES:+-P$CROSS_PROFILES} > ../build.log 2>&1) \
		|| { tail -40 "$W/build.log"; exit 1; }
	echo ">> cross-built in $(( $(date +%s) - T0 )) s"
	mv "$W"/*.deb "$W"/*.buildinfo "$W"/*.changes "$OUT/"
	mv "$W/build.log" "$OUT/build-cross.log"
	if [ "${KEEP_TREE:-0}" = 1 ]; then chown -R "$OWNER" "$W"; else rm -rf "$W"; fi
	chown -R "$OWNER" "$OUT" "$CC_DIR"
fi
if [ "${BUILD:-native}" = indep ] || [ "${BUILD:-native}" = split ] || [ "${BUILD:-native}" = cross ]; then
	W=build/chroot/indep-$PKG
	what=-b; [ "$BUILD" = split ] && what=-A; [ "$BUILD" = cross ] && what="-a ppc64 -b -d"
	rm -rf "$W"; mkdir -p "$W"
	(cd "$W" && dpkg-source -x "../../../$DSC" src >/dev/null && cd src && \
		PATH="${INDEP_PATH:+$INDEP_PATH:}$PATH" DEB_BUILD_OPTIONS="$NOCHECK" \
		dpkg-buildpackage $what -uc -us > ../build.log 2>&1) \
		|| { tail -30 "$W/build.log"; exit 1; }
	mv "$W"/*.deb "$W"/*.buildinfo "$W"/*.changes "$OUT/" 2>/dev/null || true
	mv "$W/build.log" "$OUT/build-indep.log"
	rm -rf "$W"
	chown -R "$OWNER" "$OUT"
	if [ "$BUILD" != split ]; then ls "$OUT"/*.deb; exit 0; fi
fi
ARCHBUILD=-b; [ "${BUILD:-native}" = split ] && ARCHBUILD=-B
[ "${BUILD:-native}" = cross-linux ] && ARCHBUILD=-B
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
# packages with LOCAL_DEPS start from the bare chroot every time: a cached one keeps the previous
# build of our own dependency, which apt-get build-dep doesn't replace when it already satisfies
# the build dependency (luanti got Debian's libluajit instead of ours)
[ -z "${LOCAL_DEPS:-}" ] || rm -f "$DEPS"
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
# Build dependencies among our own builds (package.conf LOCAL_DEPS="<dir> ...": build/debs/<dir>),
# served to apt in the chroot as a flat repository
if [ -n "${LOCAL_DEPS:-}" ]; then
	mkdir -p "$R/build/local"
	for d in $LOCAL_DEPS; do
		find "build/debs/$d" -maxdepth 1 -name '*.deb' ! -name '*-dbgsym_*' -exec cp -t "$R/build/local/" {} +
	done
	(cd "$R/build/local" && apt-ftparchive packages . > Packages)
	echo 'deb [trusted=yes] file:/build/local ./' > "$R/etc/apt/sources.list.d/zz-local.list"
fi
cp build/src/"$PKG"/*.dsc build/src/"$PKG"/*.tar.* "$R/build/" 2>/dev/null || true
for f in build/src/"$PKG"/*.orig*.tar.*; do [ ! -e "$f" ] || cp -L "$f" "$R/build/"; done

in_chroot() { chroot "$R" /usr/bin/env -i PATH=/usr/lib/ccache:/usr/sbin:/usr/bin:/sbin:/bin \
	HOME=/root LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive "$@"; }

T0=$(date +%s)
in_chroot sh -c "cd /build/reproducible-path && dpkg-source -x ../$(basename "$DSC") $SOURCE-$UPVER >/dev/null"
if [ ! -e "$DEPS" ]; then
	in_chroot apt-get update -qq
	# a failed build-dep stops here, nothing gets cached, and the end of apt's output is shown (dpkg's
	# own errors aren't E: lines)
	rc=0
	in_chroot sh -c "cd /build/reproducible-path/$SOURCE-$UPVER && eatmydata apt-get build-dep -y -q ${NATIVE_PROFILES:+-P $NATIVE_PROFILES} ./" \
		> "$R/build/build-dep.log" 2>&1 || rc=$?
	grep -E "^(Need to get|After this|E:)" "$R/build/build-dep.log" || true
	if [ "$rc" != 0 ]; then
		echo "!! apt-get build-dep failed ($rc): no deps chroot cached. Its last lines:" >&2
		tail -n 60 "$R/build/build-dep.log" >&2
		exit 1
	fi
	in_chroot apt-get clean
	rm -rf "$R/build/reproducible-path/$SOURCE-$UPVER.deps-cache"
	# cache the chroot with the build dependencies, without the source tree (not with LOCAL_DEPS)
	rm -f build/chroot/"$PKG"-deps-*.tar
	if [ -z "${LOCAL_DEPS:-}" ]; then
		tar --exclude=./build --exclude=./proc --exclude=./sys --exclude=./dev -cf "$DEPS.tmp" -C "$R" . && mv "$DEPS.tmp" "$DEPS"
	fi
	in_chroot sh -c "cd /build/reproducible-path && rm -rf $SOURCE-$UPVER && dpkg-source -x ../$(basename "$DSC") $SOURCE-$UPVER >/dev/null"
fi
T1=$(date +%s)
echo ">> build dependencies ready in $((T1 - T0)) s"

in_chroot sh -c "ccache -M 5G >/dev/null; cd /build/reproducible-path/$SOURCE-$UPVER && \
	CCACHE_DIR=/build/ccache CCACHE_BASEDIR=/build/reproducible-path CCACHE_NOHASHDIR=1 \
	DEB_BUILD_OPTIONS='${NOCHECK:+$NOCHECK }parallel=${JOBS:-16} ${DEB_BUILD_EXTRA:-}' \
	eatmydata dpkg-buildpackage $ARCHBUILD -uc -us ${NATIVE_PROFILES:+-P$NATIVE_PROFILES}" > "build/chroot/$PKG-build.log" 2>&1 \
	|| { tail -40 "build/chroot/$PKG-build.log"; exit 1; }
T2=$(date +%s)
echo ">> built in $((T2 - T1)) s"

in_chroot sh -c "CCACHE_DIR=/build/ccache ccache -s" | grep -E "Hits|Misses|Cache size" || true
unmount_all
META=$OUT; [ "${BUILD:-native}" = cross-linux ] && META=$OUT/native && mkdir -p "$META"
cp "$R"/build/reproducible-path/*.deb "$R"/build/reproducible-path/*.ddeb "$OUT/" 2>/dev/null || true
cp "$R"/build/reproducible-path/*.buildinfo "$R"/build/reproducible-path/*.changes "$META/" 2>/dev/null || true
mv "build/chroot/$PKG-build.log" "$META/build.log"
# only our outputs (never the chroot tree: see unmount_all)
chown -R "$OWNER" "$OUT" build/ccache
chown "$OWNER" build/chroot build/chroot/*.tar build/chroot/*.log 2>/dev/null || true
ls -la "$OUT"
exit
}
