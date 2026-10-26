#!/bin/bash
# perf-post.sh RUN_DIR...: the text out of a PERF=1 run's perf.data (docs/PROFILING.md), on the PS3
# as tibi after the run (run.sh does it when PERF=1; by hand for older runs). perf runs as root
# (sudo -n: kernel and ps3rsx symbols from /proc/kallsyms; -f: the file is ours), at nice 19.
# Writes into each run directory:
#   perf-report.txt    perf report --no-children --sort dso,sym of the game process (all its threads)
#   perf-threads.txt   samples by process, thread and DSO, the game, Xorg and kswapd0
#   perf.folded        folded stacks (perfprof.py collapse) for flamegraph.pl / inferno / speedscope
#   perf-leaf.txt      samples by process, DSO and symbol (report.py's perf_* columns)
#   perf-pkgs.txt      the package of every DSO with samples (dpkg -S): which libraries are the game's
#   perf-buildids.txt  the build ids of the DSOs with samples (to match symbols on the PC)
#   perf-irqoff.txt    the soft-masked time (samples booked to __replay_soft_interrupts) by where it
#                      ended, and the window's interrupts and htab hcalls a second (irqoff.py)
#   perf-sys-report.txt, perf-sys-leaf.txt   the same from perf-sys.data (PERF_ALL=1), by comm and DSO
# PERF_SYMS=1 also writes perf-syms.tar (the DSOs with samples and their debug files from
# /usr/lib/debug, the ps3rsx module, laid out as a root for perf --symfs) and perf-kallsyms (the
# kernel's symbols), so perf report and perf script work on the PC (docs/PROFILING.md).
# A PERF_LOAD=1 run's perf-load.data (the start-up: exec, loading screens, warm-up) the same way,
# into perf-load-report.txt, perf-load-threads.txt, perf-load.folded (the phase, load or warmup, as
# the outermost frame), perf-load-leaf.txt, perf-load-stacks.tsv (perfprof.py collapse --load) and
# loadprof.py's tables; its build ids and packages go into the same perf-buildids.txt/perf-pkgs.txt
# (merged when both records exist).
# perf script runs with --no-inline: inlined frames come from addr2line, which perf 7.2 on the PS3
# (no libbfd, no LLVM) runs as the binutils command; the image has no binutils, so perf died of
# SIGPIPE writing to the missing command after the first few samples ("addr2line configuration
# failed", exit 141: the 2026-10-18 batch). Frames perf left [unknown] in the game (libraries mapped
# while perf was disabled: ps3-gpu-tools before 0.412) are filled from the MEM=1 smaps by perfprof.py.
# The check at the end compares the collapsed samples with perf report's count.
# PERFPOST_PERF: the perf command (default sudo -n nice -n 19 perf; for tests, e.g. qemu with a root).
{
set -u
BENCH=$(cd "$(dirname "$0")/.." && pwd)	# the bench tree (this is bench/harness/; analysis/ beside it)
P=(sudo -n nice -n 19 perf); [ "$(id -u)" = 0 ] && P=(nice -n 19 perf)
if [ -n "${PERFPOST_PERF:-}" ]; then read -r -a P <<< "$PERFPOST_PERF"
else command -v perf > /dev/null || { echo "!! no perf (linux-perf)"; exit 1; }; fi
for d in "$@"; do
	[ -s "$d/perf.data" ] || [ -s "$d/perf-load.data" ] || { echo "!! $d: no perf.data or perf-load.data"; continue; }
	t0=$EPOCHREALTIME
	: > "$d/perf-post.err"
	for pre in perf perf-load; do
	f=$d/$pre.data
	[ -s "$f" ] || continue
	game=$(sed -n 's/.*game=\([0-9]*\).*/\1/p' "$d/$pre.targets" 2>/dev/null)
	# the start-up record attached to the gate: the game's own process is client.sh's game_pid
	[ "$pre" = perf-load ] && { g=$(sed -n 's/^game_pid=\([0-9]*\)$/\1/p' "$d/bench.txt" 2>/dev/null); [ -n "$g" ] && game=$g; }
	echo ">> $d: $pre.data $(du -k "$f" | cut -f1) KiB, game pid ${game:-?}"
	# percentages of the game's samples (--pid keeps its threads, not Xorg's or kswapd0's)
	"${P[@]}" report -f -i "$f" --stdio --no-children --sort dso,sym -g none --percentage relative \
		${game:+--pid "$game"} > "$d/$pre-report.txt" 2>> "$d/perf-post.err"
	"${P[@]}" report -f -i "$f" --stdio --no-children --sort pid,comm,dso -g none --percentage absolute -n \
		> "$d/$pre-threads.txt" 2>> "$d/perf-post.err"
	mode=; [ "$pre" = perf-load ] && mode=--load
	"${P[@]}" script --no-inline -f -i "$f" -F comm,pid,tid,time,period,ip,sym,dso 2>> "$d/perf-post.err" |
		python3 "$BENCH/analysis/perfprof.py" collapse $mode "$d"
	rc=${PIPESTATUS[0]}
	# every sample collapsed? (perf report's count: perf-threads.txt's Samples column)
	nrep=$(awk '$1 ~ /%$/ && $2 ~ /^[0-9]+$/ { s += $2 } END { print s + 0 }' "$d/$pre-threads.txt")
	ncol=$(sed -n 's/^# samples=\([0-9]*\).*/\1/p' "$d/$pre-leaf.txt" 2>/dev/null)
	if [ "$rc" != 0 ] || [ "${ncol:-0}" -lt $(( nrep * 99 / 100 )) ]; then
		echo "!! perf script exit $rc, ${ncol:-0} of $nrep samples collapsed: $pre.folded and $pre-leaf.txt are incomplete (perf-post.err)" |
			tee -a "$d/perf-post.err"
	fi
	done
	f=$d/perf.data; [ -s "$f" ] || f=$d/perf-load.data
	if [ -s "$d/perf-sys.data" ]; then
		"${P[@]}" report -f -i "$d/perf-sys.data" --stdio --no-children --sort comm,dso -g none \
			--percentage absolute > "$d/perf-sys-report.txt" 2>> "$d/perf-post.err"
		"${P[@]}" script --no-inline -f -i "$d/perf-sys.data" -F comm,pid,tid,time,period,ip,sym,dso 2>> "$d/perf-post.err" |
			python3 "$BENCH/analysis/perfprof.py" collapse --sys "$d"
	fi
	for x in perf perf-load; do
		[ -s "$d/$x.data" ] && "${P[@]}" buildid-list -f -i "$d/$x.data" --with-hits 2>> "$d/perf-post.err"
	done | sort -u > "$d/perf-buildids.txt"
	# the package of each DSO with samples (absolute paths only; dpkg -S takes them all at once)
	cat "$d/perf-leaf.txt" "$d/perf-load-leaf.txt" 2>/dev/null | cut -f5 | grep '^/' | sort -u > "$d/.dsos"
	exe=$(cat "$d/perf.exe" 2>/dev/null); [ -n "$exe" ] && echo "$exe" >> "$d/.dsos"
	exe=$(cat "$d/perf-load.exe" 2>/dev/null); [ -n "$exe" ] && echo "$exe" >> "$d/.dsos"
	sort -u "$d/.dsos" | xargs -r dpkg -S 2>/dev/null | awk -F': ' '{ n = split($1, p, ", "); print $2 "\t" p[1] }' \
		> "$d/perf-pkgs.txt"
	if [ "${PERF_SYMS:-0}" = 1 ]; then
		sudo -n cat /proc/kallsyms > "$d/perf-kallsyms"
		{ awk '$2 ~ /^\// { print $2 }' "$d/perf-buildids.txt"
		  awk 'length($1) > 2 { print "/usr/lib/debug/.build-id/" substr($1, 1, 2) "/" substr($1, 3) ".debug" }' \
			"$d/perf-buildids.txt"
		  m=$(modinfo -n ps3rsx 2>/dev/null) && echo "$m"; } | while read -r p; do [ -e "$p" ] && echo "${p#/}"; done |
			sort -u > "$d/.syms"
		tar -C / -chf "$d/perf-syms.tar" -T "$d/.syms"
		echo "   perf-syms.tar: $(wc -l < "$d/.syms") files, $(du -k "$d/perf-syms.tar" | cut -f1) KiB"
		rm -f "$d/.syms"
	fi
	rm -f "$d/.dsos"
	[ -s "$d/perf-post.err" ] && grep -v -E '^\s*$' "$d/perf-post.err" | head -5 | sed 's/^/   perf: /'
	[ -s "$d/perf-leaf.txt" ] && python3 "$BENCH/analysis/perfprof.py" "$d"
	[ -s "$d/perf.folded" ] && python3 "$BENCH/analysis/irqoff.py" "$d" > "$d/perf-irqoff.txt" 2>> "$d/perf-post.err"
	[ -s "$d/perf-load-stacks.tsv" ] && python3 "$BENCH/analysis/loadprof.py" "$d"
	awk -v a="$t0" -v b="$EPOCHREALTIME" 'BEGIN { printf "   (post-processing took %.0f s)\n", b - a }'
done
exit
}
