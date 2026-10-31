#!/bin/bash
# run-tests.sh [--slow] [--only NAME]: every host test suite (no PS3 involved), then a table of
# suite, result and seconds; exit 1 when any failed. Output of each suite: build/tmp/tests/<suite>.log
# (shown for failures). A suite whose prerequisites are missing is "skip", not a failure.
#   default  quick suites: bench-selftest (bench/tests/selftest.py), bench-launcher (bench/tests/test_launcher.py:
#            launcher.py and session.sh's choice of a menu launcher), gprefetch, rt-audio (ps3-rt-audio's choice of
#            threads, a stand-in game: packages/walnut/system/ps3-desktop/tests/rt-audio-check.sh), ps3rsx-shadow (and ps3rsx's clear_dcbz zeroing
#            and the validator's vertex format checks),
#            ps3-spu, xmoto-glbatch,
#            mesa-forcedxt (Mesa 0038's NV30_FORCE_DXT decision, packages/patched/graphics/mesa/tests), mesa-staging-pool
#            (Mesa 0040's NV30_STAGING_POOL_MB decisions and a model of the pool, packages/patched/graphics/mesa/tests),
#            mesa-vtx-snorm8 (Mesa 0043's NV30_VTX_SNORM8 vertex program normalization against
#            util_format's unpack, every byte; the per-input key; the translator's output run on
#            an NV40 vertex program interpreter with and without it), mesa-minmax-vmx (Mesa 0044's
#            AltiVec index min/max against the scalar loops, on the host and as ppc64 under qemu),
#            mesa-query-steal (Mesa 0046: nv30's queries when the report slots run out, against a
#            fake GPU, under AddressSanitizer), mesa-alloc-fail (0047-0049's alloc-fail-test: u_upload_mgr,
#            nouveau_mm and nouveau_fence when buffers cannot be had; 0003's context-pushbuf-test: nv30's flush
#            before any draw and destroy after a failed init; each fix off too, the file as it was before
#            its patch, which must fail: 0002, 0003, 0047, 0048; 0053's fence wait after a cancelled submission), mesa-transfer-cpu (0049's
#            transfer-cpu-test: nv30 texture uploads and reads by the CPU when the GART is full), mesa-point-smooth
#            (Mesa 0050's round GL_POINT_SMOOTH points: the fragment program variant on Mesa's
#            TGSI interpreter over each point's square, and its NV40 translation),
#            sdl2-compat-close-locked (sdl2-compat 0001: SDL_CloseAudio while locked, SDL 1.2's and
#            SDL2's API, dummy driver, a timeout; our build passes, the stock tarball's hangs), luajit-low-2gb
#            (luajit 0001: luaL_newstate() 100 times under qemu-ppc64, ours never fails, stock does),
#            ssdimg (the SSD test
#            drive's stamp.py, ps3-imgmap on a fake UFS layer, ps3-vswap's device-mapper backing;
#            ps3-imgmap-boot's maps and tables against ps3-imgmap's; both tools' write guard
#            registration against a model of the kernel's side), ps3-boot (ps3-update-kboot's
#            entries and its /etc/kboot.d extras on a fake root), agent-guard (tools/hooks/agent-guard.py, the PreToolUse hook: refused and allowed commands), ps3-lease (tools/ps3's lease, and deploy.sh and play-test.sh refusing under someone else's), leak-check (tools/leak-check.py on
#            synthetic packages and sources), repo-tools (tools/test_repo_tools.py: pkg-dir.sh,
#            licensed.py, deb-version.sh's patterns, doclinks.py, blocks.py, image-packages.sh on small
#            fake trees), licensed (tools/licensed.py check on this tree), workflows
#            (github-ci/gen-workflows.py --check: .github/workflows is what it writes), blocks
#            (tools/gamedb/blocks.py --check: no stale generated block), doclinks (tools/doclinks.py:
#            the links that don't resolve, a warning only for now), game-mentions (tools/gamedb/game-mentions.py:
#            sections of the docs that name a game without a GameDB link, a warning only), walnut-repo (apt-repo/walnut-repo.py against the real
#            walnut-watch: testing, stable and the password-protected licensed suite), walnut-watch
#            (the server's publisher on good and hostile uploads), batch-publish (github-ci/batch-publish.py,
#            testing's one publisher: its plan, and whole runs through publish.sh against a fake
#            GitHub API and the real walnut-watch), gamedb (Walnut Games' scoring against the calibration
#            table in docs/GAMEDB.md, check.py, import-run.py, catalog.py), gamedb-evidence (a played
#            session's evidence: tools/gamedb/test_evidence.py, the scrub and scan, import and re-import;
#            then reimport.py --check: every record of the repository derived again from its evidence
#            alone, unchanged), gamedb-technical (tools/gamedb/technical.py: the site's technical
#            pages, drafts left out, links, the benchmark tables, the public-safe scan), gamedb-schema
#            (tools/gamedb/test_schema.py: each gamedb/schemas kind's good and bad fixtures, every gamedb file against
#            its schema and of exactly one kind), walnut-games (the app and its root
#            helper: validation, remove-cascade refusal, Exec parsing, a window smoke test), walnut-data (the data import:
#            the helper's import-data against tar fixtures and its lock, remove-data, install-repo-data, walnut-data
#            with a stubbed game-data-packager), walnut-rate (its root helper's refusals and protocol, ps3-bench.sh's
#            helper mode with a fake sudo, the clean-system check and the watch on made-up process lists and windows,
#            the export's scrub and an export -> import-run.py round trip, the submission check's refusals and a
#            submit -> pull request -> submissions.py show/accept round trip with gh stubbed), ps3disk-guard (kernel local/0045's
#            range checks, packages/patched/kernel/linux/tests/ps3disk-guard.sh), game-data-packager (our patches'
#            --progress-fd and --no-launchers on the built deb, in the deb builder image: packages/patched/tools/
#            game-data-packager/tests; skipped without build/debs/game-data-packager)
#   --slow   also autogm (packages/walnut/system/ps3-desktop/tests/autogm-check.sh: needs Xvfb+openbox or docker/podman) and
#            ssdimg-vm (tools/ssdimg/vm-e2e.sh: ps3-imgmap on a real big-endian UFS2 image in a KVM
#            guest of the host's kernel, then Phase B: ps3-imgmap-boot's devices, the initramfs
#            booting root from the image, refused boots; the second Phase B geometry behind the
#            kernel guard, simulated by a module built from local/0045 for the host kernel (needs its
#            headers, else that part is skipped); needs docker, qemu-system-x86_64, /dev/kvm)
# A suite whose prerequisites are missing exits 77 and is "skip"; a part of a suite that can't run prints
# "SKIP suite:part reason" and goes on. Both, and unittest's skipped tests, are listed after the table,
# so a skip never reads as a pass. A known-failing suite (the xfail list below: the repro of a bug the
# shipped code doesn't fix yet) must fail with its expected exit code and message ("xfail"); a pass
# (XPASS: fixed? take it off the list) or any other failure is reported and fails the run.
set -u
cd "$(dirname "$0")/.."
slow=0; only=""
while [ $# -gt 0 ]; do
	case $1 in --slow) slow=1;; --only) only=$2; shift;; *) echo "usage: $0 [--slow] [--only NAME]" >&2; exit 2;; esac
	shift
