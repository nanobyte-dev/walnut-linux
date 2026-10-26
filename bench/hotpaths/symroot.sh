#!/bin/bash
# symroot.sh [MESA_VERSION] [KERNEL_RELEASE] [PS3RSX_VERSION]: the symbol root analyze.py reads
# (docs/HOTPATHS.md), in $HOTPATHS_SYM (default build/tmp/hotpaths-sym). Takes the debs from
# build/debs and build/aptly (the versions the profiled runs' "versions" files name) and Debian's
# libc6-dbg from the snapshot the sysroot was made from. Ephemeral: delete it after the analysis.
set -eu
top=$(cd "$(dirname "$0")/../.." && pwd)
B=$top/build
S=${HOTPATHS_SYM:-$B/tmp/hotpaths-sym}
MV=${1:-26.2.4-1+ps3.476}
KR=${2:-6.12.111+ps3.472}
RV=${3:-0.443+k6.12.111+ps3.472}
LIBC=${LIBC_VERSION:-2.43-7}
SNAP=${SNAPSHOT:-http://snapshot.debian.org/archive/debian-ports/20261013T000000Z}
mkdir -p "$S"
find_deb() {	# find_deb NAME_VERSION_GLOB: the first match in build/debs or the aptly pools
	find "$B/debs" "$B/aptly/public" "$B/aptly/pool" -name "$1" 2>/dev/null | head -1
}
unpack() {	# unpack DEB: its data into $S
	local t; t=$(mktemp -d -p "$S")
	(cd "$t" && ar x "$1" && tar -xf data.tar.* -C "$S")
	rm -rf "$t"
}
for p in mesa-libgallium mesa-libgallium-dbgsym libglx-mesa0 libglx-mesa0-dbgsym libgl1-mesa-dri \
	libgl1-mesa-dri-dbgsym libgbm1 libgbm1-dbgsym; do
	d=$(find_deb "${p}_${MV}_ppc64.deb"); [ -n "$d" ] || { echo "!! no ${p}_${MV}"; continue; }
	unpack "$d"
done
d=$(find_deb "linux-image-${KR}_*_ppc64.deb"); [ -n "$d" ] && unpack "$d" || echo "!! no linux-image-$KR (System.map)"
d=$(find_deb "ps3rsx-modules-${KR}_${RV}_ppc64.deb"); [ -n "$d" ] && unpack "$d" || echo "!! no ps3rsx-modules-$KR $RV"
if [ ! -e "$S/.libc6-dbg" ]; then
	curl -sfL -o "$S/libc6-dbg.deb" "$SNAP/pool-ppc64/main/g/glibc/libc6-dbg_${LIBC}_ppc64.deb" &&
		unpack "$S/libc6-dbg.deb" && rm -f "$S/libc6-dbg.deb" && touch "$S/.libc6-dbg" ||
		echo "!! no libc6-dbg $LIBC: libc frames keep their DSO name only"
fi
# the games' own frames: EXTRA_DEBS="<deb>..." unpacks those too, a game's deb and its -dbgsym of the
# build the run had (perf-buildids.txt has its build id; the dbgsym's usr/lib/debug/.build-id/ shows it)
for d in ${EXTRA_DEBS:-}; do unpack "$(cd "$(dirname "$d")" && pwd)/$(basename "$d")"; done
echo ">> $S: $(du -sh "$S" | cut -f1)"
