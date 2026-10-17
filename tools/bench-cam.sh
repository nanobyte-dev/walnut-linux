#!/bin/bash
# bench-cam.sh [SECONDS...]: webcam photos of the TV during PS3 bench runs, to check afterwards that
# each run showed what it should (a black screen, a menu instead of the demo, missing geometry make
# the run invalid). Run on the PC next to a run.sh session: one ssh connection for the whole batch,
# in which a small loop on the PS3 prints NEW/END for each run directory (ps3-bench-* in /dev/shm);
# the PC takes a photo SECONDS after each NEW (default 30 and 60) unless its END came first, saved as
# build/bench-cam/<run dir>-<s>s.jpg. It exits once no run.sh has been running for 90 s.
# One connection matters: a new ssh every few seconds cost the PS3's PPE enough to slow the runs it
# watched (2026-10-17: load average 5 at idle, every game slower).
set -u
cd "$(dirname "$0")/.."
if [ $# -gt 0 ]; then offsets=("$@"); else offsets=(30 60); fi
out=build/bench-cam; mkdir -p "$out"; mark=$(mktemp -d); trap 'rm -rf "$mark"' EXIT
ssh -i "$HOME/.ssh/id_ps3" -o UserKnownHostsFile=build/ps3_known_hosts -o ConnectTimeout=20 \
	-o ServerAliveInterval=30 tibi@10.0.0.77 '
	seen=" "; idle=0
	while :; do
		for d in /dev/shm/ps3-bench-*; do
			[ -d "$d" ] || continue
			case "$seen" in *" $d "*) ;; *) echo "NEW $d"; seen="$seen$d ";; esac
		done
		for d in $seen; do [ -d "$d" ] || { echo "END $d"; seen=${seen/ $d / }; }; done
		if pgrep -x run.sh >/dev/null; then idle=0; else idle=$((idle + 5)); [ $idle -ge 90 ] && exit 0; fi
		sleep 5
	done' 2>/dev/null |
while read -r ev d; do
	name=$(basename "$d")
	case "$ev" in
	NEW) ( trap - EXIT; prev=0	# not the main trap: it would delete $mark when this subshell ends
		for s in "${offsets[@]}"; do
			sleep $((s - prev)); prev=$s
			[ -e "$mark/$name" ] && exit 0	# the run ended before this photo
			tools/webcam-grab.sh "$out/$name-${s}s.jpg" >/dev/null 2>&1
		done ) & ;;
	END) touch "$mark/$name" ;;
	esac
done
wait
