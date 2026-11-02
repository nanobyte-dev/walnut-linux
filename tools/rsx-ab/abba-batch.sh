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
# Stops at the first failed arm switch, and at a graph exception (nothing reloads after one).
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
BENCH=${BENCH:-/var/tmp/bench}
RSX_ARM=${RSX_ARM:-$here/rsx-arm.sh}
DESK=${DESK:-$here/desktop-cases.sh}
cd "$BENCH/harness" || exit 1
[ -x ./run.sh ] || { echo "abba-batch: no run.sh in $BENCH/harness (make bench-push)" >&2; exit 1; }
ARMS=${ARMS:-A B B A}
CASES=${CASES:-luanti neverball supertuxkart trigger-rally openarena mpv xloop}
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
	"$RSX_ARM" "$a" "$@" || { echo "abba-batch: arm $a didn't load: stopping" >&2; exit 1; }
	sudo cat /sys/kernel/debug/dri/0/ps3rsx_mm 2>/dev/null | grep -E '^(clear|reuse):' | sed "s/^/arm $a before: /"
	for c in $CASES; do
		one "$c" "$a"
		if exception; then echo "abba-batch: graph exception: stopping (no desktop restart)" >&2; exit 1; fi
	done
	sudo cat /sys/kernel/debug/dri/0/ps3rsx_mm 2>/dev/null | grep -E '^(clear|reuse):' | sed "s/^/arm $a after: /"
}

for a in $ARMS; do arm_run "$a"; done
for e in $(tr ' ' '\n' <<<"${EXTRA:-}"); do
	a=${e%%:*}; p=${e#*:}; [ "$p" = "$e" ] && p=
	arm_run "$a" $(tr ',' ' ' <<<"$p")
done
ps3-bench.sh desktop
