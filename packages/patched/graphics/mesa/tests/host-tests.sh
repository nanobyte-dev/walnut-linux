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
#                  (alarm); late and x86: a late report keeps its value (query-lost-test.c); failed (0061): a
#                  submission the kernel failed ends a wait on any machine; fix-off: the nv30_query.c of
#                  the commit before each later patch (0061, 0062) must fail that patch's parts
#   submit-fail    0061: the winsys's push buffer submission (nouveau.c compiled in whole, the kernel's answer
#                  faked): a cancelled submission and one the kernel failed another way both count in
#                  nouveau_device::cancelled (ecanceled, failed, ok), and the count loses nothing under four
#                  threads (threads); fix-off: the part against nouveau.c as it was before 0061 must fail
#   bo-domain      0064: nv30_bo_domain (where a texture's buffer is addressed) is 0055's rule on the PS3 and VRAM
#                  anywhere else, and 0066's NV30_TEX_INVALIDATE_ALWAYS default (state: all flag combinations; fix-off: against nv30_winsys.h before 0064)
#   fragprog       nvfx_fragprog.c's translation (fragprog-test.c): txb (0065: texture2D with a bias is a TEX on the PS3, the
#                  hardware's TXB elsewhere), rep (0067: a loop ends on an instruction slot, also after a constant);
#                  fix-off: each against the file as it was before its patch
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
#                  only the index buffers' and the fragment program's domains differ); default (0069, 0070): both
#                  on by default on the PS3 only; migrate (0070): an index buffer found in VRAM moves to GART with
#                  one GPU copy kicked on its own, cpu_only, the arrays validated again; blit (0069): every move of a
#                  program to VRAM is under !fp_gart; wiring (0070, 0071): the draw hook, both screen defaults and
#                  both swtnl maps as they must be; migrate (0071): the state after a GPU move (all valid, fenced,
#                  GPU-writing, a CPU write waits), the CPU fill from a current copy (no GPU copy, no kick, the copy
#                  freed), stream/no-bind GART buffers made cpu_only with a queued GPU write kicked, GART full (one
#                  wait, then the draw dropped), a lost copy (VRAM kept), a shared buffer kept; fix-off: each of those
#                  rules off in nv30_resource.c, the defaults as before, each call site taken out: each must fail
#   fb-window      0060: nv30_validate_fb's small-surface hack (a 2x2 drawn as a 16x2 block from window origin x)
#                  clips to the block (state); the stream of four targets against the file before 0060: only
#                  the two hacks' clip words differ (golden)
#   point-smooth   0050: round GL_POINT_SMOOTH points on Mesa's TGSI interpreter, and their NV40 translation
#   idx-split      0072: indexed draws past ps3rsx's per-binding cap (1 MiB) as pieces (nv30_idx_split.h):
#                  model (every mode, restart, U8/16/32, small pieces: the pieces' primitives are the draw's,
#                  winding and provoking vertex kept), cap (2 MiB U32 lists, U16 over 1 MiB, strips, fans and
#                  loops across the cap, restart: each piece under the cap with the kernel's skew), wiring
#                  (nv30_vbo.c and the screen's NV30_PS3_IDX_SPLIT as they must be); fix-off: each rule of
#                  the header taken out must fail
# Each part prints "RESULT <suite>:<part> pass|fail|skip" (tools/ladder.py reads them). Exit 0 when every
# part passed, 77 when the tree lacks what the suite tests (its patch) or a tool is missing, else 1.
set -u
cd "$(dirname "$0")/../../../../.." || exit 1
M=${MESA_WORK:-build/work/mesa}
T=${MESA_TEST_OUT:-build/tmp/tests}
t=${MESA_TESTS:-packages/patched/graphics/mesa/tests}
S=${1:-}; shift || true
declare -A PARTS=([forcedxt]="" [staging-pool]="" [vtx-snorm8]="unpack vpsim" [minmax-vmx]="host ppc64"
	[query-steal]="" [query-lost]="end steal destroy elapsed failed deadline deadline-edge begin-sent two-ctx own-begin stale no-slot disowned wait-option late x86 fix-off" [alloc-fail]="upload mm-fallback mm-nomem fence fence-cancel pushbuf-flush pushbuf-destroy fix-off"
	[transfer-cpu]="fix-off" [blit-state]="state golden" [placement]="state golden default migrate blit wiring fix-off" [fb-window]="state golden" [point-smooth]="" [submit-fail]="ecanceled failed notify ratelimit ok threads fix-off" [bo-domain]="state fix-off" [fragprog]="txb rep end golden fix-off" [idx-split]="model cap wiring fix-off")