done
L=build/tmp/tests; mkdir -p "$L"
names=(); res=(); secs=(); skips=(); fail=0

# known-failing suites: name -> "exit code|extended regex the log must contain"
declare -A xfail=(
)

run() {	# run NAME CMD...: skip when the command is 77 (autotools convention for "skipped")
	local n=$1; shift
	[ -z "$only" ] || [ "$only" = "$n" ] || return 0
	local t0=$SECONDS rc x line
	printf '== %s\n' "$n"
	TEST_SUITE=$n "$@" > "$L/$n.log" 2>&1; rc=$?
	names+=("$n"); secs+=($((SECONDS - t0)))
	while IFS= read -r line; do skips+=("$line"); done < <(grep -a '^SKIP ' "$L/$n.log")
	x=$(grep -aoE '^OK \(skipped=[0-9]+' "$L/$n.log" | tail -n 1)
	[ -z "$x" ] || skips+=("SKIP $n:unittest ${x#OK (skipped=} tests skipped (see its log)")
	if [ $rc = 77 ]; then
		res+=(skip); sed -n '$p' "$L/$n.log"
		grep -aq '^SKIP ' "$L/$n.log" || skips+=("SKIP $n $(sed -n '$s/^skip: //p' "$L/$n.log")")
	elif [ -n "${xfail[$n]:-}" ]; then
		if [ "$rc" = "${xfail[$n]%%|*}" ] && grep -aqE -- "${xfail[$n]#*|}" "$L/$n.log"; then res+=(xfail)
		elif [ $rc = 0 ]; then res+=(XPASS); fail=1; echo "   known-failing suite passed: fixed? take it off the xfail list"
		else res+=(FAIL); fail=1; echo "   known-failing suite: exit $rc, want ${xfail[$n]%%|*} with /${xfail[$n]#*|}/"
			tail -n 25 "$L/$n.log"; fi
	elif [ $rc = 0 ]; then res+=(pass)
	else res+=(FAIL); fail=1; tail -n 25 "$L/$n.log"; fi
}
need() { command -v "$1" >/dev/null || { echo "skip: no $1"; exit 77; }; }
skip_part() { echo "SKIP ${TEST_SUITE:-?}:$1 ${*:2}"; }	# skip_part PART REASON: a part that can't run here
expect_fail() {	# expect_fail WHAT REGEX CMD...: a run with a fix off must fail, with REGEX in its output
	local what=$1 re=$2 out rc; shift 2
	out=$("$@" 2>&1); rc=$?
	if [ $rc != 0 ] && grep -qE -- "$re" <<<"$out"; then echo "ok   $what: fails as expected (exit $rc)"; return 0; fi
	printf '%s\n' "$out" | tail -n 20; echo "FAIL $what: exit $rc, want a failure with /$re/"; return 1
}

