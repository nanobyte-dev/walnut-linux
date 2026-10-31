#!/bin/bash
# rsx-ladder.sh [DIR]: the PS3 half of the fix ladder (docs/VALIDATION.md, tools/ladder.py). DIR (default
# /var/tmp/ladder) holds what make ladder-ps3-libs wrote to build/testsets/ladder/ on the PC, copied as is:
# manifest.txt and rungNN/{lib,dri,BUILD}, one Mesa build per rung. For each rung the GL tests run with
# LD_LIBRARY_PATH and LIBGL_DRIVERS_PATH on that rung's libraries: no reinstall, no reboot. Run as the
# desktop user (sudo for dmesg), with ps3rsx loaded and the user not playing; nothing else on the PS3
# meanwhile.
#   Each test's result (pass: exit 0 and the renderer is the RSX, not a softpipe fallback), and the
#   kernel lines it caused (dmesg between runs: "ps3rsx: rejected", graph exceptions, other ps3rsx and
#   nouveau lines) go to DIR/results-<time>/: results.tsv as they come (synced: a wedge keeps the
#   record), logs/, and at the end matrix.txt and summary.txt.
#   Expectations: manifest.txt's "test NAME FIXRUNG" lines (the register's ps3: tests); a test of the
#   table below that the manifest doesn't name guards no stability fix and should pass at every rung.
#   Marked as surprises like the host ladder: too early (passes below its fix), after fix (fails at or
#   above it without having passed), regression (fails after passing at the rung before).
#   A test marked "wedge" (its old failure hung the RSX) runs only at two rungs: its fix's own rung, in
#   the normal order, and the rung below it, after everything else, as the last runs of the session
#   (sync first). WEDGE=0 leaves those last runs out.
#   After every rung "rsxdrm-smoke nop" must pass; if it doesn't, the GPU is gone and the session stops.
# Env: RUNGS="0 8" (default every rung of the manifest), WEDGE=0|1 (1), T=seconds per test (120).
set -u
D=${1:-/var/tmp/ladder}
M=$D/manifest.txt
[ -e "$M" ] || { echo "usage: $0 [DIR]: no $M (make ladder-ps3-libs, then copy build/testsets/ladder to DIR)" >&2; exit 2; }
T=${T:-120}
WEDGE=${WEDGE:-1}
O=$D/results-$(date +%Y%m%d-%H%M%S)
mkdir -p "$O/logs" || exit 1
B=/usr/lib/ps3/bin
PATH=$B:$PATH
P=/tmp/rsx-ladder.ppm
grep -q '^ps3rsx ' /proc/modules || { echo "!! ps3rsx isn't loaded"; exit 1; }
DM=(dmesg); dmesg >/dev/null 2>&1 || DM=(sudo -n dmesg)
"${DM[@]}" >/dev/null 2>&1 || { echo "!! cannot read dmesg (sudo -n dmesg)"; exit 1; }

# name|command|flags (wedge: see above; once: no Mesa in it, rung 0 only; desc in the rows' order)
TESTS='egl-clear|egl-clear P|
egl-tri|egl-tri P|
egl-tex|egl-tex P|
egl-gears|egl-gears P|
egl-short|egl-short P|
gl-texfmt|gl-texfmt|
gl-texsub|gl-texsub|
gl-fog|gl-fog|
gl-sky|gl-sky|
gl-pointsmooth|gl-pointsmooth|
gl-dxt|gl-dxt|
gl-genmip|gl-genmip|wedge
gl-mipread|gl-mipread|wedge
egl-thin|egl-thin 1 64|wedge
rsxdrm-smoke_quirks|rsxdrm-smoke quirks|once'
names=() cmds=() flags=()
while IFS='|' read -r n c f; do names+=("$n"); cmds+=("${c// P/ $P}"); flags+=("$f"); done <<<"$TESTS"
declare -A fix=()
while read -r kw n r _; do [ "$kw" = test ] && fix[$n]=$r; done < <(tr '\t' ' ' < "$M")
for n in "${!fix[@]}"; do
	[[ " ${names[*]} " == *" $n "* ]] || echo "note: the manifest's test $n has no command here (left out)"
