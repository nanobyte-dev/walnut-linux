#!/bin/bash
# session.sh GAME [LAUNCHER] [VARIANT]: a played session, the graded test (docs/GAMEDB.md "How a game is
# tested", docs/games/SUITE.md "Played sessions"). On the PS3, from a terminal of the desktop:
#   /var/tmp/bench/session.sh GAME [LAUNCHER] [VARIANT]   (or run.sh session GAME ...)
# LAUNCHER is a menu entry of the game, by file name (openarena-gl2.desktop) or Name ("OpenArena
# (GL 2.0)"): the game is started with that entry's Exec, and each launcher is rated on its own.
# The launchers are the menu entries the game's packages ship and any shown entry running one of
# their programs (launcher.py, as the Walnut Games app finds them). Without LAUNCHER: the game's
# only launcher, else the first (shipped entries by file name); the list is printed. An unknown
# LAUNCHER prints the choices and exits 2. A game without a menu entry runs the recipe's command.
# If the second argument names a recipe variant (variant_NAME in the recipe, or base) it is the
# VARIANT, not a launcher; a recipe variant on top of a launcher is the third argument. The recipe
# still gives the assets, PLAY_MARK, HUD and recording settings; its ENV applies.
# The game starts through ps3-game exactly as the desktop menu starts it (game mode, the VRAM swap
# on, vsync as shipped), in the live desktop: nothing is stopped, no swap emptied. It is recorded
# like any run (the Gallium HUD's frame times, the samplers, sound counters, dmesg, temperatures;
# client.sh with BENCH_SESSION=1 under `ps3-bench.sh session`) with a screenshot about every
# minute. There is no time limit: the session ends when the player quits the game. Then the form
# (session-form.py) asks the few things only the player knows, and writes session.toml. Around the
# game: freshhome.py (the game runs as shipped: its settings and data directories are renamed aside
# before it starts and the player's own renamed back after it, losslessly), the desktop's thaw
# (desktop_back_s, a clean quit), and client.sh's inputlog.py (the input devices, counts and times).
# The terminal is frozen by game mode while the game runs: this script moves itself into
# ps3-game.slice first (as ps3-game does), and logs to ~/.cache/ps3-session/ instead of the terminal.
#   SESSION_FRESH=0              the player's own settings (no fresh defaults; then the session tests that)
#   SESSION_SHOT_EVERY=SECONDS   screenshots interval (default 60), SESSION_SHOT_MAX=N the most (40;
#                                8 MB each on the disk until make bench-pull)
#   SESSION_FORM=0               no form (ask later: python3 -I session-form.py RUN_DIR GAME VARIANT)
#   SESSION_DRY=1                print what would run (the launcher and its command included)
#   SESSION_LAUNCHER_ARGS=...    extra launcher.py options (--apps, --runtime-dir, --dpkg: tests)
#   SESSION_SCOPE=0              don't move into ps3-game.slice (no user service manager, tests)
#   PS3_BENCH=CMD                the harness (default ps3-bench.sh; needs ps3-gpu-tools with `session`)
# No commands on the PS3 during a session (SSH logins and benchmarks are interference for what is
# measured); collect afterwards with make bench-pull.
set -u
here=$(cd "$(dirname "$0")" && pwd)
g=${1:-}; v=base; want=
[ -n "$g" ] || { sed -n '2,/^set -u/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2; }
R=${BENCH_RECIPES:-$here/recipes}
[ -e "$R/$g.sh" ] || { echo "session: no recipe $g (run.sh list)" >&2; exit 2; }
# the second argument: a recipe variant (backward compatible) or a launcher; the third a variant
if [ -n "${2:-}" ]; then
	if [ "$2" = base ] || ( CMD=(); ASSETS=; . "$R/$g.sh" > /dev/null 2>&1; declare -F "variant_$2" > /dev/null ); then
		v=$2
	else
		want=$2; v=${3:-base}
	fi
fi
B=${PS3_BENCH:-ps3-bench.sh}
OUT=${BENCH_OUT:-/var/tmp/ps3-bench}

# the shot points: every SESSION_SHOT_EVERY seconds from the start, at most SESSION_SHOT_MAX
every=${SESSION_SHOT_EVERY:-60}; max=${SESSION_SHOT_MAX:-40}
[[ $every =~ ^[0-9]+$ ]] && [[ $max =~ ^[0-9]+$ ]] && [ "$every" -gt 0 ] ||
	{ echo "session: SESSION_SHOT_EVERY/SESSION_SHOT_MAX: whole numbers" >&2; exit 2; }
shots=
for ((i = 1; i <= max; i++)); do shots+="$((i * every)) "; done
[ "$max" = 0 ] && shots=off

pkgs() {	# the game's packages: the owners of the recipe's binary (its CMD, and the session command)
	local p
	[ -n "${SESSION_PACKAGES:-}" ] && { echo "$SESSION_PACKAGES" | tr -s ' ' '\n'; return; }
	p=$( (CMD=(); ENV=(); ASSETS=; . "$R/$g.sh"; declare -F "variant_$v" > /dev/null && "variant_$v"
		declare -F session > /dev/null && session
		readlink -f "$(command -v "${CMD[0]}")") 2> /dev/null)
	dpkg -S "$p" 2> /dev/null | cut -d: -f1 | tr , ' ' | tr -s ' ' '\n' | sort -u
}
# the launcher: its Exec becomes the game's command (BENCH_CMD_FILE, NUL-separated, read by client.sh)
cmdfile=; lname=; lfile=
q=; [ -n "${PS3_SESSION_SCOPED:-}" ] && q=1	# the scoped copy repeats the choice quietly
{
	cmdfile=$(mktemp); trap 'rm -f "$cmdfile"' EXIT
	# shellcheck disable=SC2086
	out=$("$here/launcher.py" ${want:+--choose "$want"} --argv-out "$cmdfile" ${SESSION_LAUNCHER_ARGS:-} $(pkgs) ); lrc=$?
	if [ "$lrc" = 2 ]; then
		echo "session: $g has no launcher '$want'; the choices (file name, Name):" >&2
		"$here/launcher.py" ${SESSION_LAUNCHER_ARGS:-} $(pkgs) | sed '1d; s/^/  /' >&2
		exit 2
	elif [ "$lrc" != 0 ]; then
		rm -f "$cmdfile"; cmdfile=
		[ -z "$want" ] || { echo "session: $g has no menu launchers (asked for '$want')" >&2; exit 2; }
		[ -n "$q" ] || echo "session: $g: no menu launcher found, the recipe's command runs"
	else
		lfile=$(sed -n '1s/\t.*//p' <<< "$out"); lname=$(sed -n '1s/^[^\t]*\t//p' <<< "$out")
		if [ -z "$q" ]; then
			echo "session: launcher: $lname ($lfile)"
			if [ "$(sed -n '$=' <<< "$out")" -gt 2 ]; then
				echo "session: this game has several launchers (pick one: session.sh $g LAUNCHER):"
				sed '1d; s/^\([^\t]*\)\t\(.*\)$/  \2  [\1]/' <<< "$out"
			fi
		fi
	fi
}
if [ "${SESSION_DRY:-0}" = 1 ]; then
	[ -n "$cmdfile" ] && echo "command: $(xargs -0 printf '%q ' < "$cmdfile")"
	echo "would run: BENCH_SESSION=1 ${cmdfile:+BENCH_CMD_FILE=<command> BENCH_LAUNCHER='$lname' BENCH_LAUNCHER_FILE=$lfile }BENCH_SHOTS='${shots% }' $B session session-$g-$v -- $here/client.sh $g $v"
	echo "then: python3 -I $here/session-form.py <run dir> $g $v"
	exit 0
fi

# game mode freezes the desktop's slice, the terminal with it: continue in ps3-game.slice
if [ -z "${PS3_SESSION_SCOPED:-}" ] && [ "${SESSION_SCOPE:-1}" = 1 ]; then
	export PS3_SESSION_SCOPED=1
	[ -n "$cmdfile" ] && rm -f "$cmdfile"
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
	# the Walnut release (os-release's VERSION_ID): the version a test is filed under in the games database
	printf 'walnut\t%s\n' "$(. /etc/os-release 2>/dev/null; echo "${VERSION_ID:-}")"
}