t_ps3rsx() {
	O=$PWD/build/tmp/ps3rsx-test
	need gcc
	make -C packages/walnut/drivers/ps3rsx/test O="$O" "$O/shadow-equiv" "$O/shadow-equiv-fast" "$O/zero-test" "$O/validate-test" || exit 1
	"$O/shadow-equiv" 200000 20000 1 || exit 1
	"$O/shadow-equiv-fast" 1000000 100000 2 || exit 1
	"$O/zero-test" 20000 1 || exit 1
	"$O/validate-test" || exit 1
	# each rule switched off in a copy of validate.c (or the submit model's old behaviour): its case must fail
	make -C packages/walnut/drivers/ps3rsx/test O="$O" fix-off || exit 1
	# big-endian run under qemu, when the cross compiler, the sysroot and qemu are there
	if command -v powerpc64-linux-gnu-gcc >/dev/null && command -v qemu-ppc64-static >/dev/null &&
	   [ -d build/mesa/sysroot ]; then
		make -C packages/walnut/drivers/ps3rsx/test O="$O" "$O/shadow-equiv-ppc64" "$O/zero-test-ppc64" || exit 1
		qemu-ppc64-static "$O/shadow-equiv-ppc64" 100000 10000 3 || exit 1
		qemu-ppc64-static "$O/zero-test-ppc64" 20000 2 || exit 1
	else skip_part ppc64 "no cross gcc, qemu or sysroot"; fi
}
t_xmoto() {
	[ -e build/work/xmoto/src/drawlib/GLBatch.cpp ] || { echo "skip: no build/work/xmoto (make xmoto-work)"; exit 77; }
	need g++
	"$(tools/pkg-dir.sh xmoto)/test/glbatch-test.sh" 100
}
t_forcedxt() {
	[ -e build/work/mesa/src/mesa/main/texforcedxt_policy.h ] || { echo "skip: no build/work/mesa with patch 0038 (make mesa-work)"; exit 77; }
	need gcc
	mkdir -p build/tmp/tests
	gcc -O2 -Wall -Wextra -Werror -I build/work/mesa/include -I build/work/mesa/src \
		-o build/tmp/tests/forcedxt-policy-test packages/patched/graphics/mesa/tests/forcedxt-policy-test.c || exit 1
	build/tmp/tests/forcedxt-policy-test
}
t_stagingpool() {
	[ -e build/work/mesa/src/gallium/drivers/nouveau/nv30/nv30_staging_policy.h ] || { echo "skip: no build/work/mesa with patch 0040 (make mesa-work)"; exit 77; }
	need gcc
	mkdir -p build/tmp/tests
	gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I build/work/mesa/src/gallium/drivers/nouveau \
		-o build/tmp/tests/staging-pool-test packages/patched/graphics/mesa/tests/staging-pool-test.c || exit 1
	build/tmp/tests/staging-pool-test
}
t_vtxsnorm8() {
	local M=build/work/mesa T=build/tmp/tests
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_vtx_snorm8.h ] || { echo "skip: no build/work/mesa with the NV30_VTX_SNORM8 patch (make mesa-work)"; exit 77; }
	need gcc
	mkdir -p $T
	# util_format's SNORM8 structs and unpack functions, as Mesa generates them
	python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml > $T/u_format_table.c || exit 1
	awk '/^struct util_format_r8(g8(b8)?)?_snorm \{/,/^\};/ { print }
	     /^util_format_r8(g8(b8(a8)?)?)?_snorm_unpack_rgba_float\(/ { print "static void"; p = 1 }
	     p { print } p && /^\}/ { p = 0 }' $T/u_format_table.c > $T/snorm8-ref.h
	[ "$(grep -c '_unpack_rgba_float(' $T/snorm8-ref.h)" = 4 ] || { echo "snorm8-ref.h: unpack functions not found"; exit 1; }
	gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I $M/src/gallium/drivers/nouveau -I $T \
		-DUTIL_ARCH_BIG_ENDIAN=0 -o $T/vtx-snorm8-test packages/patched/graphics/mesa/tests/vtx-snorm8-test.c || exit 1
	$T/vtx-snorm8-test || exit 1
	# the translator itself (nvfx_vertprog.c) on an NV40 vertex program interpreter
	local G=$T/mesa-gen N=$M/src/compiler/nir A=$M/src/gallium/auxiliary/tgsi
	mkdir -p $G/util/format
	python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml --enums > $G/util/format/u_format_gen.h &&
	python3 $M/src/compiler/builtin_types_h.py $G/builtin_types.h &&
	python3 $N/nir_opcodes_h.py > $G/nir_opcodes.h && python3 $N/nir_builder_opcodes_h.py > $G/nir_builder_opcodes.h &&
	python3 $N/nir_intrinsics_h.py --out $G/nir_intrinsics.h &&
	python3 $N/nir_intrinsics_indices_h.py --out $G/nir_intrinsics_indices.h || exit 1
	gcc -g -O1 -std=gnu11 -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE -DHAVE_TIMESPEC_GET \
		-DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$G -I$M/include -I$M/src \
		-I$M/src/gallium/include -I$M/src/gallium/auxiliary -I$M/src/gallium/drivers/nouveau -I$M/src/util \
		-I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir -I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm \
		-I$M/src/nouveau/headers -o $T/vtx-snorm8-vpsim packages/patched/graphics/mesa/tests/vtx-snorm8-vpsim.c packages/patched/graphics/mesa/tests/vtx-snorm8-stubs.c \
		$M/src/gallium/drivers/nouveau/nv30/nvfx_vertprog.c $A/tgsi_text.c $A/tgsi_build.c $A/tgsi_info.c \
		$A/tgsi_util.c $A/tgsi_strings.c $A/tgsi_parse.c $A/tgsi_dump.c $A/tgsi_iterate.c $A/tgsi_scan.c \
		$M/src/util/ralloc.c $M/src/util/u_dynarray.c -lm || exit 1
	ASAN_OPTIONS=detect_leaks=0 $T/vtx-snorm8-vpsim
}
t_minmaxvmx() {
	local M=build/work/mesa T=build/tmp/tests
	[ -e $M/src/mesa/vbo/vbo_minmax_vmx.h ] || { echo "skip: no build/work/mesa with the NV30_MINMAX_VMX patch (make mesa-work)"; exit 77; }
	need gcc
	mkdir -p $T
	gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I $M/src/mesa/vbo \
		-o $T/minmax-vmx-test packages/patched/graphics/mesa/tests/minmax-vmx-test.c || exit 1
	$T/minmax-vmx-test || exit 1
	# AltiVec itself: the deb builder's ppc64 cross compiler, run under qemu-user
	local img; img=$(docker images --format '{{.Repository}}:{{.Tag}}' ps3-deb-builder 2>/dev/null | head -1)
	if [ -z "$img" ] || ! command -v qemu-ppc64-static >/dev/null; then
		skip_part ppc64 "needs docker with a ps3-deb-builder image and qemu-ppc64-static"; return 0
	fi
	docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/w" -w /w "$img" powerpc64-linux-gnu-gcc -O2 -mcpu=cell \
		-Wall -Wextra -Werror -static -I $M/src/mesa/vbo -o $T/minmax-vmx-test-ppc64 packages/patched/graphics/mesa/tests/minmax-vmx-test.c || exit 1
	qemu-ppc64-static $T/minmax-vmx-test-ppc64
}
t_querysteal() {
	local M=build/work/mesa T=build/tmp/tests
	grep -q 'nv30_query_object_take' $M/src/gallium/drivers/nouveau/nv30/nv30_query.c 2>/dev/null ||
		{ echo "skip: no build/work/mesa with patch 0046 (make mesa-work)"; exit 77; }
	need gcc
	local G=$T/mesa-gen N=$M/src/compiler/nir
	mkdir -p $G/util/format
	python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml --enums > $G/util/format/u_format_gen.h &&
	python3 $M/src/compiler/builtin_types_h.py $G/builtin_types.h &&
	python3 $N/nir_opcodes_h.py > $G/nir_opcodes.h && python3 $N/nir_builder_opcodes_h.py > $G/nir_builder_opcodes.h &&
	python3 $N/nir_intrinsics_h.py --out $G/nir_intrinsics.h &&
	python3 $N/nir_intrinsics_indices_h.py --out $G/nir_intrinsics_indices.h || exit 1
	gcc -g -O1 -std=gnu11 -Wall -Werror -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE -DHAVE_TIMESPEC_GET \
		-DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$G -I$M/include -I$M/src \
		-I$M/src/gallium/include -I$M/src/gallium/auxiliary -I$M/src/gallium/drivers/nouveau -I$M/src/util \
		-I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir -I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm \
		-I$M/src/nouveau/headers -o $T/query-steal-test packages/patched/graphics/mesa/tests/query-steal-test.c || exit 1
	$T/query-steal-test
}
t_allocfail() {
	local M=build/work/mesa T=build/tmp/tests
	grep -q 'reallocate_or_keep' $M/src/gallium/drivers/nouveau/nouveau_buffer.c 2>/dev/null || { echo "skip: no build/work/mesa with patch 0048 (make mesa-work)"; exit 77; }
	need gcc
	local G=$T/mesa-gen N=$M/src/compiler/nir t=packages/patched/graphics/mesa/tests
	mkdir -p $G/util/format
	python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml --enums > $G/util/format/u_format_gen.h &&
	python3 $M/src/compiler/builtin_types_h.py $G/builtin_types.h &&
	python3 $N/nir_opcodes_h.py > $G/nir_opcodes.h && python3 $N/nir_builder_opcodes_h.py > $G/nir_builder_opcodes.h &&
	python3 $N/nir_intrinsics_h.py --out $G/nir_intrinsics.h &&
	python3 $N/nir_intrinsics_indices_h.py --out $G/nir_intrinsics_indices.h || exit 1
	mcc() {	# mcc OUT SRC [-I DIR...]: the directories given come first (a file as it was before a patch)
		local out=$1 src=$2; shift 2
		gcc "$@" -g -O1 -std=gnu11 -Wall -Werror -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE \
			-DHAVE_TIMESPEC_GET -DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$G \
			-I$M/include -I$M/src -I$M/src/gallium/include -I$M/src/gallium/auxiliary \
			-I$M/src/gallium/drivers/nouveau -I$M/src/util -I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir \
			-I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm -I$M/src/nouveau/headers \
			-o $out $src
	}
	mcc $T/alloc-fail-test $t/alloc-fail-test.c && $T/alloc-fail-test || exit 1
	local U=-Wl,--unresolved-symbols=ignore-all	# context-pushbuf-test: what it doesn't stub would crash
	mcc $T/context-pushbuf-test $t/context-pushbuf-test.c $U && $T/context-pushbuf-test || exit 1
	# fix off: the same cases against each file as it was before its patch (the parent of the
	# patch's commit in build/work/mesa) must fail where the patch's bug was
	before() {	# before SUBJECT PATH DEST: PATH from the work tree's commit before the one named SUBJECT
		local c; c=$(git -C $M log --format='%H %s' | grep -F " $1" | head -n 1 | cut -d' ' -f1)
		[ -n "$c" ] && mkdir -p "${3%/*}" && git -C $M show "$c^:$2" > "$3"
	}
	local S=$T/fix-off D=src/gallium/drivers/nouveau
	rm -rf $S
	if ! git -C $M rev-parse -q --verify HEAD >/dev/null 2>&1; then skip_part fix-off "build/work/mesa is no git tree"; return 0; fi
	before "u_upload, st, nv30: an upload buffer" src/gallium/auxiliary/util/u_upload_mgr.c $S/a/util/u_upload_mgr.c &&
	before "nouveau_mm: return NULL when no slab" $D/nouveau_mm.c $S/a/nouveau_mm.c &&
	before "nouveau: a buffer allocation that fails" $D/nouveau_mm.c $S/b/nouveau_mm.c &&
	before "nv30: push buffer robustness" $D/nv30/nv30_context.c $S/c/nv30/nv30_context.c &&
	before "nouveau, nv30: recover from a submission" $D/nouveau_fence.c $S/d/nouveau_fence.c || exit 1
	mcc $S/a/test $t/alloc-fail-test.c -I$S/a -I$M/src/gallium/auxiliary/util &&
	mcc $S/b/test $t/alloc-fail-test.c -I$S/b &&
	mcc $S/c/test $t/context-pushbuf-test.c $U -I$S/c -I$M/src/gallium/drivers/nouveau/nv30 &&
	mcc $S/d/test $t/alloc-fail-test.c -I$S/d || exit 1
	expect_fail "0047 off: u_upload" 'FAIL .*failed allocation left \*outbuf' $S/a/test upload &&
	expect_fail "0002 off: nouveau_mm with no slab" 'FAIL .*no memory: alloc 0x' $S/a/test mm-nomem &&
	expect_fail "0048 off: nouveau_mm fallback" 'FAIL .*a buffer of its own comes without a slab' $S/b/test mm-fallback &&
	expect_fail "0003 off: flush before any draw" 'FAIL .*kick came with no push buffer space' $S/c/test flush &&
	expect_fail "0003 off: destroy after a failed init" 'SEGV .* in (PUSH_KICK|nv30_context_destroy)' $S/c/test destroy &&
	expect_fail "0053 off: the wait on a cancelled submission's fence" 'FAIL .*cancelled submission failed' $S/d/test fence-cancel
}
t_transfercpu() {
	local M=build/work/mesa T=build/tmp/tests
	grep -q 'nv30_transfer_map_cpu' $M/src/gallium/drivers/nouveau/nv30/nv30_miptree.c 2>/dev/null || { echo "skip: no build/work/mesa with patch 0049 (make mesa-work)"; exit 77; }
	need gcc
	local G=$T/mesa-gen N=$M/src/compiler/nir
	mkdir -p $G/util/format
	python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml --enums > $G/util/format/u_format_gen.h &&
	python3 $M/src/compiler/builtin_types_h.py $G/builtin_types.h &&
	python3 $N/nir_opcodes_h.py > $G/nir_opcodes.h && python3 $N/nir_builder_opcodes_h.py > $G/nir_builder_opcodes.h &&
	python3 $N/nir_intrinsics_h.py --out $G/nir_intrinsics.h &&
	python3 $N/nir_intrinsics_indices_h.py --out $G/nir_intrinsics_indices.h || exit 1
	gcc -g -O1 -std=gnu11 -Wall -Werror -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE \
		-DHAVE_TIMESPEC_GET -DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$G \
		-I$M/include -I$M/src -I$M/src/gallium/include -I$M/src/gallium/auxiliary \
		-I$M/src/gallium/drivers/nouveau -I$M/src/util -I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir \
		-I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm -I$M/src/nouveau/headers \
		-o $T/transfer-cpu-test packages/patched/graphics/mesa/tests/transfer-cpu-test.c || exit 1
	$T/transfer-cpu-test
}
t_autogm() {
	if ! { command -v Xvfb >/dev/null && command -v openbox >/dev/null; } &&
	   ! command -v docker >/dev/null && ! command -v podman >/dev/null; then
		echo "skip: needs Xvfb+openbox or docker/podman"; exit 77; fi
	packages/walnut/system/ps3-desktop/tests/autogm-check.sh
}

