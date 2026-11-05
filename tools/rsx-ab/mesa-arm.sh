#!/bin/bash
# mesa-arm.sh select ARM | env ARM | status: the PS3 side of a same-boot Mesa A/B (abba-batch.sh with
# MESA_AB=1; the arms from mesa-arms-prep.sh on the PC, copied to /var/tmp/rsx-ab/mesa/{A,B}/{lib,dri}
# with mesa-arms.txt). A Mesa arm is a pair of directories, not an installed package: a run uses it through
# LD_LIBRARY_PATH (its libgallium, libGLX_mesa, libEGL_mesa, libgbm in front of the system's: they ask for
# the libgallium of their own package version by name), LIBGL_DRIVERS_PATH (its driver stub) and
# GBM_BACKENDS_PATH, so no file of the other build is used.
#   select ARM  checks the arm (every file by hash against manifest-<arm>.txt, no file missing or extra, the
#               links as links-<arm>.txt has them) and logs the switch to
#               /var/tmp/ps3-bench/rsx-arms.log in rsx-arm.sh's format, so abba-report.py reads a Mesa
#               A/B like a ps3rsx one (the params column says "mesa")
#   env ARM     the export lines for ARM, for eval in the caller's shell; MESA_BASE_LD is the
#               LD_LIBRARY_PATH to keep behind the arm's (default: the caller's); env off puts the
#               caller's back (unsets the other two)
#   check-run ARM LDDIR  after a run that had LD_DEBUG=libs LD_DEBUG_OUTPUT=LDDIR/ld in its environment: every
#               Mesa library any process of the run loaded (libgallium, libGLX_mesa, libEGL_mesa, libgbm,
#               gbm/, dri/*_dri.so) came from the arm's directory, and libgallium was loaded at all; fails the run
#               otherwise (the arm's files can be right and a run still load the system's Mesa)
#   status      the arms present and what the next run would use
# The A/B covers CLIENTS only: Xorg is started through the setuid Xorg.wrap, a secure exec that drops
# LD_LIBRARY_PATH and ignores LIBGL_DRIVERS_PATH and GBM_BACKENDS_PATH, so the X server (glamor, the xloop
# case) always runs the INSTALLED Mesa, which is printed with each switch. To compare the server too, install
# the Mesa under test as the system's.
# Nothing is loaded or installed, and ps3rsx is not touched.
set -euo pipefail
D=${MESA_DIR:-/var/tmp/rsx-ab/mesa}
LOG=${RSX_ARMS_LOG:-/var/tmp/ps3-bench/rsx-arms.log}
die() { echo "mesa-arm: $*" >&2; exit 1; }

installed() {	# the Mesa the X server runs
	dpkg-query -W -f='${Version}' mesa-libgallium 2>/dev/null || echo "unknown"
}

check() {	# check ARM: all the arm's files by hash, no extra file, the links
	local a=$1 g want
	[ -d "$D/$a/lib" ] && [ -d "$D/$a/dri" ] || die "no $D/$a/{lib,dri} (mesa-arms-prep.sh on the PC, then copy)"
	g=$(ls "$D/$a"/lib/libgallium-*.so 2>/dev/null) && [ "$(wc -w <<<"$g")" = 1 ] || die "arm $a: not exactly one libgallium-*.so"
	want=$(awk -F'\t' -v a="$a" '$1 == a { print $3 }' "$D/mesa-arms.txt")
	[ -n "$want" ] || die "arm $a is not in $D/mesa-arms.txt"
	[ "$(sha256sum "$g" | cut -c1-16)" = "$want" ] || die "arm $a: libgallium doesn't match mesa-arms.txt (copy again)"
	[ -e "$D/$a/dri/nouveau_dri.so" ] || die "arm $a: no dri/nouveau_dri.so"
	[ -s "$D/manifest-$a.txt" ] && [ -f "$D/links-$a.txt" ] || die "arm $a: no manifest-$a.txt / links-$a.txt (mesa-arms-prep.sh makes them)"
	(cd "$D/$a" && sha256sum -c --quiet "$D/manifest-$a.txt" >/dev/null 2>&1) || die "arm $a: a file is missing or differs from manifest-$a.txt (copy again)"
	[ "$(cd "$D/$a" && find . -type f | LC_ALL=C sort)" = "$(sed 's/^[0-9a-f]*  //' "$D/manifest-$a.txt")" ] || die "arm $a: a file that is not in manifest-$a.txt (symlinks lost? copy with tar or rsync -a, never scp -r: it turns the dri links into files)"
	[ "$(cd "$D/$a" && find . -type l -printf '%p -> %l\n' | LC_ALL=C sort)" = "$(cat "$D/links-$a.txt")" ] || die "arm $a: its symlinks differ from links-$a.txt (symlinks lost: copy with tar or rsync -a, never scp -r)"
}

