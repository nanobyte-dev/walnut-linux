#!/bin/bash
# xcc-build.sh <pkg>: builds a BUILD=xcc package (package.conf): natively in a fresh ppc64 chroot
# under qemu-user like tools/deb-build.sh, except that the compilers are x86-64 programs: Debian's
# ppc64 cross compiler, mounted read-only at /x86 and linked into the chroot (/usr/local/bin first
# on PATH, its loader and libraries under /lib64 and /usr/lib/x86_64-linux-gnu), so they run at full
# speed while dpkg-buildpackage, cmake, the generators and the debhelper tools run under qemu as in
# any native build. For packages too big for a qemu build (WebKitGTK: about ten times faster).
# Inside the deb builder, privileged, as root. Everything big is under /xcc ($(XCC_ROOT) on the host:
# config.local.mk may put it on another disk): the chroot (run-<pkg>), the build-dependency chroot
# (<pkg>-deps-<hash>.tar) and ccache (ccache/, XCC_CCACHE_SIZE, 20G).
# Output: build/debs/<pkg>/ (replaced) and its build.log.
# The build runs under nice -n 15 and ionice -c3 (the dev PC stays usable).
# Env: OWNER JOBS XCC_TREE (the exported toolchain, a path under /xcc) DEB_BUILD_EXTRA
# package.conf XCC_ENV: more VAR=value for the build (webkit2gtk: WEBKIT_NINJA_LINK_MAX=2, two links at a time)
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
PKG=$1
PD=$(tools/pkg-dir.sh "$PKG")
. "$PD/package.conf"
[ "${BUILD:-}" = xcc ] || { echo "!! $PKG is not BUILD=xcc"; exit 1; }
[ "$TYPE" = debsrc ] || { echo "!! BUILD=xcc is for Debian source packages (TYPE=debsrc)"; exit 1; }
[ -x "$XCC_TREE/usr/bin/powerpc64-linux-gnu-g++-16" ] || { echo "!! no toolchain in $XCC_TREE (make xcc-toolchain)"; exit 1; }
NOCHECK=nocheck; [ "${CHECK:-0}" = 1 ] && NOCHECK=
DSC=$(ls build/src/"$PKG"/*.dsc | head -1)
UPVER=${VERSION%-*}; UPVER=${UPVER#*:}
OUT=build/debs/$PKG
rm -rf "$OUT"; mkdir -p "$OUT"
R=/xcc/run-$PKG
mkdir -p /xcc/ccache

# Unmount everything under the chroot first: nothing may walk the tree while /sys or /dev are bound
unmount_all() {
	for m in x86 build/ccache dev proc sys; do umount -R -l "$R/$m" 2>/dev/null || true; done
	if grep -q " $R/" /proc/mounts; then echo "!! still mounted under $R" >&2; return 1; fi
}
cleanup() { unmount_all && rm -rf --one-file-system "$R"; }
trap cleanup EXIT
cleanup

hash=$(sha256sum "build/src/$PKG/$SOURCE-$UPVER/debian/control" | cut -c1-12)
DEPS=/xcc/$PKG-deps-$hash.tar
mkdir -p "$R"
if [ -e "$DEPS" ]; then tar -xf "$DEPS" -C "$R"; else tar -xf build/chroot/sid-ppc64.tar -C "$R"; fi
mkdir -p "$R/proc" "$R/sys" "$R/dev" "$R/x86" "$R/build/ccache" "$R/build/reproducible-path"
mount -t proc proc "$R/proc"; mount -t sysfs -o ro sysfs "$R/sys"; mount --rbind /dev "$R/dev"
mount --bind /xcc/ccache "$R/build/ccache"
mount --bind -o ro "$XCC_TREE" "$R/x86"
cp /etc/resolv.conf "$R/etc/resolv.conf"
cp build/src/"$PKG"/*.dsc build/src/"$PKG"/*.tar.* "$R/build/" 2>/dev/null || true
for f in build/src/"$PKG"/*.orig*.tar.*; do [ ! -e "$f" ] || cp -L "$f" "$R/build/"; done

in_chroot() { chroot "$R" /usr/bin/env -i PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
	HOME=/root LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive "$@"; }

T0=$(date +%s)
in_chroot sh -c "cd /build/reproducible-path && dpkg-source -x ../$(basename "$DSC") $SOURCE-$UPVER >/dev/null"
if [ ! -e "$DEPS" ]; then
	in_chroot apt-get update -qq
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
	rm -f /xcc/"$PKG"-deps-*.tar
	tar --exclude=./build --exclude=./proc --exclude=./sys --exclude=./dev --exclude=./x86 -cf "$DEPS.tmp" -C "$R" . && mv "$DEPS.tmp" "$DEPS"
fi
echo ">> build dependencies ready in $(( $(date +%s) - T0 )) s"

# the x86-64 toolchain inside the chroot (never in the cached deps tarball)
mkdir -p "$R/lib64" "$R/usr/local/bin" "$R/usr/libexec"
ln -sfn /x86/usr/lib64/ld-linux-x86-64.so.2 "$R/lib64/ld-linux-x86-64.so.2"
for d in usr/lib/x86_64-linux-gnu usr/powerpc64-linux-gnu usr/lib/gcc-cross usr/libexec/gcc-cross; do
	[ ! -e "$R/$d" ] || [ -L "$R/$d" ] || { echo "!! $R/$d exists in the chroot"; exit 1; }
	ln -sfn "/x86/$d" "$R/$d"
done
for f in "$XCC_TREE"/usr/bin/powerpc64-linux-gnu-*; do ln -sfn "/x86/usr/bin/${f##*/}" "$R/usr/local/bin/${f##*/}"; done
ln -sfn /x86/usr/bin/ccache "$R/usr/local/bin/ccache"
in_chroot powerpc64-linux-gnu-g++-16 --version | head -1
in_chroot ccache --version | head -1

T1=$(date +%s)
in_chroot sh -c "cd /build/reproducible-path/$SOURCE-$UPVER && \
	CC=powerpc64-linux-gnu-gcc-16 CXX=powerpc64-linux-gnu-g++-16 ${XCC_ENV:-} \
	CCACHE_DIR=/build/ccache CCACHE_BASEDIR=/build/reproducible-path CCACHE_NOHASHDIR=1 \
	CCACHE_MAXSIZE=${XCC_CCACHE_SIZE:-20G} CCACHE_COMPILERCHECK=content \
	DEB_BUILD_OPTIONS='${NOCHECK:+$NOCHECK }parallel=${JOBS:-16} ${DEB_BUILD_EXTRA:-}' \
	nice -n 15 ionice -c3 eatmydata dpkg-buildpackage -b -uc -us ${NATIVE_PROFILES:+-P$NATIVE_PROFILES}" > "$R/build/build.log" 2>&1 \
	|| { tail -40 "$R/build/build.log"; cp "$R/build/build.log" "$OUT/build.log"; chown -R "$OWNER" "$OUT"; exit 1; }
echo ">> built in $(( $(date +%s) - T1 )) s"
in_chroot sh -c "CCACHE_DIR=/build/ccache ccache -s" | grep -E "Hits|Misses|Cache size" || true
unmount_all
cp "$R"/build/reproducible-path/*.deb "$R"/build/reproducible-path/*.ddeb "$OUT/" 2>/dev/null || true
cp "$R"/build/reproducible-path/*.buildinfo "$R"/build/reproducible-path/*.changes "$OUT/" 2>/dev/null || true
cp "$R/build/build.log" "$OUT/build.log"
chown -R "$OWNER" "$OUT" /xcc/ccache
ls -la "$OUT"
exit
}
