#!/bin/bash
# client.sh <game> [variant]: the X client run.sh hands to `ps3-bench.sh x`: one game run from its
# recipe (recipes/<game>.sh, docs/games/SUITE.md). Sets up the game's files, starts it (through
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
# perf_win (on off pairs), perf_cpu (perf's own CPU seconds in them), perf_ack; /proc/interrupts and
# kernel local/0046's htab hcall stats at perf's on and off go to $D/irq/start.* and end.* (with
# WIN_ON_RE: the last pair), for irqoff.py's rates over the window. BENCH_MEM=1 (run.sh
# MEM=1): the game's smaps, smaps_rollup and status, ps3rsx's buffer census (debugfs ps3rsx_mm, once
# a snapshot) and glibc's malloc statistics (ps3-mallocstat.so preloaded: BENCH_MALLOCSTAT is its
# path, "" turns it off) at the window's start, middle and end into $D/mem/ (memprof.py); bench.txt
# gets mem_snaps (label:seconds:ms reading smaps). BENCH_MALLOC: glibc malloc presets (below,
# docs/MEMORY.md).
# Failure dump (docs/games/LOG-FINDINGS.md "Catching failures"). BENCH_DUMPON=1 (run.sh DUMPON=1; off by
# default, never in measured runs): the first game output line matching BENCH_DUMPON_RE (default:
# Wait timeout, mmap commit error, out of GART, out of VRAM, no memory to back the GART; an ENVS token
# can't hold spaces or +) stops the game (SIGSTOP), captures it into $D/dump/ (the trigger line, /proc
# of the game and its threads with kernel stacks, ALSA, ps3rsx debugfs and parameters, meminfo, vmstat,
# dmesg, a gcore compressed with zstd or xz; needs gdb on the PS3), and lets it go on. At most
# BENCH_DUMPON_MAX captures (default 1). The game also runs with `ulimit -c unlimited`; after a death by
# signal the core is collected as dump/core.crash.zst|xz (systemd-coredump, else the core file). bench.txt
# gets dump=trigger|crash|trigger+crash|none, dump_at=<s>, dump_core=<raw>:<compressed bytes>,
# oal_wedge= (always: OpenAL's "Wait timeout" and "mmap commit error" lines). dumpana.sh analyses it.
# Always too: oal_wait= and oal_xrun= (oal_wedge's two kinds apart), snd_stall=<s>, snd_stall_at=<s> (the sound card's hw pointer stood still while RUNNING:
# its DMA stopped, the cause of OpenAL's wedge) and, on a kernel with local/0047, snd_restarts= (its
# DMA watchdog's restarts in the run), snd_wd= (dma_watchdog_ms) and snd_stats= (snd_ps3's counters);
# and the sound dropouts ps3-xrunlog measured: xrunlog=, xruns_logged=, xruns_audible= (the Sound
# dropouts block below).
# Start-up profiling (docs/PROFILING.md "Start-up profiling"): BENCH_PERF_LOAD=1 (run.sh
# PERF_LOAD=1) covers the span the window leaves out: the game's exec, its loading screens and the
# warm-up. With ps3-bench.sh's BENCH_PERF_LOAD_CTL the game is started through a gate (a bash that
# waits on $D/.gate and then execs it): its pid goes to $D/.launch.pid for ps3-bench.sh's perf, the
# client writes enable, waits for the ack (perf has attached), and only then opens the gate and starts
# the clock (t0), so perf's start costs the load nothing; disable at the window's start (where the
# window perf of PERF=1 goes on). bench.txt gets perf_load_win, perf_load_ack, perf_load_cpu,
# mono_t0 (/proc/uptime at t0: perf's clock, -k mono, for the load/warm-up split), game_pid.
# Also, with or without perf: the disks' and the VM's counters at the gate, at the load's end and at
# the window's start, the game's own (stat, io, status, its main thread's stat) at the last two, into
# $D/load-io/; ld.so's statistics (LD_DEBUG=statistics, LD_DEBUG_OUTPUT=$D/ld/stats: one file per
# pid; BENCH_LDSTATS=0 turns it off). loadprof.py reads them.
# BENCH_SHADERCACHE (run.sh SHADERCACHE=, an ENVS token): Mesa's disk shader cache for this run, the
# user's own (~/.cache/mesa_shader_cache) left alone: cold (MESA_SHADER_CACHE_DIR an empty directory,
# removed after the run: a first start), warm (a directory per game kept across runs and boots in
# BENCH_SHADERCACHE_ROOT, default /var/tmp/ps3-bench/shadercache; its first run fills it and is tagged
# prime), off (MESA_SHADER_CACHE_DISABLE=true: compiles, no cache reads or writes). bench.txt gets
# shadercache=<mode> and shadercache_kb=<KiB before> <KiB after>; report.py shows it after the variant
# (base@sccold).
# The SSD arms (run.sh GAMEDATA=ssd, SWAP=ssd or ENVS tokens; ssd-arm.sh puts them in place before the
# run, docs/GAMEOS-HDD.md "Test drive, Phase A"), checked here before anything starts: BENCH_GAMEDATA=ssd
# needs every one of the recipe's DATA_DIRS to be a bind of the SSD copy (findmnt's FSROOT is
# BENCH_SSD_FSROOT, default /games, plus the dir), BENCH_SWAP must match the VRAM swap's spill as
# ssd-arm.sh left it ($BENCH_SSDARM_STATE/ps3-ssdarm.swap, usb when absent), and a run without
# GAMEDATA=ssd refuses binds left over. A mismatch stops the run (exit 2). bench.txt gets gamedata=,
# gamedata_dirs=, swap= and swaps= (/proc/swaps: name:priority); report.py shows base@gdssd, base@swssd.
# Screenshots (docs/games/SUITE.md "Screenshots"): BENCH_SHOTS (run.sh SHOTS=, on by default for smoke
# runs: auto) is a list of shot points in whole seconds (spaces or commas), auto (the recipe's SHOT_POINTS, default "30
# 90") or off. The points count from a game state, not from the start (the PS3 loads about 10x slower
# than the PC): SHOT_FROM=load (default) the load's end as the window has it (LOAD_MARK, LOAD_RE, else
# the HUD's first frame), start the client's start, mark the time a recipe's during() calls shot_mark
# (after its keys; if during() ends without it, the load's end), SHOT_RE=regex the first game output
# line matching it. Each capture is `xwd -root` at nice 19 and idle I/O class, a background job, into
# BENCH_SHOT_DIR (default /var/tmp/ps3-bench/.shots-<run> on the disk while the run directory is in
# /dev/shm: 8 MB a shot; run.sh moves it to <run>/shots, else <run dir>/shots). bench.txt gets
# shots=<points>, shot_ref=<from>:<s after t0>, per shot shot=<p>s:shots/<file>:<ms of the capture>
# and shot_t=<p>s:<s after t0>:<HUD frame times before>:<after> (the frametime file's line counts
# around the capture: the frames it touched, for shots.py cost), shot_skipped= (points the run never
# reached), shot_static=1 (the recipe's SHOT_STATIC: a menu or title screen, a close comparison is
# fair). Each shot also adds a row to BENCH_SHOT_DIR/geometry.tsv: the game's window and the screen
# (geom.py, x11-utils; for the Display score). shots.py turns them into PNGs and the side-by-side sheet against the PC reference
# (tools/bench-ref.sh). BENCH_REF=1: a PC reference run (tools/bench-ref.sh): no game mode.
# A played session (BENCH_SESSION=1, started by session.sh through `ps3-bench.sh session`, docs/games/
# SUITE.md "Played sessions"): the game as the desktop menu starts it, in the live desktop. Through
# ps3-game (BENCH_PS3GAME another command), vsync as shipped (the recipe's vblank_mode=0 dropped), no
# window manager of ours, no time limit, no END_RE, no passes or windows; the recipe's during() and
# parse() are dropped and its optional session() function changes CMD/ENV to a playable command (a
# smoke recipe's autoplay arguments out). The player quits the game; bench.txt gets kind=session,
# ended= quit (exit code 0), error (another code), crash (signal or a segfault in dmesg.log), oom,
# closed (SIGTERM/SIGKILL: Ctrl+Alt+Esc), killed (the harness stopped it), ended_why=, play_from=
# mark|load|start, play_start=, play_seconds=, window= (the play, for the HUD metrics), vsync=shipped,
# gamemode=1. BENCH_CMD_FILE (session.sh, a menu launcher): the file holds the game's command, NUL-separated
# (it replaces the recipe's CMD, without ps3-game: the session adds it); BENCH_LAUNCHER and
# BENCH_LAUNCHER_FILE (the entry's Name and file name) go to bench.txt as launcher= and launcher_file=.
# PLAY_MARK (a recipe's regex on the game's output) is the first gameplay frame; else
# the load mark (LOAD_MARK, LOAD_RE); else play_from=start and play_seconds is the whole run.
{
set -u
here=$(cd "$(dirname "$0")" && pwd)
GAME=${1:?game}; VARIANT=${2:-base}
D=${BENCH_DIR:-/dev/shm/bench-client-$$}; mkdir -p "$D"
ASSETS=$here/assets

# recipe interface (README in recipes/): the recipe sets these, a variant_<name> function changes them
CMD=(); ENV=(); BACKUP=(); DATA_DIRS=(); GAMEMODE=0; HUD=1; COMM=; WM=openbox; PLAY_MARK=
# DURATION: a safety cap (load + 60 s warm-up + 120 s measured + margin); recipes set their own
END_RE=; DURATION=270; HUD_SKIP=12; WARMUP=60; MEASURE=120; PASSES=0; WINDOW_STOP=0; STEADY_RE=
STEADY_MAXMS_RE=; WIN_ON_RE=; WIN_OFF_RE=; FPS_RE=; MAXMS_RE=; LOAD_RE=; LOAD_MARK=; RESULT_FILE=; SOUND_AT=15
SHOT_POINTS="30 90"; SHOT_FROM=load; SHOT_RE=; SHOT_STATIC=0
setup() { :; }; parse() { :; }
. "${BENCH_RECIPES:-$here/recipes}/$GAME.sh" || exit 2
if declare -F "variant_$VARIANT" >/dev/null; then "variant_$VARIANT"
elif [ "$VARIANT" != base ]; then echo "!! $GAME: no variant $VARIANT"; exit 2; fi
[ ${#CMD[@]} -gt 0 ] || { echo "!! $GAME: no CMD"; exit 2; }
[ "${BENCH_REF:-0}" = 1 ] && GAMEMODE=0	# the PC reference (tools/bench-ref.sh): no ps3-game there
# A played session (the top): what a desktop launch does, nothing of the bench's scripting
SESSION=${BENCH_SESSION:-0}; UP0=
if [ "$SESSION" = 1 ]; then
	unset -f during; parse() { :; }
	if declare -F session > /dev/null; then session; fi	# the recipe's playable command
	# the game as shipped: session.sh renamed its directories aside (fresh defaults) and puts the player's
	# back afterwards (freshhome.py), so no BACKUP here; no recipe setup (it prepares benchmark files)
	BACKUP=(); setup() { :; }
	# the command of a menu launcher (session.sh): NUL-separated argv, ps3-game left out (launch= adds it)
	if [ -n "${BENCH_CMD_FILE:-}" ]; then
		mapfile -d '' -t CMD < "$BENCH_CMD_FILE"
		[ ${#CMD[@]} -gt 0 ] || { echo "!! BENCH_CMD_FILE $BENCH_CMD_FILE: no command"; exit 2; }
	fi
	_new=(); for _x in "${ENV[@]}"; do [ "$_x" = vblank_mode=0 ] || _new+=("$_x"); done
	ENV=("${_new[@]}")	# vsync as shipped
	GAMEMODE=1; WM=; DURATION=0; END_RE=; PASSES=0; WINDOW_STOP=0; WIN_ON_RE=; WIN_OFF_RE=
	STEADY_RE=; STEADY_MAXMS_RE=; SHOT_FROM=start; SHOT_RE=
	read -r UP0 _ < /proc/uptime	# dmesg.log's clock (a crash line since the start)
fi
# Screenshots (the top): the shot points, the reference they count from, the directory on disk
SHOTS=${BENCH_SHOTS:-}; SHOT_PTS=(); SHOT_AT=(); SHOT_Q=(); SHOT_JOBS=(); SHOT_REF_US=; SHOT_DIR=; SHOT_ERR=
SHOTS=${SHOTS//,/ }	# commas for an ENVS token (SHOTS=70,130)
case $SHOTS in off|0|none) SHOTS= ;; auto) SHOTS=$SHOT_POINTS ;; esac
for s in $SHOTS; do [[ $s =~ ^[0-9]+$ ]] || { echo "!! BENCH_SHOTS=$BENCH_SHOTS: whole seconds, auto or off"; exit 2; }; done
[ -n "$SHOT_RE" ] && SHOT_FROM=re
case $SHOT_FROM in load|start|mark|re) ;; *) echo "!! $GAME: SHOT_FROM=$SHOT_FROM: load, start, mark (or SHOT_RE)"; exit 2 ;; esac
SHOT_SRC=$SHOT_FROM
if [ -n "$SHOTS" ] && ! command -v xwd > /dev/null; then
	echo "!! BENCH_SHOTS: no xwd (x11-apps): no screenshots"; SHOT_ERR="no xwd"; SHOTS=
fi
# a played session also takes the player's marked shots (Alt+F12, pad Select+Start: inputlog.py), with
# or without timed ones
if [ -n "$SHOTS" ] || { [ "$SESSION" = 1 ] && command -v xwd > /dev/null; }; then
	if [ -n "${BENCH_SHOT_DIR:-}" ]; then SHOT_DIR=$BENCH_SHOT_DIR
	elif [[ $D == /dev/shm/* ]]; then SHOT_DIR=${BENCH_SHOT_ROOT:-/var/tmp/ps3-bench}/.shots-${D##*/}	# not RAM
	else SHOT_DIR=$D/shots; fi
	mkdir -p "$SHOT_DIR"
	read -r -a SHOT_PTS <<< "$(printf '%s\n' $SHOTS | sort -n -u | tr '\n' ' ')"
	SHOT_NICE=(nice -n 19); command -v ionice > /dev/null && SHOT_NICE+=(ionice -c 3)
fi
rm -f "$D/.shot-mark"
shot_mark() { printf '%s\n' "$EPOCHREALTIME" > "$D/.shot-mark"; }	# for during(): SHOT_FROM=mark counts from here
shot_plan() {	# shot_plan REF_US: the reference happened REF_US after the start: the shot times after it
	[ ${#SHOT_PTS[@]} -gt 0 ] && [ -z "$SHOT_REF_US" ] || return 0
	local s
	SHOT_REF_US=$1; SHOT_AT=(); SHOT_Q=("${SHOT_PTS[@]}")
	for s in "${SHOT_PTS[@]}"; do SHOT_AT+=($(( $1 + s * 1000000 ))); done
}
shot_take() {	# shot_take POINT [PREFIX]: one capture now (a background job): $SHOT_DIR/shot-<POINT>s.xwd, .shot-<POINT>.info
	# (PREFIX mark: the player's, mark-<POINT>s.xwd, .mark-<POINT>.info)
	local pre=${2:-shot} p=$1 f=${2:-shot}-$1s.xwd a b n0=- n1=- rc t
	[ -n "$hud_file" ] && [ -e "$hud_file" ] && n0=$(wc -l < "$hud_file")
	us; a=$US
	"${SHOT_NICE[@]}" xwd -root -silent > "$SHOT_DIR/$f" 2> "$SHOT_DIR/.$pre-$p.err"; rc=$?
	us; b=$US
	[ -n "$hud_file" ] && [ -e "$hud_file" ] && n1=$(wc -l < "$hud_file")
	secs t $(( a - u0 ))
	# where the game's window is (after the capture, so its time isn't in the capture's): one row of
	# $SHOT_DIR/geometry.tsv (geom.py; docs/GAMEDB.md "Display"); no pid or no xwininfo: no row
	if [ "$rc" = 0 ] && command -v xwininfo > /dev/null && command -v python3 > /dev/null && read -r gp < "$D/.pid" 2>/dev/null; then
		"${SHOT_NICE[@]}" python3 -I "$here/geom.py" "$gp" "$t" "$SHOT_DIR/geometry.tsv" > /dev/null 2>&1
	fi
	printf '%s %s %s %d %s %s %s\n' "$p" "$f" "$t" $(( (b - a) / 1000 )) "$n0" "$n1" "$rc" > "$SHOT_DIR/.$pre-$p.info"
}
# the player's marked shots (inputlog.py appends a time to $D/.mark-req at Alt+F12 or Select+Start; this
# loop, a helper of its own, takes the capture: the same xwd + geom.py as the timed ones, named by the
# seconds after the start, SHOT_MARK_MAX of them at most, 2 s apart). A short sound as the answer when a
# sample is at hand (SHOT_MARK_SOUND, played at nice 19 in the background), no overlay on the game.
SHOT_MARK_MAX=${BENCH_MARK_MAX:-30}
mark_watch() {
	local req=$D/.mark-req n=0 last=0 t_us ms tk line t
	mkfifo "$D/.mtick"; exec {tk}<> "$D/.mtick"; rm -f "$D/.mtick"
	while :; do
		if [ -s "$req" ] && [ $n -lt "$SHOT_MARK_MAX" ]; then
			line=; read -r line < "$req"; : > "$req"
			[ -n "$line" ] || { read -r -t 0.5 -u "$tk"; continue; }
			us; t_us=$US
			if [ $(( t_us - last )) -ge 2000000 ]; then
				last=$t_us; n=$((n + 1))
				secs t $(( t_us - u0 ))
				[ -n "${SHOT_MARK_SOUND:-}" ] && [ -r "$SHOT_MARK_SOUND" ] && command -v aplay > /dev/null &&
					{ nice -n 19 aplay -q "$SHOT_MARK_SOUND" > /dev/null 2>&1 & }
				: > "$D/.markbusy"
				shot_take "$t" mark > /dev/null 2>&1
				rm -f "$D/.markbusy"
			fi
		fi
		read -r -t 0.5 -u "$tk"
	done
}
mark_lines() {	# the marked shots' bench.txt lines: shot_mark=<s after t0>s:shots/<file>:<capture ms> ("-" when it failed)
	local i p f t ms n0 n1 rc
	[ -n "$SHOT_DIR" ] || return 0
	for i in "$SHOT_DIR"/.mark-*.info; do
		[ -e "$i" ] || continue
		if read -r p f t ms n0 n1 rc < "$i"; then
			if [ "$rc" = 0 ] && [ -s "$SHOT_DIR/$f" ]; then echo "shot_mark=${p}s:shots/$f:$ms"
			else echo "shot_mark=${p}s:-:$ms"; rm -f "$SHOT_DIR/$f"; fi
		fi
		rm -f "$i" "${i%.info}.err"
	done
	return 0
}
shot_lines() {	# the shots' bench.txt lines (also from the trap of a run stopped from outside)
	local p f t ms n0 n1 rc skipped= r=-
	[ -n "$SHOT_ERR" ] && echo "shot_err=$SHOT_ERR"
	mark_lines
	[ ${#SHOT_PTS[@]} -gt 0 ] || return 0
	echo "shots=${SHOT_PTS[*]}"
	[ -n "$SHOT_REF_US" ] && secs r "$SHOT_REF_US"
	echo "shot_ref=$SHOT_SRC:$r"
	[ "$SHOT_STATIC" = 1 ] && echo "shot_static=1"
	for p in "${SHOT_PTS[@]}"; do
		if read -r _ f t ms n0 n1 rc < "$SHOT_DIR/.shot-$p.info" 2>/dev/null; then
			if [ "$rc" = 0 ] && [ -s "$SHOT_DIR/$f" ]; then echo "shot=${p}s:shots/$f:$ms"
			else
				echo "shot=${p}s:-:$ms"
				echo "shot_err=${p}s: xwd rc $rc: $(head -c 200 "$SHOT_DIR/.shot-$p.err" 2>/dev/null | tr '\n' ' ')"
				rm -f "$SHOT_DIR/$f"
			fi
			echo "shot_t=${p}s:$t:$n0:$n1"
			rm -f "$SHOT_DIR/.shot-$p.info"; [ -s "$SHOT_DIR/.shot-$p.err" ] || rm -f "$SHOT_DIR/.shot-$p.err"
		else skipped+=" ${p}s"; fi
	done
	[ -n "$skipped" ] && echo "shot_skipped=${skipped# }"
	return 0
}
# OpenAL Soft's settings for this run on top of the variant (run.sh's ENVS token OAL=b16 or OAL=b16,lin
# becomes BENCH_OAL): recipes/openal.inc's names, written to the run's alsoft.conf (ALSOFT_CONF) like its
# variants; sourced here for recipes that don't (SuperTuxKart). bench.txt gets oal=; report.py base@oalb16.
OAL_TOKEN=${BENCH_OAL:-}
if [ -n "$OAL_TOKEN" ]; then
	declare -F oal_variant > /dev/null || . "$here/recipes/openal.inc"
	for v in ${OAL_TOKEN//,/ }; do oal_variant "$v" || { echo "!! BENCH_OAL=$OAL_TOKEN: no $v in openal.inc"; exit 2; }; done
fi

# the SSD arms (the top): is the system in the arm this run asks for?
GD=${BENCH_GAMEDATA:-}; SW=${BENCH_SWAP:-}; GD_DIRS=; ARMST=${BENCH_SSDARM_STATE:-/dev/shm}
case $GD in ''|usb|ssd) ;; *) echo "!! BENCH_GAMEDATA=$GD: usb or ssd"; exit 2 ;; esac
case $SW in ''|usb|ssd) ;; *) echo "!! BENCH_SWAP=$SW: usb or ssd"; exit 2 ;; esac
if [ "$GD" = ssd ]; then
	[ ${#DATA_DIRS[@]} -gt 0 ] || { echo "!! BENCH_GAMEDATA=ssd: $GAME's recipe has no DATA_DIRS"; exit 2; }
	for d in "${DATA_DIRS[@]}"; do
		r=$(${BENCH_FINDMNT:-findmnt} -n -o FSROOT --mountpoint "$d" 2>/dev/null)
		[ "$r" = "${BENCH_SSD_FSROOT:-/games}$d" ] ||
			{ echo "!! BENCH_GAMEDATA=ssd: $d is not the SSD copy (${r:-not a mount point}): ssd-arm.sh set"; exit 2; }
		GD_DIRS+="${GD_DIRS:+ }$d"
	done
elif [ -s "$ARMST/ps3-ssdarm.binds" ]; then
	echo "!! SSD binds left from a GAMEDATA=ssd run ($(tr '\n' ' ' < "$ARMST/ps3-ssdarm.binds")): ssd-arm.sh reset"; exit 2
fi
a=$(cat "$ARMST/ps3-ssdarm.swap" 2>/dev/null || echo usb)
[ "${SW:-usb}" = "$a" ] || { echo "!! BENCH_SWAP=${SW:-usb}, but the VRAM swap spills to $a: ssd-arm.sh set or reset"; exit 2; }

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
# Failure dump (header): core dumps on for the game, the trigger list, the sudo test
DUMPON=${BENCH_DUMPON:-0}; DUMP_N=0; DUMP_JOBS=(); DUMP_WHY=; DUMP_AT=; DUMP_SUDO=
DUMP_RE=${BENCH_DUMPON_RE:-Wait timeout|mmap commit error|out of GART|out of VRAM|no memory to back the GART}
DUMP_MAX=${BENCH_DUMPON_MAX:-1}
if [ "$DUMPON" = 1 ]; then
	mkdir -p "$D/dump"
	sudo -n true 2>/dev/null && DUMP_SUDO="sudo -n"
	{ echo "core_pattern=$(cat /proc/sys/kernel/core_pattern 2>&1)"; echo "trigger_re=$DUMP_RE"
	  command -v gdb > /dev/null || echo "gdb: missing (apt install gdb): no gcore"
	  echo "sudo=${DUMP_SUDO:-no}"; } > "$D/dump/setup.txt"
	ulimit -c unlimited 2>> "$D/dump/setup.txt" || echo "ulimit -c unlimited refused: no crash core" >> "$D/dump/setup.txt"
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
# Library switches for this run (docs/SPU-AUDIO.md, docs/PROFILING.md "Load-time decoders and
# linking"), run.sh ENVS tokens that become BENCH_<NAME>, added after the recipe and its variant:
#   FLUIDFX=off|norev|nocho|cho1|on  FluidSynth's reverb and chorus whatever the game asks for
#                   (FLUIDSYNTH_SETTINGS, our libfluidsynth3's patch 0002): both off, reverb off,
#                   chorus off, one chorus line instead of three, both on
#   ZLIB=ng|stock   zlib-ng's compat libz.so.1 in front of zlib1g's (LD_LIBRARY_PATH=$ZLIBNG_DIR, our
#                   libz-ng-compat; BENCH_ZLIBNG_DIR another directory), the default when it is
#                   installed, as ps3-game does for the menu's games; stock: zlib1g's (PS3_ZLIB=stock
#                   for ps3-game too). bench.txt gets zlib=ng|stock either way
#   LDPATH=DIR      DIR in front of the game's LD_LIBRARY_PATH: copies of the libraries a package of
#                   ours replaced (the stock libopenal.so.1, libvorbisfile.so.3, ...) for a same-boot A/B
#   BINDNOW=1       LD_BIND_NOW=1: every symbol bound at start and dlopen instead of at its first call
#   JSIMD=none|altivec  libjpeg-turbo's SIMD forced off or on (JSIMD_FORCENONE=1, JSIMD_FORCEALTIVEC=1)
# A switch the installed library can't honour stops the run (exit 2) rather than measure nothing.
# bench.txt gets libenv=<NAME=VALUE ...>; report.py shows it after the variant (base@fxoff, base@zng,
# base@bindnow, base@jsimdnone, base@ld<dir's name>).
LIBENV=()
ZLIBNG_DIR=${BENCH_ZLIBNG_DIR:-/usr/lib/powerpc64-linux-gnu/zlib-ng}
env_add() {	# env_add NAME VALUE: NAME=VALUE in the game's environment (the recipe's value replaced)
	local i
	for i in "${!ENV[@]}"; do [[ ${ENV[$i]} == "$1="* ]] && { ENV[$i]="$1=$2"; return; }; done
	ENV+=("$1=$2")
}
if [ -n "${BENCH_FLUIDFX:-}" ]; then
	case $BENCH_FLUIDFX in
	off) fx=synth.reverb.active=0,synth.chorus.active=0 ;;
	norev) fx=synth.reverb.active=0 ;;
	nocho) fx=synth.chorus.active=0 ;;
	cho1) fx=synth.chorus.nr=1 ;;
	on) fx=synth.reverb.active=1,synth.chorus.active=1 ;;
	*) echo "!! BENCH_FLUIDFX=$BENCH_FLUIDFX: off, norev, nocho, cho1 or on"; exit 2 ;;
	esac
	lib=${BENCH_FLUIDLIB:-/usr/lib/powerpc64-linux-gnu/libfluidsynth.so.3}
	grep -qa FLUIDSYNTH_SETTINGS "$lib" 2>/dev/null ||
		{ echo "!! BENCH_FLUIDFX: $lib has no FLUIDSYNTH_SETTINGS (libfluidsynth3 with patch 0002)"; exit 2; }
	env_add FLUIDSYNTH_SETTINGS "$fx"; LIBENV+=("FLUIDFX=$BENCH_FLUIDFX")
