#!/bin/bash
# Host repro of the sdl2-compat "close while locked" deadlock (patch 0001).
# Builds sdl2-compat 2.32.74 twice (stock, and with our series) under build/tmp/sdl2-compat-test, runs
# close-locked-test.c against each with SDL_AUDIODRIVER=dummy, once through SDL2's API and once through
# SDL 1.2's (host sdl12-compat, pointed at the built libSDL2 with LD_LIBRARY_PATH).
# Pass = the patched build exits 0 and the stock build hangs (killed by the timeout).
# Exit 0 pass, 77 prerequisites missing, 1 failure.  No PS3 involved.
set -u
cd "$(dirname "$0")/../../../../.." || exit 1
pkg=packages/patched/graphics/sdl2-compat
W=$PWD/build/tmp/sdl2-compat-test
tar_=$PWD/build/work/sdl2-compat_2.32.74+ds.orig.tar.xz
T=${CLOSE_LOCKED_TIMEOUT:-5}
skip() { echo "SKIP sdl2-compat:$1 $2"; }
for c in cmake gcc patch tar timeout; do
	command -v $c >/dev/null || { skip all "no $c"; exit 77; }
done
[ -f "$tar_" ] || { skip all "no $tar_"; exit 77; }
[ -e /usr/include/SDL3/SDL.h ] || [ -e /usr/local/include/SDL3/SDL.h ] || { skip all "no SDL3 development files"; exit 77; }
ldconfig -p | grep -q 'libSDL3\.so\.0' || { skip all "no libSDL3 runtime"; exit 77; }
mkdir -p "$W"
lowprio() { nice -n 10 ionice -c3 "$@"; }

build() {	# build NAME PATCHED(0|1): src tree + cmake build, cached; patched rebuilds when the series is newer
	local n=$1 p=$2; local src=$W/src-$n b=$W/build-$n so=$W/build-$n/libSDL2-2.0.so.0
	local deps=("$tar_"); [ "$p" = 1 ] && deps+=("$pkg"/patches/*.patch "$pkg/patches/series")
	if [ -e "$so" ]; then
		local stale=0 d; for d in "${deps[@]}"; do [ "$d" -nt "$so" ] && stale=1; done
		[ $stale = 0 ] && return 0
	fi
	rm -rf "$src" "$b"; mkdir -p "$src" || return 1
	tar -C "$src" --strip-components=1 -xf "$tar_" || return 1
	if [ "$p" = 1 ]; then
		local f; for f in $(grep -v '^#' "$pkg/patches/series"); do
			patch -s -p1 -d "$src" < "$pkg/patches/$f" || { echo "patch $f failed"; return 1; }
		done
	fi
	lowprio cmake -S "$src" -B "$b" -DCMAKE_BUILD_TYPE=Release -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF >"$W/cmake-$n.log" 2>&1 \
		&& lowprio cmake --build "$b" -j6 >>"$W/cmake-$n.log" 2>&1 || { echo "build $n failed, see $W/cmake-$n.log"; tail -n 15 "$W/cmake-$n.log"; return 1; }
	[ -e "$so" ] || { echo "build $n: no $so"; return 1; }
	rm -rf "$src"	# the build tree stays, the source tree is not needed
}

build stock 0 && build patched 1 || exit 1
lowprio gcc -O1 -Wall -o "$W/close-locked-test" "$pkg/tests/close-locked-test.c" -ldl || { echo "cannot compile the test program"; exit 1; }

# run NAME API LIBDIR: prints the outcome word (pass, hang or fail)
trial() {
	local api=$1 dir=$2 rc lib
	if [ "$api" = sdl2 ]; then lib=$dir/libSDL2-2.0.so.0; else lib=libSDL-1.2.so.0; fi
	SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=$dir timeout -s KILL "$T" "$W/close-locked-test" "$api" "$lib" >"$W/out-$api.txt" 2>&1
	rc=$?
	case $rc in 0) echo pass;; 137|124) echo hang;; *) echo "fail(rc=$rc: $(tail -n1 "$W/out-$api.txt"))";; esac
}

status=0; ran=0
for api in sdl2 sdl12; do
	if [ $api = sdl12 ] && ! ldconfig -p | grep -q 'libSDL-1\.2\.so\.0'; then skip sdl12 "no sdl12-compat (libSDL-1.2.so.0)"; continue; fi
	s=$(trial $api "$W/build-stock"); p=$(trial $api "$W/build-patched")
	echo "sdl2-compat:$api stock=$s patched=$p"
	ran=$((ran + 1))
	[ "$p" = pass ] || { echo "FAIL sdl2-compat:$api the patched build should close cleanly, got $p"; status=1; }
	[ "$s" = hang ] || { echo "FAIL sdl2-compat:$api the stock build should hang (the deadlock), got $s"; status=1; }
done
[ $ran = 0 ] && exit 77
exit $status