t_pointsmooth() {
	local M=build/work/mesa T=build/tmp/tests
	[ -e $M/src/gallium/drivers/nouveau/nv30/nv30_point_smooth.c ] ||
		{ echo "skip: no build/work/mesa with the NV30_POINT_SMOOTH patch (make mesa-work)"; exit 77; }
	need gcc
	local G=$T/mesa-gen N=$M/src/compiler/nir A=$M/src/gallium/auxiliary/tgsi
	mkdir -p $G/util/format
	python3 $M/src/util/format/u_format_table.py $M/src/util/format/u_format.yaml --enums > $G/util/format/u_format_gen.h &&
	python3 $M/src/compiler/builtin_types_h.py $G/builtin_types.h &&
	python3 $N/nir_opcodes_h.py > $G/nir_opcodes.h && python3 $N/nir_builder_opcodes_h.py > $G/nir_builder_opcodes.h &&
	python3 $N/nir_intrinsics_h.py --out $G/nir_intrinsics.h &&
	python3 $N/nir_intrinsics_indices_h.py --out $G/nir_intrinsics_indices.h || exit 1
	gcc -g -O1 -std=gnu11 -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE -DHAVE_TIMESPEC_GET \
		-DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$G -I$M/include -I$M/src \
		-I$M/src/gallium/include -I$M/src/gallium/auxiliary -I$M/src/gallium/drivers/nouveau -I$M/src/util \
		-I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir -I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm \
		-I$M/src/nouveau/headers -o $T/point-smooth-test packages/patched/graphics/mesa/tests/point-smooth-test.c \
		$M/src/gallium/drivers/nouveau/nv30/nv30_point_smooth.c $M/src/gallium/drivers/nouveau/nv30/nvfx_fragprog.c \
		$A/tgsi_exec.c $A/tgsi_transform.c $A/tgsi_text.c $A/tgsi_build.c $A/tgsi_info.c $A/tgsi_util.c \
		$A/tgsi_strings.c $A/tgsi_parse.c $A/tgsi_dump.c $A/tgsi_iterate.c $A/tgsi_scan.c \
		$M/src/util/ralloc.c $M/src/util/u_dynarray.c -lm || exit 1
	ASAN_OPTIONS=detect_leaks=0 $T/point-smooth-test
}

