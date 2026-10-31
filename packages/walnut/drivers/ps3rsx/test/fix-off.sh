#!/bin/bash
# validate-test with one stability rule turned off at a time: each build must fail, with the
# FAIL line given for it, so the case shows the old behaviour. The rule is turned off in a copy
# of src/validate.c under OUTDIR/fix-off (a literal edit, which must match exactly once), or for
# the submit.c model in validate-test.c by a define (OLD_*). Nothing here touches src/.
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

# off NAME 'FAIL line it must print' [-DDEFINE | 'FROM' 'TO']...
off() {
	local name=$1 expect=$2 vc=../src/validate.c defs=()
	shift 2
	while [ $# -gt 0 ]; do
		case $1 in
		-D*)	defs+=("$1"); shift ;;
		*)	if [ "$vc" = ../src/validate.c ]; then cp ../src/validate.c "$D/$name.c"; vc=$D/$name.c; fi
			FROM=$1 TO=$2 NAME=$name perl -0777 -i -pe \
				'my $n = () = /\Q$ENV{FROM}\E/g;
				 $n == 1 or die "fix-off.sh: $ENV{NAME}: found $n times in validate.c: $ENV{FROM}\n";
				 s/\Q$ENV{FROM}\E/$ENV{TO}/' "$vc" || exit 1
			shift 2 ;;
		esac
	done
	while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
	( gcc -O1 -g -w -fno-strict-aliasing -Iinc -iquote ../src -DVALIDATE_C="\"$vc\"" "${defs[@]}" \
		-o "$D/$name" validate-test.c && touch "$D/$name.built" ) &
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
	'if (mthd == 0x50 || mthd == 0x60 || mthd == 0x64 || mthd == 0x6c)' 'if (1)'
off context-dma "FAIL: 3D context DMA feed0002 accepted" \
	'mthd <= 0x1bc && !known_dma(data)' 'mthd <= 0x1bc && 0'
off sifm-surface "FAIL: SIFM surface feed0000 accepted" \
	'if (data != 0x313371c3 && data != 0x31337a73) {' 'if (0) {'
# the model of submit.c in validate-test.c, as it was before each change
off no-switch-in-primitive "FAIL: 7: channel 1's submit reached the GPU inside channel 0's primitive" \
	-DOLD_SWITCH
off submit-state "FAIL: 2: a copy through the bind of a cancelled submit's first push not cancelled" \
	-DOLD_SUBMIT_STATE
off rejected-submit-cancel "FAIL: 1: the X16 submit not cancelled" -DOLD_NO_RECOVER
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
