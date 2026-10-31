#!/bin/bash
# ladder-mesa-build.sh RUNG: builds Mesa's libraries (the nouveau and softpipe drivers, EGL, GLX, GBM)
# for one rung of the fix ladder (tools/ladder.py; docs/VALIDATION.md) into build/testsets/ladder/rungNN/
# {lib,dri}, for the PS3 runner (ps3-gpu-tools' rsx-ladder.sh, with LD_LIBRARY_PATH and LIBGL_DRIVERS_PATH on them).
# make ladder-ps3-libs runs it once per rung, after tools/ladder.py --prepare has made the rung branches
# and with the rung's branch checked out in the clone (the builder image has no git: the host checks out).
# Inside the deb builder, privileged, as root, like tools/xcc-build.sh: a ppc64 chroot made from Mesa's
# cached build-dependency tarball (build/chroot/mesa-deps-<hash>.tar, make mesa-deb makes it) with the
# x86-64 cross compiler from the xcc toolchain linked in, so meson, ninja and the generators run under
# qemu and the compilers natively. One meson build directory (LADDER/mesa-build) serves every rung in
# turn: a rung's checkout touches only the files that differ, so ninja rebuilds only those (and ccache
# under /xcc).
# The options are debian/rules' for ppc64 (build/debs/mesa/build.log) minus the drm-shim tool; no -g,
# the libraries stripped. Runs under nice -n 10 and ionice -c3.
# Env: OWNER (uid:gid) JOBS (8) XCC_TREE (the toolchain, under /xcc) LADDER (the clone's directory,
# mounted; LADDER_DIR on the host).
set -euo pipefail
{
[ $# = 1 ] || { echo "usage: $0 RUNG" >&2; exit 2; }
L=${LADDER:-/ladder}
C=$L/mesa
B=$L/mesa-build
OUT=build/testsets/ladder
JOBS=${JOBS:-8}
[ -d "$C/.git" ] || { echo "!! no clone at $C (tools/ladder.py --prepare)"; exit 1; }
[ -x "$XCC_TREE/usr/bin/powerpc64-linux-gnu-gcc-16" ] || { echo "!! no toolchain in $XCC_TREE (make xcc-toolchain)"; exit 1; }
. packages/patched/graphics/mesa/package.conf
UPVER=${VERSION%-*}; UPVER=${UPVER#*:}
CTL=build/src/mesa/$SOURCE-$UPVER/debian/control
[ -e "$CTL" ] || { echo "!! no $CTL (make mesa-src)"; exit 1; }
DEPS=build/chroot/mesa-deps-$(sha256sum "$CTL" | cut -c1-12).tar
[ -e "$DEPS" ] || { echo "!! no $DEPS: make mesa-deb once makes it"; exit 1; }
R=/xcc/run-ladder-mesa
unmount_all() {
	for m in x86 build/ccache ladder dev proc sys; do umount -R -l "$R/$m" 2>/dev/null || true; done
	if grep -q " $R/" /proc/mounts; then echo "!! still mounted under $R" >&2; return 1; fi
}
cleanup() {	# the clone and the outputs back to their owner even when a build failed (git ran as root)
	unmount_all && rm -rf --one-file-system "$R"
	chown -R "$OWNER" "$OUT" "$C" "$L"/*.log /xcc/ccache 2>/dev/null || true
	[ ! -d "$B" ] || chown -R "$OWNER" "$B"
}
trap cleanup EXIT
cleanup
mkdir -p "$R" /xcc/ccache
tar -xf "$DEPS" -C "$R"
mkdir -p "$R/proc" "$R/sys" "$R/dev" "$R/x86" "$R/build/ccache" "$R/ladder"
mount -t proc proc "$R/proc"; mount -t sysfs -o ro sysfs "$R/sys"; mount --rbind /dev "$R/dev"
mount --bind /xcc/ccache "$R/build/ccache"
mount --bind -o ro "$XCC_TREE" "$R/x86"
mount --bind "$L" "$R/ladder"
mkdir -p "$R/lib64" "$R/usr/local/bin"
ln -sfn /x86/usr/lib64/ld-linux-x86-64.so.2 "$R/lib64/ld-linux-x86-64.so.2"
for d in usr/lib/x86_64-linux-gnu usr/powerpc64-linux-gnu usr/lib/gcc-cross usr/libexec/gcc-cross; do
	[ ! -e "$R/$d" ] || [ -L "$R/$d" ] || { echo "!! $R/$d exists in the chroot"; exit 1; }
	ln -sfn "/x86/$d" "$R/$d"
done
for f in "$XCC_TREE"/usr/bin/powerpc64-linux-gnu-*; do ln -sfn "/x86/usr/bin/${f##*/}" "$R/usr/local/bin/${f##*/}"; done
ln -sfn /x86/usr/bin/ccache "$R/usr/local/bin/ccache"
in_chroot() { chroot "$R" /usr/bin/env -i PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin HOME=/root LANG=C.UTF-8 \
	CCACHE_DIR=/build/ccache CCACHE_BASEDIR=/ladder CCACHE_NOHASHDIR=1 CCACHE_MAXSIZE=20G CCACHE_COMPILERCHECK=content \
	nice -n 10 ionice -c3 "$@"; }
TRIPLET=powerpc64-linux-gnu
rung=$1
n=$(printf '%02d' "$rung")
head=$(cat "$C/.git/HEAD")
[ "$head" = "ref: refs/heads/ladder/rung$n" ] || { echo "!! the clone has $head checked out, not ladder/rung$n"; exit 1; }
commit=$(cat "$C/.git/refs/heads/ladder/rung$n" 2>/dev/null || awk -v r="refs/heads/ladder/rung$n" '$2 == r { print $1 }' "$C/.git/packed-refs")
{
	T0=$(date +%s)
	if [ ! -e "$B/build.ninja" ]; then
		in_chroot sh -c "cd /ladder/mesa && CC='ccache $TRIPLET-gcc-16' CXX='ccache $TRIPLET-g++-16' \
			CFLAGS='-O2 -mcpu=cell -mtune=cell' CXXFLAGS='-O2 -mcpu=cell -mtune=cell' \
			meson setup /ladder/mesa-build --wrap-mode=nodownload --buildtype=plain --prefix=/usr \
			--libdir=lib/$TRIPLET -Ddri-drivers-path=/usr/lib/$TRIPLET/dri -Dplatforms=x11,wayland \
			-Dvulkan-drivers= -Dvulkan-layers= -Dglvnd=enabled -Db_ndebug=true -Dbuild-tests=false \
			-Dglx-direct=true -Dgbm=enabled -Dgallium-extra-hud=true -Dlmsensors=disabled -Dgallium-va=disabled \
			-Dvideo-codecs= -Dllvm=disabled -Dgallium-drivers=softpipe,nouveau -Dgles1=disabled -Dgles2=enabled \
			-Dvalgrind=disabled -Dteflon=false" > "$L/meson-setup.log" 2>&1 ||
			{ tail -40 "$L/meson-setup.log"; exit 1; }
	fi
	in_chroot ninja -C /ladder/mesa-build -j"$JOBS" > "$L/build-rung$n.log" 2>&1 || { tail -40 "$L/build-rung$n.log"; exit 1; }
	rm -rf --one-file-system "$L/inst"
	in_chroot sh -c "DESTDIR=/ladder/inst meson install -C /ladder/mesa-build --no-rebuild --quiet" >> "$L/build-rung$n.log" 2>&1 ||
		{ tail -20 "$L/build-rung$n.log"; exit 1; }
	D=$OUT/rung$n
	# the rung's previous build is kept as rungNN.prev until the next one
	[ ! -d "$D" ] || { rm -rf --one-file-system "$D.prev"; mv "$D" "$D.prev"; }
	mkdir -p "$D/lib/gbm" "$D/dri"
	I=$L/inst/usr/lib/$TRIPLET
	cp -a "$I"/libgallium*.so "$I"/libEGL_mesa.so* "$I"/libGLX_mesa.so* "$I"/libgbm.so* "$D/lib/"
	cp -a "$I"/gbm/*.so "$D/lib/gbm/"
	cp -a "$I"/dri/* "$D/dri/"
	find "$D" -type f -name '*.so*' -exec $TRIPLET-strip --strip-unneeded {} +	# the deb builder's cross binutils
	{ echo "rung $rung"; echo "branch ladder/rung$n"; echo "commit $commit"
	  echo "built $(date -u +%FT%TZ) in $(( $(date +%s) - T0 )) s"; } > "$D/BUILD"
	echo ">> rung $n: $(du -sh "$D" | cut -f1) in $(( $(date +%s) - T0 )) s"
}
rm -rf --one-file-system "$L/inst"
in_chroot sh -c "ccache -s" | grep -E "Hits|Misses|Cache size" || true
exit
}