done
all=$(sed -n 's/^rung \([0-9]*\) .*/\1/p' "$M")
rungs=${RUNGS:-$all}
echo "rung	test	result	rejected	exceptions	kernel_lines" > "$O/results.tsv"
{ echo "rsx-ladder $(date -Is) on $(uname -r)"; grep -E '^(key|rung|fused) ' "$M"; } > "$O/header.txt"
sync

libs_ok() {	# libs_ok RUNG_DIR: the rung's libgallium is the one the GL stack loads
	local r=$1 got
	got=$(LD_LIBRARY_PATH=$r/lib LIBGL_DRIVERS_PATH=$r/dri GBM_BACKENDS_PATH=$r/lib/gbm LD_DEBUG=libs \
		timeout 60 egl-clear $P 2>&1 | grep -m1 -o 'calling init: [^ ]*libgallium[^ ]*' )
	[[ "$got" == *"$r/lib/"* ]] || { echo "!! $r: the GL stack loaded ${got:-no libgallium} instead"; return 1; }
}
one() {	# one RUNG INDEX: run test INDEX against rung RUNG, record it
	local rung=$1 i=$2 n r rc before out res rej exc kl
	n=${names[$i]}
	r=$D/rung$(printf %02d "$rung")
	before=$("${DM[@]}" | wc -l)
	echo "--- rung $rung: $n (${cmds[$i]})"
	out=$(LD_LIBRARY_PATH=$r/lib LIBGL_DRIVERS_PATH=$r/dri GBM_BACKENDS_PATH=$r/lib/gbm timeout "$T" ${cmds[$i]} 2>&1); rc=$?
	printf '%s\n' "$out" > "$O/logs/rung$rung-$n.log"
	"${DM[@]}" | tail -n +$((before + 1)) | grep -E 'ps3rsx|nouveau|graph exception|RSX' > "$O/logs/rung$rung-$n.dmesg"
	rej=$(grep -c 'rejected' "$O/logs/rung$rung-$n.dmesg"); exc=$(grep -ci 'graph exception' "$O/logs/rung$rung-$n.dmesg")
	kl=$(wc -l < "$O/logs/rung$rung-$n.dmesg")
	res=fail
	if [ $rc = 0 ]; then
		if [[ $n == rsxdrm-* ]] || grep -q 'RENDERER: .*NV' <<<"$out"; then res=pass
		else echo "    not on the RSX (softpipe fallback?)"; fi
	elif [ $rc = 124 ]; then res=timeout; fi
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$rung" "$n" "$res" "$rej" "$exc" "$kl" >> "$O/results.tsv"
	sync
	echo "    $res (exit $rc; dmesg: $rej rejected, $exc graph exceptions, $kl lines)"
}
health() {	# the GPU still answers
	rsxdrm-smoke nop > "$O/logs/health-$1.log" 2>&1 && return 0
	echo "!! rsxdrm-smoke nop failed after rung $1: the GPU is gone, stopping (see $O/logs)"; return 1
}

deferred=()
stop=0
for rung in $rungs; do
	r=$D/rung$(printf %02d "$rung")
	[ -d "$r/lib" ] || { echo "!! no $r/lib: rung $rung left out"; continue; }
	echo "=== rung $rung: $(sed -n 's/^rung '"$rung"' [^ ]* //p' "$M")"
	libs_ok "$r" || continue
	for i in "${!names[@]}"; do
		n=${names[$i]} f=${flags[$i]} fr=${fix[${names[$i]}]:-0}
		case $f in
		once)	[ "$rung" = "$(head -n 1 <<<"$rungs")" ] || continue ;;
		wedge)	if [ "$rung" = $((fr - 1)) ]; then deferred+=("$rung $i"); continue; fi
			[ "$rung" = "$fr" ] || continue ;;
		esac
		one "$rung" "$i"
	done
	health "$rung" || { stop=1; break; }