run bench-selftest python3 bench/tests/selftest.py
run bench-launcher python3 -I bench/tests/test_launcher.py
run gprefetch python3 packages/walnut/system/ps3-desktop/tests/gprefetch-check.py
run rt-audio sh packages/walnut/system/ps3-desktop/tests/rt-audio-check.sh
run game-menu sh packages/walnut/system/ps3-desktop/tests/game-menu-check.sh
run ps3rsx-shadow bash -c "$(declare -f need skip_part t_ps3rsx); t_ps3rsx"
run ps3-spu make -C packages/walnut/tools/ps3-spu test
run xmoto-glbatch bash -c "$(declare -f need t_xmoto); t_xmoto"
run mesa-forcedxt bash -c "$(declare -f need t_forcedxt); t_forcedxt"
run mesa-staging-pool bash -c "$(declare -f need t_stagingpool); t_stagingpool"
run mesa-vtx-snorm8 bash -c "$(declare -f need t_vtxsnorm8); t_vtxsnorm8"
run mesa-minmax-vmx bash -c "$(declare -f need skip_part t_minmaxvmx); t_minmaxvmx"
run mesa-query-steal bash -c "$(declare -f need t_querysteal); t_querysteal"
run mesa-alloc-fail bash -c "$(declare -f need skip_part expect_fail t_allocfail); t_allocfail"
run mesa-transfer-cpu bash -c "$(declare -f need t_transfercpu); t_transfercpu"
run mesa-point-smooth bash -c "$(declare -f need t_pointsmooth); t_pointsmooth"
run sdl2-compat-close-locked bash packages/patched/graphics/sdl2-compat/tests/close-locked-test.sh
run luajit-low-2gb packages/patched/libs/luajit/tests/low-2gb.sh
run ssdimg python3 tools/ssdimg/test_ssdimg.py
run ps3-boot python3 packages/walnut/system/ps3-boot/tests/test_kboot.py
run agent-guard python3 -I tools/hooks/test_agent_guard.py
run ps3-lease bash tools/test_ps3_lease.sh
run leak-check python3 tools/test_leak_check.py
run repo-tools python3 -I tools/test_repo_tools.py
run licensed python3 -I tools/licensed.py check
run workflows python3 -I github-ci/gen-workflows.py --check
run blocks python3 -I tools/gamedb/blocks.py --check
run doclinks python3 -I tools/doclinks.py --report
# report only for now: what doesn't resolve is listed in its log, not a failure
[ ! -s "$L/doclinks.log" ] || { [ -n "$only" ] && [ "$only" != doclinks ]; } ||
	grep -q ' 0 unresolved' "$L/doclinks.log" || echo "   warn: $(tail -n 1 "$L/doclinks.log") ($L/doclinks.log)"
