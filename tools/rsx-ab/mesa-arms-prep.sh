#!/bin/bash
# mesa-arms-prep.sh B_DEBS [A_DEBS]: the Mesa arms of a same-boot Mesa A/B (abba-batch.sh with MESA_AB=1,
# mesa-arm.sh on the PS3), made on the PC: two Mesa builds side by side, each in its own directory, chosen
# per run through LD_LIBRARY_PATH, LIBGL_DRIVERS_PATH and GBM_BACKENDS_PATH as the fix ladder's rungs are
# (tools/ladder-mesa-build.sh, rsx-ladder.sh). Nothing is installed anywhere; ps3rsx stays what it is.
#   B_DEBS  a directory with the new build's Mesa debs (build/debs/mesa): mesa-libgallium, libgl1-mesa-dri,
#           libegl-mesa0, libglx-mesa0 and libgbm1
#   A_DEBS  the same for the old build (a directory); without it walnut-stable's: its signed index checked with
#           the archive key, each deb with the index's sha256 (the way arms-prep.sh gets ps3rsx's arm A)
# Writes build/tmp/rsx-ab/mesa/{A,B}/{lib,dri} (lib: libgallium-<version>.so, libEGL_mesa, libGLX_mesa, libgbm,
# gbm/; dri: the driver stub and its links) and mesa-arms.txt (arm, version, libgallium's sha256 prefix), then
# copy the directory to the PS3 with tar or rsync -a (scp -r turns the dri/ symlinks into files, and the PS3 then refuses the arms):
#   tar -C build/tmp/rsx-ab -c mesa | ssh PS3 'tar -C /var/tmp/rsx-ab -x'
# libgallium's file name carries the package version, and libGLX_mesa, libEGL_mesa and libgbm need exactly that
# name (the driver stub, dri/libdril_dri.so, finds libgallium's symbols in the process): an arm is only whole when
# it is used with its own lib/ in front, and with its own dri/ and gbm/, so no file of the other build is used;
# that is what the three variables of mesa-arm.sh do. The prep refuses an arm whose libGLX_mesa asks for another
# libgallium than the one in its lib/ (debs of two builds mixed).
set -euo pipefail
cd "$(dirname "$0")/../.."
[ $# -ge 1 ] && [ $# -le 2 ] || { echo "usage: $0 B_DEBS [A_DEBS]" >&2; exit 2; }
O=${MESA_ARMS_OUT:-build/tmp/rsx-ab/mesa}
PKGS="mesa-libgallium libgl1-mesa-dri libegl-mesa0 libglx-mesa0 libgbm1"
W=$(mktemp -d build/tmp/rsx-ab-mesaprep.XXXXXX)
trap 'rm -rf "$W"' EXIT
URL=${WALNUT_APT:-https://walnut.nanobyte.dev/apt}
KEYRING=${ARCHIVE_KEYRING:-apt-repo/keyrings/archive-keyring.gpg}

field() { sed -n "s/^$2: //p" "$1" | head -1; }

unpack() {	# unpack ARM DEBDIR-or-list: the five debs of ARM extracted under $W/ARM
	local arm=$1 p f c n
	shift
	mkdir -p "$W/$arm"
	for p in $PKGS; do
		f=	# "<package>_" never matches the -dbgsym debs
		n=0
		for c in "$1"/"${p}"_*.deb; do [ ! -e "$c" ] || { f=$(realpath "$c"); n=$((n + 1)); }; done
		[ -n "$f" ] || { echo "!! arm $arm: no ${p}_*.deb in $1" >&2; exit 1; }
		[ "$n" = 1 ] || { echo "!! arm $arm: $n ${p}_*.deb in $1 (want one: the arm is one build)" >&2; exit 1; }
		mkdir -p "$W/$arm/x-$p"
		(cd "$W/$arm/x-$p" && ar x "$f" && tar -xf data.tar.* && tar -xf control.tar.* ./control)
		[ "$p" != mesa-libgallium ] || field "$W/$arm/x-$p/control" Version > "$W/$arm/version"
	done
	for p in $PKGS; do	# one build: the five debs have one version
		[ "$(field "$W/$arm/x-$p/control" Version)" = "$(cat "$W/$arm/version")" ] ||
			{ echo "!! arm $arm: $p is $(field "$W/$arm/x-$p/control" Version), mesa-libgallium $(cat "$W/$arm/version"): not one build" >&2; exit 1; }
	done
}

lay_out() {	# lay_out ARM: the ladder layout under $O/ARM
	local arm=$1 t g n
	rm -rf "${O:?}/$arm"
	mkdir -p "$O/$arm/lib/gbm" "$O/$arm/dri"
	for t in "$W/$arm"/x-*/usr/lib/*-linux-gnu; do
		[ -d "$t" ] || continue
		cp -a "$t"/libgallium-*.so "$t"/libEGL_mesa.so* "$t"/libGLX_mesa.so* "$t"/libgbm.so* "$O/$arm/lib/" 2>/dev/null || true
		[ ! -d "$t/gbm" ] || cp -a "$t"/gbm/*.so "$O/$arm/lib/gbm/"
		[ ! -d "$t/dri" ] || cp -a "$t"/dri/. "$O/$arm/dri/"
	done
	n=$(ls "$O/$arm"/lib/libgallium-*.so 2>/dev/null | wc -l)
	[ "$n" = 1 ] || { echo "!! arm $arm: $n libgallium-*.so files (want 1)" >&2; exit 1; }
	g=$(ls "$O/$arm"/lib/libgallium-*.so)
	for f in libEGL_mesa.so.0 libGLX_mesa.so.0 libgbm.so.1; do
		[ -e "$O/$arm/lib/$f" ] || { echo "!! arm $arm: no $f" >&2; exit 1; }
	done
	[ -e "$O/$arm/dri/nouveau_dri.so" ] || { echo "!! arm $arm: no dri/nouveau_dri.so" >&2; exit 1; }
	# libGLX_mesa's dependency on libgallium names this very package version
	grep -q -a "$(basename "$g")" "$O/$arm/lib/libGLX_mesa.so.0" ||
		{ echo "!! arm $arm: libGLX_mesa.so.0 doesn't ask for $(basename "$g"): the debs are not of one build" >&2; exit 1; }
	printf '%s\t%s\t%s\n' "$arm" "$(cat "$W/$arm/version")" "$(sha256sum "$g" | cut -c1-16)" >> "$O/mesa-arms.txt"
	# every file of the arm (libgallium, the dri drivers, libGLX_mesa, libEGL_mesa, libgbm, gbm/), by hash,
	# and its links: mesa-arm.sh checks them all before a run uses the arm
	(cd "$O/$arm" && find . -type f | LC_ALL=C sort | xargs sha256sum) > "$O/manifest-$arm.txt"
	(cd "$O/$arm" && find . -type l -printf '%p -> %l\n' | LC_ALL=C sort) > "$O/links-$arm.txt"
}

fetch_stable() {	# fetch_stable DIR: walnut-stable's five debs, checked against its signed index
	local p file sha
	mkdir -p "$1"
	curl -sf -o "$W/InRelease" "$URL/dists/stable/InRelease"
	# only what gpgv verified is read: text added around the signed part is never seen (--output is the
	# signed text alone)
	gpgv --keyring "$KEYRING" --output "$W/Release" "$W/InRelease" 2>/dev/null ||
		{ echo "!! stable InRelease: bad or missing signature" >&2; exit 1; }
	curl -sf -o "$W/Packages" "$URL/dists/stable/main/binary-ppc64/Packages"
	[ "$(sha256sum "$W/Packages" | cut -d' ' -f1)" = "$(grep ' main/binary-ppc64/Packages$' "$W/Release" | tail -1 | awk '{print $1}')" ] ||
		{ echo "!! Packages doesn't match InRelease" >&2; exit 1; }
	for p in $PKGS; do
		read -r file sha < <(python3 -I - "$W/Packages" "$p" <<'PY'
import re, sys
best = None
for para in open(sys.argv[1]).read().split("\n\n"):
    f = dict(re.findall(r"^([A-Za-z0-9-]+): (.*)$", para, re.M))
    if f.get("Package") == sys.argv[2]:
        best = f		# the index lists one version per package and suite
if best:
    print(best["Filename"], best["SHA256"])
PY
)
		[ -n "${file:-}" ] || { echo "!! $p is not in walnut-stable" >&2; exit 1; }
		curl -sf -o "$1/${file##*/}" "$URL/$file"
		echo "$sha  $1/${file##*/}" | sha256sum -c --quiet - || { echo "!! $file: sha256 mismatch" >&2; exit 1; }
	done
}

mkdir -p "$O"
: > "$O/mesa-arms.txt"
if [ $# = 2 ]; then
	A=$2
else
	A=$W/stable-debs
	fetch_stable "$A"
fi
unpack A "$A"
unpack B "$1"
lay_out A
lay_out B
# MESA_ARM_ALIASES="C:B D:B ...": more arms that are the same Mesa as an existing one (a symlink, the same manifest), to
# run it with a driver switch on (abba-batch.sh ARM_ENV_<arm>); the arm letters stay the report's columns
for al in ${MESA_ARM_ALIASES:-}; do
	a=${al%%:*} base=${al#*:}
	[[ $a =~ ^[C-Z]$ && $base =~ ^[AB]$ ]] || { echo "!! MESA_ARM_ALIASES: $al is not <C-Z>:<A|B>" >&2; exit 1; }
	rm -rf "${O:?}/$a"; ln -s "$base" "$O/$a"
	cp "$O/manifest-$base.txt" "$O/manifest-$a.txt"; cp "$O/links-$base.txt" "$O/links-$a.txt"
	awk -F'\t' -v a="$a" -v b="$base" -v OFS='\t' '$1 == b { $1 = a; print }' "$O/mesa-arms.txt" >> "$O/mesa-arms.txt.new"
done
[ ! -f "$O/mesa-arms.txt.new" ] || { cat "$O/mesa-arms.txt.new" >> "$O/mesa-arms.txt"; rm "$O/mesa-arms.txt.new"; }
column -t "$O/mesa-arms.txt"
echo "?? the A/B covers the clients only: Xorg runs through the setuid Xorg.wrap and ignores the arm's Mesa (the server runs the installed one)" >&2
[ "$(cut -f2 "$O/mesa-arms.txt" | sort -u | wc -l)" = 2 ] || echo "?? both arms are the same Mesa version: the A/B compares a build with itself" >&2
echo ">> $O: copy to the PS3 with tar or rsync -a, not scp -r (it turns the symlinks into files):"
echo ">>   tar -C $(dirname "$O") -c $(basename "$O") | ssh PS3 'mkdir -p /var/tmp/rsx-ab && tar -C /var/tmp/rsx-ab -x'"
