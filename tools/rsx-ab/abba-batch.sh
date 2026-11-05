#!/bin/bash
# abba-batch.sh: boot 2's same-boot A/B of ps3rsx (run on the PS3 as tibi, under `nohup setsid`: a
# `systemd-run --user` unit dies with the user manager when the harness's quiet ends the last session;
# the harness from `make bench-push`, BENCH=/var/tmp/bench with run.sh in its harness/; the arms from
# arms-prep.sh in /var/tmp/rsx-ab). RSX_ARM, DESK: other rsx-arm.sh and desktop-cases.sh (tests).
# Arms in ARMS order (default "A B B A": A the current stable module, B the new build), each loaded
# by rsx-arm.sh, then every case once through the bench harness (60 s warm-up, at least 120 s
# measured, the stall recorder on: docs/STALLS.md), the riskiest last within an arm:
#   luanti view120        memory and VRAM swap
#   neverball base        light, GPU-bound, the 60 fps case
#   supertuxkart base     heavier GPU-bound
#   trigger-rally full    a big-texture load, timed (load=)
#   openarena gl2min      the timedemo replayed in the window: a level load inside the measured part
#   mpv, xloop            the desktop cases (desktop-cases.sh)
# EXTRA="C:test_clear_off=1" adds arms after the ABBA (ARM:PARAM=VALUE,..., e.g. the test-switch
# build with clearing off, to attribute a cost to it). CASES="..." runs a subset (names as above).
# Then the desktop comes back. Read the results on the PC: make bench-pull, then
# tools/rsx-ab/abba-report.py build/tmp/rsx-ab/rsx-arms.log build/bench/ps3-bench-<stamp>-*.
# ARM_ENV_<arm>="NAME=VALUE ...": environment of one arm's runs, as the bench harness's BENCH_<NAME> tokens (an arm
# that is the same Mesa as another with a switch on: mesa-arms-prep.sh MESA_ARM_ALIASES="C:B D:B").
# MESA_AB=1: a Mesa A/B instead (old Mesa against new, the ps3rsx build unchanged): the arms are two Mesa
# builds in /var/tmp/rsx-ab/mesa/{A,B} (mesa-arms-prep.sh on the PC), and each run gets its arm's
# LD_LIBRARY_PATH, LIBGL_DRIVERS_PATH and GBM_BACKENDS_PATH (mesa-arm.sh, MESA_ARM); the same arm
# letters and log, so abba-report.py reads it the same way. The arms cover the CLIENTS only: Xorg is started through
# the setuid Xorg.wrap, which ignores the environment, so the X server (and the xloop case) runs the installed
# Mesa. Every arm's files are checked by hash at the switch, and every run afterwards: LD_DEBUG=libs (to tmpfs, loads
# only, the same for both arms) lists what each process loaded, and a run that loaded a Mesa library from outside
# the arm's directory, or none, stops the batch (mesa-arm.sh check-run). EXTRA is for ps3rsx arms: not with MESA_AB.
# Stops at the first failed arm switch, and at a graph exception (nothing reloads after one).
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
BENCH=${BENCH:-/var/tmp/bench}
RSX_ARM=${RSX_ARM:-$here/rsx-arm.sh}
DESK=${DESK:-$here/desktop-cases.sh}
MESA_ARM=${MESA_ARM:-$here/mesa-arm.sh}
cd "$BENCH/harness" || exit 1
[ -x ./run.sh ] || { echo "abba-batch: no run.sh in $BENCH/harness (make bench-push)" >&2; exit 1; }
ARMS=${ARMS:-A B B A}
CASES=${CASES:-luanti neverball supertuxkart trigger-rally openarena mpv xloop}
[ -z "${MESA_AB:-}" ] || [ -z "${EXTRA:-}" ] || { echo "abba-batch: EXTRA (ps3rsx arms) with MESA_AB" >&2; exit 1; }
LDDIR=${LDDIR:-/dev/shm/rsx-ab-ld}
[ -z "${MESA_AB:-}" ] || echo "abba-batch: Mesa A/B: the arms cover the CLIENTS only; Xorg (setuid Xorg.wrap) ignores them and runs the installed Mesa" >&2
export MESA_BASE_LD=${LD_LIBRARY_PATH-}	# what a Mesa arm's LD_LIBRARY_PATH keeps behind its own
n_arms=$(( $(wc -w <<<"$ARMS") + $(wc -w <<<"${EXTRA:-}") ))
export BATCH_NAME=rsxab BATCH_TOTAL=$(( n_arms * $(wc -w <<<"$CASES") ))
rm -f /dev/shm/ps3-bench.batchpos

