#!/bin/bash
# mesa-arm.sh select ARM | env ARM | status: the PS3 side of a same-boot Mesa A/B (abba-batch.sh with
# MESA_AB=1; the arms from mesa-arms-prep.sh on the PC, copied to /var/tmp/rsx-ab/mesa/{A,B}/{lib,dri}
# with mesa-arms.txt). A Mesa arm is a pair of directories, not an installed package: a run uses it through
# LD_LIBRARY_PATH (its libgallium, libGLX_mesa, libEGL_mesa, libgbm in front of the system's: they ask for
# the libgallium of their own package version by name), LIBGL_DRIVERS_PATH (its driver stub) and
# GBM_BACKENDS_PATH, so no file of the other build is used.
#   select ARM  checks the arm (its libgallium against mesa-arms.txt's hash) and logs the switch to
#               /var/tmp/ps3-bench/rsx-arms.log in rsx-arm.sh's format, so abba-report.py reads a Mesa
#               A/B like a ps3rsx one (the params column says "mesa")
#   env ARM     the export lines for ARM, for eval in the caller's shell; MESA_BASE_LD is the
#               LD_LIBRARY_PATH to keep behind the arm's (default: the caller's); env off puts the
#               caller's back (unsets the other two)
#   status      the arms present and what the next run would use
# Nothing is loaded or installed, and ps3rsx is not touched.
set -euo pipefail
D=${MESA_DIR:-/var/tmp/rsx-ab/mesa}
LOG=${RSX_ARMS_LOG:-/var/tmp/ps3-bench/rsx-arms.log}
die() { echo "mesa-arm: $*" >&2; exit 1; }

check() {	# check ARM: the arm's files, and libgallium as mesa-arms.txt has it
	local a=$1 g want
	[ -d "$D/$a/lib" ] && [ -d "$D/$a/dri" ] || die "no $D/$a/{lib,dri} (mesa-arms-prep.sh on the PC, then copy)"
	g=$(ls "$D/$a"/lib/libgallium-*.so 2>/dev/null) && [ "$(wc -w <<<"$g")" = 1 ] || die "arm $a: not exactly one libgallium-*.so"
	want=$(awk -F'\t' -v a="$a" '$1 == a { print $3 }' "$D/mesa-arms.txt")
	[ -n "$want" ] || die "arm $a is not in $D/mesa-arms.txt"
	[ "$(sha256sum "$g" | cut -c1-16)" = "$want" ] || die "arm $a: libgallium doesn't match mesa-arms.txt (copy again)"
	[ -e "$D/$a/dri/nouveau_dri.so" ] || die "arm $a: no dri/nouveau_dri.so"
}

case ${1:-} in
select)
	[ $# = 2 ] || die "select ARM"
	check "$2"
	mkdir -p "$(dirname "$LOG")"
	printf '%s\t%s\t%s\t%s\n' "$(date -u +%Y%m%dT%H%M%SZ)" "$2" "$(awk -F'\t' -v a="$2" '$1 == a { print $2 }' "$D/mesa-arms.txt")" mesa >> "$LOG"
	echo "mesa-arm: Mesa arm $2 ($(awk -F'\t' -v a="$2" '$1 == a { print $2 }' "$D/mesa-arms.txt"))"
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
