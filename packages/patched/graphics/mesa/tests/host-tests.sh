#!/bin/bash
# host-tests.sh SUITE [PART...] | --list: Mesa's host tests (no PS3), each compiling the Mesa files it
# tests out of a Mesa work tree against fakes, then running them.
#   MESA_WORK      the Mesa tree (default build/work/mesa, made by make mesa-work); tools/ladder.py
#                  points it at one rung's tree of its clone
#   MESA_TEST_OUT  where programs and generated headers go (default build/tmp/tests)
#   MESA_TESTS     the test sources (default this folder; tools/ladder.py --rev exports a commit's)
# Suites and their parts (no part given: all of them, and for alloc-fail its fix-off checks too):
#   forcedxt       Mesa 0038's NV30_FORCE_DXT decision
#   staging-pool   0040's NV30_STAGING_POOL_MB decisions and a model of the pool
#   vtx-snorm8     unpack (0043's normalization against util_format, every byte), vpsim (nvfx_vertprog.c's
#                  output on an NV40 vertex program interpreter)
#   minmax-vmx     host (0044's AltiVec index min/max against the scalar loops), ppc64 (the same under qemu)
#   query-steal    0046: nv30's queries when the report slots run out, fake GPU, ASan
#   query-lost     0056: a query whose report never lands (a cancelled submission, 0053; a GPU
#                  that stopped): end, steal, destroy, elapsed, deadline must return, not hang
#                  (alarm); late and x86: a late report keeps its value (query-lost-test.c)
#   alloc-fail     upload (0047), mm-fallback and mm-nomem (0048), fence (0049), fence-cancel (0053):
#                  alloc-fail-test.c; pushbuf-flush, pushbuf-destroy (0003): context-pushbuf-test.c;
#                  fix-off: the parts against each file as it was before its patch (the parent of the
#                  patch's commit in MESA_WORK), which must fail
#   transfer-cpu   0049: nv30 texture uploads and reads by the CPU when the GART is full; 0040's small
#                  staging classes (nv30_miptree.c's pool functions)
#   blit-state     0058: nv30_transfer_rect_blit sets the window offset 0, the window clip and COORD_CONVENTIONS of
#                  its destination and points zeta at it, with the dirty bits for the next draw (state); the stream
#                  the blit writes against the file as it was before 0058: only those words differ (golden)
#   placement      0059: NV30_PS3_IDX_GART and NV30_PS3_FP_GART (state: both off as before, index buffers and
#                  fragment programs in GART when on, a busy GART index buffer waited for and never staged and
#                  GPU-copied; golden: the placement of a list of buffers with each switch against the default's:
#                  only the index buffers' and the fragment program's domains differ)
#   fb-window      0060: nv30_validate_fb's small-surface hack (a 2x2 drawn as a 16x2 block from window origin x)
#                  clips to the block (state); the stream of four targets against the file before 0060: only
#                  the two hacks' clip words differ (golden)
#   point-smooth   0050: round GL_POINT_SMOOTH points on Mesa's TGSI interpreter, and their NV40 translation
# Each part prints "RESULT <suite>:<part> pass|fail|skip" (tools/ladder.py reads them). Exit 0 when every
# part passed, 77 when the tree lacks what the suite tests (its patch) or a tool is missing, else 1.
set -u
cd "$(dirname "$0")/../../../../.." || exit 1
M=${MESA_WORK:-build/work/mesa}
T=${MESA_TEST_OUT:-build/tmp/tests}
t=${MESA_TESTS:-packages/patched/graphics/mesa/tests}
S=${1:-}; shift || true
declare -A PARTS=([forcedxt]="" [staging-pool]="" [vtx-snorm8]="unpack vpsim" [minmax-vmx]="host ppc64"
	[query-steal]="" [query-lost]="end steal destroy elapsed deadline late x86" [alloc-fail]="upload mm-fallback mm-nomem fence fence-cancel pushbuf-flush pushbuf-destroy fix-off"
	[transfer-cpu]="" [blit-state]="state golden" [placement]="state golden" [fb-window]="state golden" [point-smooth]="")
if [ "$S" = --list ]; then
	for s in forcedxt staging-pool vtx-snorm8 minmax-vmx query-steal query-lost alloc-fail transfer-cpu blit-state placement fb-window point-smooth; do
		echo "$s ${PARTS[$s]}"; done
	exit 0