fi
LDPRE=	# directories in front of the game's LD_LIBRARY_PATH (ZLIB=ng, LDPATH=)
ZLIB_USED=stock
case ${BENCH_ZLIB:-} in
ng)	[ -e "$ZLIBNG_DIR/libz.so.1" ] || { echo "!! BENCH_ZLIB=ng: no $ZLIBNG_DIR/libz.so.1 (libz-ng-compat)"; exit 2; }
	ZLIB_USED=ng; LIBENV+=("ZLIB=ng") ;;
stock)	env_add PS3_ZLIB stock; LIBENV+=("ZLIB=stock") ;;
'')	[ -e "$ZLIBNG_DIR/libz.so.1" ] && ZLIB_USED=ng ;;	# the default, silently stock without it (the PC reference)
*)	echo "!! BENCH_ZLIB=$BENCH_ZLIB: ng or stock"; exit 2 ;;
esac
[ "$ZLIB_USED" = ng ] && LDPRE=$ZLIBNG_DIR
if [ -n "${BENCH_LDPATH:-}" ]; then	# e.g. a copy of the libraries a package replaced, for a same-boot A/B
	[ -d "$BENCH_LDPATH" ] && [ -n "$(ls "$BENCH_LDPATH"/*.so* 2>/dev/null)" ] ||
		{ echo "!! BENCH_LDPATH=$BENCH_LDPATH: no libraries there"; exit 2; }
	LDPRE=$BENCH_LDPATH${LDPRE:+:$LDPRE}; LIBENV+=("LDPATH=$BENCH_LDPATH")
