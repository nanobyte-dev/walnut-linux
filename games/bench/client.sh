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
# result over its own window. WIN_ON_RE/WIN_OFF_RE: the game marks its own window, maybe in pieces
# (each ON line opens one, the next OFF line closes it; it replaces load + WARMUP). bench.txt gets
# load_end, window (start end pairs), pass<N> and steady_* for report.py; t0 (the start, epoch
# seconds) places them on the clock of ps3-stallrec's record (stalls.py).
# Profiling (docs/PROFILING.md). BENCH_PERF_CTL (ps3-bench.sh BENCH_PERF=1, run.sh PERF=1): perf's
# control fifos as ctl:ack pairs; perf is switched on WARMUP s after the load (0.9 x WARMUP with
# PASSES=1, where the first counted pass may start that early; with WIN_ON_RE at each ON line) and
# off at the window's end (MEASURE s later, the last counted pass, each OFF line); bench.txt gets
# perf_win (on off pairs), perf_cpu (perf's own CPU seconds in them), perf_ack. BENCH_MEM=1 (run.sh
# MEM=1): the game's smaps, smaps_rollup and status, ps3rsx's buffer census (debugfs ps3rsx_mm, once
# a snapshot) and glibc's malloc statistics (ps3-mallocstat.so preloaded: BENCH_MALLOCSTAT is its
# path, "" turns it off) at the window's start, middle and end into $D/mem/ (memprof.py); bench.txt
# gets mem_snaps (label:seconds:ms reading smaps). BENCH_MALLOC: glibc malloc presets (below,
# docs/MEMORY.md).
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
STEADY_MAXMS_RE=; WIN_ON_RE=; WIN_OFF_RE=; FPS_RE=; MAXMS_RE=; LOAD_RE=; LOAD_MARK=; RESULT_FILE=; SOUND_AT=15
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

# per-second fps (hud/fps, as ps3-bench.sh BENCH_HUD=1) and every frame's time in ms (hud/frametime_(ms):
# the HUD's frametime graph adds a value at every swap whatever the period; the first frame has none;
# the file is the graph's name "frametime (ms)" with the space made '_', hud_context.c)
hud_file=
if [ "$HUD" = 1 ] && [ -z "${GALLIUM_HUD:-}" ]; then
	mkdir -p "$D/hud"
	ENV+=(GALLIUM_HUD=fps,frametime GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_PERIOD=1 GALLIUM_HUD_DUMP_DIR=$D/hud)
	hud_file="$D/hud/frametime_(ms)"
elif [ -n "${GALLIUM_HUD_DUMP_DIR:-}" ]; then	# ps3-bench.sh's BENCH_HUD=1: per-second fps only
	hud_file=$GALLIUM_HUD_DUMP_DIR/fps
fi
# MEM=1: the malloc statistics library in the game (it acts only in the process named COMM), asked
# through a fifo
preload_add() {	# preload_add LIB: LIB first in the game's LD_PRELOAD (the recipe's, or the caller's)
	local i
	for i in "${!ENV[@]}"; do
		[[ ${ENV[$i]} == LD_PRELOAD=* ]] && { ENV[$i]="LD_PRELOAD=$1:${ENV[$i]#LD_PRELOAD=}"; return; }
	done
	ENV+=(LD_PRELOAD="$1${LD_PRELOAD:+:$LD_PRELOAD}")
}
MEM=${BENCH_MEM:-0}; MALLOC_FD=
MALLOCSTAT=${BENCH_MALLOCSTAT-/usr/lib/ps3/lib/ps3-mallocstat.so}
MEM_SUDO=0
if [ "$MEM" = 1 ]; then
	mkdir -p "$D/mem"
	sudo -n true 2>/dev/null && MEM_SUDO=1	# ps3rsx's census is in debugfs (root only)
	if [ -n "$MALLOCSTAT" ] && [ -e "$MALLOCSTAT" ] && [ -n "$COMM" ]; then
		rm -f "$D/.malloc.fifo"; mkfifo "$D/.malloc.fifo"
		preload_add "$MALLOCSTAT"
		ENV+=(PS3_MALLOCSTAT_FIFO="$D/.malloc.fifo" PS3_MALLOCSTAT_DIR="$D/mem" PS3_MALLOCSTAT_COMM="$COMM")
	fi
