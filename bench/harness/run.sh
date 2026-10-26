#!/bin/bash
# run.sh: the game suite runner (docs/games/SUITE.md), on the PS3 as tibi over SSH. Every run goes
# through `ps3-bench.sh x` (desktop stopped, swaps emptied, samples, thermal watchdog) with
# client.sh and a recipe (the game's benchmark/recipe.sh: games.sh). Copy this directory and the
# games' benchmarks with `make bench-push`. GAME is a gamedb id (an old recipe name also works).
#   run.sh smoke GAME [VARIANT]        a first launch: VRAM swap off (NOVSWAP=0 keeps it), 360 s
#                                      at most (warm-up and window included); prints the result,
#                                      kernel and game errors to check
#   run.sh bench GAME "VARIANTS" [N]   N rounds (default 3), the variants interleaved in each round
#   run.sh suite "VARIANTS" [N]        the same over every recipe with SUITE=1, games interleaved
#                                      (a game without a variant runs base once per round)
#   run.sh list                        recipes and their variants
#   run.sh session GAME [LAUNCHER]     a played session (session.sh): the live desktop, game mode, vsync
#                                      as shipped, no time limit; the player quits, a form follows
# Variant `base` is the recipe as written. End the session with `ps3-bench.sh desktop`.
# STALLS=1: the stall recorder runs alongside (ps3-bench.sh BENCH_STALLS=1, docs/STALLS.md); smoke
# then prints stalls.py's tables, else `python3 /var/tmp/bench/analysis/stalls.py <run dir>...` does.
# PERF=1: perf profiles the measured window (ps3-bench.sh BENCH_PERF=1, docs/PROFILING.md;
# PERF_HZ samples a second, default 499; PERF_ALL=1 adds a system-wide record without call graphs);
# after each run, outside the timed part, perf-post.sh turns perf.data into perf-report.txt,
# perf.folded and the perf_* columns (PERF_SYMS=1: also the symbols for the PC). Profiled runs are
# for profiles, not for fps. MEM=1: the game's memory by mapping class, glibc's malloc numbers and
# its GPU buffers at the window's start, middle and end (client.sh, memprof.py; mem_* columns).
# WAKER=MS (WAKER_MODE sleep|kick, WAKER_CPU=LIST), PROBE=1 (PROBE_MS, PROBE_CPU), STALLS_MS,
# STALLS_CPU, STALLS_NOWRITE=1: ps3-bench.sh's BENCH_* of the same names (ps3-waker; docs/STALLS.md
# "The recorder removes stalls"). ENVS="TOKEN...": bench runs every variant once per token in each
# round, the tokens interleaved; a token is - (nothing) or NAME=VALUE pairs joined by + (NAME
# without BENCH_ gets it: WAKER=10+WAKER_CPU=1), e.g. ENVS="- STALLS=1 WAKER=10".
# Report.py shows each run's switches after its variant (base@rec10, bench.txt harness=).
# MALLOC=LIST: glibc malloc presets for the game (client.sh BENCH_MALLOC: arena1, arena2, mmap, trim,
# tune; MALLOC_TRIM_S the trim period), docs/MEMORY.md; as ENVS tokens: ENVS="- MALLOC=arena2
# MALLOC=tune" (report.py: base@marena2, base@mtune). ENVS works in suite runs too. Mesa driver
# switches as tokens: NV30_X=V goes to the game as NV30_X=V (client.sh genv=, report.py base@x<V>),
# e.g. ENVS="- NV30_TRANSLATE_LUT=1 NV30_FENCE_BO_POOL=16" (docs/HOTPATHS.md). ps3rsx module
# parameters as tokens: PS3RSX_NAME=V is written to /sys/module/ps3rsx/parameters/name for that run
# and put back after it (sudo -n; client.sh kparam=, report.py base@rsx_name<V>); PS3HTAB_NAME=V the
# same for the kernel's /sys/module/ps3_htab/parameters/name (local/0046, base@htab_name<V>),
# SNDPS3_NAME=V for snd_ps3's (local/0047: SNDPS3_DMA_WATCHDOG_MS=0, base@snd_dma_watchdog_ms0).
# GAMEPREFETCH=fetch|learn|on: the game's own files read at its start (client.sh BENCH_GAMEPREFETCH,
# docs/STALLS.md "Game prefetch"): one learn run first, then ENVS="- GAMEPREFETCH=fetch" (report.py:
# base@gpffetch); lists in /var/tmp/ps3-bench/gprefetch; GAMEPREFETCH_MB, _RESERVE_MB, _IO its budget,
# reserve and I/O priority (tokens: GAMEPREFETCH=fetch+GAMEPREFETCH_MB=128, report.py gpffetch_mb128).
# PERF_LOAD=1: perf profiles the start-up instead (ps3-bench.sh BENCH_PERF_LOAD=1, docs/PROFILING.md
# "Start-up profiling"): from the game's exec through its loading screens and the warm-up to where the
# window starts; perf-post.sh then writes perf-load.folded, perf-load-report.txt, perf-load-stacks.tsv
# and loadprof.py's tables (report.py --load). Also the disk, VM and game counters of the load
# (load-io/) and ld.so's statistics (ld/; LDSTATS=0: without). SHADERCACHE=cold|warm|off: Mesa's
# disk shader cache for the run (client.sh BENCH_SHADERCACHE; the user's own cache is left alone;
# report.py base@sccold, base@scwarm, base@scprime for the run that filled the warm one), e.g.
# PERF_LOAD=1 ENVS="SHADERCACHE=cold SHADERCACHE=warm" run.sh bench openarena gl2min 2.
# BATCH_TOTAL=N (BATCH_NAME=x): the bench screen shows "Test K of N" for each run of a batch; K counts in
# BATCH_POS_FILE (default /dev/shm/ps3-bench.batchpos), which the batch script removes when it starts.
# ROTATE=1: bench rotates the ENVS tokens each round (round 2 starts at the second token), against
# order bias. GAMEDATA=ssd, SWAP=ssd (also as ENVS tokens: ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd"): the SSD
# test drive's arms (docs/GAMEOS-HDD.md "Test drive, Phase A"), put in place by ssd-arm.sh before each
# run's quiet and taken back for runs without them (usb, the default): the game's data dirs (the
# recipe's DATA_DIRS) from the SSD copy, the VRAM swap's spill on the SSD; a switch of the swap arm
# stops the desktop and empties the swaps first (ps3-bench.sh quiet). client.sh checks the arm is
# there and records gamedata=, swap= (report.py: base@gdssd, base@gdssd+swssd). Everything back to
# usb when run.sh ends (ssd-arm.sh reset).
# Library switches as ENVS tokens (client.sh, docs/SPU-AUDIO.md, docs/PROFILING.md "Load-time decoders
# and linking"): OAL=b16[,lin,...] OpenAL Soft's settings (openal.inc's names) on any variant,
# FLUIDFX=off|norev|nocho|cho1|on FluidSynth's reverb and chorus (our libfluidsynth3's
# FLUIDSYNTH_SETTINGS), ZLIB=ng|stock zlib-ng's libz.so.1 (libz-ng-compat, the default) or zlib1g's, LDPATH=DIR copies of replaced
# libraries in front (a same-boot A/B against a package's previous version), BINDNOW=1 LD_BIND_NOW,
# JSIMD=none|altivec libjpeg-turbo's SIMD; report.py base@oalb16, @fxoff, @zng, @bindnow, @jsimdnone.
# DUMPON=1: a failure dump for investigation runs, never for measured ones (client.sh header,
# docs/games/LOG-FINDINGS.md "Catching failures"): the first "Wait timeout", "mmap commit error", "out of
# GART", "out of VRAM" or "no memory to back the GART" line (DUMPON_RE another regex, DUMPON_MAX captures,
# default 1) freezes the game and saves its core (gdb on the PS3), /proc, threads, ALSA and ps3rsx state into
# the run's dump/; a crash's core too. bench.txt dump=, dump_at=, oal_wedge=; analyse with dumpana.sh DIR.
# Interference (docs/STALLS.md "Interference"): ps3-bench.sh's detector flags a run somebody
# disturbed (input, a VT switch, a device, an SSH login or command); run.sh prints a warning after it,
# report.py marks it and leaves it out of the medians, matrix.py skips it. RERUN_INTERFERED=1 runs
# such a run once more right away (both directories stay; the clean one counts).
# SHOTS="30 90" (an ENVS token with commas: SHOTS=70,130): screenshots of the screen (xwd, client.sh BENCH_SHOTS) at those
# seconds after the game state the recipe names (SHOT_FROM: the load's end by default); auto: the
# recipe's SHOT_POINTS; off: none. Smoke runs take them by default (auto), bench and suite runs only
# when SHOTS is given (a capture costs frames: report.py keeps runs with a shot in the window apart,
# base@shot). The captures are written to /var/tmp/ps3-bench/.shots-<run> (the disk, not /dev/shm) and
# moved into the run's shots/ after it; on the PC `make bench-pull` turns them into PNGs and the sheet
# against the PC reference (shots.py, tools/bench-ref.sh; docs/games/SUITE.md "Screenshots").
# Results: /var/tmp/ps3-bench/<stamp>-<game>-<variant>/; report.py summarises them.
{
set -u
here=$(cd "$(dirname "$0")" && pwd); BENCH=${here%/*}	# bench/harness, bench (lib/, analysis/ beside it)
. "$here/games.sh"
B=ps3-bench.sh
BATCH_POS_FILE=${BATCH_POS_FILE:-/dev/shm/ps3-bench.batchpos}	# the batch counter (reset it when a batch starts)
LOG=/dev/shm/run.sh.$$
[ -n "${STALLS:-}" ] && export BENCH_STALLS=$STALLS
[ -n "${PERF:-}" ] && export BENCH_PERF=$PERF
[ -n "${PERF_HZ:-}" ] && export BENCH_PERF_HZ=$PERF_HZ
[ -n "${PERF_ALL:-}" ] && export BENCH_PERF_ALL=$PERF_ALL
[ -n "${PERF_LOAD:-}" ] && export BENCH_PERF_LOAD=$PERF_LOAD
[ -n "${MEM:-}" ] && export BENCH_MEM=$MEM
[ -n "${SHOTS:-}" ] && export BENCH_SHOTS=$SHOTS
SHOT_ROOT=${BENCH_SHOT_ROOT:-/var/tmp/ps3-bench}
for v in STALLS_MS STALLS_CPU STALLS_NOWRITE WAKER WAKER_MODE WAKER_CPU PROBE PROBE_MS PROBE_CPU MALLOC MALLOC_TRIM_S GAMEPREFETCH GAMEPREFETCH_MB GAMEPREFETCH_RESERVE_MB GAMEPREFETCH_IO SHADERCACHE SHADERCACHE_ROOT LDSTATS PERF_LOAD_WAIT GAMEDATA SWAP DUMPON DUMPON_RE DUMPON_MAX; do
	[ -n "${!v:-}" ] && export "BENCH_$v=${!v}"
done
RUNS=()
ARMST=${BENCH_SSDARM_STATE:-/dev/shm}
arm_used() { [ -s "$ARMST/ps3-ssdarm.binds" ] || [ "$(cat "$ARMST/ps3-ssdarm.swap" 2>/dev/null || echo usb)" != usb ]; }
arm() {	# arm GAME VARIANT GAMEDATA SWAP: this run's SSD arm (ssd-arm.sh), before ps3-bench.sh's quiet
	[ "$3" = usb ] && [ "$4" = usb ] && ! arm_used && return 0
	if [ "$4" != "$(cat "$ARMST/ps3-ssdarm.swap" 2>/dev/null || echo usb)" ]; then
		$B quiet || return 1	# desktop down, swaps empty: the VRAM swap goes off next
	fi
	"$here/ssd-arm.sh" set "$@"
}
arm_reset() { arm_used && "$here/ssd-arm.sh" reset; return 0; }
trap arm_reset EXIT

one() {	# one LABEL GAME VARIANT [ENVTOKEN]: one run, its directory appended to RUNS
	local rc d x n ev=() a=()
	if [ -n "${4:-}" ] && [ "$4" != - ]; then	# NAME=VALUE+NAME=VALUE: BENCH_NAME for this run
		IFS=+ read -r -a a <<< "$4"
		for x in "${a[@]}"; do
			n=${x%%=*}; [ "$n" != "$x" ] || { echo "!! ENVS: $x is not NAME=VALUE"; return 1; }
			[[ $n == BENCH_* ]] || n=BENCH_$n
			ev+=("$n=${x#*=}")
		done
		echo ">> environment for this run: ${ev[*]}"
	fi
	local gd=${BENCH_GAMEDATA:-usb} sw=${BENCH_SWAP:-usb}
	for x in "${ev[@]}"; do case $x in BENCH_GAMEDATA=*) gd=${x#*=} ;; BENCH_SWAP=*) sw=${x#*=} ;; esac; done
	arm "$2" "$3" "$gd" "$sw" || { echo "!! the SSD arm (GAMEDATA=$gd SWAP=$sw) failed: stopping"; return 1; }
	local prog=	# "Test N of M" on the bench screen when a batch exports BATCH_TOTAL (and BATCH_NAME)
	if [ -n "${BATCH_TOTAL:-}" ]; then
		x=$(( $(cat "$BATCH_POS_FILE" 2>/dev/null || echo 0) + 1 )); echo "$x" > "$BATCH_POS_FILE"
		prog="Test $x of $BATCH_TOTAL${BATCH_NAME:+ in batch $BATCH_NAME}"
	fi
	env "${ev[@]}" BENCH_PROGRESS="$prog" BENCH_WHAT="$2 $3${ev[*]:+ (${ev[*]})}" $B x "$1" -- "$here/client.sh" "$2" "$3" 2>&1 | tee "$LOG"
	rc=${PIPESTATUS[0]}
	d=$(sed -n 's/^>> \(\/var\/tmp\/ps3-bench\/[^ ]*\)$/\1/p' "$LOG" | tail -1)
	rm -f "$LOG"
	[ -n "$d" ] && RUNS+=("$d") && versions "$2" "$3" > "$d/versions"
	# the screenshots from the disk into the run directory (client.sh wrote them beside it)
	[ -n "$d" ] && [ -d "$SHOT_ROOT/.shots-${d##*/}" ] && mv "$SHOT_ROOT/.shots-${d##*/}" "$d/shots"
	# the profile's text, between runs (the next run's quiet drops the caches and swaps again)
	{ [ -s "$d/perf.data" ] || [ -s "$d/perf-load.data" ]; } && "$here/perf-post.sh" "$d"
	[ "$rc" = 0 ] || { echo "!! ps3-bench.sh failed ($rc): stopping"; return 1; }
	[ -e "$d/THERMAL" ] && { echo "!! thermal alert: stopping"; return 1; }
	[ -e "$d/TEMPLIMIT" ] && { echo "!! temperature limit (BENCH_TEMP_MAX): stopping"; return 1; }
	if [ -n "$d" ] && grep -q '^interfered=1' "$d/bench.txt" 2>/dev/null; then
		echo "!! INTERFERENCE in ${d##*/}: $(sed -n 's/^interference=//p' "$d/bench.txt" | tail -1)"
		echo "!! this run doesn't count (report.py and matrix.py leave it out)"
		if [ "${RERUN_INTERFERED:-0}" = 1 ] && [ -z "${RERUN:-}" ]; then
			echo ">> RERUN_INTERFERED=1: the same run once more"
			RERUN=1 one "$@"
			return
		fi
	fi
	return 0
}

versions() {	# versions GAME VARIANT: the game's package and the graphics stack (outside the timed client)
	local p
	p=$( (CMD=(); bench_use "$1"; . "$(bench_recipe "$1")"; declare -F "variant_$2" >/dev/null && "variant_$2"
		readlink -f "$(command -v "${CMD[0]}")") 2>/dev/null)
	dpkg-query -W $(dpkg -S "$p" 2>/dev/null | cut -d: -f1 | tr , ' ') mesa-libgallium ps3rsx-modules-ps3 libspeq0 libfluidsynth3 libopenal1 libsdl3-0 ps3-gpu-tools 2>/dev/null
	uname -r
	# the Walnut release (os-release's VERSION_ID): the version a test is filed under in the games database
	printf 'walnut\t%s\n' "$(. /etc/os-release 2>/dev/null; echo "${VERSION_ID:-}")"
}
game() { bench_id "$1" || { echo "!! no recipe $1 (run.sh list)" >&2; return 1; }; }	# a name -> its id
variants() { grep -o '^variant_[a-z0-9_]*' "$(bench_recipe "$1")" | sed 's/^variant_//' | tr '\n' ' '; }
report() { [ ${#RUNS[@]} -gt 0 ] && python3 "$BENCH/analysis/report.py" "$@" "${RUNS[@]}"; }

# rotated ROUND TOKEN...: the tokens, started at a different one each round when ROTATE=1 (round 1 as
# given, round 2 from the second, ...), so no arm always runs first in its round; off by default
# (an order like SHADERCACHE=cold then warm must stay as written).
rotated() {
	local r=$1; shift
	[ "${ROTATE:-0}" = 1 ] || { echo "$@"; return; }
	local a=("$@") k=$(( (r - 1) % $# )) i
	for ((i = 0; i < $#; i++)); do printf '%s ' "${a[(k + i) % $#]}"; done
	echo
}

case ${1:-} in
smoke)
	g=$(game "${2:?game}") || exit 2; v=${3:-base}
	BENCH_SHOTS=${BENCH_SHOTS:-auto} BENCH_TIMEOUT=${BENCH_TIMEOUT:-360} BENCH_NOVSWAP=${NOVSWAP:-1} one "smoke-$g-$v" "$g" "$v"
	d=${RUNS[0]:-}; [ -n "$d" ] || exit 1
	echo "== $g $v"; cat "$d/bench.txt" 2>/dev/null || echo "(no bench.txt: the client didn't finish)"
	echo "== kernel, during the run"
	grep -aiE 'ps3rsx|nouveau|rsx|exception|error|fault|oom|killed|thermal|segfault' "$d/dmesg.log" | tail -20
	echo "== game output: lines worth a look"
	grep -aiE 'error|fail|unsupported|not supported|missing|shader|link|segmentation|abort|assert|nouveau|out of temps' \
		"$d/game.log" 2>/dev/null | head -30
	report --stalls --perf --mem --load
	n=$(ls "$d"/shots/*.xwd 2>/dev/null | wc -l)
	echo "Picture: $n screenshot(s) in $d/shots (bench.txt shot=); on the PC make bench-pull makes the PS3 | PC sheet."
	echo "Sound by ear (the TV); the sound card state at ${SOUND_AT:-15} s is in bench.txt."
	;;
bench)
	g=$(game "${2:?game}") || exit 2; vs=${3:-base}; n=${4:-3}
	for i in $(seq "$n"); do
		for v in $vs; do
			for e in $(rotated "$i" ${ENVS:--}); do one "$g-$v" "$g" "$v" "$e" || break 3; done
		done
	done
	report
	;;
suite)
	vs=${2:-base}; n=${3:-3}
	games=$(for g in $(bench_ids); do grep -q '^SUITE=1' "$(bench_recipe "$g")" && echo "$g"; done)
	for i in $(seq "$n"); do
		for g in $games; do
			for v in $vs; do	# a game without the variant runs base once per round instead
				[ "$v" = base ] || grep -q "^variant_$v()" "$(bench_recipe "$g")" || v=base
				[ "$v" = base ] && [[ " ${done_base:-} " == *" $g.$i "* ]] && continue
				[ "$v" = base ] && done_base="${done_base:-} $g.$i"
				for e in ${ENVS:--}; do one "$g-$v" "$g" "$v" "$e" || break 4; done
			done
		done
	done
	report
	;;
session) shift; exec "$here/session.sh" "$@" ;;
list)
	for g in $(bench_ids); do
		r=$(bench_recipe "$g")
		printf '%-14s %s variants: base %s\n' "$g" "$(grep -q '^SUITE=1' "$r" && echo suite || echo '     ')" "$(variants "$g")"
	done
	;;
*) sed -n '2,/^[^#]/s/^# \{0,1\}//p' "$0"; exit 2 ;;
esac
exit
}
