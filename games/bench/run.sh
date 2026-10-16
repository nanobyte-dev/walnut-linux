#!/bin/bash
# run.sh: the game suite runner (docs/GAME-PLAN.md), on the PS3 as tibi over SSH. Every run goes
# through `ps3-bench.sh x` (desktop stopped, swaps emptied, samples, thermal watchdog) with
# client.sh and a recipe (recipes/<game>.sh). Copy this directory with `make bench-push`.
#   run.sh smoke GAME [VARIANT]        a first launch: VRAM swap off (NOVSWAP=0 keeps it), 180 s
#                                      at most; prints the result, kernel and game errors to check
#   run.sh bench GAME "VARIANTS" [N]   N rounds (default 3), the variants interleaved in each round
#   run.sh suite "VARIANTS" [N]        the same over every recipe with SUITE=1, games interleaved
#                                      (a game without a variant runs base once per round)
#   run.sh list                        recipes and their variants
# Variant `base` is the recipe as written. End the session with `ps3-bench.sh desktop`.
# Results: /var/tmp/ps3-bench/<stamp>-<game>-<variant>/; report.py summarises them.
{
set -u
here=$(cd "$(dirname "$0")" && pwd)
B=ps3-bench.sh
LOG=/dev/shm/run.sh.$$
RUNS=()

one() {	# one LABEL GAME VARIANT: one run, its directory appended to RUNS
	local rc d
	$B x "$1" -- "$here/client.sh" "$2" "$3" 2>&1 | tee "$LOG"
	rc=${PIPESTATUS[0]}
	d=$(sed -n 's/^>> \(\/var\/tmp\/ps3-bench\/[^ ]*\)$/\1/p' "$LOG" | tail -1)
	rm -f "$LOG"
	[ -n "$d" ] && RUNS+=("$d") && versions "$2" "$3" > "$d/versions"
	[ "$rc" = 0 ] || { echo "!! ps3-bench.sh failed ($rc): stopping"; return 1; }
	[ -e "$d/THERMAL" ] && { echo "!! thermal alert: stopping"; return 1; }
	return 0
}

versions() {	# versions GAME VARIANT: the game's package and the graphics stack (outside the timed client)
	local p
	p=$( (CMD=(); ASSETS=; . "$here/recipes/$1.sh"; declare -F "variant_$2" >/dev/null && "variant_$2"
		readlink -f "$(command -v "${CMD[0]}")") 2>/dev/null)
	dpkg-query -W $(dpkg -S "$p" 2>/dev/null | cut -d: -f1 | tr , ' ') mesa-libgallium libspeq0 ps3-gpu-tools 2>/dev/null
	uname -r
}
variants() { grep -o '^variant_[a-z0-9_]*' "$here/recipes/$1.sh" | sed 's/^variant_//' | tr '\n' ' '; }
report() { [ ${#RUNS[@]} -gt 0 ] && python3 "$here/report.py" "${RUNS[@]}"; }

case ${1:-} in
smoke)
	g=${2:?game}; v=${3:-base}
	BENCH_TIMEOUT=${BENCH_TIMEOUT:-180} BENCH_NOVSWAP=${NOVSWAP:-1} one "smoke-$g-$v" "$g" "$v"
	d=${RUNS[0]:-}; [ -n "$d" ] || exit 1
	echo "== $g $v"; cat "$d/bench.txt" 2>/dev/null || echo "(no bench.txt: the client didn't finish)"
	echo "== kernel, during the run"
	grep -aiE 'ps3rsx|nouveau|rsx|exception|error|fault|oom|killed|thermal|segfault' "$d/dmesg.log" | tail -20
	echo "== game output: lines worth a look"
	grep -aiE 'error|fail|unsupported|not supported|missing|shader|link|segmentation|abort|assert|nouveau|out of temps' \
		"$d/game.log" 2>/dev/null | head -30
	report
	echo "Check on the TV: picture and colours. Sound card state at ${SOUND_AT:-15} s is in bench.txt."
	;;
bench)
	g=${2:?game}; vs=${3:-base}; n=${4:-3}
	for i in $(seq "$n"); do
		for v in $vs; do one "$g-$v" "$g" "$v" || break 2; done
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
				one "$g-$v" "$g" "$v" || break 3
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