exception() { grep -q -e 'graph exception' -e 'GPU stopped' <<<"$(sudo dmesg)"; }

one() {	# one CASE ARM
	case $1 in
	luanti) STALLS=1 ./run.sh bench luanti view120 1 ;;
	neverball) STALLS=1 ./run.sh bench neverball base 1 ;;
	supertuxkart) STALLS=1 ./run.sh bench supertuxkart base 1 ;;
	trigger-rally) STALLS=1 ./run.sh bench trigger-rally full 1 ;;
	openarena) STALLS=1 ./run.sh bench openarena gl2min 1 ;;
	mpv|xloop) "$DESK" "$1" "$2" ;;
	*) echo "abba-batch: no case $1" >&2 ;;
	esac
}

arm_run() {	# arm_run ARM [PARAM=VALUE...]
	local a=$1; shift
	if [ -n "${MESA_AB:-}" ]; then	# the arm is the Mesa the runs use: no module is reloaded
		e=$("$MESA_ARM" select "$a" && "$MESA_ARM" env "$a") && eval "$e" || { echo "abba-batch: Mesa arm $a isn't usable: stopping" >&2; exit 1; }
	else
		"$RSX_ARM" "$a" "$@" || { echo "abba-batch: arm $a didn't load: stopping" >&2; exit 1; }
	fi
	# ARM_ENV_<arm>="NAME=VALUE ...": driver switches of this arm (e.g. ARM_ENV_C="NV30_PS3_IDX_GART=1" for the
	# Mesa under test with that switch on), handed to the games as the harness's BENCH_<NAME> tokens
	local ev=ARM_ENV_$a kv armenv=()
	for kv in ${!ev:-}; do export "BENCH_$kv"; armenv+=("BENCH_${kv%%=*}"); echo "abba-batch: arm $a: $kv" >&2; done
	sudo cat /sys/kernel/debug/dri/0/ps3rsx_mm 2>/dev/null | grep -E '^(clear|reuse):' | sed "s/^/arm $a before: /"
	for c in $CASES; do
		if [ -n "${MESA_AB:-}" ]; then	# LD_DEBUG=libs on tmpfs, loads only: what each process of the run loaded
			rm -rf "$LDDIR"; mkdir -p "$LDDIR"
			export LD_DEBUG=libs LD_DEBUG_OUTPUT="$LDDIR/ld"
		fi
		one "$c" "$a"
		if [ -n "${MESA_AB:-}" ]; then
			unset LD_DEBUG LD_DEBUG_OUTPUT
			"$MESA_ARM" check-run "$a" "$LDDIR" || { echo "abba-batch: the $c run did not use Mesa arm $a: stopping" >&2; exit 1; }
		fi
		if exception; then echo "abba-batch: graph exception: stopping (no desktop restart)" >&2; exit 1; fi
	done
	sudo cat /sys/kernel/debug/dri/0/ps3rsx_mm 2>/dev/null | grep -E '^(clear|reuse):' | sed "s/^/arm $a after: /"
	[ ${#armenv[@]} = 0 ] || unset "${armenv[@]}"
}

for a in $ARMS; do arm_run "$a"; done
for e in $(tr ' ' '\n' <<<"${EXTRA:-}"); do
	a=${e%%:*}; p=${e#*:}; [ "$p" = "$e" ] && p=
	arm_run "$a" $(tr ',' ' ' <<<"$p")
done
[ -z "${MESA_AB:-}" ] || eval "$("$MESA_ARM" env off)"	# the desktop comes back on the system's Mesa
ps3-bench.sh desktop
