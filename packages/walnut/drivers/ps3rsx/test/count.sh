#!/bin/bash
# Dynamic ppc64 instruction counts per payload word of the shadow replay, and per restore, both
# paths: qemu with one instruction per TB, its exec log filtered to shadow.c's code (-O2
# -mcpu=cell; the kernel's own flags differ a little: an estimate, not the PPE's cycles).
set -euo pipefail
cd "$(dirname "$0")"
O=${O:-../../../../../build/tmp/ps3rsx-test}
make -s O="$O" "$O/shadow-equiv-fast" "$O/cnt-ppc64"
# shadow.c's functions (dot symbols, ELFv1), not the ones run once (init) or the test's
R=$(powerpc64-linux-gnu-objdump -d "$O/cnt-ppc64" | awk '/^[0-9a-f]+ <\./ {a=strtonum("0x"$1);
	if (prev != "") printf "%s %x %x\n", prev, pa, a - 1; prev=$2; pa=a}' |
	grep -E '<\.(ps3rsx_|upload|burst|set_bits|state3d|restore|obj_index|write|find_next_bit)' |
	grep -v -E '_(init|new|free|clear|show)>' |
	awk '{printf "%s0x%s..0x%s", (NR > 1 ? "," : ""), $2, $3}')
insns() { qemu-ppc64-static -one-insn-per-tb -d exec,nochain -dfilter "$R" -D "$O/cnt.log" \
	"$O/cnt-ppc64" "$@" > /dev/null; grep -c '^Trace' "$O/cnt.log"; }
names=("3D state runs of 1-6" "2D runs of 8" "VP: pointer, 32 words" "CST: pointer + 16")
for kind in 0 1 2 3; do
	p=$("$O/shadow-equiv-fast" count 0 $kind 1 | awk '{print $3}')
	for f in 0 1; do
		echo "${names[$kind]} shadow_fast=$f: $(echo "scale=1; $(insns count $f $kind 20) / ($p * 20)" | bc) insns/word"
	done
done
for f in 0 1; do
	a=$(insns rcount $f 1) b=$(insns rcount $f 11)
	echo "restore shadow_fast=$f: $(( (b - a) / 10 )) insns ($("$O/shadow-equiv-fast" rcount $f 1))"
done
rm -f "$O/cnt.log"