fi
[ -n "$S" ] && [ -n "${PARTS[$S]+x}" ] || { echo "usage: $0 SUITE [PART...] | --list" >&2; exit 2; }
want=("$@"); all=0; [ $# = 0 ] && all=1
fails=0; ran=0
skip() { echo "skip: $*"; exit 77; }
need() { command -v "$1" >/dev/null || skip "no $1"; }
result() {	# result PART RC: one RESULT line; a failure counts
	local r=pass; [ "$2" = 0 ] || r=fail; [ "$2" = 77 ] && r=skip
	echo "RESULT $S${1:+:$1} $r"; ran=$((ran + 1)); [ $r = fail ] && fails=$((fails + 1)); return 0
}
wants() {	# wants PART: asked for (fix-off only when asked or when the whole suite runs)
	[ $all = 1 ] && return 0
	local p; for p in "${want[@]}"; do [ "$p" = "$1" ] && return 0; done; return 1
}
for p in "${want[@]}"; do [[ " ${PARTS[$S]} " == *" $p "* ]] || { echo "$S has no part $p (${PARTS[$S]:-none})" >&2; exit 2; }; done
mkdir -p "$T"

gen() {	# Mesa's generated headers that the nv30 and nir files include, into $T/mesa-gen
	local G=$T/mesa-gen N=$M/src/compiler/nir
	mkdir -p $G/util/format
	python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml --enums > $G/util/format/u_format_gen.h &&
	python3 $M/src/compiler/builtin_types_h.py $G/builtin_types.h &&
	python3 $N/nir_opcodes_h.py > $G/nir_opcodes.h && python3 $N/nir_builder_opcodes_h.py > $G/nir_builder_opcodes.h &&
	python3 $N/nir_intrinsics_h.py --out $G/nir_intrinsics.h &&
	python3 $N/nir_intrinsics_indices_h.py --out $G/nir_intrinsics_indices.h
}
mcc() {	# mcc OUT SRC... [-I DIR...]: a test program against the nouveau driver's headers, ASan and UBSan
	local out=$1; shift
	gcc "$@" -g -O1 -std=gnu11 -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE \
		-DHAVE_TIMESPEC_GET -DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$T/mesa-gen \
		-I$M/include -I$M/src -I$M/src/gallium/include -I$M/src/gallium/auxiliary \
		-I$M/src/gallium/drivers/nouveau -I$M/src/util -I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir \
		-I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm -I$M/src/nouveau/headers \
		-o "$out" -lm
}
expect_fail() {	# expect_fail WHAT REGEX CMD...: a run with a fix off must fail, with REGEX in its output
	local what=$1 re=$2 out rc; shift 2
	out=$("$@" 2>&1); rc=$?
	if [ $rc != 0 ] && grep -qE -- "$re" <<<"$out"; then echo "ok   $what: fails as expected (exit $rc)"; return 0; fi
	printf '%s\n' "$out" | tail -n 20; echo "FAIL $what: exit $rc, want a failure with /$re/"; return 1
}
A=$M/src/gallium/auxiliary/tgsi
TGSI="$A/tgsi_text.c $A/tgsi_build.c $A/tgsi_info.c $A/tgsi_util.c $A/tgsi_strings.c $A/tgsi_parse.c $A/tgsi_dump.c $A/tgsi_iterate.c $A/tgsi_scan.c"
need gcc

case $S in
forcedxt)
	[ -e $M/src/mesa/main/texforcedxt_policy.h ] || skip "no $M with patch 0038 (make mesa-work)"
	gcc -O2 -Wall -Wextra -Werror -I $M/include -I $M/src -o $T/forcedxt-policy-test $t/forcedxt-policy-test.c &&
		$T/forcedxt-policy-test; result "" $?
	;;
staging-pool)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_staging_policy.h ] || skip "no $M with patch 0040 (make mesa-work)"
	gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I $M/src/gallium/drivers/nouveau \
		-o $T/staging-pool-test $t/staging-pool-test.c && $T/staging-pool-test; result "" $?
	;;