run game-mentions python3 -I tools/gamedb/game-mentions.py
[ ! -s "$L/game-mentions.log" ] || { [ -n "$only" ] && [ "$only" != game-mentions ]; } ||
	grep -q ': 0 sections' "$L/game-mentions.log" || echo "   warn: $(tail -n 1 "$L/game-mentions.log") ($L/game-mentions.log)"
run walnut-repo python3 apt-repo/test_walnut_repo.py
run walnut-watch python3 servers/walnut.nanobyte.dev/test_walnut_watch.py
run batch-publish python3 -I github-ci/test_batch_publish.py
run gamedb python3 -I tools/gamedb/test_gamedb.py
run gamedb-evidence bash -c "python3 -I tools/gamedb/test_evidence.py && python3 -I tools/gamedb/reimport.py --check"
run gamedb-technical python3 -I tools/gamedb/test_technical.py
run gamedb-schema python3 -I tools/gamedb/test_schema.py
run patch-register python3 -I tools/gamedb/patches.py
run walnut-games python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_games.py
run walnut-data python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_data.py
run walnut-rate python3 -I packages/walnut/apps/walnut-rate/tests/test_walnut_rate.py
run ps3disk-guard packages/patched/kernel/linux/tests/ps3disk-guard.sh
run game-data-packager packages/patched/tools/game-data-packager/tests/run-in-builder.sh
[ $slow = 1 ] && run autogm bash -c "$(declare -f t_autogm); t_autogm"
[ $slow = 1 ] && run ssdimg-vm tools/ssdimg/vm-e2e.sh

echo; printf '%-18s %-6s %s\n' suite result seconds
for i in "${!names[@]}"; do printf '%-18s %-6s %s\n' "${names[$i]}" "${res[$i]}" "${secs[$i]}"; done
[ ${#skips[@]} = 0 ] || { echo "skipped (not passes):"; printf '  %s\n' "${skips[@]}"; }
[ $fail = 0 ] && echo "all passed" || echo "FAILED (logs in $L)"
exit $fail
