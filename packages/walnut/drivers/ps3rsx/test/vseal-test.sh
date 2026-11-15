#!/bin/bash
# vseal-test.sh OUTDIR: the SPE client's host tests: spu/vseal.c on a reordering MFC model (the test image's ops
# and the release image's), spe_core.h's decisions, spe_run.c's running code against a fake SPE that runs the
# real program, the sealed swap's page logic (vswap_core.c) on a model pool, bounce, disk and SPE; each rule
# switched off once (its build must fail); and the embedded images (src/vseal_spu.h,
# src/vseal_spu_test.h): their SHA-256 as stated, and, when the spu-elf toolchain is there (SPU_TC, default
# build/spu/install/opt/spu/bin), rebuilt from spu/ to the same bytes. Exits 0 only if everything held.
set -u
[ $# -eq 1 ] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
mkdir -p "$1" && O=$(cd "$1" && pwd) || exit 2
cd "$(dirname "$0")" || exit 2
S=../spu
CORE="$S/v2_chacha20.c $S/v2_poly1305.c $S/v2_poly1305_x4.c $S/v2_aead.c"
CF="-O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -pthread -I. -I../src -I$S"
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
build vswap-test vswap-test.c $SAN -DV2_SCALAR_ONLY && { "$O/vswap-test" || bad=1; }
# the fix-offs: each must fail
n_off=0
for v in "vseal-test VSEAL_FIXOFF_PLAIN_GET" "vseal-test VSEAL_FIXOFF_SPLIT_TAGS" "vseal-test VSEAL_FIXOFF_PUT_REFUSED" \
	"vseal-test VSEAL_FIXOFF_DONE_EARLY" \
	"spe-core-test SPE_FIXOFF_NO_CHECK" "spe-core-test SPE_FIXOFF_NO_REFUSED" "spe-core-test SPE_FIXOFF_NO_TIMEOUT" \
	"spe-core-test SPE_FIXOFF_RESTART_ON_SLOW" "spe-core-test SPE_FIXOFF_FREE_UNQUIET" \
	"spe-core-test SPE_FIXOFF_RAM_WITHOUT_CHECK" \
	"spe-run-test SPE_FIXOFF_GOLDEN_COUNTS" "spe-run-test SPE_FIXOFF_RESET_FAIL_NODEV" \
	"spe-run-test SPE_FIXOFF_WAKE_IS_TIMEOUT" "spe-run-test SPE_FIXOFF_KEEP_RING" \
	"spe-run-test SPE_FIXOFF_RELEASED_IS_FAILURE" "spe-run-test SPE_FIXOFF_NO_WANT_IRQ" \
	"vswap-test VSX_FIXOFF_IGNORE_TAG" "vswap-test VSX_FIXOFF_SAME_CTR" "vswap-test VSX_FIXOFF_AD_NO_PAGE" \
	"vswap-test VSX_FIXOFF_WB_INTO_BOUNCE" "vswap-test VSX_FIXOFF_NO_RETRY" "vswap-test VSX_FIXOFF_DISCARD_BAD" \
	"vswap-test VSX_FIXOFF_NO_DEFER" "vswap-test VSX_FIXOFF_PPE_NO_RETRY" "vswap-test VSX_FIXOFF_PARTIAL_EMPTY" \
	"vswap-test VSX_FIXOFF_MODE_BY_SWITCH" "vswap-test VSX_FIXOFF_BATCH_REORDER" "vswap-test VSX_FIXOFF_R1_FINAL" \
	"vswap-test VSX_FIXOFF_NO_FREE" "vswap-test VSX_FIXOFF_FREE_KEEPS_MARK" "vswap-test VSX_FIXOFF_FREE_NOW" \
	"vswap-test VSX_FIXOFF_WB_KEEP_ALL" "vswap-test VSX_FIXOFF_SHORT_NO_APPLY" "vswap-test VSX_FIXOFF_EAGER_APPLY" \
	"vswap-test VSX_FIXOFF_PEND_UNCOUNTED" "vswap-test VSX_FIXOFF_DISCARD_KEEPS_MARK" \
	"vswap-test VSX_FIXOFF_SHORT_COUNT_ANY" "vswap-test VSX_FIXOFF_KEEP_NO_APPLY" \
	"vswap-test VSX_FIXOFF_OFF_MARKS" "vswap-test VSX_FIXOFF_OFF_SHORT_APPLY" \
	"vswap-test VSX_FIXOFF_AGE_AT_JOIN" "vswap-test VSX_FIXOFF_AGE_ALWAYS"; do
	set -- $v
	n_off=$((n_off + 1))
	if build "fixoff-$2" "$1.c" -w -DVSEAL_TEST_OPS $([ "$1" = vswap-test ] && echo -DV2_SCALAR_ONLY) -D"$2" && ! timeout 300 "$O/fixoff-$2" > "$O/fixoff-$2.log" 2>&1; then
		echo "fix-off $2: caught ($(tail -1 "$O/fixoff-$2.log"))"
	else
		echo "!! fix-off $2: NOT caught"; bad=1
	fi
done
# vswap.c's request path never waits for mm_lock (it holds vs->lock there): slot_alloc only trylocks it
if awk '/^static int slot_alloc\(/,/^}/' ../src/vswap.c | grep -q 'mutex_lock(&rsx->mm_lock)'; then
	echo "!! vswap.c slot_alloc waits for mm_lock"; bad=1
else
	echo "vswap.c: slot_alloc only trylocks mm_lock"
fi
# the free notify only marks (vswap_core.h "Frees": a game's frees cost no wakeup and no lock): neither the notify
# nor vsx_free_mark queues or kicks work, completes, wakes, schedules, locks, allocates or sleeps; io_work applies
# no marks (a short pool, the demotion, the shrink and the write-back do), and a failed allocation asks for a
# demotion only when it is the last word (after the marks were applied)
NOTIFY_BAD='queue_|work|complete|wake|schedule|lock\(|_lock|spin_|mutex|alloc|sleep'
if { awk '/^static void vswap_slot_free_notify\(/,/^}/' ../src/vswap.c
     awk '/^int vsx_free_mark\(/,/^}/' ../src/vswap_core.c; } | grep -Eq "$NOTIFY_BAD"; then
	echo "!! vswap.c/vswap_core.c: the free notify or vsx_free_mark queues work, wakes, locks or allocates"; bad=1
elif ! awk '/^int vsx_free_mark\(/,/^}/' ../src/vswap_core.c | grep -q 'vsx_mark_bit('; then
	echo "!! vswap_core.c: vsx_free_mark not found or doesn't mark"; bad=1
elif ! awk '/^static void vswap_slot_free_notify\(/,/^}/' ../src/vswap.c | grep -q 'vsx_free_mark('; then
	echo "!! vswap.c: the free notify doesn't mark"; bad=1
elif awk '/^static void vswap_io_work\(/,/^}/' ../src/vswap.c | grep -q 'vsx_apply_frees'; then
	echo "!! vswap.c: io_work applies the free marks"; bad=1
elif ! awk '/^int vsx_hw_slot_alloc\(/,/^}/' ../src/vswap.c | grep -q 'maybe_demote(vs, slot < 0 && last)'; then
	echo "!! vswap.c: a first failed allocation asks for a demotion"; bad=1
else
	echo "vswap.c: the free notify and vsx_free_mark only mark, io_work applies none, a demotion only after the marks"
fi
# slot reuse (vswap_reuse) is off by default and can be changed at run time; backing_store points the core at it
# before it publishes x->freed (the notify's acquire), so every mark and apply sees it
if ! grep -q '^static bool vswap_reuse;' ../src/vswap.c || ! grep -q '^module_param(vswap_reuse, bool, 0644);' ../src/vswap.c; then
	echo "!! vswap.c: vswap_reuse isn't a bool, off by default, writable at run time"; bad=1
elif ! awk '/^static ssize_t backing_store\(/,/^}/' ../src/vswap.c | awk '
	/vs->x\.reuse = &vswap_reuse;/ { r = NR } /smp_store_release\(&vs->x\.freed/ { f = NR }
	END { exit !(r && f && r < f) }'; then
	echo "!! vswap.c backing_store: x.reuse not set before x->freed is published"; bad=1
else
	echo "vswap.c: vswap_reuse off by default, writable, set before the marks' bitmap is published"
fi
# the age order: slot_alloc hands every slot out through its found: path, which tells the core (vsx_chunk_used, the
# model's rule), and the demotion takes the lowest age
if ! awk '/^static int slot_alloc\(/,/^}/' ../src/vswap.c | awk '
	/^found:/ { f = NR } /vsx_chunk_used\(&vs->x, &c->born, &vs->seq\);/ { u = NR } /return i \* SLOTS \+ n;/ { r = NR }
	END { exit !(f && u > f && r > u) }'; then
	echo "!! vswap.c slot_alloc: a handed-out slot doesn't set its chunk's age (vsx_chunk_used)"; bad=1
elif ! awk '/^static void vswap_demote_work\(/,/^}/' ../src/vswap.c | grep -q 'vs->chunks\[i\]\.born < vs->chunks\[oldest\]\.born'; then
	echo "!! vswap.c: the demotion doesn't take the lowest age"; bad=1
else
	echo "vswap.c: slot_alloc sets the chunk's age through vsx_chunk_used, the demotion takes the lowest"
fi
# the age is otherwise set only where the model sets it: slot_alloc's grow (before found:) and chunk_evict's empty
# branch; nowhere else in vswap.c
if ! awk '/^static int slot_alloc\(/,/^found:/' ../src/vswap.c | grep -q 'c->born = ++vs->seq;'; then
	echo "!! vswap.c slot_alloc: the grow doesn't set the new chunk's age"; bad=1
elif ! awk '/^static .*chunk_evict\(/,/^}/' ../src/vswap.c | awk '/} else if \(empty\) \{/ { e = NR } /c->born = \+\+vs->seq;/ && e && NR == e + 1 { ok = 1 } END { exit !ok }'; then
	echo "!! vswap.c chunk_evict: emptying a chunk doesn't give it a new age"; bad=1
elif [ "$(grep -c 'born = ' ../src/vswap.c)" != 2 ]; then
	echo "!! vswap.c: a chunk's age is set somewhere the model doesn't know ($(grep -c 'born = ' ../src/vswap.c) places)"; bad=1
else
	echo "vswap.c: a chunk's age is set at the grow and when its write-back empties it, as the model does"
fi
# chunk_evict drops what changed since its open (vsx_writeback_keep, the model's rule) right before it sets
# wb_pages, the only place it does, with no unlock between the two
if awk '/^static bool chunk_evict\(/,/^}/' ../src/vswap.c | awk '
	/vsx_writeback_keep\(/ { keep = NR } /mutex_unlock\(/ { unlock = NR }
	/x\.wb_pages = / { n++; if (!keep || unlock > keep) bad = 1 }
	END { exit !(n == 1 && !bad) }'; then
	echo "vswap.c: chunk_evict keeps only unchanged pages before wb_pages"
else
	echo "!! vswap.c chunk_evict: no vsx_writeback_keep under the lock before wb_pages"; bad=1
fi
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