done
if [ $stop = 0 ] && [ "$WEDGE" = 1 ] && [ ${#deferred[@]} -gt 0 ]; then
	echo "=== the wedging repros, below their fixes (last: they may hang the RSX)"
	for d in "${deferred[@]}"; do
		set -- $d
		sync
		one "$1" "$2"
		health "$1" || break
	done
fi

# the matrix and the surprises, from results.tsv
cols=$(tr ' ' '\n' <<<"$rungs" | tr '\n' ' ')
for i in "${!names[@]}"; do printf '%s\t%s\n' "${names[$i]}" "${fix[${names[$i]}]:-0}"; done > "$O/tests.tsv"
awk -F'\t' -v cols="$cols" -v M="$O/matrix.txt" -v S="$O/surprises.txt" '
	FNR == NR { order[++nt] = $1; fr[$1] = $2; next }
	FNR == 1 { next }
	{ res[$2, $1] = $3; rej[$2, $1] += $4; exc[$2, $1] += $5 }
	END {
		nc = split(cols, c, " ")
		printf "%-22s %4s ", "test", "fix" > M
		for (j = 1; j <= nc; j++) printf "%5s", c[j] > M
		printf "\n" > M
		for (k = 1; k <= nt; k++) {
			t = order[k]; printf "%-22s %4s ", t, fr[t] > M
			passed = 0; prev = ""; after = 0; early = ""
			for (j = 1; j <= nc; j++) {
				r = c[j]; v = res[t, r]
				if (v == "") { printf "%5s", "-" > M; continue }
				ok = (v == "pass"); ex = (r + 0 >= fr[t] + 0)
				m = ok ? (ex ? "P" : "!P") : (ex ? "!F" : "x")
				if (rej[t, r] + exc[t, r]) m = m "*"
				printf "%5s", m > M
				if (!ex) { if (ok) early = early " " r; continue }
				if (!ok && passed && prev == "pass") print t ": regression: " v " at rung " r " after passing below it: caused by rung " r > S
				else if (!ok && !passed && !after) { after = 1; print t ": after fix: " v " at rung " r ", its fix rung is " fr[t] > S }
				if (ok) passed = 1; prev = v
			}
			printf "\n" > M
			if (early != "") print t ": too early: passes at rung" early ", below its fix rung " fr[t] ": the repro does not reproduce" > S
		}
		print "(P pass, x fails below its fix as expected, !P too-early pass, !F fails at or above its fix, - not run; * kernel lines: rejected or graph exception, see logs/)" > M
	}' "$O/tests.tsv" "$O/results.tsv"
touch "$O/surprises.txt"
{
	cat "$O/header.txt"
	echo
	cat "$O/matrix.txt"
	echo
	echo "surprises: $(wc -l < "$O/surprises.txt")"; sed 's/^/  /' "$O/surprises.txt"
	echo
	echo "kernel lines per rung (rejected / graph exceptions):"
	awk -F'\t' 'NR > 1 { r[$1] += $4; e[$1] += $5 } END { for (k in r) print "  rung " k ": " r[k] " / " e[k] }' "$O/results.tsv" | sort -k2n
	[ ${#deferred[@]} = 0 ] || echo "wedging repros below their fixes: ${#deferred[@]} ($( [ "$WEDGE" = 1 ] && echo run last || echo left out: WEDGE=0))"
	[ $stop = 0 ] || echo "STOPPED: the GPU stopped answering"
} > "$O/summary.txt"
sync
cat "$O/summary.txt"
echo "results: $O"
if [ -s "$O/surprises.txt" ] || [ $stop = 1 ]; then exit 1; fi
exit 0
