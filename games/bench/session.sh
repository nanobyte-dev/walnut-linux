#!/bin/bash
# session.sh GAME [VARIANT]: a played session, the graded test (docs/GAMEDB.md "How a game is
# tested", docs/games/SUITE.md "Played sessions"). On the PS3, from a terminal of the desktop:
#   /var/tmp/bench/session.sh GAME [VARIANT]      (or run.sh session GAME [VARIANT])
# The game starts through ps3-game exactly as the desktop menu starts it (game mode, the VRAM swap
# on, vsync as shipped), in the live desktop: nothing is stopped, no swap emptied. It is recorded
# like any run (the Gallium HUD's frame times, the samplers, sound counters, dmesg, temperatures;
# client.sh with BENCH_SESSION=1 under `ps3-bench.sh session`) with a screenshot about every
# minute. There is no time limit: the session ends when the player quits the game. Then the form
# (session-form.py) asks about sound, music, controls and notes, and writes session.toml.
# The terminal is frozen by game mode while the game runs: this script moves itself into
# ps3-game.slice first (as ps3-game does), and logs to ~/.cache/ps3-session/ instead of the terminal.
#   SESSION_SHOT_EVERY=SECONDS   screenshots interval (default 60), SESSION_SHOT_MAX=N the most (40;
#                                8 MB each on the disk until make bench-pull)
#   SESSION_FORM=0               no form (ask later: python3 -I session-form.py RUN_DIR GAME VARIANT)
#   SESSION_DRY=1                print what would run
#   SESSION_SCOPE=0              don't move into ps3-game.slice (no user service manager, tests)
#   PS3_BENCH=CMD                the harness (default ps3-bench.sh; needs ps3-gpu-tools with `session`)
# No commands on the PS3 during a session (SSH logins and benchmarks are interference for what is
# measured); collect afterwards with make bench-pull.
set -u
here=$(cd "$(dirname "$0")" && pwd)
g=${1:-}; v=${2:-base}
[ -n "$g" ] || { sed -n '2,/^set -u/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2; }
R=${BENCH_RECIPES:-$here/recipes}
[ -e "$R/$g.sh" ] || { echo "session: no recipe $g (run.sh list)" >&2; exit 2; }
B=${PS3_BENCH:-ps3-bench.sh}
OUT=${BENCH_OUT:-/var/tmp/ps3-bench}

# the shot points: every SESSION_SHOT_EVERY seconds from the start, at most SESSION_SHOT_MAX
every=${SESSION_SHOT_EVERY:-60}; max=${SESSION_SHOT_MAX:-40}
[[ $every =~ ^[0-9]+$ ]] && [[ $max =~ ^[0-9]+$ ]] && [ "$every" -gt 0 ] ||
	{ echo "session: SESSION_SHOT_EVERY/SESSION_SHOT_MAX: whole numbers" >&2; exit 2; }
shots=
for ((i = 1; i <= max; i++)); do shots+="$((i * every)) "; done
[ "$max" = 0 ] && shots=off

if [ "${SESSION_DRY:-0}" = 1 ]; then
	echo "would run: BENCH_SESSION=1 BENCH_SHOTS='${shots% }' $B session session-$g-$v -- $here/client.sh $g $v"
	echo "then: python3 -I $here/session-form.py <run dir> $g $v"
	exit 0
fi

# game mode freezes the desktop's slice, the terminal with it: continue in ps3-game.slice
if [ -z "${PS3_SESSION_SCOPED:-}" ] && [ "${SESSION_SCOPE:-1}" = 1 ]; then
	export PS3_SESSION_SCOPED=1
	if systemctl --user show-environment > /dev/null 2>&1; then
		exec systemd-run --user --scope --slice=ps3-game.slice --unit="ps3-session-$$" -q "$0" "$@"
	fi
	echo "session: no user service manager: the terminal would not be frozen, but game mode may not come on" >&2
fi

export DISPLAY=${DISPLAY:-:0}
if pgrep -x ps3-bench.sh > /dev/null || [ -e /dev/shm/ps3-bench.lock ]; then
	echo "session: a bench session holds /dev/shm/ps3-bench.lock (the desktop is stopped?); ps3-bench.sh desktop first" >&2
	exit 1
fi
free_mb=$(df -Pm "$OUT" 2> /dev/null | awk 'NR == 2 { print $4 }')
[ -n "${free_mb:-}" ] && [ "$free_mb" -lt $((max * 8 + 500)) ] &&
	echo "session: only $free_mb MB free in $OUT: fewer screenshots wanted (SESSION_SHOT_MAX=N)" >&2

log=$HOME/.cache/ps3-session/$(date -u +%Y%m%dT%H%M%SZ)-$g-$v.log
mkdir -p "${log%/*}"
echo "session: $g $v; the game starts now, play it normally and quit it when done; the form follows."
echo "session: log $log"
if [ -z "${SSH_TTY:-}" ] && { [ -t 1 ] || [ -t 2 ]; }; then exec > "$log" 2>&1; else exec > >(tee "$log") 2>&1; fi

versions() {	# the game's package and the graphics stack, as run.sh's
	local p
	p=$( (CMD=(); ASSETS=; . "$R/$g.sh"; declare -F "variant_$v" > /dev/null && "variant_$v"
		readlink -f "$(command -v "${CMD[0]}")") 2> /dev/null)
	dpkg-query -W $(dpkg -S "$p" 2> /dev/null | cut -d: -f1 | tr , ' ') mesa-libgallium ps3rsx-modules-ps3 libspeq0 libfluidsynth3 libopenal1 libsdl3-0 ps3-gpu-tools ps3-desktop 2> /dev/null
	uname -r
}

tmp=$(mktemp)
env BENCH_SESSION=1 BENCH_SHOTS="${shots% }" BENCH_WHAT="session $g $v" \
	"$B" session "session-$g-$v" -- "$here/client.sh" "$g" "$v" 2>&1 | tee "$tmp"
rc=${PIPESTATUS[0]}
d=$(sed -n 's/^>> \(\/[^ ]*\)$/\1/p' "$tmp" | tail -1)
rm -f "$tmp"
if [ -z "$d" ] || [ ! -d "$d" ]; then echo "session: no run directory (ps3-bench.sh rc $rc)"; exit 1; fi
versions > "$d/versions"
# the screenshots from the disk into the run directory (client.sh wrote them beside it)
[ -d "$OUT/.shots-${d##*/}" ] && mv "$OUT/.shots-${d##*/}" "$d/shots"
echo "== $g $v: $(grep -E '^(ended|ended_why|rc|seconds|play_from|play_seconds|underruns|oal_wedge|snd_stall)=' "$d/bench.txt" 2> /dev/null | tr '\n' ' ')"
[ -e "$d/THERMAL" ] && echo "!! thermal alert: let the PS3 cool down"
[ -e "$d/TEMPLIMIT" ] && echo "!! $(cat "$d/TEMPLIMIT")"

# the desktop comes back about a second after the last game goes: the form needs it (a window)
cg=$(systemctl --user show -p ControlGroup --value ps3-desktop.slice 2> /dev/null)
fz=/sys/fs/cgroup$cg/cgroup.freeze
for _ in $(seq 40); do [ "$(cat "$fz" 2> /dev/null || echo 0)" = 0 ] && break; sleep 0.5; done
sleep 1
if [ "${SESSION_FORM:-1}" = 1 ]; then
	python3 -I "$here/session-form.py" "$d" "$g" "$v" || echo "session: no session.toml (python3 -I $here/session-form.py $d $g $v)"
fi
echo ">> $d"
echo "session: done. On the PC: make bench-pull"