fi
# glibc's malloc tuned for this run (docs/MEMORY.md): BENCH_MALLOC (run.sh MALLOC=, or an ENVS token
# MALLOC=...) is a comma list of presets, added after the recipe and its variant:
#   arena1, arena2  MALLOC_ARENA_MAX=1 or 2 (glibc's default for the PPE's two threads: 16 arenas)
#   mmap            fixed thresholds: blocks of 256 KiB and more mmapped and unmapped when freed
#                   (MALLOC_MMAP_THRESHOLD_=262144), the main heap's top given back once 1 MiB is
#                   free (MALLOC_TRIM_THRESHOLD_=1048576); setting them stops glibc raising the mmap
#                   threshold to 32 MiB after the first big free
#   trim            ps3-malloctrim.so (BENCH_MALLOCTRIM is its path) preloaded: malloc_trim(0) every
#                   BENCH_MALLOC_TRIM_S seconds (default 10), a "ps3-malloctrim:" line per trim
#   tune            arena2,mmap,trim
# bench.txt gets malloc=<the list>; report.py shows it after the variant (base@marena2_trim).
MALLOC=${BENCH_MALLOC:-}
MALLOCTRIM=${BENCH_MALLOCTRIM:-/usr/lib/ps3/lib/ps3-malloctrim.so}
if [ -n "$MALLOC" ]; then
	for m in ${MALLOC//,/ }; do
		[ "$m" = tune ] && m="arena2 mmap trim"
		for m in $m; do
			case $m in
			arena1) ENV+=(MALLOC_ARENA_MAX=1) ;;
			arena2) ENV+=(MALLOC_ARENA_MAX=2) ;;
			mmap) ENV+=(MALLOC_MMAP_THRESHOLD_=262144 MALLOC_TRIM_THRESHOLD_=1048576) ;;
			trim)
				[ -e "$MALLOCTRIM" ] || { echo "!! BENCH_MALLOC=trim: no $MALLOCTRIM (ps3-gpu-tools with docs/MEMORY.md's preload)"; exit 2; }
				preload_add "$MALLOCTRIM"
				ENV+=(PS3_MALLOC_TRIM="${BENCH_MALLOC_TRIM_S:-10}" PS3_MALLOC_TRIM_COMM="$COMM" PS3_MALLOC_TRIM_LOG=1) ;;
			*) echo "!! BENCH_MALLOC: no preset $m (arena1 arena2 mmap trim tune)"; exit 2 ;;
			esac
		done
	done