homes() {	# the game's settings and data directories, relative to ~: the recipe's SESSION_HOME (globs
	# allowed), else the top directories of its BACKUP files (~/.X, ~/.config/X, ~/.local/share/X)
	( CMD=(); ENV=(); BACKUP=(); SESSION_HOME=(); ASSETS=; . "$R/$g.sh"
	  declare -F "variant_$v" > /dev/null && "variant_$v"
	  if [ ${#SESSION_HOME[@]} -gt 0 ]; then printf '%s\n' "${SESSION_HOME[@]}"; exit 0; fi
	  for f in "${BACKUP[@]}"; do
		r=${f#"$HOME"/}; [ "$r" = "$f" ] && continue
		case $r in .config/*/*) echo "$r" | cut -d/ -f1-2 ;; .local/share/*/*) echo "$r" | cut -d/ -f1-3 ;;
			*/*) echo "${r%%/*}" ;; esac
	  done | sort -u ) 2> /dev/null
}
# the game as shipped, on fresh defaults (the user, 2026-10-26): its directories renamed aside (a sibling
# on the same filesystem, instant, never a delete), the player's own renamed back after the game; a
# session cut short is put back at the next session's start (freshhome.py: the plan is written first)
fh_state=$HOME/.cache/ps3-session/freshhome; fh_plan=; fh_lines=; fh_done=
fresh_restore() {	# fresh_restore RUN_DIR|-: the player's own back; the session's small fresh ones to RUN_DIR
	[ -n "$fh_plan" ] && [ -z "$fh_done" ] || return 0
	fh_done=1
	python3 -I "$here/freshhome.py" restore "$fh_plan" "${1:--}"
}
if [ "${SESSION_FRESH:-1}" = 1 ]; then
	mapfile -t hl < <(homes)
	if [ ${#hl[@]} -gt 0 ]; then
		echo "session: fresh defaults: setting aside ~/${hl[*]}"
		if out=$(python3 -I "$here/freshhome.py" aside "$fh_state" "$g" "${hl[@]}"); then
			fh_plan=$(head -1 <<< "$out"); fh_lines=$(sed 1d <<< "$out")
			trap 'fresh_restore -; [ -n "$cmdfile" ] && rm -f "$cmdfile"' EXIT
		else
			echo "session: freshhome.py failed: the game runs on the player's own settings" >&2
		fi
	else
		echo "session: $g: no SESSION_HOME or BACKUP in its recipe: the player's own settings"
	fi
fi
# the desktop's thaw after the game (game mode freezes ps3-desktop.slice while it runs): when it came back,
# for Stability's clean quit (bench.txt desktop_back_s, from the game's end at t0 + seconds); a second's
# resolution, builtins only (no fork a second: the tick is a read timing out on a fifo nobody writes)
cg=$(systemctl --user show -p ControlGroup --value ps3-desktop.slice 2> /dev/null)
fz=/sys/fs/cgroup$cg/cgroup.freeze
thawf=$(mktemp)
watch_pid=
if [ -n "$cg" ] && [ -e "$fz" ]; then
	( mkfifo "$thawf.tick"; exec {tk}<> "$thawf.tick"; rm -f "$thawf.tick"
	  was=0; while :; do f=0; read -r f < "$fz" 2> /dev/null || f=0
		[ "$f" = 1 ] && [ "$was" = 0 ] && echo "frozen" >> "$thawf"
		[ "$f" = 0 ] && [ "$was" = 1 ] && echo "thaw $EPOCHREALTIME" >> "$thawf"
		was=$f; read -r -t 1 -u "$tk"; done ) > /dev/null 2>&1 &
	watch_pid=$!
fi

tmp=$(mktemp)
[ -n "$cmdfile" ] && export BENCH_CMD_FILE=$cmdfile BENCH_LAUNCHER=$lname BENCH_LAUNCHER_FILE=$lfile
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
for _ in $(seq 40); do [ "$(cat "$fz" 2> /dev/null || echo 0)" = 0 ] && break; sleep 0.5; done
sleep 1
if [ -n "$watch_pid" ]; then
	kill "$watch_pid" 2> /dev/null; wait "$watch_pid" 2> /dev/null
	end=$(awk -F= '$1 == "t0" { t = $2 } $1 == "seconds" { s = $2 } END { if (t != "" && s != "") printf "%.3f", t + s }' "$d/bench.txt")
	if [ "$(cat "$fz" 2> /dev/null || echo 0)" = 1 ]; then echo "desktop_stuck=1" >> "$d/bench.txt"
	elif ! grep -q frozen "$thawf"; then echo "desktop_back_s=0" >> "$d/bench.txt"	# never frozen: there all along
	elif [ -n "$end" ] && grep -q '^thaw ' "$thawf"; then
		awk -v e="$end" '$1 == "thaw" { t = $2 } END { b = t - e; printf "desktop_back_s=%.1f\n", b > 0 ? b : 0 }' "$thawf" >> "$d/bench.txt"
	fi
fi
rm -f "$thawf"
[ -n "$fh_lines" ] && printf '%s\n' "$fh_lines" >> "$d/bench.txt"
[ -n "$fh_plan" ] && fresh_restore "$d" >> "$d/bench.txt"
if [ "${SESSION_FORM:-1}" = 1 ]; then
	python3 -I "$here/session-form.py" "$d" "$g" "$v" || echo "session: no session.toml (python3 -I $here/session-form.py $d $g $v)"
fi
echo ">> $d"
echo "session: done. On the PC: make bench-pull"
