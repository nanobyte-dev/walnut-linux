#!/bin/bash
# vseal-test.sh OUTDIR: the SPE client's host tests: spu/vseal.c on a reordering MFC model (the test image's ops
# and the release image's), spe_core.h's decisions, spe_run.c's running code against a fake SPE that runs the
# real program; each rule switched off once (its build must fail); and the embedded images (src/vseal_spu.h,
# src/vseal_spu_test.h): their SHA-256 as stated, and, when the spu-elf toolchain is there (SPU_TC, default
# build/spu/install/opt/spu/bin), rebuilt from spu/ to the same bytes. Exits 0 only if everything held.
set -u
[ $# -eq 1 ] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
mkdir -p "$1" && O=$(cd "$1" && pwd) || exit 2
cd "$(dirname "$0")" || exit 2
S=../spu
CORE="$S/v2_chacha20.c $S/v2_poly1305.c $S/v2_poly1305_x4.c $S/v2_aead.c"
CF="-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -I. -I../src -I$S"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
bad=0
build() {	# build NAME SRC DEFINES...
	local n=$1 src=$2
	shift 2
	gcc $CF "$@" -o "$O/$n" "$src" $( [ "$src" != spe-core-test.c ] && echo $CORE ) || { echo "!! build $n"; bad=1; return 1; }
}
build vseal-test vseal-test.c $SAN -DVSEAL_TEST_OPS && { "$O/vseal-test" || bad=1; }
build vseal-test-release vseal-test.c $SAN && { "$O/vseal-test-release" | sed 's/^/release image: /'; [ "${PIPESTATUS[0]}" = 0 ] || bad=1; }
build vseal-test-checked vseal-test.c $SAN -DVSEAL_TEST_OPS -DV2_CHECKED && { "$O/vseal-test-checked" | tail -1; [ "${PIPESTATUS[0]}" = 0 ] || bad=1; }
build spe-core-test spe-core-test.c $SAN && { "$O/spe-core-test" || bad=1; }
build spe-run-test spe-run-test.c $SAN && { "$O/spe-run-test" || bad=1; }
# the fix-offs: each must fail
n_off=0
for v in "vseal-test VSEAL_FIXOFF_PLAIN_GET" "vseal-test VSEAL_FIXOFF_SPLIT_TAGS" "vseal-test VSEAL_FIXOFF_PUT_REFUSED" \
	"vseal-test VSEAL_FIXOFF_DONE_EARLY" \
	"spe-core-test SPE_FIXOFF_NO_CHECK" "spe-core-test SPE_FIXOFF_NO_REFUSED" "spe-core-test SPE_FIXOFF_NO_TIMEOUT" \
	"spe-core-test SPE_FIXOFF_RESTART_ON_SLOW" "spe-core-test SPE_FIXOFF_FREE_UNQUIET" \
	"spe-core-test SPE_FIXOFF_RAM_WITHOUT_CHECK" \
	"spe-run-test SPE_FIXOFF_GOLDEN_COUNTS" "spe-run-test SPE_FIXOFF_RESET_FAIL_NODEV" \
	"spe-run-test SPE_FIXOFF_WAKE_IS_TIMEOUT" "spe-run-test SPE_FIXOFF_KEEP_RING" \
	"spe-run-test SPE_FIXOFF_RELEASED_IS_FAILURE"; do
	set -- $v
	n_off=$((n_off + 1))
	if build "fixoff-$2" "$1.c" -w -DVSEAL_TEST_OPS -D"$2" && ! timeout 300 "$O/fixoff-$2" > "$O/fixoff-$2.log" 2>&1; then
		echo "fix-off $2: caught ($(tail -1 "$O/fixoff-$2.log"))"
	else
		echo "!! fix-off $2: NOT caught"; bad=1
	fi
done
# the embedded images
for h in vseal_spu vseal_spu_test; do
	python3 -I - ../src/$h.h $h <<'PY' || bad=1
import hashlib, re, sys
t, name = open(sys.argv[1]).read(), sys.argv[2]
img = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2}),', t.split(name + '_sha256')[0].split(name + '[')[1]))
dig = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2}),', t.split(name + '_sha256[32] = {')[1].split('};')[0]))
stated = re.search(r'sha256 ([0-9a-f]{64})', t).group(1)
ok = hashlib.sha256(img).digest() == dig and dig.hex() == stated and len(img) <= 0x10000
print('%s.h: %d bytes, sha256 %s: %s' % (name, len(img), stated, 'ok' if ok else 'MISMATCH'))
sys.exit(0 if ok else 1)
PY
done
SPU_TC=${SPU_TC:-../../../../../build/spu/install/opt/spu/bin}
if [ -x "$SPU_TC/spu-elf-gcc" ]; then
	make -s -C $S SPU_TC="$(cd "$SPU_TC" && pwd)" O="$O/spu" images || bad=1
	for p in "vseal vseal_spu" "vseal-test vseal_spu_test"; do
		set -- $p
		python3 -I $S/bin2c.py "$O/spu/$1.bin" $2 > "$O/$2.h" && cmp -s "$O/$2.h" ../src/$2.h &&
			echo "$2.h: rebuilt from spu/, the same bytes" ||
			{ echo "!! $2.h differs from a rebuild of spu/ (make -C spu)"; bad=1; }
	done
else
	echo "vseal_spu*.h: no spu-elf toolchain at $SPU_TC: not rebuilt (make spu-toolchain)"
fi
echo "vseal: $n_off fix-offs; $([ $bad = 0 ] && echo ok || echo FAIL)"
exit $bad