if [ "$S" = --list ]; then
	for s in forcedxt staging-pool vtx-snorm8 minmax-vmx query-steal query-lost alloc-fail transfer-cpu blit-state placement fb-window point-smooth submit-fail bo-domain fragprog idx-split; do
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
		[ $p = fix-off ] && continue
		[ -n "$built" ] || { mcc $T/query-lost-test $t/query-lost-test.c -Wall -Werror -pthread $CANC && built=ok || built=no; }
		if [ $built = ok ]; then $T/query-lost-test $p; result $p $?; else echo "query-lost-test.c does not build against $M"; result $p 1; fi
	done
	if wants fix-off; then
		# fix off: the parts of a patch against nv30_query.c as it was before that patch (the parent of its
		# commit in MESA_WORK) must fail
		D=src/gallium/drivers/nouveau/nv30 O=$T/query-before
		rm -rf $O
		qoff() {	# qoff SUBJECT LABEL PART...
			local subj=$1 label=$2 c; shift 2
			c=$(git -C $M log --format='%H %s' | grep -F " $subj" | head -n 1 | cut -d' ' -f1)
			[ -n "$c" ] || { echo "no commit \"$subj\" in $M"; return 1; }
			mkdir -p $O/$label/nv30 && git -C $M show "$c^:$D/nv30_query.c" > $O/$label/nv30/nv30_query.c &&
			mcc $O/$label/test $t/query-lost-test.c -Wall -Werror -pthread $CANC -I$O/$label || return 1
			local part rc=0
			for part in "$@"; do
				expect_fail "$label off: $part" 'FAIL' $O/$label/test $part || rc=1
			done
			return $rc
		}
		if ! git -C $M rev-parse -q --verify HEAD >/dev/null 2>&1; then
			echo "SKIP ${TEST_SUITE:-mesa-query-lost}:fix-off $M is no git tree"; result fix-off 77
		else
			rc=0
			qoff "nouveau: a submission the kernel fails counts as cancelled" 0061 failed || rc=1
			qoff "nv30: queries" 0062 deadline-edge begin-sent two-ctx own-begin stale no-slot disowned || rc=1
			result fix-off $rc
		fi
	fi
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
	if [ $all = 1 ] || [ ${#want[@]} = 0 ]; then
		gen && mcc $T/transfer-cpu-test $t/transfer-cpu-test.c -Wall -Werror && $T/transfer-cpu-test; result "" $?
	else
		gen || exit 1
	fi
	if wants fix-off; then
		# fix off (0063): the small pool's ageing against the nv30 files as they were before it
		O=$T/transfer-before D=src/gallium/drivers/nouveau/nv30
		rm -rf $O; mkdir -p $O/nv30
		c=$(git -C $M log --format='%H %s' | grep -F " nv30: the small staging pool gives back" | head -n 1 | cut -d' ' -f1)
		if [ -z "$c" ]; then
			echo "SKIP ${TEST_SUITE:-mesa-transfer-cpu}:fix-off no 0063 commit in $M"; result fix-off 77
		else
			for f in nv30_miptree.c nv30_screen.h nv30_staging_policy.h; do git -C $M show "$c^:$D/$f" > $O/nv30/$f || exit 1; done
			mcc $O/test $t/transfer-cpu-test.c -Wall -Werror -I$O &&
			expect_fail "0063 off: idle small-pool buffers" 'FAIL .*after a put 1.2 s on' $O/test
			result fix-off $?
		fi
	fi
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
	PD=; grep -q nv30_ps3_fp_gart_default $M/src/gallium/drivers/nouveau/nv30/nv30_screen.h && PD=-DNV30_HAVE_GART_DEFAULT	# 0069
	grep -q nv30_idxbuf_to_gart $M/src/gallium/drivers/nouveau/nv30/nv30_resource.c && PD="$PD -DNV30_HAVE_IDX_MIGRATE"	# 0070
	grep -q nv30_idxbuf_swap $M/src/gallium/drivers/nouveau/nv30/nv30_resource.c && PD="$PD -DNV30_HAVE_IDX_MOVE2"	# 0071
	# shellcheck disable=SC2086
	mcc $T/placement-test $t/placement-test.c -Wall -Werror -Wno-unused-function $PD || { echo "placement-test.c does not build against $M"; wants state && result state 1; wants golden && result golden 1; exit 1; }
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
	# 0069, 0070: the defaults; the move of an index buffer found in VRAM
	if wants default; then
		if [ -z "$PD" ]; then result default 77; else $T/placement-test default; result default $?; fi
	fi
	if wants migrate; then
		if [[ $PD != *IDX_MIGRATE* ]]; then result migrate 77; else $T/placement-test migrate; result migrate $?; fi
	fi
	# 0069: every move of a fragment program's buffer to VRAM is under !fp_gart (the program's and the blit's)
	if wants blit; then
		if [ -z "$PD" ]; then result blit 77; else
			n=$(grep -B2 'nouveau_buffer_migrate(.*NOUVEAU_BO_VRAM)' $M/src/gallium/drivers/nouveau/nv30/nv30_fragprog.c \
				$M/src/gallium/drivers/nouveau/nv30/nv30_transfer.c | grep -c 'fp_gart')
			m=$(cat $M/src/gallium/drivers/nouveau/nv30/nv30_fragprog.c $M/src/gallium/drivers/nouveau/nv30/nv30_transfer.c |
				grep -c 'nouveau_buffer_migrate(.*NOUVEAU_BO_VRAM)')
			[ "$n" = "$m" ] && [ "$m" = 2 ] && echo "ok   blit: both moves of a program to VRAM (the program's, the blit's) are under !fp_gart"
			result blit $([ "$n" = "$m" ] && [ "$m" = 2 ]; echo $?)
		fi
	fi
	# 0070, 0071: the call sites placement-test can't compile, in the nv30 files of DIR: the draw hook (under
	# idx_gart, for indexed draws from a buffer, the draw dropped when the buffer can't be in GART), both screen
	# defaults through their helpers, both swtnl maps synchronized for a buffer the GPU still writes
	wiring_check() {	# wiring_check DIR: 0, or FAIL lines and 1
		local V=$1 rc=0
		tr -d '\n' < $V/nv30_vbo.c | grep -q 'if (unlikely(nv30->screen->idx_gart) && info->index_size > 1 && !info->has_user_indices &&  *!nv30_idxbuf_to_gart(nv30, nv04_resource(info->index.resource)))  *return;' ||
			{ echo "FAIL wiring: the draw hook in nv30_vbo.c"; rc=1; }
		grep -q 'debug_get_bool_option("NV30_PS3_IDX_GART", nv30_ps3_idx_gart_default(ps3))' $V/nv30_screen.c ||
			{ echo "FAIL wiring: NV30_PS3_IDX_GART's default in nv30_screen.c"; rc=1; }
		grep -q 'debug_get_bool_option("NV30_PS3_FP_GART", nv30_ps3_fp_gart_default(ps3))' $V/nv30_screen.c ||
			{ echo "FAIL wiring: NV30_PS3_FP_GART's default in nv30_screen.c"; rc=1; }
		{ [ "$(grep -c 'nv30_render_map_flags(nv30, ' $V/nv30_draw.c)" = 2 ] &&
			tr -d '\n' < $V/nv30_draw.c | grep -q 'NOUVEAU_BUFFER_STATUS_GPU_WRITING))      return PIPE_MAP_READ;'; } ||
			{ echo "FAIL wiring: the swtnl maps in nv30_draw.c"; rc=1; }
		return $rc
	}
	if wants wiring; then
		if [[ $PD != *IDX_MOVE2* ]]; then result wiring 77; else
			wiring_check $M/src/gallium/drivers/nouveau/nv30 && echo "ok   wiring: the draw hook, both screen defaults, both swtnl maps"
			result wiring $?
		fi
	fi
	if wants fix-off; then
		if [[ $PD != *IDX_MIGRATE* ]]; then result fix-off 77; else
			# nv30_resource.c with one rule of 0070/0071 off at a time, the defaults as before 0069/0070, the call
			# sites taken out: each must fail
			O=$(realpath -m $T/placement-before); rm -rf $O; mkdir -p $O/nv30; rc=0
			V=$M/src/gallium/drivers/nouveau/nv30
			R=$V/nv30_resource.c
			if [[ $PD == *IDX_MOVE2* ]]; then
				variants=(
					"kick|/^nv30_idxbuf_kick/,/^}/{/PUSH_KICK(push);/d}|not kicked"
					"cpu_only|/NV30_NEW_ARRAYS;/{x;/^$/!{x;b};x};/^   res->cpu_only = 1;$/d|cpu_only 0"
					"range|/util_range_add(&res->base, &res->valid_buffer_range, 0, res->base.width0);/d|valid range"
					"fence|/nouveau_fence_ref(fence, &res->fence_wr, &screen->base);/d|no write fence"
					"cpu-fill|s/   if (res->data \&\& !(res->status \& (NOUVEAU_BUFFER_STATUS_DIRTY |/   if (0 \&\& res->data \&\& !(res->status \& (NOUVEAU_BUFFER_STATUS_DIRTY |/|move by the CPU"
					"stream-cpu_only|/^      res->cpu_only = 1;$/d|stream: not cpu_only"
					"queued-kick|s/if (res->fence_wr \&\& res->fence_wr == nv30->base.fence) {/if (0) {/|queued GPU write not kicked"
					"gart-wait|/^      nouveau_fence_wait_idle(&nv30->base);$/d|GART full once"
					"lost-copy|s/if (NOUVEAU_CANCELLED(dev) != cancelled) {/if (0 \&\& NOUVEAU_CANCELLED(dev) != cancelled) {/|a lost copy"
					"shared|s/if (res->base.bind \& PIPE_BIND_SHARED)/if (0)/|a shared buffer moved")
			else
				variants=("kick|/^bool$/,/^}$/{/PUSH_KICK(nv30->base.pushbuf);/d}|not kicked"
					  "cpu_only|/^bool$/,/^}$/{/res->cpu_only = 1;/d}|cpu_only 0")
			fi
			for e in "${variants[@]}"; do
				v=${e%%|*}; rest=${e#*|}; sx=${rest%|*}; re=${rest##*|}
				sed "$sx" $R > $O/nv30/nv30_resource.c
				if cmp -s $R $O/nv30/nv30_resource.c; then echo "FAIL fix-off $v: the edit matched nothing"; rc=1; continue; fi
				cp $t/placement-test.c $O/test.c
				sed -i 's|#include "nv30/nv30_resource.c"|#include "'$O'/nv30/nv30_resource.c"|' $O/test.c
				# shellcheck disable=SC2086
				mcc $O/test-$v $O/test.c -Wall -Werror -Wno-unused-function $PD -I$t &&
					expect_fail "0070/0071 without the $v" "FAIL .*$re" $O/test-$v migrate || rc=1
			done
			cp $V/*.h $O/nv30/ 2>/dev/null; sed -i 's/^   return ps3;$/   return false;/' $O/nv30/nv30_screen.h
			# shellcheck disable=SC2086
			mcc $O/test-default $t/placement-test.c -Wall -Werror -Wno-unused-function $PD -I$O &&
				expect_fail "0069/0070 defaults off" "FAIL .*default: PS3 0" $O/test-default default || rc=1
			if [[ $PD == *IDX_MOVE2* ]]; then
				W=$O/wiring; mkdir -p $W
				for v in hook idx-default fp-default swtnl; do
					cp $V/nv30_vbo.c $V/nv30_screen.c $V/nv30_draw.c $W/
					case $v in
					hook) sed -i '/!nv30_idxbuf_to_gart(nv30, nv04_resource(info->index.resource)))/,+1d' $W/nv30_vbo.c ;;
					idx-default) sed -i 's/"NV30_PS3_IDX_GART", nv30_ps3_idx_gart_default(ps3)/"NV30_PS3_IDX_GART", false/' $W/nv30_screen.c ;;
					fp-default) sed -i 's/"NV30_PS3_FP_GART", nv30_ps3_fp_gart_default(ps3)/"NV30_PS3_FP_GART", false/' $W/nv30_screen.c ;;
					swtnl) sed -i 's/      return PIPE_MAP_READ;/      return PIPE_MAP_UNSYNCHRONIZED | PIPE_MAP_READ;/' $W/nv30_draw.c ;;
					esac
					expect_fail "0070/0071 wiring without the $v" "FAIL wiring" wiring_check $W || rc=1
				done
			fi
			result fix-off $rc
		fi
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
submit-fail)
	[ -e $M/src/gallium/winsys/nouveau/drm/nouveau.c ] || skip "no Mesa tree at $M (make mesa-work)"
	# nouveau.c wants libdrm's xf86drm.h, which a host without libdrm lacks: a stub declares what it calls
	SD=$T/submit-fail-stub; mkdir -p $SD
	cat > $SD/xf86drm.h <<'XEOF'
#include <stdint.h>
#include <stddef.h>
typedef struct { int version_major, version_minor, version_patchlevel; } drmVersion, *drmVersionPtr;
#define DRM_BUS_PCI 0
typedef struct { uint16_t domain; uint8_t bus, dev, func; } drmPciBusInfo, *drmPciBusInfoPtr;
typedef struct { uint16_t device_id; uint8_t revision_id; } drmPciDeviceInfo, *drmPciDeviceInfoPtr;
typedef struct { int bustype; union { drmPciBusInfoPtr pci; } businfo; union { drmPciDeviceInfoPtr pci; } deviceinfo; } drmDevice, *drmDevicePtr;
int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *d);
int drmCommandWriteRead(int fd, unsigned long idx, void *data, unsigned long size);
int drmCommandWrite(int fd, unsigned long idx, void *data, unsigned long size);
int drmIoctl(int fd, unsigned long req, void *arg);
int drmCloseBufferHandle(int fd, uint32_t handle);
void drmFreeDevice(drmDevicePtr *d);
drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr v);
int drmPrimeFDToHandle(int fd, int pfd, uint32_t *h);
int drmPrimeHandleToFD(int fd, uint32_t h, uint32_t flags, int *pfd);
XEOF
	SI="-I$SD -I$M/src/nouveau/headers/nvidia/classes -I$M/src/nouveau/drm"
	sf() { mcc "$1" $t/submit-fail-test.c -Wall -Werror -Wno-unused-function -pthread -Wl,--unresolved-symbols=ignore-all $SI "${@:2}"; }
	gen || exit 1
	export ASAN_OPTIONS=detect_leaks=0
	built=
	for p in ecanceled failed notify ratelimit ok threads; do
		wants $p || continue
		[ -n "$built" ] || { sf $T/submit-fail-test && built=ok || built=no; }
		if [ $built = ok ]; then $T/submit-fail-test $p; result $p $?; else echo "submit-fail-test.c does not build against $M"; result $p 1; fi
	done
	if wants fix-off; then
		O=$T/submit-fail-before; rm -rf $O; mkdir -p $O
		W=src/gallium/winsys/nouveau/drm
		c=$(git -C $M log --format='%H %s' | grep -F " nouveau: a submission the kernel fails counts as cancelled" | head -n 1 | cut -d' ' -f1)
		if [ -z "$c" ]; then
			echo "SKIP mesa-submit-fail:fix-off no 0061 commit in $M"; result fix-off 77
		else
			cp -r $M/$W/. $O/ && git -C $M show "$c^:$W/nouveau.c" > $O/nouveau.c && git -C $M show "$c^:$W/nouveau.h" > $O/nouveau.h &&
			sf $O/test -I$O &&
			expect_fail "0061 off: a failed submission is not counted" 'FAIL .*EINVAL: cancelled is 0' $O/test failed &&
			expect_fail "0061 off: the count under threads" 'FAIL .*were counted' $O/test threads &&
			expect_fail "0061 off: a failed fence" 'FAIL .*notify' $O/test notify &&
			expect_fail "0061 off: the message at every failure" 'FAIL .*ratelimit' $O/test ratelimit
			result fix-off $?
		fi
	fi
	;;
bo-domain)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_winsys.h ] || skip "no Mesa tree at $M (make mesa-work)"
	grep -q 'bool ps3;' $M/src/gallium/winsys/nouveau/drm/nouveau.h || {
		echo "no nouveau_device::ps3 in this tree (before 0064)"; wants state && result state 1; wants fix-off && result fix-off 77; exit 1; }
	gen || exit 1
	export ASAN_OPTIONS=detect_leaks=0
	U=-Wl,--unresolved-symbols=ignore-all
	TD=; grep -q nv30_tex_invalidate_always_default $M/src/gallium/drivers/nouveau/nv30/nv30_screen.h && TD=-DNV30_HAVE_TEX_DEFAULT	# 0066
	if wants state; then mcc $T/bo-domain-test $t/bo-domain-test.c -Wall -Werror $U $TD && $T/bo-domain-test; result state $?; fi
	if wants fix-off; then
		O=$T/bo-domain-before; rm -rf $O; mkdir -p $O/nv30
		c=$(git -C $M log --format='%H %s' | grep -F " nv30: textures and render targets are addressed in VRAM" | head -n 1 | cut -d' ' -f1)
		if [ -z "$c" ]; then echo "SKIP mesa-bo-domain:fix-off no 0064 commit in $M"; result fix-off 77
		else
			cp $M/src/gallium/drivers/nouveau/nv30/*.h $O/nv30/ &&
			git -C $M show "$c^:src/gallium/drivers/nouveau/nv30/nv30_winsys.h" > $O/nv30/nv30_winsys.h &&
			mcc $O/test $t/bo-domain-test.c -Wall -Werror $U -I$O &&
			expect_fail "0064 off: a GART-flagged buffer off the PS3" 'FAIL .*not the PS3' $O/test
			result fix-off $?
		fi
	fi
	;;
fragprog)
	[ -e $M/src/gallium/drivers/nouveau/nv30/nvfx_fragprog.c ] || skip "no Mesa tree at $M (make mesa-work)"
	grep -q 'txb_hw' $M/src/gallium/drivers/nouveau/nv30/nv30_state.h || {
		echo "no nv30_fragprog::txb_hw in this tree (before 0065)"; for p in txb rep end; do wants $p && result $p 1; done; wants fix-off && result fix-off 77; exit 1; }
	gen || exit 1
	export ASAN_OPTIONS=detect_leaks=0
	FP="$M/src/gallium/drivers/nouveau/nv30/nvfx_fragprog.c $TGSI $M/src/util/ralloc.c $M/src/util/u_dynarray.c"
	built=
	for p in txb rep end; do
		wants $p || continue
		[ -n "$built" ] || { mcc $T/fragprog-test $t/fragprog-test.c $FP -Wall -Werror -Wno-unused-function && built=ok || built=no; }
		if [ $built = ok ]; then $T/fragprog-test $p; result $p $?; else echo "fragprog-test.c does not build against $M"; result $p 1; fi
	done
	if wants golden; then
		# the PS3's translation (txb_hw off) of eight programs against nvfx_fragprog.c as it was before 0065 and before
		# 0067: before 0065 nothing differs; before 0067 only the loop whose last instruction reads a constant
		# (program 5), and in one word, its end
		O=$T/fragprog-golden; rm -rf $O; mkdir -p $O; rc=0
		c65=$(git -C $M log --format='%H %s' | grep -F " nv30: TXB is the hardware's TXB off the PS3" | head -n 1 | cut -d' ' -f1)
		c67=$(git -C $M log --format='%H %s' | grep -F " nv30: a fragment program loop ends on an instruction slot" | head -n 1 | cut -d' ' -f1)
		if [ -z "$c65" ] || [ -z "$c67" ]; then echo "SKIP mesa-fragprog:golden no 0065/0067 commit in $M"; result golden 77
		else
			mcc $O/new $t/fragprog-test.c $FP -Wall -Werror -Wno-unused-function || rc=1
			git -C $M show "$c65^:src/gallium/drivers/nouveau/nv30/nvfx_fragprog.c" > $O/a.c &&
			mcc $O/a $t/fragprog-test.c $O/a.c $TGSI $M/src/util/ralloc.c $M/src/util/u_dynarray.c -I$M/src/gallium/drivers/nouveau/nv30 -Wall -Werror -Wno-unused-function || rc=1
			git -C $M show "$c67^:src/gallium/drivers/nouveau/nv30/nvfx_fragprog.c" > $O/b.c &&
			mcc $O/b $t/fragprog-test.c $O/b.c $TGSI $M/src/util/ralloc.c $M/src/util/u_dynarray.c -I$M/src/gallium/drivers/nouveau/nv30 -Wall -Werror -Wno-unused-function || rc=1
			if [ $rc = 0 ]; then
				$O/new dump > $O/new.txt && $O/a dump > $O/a.txt && $O/b dump > $O/b.txt || rc=1
				if [ $rc = 0 ]; then
					# 0065 and 0067 are both in the new file: compare with the file before 0065 minus 0067's own change
					d=$(diff $O/a.txt $O/new.txt | grep -c '^>')
					p=$(diff $O/a.txt $O/new.txt | grep '^>' | awk '{print $2}' | sort -u | tr '\n' ' ')
					e=$(diff $O/b.txt $O/new.txt | grep '^>' | awk '{print $2}' | sort -u | tr '\n' ' ')
					if [ "$d" = 1 ] && [ "$p" = "5 " ] && [ "$e" = "5 " ]; then
						echo "ok   golden: $(wc -l < $O/new.txt) lines of eight programs; the PS3 translation differs from before 0065 and 0067 in one word, the end of program 5's loop"
					else
						echo "golden: $d words differ from before 0065 (programs: $p), programs differing from before 0067: $e"; diff $O/a.txt $O/new.txt | head; rc=1
					fi
				fi
			fi
			result golden $rc
		fi
	fi
	if wants fix-off; then
		# fix off: nvfx_fragprog.c as it was before each patch (and nv30_state.h as it is: the field stays)
		O=$T/fragprog-before; rm -rf $O; mkdir -p $O
		rc=0
		for pair in "0065|nv30: TXB is the hardware's TXB off the PS3|txb|FAIL .*txb_hw on" "0067|nv30: a fragment program loop ends on an instruction slot|rep|FAIL .*not an" "0068|nv30: a fragment program that closes an IF or a loop|end|FAIL"; do
			IFS='|' read -r n subj part re <<<"$pair"
			c=$(git -C $M log --format='%H %s' | grep -F " $subj" | head -n 1 | cut -d' ' -f1)
			[ -n "$c" ] || { echo "no commit \"$subj\" in $M"; rc=1; continue; }
			git -C $M show "$c^:src/gallium/drivers/nouveau/nv30/nvfx_fragprog.c" > $O/$n.c &&
			mcc $O/$n-test $t/fragprog-test.c $O/$n.c $TGSI $M/src/util/ralloc.c $M/src/util/u_dynarray.c -I$M/src/gallium/drivers/nouveau/nv30 -Wall -Werror -Wno-unused-function &&
			expect_fail "$n off: $part" "FAIL" $O/$n-test $part || rc=1
		done
		result fix-off $rc
	fi
	;;
idx-split)
	H=$M/src/gallium/drivers/nouveau/nv30/nv30_idx_split.h V=$M/src/gallium/drivers/nouveau/nv30/nv30_vbo.c
	[ -e $H ] || skip "no $M with 0072 (nv30_idx_split.h; make mesa-work)"
	export ASAN_OPTIONS=detect_leaks=0
	scc() { gcc -g -O1 -std=gnu11 -Wall -Werror -fsanitize=address,undefined -D_GNU_SOURCE \
		-DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 "$@" -I$M/include -I$M/src -I$M/src/gallium/include \
		-I$M/src/gallium/drivers/nouveau -I$M/src/util -o $T/idx-split-test${O:+-$O} $t/idx-split-test.c; }
	if wants model || wants cap; then
		O= scc || exit 1
		for p in model cap; do wants $p && { $T/idx-split-test $p; result $p $?; }; done
	fi
	if wants wiring; then
		rc=0
		w() { grep -qF -- "$2" "$1" || { echo "FAIL wiring: $(basename "$1") lacks: $2"; rc=1; }; }
		w $V 'unlikely(nv30->screen->idx_split) &&'
		w $V 'nv30_idx_split(mode, count, NV30_PS3_IDX_PIECE_BYTES / isz, restart,'
		w $V 'draw_count = nv30_idx_convert(info->mode, draws[0].count, info->primitive_restart,'
		w $V 'nv30->rast && nv30->rast->pipe.flatshade_first, map);'
		w $V 'PUSH_RESRC(push, NV30_3D(IDXBUF_OFFSET), BUFCTX_IDXBUF, e->res, e->offset + (uint32_t)at,'
		w $V '!idx_conv && info->primitive_restart);'
		w $M/src/gallium/drivers/nouveau/nv30/nv30_screen.c \
			'screen->idx_split = ps3 && debug_get_bool_option("NV30_PS3_IDX_SPLIT", true);'
		[ $rc = 0 ] && echo "wiring: the draw path and the screen default as they must be"
		result wiring $rc
	fi
	if wants fix-off; then
		F=$T/idx-split-off rc=0; rm -rf $F; mkdir -p $F/nv30
		off() {	# off NAME FROM TO: the header with FROM made TO must fail the test
			cp $H $F/nv30/ && FROM=$2 TO=$3 perl -0777 -i -pe \
				'my $n = () = /\Q$ENV{FROM}\E/g; $n == 1 or die "idx-split fix-off: $n matches\n"; s/\Q$ENV{FROM}\E/$ENV{TO}/' \
				$F/nv30/nv30_idx_split.h || { rc=1; return; }
			O=$1 scc -I$F -w -Wno-error || { rc=1; return; }
			expect_fail "0072 off: $1" '^FAIL ' $T/idx-split-test-$1 || rc=1
		}
		off align '            e -= (e - seg) % unit;' '            ;'
		off margin '(NV30_PS3_IDX_BINDING_MAX - 4096)' '(NV30_PS3_IDX_BINDING_MAX)'
		off strip-overlap $'case MESA_PRIM_TRIANGLE_STRIP: unit = 2; overlap = 2; break;' $'case MESA_PRIM_TRIANGLE_STRIP: unit = 2; overlap = 0; break;'
		off restart-first '         seg = s + 1;' '         ;'
		off restart-cut '         s = seg = e;' '         s = e;'
		off provoking 'bool c_first = mode == MESA_PRIM_POLYGON ? first : !first;' 'bool c_first = !first;'
		off loop-close $'            out[o++] = nv30_idx_get(idx, isz, i - 1);\n            out[o++] = nv30_idx_get(idx, isz, s0);\n' ''
		result fix-off $rc
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
