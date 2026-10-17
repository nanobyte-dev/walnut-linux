#!/bin/bash
# client.sh <game> [variant]: the X client run.sh hands to `ps3-bench.sh x`: one game run from its
# recipe (recipes/<game>.sh, docs/GAME-PLAN.md). Sets up the game's files, starts it (through
# ps3-game when the recipe says GAMEMODE=1), passes its output on (ps3-bench.sh stamps it into
# out.log), stops it at END_RE, after DURATION or when the measured window is complete, then
# writes $BENCH_DIR/bench.txt (key=value: game, variant, how it ended, fps, longest frame, load
# seconds, the steady-state window and passes, peak RSS, sound state) for report.py, and restores
# the config files the game rewrites.
# Steady state (the user, 2026-10-17): WARMUP seconds after the load ends (LOAD_MARK or LOAD_RE when
# the recipe has one, else the first frame the HUD saw) don't count, then at least MEASURE seconds
# are measured. PASSES=1: every FPS_RE line ends one pass of a demo the game repeats; a pass counts
# when it starts at least 0.9 x WARMUP after the load, and the game is stopped once the counted
# passes cover 0.95 x MEASURE seconds (ended=window). WINDOW_STOP=1: the game is stopped once the window
# (load + WARMUP + MEASURE) is over (the HUD measures it). STEADY_RE/STEADY_MAXMS_RE: the game's own
# result over its own window. bench.txt gets load_end, window, pass<N> and steady_* for report.py.
{
set -u
here=$(cd "$(dirname "$0")" && pwd)
GAME=${1:?game}; VARIANT=${2:-base}
D=${BENCH_DIR:-/dev/shm/bench-client-$$}; mkdir -p "$D"
ASSETS=$here/assets

# recipe interface (README in recipes/): the recipe sets these, a variant_<name> function changes them
CMD=(); ENV=(); BACKUP=(); GAMEMODE=0; HUD=1; COMM=; WM=openbox
# DURATION: a safety cap (load + 60 s warm-up + 120 s measured + margin); recipes set their own
END_RE=; DURATION=270; HUD_SKIP=12; WARMUP=60; MEASURE=120; PASSES=0; WINDOW_STOP=0; STEADY_RE=
STEADY_MAXMS_RE=; FPS_RE=; MAXMS_RE=; LOAD_RE=; LOAD_MARK=; RESULT_FILE=; SOUND_AT=15
setup() { :; }; parse() { :; }
. "${BENCH_RECIPES:-$here/recipes}/$GAME.sh" || exit 2
if declare -F "variant_$VARIANT" >/dev/null; then "variant_$VARIANT"
elif [ "$VARIANT" != base ]; then echo "!! $GAME: no variant $VARIANT"; exit 2; fi
[ ${#CMD[@]} -gt 0 ] || { echo "!! $GAME: no CMD"; exit 2; }

# config files the game rewrites on exit: restored after every run, whatever happens. The copies
# are on disk with a list of what was saved: a run cut short by a reboot (a wedged RSX) leaves them,
# and the next run of the game restores them first.
B=${BENCH_BACKUP:-/var/tmp/ps3-bench/backup}/$GAME
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

# per-second fps (hud/fps, as ps3-bench.sh BENCH_HUD=1) and every frame's time in ms (hud/frametime(ms):
# the HUD's frametime graph adds a value at every swap whatever the period; the first frame has none)
hud_file=
if [ "$HUD" = 1 ] && [ -z "${GALLIUM_HUD:-}" ]; then
	mkdir -p "$D/hud"
	ENV+=(GALLIUM_HUD=fps,frametime GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_PERIOD=1 GALLIUM_HUD_DUMP_DIR=$D/hud)
	hud_file="$D/hud/frametime(ms)"
elif [ -n "${GALLIUM_HUD_DUMP_DIR:-}" ]; then	# ps3-bench.sh's BENCH_HUD=1: per-second fps only
	hud_file=$GALLIUM_HUD_DUMP_DIR/fps
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
# stopped from outside (ps3-bench.sh's timeout, the thermal watchdog): still say how it ended
trap 'stop_game; kill "${helpers[@]}" $wm 2>/dev/null
	{ echo "game=$GAME"; echo "variant=$VARIANT"; echo "comm=$COMM"; echo "ended=killed"
	  echo "hwm=$(cat "$D/.hwm" 2>/dev/null)"; echo "sound=$(cat "$D/.sound" 2>/dev/null)"; } > "$D/bench.txt"
	exit 1' HUP INT TERM

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
# the recipe's own helper while the game runs (input recording/replay); stopped with the others
declare -F during >/dev/null && { during > "$D/during.log" 2>&1 & helpers+=($!); }
(sleep "$SOUND_AT"; cat /proc/asound/card*/pcm*p/sub*/status 2>/dev/null |
	awk '$1 == "state:" { s = s (s ? "," : "") $2 } END { print s ? s : "none" }' > "$D/.sound") \
	> /dev/null 2>&1 & helpers+=($!)

ended=exit; load=; t_load=; l=; part=
# time without forks (the client's CPU time counts them): us sets US to microseconds since the epoch
us() { local t=${1:-$EPOCHREALTIME}; US=$(( ${t%.*} * 1000000 + 10#${t#*.} )); }
us "$t0"; u0=$US
age() { us; local d=$(( US - u0 )); printf '%d.%02d' $((d / 1000000)) $((d % 1000000 / 10000)); }
secs() { printf -v "$1" '%d.%02d' $(($2 / 1000000)) $(($2 % 1000000 / 10000)); }	# secs VAR MICROSECONDS
# the steady-state window, in microseconds from the start: load_us when the load ended (LOAD_MARK,
# the first LOAD_RE line, else the HUD's first frame), hud_us when the HUD's dump got its first
# value, stop_us when WINDOW_STOP ends the run; passes (PASSES=1) in PASS, measured their counted time
load_us=; hud_us=; stop_us=; np=0; plast=; measured=0; win0=; win1=; PASS=()
loaded() {	# the load ended now (US)
	load_us=$(( US - u0 ))
	[ "$WINDOW_STOP" = 1 ] && stop_us=$(( load_us + (WARMUP + MEASURE + 2) * 1000000 ))
}
pass() {	# pass LINE (BASH_REMATCH from FPS_RE): a pass ended now; true once the counted passes cover MEASURE
	local f=${BASH_REMATCH[1]} ms= end=$(( US - u0 )) start=${plast:-${load_us:-0}} c=0 s e
	[ -n "$MAXMS_RE" ] && [[ $1 =~ $MAXMS_RE ]] && ms=${BASH_REMATCH[1]}
	np=$((np + 1)); plast=$end
	if [ $(( start - ${load_us:-0} )) -ge $(( WARMUP * 900000 )) ]; then
		c=1; measured=$(( measured + end - start )); [ -n "$win0" ] || win0=$start; win1=$end
	fi
	secs s "$start"; secs e "$end"
	PASS+=("pass$np=$s $e ${f:--} ${ms:--} $c")	# start end fps maxms counted
	# MEASURE less 5%: a pass of 59 s twice is enough, not a third pass for the last second
	[ "$c" = 1 ] && [ "$measured" -ge $(( MEASURE * 950000 )) ]
}
while :; do
	us
	if [ -n "$hud_file" ] && [ -z "$hud_us" ] && [ -s "$hud_file" ]; then
		hud_us=$(( US - u0 ))
		[ -z "$load_us$LOAD_MARK$LOAD_RE" ] && loaded
	fi
	lim=; why=; wt=
	[ "$DURATION" -gt 0 ] && { lim=$(( DURATION * 1000000 )); why=duration; }
	[ -n "$stop_us" ] && { [ -z "$lim" ] || [ "$stop_us" -lt "$lim" ]; } && { lim=$stop_us; why=window; }
	if [ -n "$lim" ]; then
		wt=$(( lim - US + u0 ))
		[ $wt -le 0 ] && { ended=$why; break; }
	fi
	# until the HUD's first value: a look at its dump file every 0.25 s
	[ -n "$hud_file" ] && [ -z "$hud_us" ] && { [ -z "$wt" ] || [ $wt -gt 250000 ]; } && wt=250000
	l=
	if [ -n "$wt" ]; then
		printf -v wt '%d.%06d' $((wt / 1000000)) $((wt % 1000000))
		IFS= read -r -t "$wt" -u 3 l; rc=$?
		[ $rc -gt 128 ] && { part+=$l; continue; }	# timed out: a partial line is kept
	else
		IFS= read -r -u 3 l; rc=$?
	fi
	l=$part$l; part=
	[ $rc -ne 0 ] && [ -z "$l" ] && break	# end of output: the game is gone
	printf '%s\n' "$l"; printf '%s\n' "$l" >> "$D/game.log"
	us
	if [ -z "$load_us" ] && { { [ -n "$LOAD_MARK" ] && [[ $l =~ $LOAD_MARK ]]; } ||
	                          { [ -n "$LOAD_RE" ] && [[ $l =~ $LOAD_RE ]]; }; }; then loaded; fi
	if [ -z "$t_load" ] && [ -n "$LOAD_MARK" ] && [[ $l =~ $LOAD_MARK ]]; then t_load=$(age); fi
	if [ "$PASSES" = 1 ] && [ -n "$FPS_RE" ] && [[ $l =~ $FPS_RE ]] && pass "$l"; then ended=window; break; fi
	if [ -n "$END_RE" ] && [[ $l =~ $END_RE ]]; then ended=end; break; fi
done
us; run_us=$(( US - u0 )); secs t_run "$run_us"
stop_game
while IFS= read -r -t 2 -u 3 l; do l=$part$l; part=; printf '%s\n' "$l"; printf '%s\n' "$l" >> "$D/game.log"; done
wait "$gp" 2>/dev/null; rc=$?
exec 3<&-; rm -f "$fifo"
kill "${helpers[@]}" $wm 2>/dev/null; wait 2>/dev/null

last() {	# last <regex>: the first capture group of the regex's last match in game.log
	local re=$1 v= l
	[ -n "$re" ] && [ -e "$D/game.log" ] || return 0
	while IFS= read -r l; do [[ $l =~ $re ]] && v=${BASH_REMATCH[1]}; done < "$D/game.log"
	printf '%s' "$v"
}
first() {	# first <regex>: the first capture group of the regex's first match in game.log
	local re=$1 l
	[ -n "$re" ] && [ -e "$D/game.log" ] || return 0
	while IFS= read -r l; do [[ $l =~ $re ]] && { printf '%s' "${BASH_REMATCH[1]}"; return 0; }; done < "$D/game.log"
}
# a demo repeated (PASSES=1): fps, longest frame and load are the first pass's, as a single run had them
pick=last; [ "$PASSES" = 1 ] && pick=first
load=$($pick "$LOAD_RE"); [ -z "$load" ] && load=$t_load
# the window: the counted passes, else WARMUP s after the load (or the HUD's first frame) for at most
# MEASURE s, as far as the run went
[ -z "$load_us" ] && load_us=$hud_us
window=
if [ -n "$win0" ]; then secs w0 "$win0"; secs w1 "$win1"; window="$w0 $w1"
elif [ "$PASSES" != 1 ] && [ -n "$load_us" ]; then
	a=$(( load_us + WARMUP * 1000000 )); b=$(( a + MEASURE * 1000000 )); [ $b -gt $run_us ] && b=$run_us
	[ $b -gt $a ] && { secs w0 "$a"; secs w1 "$b"; window="$w0 $w1"; }
fi
[ -n "$load_us" ] && secs load_end "$load_us"; [ -n "$hud_us" ] && secs hud_t1 "$hud_us"
secs measured "$measured"
[ -n "$RESULT_FILE" ] && for f in $RESULT_FILE; do [ -e "$f" ] && cp -p "$f" "$D/"; done
{ echo "game=$GAME"; echo "variant=$VARIANT"; echo "comm=$COMM"
  echo "ended=$ended"; echo "rc=$rc"; echo "seconds=$t_run"
  echo "fps=$($pick "$FPS_RE")"; echo "maxms=$($pick "$MAXMS_RE")"; echo "load=$load"
  # steady state: settings, the load's end and the HUD's first value (s from the start), the window
  echo "warmup=$WARMUP"; echo "measure=$MEASURE"; echo "load_end=${load_end:-}"; echo "hud_t1=${hud_t1:-}"
  echo "window=$window"
  [ "$PASSES" = 1 ] && { echo "passes=$np"; echo "measured=$measured"; printf '%s\n' "${PASS[@]}"; }
  [ -n "$STEADY_RE" ] && { echo "steady_fps=$(first "$STEADY_RE")"; echo "steady_maxms=$(first "$STEADY_MAXMS_RE")"; }
  echo "hud_skip=$HUD_SKIP"; echo "hwm=$(cat "$D/.hwm" 2>/dev/null)"; echo "sound=$(cat "$D/.sound" 2>/dev/null)"
  # ALSA's "underrun occurred": the sound buffer ran dry (what a listener hears as chopping)
  echo "underruns=$(grep -ac 'underrun occurred' "$D/game.log" 2>/dev/null)"
  parse; } > "$D/bench.txt"
rm -f "$D/.hwm" "$D/.pid" "$D/.sound"
sed 's/^/bench: /' "$D/bench.txt"
exit 0
}