vtx-snorm8)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_vtx_snorm8.h ] || skip "no $M with the NV30_VTX_SNORM8 patch (make mesa-work)"
	if wants unpack; then
		# util_format's SNORM8 structs and unpack functions, as Mesa generates them
		python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml > $T/u_format_table.c &&
		awk '/^struct util_format_r8(g8(b8)?)?_snorm \{/,/^\};/ { print }
		     /^util_format_r8(g8(b8(a8)?)?)?_snorm_unpack_rgba_float\(/ { print "static void"; p = 1 }
		     p { print } p && /^\}/ { p = 0 }' $T/u_format_table.c > $T/snorm8-ref.h &&
		{ [ "$(grep -c '_unpack_rgba_float(' $T/snorm8-ref.h)" = 4 ] || { echo "snorm8-ref.h: unpack functions not found"; false; }; } &&
		gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I $M/src/gallium/drivers/nouveau -I $T \
			-DUTIL_ARCH_BIG_ENDIAN=0 -o $T/vtx-snorm8-test $t/vtx-snorm8-test.c && $T/vtx-snorm8-test
		result unpack $?
	fi
	if wants vpsim; then
		# the translator itself (nvfx_vertprog.c) on an NV40 vertex program interpreter
		gen && mcc $T/vtx-snorm8-vpsim $t/vtx-snorm8-vpsim.c $t/vtx-snorm8-stubs.c \
			$M/src/gallium/drivers/nouveau/nv30/nvfx_vertprog.c $TGSI $M/src/util/ralloc.c $M/src/util/u_dynarray.c &&
			ASAN_OPTIONS=detect_leaks=0 $T/vtx-snorm8-vpsim
		result vpsim $?
	fi
	;;
minmax-vmx)
	[ -e $M/src/mesa/vbo/vbo_minmax_vmx.h ] || skip "no $M with the NV30_MINMAX_VMX patch (make mesa-work)"
	if wants host; then
		gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I $M/src/mesa/vbo \
			-o $T/minmax-vmx-test $t/minmax-vmx-test.c && $T/minmax-vmx-test; result host $?
	fi
	if wants ppc64; then
		# AltiVec itself: the deb builder's ppc64 cross compiler, run under qemu-user
		img=$(docker images --format '{{.Repository}}:{{.Tag}}' ps3-deb-builder 2>/dev/null | head -1)
		if [ -z "$img" ] || ! command -v qemu-ppc64-static >/dev/null; then
			echo "SKIP ${TEST_SUITE:-mesa-minmax-vmx}:ppc64 needs docker with a ps3-deb-builder image and qemu-ppc64-static"
			result ppc64 77
		else
			docker run --rm -u "$(id -u):$(id -g)" -v "$(realpath "$M"):/mesa:ro" -v "$(realpath "$t"):/tests:ro" \
				-v "$(realpath "$T"):/out" -w /out "$img" \
				powerpc64-linux-gnu-gcc -O2 -mcpu=cell -Wall -Wextra -Werror -static -I /mesa/src/mesa/vbo \
				-o /out/minmax-vmx-test-ppc64 /tests/minmax-vmx-test.c && qemu-ppc64-static $T/minmax-vmx-test-ppc64
			result ppc64 $?
		fi
	fi
	;;
query-steal)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_query.c ] || skip "no Mesa tree at $M (make mesa-work)"
	gen && mcc $T/query-steal-test $t/query-steal-test.c -Wall -Werror && $T/query-steal-test; result "" $?
	;;
query-lost)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_query.c ] || skip "no Mesa tree at $M (make mesa-work)"
	gen || exit 1
	CANC=; grep -q 'uint32_t cancelled;' $M/src/gallium/winsys/nouveau/drm/nouveau.h && CANC=-DHAVE_DEVICE_CANCELLED
	built=
	for p in ${PARTS[$S]}; do
		wants $p || continue
		[ -n "$built" ] || { mcc $T/query-lost-test $t/query-lost-test.c -Wall -Werror -pthread $CANC && built=ok || built=no; }
		if [ $built = ok ]; then $T/query-lost-test $p; result $p $?; else echo "query-lost-test.c does not build against $M"; result $p 1; fi
	done
	;;