fi
if [ -n "$LDPRE" ]; then
	l=${LD_LIBRARY_PATH:-}	# the recipe's, else ours: LDPRE goes in front
	for x in "${ENV[@]}"; do [[ $x == LD_LIBRARY_PATH=* ]] && l=${x#LD_LIBRARY_PATH=}; done
	env_add LD_LIBRARY_PATH "$LDPRE${l:+:$l}"
fi
if [ -n "${BENCH_BINDNOW:-}" ]; then
	[ "$BENCH_BINDNOW" = 1 ] || { echo "!! BENCH_BINDNOW=$BENCH_BINDNOW: 1"; exit 2; }
	env_add LD_BIND_NOW 1; LIBENV+=("BINDNOW=1")
fi
if [ -n "${BENCH_JSIMD:-}" ]; then
	case $BENCH_JSIMD in
	none) env_add JSIMD_FORCENONE 1 ;;
	altivec) env_add JSIMD_FORCEALTIVEC 1 ;;
	*) echo "!! BENCH_JSIMD=$BENCH_JSIMD: none or altivec"; exit 2 ;;
	esac
	LIBENV+=("JSIMD=$BENCH_JSIMD")
fi
# Sound dropouts (docs/games/SUITE.md "Sound dropouts"), on unless BENCH_XRUNLOG=0 (run.sh's ENVS token
# XRUNLOG=0): ps3-gpu-tools' ps3-xrunlog.so, an ALSA plugin, in front of the game's default sound device
# for this run only. The game's ALSA_CONFIG_PATH is alsa.conf (or the recipe's or the caller's list) and
# then ps3-xrunlog.conf, whose hook wraps pcm.default as /etc/alsa/conf.d, asound.conf and the user's
# ~/.asoundrc made it: no file of theirs is read differently or written. PS3_XRUNLOG=$D/xruns.log gets a
# line per dropout (when the output ran dry, the gap, the loudness on either side); xruns.py turns it
# into bench.txt's xrunlog=on|unused, xruns_logged=, xruns_audible=, xrun_gap_ms_total=,
# xrun_gap_ms_max=, xrun_audible_ms=, xrun_rule= and $D/xruns.tsv. xrunlog=missing: the plugin isn't
# installed; off: turned off. BENCH_XRUNLOG_CONF, BENCH_XRUNLOG_BASE: another conf and base (selftest.py).
XRUNLOG=off
XRUNLOG_CONF=${BENCH_XRUNLOG_CONF:-/usr/lib/ps3/lib/ps3-xrunlog.conf}
if [ "${BENCH_XRUNLOG:-1}" != 0 ]; then
	XRUNLOG=missing
	xbase=${BENCH_XRUNLOG_BASE:-${ALSA_CONFIG_PATH:-/usr/share/alsa/alsa.conf}}
	for x in "${ENV[@]}"; do [[ $x == ALSA_CONFIG_PATH=* ]] && xbase=${x#ALSA_CONFIG_PATH=}; done
	xlib=$(sed -n 's/^[[:space:]]*lib "\(.*\)"[[:space:]]*$/\1/p' "$XRUNLOG_CONF" 2>/dev/null | head -1)
	if [ -n "$xlib" ] && [ -e "$xlib" ] && [ -e "${xbase%%:*}" ]; then
		env_add ALSA_CONFIG_PATH "$xbase:$XRUNLOG_CONF"; env_add PS3_XRUNLOG "$D/xruns.log"
		rm -f "$D/xruns.log" "$D/xruns.tsv"; XRUNLOG=on
	fi
fi
# ps3rsx module parameters for this run (docs/HOTPATHS.md): every BENCH_PS3RSX_<NAME>=V (run.sh's
# ENVS token PS3RSX_SHADOW_FAST=1) is written to /sys/module/ps3rsx/parameters/<name> (lower case)
# through sudo -n before the game starts, and the value it had is put back when this script exits,
# however it ends. bench.txt gets kparam=<name=value ...>; report.py shows it after the variant
# (base@rsx_shadow_fast1). BENCH_PS3HTAB_<NAME>=V the same for the kernel's ps3_htab parameters
# (local/0046: PS3HTAB_INSERT_READBACK=0; kparam htab_insert_readback=0, report.py
# base@htab_insert_readback0). BENCH_SNDPS3_<NAME>=V the same for snd_ps3's (local/0047:
# SNDPS3_DMA_WATCHDOG_MS=0; kparam snd_dma_watchdog_ms=0, report.py base@snd_dma_watchdog_ms0; the
# card reads it when its stream starts, which is when the game opens it with nothing else playing).
# BENCH_KPARAM_DIR, BENCH_KPARAM_HTAB_DIR, BENCH_KPARAM_SND_DIR: other directories for them
# (selftest.py).
KPARAM=(); KPARAM_OLD=(); KPARAM_DIR=${BENCH_KPARAM_DIR:-/sys/module/ps3rsx/parameters}
KPARAM_HTAB_DIR=${BENCH_KPARAM_HTAB_DIR:-/sys/module/ps3_htab/parameters}
KPARAM_SND_DIR=${BENCH_KPARAM_SND_DIR:-/sys/module/snd_ps3/parameters}
kparam_write() {	# kparam_write FILE VALUE
	if [ -w "$1" ]; then echo "$2" > "$1"; else echo "$2" | sudo -n tee "$1" > /dev/null 2>&1; fi
}
kparam_restore() {
	local x
	for x in "${KPARAM_OLD[@]}"; do	# FILE=OLD
		kparam_write "${x%%=*}" "${x#*=}" ||
			echo "!! parameter ${x%%=*}: could not put ${x#*=} back"
	done
	KPARAM_OLD=()
}
for v in $(compgen -v BENCH_PS3RSX_) $(compgen -v BENCH_PS3HTAB_) $(compgen -v BENCH_SNDPS3_); do
	case $v in
	BENCH_PS3HTAB_*) n=${v#BENCH_PS3HTAB_}; n=${n,,}; f=$KPARAM_HTAB_DIR/$n; t=htab_$n ;;
	BENCH_SNDPS3_*) n=${v#BENCH_SNDPS3_}; n=${n,,}; f=$KPARAM_SND_DIR/$n; t=snd_$n ;;
	*) n=${v#BENCH_PS3RSX_}; n=${n,,}; f=$KPARAM_DIR/$n; t=$n ;;
	esac
	[ -e "$f" ] || { echo "!! $v: no $f (a ps3rsx or kernel without that parameter)"; exit 2; }
	o=$(cat "$f")
	kparam_write "$f" "${!v}" || { echo "!! $v: could not write $f (sudo -n)"; exit 2; }
	KPARAM_OLD+=("$f=$o"); KPARAM+=("$t=${!v}")
	trap 'restore; kparam_restore' EXIT
done
# Mesa's disk shader cache for this run (BENCH_SHADERCACHE, the top)
SC=${BENCH_SHADERCACHE:-}; SC_DIR=; SC_KB=
SC_ROOT=${BENCH_SHADERCACHE_ROOT:-/var/tmp/ps3-bench/shadercache}
sc_kb() { [ -n "$SC_DIR" ] && [ -d "$SC_DIR" ] && du -sk "$SC_DIR" 2>/dev/null | cut -f1; }
sc_done() { [ "$SC" = cold ] && [ -n "$SC_DIR" ] && rm -rf "$SC_DIR"; SC_DIR=; }
case $SC in
'') ;;
cold) SC_DIR=$SC_ROOT/cold.$$; rm -rf "$SC_DIR"; mkdir -p "$SC_DIR" ;;
warm) SC_DIR=$SC_ROOT/warm-$GAME; mkdir -p "$SC_DIR"
	[ -n "$(find "$SC_DIR" -type f -print -quit 2>/dev/null)" ] || SC=prime ;;	# empty: this run fills it
