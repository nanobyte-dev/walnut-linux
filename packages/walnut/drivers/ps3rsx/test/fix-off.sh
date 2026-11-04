#!/bin/bash
# validate-test with one stability rule turned off at a time: each build must fail, with the
# FAIL line given for it, so the case shows the old behaviour. The rule is turned off in a copy
# of src/validate.c (or the source -E names, with the define its test includes it by) under
# OUTDIR/fix-off (a literal edit, which must match exactly once), or for the submit.c model in
# validate-test.c (or the test -T names) by a define (OLD_*). Nothing here touches src/.
#   fix-off.sh OUTDIR		(make fix-off; exits 0 only when every variant fails as expected)
set -u
[ $# -eq 1 ] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
mkdir -p "$1" && O=$(cd "$1" && pwd) || exit 2
cd "$(dirname "$0")" || exit 2
D=$O/fix-off
mkdir -p "$D"
rm -f "$D"/*.built
JOBS=${JOBS:-6}
names=() expects=()

# off NAME 'FAIL line it must print' [-T TEST.c] [-E SRC MACRO] [-DDEFINE | 'FROM' 'TO']...
off() {
	local name=$1 expect=$2 src=validate.c macro=VALIDATE_C vc=../src/validate.c defs=()
	local t=validate-test.c
	shift 2
	while [ $# -gt 0 ]; do
		case $1 in
		-T)	t=$2; shift 2 ;;
		-E)	src=$2 macro=$3 vc=../src/$2; shift 3 ;;
		-D*)	defs+=("$1"); shift ;;
		*)	if [ "$vc" = "../src/$src" ]; then
				cp "../src/$src" "$D/$name.${src##*.}"; vc=$D/$name.${src##*.}
			fi
			FROM=$1 TO=$2 NAME=$name SRC=$src perl -0777 -i -pe \
				'my $n = () = /\Q$ENV{FROM}\E/g;
				 $n == 1 or die "fix-off.sh: $ENV{NAME}: found $n times in $ENV{SRC}: $ENV{FROM}\n";
				 s/\Q$ENV{FROM}\E/$ENV{TO}/' "$vc" || exit 1
			shift 2 ;;
		esac
	done
	while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
	( gcc -O1 -g -w -fno-strict-aliasing -Iinc -iquote ../src -D"$macro"="\"$vc\"" "${defs[@]}" \
		-o "$D/$name" "$t" && touch "$D/$name.built" ) &
	names+=("$name") expects+=("$expect")
}

# the validator (src/validate.c), one rule each: the register entry's or the commit's
off x16 "FAIL: unit 0 format 14: 0, expected refuse" \
	'return ((data >> 8) & 0x1f) != TEXFMT_X16;' 'return true;'
off tex-offset "FAIL: unit 0: texture offset +0xaa0 accepted" \
	'(f == F_TEXOFS && !(data & 127))' '(f == F_TEXOFS)' \
	'tex_offset(mthd) && (data & 127)' 'tex_offset(mthd) && 0'
off thin-swizzled-rt "FAIL: swizzled render target 1 pixel wide accepted" \
	'(data & 0xf00) == 0x200 && !(data & 0x00ff0000)' '0'
off rt-enable "FAIL: RT_ENABLE 00000012: 0, expected refuse" \
	'return !(data & ~0x1fu) && !(targets & (targets + 1))' 'return 1 || !(targets & (targets + 1))'
off vertex-type0 "FAIL: slot 0 data 00000000: 0, expected refuse" \
	'return type != 0 && (type != 7' 'return (type != 7'
off ub256 "FAIL: slot 0 data 00000037: 0, expected refuse" \
	'return type != 0 && (type != 7 || size == 0 || size == 4);' 'return type != 0;'
off vertex-texture "FAIL: vertex unit 0 900 01943aa0 (offset +0xaa0 (Neverball's)): 0, expected refuse" \
	'switch (m & 0x1c) {' 'if (1) return true; switch (m & 0x1c) {'
off allowlist "FAIL: M2MF methods on the surface-2D object accepted" \
	'return test_bit(m / 4, allowed[vclass(handle)]);' 'return true;'
off prim-order "FAIL: a BEGIN in an open primitive accepted" \
	'if ((data != 0) == vs->prim_open) {' 'if (0) {' \
	'prim_method(mthd) && !vs->prim_open' 'prim_method(mthd) && 0'
off header "FAIL: header 20001000 accepted" \
	'if ((h & 0xe0030003) == 0) {' 'if ((h & 0x40000000) == 0) {'
off object-bind "FAIL: bind of unknown object 31337001 accepted" \
	'if (!ps3rsx_known_object(data)) {' 'if (0) {'
off host-method "FAIL: host method 68 accepted (semaphore acquire)" \
	'if (mthd < 0x100) {' 'if (mthd < 0x100) { return 0;'
off host-reference "FAIL: host method 50 accepted (SET_REFERENCE)" \
	'if (mthd < 0x100) {' 'if (mthd < 0x100) { if (mthd == 0x50 || mthd == 0x60 || mthd == 0x64 || mthd == 0x6c) return 0;'
off context-dma "FAIL: 3D context DMA feed0002 accepted" \
	'if (d < 0 || !known_dma(data)) {' 'if (0) {' \
	'(d < 0 || !(slot_allow(vs->cls[subch], mthd) & DM(d)))' '0'
off sifm-surface "FAIL: SIFM surface feed0000 accepted" \
	'if (data != 0x313371c3 && data != 0x31337a73) {' 'if (0) {'
# the address checks (src/validate.c, src/p4.h): each rule off, its p4-test case fails
P4=(-T p4-test.c)
off p4-bind "FAIL: bind of 3137c0de accepted" "${P4[@]}" \
	'if ((data == 0x3137c0de || data == 0x31337808) &&' 'if (0 &&'
off p4-slot "FAIL: slot: class 1 slot 184 handle feed0001 accepted" "${P4[@]}" \
	'(d < 0 || !(slot_allow(vs->cls[subch], mthd) & DM(d)))' '(d < 0)'
off p4-fence "FAIL: fence: channel 1 offset 00000060 accepted" "${P4[@]}" \
	'if (data < lo || (u64)data + 4 > lo + 32 || (data & 3)) {' 'if (data < lo || (data & 3)) {'
off p4-fence-wrap "FAIL: fence: channel 0 offset fffffffc accepted" "${P4[@]}" \
	'(u64)data + 4 > lo + 32' 'data + 4 > lo + 32'
off p4-fence-unset "FAIL: fence: release with no offset accepted" "${P4[@]}" \
	'if (!fvalid(vs, P4F_FENCE) || !fvalid(vs, P4F_3D_DMA + 9)) {' 'if (0) {'
off p4-query "FAIL: query: channel 1 type 0 offset 1000 accepted" "${P4[@]}" \
	'if ((data >> 24) < 1 || (data >> 24) > 5 || !slice) {' 'if (0) {'
off p4-render "FAIL: render enable: channel 1 offset 2000 accepted" "${P4[@]}" \
	'if ((data >> 24) != 2 || !slice) {' 'if (0) {'
off p4-m2mf-src "FAIL: m2mf: source one byte past A accepted" "${P4[@]}" \
	'if (!in_set(vs, vs->f[P4F_M_IN_DMA], vs->f[P4F_M_OFF_IN], in)) {' 'if (0) {'
off p4-m2mf-dst "FAIL: m2mf: destination across A and B accepted" "${P4[@]}" \
	'if (!in_set(vs, vs->f[P4F_M_OUT_DMA], vs->f[P4F_M_OFF_OUT], out)) {' 'if (0) {'
off p4-m2mf-unset "FAIL: m2mf: a launch without its own OFFSET_OUT accepted" "${P4[@]}" \
	$'\t\t*why = "M2MF copy with a field not set by this channel";\n\t\treturn P4R_UNSET;' ''
off p4-m2mf-pitch "FAIL: m2mf: pitch 2^31 accepted" "${P4[@]}" \
	'return v >= 0x80000000u ? "M2MF pitch at or above 2^31" : NULL;' 'return NULL;' \
	'(vs->f[P4F_M_PITCH_IN] | vs->f[P4F_M_PITCH_OUT]) >> 31' '0'
off p4-m2mf-notify "FAIL: m2mf: notify 1 accepted" "${P4[@]}" \
	$'\tif (data) {\n\t\t*why = "M2MF notify other than 0";' $'\tif (0) {\n\t\t*why = "M2MF notify other than 0";'
off p4-sifm-src "FAIL: sifm: a source one texel past G accepted" "${P4[@]}" \
	'!in_set(vs, vs->f[P4F_F_DMA], vs->f[P4F_F_OFF], len)) {' '0) {'
off p4-sifm-span "FAIL: sifm: a step sampling past the source accepted" "${P4[@]}" \
	'cols = max_t(u64, LO16(size), uend) + 1;' 'cols = LO16(size) + 1; (void)uend;'
off p4-sifm-dst "FAIL: sifm: a clip one pixel past A accepted" "${P4[@]}" \
	'xmax = max_t(u64, LO16(opt) + LO16(osz), LO16(cpt) + LO16(csz));' 'xmax = LO16(opt) + LO16(osz);'
off p4-sifm-swz "FAIL: sifm: a rectangle outside its swizzled block accepted" "${P4[@]}" \
	'xmax > (1ULL << lu) || ymax > (1ULL << lv)) {' '0) {'
off p4-sifm-surface "FAIL: sifm: into a surface not set by this channel accepted" "${P4[@]}" \
	$'\t\t\t*why = "scaled image into a surface not set by this channel";\n\t\t\treturn P4R_UNSET;' ''
off p4-coords "FAIL: sifm: an output point y of 0x8000 accepted" "${P4[@]}" \
	$'return LO16(v) > 4096 || HI16(v) > 4096 ? "scaled image point or size out of range" :\n\t\t\tNULL;' 'return NULL;'
off p4-clear-extent "FAIL: clear: one row past A accepted" "${P4[@]}" \
	$'\tif (!in_set(vs, handle, lo, len)) {\n\t\t*why = "render target outside' $'\tif (0) {\n\t\t*why = "render target outside'
off p4-clear-round "FAIL: clear: an offset the hardware rounds down to A refused" "${P4[@]}" \
	'lo = off & ~63u, len, rb;' 'lo = off, len, rb;'
off p4-clear-aa "FAIL: clear: a 4x surface one row past A accepted" "${P4[@]}" \
	'u64 xs = aa ? 2 : 1, ys = aa == 4 || aa == 5 ? 2 : 1,' 'u64 xs = 1, ys = 1,'
off p4-clear-window "FAIL: clear: a window offset past A accepted" "${P4[@]}" \
	'rows = max_t(u64, LO16(v) + HI16(v), HI16(wo) + HI16(vs->f[P4F_WIN_V]) + 1);' 'rows = LO16(v) + HI16(v);'
off p4-clear-coord "FAIL: clear: a coordinate height past A accepted" "${P4[@]}" \
	'rows = max_t(u64, rows, vs->f[P4F_COORD] & 0xfff);' ''
off p4-clear-rt1 "FAIL: clear: target 1 enabled with no offset accepted" "${P4[@]}" \
	'en = (vs->f[P4F_RT_ENABLE] & 0xf) | 1;' 'en = 1;'
off p4-clear-swz "FAIL: clear: a 1024x512 swizzled block past A accepted" "${P4[@]}" \
	'len = (1ULL << lw) * (1ULL << lh) * bpp;' 'len = 1;'
off p4-shift "FAIL: clear: RT_FORMAT log2 13 accepted" "${P4[@]}" \
	$'\t\tif (((v >> 16) & 0xff) > 12 || (v >> 24) > 12)\n\t\t\treturn "swizzled surface log2 size above 12";\n' ''
off p4-clear-unset "FAIL: clear: without its coordinate conventions accepted" "${P4[@]}" \
	$'\t\t*why = "clear with a surface field not set by this channel";\n\t\treturn P4R_UNSET;' ''
off p4-lost "FAIL: m2mf: launch after a lost channel accepted" "${P4[@]}" \
	'memset(vs->valid, 0, sizeof(vs->valid));' ''
off p4-set-find "FAIL: set: range across two BOs found" "${P4[@]}" -E p4.h P4_H \
	'return lo < hi && i >= 0 && hi <= d->e[i].hi;' 'return lo < hi && i >= 0;'
off p4-set-overlap "FAIL: set: overlap at the end" "${P4[@]}" -E p4.h P4_H \
	'if ((i >= 0 && d->e[i].hi > lo) || (i + 1 < (int)d->n && d->e[i + 1].lo < hi))' 'if (0)'
off p4-set-handles "FAIL: set: first of two handles removed the BO" "${P4[@]}" -E p4.h P4_H \
	'		d->e[i].handles++;' ''
off p4-coverage "FAIL: coverage: field 16 (class 1 method 1d6c) not restored" "${P4[@]}" -E shadow.c SHADOW_C \
	'case 0x1d70: case 0x1d74:			/* semaphore releases */' 'case 0x1d6c: case 0x1d70: case 0x1d74:'
off p4-p7 "FAIL: p7: a state write inside a primitive accepted" "${P4[@]}" -DPS3RSX_P4_COUNT \
	$'\t\t\t\tif (vs->prim_open && !p7_ok(vs->cls[subch], m) &&' $'\t\t\t\tif (0 &&'
off p4-count "FAIL: count: slice a enforcing let a wrong slot through" "${P4[@]}" -DPS3RSX_P4_COUNT \
	'if (!(READ_ONCE(test_p4_enforce) & p4_rule_slice[rule])) {' 'if (1) {'
# the property fuzz (p4-fuzz.c, its default seed): its independent model finds the accesses a rule
# off lets through
F=(-T p4-fuzz.c)
off fuzz-m2mf-dst ": M2MF destination" "${F[@]}" \
	'if (!in_set(vs, vs->f[P4F_M_OUT_DMA], vs->f[P4F_M_OFF_OUT], out)) {' 'if (0) {'
off fuzz-sifm-src ": scaled image source" "${F[@]}" \
	'!in_set(vs, vs->f[P4F_F_DMA], vs->f[P4F_F_OFF], len)) {' '0) {'
off fuzz-clear-window ": colour target" "${F[@]}" \
	'rows = max_t(u64, LO16(v) + HI16(v), HI16(wo) + HI16(vs->f[P4F_WIN_V]) + 1);' 'rows = LO16(v) + HI16(v);'
off fuzz-unset ": M2MF copy with a value this channel never wrote" "${F[@]}" \
	$'\t\t*why = "M2MF copy with a field not set by this channel";\n\t\treturn P4R_UNSET;' ''
# the model of submit.c in validate-test.c, as it was before each change
off no-switch-in-primitive "FAIL: 7: channel 1's submit reached the GPU inside channel 0's primitive" \
	-DOLD_SWITCH
off submit-state "FAIL: 2: a copy through the bind of a cancelled submit's first push not cancelled" \
	-DOLD_SUBMIT_STATE
off rejected-submit-cancel "FAIL: 1: the X16 submit not cancelled" -DOLD_NO_RECOVER
# GEM_PUSHBUF's argument checks (src/pushbuf.h), submit.c's sums as they were
off push-overflow "FAIL: offset 2^64-4, 8 bytes: push 0 at fffffffffffffffc, 8 bytes accepted past its BO of 100000" \
	-T pushbuf-test.c -DOLD_PUSH_CHECKS
off reloc-u32 "FAIL: relocation at 2^32-4, push at 0: patches word 3fffffff, expected skips word 0" \
	-T pushbuf-test.c -DOLD_RELOC_U32
# the VRAM clear (src/vclear.h): a freed range back at once as before, a fill from before a
# recovery trusted, a last launch rounded up past its zombie
off clear-at-free "FAIL: a fill that never runs: BO 2 got page 0 uncleared (tag 1)" \
	-T vclear-test.c -DOLD_FREE_AT_ONCE
off clear-gen "FAIL: a zombie fully queued when the recovery comes: handed out unfilled" \
	-T vclear-test.c -E vclear.h VCLEAR_H \
	$'\tif (c->gen != gen_now)\n\t\treturn VCLEAR_REFILL;\n' ''
off clear-prim-close "FAIL: a primitive left open: ended with no allocation waiting" \
	-T vclear-test.c -E vclear.h VCLEAR_H 'return open_ms >= 1000 && alloc_waiting;' 'return open_ms >= 1000;'
# same-owner reuse (vclear-test's second model): the owner a raw pointer, pressure freeing kept
# ranges, the shared mark ignored, an expired entry left on the kept list
off reuse-owner-alias "FAIL: reuse: owner " -T vclear-test.c -DOLD_OWNER_ALIAS
off reuse-pressure-free "FAIL: reuse: owner " -T vclear-test.c -DOLD_PRESSURE_FREE
off reuse-shared "FAIL: reuse: owner " -T vclear-test.c -DOLD_SHARED_KEPT
off reuse-double-claim "FAIL: reuse: a reused range was also a zombie" -T vclear-test.c -DOLD_EXPIRY_NO_UNLIST
off reuse-count-cap "FAIL: reuse: " -T vclear-test.c -E vclear.h VCLEAR_H \
	' ||
	       total > VCLEAR_KEEP_TOTAL || total_n > VCLEAR_KEEP_TOTAL_N;' ' ||
	       total > VCLEAR_KEEP_TOTAL;'
off reuse-miss-all "FAIL: a miss of 8 pages" -T vclear-test.c -DOLD_PRESSURE_ALL
off gart-reap-one-read "FAIL: the GART reap: " -T vclear-test.c -E vclear.h VCLEAR_H \
	'for (i = 0; i < n && vclear_seq_passed(ref, free_seq[i]); i++)' 'for (i = 0; i < n; i++)'
off reuse-cheap-first "FAIL: the reuse walk: " -T vclear-test.c -E vclear.h VCLEAR_H \
	'return same_owner && kept_size == size;' 'return true;'
off clear-overrun "FAIL: random: launch " -T vclear-test.c -E vclear.h VCLEAR_H \
	'u64 lines = (c->hi - at) / VCLEAR_LINE;' \
	'u64 lines = (c->hi - at + VCLEAR_ZERO_SIZE - 1) / VCLEAR_ZERO_SIZE * VCLEAR_MAX_LINES;'
wait

bad=0
for i in "${!names[@]}"; do
	n=${names[$i]} e=${expects[$i]}
	if [ ! -e "$D/$n.built" ]; then
		echo "fix-off $n: build failed"; bad=1; continue
	fi
	out=$("$D/$n" 2>&1); rc=$?
	nf=$(grep -c '^FAIL: ' <<<"$out")
	if [ $rc -eq 0 ]; then
		echo "fix-off $n: passed with the rule off (the cases don't show the old behaviour)"; bad=1
	elif ! grep -qF "$e" <<<"$out"; then
		echo "fix-off $n: failed ($nf checks, exit $rc) but not with: $e"
		grep '^FAIL: ' <<<"$out" | head -n 3; bad=1
	else
		echo "fix-off $n: fails as expected ($nf checks)"
	fi
done
[ $bad -eq 0 ] && echo "fix-off: ${#names[@]} rules off, each caught"
exit $bad