fi
# Mesa driver switches for this run (docs/HOTPATHS.md): every BENCH_NV30_* in the environment
# (run.sh's ENVS token NV30_X=1 becomes BENCH_NV30_X=1) goes to the game without the BENCH_ prefix,
# after the recipe and its variant. bench.txt gets genv=<NAME=VALUE ...>; report.py shows it after the
# variant (base@translate_lut1).
GENV=()
for v in $(compgen -v BENCH_NV30_); do GENV+=("${v#BENCH_}=${!v}"); done
[ ${#GENV[@]} -gt 0 ] && ENV+=("${GENV[@]}")
launch=(); [ "$GAMEMODE" = 1 ] && launch=(ps3-game)
# GAMEMODE=0 means no game mode: not even ps3-game-detect's (it reads the game's environment)
[ "$GAMEMODE" = 1 ] || ENV+=(PS3_GAME_MODE=off)

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
# perf's control fifos and the malloc fifo, opened read-write (never blocks) after the game started
# (it inherits none of them)
PERF_FD=(); PERF_ACK=()
for c in ${BENCH_PERF_CTL:-}; do
	exec {f}<> "${c%%:*}"; PERF_FD+=("$f"); exec {f}<> "${c#*:}"; PERF_ACK+=("$f")
done
[ -e "$D/.malloc.fifo" ] && exec {MALLOC_FD}<> "$D/.malloc.fifo"
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

# profiling: PERF_AT/PERF_END when perf goes on/off, MEM_AT/MEM_LB the snapshots (microseconds from
# the start); prof_due runs what is due and sets PROF_NEXT to the next one
PERF_AT=; PERF_END=; PERF_ON=0; PERF_WIN=(); PERF_TICKS=0; PERF_T0=; PERF_ACKS=ok; MEM_AT=(); MEM_LB=()
MEM_JOBS=(); MEM_DONE=(); PROF_NEXT=
perf_ticks() {	# perf_ticks VAR: user+sys clock ticks of the perf processes so far (locals _*: not VAR)
	local _p _f _t=0 _pids=
	read -r _pids < "$D/.perf.pid" 2>/dev/null
	for _p in $_pids; do
		read -r -a _f < "/proc/$_p/stat" 2>/dev/null && _t=$(( _t + _f[13] + _f[14] ))
	done
	printf -v "$1" '%d' "$_t"
}
perf_ctl() {	# perf_ctl enable|disable: every perf, waiting 1 s at most for each one's ack
	local i a t
	[ ${#PERF_FD[@]} -gt 0 ] || return 0
	[ "$1" = enable ] && [ "$PERF_ON" = 1 ] && return 0
	[ "$1" = disable ] && [ "$PERF_ON" = 0 ] && return 0
	for i in "${!PERF_FD[@]}"; do
		printf '%s\n' "$1" >&"${PERF_FD[$i]}"
		read -r -t 1 -u "${PERF_ACK[$i]}" a && [ "$a" = ack ] || PERF_ACKS=missing
	done
	us; secs t $(( US - u0 )); PERF_WIN+=("$t")
	if [ "$1" = enable ]; then PERF_ON=1; perf_ticks PERF_T0
	else PERF_ON=0; perf_ticks a; PERF_TICKS=$(( PERF_TICKS + a - PERF_T0 )); fi
}
mem_snap() {	# mem_snap LABEL: the game's memory now into $D/mem/LABEL.*
	local p a b t m
	read -r p < "$D/.pid" 2>/dev/null && [ -e "/proc/$p/smaps" ] || return 0
	[ -n "$MALLOC_FD" ] && printf '%s\n' "$1" >&"$MALLOC_FD"	# the game writes LABEL.malloc
	us; a=$US
	cat "/proc/$p/smaps" > "$D/mem/$1.smaps" 2>/dev/null
	us; b=$US
	cat "/proc/$p/smaps_rollup" > "$D/mem/$1.rollup" 2>/dev/null
	cat "/proc/$p/status" > "$D/mem/$1.status" 2>/dev/null
	# every thread's name and stack pointer (in a system call: memprof.py finds the thread stacks)
	local tk c y
	for tk in /proc/$p/task/*; do
		read -r c < "$tk/comm"; read -r y < "$tk/syscall"; printf '%s\t%s\t%s\n' "${tk##*/}" "$c" "$y"
	done > "$D/mem/$1.tasks" 2>/dev/null
	cat /proc/meminfo > "$D/mem/$1.meminfo"
	# ps3rsx's buffers per client (takes mm_lock and walks them: once a snapshot)
	[ "$MEM_SUDO" = 1 ] && sudo -n sh -c 'cat /sys/kernel/debug/dri/*/ps3rsx_mm' > "$D/mem/$1.gpu" 2>/dev/null
	secs t $(( a - u0 )); m=$(( (b - a) / 1000 ))
	printf 'label=%s pid=%s t=%s smaps_ms=%s exe=%s comm=%s\n' "$1" "$p" "$t" "$m" \
		"$(readlink "/proc/$p/exe" 2>/dev/null)" "$COMM" > "$D/mem/$1.info"
}
prof_plan() {	# prof_plan START_US: the window starts at START_US: perf on then, snapshots in it
	local s=$1
	if [ ${#PERF_FD[@]} -gt 0 ] && [ -z "$WIN_ON_RE" ]; then
		PERF_AT=$s
		[ "$PASSES" = 1 ] || PERF_END=$(( s + MEASURE * 1000000 ))
	fi
	if [ "$MEM" = 1 ]; then
		MEM_AT=($(( s + 1000000 )) $(( s + MEASURE * 500000 ))); MEM_LB=(start mid)
		# the end 2 s before the window's (the snapshot is done before WINDOW_STOP stops the
		# game); PASSES=1: at the last counted pass, when the client stops the game
		[ "$PASSES" = 1 ] || { MEM_AT+=($(( s + MEASURE * 1000000 - 2000000 ))); MEM_LB+=(end); }
	fi
}
prof_due() {	# runs the profiling events due now (US); PROF_NEXT: the next one's time, or empty
	local now=$(( US - u0 ))
	PROF_NEXT=
	if [ -n "$PERF_AT" ]; then
		if [ $now -ge "$PERF_AT" ]; then perf_ctl enable; PERF_AT=; else PROF_NEXT=$PERF_AT; fi
	fi
	if [ -z "$PERF_AT" ] && [ -n "$PERF_END" ]; then
		if [ $now -ge "$PERF_END" ]; then perf_ctl disable; PERF_END=
		elif [ -z "$PROF_NEXT" ] || [ "$PERF_END" -lt "$PROF_NEXT" ]; then PROF_NEXT=$PERF_END; fi
	fi
	while [ ${#MEM_AT[@]} -gt 0 ]; do
		if [ $now -ge "${MEM_AT[0]}" ]; then
			mem_snap "${MEM_LB[0]}" > /dev/null 2>&1 & MEM_JOBS+=($!); MEM_DONE+=("${MEM_LB[0]}")
			MEM_AT=("${MEM_AT[@]:1}"); MEM_LB=("${MEM_LB[@]:1}")
		else
			[ -z "$PROF_NEXT" ] || [ "${MEM_AT[0]}" -lt "$PROF_NEXT" ] && PROF_NEXT=${MEM_AT[0]}
			break
		fi
	done
}
# the steady-state window, in microseconds from the start: load_us when the load ended (LOAD_MARK,
# the first LOAD_RE line, else the HUD's first frame), hud_us when the HUD's dump got its first
# value, stop_us when WINDOW_STOP ends the run; passes (PASSES=1) in PASS, measured their counted time
load_us=; hud_us=; stop_us=; np=0; plast=; measured=0; win0=; win1=; PASS=(); son=; SEGS=()
loaded() {	# the load ended now (US)
	load_us=$(( US - u0 ))
	[ "$WINDOW_STOP" = 1 ] && stop_us=$(( load_us + (WARMUP + MEASURE + 2) * 1000000 ))
	if [ -z "$WIN_ON_RE" ]; then	# profiling from the window's start (PASSES: 0.9 x WARMUP)
		if [ "$PASSES" = 1 ]; then prof_plan $(( load_us + WARMUP * 900000 ))
		else prof_plan $(( load_us + WARMUP * 1000000 )); fi
	fi
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
	prof_due
	lim=; why=; wt=
	[ "$DURATION" -gt 0 ] && { lim=$(( DURATION * 1000000 )); why=duration; }
	[ -n "$stop_us" ] && { [ -z "$lim" ] || [ "$stop_us" -lt "$lim" ]; } && { lim=$stop_us; why=window; }
	if [ -n "$lim" ]; then
		wt=$(( lim - US + u0 ))
		[ $wt -le 0 ] && { ended=$why; break; }
	fi
	# until the HUD's first value: a look at its dump file every 0.25 s
	[ -n "$hud_file" ] && [ -z "$hud_us" ] && { [ -z "$wt" ] || [ $wt -gt 250000 ]; } && wt=250000
	# the next profiling event (perf on/off, a memory snapshot)
	if [ -n "$PROF_NEXT" ]; then
		pw=$(( PROF_NEXT - US + u0 )); [ $pw -lt 1000 ] && pw=1000
		[ -z "$wt" ] || [ $wt -gt $pw ] && wt=$pw
	fi
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
	if [ -n "$WIN_ON_RE" ]; then	# the game's own window pieces
		if [ -z "$son" ] && [[ $l =~ $WIN_ON_RE ]]; then son=$(( US - u0 ))
			perf_ctl enable; [ ${#SEGS[@]} -eq 0 ] && prof_plan "$son"
		elif [ -n "$son" ] && [ -n "$WIN_OFF_RE" ] && [[ $l =~ $WIN_OFF_RE ]]; then
			secs a "$son"; secs b $(( US - u0 )); SEGS+=("$a $b"); son=
			perf_ctl disable
		fi
	fi
	if [ "$PASSES" = 1 ] && [ -n "$FPS_RE" ] && [[ $l =~ $FPS_RE ]] && pass "$l"; then ended=window; break; fi
	if [ -n "$END_RE" ] && [[ $l =~ $END_RE ]]; then ended=end; break; fi
done
us; run_us=$(( US - u0 )); secs t_run "$run_us"
# the window is over (or the game is): perf off, the last snapshot while the game still runs
perf_ctl disable
[ ${#MEM_JOBS[@]} -gt 0 ] && wait "${MEM_JOBS[@]}"
if [ "$MEM" = 1 ] && [ ${#MEM_DONE[@]} -gt 0 ] && [[ " ${MEM_DONE[*]} " != *" end "* ]]; then
	mem_snap end > /dev/null 2>&1; MEM_DONE+=(end)
fi
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
[ -n "$son" ] && { secs a "$son"; secs b "$run_us"; SEGS+=("$a $b"); }	# a piece still open at the end
if [ -n "$WIN_ON_RE" ]; then window=${SEGS[*]}
elif [ -n "$win0" ]; then secs w0 "$win0"; secs w1 "$win1"; window="$w0 $w1"
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
  echo "t0=$t0"	# the start as $EPOCHREALTIME: the other times are seconds after it (stalls.py)
  echo "window=$window"
  [ "$PASSES" = 1 ] && { echo "passes=$np"; echo "measured=$measured"; printf '%s\n' "${PASS[@]}"; }
  [ -n "$STEADY_RE" ] && { echo "steady_fps=$(first "$STEADY_RE")"; echo "steady_maxms=$(first "$STEADY_MAXMS_RE")"; }
  [ -n "$MALLOC" ] && echo "malloc=$MALLOC"
  [ ${#GENV[@]} -gt 0 ] && echo "genv=${GENV[*]}"
  echo "hud_skip=$HUD_SKIP"; echo "hwm=$(cat "$D/.hwm" 2>/dev/null)"; echo "sound=$(cat "$D/.sound" 2>/dev/null)"
  if [ ${#PERF_FD[@]} -gt 0 ]; then
	echo "perf_win=${PERF_WIN[*]}"; echo "perf_ack=$PERF_ACKS"
	awk -v t="$PERF_TICKS" 'BEGIN { printf "perf_cpu=%.2f\n", t / 100 }'	# USER_HZ
  fi
  if [ "$MEM" = 1 ]; then
	echo "mem_snaps=$(for f in "$D"/mem/*.info; do [ -e "$f" ] && sed -E 's/.*label=([^ ]*) .* t=([^ ]*) smaps_ms=([^ ]*) .*/\1:\2:\3/' "$f"; done | tr '\n' ' ')"
  fi
  # ALSA's "underrun occurred": the sound buffer ran dry (what a listener hears as chopping)
  echo "underruns=$(grep -ac 'underrun occurred' "$D/game.log" 2>/dev/null)"
  parse; } > "$D/bench.txt"
rm -f "$D/.hwm" "$D/.pid" "$D/.sound" "$D/.malloc.fifo"
sed 's/^/bench: /' "$D/bench.txt"
exit 0
}