off) ENV+=(MESA_SHADER_CACHE_DISABLE=true) ;;
*) echo "!! BENCH_SHADERCACHE=$SC: cold, warm or off"; exit 2 ;;
esac
if [ -n "$SC_DIR" ]; then
	ENV+=(MESA_SHADER_CACHE_DIR="$SC_DIR"); SC_KB=$(sc_kb)
	trap 'restore; kparam_restore; sc_done' EXIT
fi
# start-up profiling (BENCH_PERF_LOAD, the top): the gate, ld.so's statistics
PLOAD=${BENCH_PERF_LOAD:-0}; GATE=(); GATE_FD=; PL_FD=; PL_ACK=; PL_ON=0; PL_WIN=(); PL_ACKS=; PL_END=
PL_TICKS=0; PL_T0=; MONO_T0=
if [ "$PLOAD" = 1 ]; then
	mkdir -p "$D/load-io"
	if [ "${BENCH_LDSTATS:-1}" = 1 ]; then
		mkdir -p "$D/ld"; ENV+=(LD_DEBUG=statistics LD_DEBUG_OUTPUT="$D/ld/stats")
	fi
	while read -r k _ v; do [ "$k" = timebase ] && echo "$v" > "$D/load-io/timebase"; done < /proc/cpuinfo
	if [ -n "${BENCH_PERF_LOAD_CTL:-}" ]; then
		rm -f "$D/.gate"; mkfifo "$D/.gate"; exec {GATE_FD}<> "$D/.gate"
		# the gate: waits for the client's line (BENCH_PERF_LOAD_WAIT + 30 s at most), then execs the game
		GATE=(bash -c 'exec 9<> "$0"; read -r -t "$1" -u 9 _; exec 9<&-; shift; exec "$@"' "$D/.gate"
			$(( ${BENCH_PERF_LOAD_WAIT:-120} + 30 )))
	fi
