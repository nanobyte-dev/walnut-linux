#!/bin/bash
# The push dump's host side end to end (pushdump-test.c writes a sample dump with the kernel's
# record builders; pushdump-split.py splits it; validate-capture checks the parts):
#   pushdump-test.sh O		O: the test build directory (the Makefile's), with pushdump-test
#				and validate-capture built
set -euo pipefail
O=$1
T=$O/pushdump
here=$(cd "$(dirname "$0")" && pwd)
rm -rf "$T"
mkdir -p "$T"
"$O/pushdump-test" "$T/sample.bin"
python3 -I "$here/pushdump-split.py" "$T/sample.bin" "$T/split"
S=$T/split
fail() { echo "FAIL pushdump-test.sh: $*"; exit 1; }
for f in chan1.push chan2.push chan1.gpu submits.tsv bos.tsv set.tsv contents.tsv summary.txt; do
	[ -s "$S/$f" ] || fail "no $f"
done
[ "$(wc -l <"$S/submits.tsv")" = 2 ] || fail "submits.tsv: $(wc -l <"$S/submits.tsv") lines"
[ "$(wc -l <"$S/bos.tsv")" = 4 ] || fail "bos.tsv"
[ "$(wc -l <"$S/set.tsv")" = 3 ] || fail "set.tsv"
grep -q $'^0x2\t2\t102\t5\t4321\tsample\trejected\t0\t1\t2$' "$S/submits.tsv" || fail "the rejected submit's line"
grep -q $'^close\t0x4\t5\t4321\tgart\tgart\t0x200000\t0x1000$' "$S/set.tsv" || fail "the close event"
grep -q $'\tfp\tgart\t0x100000\t0x1000\tread\t0x6\tcontents/' "$S/contents.tsv" || fail "the program's content line"
grep -q $'\tindex\tgart\t0x200000\t0x16\tunreadable\t0x0\t-$' "$S/contents.tsv" || fail "the index line"
[ "$(head -c 5 "$S"/contents/*-fp-*.bin)" = "PROG!" ] || fail "the program's bytes"
grep -q '^gaps 1, records dropped 7$' "$S/summary.txt" || fail "the gap"
grep -q '^version 1, vram reserved 0x900000' "$S/summary.txt" || fail "the start record"
# channel 1's pushes pass as Mesa's would; channel 2's refused END is refused again
"$O/validate-capture" "$S/chan1.push" >"$T/v1.log" || { cat "$T/v1.log"; fail "chan1.push refused"; }
if "$O/validate-capture" "$S/chan2.push" >"$T/v2.log"; then fail "chan2.push passed"; fi
grep -q 'END with no primitive open' "$T/v2.log" || fail "chan2's reason"
# what reached the GPU from channel 1, ps3rsx's END included, is one valid stream with one draw
"$O/validate-capture" --gpu "$S/chan1.gpu" >"$T/g1.log" || { cat "$T/g1.log"; fail "chan1.gpu refused"; }
grep -q '1 draws: every word passes' "$T/g1.log" || fail "chan1.gpu: $(cat "$T/g1.log")"
echo "pushdump-test.sh: ok"
