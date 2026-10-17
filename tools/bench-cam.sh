#!/bin/bash
# bench-cam.sh [SECONDS...]: webcam photos of the TV during PS3 bench runs, to check afterwards that
# each run showed what it should (a black screen, a menu instead of the demo, missing geometry make
# the run invalid). Run on the PC next to a run.sh session: it watches /dev/shm on the PS3 for new
# run directories (ps3-bench-*) and takes a photo SECONDS after each appears (default 30 and 60),
# saved as build/bench-cam/<run dir>-<s>s.jpg. It exits once no run.sh has been running for 90 s.
set -u
cd "$(dirname "$0")/.."
if [ $# -gt 0 ]; then offsets=("$@"); else offsets=(30 60); fi
S="ssh -i $HOME/.ssh/id_ps3 -o UserKnownHostsFile=build/ps3_known_hosts -o ConnectTimeout=20 tibi@10.0.0.77"
out=build/bench-cam; mkdir -p "$out"
declare -A seen
idle=0
while :; do
	state=$($S 'pgrep -x run.sh >/dev/null && echo run || echo idle; ls -d /dev/shm/ps3-bench-* 2>/dev/null' 2>/dev/null)
	[ -z "$state" ] && { sleep 10; continue; }
	if [ "${state%%$'\n'*}" = idle ]; then
		idle=$((idle + 5)); [ $idle -ge 90 ] && exit 0
	else
		idle=0
	fi
	while read -r d; do
		[ -n "$d" ] && [ -z "${seen[$d]:-}" ] || continue
		seen[$d]=1
		name=$(basename "$d")
		( for s in "${offsets[@]}"; do
			sleep $(( s - ${prev:-0} )); prev=$s
			$S "[ -d $d ]" 2>/dev/null || exit 0	# the run ended before this photo
			tools/webcam-grab.sh "$out/$name-${s}s.jpg" >/dev/null 2>&1
		  done ) &
	done < <(printf '%s\n' "$state" | tail -n +2)
	sleep 5
done