fi
fcopy() {	# fcopy FILE OUT: a /proc file copied by bash's builtins (no fork while the game loads)
	local l
	{ while IFS= read -r l || [ -n "$l" ]; do printf '%s\n' "$l"; done < "$1" > "$2"; } 2>/dev/null
}
io_snap() {	# io_snap LABEL: the disks, the VM and (once the game's pid is known) the game, into load-io/
	local o=$D/load-io/$1 p=
	fcopy /proc/diskstats "$o.diskstats"; fcopy /proc/vmstat "$o.vmstat"
	read -r p < "$D/.pid" 2>/dev/null
	if [ -n "$p" ] && [ -e "/proc/$p/stat" ]; then
		fcopy "/proc/$p/stat" "$o.stat"; fcopy "/proc/$p/task/$p/stat" "$o.mainstat"
		fcopy "/proc/$p/io" "$o.io"; fcopy "/proc/$p/status" "$o.status"
	fi
	printf 'label=%s real=%s pid=%s\n' "$1" "$EPOCHREALTIME" "$p" > "$o.info"
}
pload_off() {	# the start-up span is over: perf off, the counters
	local a t
	[ "$PLOAD" = 1 ] || return 0
	PL_END=
	if [ "$PL_ON" = 1 ]; then
		printf 'disable\n' >&"$PL_FD"
		read -r -t 1 -u "$PL_ACK" a && [ "$a" = ack ] || PL_ACKS=missing
		perf_ticks a "$D/.perfload.pid"; PL_TICKS=$(( a - PL_T0 )); PL_ON=0
		us; secs t $(( US - u0 )); PL_WIN+=("$t")
	fi
	[ -e "$D/load-io/warm.info" ] && return 0
	io_snap warm
	read -r a < "$D/.pid" 2>/dev/null && readlink "/proc/$a/exe" > "$D/perf-load.exe" 2>/dev/null
	return 0
}
launch=(); [ "$GAMEMODE" = 1 ] && launch=("${BENCH_PS3GAME:-ps3-game}")
# GAMEMODE=0 means no game mode: not even ps3-game-detect's (it reads the game's environment)
[ "$GAMEMODE" = 1 ] || ENV+=(PS3_GAME_MODE=off)
# The game's own files read at its start (docs/STALLS.md "Game prefetch"): BENCH_GAMEPREFETCH (run.sh
# GAMEPREFETCH=, an ENVS token GAMEPREFETCH=...) fetch (or 1): the list a learn run left read in one
# pass alongside the load, nothing learnt (every run reads the same list); learn: no fetch, the
# list made (or grown) from this run; on: both, as game mode does; off, 0 or unset: neither. Without
# game mode the command runs through `ps3-game-prefetch game-run` (with fetch it execs the game: same
# pid, nothing of it left in memory); with GAMEMODE=1 game mode's watcher does it
# (PS3_GAME_PREFETCH_GAME, which is off for bench runs otherwise). Lists in BENCH_GAMEPREFETCH_DIR
# (/var/tmp/ps3-bench/gprefetch: kept across runs and boots, apart from the desktop's).
# BENCH_GAMEPREFETCH_MB, _RESERVE_MB, _IO (tokens GAMEPREFETCH_MB=128 ...): the fetch's budget,
# memory reserve and I/O priority (PS3_GAME_PREFETCH_*: 64, 48, be7 when unset).
# bench.txt gets gprefetch=<mode>[_mb<N>][_reserve<N>][_io<X>]; report.py shows it after the
# variant (base@gpffetch, base@gpffetch_mb128).
GPF=${BENCH_GAMEPREFETCH:-}
case $GPF in 1) GPF=fetch ;; 0|off) GPF= ;; on|fetch|learn|'') ;;
*) echo "!! BENCH_GAMEPREFETCH=$GPF: on, fetch (1), learn or off (0)"; exit 2 ;; esac
GPF_BIN=${BENCH_GAMEPREFETCH_BIN:-/usr/lib/ps3/desktop/ps3-game-prefetch}
GPF_DIR=${BENCH_GAMEPREFETCH_DIR:-/var/tmp/ps3-bench/gprefetch}
if [ -n "$GPF" ]; then
	grep -q '^def game_run' "${GPF_BIN%/*}/ps3_game_prefetch.py" 2>/dev/null ||
		{ echo "!! BENCH_GAMEPREFETCH: no game-run in $GPF_BIN (ps3-desktop with the game prefetch)"; exit 2; }
	mkdir -p "$GPF_DIR"
	for v in MB RESERVE_MB IO; do
		x=BENCH_GAMEPREFETCH_$v
		[ -n "${!x:-}" ] || continue
		ENV+=("PS3_GAME_PREFETCH_$v=${!x}"); t=${v%_MB}; GPF+="_${t,,}${!x}"
	done
	if [ "$GAMEMODE" = 1 ]; then ENV+=(PS3_GAME_PREFETCH_GAME="${GPF%%_*}" PS3_GAME_PREFETCH_DIR="$GPF_DIR")
	else launch=("$GPF_BIN" game-run --mode "${GPF%%_*}" --dir "$GPF_DIR" --key "${CMD[0]}" --); fi
elif [ "$GAMEMODE" = 1 ] && [ "$SESSION" != 1 ]; then
	ENV+=(PS3_GAME_PREFETCH_GAME=off)
fi

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
[ -n "${DISPLAY:-}" ] && [ "$SESSION" != 1 ] && xset s off -dpms 2>/dev/null