alloc-fail)
	[ -e $M/src/gallium/drivers/nouveau/nouveau_mm.c ] || skip "no Mesa tree at $M (make mesa-work)"
	gen || exit 1
	CANC=; grep -q 'uint32_t cancelled;' $M/src/gallium/winsys/nouveau/drm/nouveau.h && CANC=-DHAVE_DEVICE_CANCELLED
	af=(upload mm-fallback mm-nomem fence fence-cancel) built=
	for p in "${af[@]}"; do
		wants $p || continue
		[ -n "$built" ] || { mcc $T/alloc-fail-test $t/alloc-fail-test.c -Wall -Werror $CANC && built=ok || built=no; }
		if [ $built = ok ]; then $T/alloc-fail-test $p; result $p $?; else echo "alloc-fail-test.c does not build against $M"; result $p 1; fi
	done
	U=-Wl,--unresolved-symbols=ignore-all	# context-pushbuf-test: what it doesn't stub would crash
	built=
	for p in flush destroy; do
		wants pushbuf-$p || continue
		[ -n "$built" ] || { mcc $T/context-pushbuf-test $t/context-pushbuf-test.c -Wall -Werror $U && built=ok || built=no; }
		if [ $built = ok ]; then $T/context-pushbuf-test $p; result pushbuf-$p $?; else echo "context-pushbuf-test.c does not build against $M"; result pushbuf-$p 1; fi
	done
	if wants fix-off; then
		# fix off: the same cases against each file as it was before its patch (the parent of the
		# patch's commit in MESA_WORK) must fail where the patch's bug was
		before() {	# before SUBJECT PATH DEST: PATH from the work tree's commit before the one named SUBJECT
			local c; c=$(git -C $M log --format='%H %s' | grep -F " $1" | head -n 1 | cut -d' ' -f1)
			[ -n "$c" ] && mkdir -p "${3%/*}" && git -C $M show "$c^:$2" > "$3"
		}
		O=$T/fix-off D=src/gallium/drivers/nouveau
		rm -rf $O
		if ! git -C $M rev-parse -q --verify HEAD >/dev/null 2>&1; then
			echo "SKIP ${TEST_SUITE:-mesa-alloc-fail}:fix-off $M is no git tree"; result fix-off 77
		else
			# mm-nomem's fix was 0002 until it was folded into 0048 (2026-10-31): nouveau_mm.c
			# before 0048 is then also before it (a tree with 0002 still apart uses 0002's parent)
			nomem=$O/b
			git -C $M log --format=%s | grep -qF "nouveau_mm: return NULL when no slab" && nomem=$O/a
			{ [ $nomem = $O/b ] || before "nouveau_mm: return NULL when no slab" $D/nouveau_mm.c $O/a/nouveau_mm.c; } &&
			before "u_upload, st, nv30: an upload buffer" src/gallium/auxiliary/util/u_upload_mgr.c $O/a/util/u_upload_mgr.c &&
			before "nouveau: a buffer allocation that fails" $D/nouveau_mm.c $O/b/nouveau_mm.c &&
			before "nv30: push buffer robustness" $D/nv30/nv30_context.c $O/c/nv30/nv30_context.c &&
			before "nouveau, nv30: recover from a submission" $D/nouveau_fence.c $O/d/nouveau_fence.c &&
			mcc $O/a/test $t/alloc-fail-test.c -Wall -Werror $CANC -I$O/a -I$M/src/gallium/auxiliary/util &&
			mcc $O/b/test $t/alloc-fail-test.c -Wall -Werror $CANC -I$O/b &&
			mcc $O/c/test $t/context-pushbuf-test.c -Wall -Werror $U -I$O/c -I$M/src/gallium/drivers/nouveau/nv30 &&
			mcc $O/d/test $t/alloc-fail-test.c -Wall -Werror $CANC -I$O/d &&
			expect_fail "0047 off: u_upload" 'FAIL .*failed allocation left \*outbuf' $O/a/test upload &&
			expect_fail "0048 off: nouveau_mm with no slab" 'FAIL .*no memory: alloc 0x' $nomem/test mm-nomem &&
			expect_fail "0048 off: nouveau_mm fallback" 'FAIL .*a buffer of its own comes without a slab' $O/b/test mm-fallback &&
			expect_fail "0003 off: flush before any draw" 'FAIL .*kick came with no push buffer space' $O/c/test flush &&
			expect_fail "0003 off: destroy after a failed init" 'SEGV .* in (PUSH_KICK|nv30_context_destroy)' $O/c/test destroy &&
			expect_fail "0053 off: the wait on a cancelled submission's fence" 'FAIL .*cancelled submission failed' $O/d/test fence-cancel
			result fix-off $?
		fi
	fi
	;;
