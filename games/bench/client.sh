#!/bin/bash
# client.sh <game> [variant]: the X client run.sh hands to `ps3-bench.sh x`: one game run from its
# recipe (recipes/<game>.sh, docs/GAME-PLAN.md). Sets up the game's files, starts it (through
# ps3-game when the recipe says GAMEMODE=1), passes its output on (ps3-bench.sh stamps it into
# out.log), stops it at END_RE or after DURATION, then writes $BENCH_DIR/bench.txt (key=value:
# game, variant, how it ended, fps, longest frame, load seconds, peak RSS, sound state) for
# report.py, and restores the config files the game rewrites.
{
set -u
here=$(cd "$(dirname "$0")" && pwd)
GAME=${1:?game}; VARIANT=${2:-base}
D=${BENCH_DIR:-/dev/shm/bench-client-$$}; mkdir -p "$D"
ASSETS=$here/assets

# recipe interface (README in recipes/): the recipe sets these, a variant_<name> function changes them
CMD=(); ENV=(); BACKUP=(); GAMEMODE=0; HUD=1; COMM=; WM=openbox
END_RE=; DURATION=0; HUD_SKIP=12; FPS_RE=; MAXMS_RE=; LOAD_RE=; LOAD_MARK=; RESULT_FILE=; SOUND_AT=15
setup() { :; }; parse() { :; }
. "$here/recipes/$GAME.sh" || exit 2
if declare -F "variant_$VARIANT" >/dev/null; then "variant_$VARIANT"
elif [ "$VARIANT" != base ]; then echo "!! $GAME: no variant $VARIANT"; exit 2; fi
[ ${#CMD[@]} -gt 0 ] || { echo "!! $GAME: no CMD"; exit 2; }

# config files the game rewrites on exit: restored after every run, whatever happens. The copies
# are on disk with a list of what was saved: a run cut short by a reboot (a wedged RSX) leaves them,
# and the next run of the game restores them first.
B=/var/tmp/ps3-bench/backup/$GAME
restore() {
	[ -e "$B/.saved" ] || return 0
	local f s
	while IFS= read -r f; do
		s=$B/$(echo "$f" | tr / %)
		if [ -e "$s" ]; then cp -p "$s" "$f"; else rm -f "$f"; fi
	done < "$B/.saved"
	rm -rf "$B"
}
restore
mkdir -p "$B"
for f in "${BACKUP[@]}"; do
	[ -e "$f" ] && cp -p "$f" "$B/$(echo "$f" | tr / %)"
	printf '%s\n' "$f" >> "$B/.saved.new"
done
[ -e "$B/.saved.new" ] && mv "$B/.saved.new" "$B/.saved"
trap restore EXIT	# also when this script dies of an error
setup || { echo "!! $GAME: setup failed"; exit 2; }

if [ "$HUD" = 1 ] && [ -z "${GALLIUM_HUD:-}" ]; then	# per-second fps, as ps3-bench.sh BENCH_HUD=1
	mkdir -p "$D/hud"
	ENV+=(GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_PERIOD=1 GALLIUM_HUD_DUMP_DIR=$D/hud)
fi
launch=(); [ "$GAMEMODE" = 1 ] && launch=(ps3-game)

# a window manager, as on the desktop: without one SDL's fullscreen request goes unanswered and the
# game's window never reaches the screen (it still renders). Plain openbox: no autostart, no panel.
wm=
if [ -n "$WM" ] && [ -n "${DISPLAY:-}" ]; then
	$WM > "$D/wm.log" 2>&1 & wm=$!
	for i in $(seq 50); do
		xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id' && break; sleep 0.1
	done
fi
# no screen saver or DPMS blanking: a run longer than X's 10 idle minutes (a soak) would go
# black, and a run with nothing on screen doesn't count
[ -n "${DISPLAY:-}" ] && xset s off -dpms 2>/dev/null

helpers=()
# the game in its own process group (job control), so stopping it reaches every process it forks;
# its output through a fifo, read line by line here
set -m
fifo=$D/.game.fifo; rm -f "$fifo"; mkfifo "$fifo"
t0=$EPOCHREALTIME
# stdbuf: line-buffered stdio into the pipe, so END_RE and LOAD_MARK lines arrive when printed
env "${ENV[@]}" "${launch[@]}" stdbuf -oL -eL "${CMD[@]}" < /dev/null > "$fifo" 2>&1 &
gp=$!
set +m
exec 3< "$fifo"
stop_game() {
	kill -0 "$gp" 2>/dev/null || return 0
	kill -TERM -- -"$gp" 2>/dev/null
	for i in $(seq 50); do kill -0 "$gp" 2>/dev/null || return 0; sleep 0.2; done
	kill -KILL -- -"$gp" 2>/dev/null
}
trap 'stop_game; kill "${helpers[@]}" $wm 2>/dev/null; exit 1' HUP INT TERM

# peak RSS of the game process (COMM), and the playback state of the sound card at SOUND_AT s
watch_game() {
	local p= i
	for i in $(seq 120); do
		p=$(pgrep -g "$gp" -x "$COMM" | head -1); [ -n "$p" ] && break; sleep 0.5
	done
	[ -n "$p" ] || return
	echo "$p" > "$D/.pid"
	# builtins only, no forks every second (the client's CPU time counts them): the tick is a read
	# timing out on a fifo nobody writes
	local k v u hwm= tick
	mkfifo "$D/.tick"; exec {tick}<> "$D/.tick"; rm -f "$D/.tick"
	while [ -e "/proc/$p/status" ]; do
		while read -r k v u; do [ "$k" = VmHWM: ] && hwm=$v; done < "/proc/$p/status" 2>/dev/null
		[ -n "$hwm" ] && printf '%s\n' "$hwm" > "$D/.hwm"
		read -r -t 1 -u "$tick"
	done
}
# (helpers write nothing to stdout: it is ps3-bench.sh's pipe, held open by whoever has it)
[ -n "$COMM" ] && { watch_game > /dev/null 2>&1 & helpers+=($!); }
(sleep "$SOUND_AT"; cat /proc/asound/card*/pcm*p/sub*/status 2>/dev/null |
	awk '$1 == "state:" { s = s (s ? "," : "") $2 } END { print s ? s : "none" }' > "$D/.sound") \
	> /dev/null 2>&1 & helpers+=($!)

ended=exit; load=; t_load=; l=
# time without forks (the client's CPU time counts them): us sets US to microseconds since the epoch
us() { local t=${1:-$EPOCHREALTIME}; US=$(( ${t%.*} * 1000000 + 10#${t#*.} )); }
us "$t0"; u0=$US
age() { us; local d=$(( US - u0 )); printf '%d.%02d' $((d / 1000000)) $((d % 1000000 / 10000)); }
while :; do
	l=
	if [ "$DURATION" -gt 0 ]; then
		us; left=$(( u0 + DURATION * 1000000 - US ))
		[ $left -le 0 ] && { ended=duration; break; }
		printf -v left '%d.%06d' $((left / 1000000)) $((left % 1000000))
		IFS= read -r -t "$left" -u 3 l; rc=$?
		[ $rc -gt 128 ] && { ended=duration; break; }
	else
		IFS= read -r -u 3 l; rc=$?
	fi
	[ $rc -ne 0 ] && [ -z "$l" ] && break	# end of output: the game is gone
	printf '%s\n' "$l"; printf '%s\n' "$l" >> "$D/game.log"
	if [ -z "$t_load" ] && [ -n "$LOAD_MARK" ] && [[ $l =~ $LOAD_MARK ]]; then t_load=$(age); fi
	if [ -n "$END_RE" ] && [[ $l =~ $END_RE ]]; then ended=end; break; fi
done
t_run=$(age)
stop_game
while IFS= read -r -t 2 -u 3 l; do printf '%s\n' "$l"; printf '%s\n' "$l" >> "$D/game.log"; done
wait "$gp" 2>/dev/null; rc=$?
exec 3<&-; rm -f "$fifo"
kill "${helpers[@]}" $wm 2>/dev/null; wait 2>/dev/null

last() {	# last <regex>: the first capture group of the regex's last match in game.log
	local re=$1 v= l
	[ -n "$re" ] && [ -e "$D/game.log" ] || return 0
	while IFS= read -r l; do [[ $l =~ $re ]] && v=${BASH_REMATCH[1]}; done < "$D/game.log"
	printf '%s' "$v"
}
load=$(last "$LOAD_RE"); [ -z "$load" ] && load=$t_load
[ -n "$RESULT_FILE" ] && for f in $RESULT_FILE; do [ -e "$f" ] && cp -p "$f" "$D/"; done
{ echo "game=$GAME"; echo "variant=$VARIANT"; echo "comm=$COMM"
  echo "ended=$ended"; echo "rc=$rc"; echo "seconds=$t_run"
  echo "fps=$(last "$FPS_RE")"; echo "maxms=$(last "$MAXMS_RE")"; echo "load=$load"
  echo "hud_skip=$HUD_SKIP"; echo "hwm=$(cat "$D/.hwm" 2>/dev/null)"; echo "sound=$(cat "$D/.sound" 2>/dev/null)"
  parse; } > "$D/bench.txt"
rm -f "$D/.hwm" "$D/.pid" "$D/.sound"
sed 's/^/bench: /' "$D/bench.txt"
exit 0
}