helpers=()
# snd_ps3's interrupt counters and DMA watchdog restarts (kernel local/0047; absent on older kernels)
SND_STATS=$KPARAM_SND_DIR/dma_stats; SND_PRE=
[ -r "$SND_STATS" ] && read -r SND_PRE < "$SND_STATS"
# the game in its own process group (job control), so stopping it reaches every process it forks;
# its output through a fifo, read line by line here
set -m
fifo=$D/.game.fifo; rm -f "$fifo"; mkfifo "$fifo"
t0=$EPOCHREALTIME
# stdbuf: line-buffered stdio into the pipe, so END_RE and LOAD_MARK lines arrive when printed
"${GATE[@]}" env "${ENV[@]}" "${launch[@]}" stdbuf -oL -eL "${CMD[@]}" < /dev/null > "$fifo" 2>&1 &
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
# the start-up profile: perf on and attached (its ack) before the gate opens and the clock starts
if [ ${#GATE[@]} -gt 0 ]; then
	printf '%s\n' "$gp" > "$D/.launch.pid.new"; mv "$D/.launch.pid.new" "$D/.launch.pid"
	exec {PL_FD}<> "${BENCH_PERF_LOAD_CTL%%:*}"; exec {PL_ACK}<> "${BENCH_PERF_LOAD_CTL#*:}"
	printf 'enable\n' >&"$PL_FD"
	if read -r -t "${BENCH_PERF_LOAD_WAIT:-120}" -u "$PL_ACK" a && [ "$a" = ack ]; then
		PL_ON=1; PL_ACKS=ok
	else
		PL_ACKS="no ack for enable"; echo "!! start-up profile: perf never acknowledged enable (perf-load.err)"
	fi
fi
if [ "$PLOAD" = 1 ]; then
	io_snap start
	read -r MONO_T0 _ < /proc/uptime; t0=$EPOCHREALTIME	# the clock starts as the game execs
	[ -n "$GATE_FD" ] && printf 'go\n' >&"$GATE_FD"
	[ "$PL_ON" = 1 ] && PL_WIN+=(0.00)
fi
stop_game() {
	kill -0 "$gp" 2>/dev/null || return 0
	kill -TERM -- -"$gp" 2>/dev/null
	for i in $(seq 50); do kill -0 "$gp" 2>/dev/null || return 0; sleep 0.2; done
	kill -KILL -- -"$gp" 2>/dev/null
}
# stopped from outside (ps3-bench.sh's timeout, the thermal watchdog): still say how it ended
trap 'stop_game; kill "${helpers[@]}" $wm 2>/dev/null
	{ echo "game=$GAME"; echo "variant=$VARIANT"; echo "comm=$COMM"; echo "ended=killed"
	  [ "$SESSION" = 1 ] && echo "kind=session"
	  [ "$SESSION" = 1 ] && [ -n "${BENCH_LAUNCHER:-}" ] && { echo "launcher=$BENCH_LAUNCHER"; echo "launcher_file=${BENCH_LAUNCHER_FILE:-}"; }
	  echo "hwm=$(cat "$D/.hwm" 2>/dev/null)"; echo "sound=$(cat "$D/.sound" 2>/dev/null)"; shot_lines; } > "$D/bench.txt"
	exit 1' HUP INT TERM

# Failure dump: the game's state at the trigger line, into $D/dump/ (a background job: dump_capture PID
# LINE SECONDS). The game is stopped first so that what is read is one moment, and let go at the end.
dump_capture() {
	local x=$D/dump p=$1 tk f raw z
	{ printf 'trigger=%s\nat=%s\npid=%s\nreal=%s\n' "$2" "$3" "$p" "$EPOCHREALTIME"; } > "$x/trigger.txt"
	# the card first, with the clock its tstamp is on (the last time the hw pointer moved; uptime is
	# the same clock on the PS3, which never suspends), then again with the interrupts 1 s later
	{ echo "uptime: $(cat /proc/uptime)"; cat /proc/asound/cards
	  tail -n +1 /proc/asound/card*/pcm*p/sub*/{status,hw_params,sw_params}; } > "$x/alsa.txt" 2>&1
	{ grep -E 'snd|PS3 Audio|audio' /proc/interrupts; } > "$x/interrupts.txt" 2>&1
	{ tail -n +1 /sys/module/snd_ps3/parameters/*; } > "$x/snd_ps3.txt" 2>&1
	sleep 1
	{ echo "== 1 s later, uptime: $(cat /proc/uptime)"; tail -n +1 /proc/asound/card*/pcm*p/sub*/status; } >> "$x/alsa.txt" 2>&1
	{ echo "== 1 s later"; grep -E 'snd|PS3 Audio|audio' /proc/interrupts; echo "== all"; cat /proc/interrupts; } >> "$x/interrupts.txt" 2>&1
	for f in status maps smaps_rollup limits; do cat "/proc/$p/$f" > "$x/proc.$f" 2>&1; done
	ls "/proc/$p/fd" 2>/dev/null | wc -l > "$x/proc.nfd"
	for tk in /proc/$p/task/*; do
		echo "== tid ${tk##*/} comm $(cat "$tk/comm" 2>&1)"
		echo "stat: $(cat "$tk/stat" 2>&1)"; echo "wchan: $(cat "$tk/wchan" 2>&1)"; echo "syscall: $(cat "$tk/syscall" 2>&1)"
		echo "stack:"; $DUMP_SUDO cat "$tk/stack" 2>&1
	done > "$x/threads.txt"
	{ $DUMP_SUDO sh -c 'tail -n +1 /sys/kernel/debug/dri/*/ps3rsx_* /sys/module/ps3rsx/parameters/*'; } > "$x/gpu.txt" 2>&1
	cat /proc/meminfo > "$x/meminfo"; cat /proc/vmstat > "$x/vmstat"; cat /proc/swaps > "$x/swaps"
	$DUMP_SUDO dmesg 2>&1 | tail -200 > "$x/dmesg.tail"
	if command -v gdb > /dev/null; then
		# the backtraces of what the PS3 itself can name, then the core (gcore writes the file as named)
		$DUMP_SUDO timeout 300 gdb -batch -nx -p "$p" -ex 'thread apply all bt 40' -ex "gcore $x/core" > "$x/gdb.log" 2>&1
		[ -n "$DUMP_SUDO" ] && $DUMP_SUDO chown "$(id -u):$(id -g)" "$x/core" 2>/dev/null
		if [ -s "$x/core" ]; then
			raw=$(stat -c %s "$x/core")
			if command -v zstd > /dev/null; then zstd -q -1 -T0 --rm "$x/core" && z=$x/core.zst
			else xz -1 -T0 "$x/core" && z=$x/core.xz; fi
			echo "dump_core=$raw:$(stat -c %s "$z" 2>/dev/null || echo -)" > "$x/sizes.txt"
		else echo "gdb produced no core (see gdb.log)" >> "$x/setup.txt"; fi
	else echo "no gdb on the PS3: no core (apt install gdb)" >> "$x/setup.txt"; fi
	kill -CONT "$p" 2>/dev/null
	echo done > "$x/.complete"
}
dump_trigger() {	# dump_trigger LINE SECONDS: the first matching line: capture in the background
	local p
	read -r p < "$D/.pid" 2>/dev/null || p=$(pgrep -g "$gp" -x "$COMM" | head -1)
	DUMP_N=$((DUMP_N + 1)); DUMP_WHY=trigger; DUMP_AT=$2
	[ -n "$p" ] && [ -d "/proc/$p" ] || { echo "no game pid for the dump" >> "$D/dump/setup.txt"; return; }
	kill -STOP "$p" 2>/dev/null
	dump_capture "$p" "$1" "$2" > /dev/null 2>&1 & DUMP_JOBS+=($!)
}
dump_crash() {	# after a death by signal: the kernel's core into dump/core.crash.zst (or .xz)
	local x=$D/dump pat n i f cwd z=
	read -r pat < /proc/sys/kernel/core_pattern
	if [[ $pat == *systemd-coredump* ]] && command -v coredumpctl > /dev/null; then
		for i in 1 2 3 4 5 6 7 8; do	# systemd-coredump finishes after the process is gone
			$DUMP_SUDO coredumpctl dump "$game_pid" -o "$x/core.crash" > /dev/null 2>&1 && break; sleep 1
		done
	elif [[ $pat != '|'* ]]; then	# a file: the pattern's directory, else the game's working directory
		cwd=$(cat "$D/.cwd" 2>/dev/null); [[ $pat == /* ]] && cwd=${pat%/*}
		f=$(find "${cwd:-.}" -maxdepth 1 -name 'core*' -newer "$x/setup.txt" 2>/dev/null | head -1)
		[ -n "$f" ] && mv "$f" "$x/core.crash"
	fi
	if [ -s "$x/core.crash" ]; then
		n=$(stat -c %s "$x/core.crash")
		if command -v zstd > /dev/null; then zstd -q -1 -T0 --rm "$x/core.crash" && z=$x/core.crash.zst
		else xz -1 -T0 "$x/core.crash" && z=$x/core.crash.xz; fi
		echo "dump_core_crash=$n:$(stat -c %s "$z" 2>/dev/null || echo -)" >> "$x/sizes.txt"
		[ -n "$DUMP_WHY" ] && DUMP_WHY=trigger+crash || DUMP_WHY=crash
	else echo "no crash core found (core_pattern $pat)" >> "$x/setup.txt"; fi
}

# peak RSS of the game process (COMM), and the playback state of the sound card at SOUND_AT s
watch_game() {
	local p= i
	for i in $(seq 120); do
		p=$(pgrep -g "$gp" -x "$COMM" | head -1); [ -n "$p" ] && break; sleep 0.5
	done
	[ -n "$p" ] || return
	echo "$p" > "$D/.pid"
	[ "$DUMPON" = 1 ] && readlink "/proc/$p/cwd" > "$D/.cwd" 2>/dev/null
	# builtins only, no forks every second (the client's CPU time counts them): the tick is a read
	# timing out on a fifo nobody writes
	local k v u hwm= tick snd= st hp lhp= stall=0 first=
	mkfifo "$D/.tick"; exec {tick}<> "$D/.tick"; rm -f "$D/.tick"
	for snd in /proc/asound/card*/pcm*p/sub0/status; do break; done
	while [ -e "/proc/$p/status" ]; do
		while read -r k v u; do [ "$k" = VmHWM: ] && hwm=$v; done < "/proc/$p/status" 2>/dev/null
		[ -n "$hwm" ] && printf '%s\n' "$hwm" > "$D/.hwm"
		# the card's hw pointer standing still while RUNNING: its DMA stopped (LOG-FINDINGS.md 1;
		# dmix keeps the card running while any client is open, so a second without movement is one)
		if [ -r "$snd" ]; then
			st=; hp=
			while read -r k v u; do case $k in state:) st=$v ;; hw_ptr) hp=$u ;; esac; done < "$snd" 2>/dev/null
			if [ "$st" = RUNNING ] && [ -n "$hp" ] && [ "$hp" = "$lhp" ]; then
				stall=$((stall + 1)); [ -n "$first" ] || first=$EPOCHREALTIME
				printf '%s %s\n' "$stall" "$first" > "$D/.sndstall"
			fi
			lhp=$hp
		fi
		read -r -t 1 -u "$tick"
	done
}
# (helpers write nothing to stdout: it is ps3-bench.sh's pipe, held open by whoever has it)
[ -n "$COMM" ] && { watch_game > /dev/null 2>&1 & helpers+=($!); }
# the recipe's own helper while the game runs (input recording/replay); stopped with the others
DURING_PID=
declare -F during >/dev/null && { during > "$D/during.log" 2>&1 & DURING_PID=$!; helpers+=($!); }
# a played session: the input devices' activity, which ones the game opened and when each press came, for
# the Controls checks and the level load (inputlog.py: read-only, never grabs, never which key), as the
# player: the pads always, keyboards and mice with the `input` group (else those items stay untested). It
# ends by itself when the game's process group leader is gone (the final wait below collects it).
# BENCH_INPUTLOG=0: none
if [ "$SESSION" = 1 ] && [ "${BENCH_INPUTLOG:-1}" = 1 ] && command -v python3 > /dev/null; then
	rm -f "$D/.mark-req"
	nice -n 19 python3 -I "$here/inputlog.py" "$t0" "$gp" "$D/.mark-req" > "$D/input.tsv" 2> "$D/input.err" &
	[ -n "$SHOT_DIR" ] && declare -F mark_watch > /dev/null && { mark_watch > /dev/null 2>&1 & helpers+=($!); }
fi
(sleep "$SOUND_AT"; cat /proc/asound/card*/pcm*p/sub*/status 2>/dev/null |
	awk '$1 == "state:" { s = s (s ? "," : "") $2 } END { print s ? s : "none" }' > "$D/.sound") \
	> /dev/null 2>&1 & helpers+=($!)

ended=exit; load=; t_load=; l=; part=
# time without forks (the client's CPU time counts them): us sets US to microseconds since the epoch
us() { local t=${1:-$EPOCHREALTIME}; US=$(( ${t%.*} * 1000000 + 10#${t#*.} )); }
us "$t0"; u0=$US
[ "$SHOT_FROM" = start ] && shot_plan 0
age() { us; local d=$(( US - u0 )); printf '%d.%02d' $((d / 1000000)) $((d % 1000000 / 10000)); }
secs() { printf -v "$1" '%d.%02d' $(($2 / 1000000)) $(($2 % 1000000 / 10000)); }	# secs VAR MICROSECONDS

# profiling: PERF_AT/PERF_END when perf goes on/off, MEM_AT/MEM_LB the snapshots (microseconds from
# the start); prof_due runs what is due and sets PROF_NEXT to the next one
PERF_AT=; PERF_END=; PERF_ON=0; PERF_WIN=(); PERF_TICKS=0; PERF_T0=; PERF_ACKS=ok; MEM_AT=(); MEM_LB=()
MEM_JOBS=(); MEM_DONE=(); PROF_NEXT=
perf_ticks() {	# perf_ticks VAR [PIDFILE]: user+sys clock ticks of the perf processes so far (locals _*: not VAR)
	local _p _f _t=0 _pids=
	read -r _pids < "${2:-$D/.perf.pid}" 2>/dev/null
	for _p in $_pids; do
		read -r -a _f < "/proc/$_p/stat" 2>/dev/null && _t=$(( _t + _f[13] + _f[14] ))
	done
	printf -v "$1" '%d' "$_t"
}
[ "$PL_ON" = 1 ] && perf_ticks PL_T0 "$D/.perfload.pid"	# perf's own CPU from here (its attach before)
irq_snap() {	# irq_snap LABEL: interrupts and the htab hcalls (kernel local/0046) into irq/ (irqoff.py)
	mkdir -p "$D/irq"
	fcopy /proc/interrupts "$D/irq/$1.interrupts"
	[ -r "$KPARAM_HTAB_DIR/stats" ] && fcopy "$KPARAM_HTAB_DIR/stats" "$D/irq/$1.htab"
	printf 'label=%s real=%s\n' "$1" "$EPOCHREALTIME" > "$D/irq/$1.info"
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
	if [ "$1" = enable ]; then PERF_ON=1; perf_ticks PERF_T0; irq_snap start
	else PERF_ON=0; perf_ticks a; PERF_TICKS=$(( PERF_TICKS + a - PERF_T0 )); irq_snap end; fi
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
	[ "$PLOAD" = 1 ] && [ ! -e "$D/load-io/warm.info" ] && PL_END=$s	# the start-up profile ends there
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
	if [ -n "$PL_END" ]; then
		if [ $now -ge "$PL_END" ]; then pload_off; else PROF_NEXT=$PL_END; fi
	fi
	if [ -n "$PERF_AT" ]; then
		if [ $now -ge "$PERF_AT" ]; then perf_ctl enable; PERF_AT=
		elif [ -z "$PROF_NEXT" ] || [ "$PERF_AT" -lt "$PROF_NEXT" ]; then PROF_NEXT=$PERF_AT; fi
	fi
	if [ -z "$PERF_AT" ] && [ -n "$PERF_END" ]; then
		if [ $now -ge "$PERF_END" ]; then perf_ctl disable; PERF_END=
		elif [ -z "$PROF_NEXT" ] || [ "$PERF_END" -lt "$PROF_NEXT" ]; then PROF_NEXT=$PERF_END; fi
	fi
	while [ ${#SHOT_AT[@]} -gt 0 ]; do	# the screenshots due
		if [ $now -ge "${SHOT_AT[0]}" ]; then
			shot_take "${SHOT_Q[0]}" > /dev/null 2>&1 & SHOT_JOBS+=($!)
			SHOT_AT=("${SHOT_AT[@]:1}"); SHOT_Q=("${SHOT_Q[@]:1}")
		else
			{ [ -z "$PROF_NEXT" ] || [ "${SHOT_AT[0]}" -lt "$PROF_NEXT" ]; } && PROF_NEXT=${SHOT_AT[0]}
			break
		fi
	done
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
load_us=; hud_us=; stop_us=; play_us=; LOADMARK=0; np=0; plast=; measured=0; win0=; win1=; PASS=(); son=; SEGS=()
loaded() {	# the load ended now (US)
	load_us=$(( US - u0 ))
	[ "$SHOT_FROM" = load ] && shot_plan "$load_us"
	[ "$PLOAD" = 1 ] && io_snap load
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
	if [ "$SHOT_FROM" = mark ] && [ -z "$SHOT_REF_US" ] && [ ${#SHOT_PTS[@]} -gt 0 ]; then	# during()'s shot_mark
		if read -r shot_m < "$D/.shot-mark" 2>/dev/null && [ -n "$shot_m" ]; then
			us "$shot_m"; shot_plan $(( US - u0 )); us
		elif [ -z "$DURING_PID" ] || ! kill -0 "$DURING_PID" 2>/dev/null; then	# never came: the load's end
			SHOT_FROM=load; SHOT_SRC=load-nomark; [ -n "$load_us" ] && shot_plan "$load_us"
		fi
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
	# until during()'s shot_mark (SHOT_FROM=mark): a look every 0.5 s
	[ "$SHOT_FROM" = mark ] && [ -z "$SHOT_REF_US" ] && [ ${#SHOT_PTS[@]} -gt 0 ] &&
		{ [ -z "$wt" ] || [ $wt -gt 500000 ]; } && wt=500000
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
	if [ "$DUMPON" = 1 ] && [ "$DUMP_N" -lt "$DUMP_MAX" ] && [[ $l =~ $DUMP_RE ]]; then
		secs DUMP_AT $(( US - u0 )); dump_trigger "$l" "$DUMP_AT"
	fi
	if [ -z "$load_us" ] && { { [ -n "$LOAD_MARK" ] && [[ $l =~ $LOAD_MARK ]]; } ||
	                          { [ -n "$LOAD_RE" ] && [[ $l =~ $LOAD_RE ]]; }; }; then LOADMARK=1; loaded; fi
	if [ -n "$PLAY_MARK" ] && [ -z "$play_us" ] && [[ $l =~ $PLAY_MARK ]]; then play_us=$(( US - u0 )); fi
	if [ -z "$t_load" ] && [ -n "$LOAD_MARK" ] && [[ $l =~ $LOAD_MARK ]]; then t_load=$(age); fi
	if [ "$SHOT_FROM" = re ] && [ -z "$SHOT_REF_US" ] && [[ $l =~ $SHOT_RE ]]; then shot_plan $(( US - u0 )); fi
	if [ -n "$WIN_ON_RE" ]; then	# the game's own window pieces
		if [ -z "$son" ] && [[ $l =~ $WIN_ON_RE ]]; then son=$(( US - u0 ))
			[ ${#SEGS[@]} -eq 0 ] && pload_off	# the start-up span ends at the first piece
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
pload_off
[ -n "$GATE_FD" ] && exec {GATE_FD}>&-
read -r game_pid < "$D/.pid" 2>/dev/null || game_pid=
[ ${#MEM_JOBS[@]} -gt 0 ] && wait "${MEM_JOBS[@]}"
if [ "$MEM" = 1 ] && [ ${#MEM_DONE[@]} -gt 0 ] && [[ " ${MEM_DONE[*]} " != *" end "* ]]; then
	mem_snap end > /dev/null 2>&1; MEM_DONE+=(end)
fi
[ ${#SHOT_JOBS[@]} -gt 0 ] && wait "${SHOT_JOBS[@]}"	# a screenshot still being taken: of the game
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do [ -e "$D/.markbusy" ] || break; sleep 0.5; done
[ ${#DUMP_JOBS[@]} -gt 0 ] && wait "${DUMP_JOBS[@]}"	# the capture first (it has timeouts), then the game goes
stop_game
while IFS= read -r -t 2 -u 3 l; do l=$part$l; part=; printf '%s\n' "$l"; printf '%s\n' "$l" >> "$D/game.log"; done
wait "$gp" 2>/dev/null; rc=$?
[ "$DUMPON" = 1 ] && [ "$rc" -gt 128 ] && [ "$rc" -ne 143 ] && [ "$rc" -ne 137 ] && dump_crash
exec 3<&-; rm -f "$fifo"
kill "${helpers[@]}" $wm 2>/dev/null; wait 2>/dev/null
# A played session: how it ended. rc is the game's exit code (ps3-game passes it on); a crash shows
# in dmesg.log (the sampler's dmesg -W, since UP0) as a segfault of the game or an oom kill.
ended_why=
if [ "$SESSION" = 1 ]; then
	dm=$(awk -v up="$UP0" -v c="$COMM" -v p="${game_pid:-0}" '
		match($0, /^\[ *[0-9.]+\]/) { t = substr($0, 2, RLENGTH - 2) + 0 }
		t + 1 >= up && /segfault|unhandled signal|trap invalid|oom-kill|Out of memory|Killed process/ &&
			((c != "" && index($0, c)) || p > 0 && (index($0, "[" p "]") || index($0, " " p " ") || index($0, "(" p ")"))) { print }
		' "$D/dmesg.log" 2>/dev/null | head -3)
	if [ "$ended" != exit ]; then ended_why="the harness stopped the game ($ended)"; ended=killed
	elif [[ $dm =~ oom-kill|Out\ of\ memory|Killed\ process ]]; then ended=oom; ended_why="out of memory: ${dm%%$'\n'*}"
	elif [ -n "$dm" ]; then ended=crash; ended_why="${dm%%$'\n'*}"
	elif [ "$rc" = 0 ]; then ended=quit; ended_why="exit code 0"
	elif [ "$rc" = 143 ] || [ "$rc" = 137 ]; then ended=closed; ended_why="SIGTERM/SIGKILL (Ctrl+Alt+Esc or kill)"
	elif [ "$rc" -gt 128 ]; then ended=crash; ended_why="signal $((rc - 128)) ($(kill -l $((rc - 128)) 2>/dev/null))"
	else ended=error; ended_why="exit code $rc"; fi
fi

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
# A played session: the play is everything from the first gameplay frame (PLAY_MARK), else the load
# mark, to the end; without either the whole run (play_from=start, an upper bound); window= is that span
play_from=; play_start=; play_seconds=
if [ "$SESSION" = 1 ]; then
	if [ -n "$play_us" ]; then play_from=mark; ps_us=$play_us
	elif [ "$LOADMARK" = 1 ] && [ -n "$load_us" ]; then play_from=load; ps_us=$load_us
	else play_from=start; ps_us=0; fi
	[ "$ps_us" -gt "$run_us" ] && ps_us=$run_us
	secs play_start "$ps_us"; secs play_seconds $(( run_us - ps_us ))
	w_us=$ps_us; [ "$play_from" = start ] && [ -n "$hud_us" ] && w_us=$hud_us
	secs w0 "$w_us"; window="$w0 $t_run"
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
  [ -n "$OAL_TOKEN" ] && echo "oal=$OAL_TOKEN"
  [ ${#LIBENV[@]} -gt 0 ] && echo "libenv=${LIBENV[*]}"
  echo "zlib=$ZLIB_USED"
  [ ${#KPARAM[@]} -gt 0 ] && echo "kparam=${KPARAM[*]}"
  [ -n "$GPF" ] && echo "gprefetch=$GPF"
  [ -n "$SC" ] && { echo "shadercache=$SC"; echo "shadercache_kb=${SC_KB:--} $(sc_kb || echo -)"; }
  [ -n "$GD" ] && { echo "gamedata=$GD"; [ -n "$GD_DIRS" ] && echo "gamedata_dirs=$GD_DIRS"; }
  [ -n "$SW" ] && { echo "swap=$SW"; echo "swaps=$(awk 'NR > 1 { printf "%s%s:%s", s, $1, $5; s = " " }' /proc/swaps)"; }
  echo "game_pid=$game_pid"
  if [ "$SESSION" = 1 ]; then
	echo "kind=session"; [ -n "${BENCH_LAUNCHER:-}" ] && { echo "launcher=$BENCH_LAUNCHER"; echo "launcher_file=${BENCH_LAUNCHER_FILE:-}"; }; echo "ended_why=$ended_why"
	echo "play_from=$play_from"; echo "play_start=$play_start"; echo "play_seconds=$play_seconds"
	echo "vsync=shipped"; echo "gamemode=1"
  fi
  echo "root=$(findmnt -n -o SOURCE / 2>/dev/null || echo -)"	# USB root or the SSD's /dev/mapper/ps3root (Phase B)
  if [ "$PLOAD" = 1 ]; then
	echo "mono_t0=$MONO_T0"
	if [ -n "${BENCH_PERF_LOAD_CTL:-}" ]; then
		echo "perf_load_win=${PL_WIN[*]}"; echo "perf_load_ack=${PL_ACKS:-missing}"
		awk -v t="$PL_TICKS" 'BEGIN { printf "perf_load_cpu=%.2f\n", t / 100 }'
	fi
  fi
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
  # OpenAL Soft's wedged mixer: "Wait timeout... buffer size too low?" and ALSA's "mmap commit error"
  echo "oal_wedge=$(grep -acE 'Wait timeout|mmap commit error' "$D/game.log" 2>/dev/null)"
  # the two apart: Wait timeout is the wedge (the card's DMA stopped: snd_stall below), mmap commit
  # error / available update failed a client xrun (the mixer thread starved past its 32 ms buffer;
  # OpenAL recovers, a click)
  echo "oal_wait=$(grep -ac 'Wait timeout' "$D/game.log" 2>/dev/null)"
  echo "oal_xrun=$(grep -acE 'mmap commit error|available update failed' "$D/game.log" 2>/dev/null)"
  # the card's DMA stopped: seconds its hw pointer stood still while RUNNING, and when that began;
  # snd_ps3's watchdog restarts in this run (kernel local/0047), its setting and counters
  read -r SST_N SST_AT < "$D/.sndstall" 2>/dev/null || SST_N=0
  echo "snd_stall=$SST_N"
  [ -n "${SST_AT:-}" ] && awk -v b="$SST_AT" -v t="$t0" 'BEGIN { printf "snd_stall_at=%.2f\n", b - t }'
  if [ -n "$SND_PRE" ] && read -r SND_POST < "$SND_STATS"; then
	r0=${SND_PRE#*restarts=}; r0=${r0%% *}; r1=${SND_POST#*restarts=}; r1=${r1%% *}
	echo "snd_restarts=$((r1 - r0))"; echo "snd_wd=$(cat "$KPARAM_SND_DIR/dma_watchdog_ms" 2>/dev/null)"
	echo "snd_stats=$SND_POST"
  fi
  # the dropouts ps3-xrunlog measured (the Sound dropouts block above): which ones could be heard
  if [ "$XRUNLOG" = on ]; then
	python3 -I "$here/xruns.py" "$D/xruns.log" "$t0" "$D/xruns.tsv" 2>/dev/null || echo "xrunlog=error"
  else
	echo "xrunlog=$XRUNLOG"
  fi
  if [ "$DUMPON" = 1 ]; then
	echo "dump=${DUMP_WHY:-none}"; [ -n "$DUMP_AT" ] && echo "dump_at=$DUMP_AT"
	cat "$D/dump/sizes.txt" 2>/dev/null
  fi
  shot_lines
  parse; } > "$D/bench.txt"
rm -f "$D/.shot-mark" "$D/.cwd" "$D/.hwm" "$D/.pid" "$D/.sound" "$D/.malloc.fifo" "$D/.gate" "$D/.launch.pid"
sed 's/^/bench: /' "$D/bench.txt"
exit 0
}