transfer-cpu)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_miptree.c ] || skip "no Mesa tree at $M (make mesa-work)"
	gen && mcc $T/transfer-cpu-test $t/transfer-cpu-test.c -Wall -Werror && $T/transfer-cpu-test; result "" $?
	;;
blit-state)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_transfer.c ] || skip "no Mesa tree at $M (make mesa-work)"
	grep -q 'VIEWPORT_TX_ORIGIN' $M/src/gallium/drivers/nouveau/nv30/nv30_transfer.c || {
		echo "the blit sets no window state in this tree (before 0058)"; wants state && result state 1; wants golden && result golden 77; exit 1; }
	U=-Wl,--unresolved-symbols=ignore-all
	gen || exit 1
	if wants state; then
		mcc $T/blit-state-test $t/blit-state-test.c -Wall -Werror $U && ASAN_OPTIONS=detect_leaks=0 $T/blit-state-test; result state $?
	fi
	if wants golden; then
		# the stream of three blits (swizzled, linear, GART) with 0058 and with nv30_transfer.c as it was before it
		D=src/gallium/drivers/nouveau/nv30 O=$T/blit-before
		rm -rf $O; mkdir -p $O/nv30
		c=$(git -C $M log --format='%H %s' | grep -F " nv30: the blit draws with its own window state" | head -n 1 | cut -d' ' -f1)
		if [ -z "$c" ] || ! git -C $M show "$c^:$D/nv30_transfer.c" > $O/nv30/nv30_transfer.c 2>/dev/null; then
			echo "SKIP mesa-blit-state:golden no commit before 0058 in $M"; result golden 77
		else
			mcc $T/blit-state-new $t/blit-state-test.c $U && mcc $T/blit-state-old $t/blit-state-test.c $U -I$O &&
			ASAN_OPTIONS=detect_leaks=0 $T/blit-state-new dump > $T/blit-new.txt && ASAN_OPTIONS=detect_leaks=0 $T/blit-state-old dump > $T/blit-old.txt
			rc=$?
			if [ $rc = 0 ]; then
				# the words 0058 adds: the window block (2b8..2c4), COORD (1d88), ZETA_PITCH (22c), ZETA_OFFSET (214), DMA_ZETA (198)
				flt() { grep -v -E '^(02b8|02bc|02c0|02c4|1d88|022c|0214|0198) ' "$1"; }
				if [ "$(flt $T/blit-new.txt | md5sum)" = "$(flt $T/blit-old.txt | md5sum)" ] && ! cmp -s $T/blit-new.txt $T/blit-old.txt; then
					echo "ok   golden: $(wc -l < $T/blit-old.txt) words before, $(wc -l < $T/blit-new.txt) after; the others are identical"
				else
					diff <(flt $T/blit-old.txt) <(flt $T/blit-new.txt) | head -20; rc=1
				fi
			fi
			result golden $rc
		fi
	fi
	;;
