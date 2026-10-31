#!/bin/bash
# low-2gb.sh: patch 0001 (ppc64 big-endian: LuaJIT allocates its state in the low 2 GB). A minimal program that
# calls luaL_newstate(), run N times under qemu-ppc64 (qemu-user honours the mmap hints like the kernel):
# linked against the library from our built deb (build/debs/luajit) every start must pass; linked against the
# stock library (Debian's orig tarball, build/work/luajit_*.orig.tar.xz, built here for ppc64) roughly half of
# the starts fail. Static programs, so no ppc64 root is needed. Compiling and building use the unprivileged
# ps3-deb-builder docker image (its ppc64 cross gcc and libc.a).
# Exit 0: patched passes and stock fails; 1: otherwise; 77 (with a "SKIP luajit-low-2gb" line): prerequisites missing.
# Cold about 60 s (the stock library is built), warm about 4 s. Cache: build/tmp/luajit-test (about 12 MB).
set -u
cd "$(dirname "$0")/../../../../.."
N=${N:-100}
W=build/tmp/luajit-test
skip() { echo "SKIP luajit-low-2gb:$1 $2"; }
whole() { skip all "$1"; exit 77; }
command -v qemu-ppc64-static >/dev/null || whole "no qemu-ppc64-static"
command -v ar >/dev/null || whole "no ar"
command -v docker >/dev/null || whole "no docker"
IMG=$(docker images --format '{{.Repository}}:{{.Tag}}' ps3-deb-builder 2>/dev/null | head -1)
[ -n "$IMG" ] || whole "no ps3-deb-builder docker image"
DEV=$(ls -t build/debs/luajit/libluajit-5.1-dev_*_ppc64.deb 2>/dev/null | head -1)
[ -n "$DEV" ] || whole "no built libluajit-5.1-dev in build/debs/luajit"
ORIG=$(ls build/work/luajit_*.orig.tar.xz 2>/dev/null | head -1)
dk() { docker run --rm -u "$(id -u):$(id -g)" -v "$PWD/$W:/w" -w /w "$IMG" "$@"; }
mkdir -p $W/t
STAMP="$(basename "$DEV") $IMG"
if [ "$(cat $W/stamp 2>/dev/null)" != "$STAMP" ]; then
	rm -rf $W/patched $W/t/* $W/stamp; mkdir -p $W/patched
	ar p "$DEV" data.tar.xz | tar -xJ -C $W/patched ./usr/include ./usr/lib/powerpc64-linux-gnu/libluajit-5.1.a || whole "cannot unpack $DEV"
	echo "$STAMP" > $W/stamp
fi
cp packages/patched/libs/luajit/tests/newstate.c $W/t/newstate.c
build() {	# build NAME LIB: link t/newstate.c statically against LIB
	[ -x $W/t/$1 ] || dk powerpc64-linux-gnu-gcc -O2 -static -Ipatched/usr/include/luajit-2.1 t/newstate.c "$2" -lm -ldl -o t/$1 2>/dev/null
}
build new-patched patched/usr/lib/powerpc64-linux-gnu/libluajit-5.1.a || whole "cannot link against the deb's library"
[ -x $W/t/new-patched ] || whole "cannot link against the deb's library"
fails() {	# fails BINARY: how many of N starts fail
	local f=0 i; for ((i = 0; i < N; i++)); do qemu-ppc64-static $W/t/$1 >/dev/null 2>&1 || f=$((f + 1)); done; echo $f
}
rc=0
P=$(fails new-patched)
echo "patched (our deb): $P of $N starts failed (expected 0)"
[ "$P" = 0 ] || { echo "FAIL luajit-low-2gb: the patched library fails luaL_newstate()"; rc=1; }
if [ -z "$ORIG" ]; then
	skip stock "no build/work/luajit_*.orig.tar.xz for the stock library"; exit $rc
fi
if [ ! -e $W/stock/libluajit.a ]; then
	rm -rf $W/src; mkdir -p $W/src $W/stock
	tar -xf "$ORIG" -C $W/src --strip-components=1 || { skip stock "cannot unpack $ORIG"; exit $rc; }
	if dk sh -c 'cd src && nice -n 10 make -j6 amalg HOST_CC="gcc -m64" CROSS=powerpc64-linux-gnu- TARGET_SYS=Linux BUILDMODE=static' >$W/stock-build.log 2>&1; then
		cp $W/src/src/libluajit.a $W/stock/
	fi
	rm -rf $W/src
fi
[ -e $W/stock/libluajit.a ] || { skip stock "building the stock library failed (see $W/stock-build.log)"; exit $rc; }
build new-stock stock/libluajit.a
[ -x $W/t/new-stock ] || { skip stock "cannot link against the stock library"; exit $rc; }
S=$(fails new-stock)
echo "stock (orig tarball): $S of $N starts failed (expected some, roughly half)"
[ "$S" -ge 1 ] || { echo "FAIL luajit-low-2gb: the stock library never failed, the repro shows nothing"; rc=1; }
[ $rc = 0 ] && echo "ok luajit-low-2gb"
exit $rc
