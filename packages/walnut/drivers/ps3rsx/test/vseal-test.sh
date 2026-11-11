#!/bin/bash
# vseal-test.sh OUTDIR: the SPE client's host tests (spe.c's decisions, spu/vseal.c on a reordering MFC model),
# each rule switched off once (its build must fail), and the embedded image (src/vseal_spu.h): its SHA-256 as
# stated, and, when the spu-elf toolchain is there (SPU_TC, default build/spu/install/opt/spu/bin), rebuilt
# from spu/ to the same bytes. Exits 0 only if everything held.
set -u
[ $# -eq 1 ] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
mkdir -p "$1" && O=$(cd "$1" && pwd) || exit 2
cd "$(dirname "$0")" || exit 2
S=../spu
CORE="$S/v2_chacha20.c $S/v2_poly1305.c $S/v2_poly1305_x4.c $S/v2_aead.c"
CF="-O2 -g -Wall -Wextra -Wno-unused-parameter -I. -I../src -I$S"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
bad=0
build() {	# build NAME SRC DEFINES...
	local n=$1 src=$2
	shift 2
	gcc $CF "$@" -o "$O/$n" "$src" $( [ "$src" = vseal-test.c ] && echo $CORE ) || { echo "!! build $n"; bad=1; return 1; }
}
build vseal-test vseal-test.c $SAN && { "$O/vseal-test" || bad=1; }
build vseal-test-checked vseal-test.c $SAN -DV2_CHECKED && { "$O/vseal-test-checked" | tail -1; [ "${PIPESTATUS[0]}" = 0 ] || bad=1; }
build spe-core-test spe-core-test.c $SAN && { "$O/spe-core-test" || bad=1; }
# the fix-offs: each must fail
n_off=0
for v in "vseal-test VSEAL_FIXOFF_PLAIN_GET" "spe-core-test SPE_FIXOFF_NO_CHECK" "spe-core-test SPE_FIXOFF_NO_TIMEOUT" \
	"spe-core-test SPE_FIXOFF_RESTART_ON_SLOW" "spe-core-test SPE_FIXOFF_FREE_UNQUIET"; do
	set -- $v
	n_off=$((n_off + 1))
	if build "fixoff-$2" "$1.c" -w -D"$2" && ! "$O/fixoff-$2" > "$O/fixoff-$2.log" 2>&1; then
		echo "fix-off $2: caught ($(tail -1 "$O/fixoff-$2.log"))"
	else
		echo "!! fix-off $2: NOT caught"; bad=1
	fi
done
# the embedded image
python3 -I - ../src/vseal_spu.h <<'PY' || bad=1
import hashlib, re, sys
t = open(sys.argv[1]).read()
img = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2}),', t.split('vseal_spu_sha256')[0].split('vseal_spu[')[1]))
dig = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2}),', t.split('vseal_spu_sha256[32] = {')[1].split('};')[0]))
stated = re.search(r'sha256 ([0-9a-f]{64})', t).group(1)
ok = hashlib.sha256(img).digest() == dig and dig.hex() == stated and len(img) <= 0x10000
print('vseal_spu.h: %d bytes, sha256 %s: %s' % (len(img), stated, 'ok' if ok else 'MISMATCH'))
sys.exit(0 if ok else 1)
PY
SPU_TC=${SPU_TC:-../../../../../build/spu/install/opt/spu/bin}
if [ -x "$SPU_TC/spu-elf-gcc" ]; then
	make -s -C $S SPU_TC="$(cd "$SPU_TC" && pwd)" O="$O/spu" image && python3 -I $S/bin2c.py "$O/spu/vseal.bin" > "$O/vseal_spu.h" &&
		cmp -s "$O/vseal_spu.h" ../src/vseal_spu.h && echo "vseal_spu.h: rebuilt from spu/, the same bytes" ||
		{ echo "!! vseal_spu.h differs from a rebuild of spu/ (make -C spu)"; bad=1; }
else
	echo "vseal_spu.h: no spu-elf toolchain at $SPU_TC: not rebuilt (make spu-toolchain)"
fi
echo "vseal: $n_off fix-offs; $([ $bad = 0 ] && echo ok || echo FAIL)"
exit $bad
