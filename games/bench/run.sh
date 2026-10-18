#!/bin/bash
# run.sh: the game suite runner (docs/GAME-PLAN.md), on the PS3 as tibi over SSH. Every run goes
# through `ps3-bench.sh x` (desktop stopped, swaps emptied, samples, thermal watchdog) with
# client.sh and a recipe (recipes/<game>.sh). Copy this directory with `make bench-push`.
#   run.sh smoke GAME [VARIANT]        a first launch: VRAM swap off (NOVSWAP=0 keeps it), 360 s
#                                      at most (warm-up and window included); prints the result,
#                                      kernel and game errors to check
#   run.sh bench GAME "VARIANTS" [N]   N rounds (default 3), the variants interleaved in each round
#   run.sh suite "VARIANTS" [N]        the same over every recipe with SUITE=1, games interleaved
#                                      (a game without a variant runs base once per round)
#   run.sh list                        recipes and their variants
# Variant `base` is the recipe as written. End the session with `ps3-bench.sh desktop`.
# STALLS=1: the stall recorder runs alongside (ps3-bench.sh BENCH_STALLS=1, docs/STALLS.md); smoke
# then prints stalls.py's tables, else `python3 stalls.py <run dir>...` does.
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
# e.g. ENVS="- NV30_TRANSLATE_LUT=1 NV30_FENCE_BO_POOL=16" (docs/HOTPATHS.md).
# Results: /var/tmp/ps3-bench/<stamp>-<game>-<variant>/; report.py summarises them.
{
set -u
here=$(cd "$(dirname "$0")" && pwd)
B=ps3-bench.sh
LOG=/dev/shm/run.sh.$$
[ -n "${STALLS:-}" ] && export BENCH_STALLS=$STALLS
[ -n "${PERF:-}" ] && export BENCH_PERF=$PERF
[ -n "${PERF_HZ:-}" ] && export BENCH_PERF_HZ=$PERF_HZ
[ -n "${PERF_ALL:-}" ] && export BENCH_PERF_ALL=$PERF_ALL
[ -n "${MEM:-}" ] && export BENCH_MEM=$MEM
for v in STALLS_MS STALLS_CPU STALLS_NOWRITE WAKER WAKER_MODE WAKER_CPU PROBE PROBE_MS PROBE_CPU MALLOC MALLOC_TRIM_S; do
	[ -n "${!v:-}" ] && export "BENCH_$v=${!v}"
done
RUNS=()

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
	env "${ev[@]}" $B x "$1" -- "$here/client.sh" "$2" "$3" 2>&1 | tee "$LOG"
	rc=${PIPESTATUS[0]}
	d=$(sed -n 's/^>> \(\/var\/tmp\/ps3-bench\/[^ ]*\)$/\1/p' "$LOG" | tail -1)
	rm -f "$LOG"
	[ -n "$d" ] && RUNS+=("$d") && versions "$2" "$3" > "$d/versions"
	# the profile's text, between runs (the next run's quiet drops the caches and swaps again)
	[ -s "$d/perf.data" ] && "$here/perf-post.sh" "$d"
	[ "$rc" = 0 ] || { echo "!! ps3-bench.sh failed ($rc): stopping"; return 1; }
	[ -e "$d/THERMAL" ] && { echo "!! thermal alert: stopping"; return 1; }
	return 0
}

versions() {	# versions GAME VARIANT: the game's package and the graphics stack (outside the timed client)
	local p
	p=$( (CMD=(); ASSETS=; . "$here/recipes/$1.sh"; declare -F "variant_$2" >/dev/null && "variant_$2"
		readlink -f "$(command -v "${CMD[0]}")") 2>/dev/null)
	dpkg-query -W $(dpkg -S "$p" 2>/dev/null | cut -d: -f1 | tr , ' ') mesa-libgallium ps3rsx-modules-ps3 libspeq0 libfluidsynth3 libopenal1 libsdl3-0 ps3-gpu-tools 2>/dev/null
	uname -r
}
variants() { grep -o '^variant_[a-z0-9_]*' "$here/recipes/$1.sh" | sed 's/^variant_//' | tr '\n' ' '; }
report() { [ ${#RUNS[@]} -gt 0 ] && python3 "$here/report.py" "$@" "${RUNS[@]}"; }

case ${1:-} in
smoke)
	g=${2:?game}; v=${3:-base}
	BENCH_TIMEOUT=${BENCH_TIMEOUT:-360} BENCH_NOVSWAP=${NOVSWAP:-1} one "smoke-$g-$v" "$g" "$v"
	d=${RUNS[0]:-}; [ -n "$d" ] || exit 1
	echo "== $g $v"; cat "$d/bench.txt" 2>/dev/null || echo "(no bench.txt: the client didn't finish)"
	echo "== kernel, during the run"
	grep -aiE 'ps3rsx|nouveau|rsx|exception|error|fault|oom|killed|thermal|segfault' "$d/dmesg.log" | tail -20
	echo "== game output: lines worth a look"
	grep -aiE 'error|fail|unsupported|not supported|missing|shader|link|segmentation|abort|assert|nouveau|out of temps' \
		"$d/game.log" 2>/dev/null | head -30
	report --stalls --perf --mem
	echo "Check on the TV: picture and colours. Sound card state at ${SOUND_AT:-15} s is in bench.txt."
	;;
bench)
	g=${2:?game}; vs=${3:-base}; n=${4:-3}
	for i in $(seq "$n"); do
		for v in $vs; do
			for e in ${ENVS:--}; do one "$g-$v" "$g" "$v" "$e" || break 3; done
		done
	done
	report
	;;
suite)
	vs=${2:-base}; n=${3:-3}
	games=$(grep -l '^SUITE=1' "$here"/recipes/*.sh | xargs -n1 basename | sed 's/\.sh$//')
	for i in $(seq "$n"); do
		for g in $games; do
			for v in $vs; do	# a game without the variant runs base once per round instead
				[ "$v" = base ] || grep -q "^variant_$v()" "$here/recipes/$g.sh" || v=base
				[ "$v" = base ] && [[ " ${done_base:-} " == *" $g.$i "* ]] && continue
				[ "$v" = base ] && done_base="${done_base:-} $g.$i"
				for e in ${ENVS:--}; do one "$g-$v" "$g" "$v" "$e" || break 4; done
			done
		done
	done
	report
	;;
list)
	for r in "$here"/recipes/*.sh; do
		g=$(basename "$r" .sh)
		printf '%-14s %s variants: base %s\n' "$g" "$(grep -q '^SUITE=1' "$r" && echo suite || echo '     ')" "$(variants "$g")"
	done
	;;
*) sed -n '2,/^[^#]/s/^# \{0,1\}//p' "$0"; exit 2 ;;
esac
exit
}