placement)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_screen.c ] || skip "no Mesa tree at $M (make mesa-work)"
	grep -q 'NV30_PS3_IDX_GART' $M/src/gallium/drivers/nouveau/nv30/nv30_screen.c || {
		echo "no NV30_PS3_IDX_GART in this tree (before 0059)"; wants state && result state 1; wants golden && result golden 77; exit 1; }
	gen || exit 1
	mcc $T/placement-test $t/placement-test.c -Wall -Werror -Wno-unused-function || { echo "placement-test.c does not build against $M"; wants state && result state 1; wants golden && result golden 1; exit 1; }
	export ASAN_OPTIONS=detect_leaks=0
	if wants state; then $T/placement-test; result state $?; fi
	if wants golden; then
		rc=0
		$T/placement-test dump > $T/placement-off.txt && $T/placement-test dump i > $T/placement-i.txt &&
			$T/placement-test dump f > $T/placement-f.txt && $T/placement-test dump if > $T/placement-if.txt || rc=1
		if [ $rc = 0 ]; then
			# each switch changes only its own lines: IDX_GART the two index buffers that are not also vertex buffers and not stream
			# (static, dynamic), FP_GART the fragment program
			d=$(diff $T/placement-off.txt $T/placement-i.txt | grep '^>' | sed 's/:.*//; s/^> //' | tr '\n' ',')
			[ "$d" = "index static,index dynamic," ] || { echo "IDX_GART changed: $d"; rc=1; }
			d=$(diff $T/placement-off.txt $T/placement-f.txt | grep '^>' | sed 's/:.*//; s/^> //' | tr '\n' ',')
			[ "$d" = "fragment program," ] || { echo "FP_GART changed: $d"; rc=1; }
			[ "$(diff $T/placement-off.txt $T/placement-if.txt | grep -c '^>')" = 3 ] || { echo "both: not three changed lines"; rc=1; }
			[ $rc != 0 ] || echo "ok   golden: IDX_GART moves the static and dynamic index buffers, FP_GART the fragment program, nothing else"
		fi
		result golden $rc
	fi
	;;
fb-window)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_state_validate.c ] || skip "no Mesa tree at $M (make mesa-work)"
	grep -q 'clip_w' $M/src/gallium/drivers/nouveau/nv30/nv30_state_validate.c || {
		echo "no small-surface clip fix in this tree (before 0060)"; wants state && result state 1; wants golden && result golden 77; exit 1; }
	U=-Wl,--unresolved-symbols=ignore-all
	gen || exit 1
	export ASAN_OPTIONS=detect_leaks=0
	if wants state; then mcc $T/fb-window-test $t/fb-window-test.c -Wall -Werror -Wno-unused-function $U && $T/fb-window-test; result state $?; fi
	if wants golden; then
		O=$T/fbw-before
		rm -rf $O; mkdir -p $O/nv30
		c=$(git -C $M log --format='%H %s' | grep -F " nv30: the small-surface hack's window clip" | head -n 1 | cut -d' ' -f1)
		if [ -z "$c" ] || ! git -C $M show "$c^:src/gallium/drivers/nouveau/nv30/nv30_state_validate.c" > $O/nv30/nv30_state_validate.c 2>/dev/null; then
			echo "SKIP mesa-fb-window:golden no commit before 0060 in $M"; result golden 77
		else
			rc=0
			mcc $T/fbw-new $t/fb-window-test.c -Wno-unused-function $U && mcc $T/fbw-old $t/fb-window-test.c -Wno-unused-function $U -I$O &&
				$T/fbw-new dump > $T/fbw-new.txt && $T/fbw-old dump > $T/fbw-old.txt || rc=1
			if [ $rc = 0 ]; then
				n=$(diff $T/fbw-old.txt $T/fbw-new.txt | grep -c '^>')
				m=$(diff $T/fbw-old.txt $T/fbw-new.txt | grep '^>' | sed 's/^> //; s/ .*//' | sort -u | tr '\n' ' ')
				if [ "$n" = 2 ] && [ "$m" = "02c0 " ]; then
					echo "ok   golden: $(wc -l < $T/fbw-old.txt) words of four targets; two differ, the window clip (02c0) of the swizzled and the linear small-surface hack"
				else
					echo "$n words differ ($m), want only the two hacks' clips 02c0"; diff $T/fbw-old.txt $T/fbw-new.txt | head; rc=1
				fi
			fi
			result golden $rc
		fi
	fi
	;;
point-smooth)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_point_smooth.c ] ||
		skip "no $M with the NV30_POINT_SMOOTH patch (make mesa-work)"
	gen && mcc $T/point-smooth-test $t/point-smooth-test.c $M/src/gallium/drivers/nouveau/nv30/nv30_point_smooth.c \
		$M/src/gallium/drivers/nouveau/nv30/nvfx_fragprog.c $A/tgsi_exec.c $A/tgsi_transform.c $TGSI \
		$M/src/util/ralloc.c $M/src/util/u_dynarray.c &&
		ASAN_OPTIONS=detect_leaks=0 $T/point-smooth-test
	result "" $?
	;;
esac
[ $fails = 0 ]
