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
# The PS3's address, user and key: PS3_IP, PS3_USER, PS3_SSH_KEY from the environment or config.local.mk
{ read -r ps3_ip; read -r ps3_user; read -r ps3_key; } < <(tools/config-get.sh PS3_IP PS3_USER PS3_SSH_KEY)
[ -n "$ps3_ip" ] && [ -n "$ps3_user" ] || { echo "bench-cam.sh: set PS3_IP and PS3_USER (config.local.mk)" >&2; exit 1; }
out=build/bench-cam; mkdir -p "$out"; mark=$(mktemp -d); trap 'rm -rf "$mark"' EXIT
ssh -i "$ps3_key" -o UserKnownHostsFile=build/ps3_known_hosts -o ConnectTimeout=20 \
	-o ServerAliveInterval=30 "$ps3_user@$ps3_ip" '
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