case ${1:-} in
select)
	[ $# = 2 ] || die "select ARM"
	check "$2"
	mkdir -p "$(dirname "$LOG")"
	printf '%s\t%s\t%s\t%s\n' "$(date -u +%Y%m%dT%H%M%SZ)" "$2" "$(awk -F'\t' -v a="$2" '$1 == a { print $2 }' "$D/mesa-arms.txt")" mesa >> "$LOG"
	echo "mesa-arm: Mesa arm $2 ($(awk -F'\t' -v a="$2" '$1 == a { print $2 }' "$D/mesa-arms.txt")), every file checked" >&2
	echo "mesa-arm: clients only: Xorg (setuid Xorg.wrap) ignores the arm and runs the installed Mesa $(installed)" >&2
	;;
check-run)
	[ $# = 3 ] || die "check-run ARM LDDIR"
	a=$2 dir=$3
	ls "$dir"/ld.* >/dev/null 2>&1 || die "run check: no LD_DEBUG output in $dir (the environment did not reach the run)"
	# the 'calling init: PATH' lines of the Mesa libraries, any process of the run
	libs=$(cat "$dir"/ld.* | sed -n 's/^.*calling init: *//p' | grep -E '(^|/)(libgallium-[^/]*|libGLX_mesa|libEGL_mesa|libgbm)\.so|/gbm/[^/]*\.so$|_dri\.so$' | sort -u || true)
	grep -q 'libgallium-' <<<"$libs" || die "run check: no libgallium was loaded by the run"
	bad=$(grep -v "^$D/$a/" <<<"$libs" || true)
	[ -z "$bad" ] || die "run check: arm $a, but the run loaded Mesa from elsewhere: $(tr '\n' ' ' <<<"$bad")"
	echo "mesa-arm: run check ok: $(wc -l <<<"$libs") Mesa libraries, all from $D/$a"
	;;
env)
	[ $# = 2 ] || die "env ARM|off"
	base=${MESA_BASE_LD-${LD_LIBRARY_PATH-}}
	if [ "$2" = off ]; then
		if [ -n "$base" ]; then echo "export LD_LIBRARY_PATH='$base'"; else echo "unset LD_LIBRARY_PATH"; fi
		echo "unset LIBGL_DRIVERS_PATH GBM_BACKENDS_PATH"
	else
		check "$2"
		echo "export LD_LIBRARY_PATH='$D/$2/lib${base:+:$base}'"
		echo "export LIBGL_DRIVERS_PATH='$D/$2/dri'"
		echo "export GBM_BACKENDS_PATH='$D/$2/lib/gbm'"
	fi
	;;
status)
	echo "installed Mesa (what Xorg runs): $(installed)"
	for a in A B; do
		if (check "$a") 2>/dev/null; then echo "arm $a: $(awk -F'\t' -v a="$a" '$1 == a { print $2 }' "$D/mesa-arms.txt")"
		else echo "arm $a: missing or wrong"; fi
	done
	echo "now: LD_LIBRARY_PATH=${LD_LIBRARY_PATH-} LIBGL_DRIVERS_PATH=${LIBGL_DRIVERS_PATH-} GBM_BACKENDS_PATH=${GBM_BACKENDS_PATH-}"
	;;
*)
	sed -n '2,15p' "$0" >&2
	exit 2
	;;
esac
