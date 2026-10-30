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
#            fake GPU, under AddressSanitizer; with it Mesa 0047-0049's alloc-fail-test: u_upload_mgr,
#            nouveau_mm and nouveau_fence when buffers cannot be had, and 0049's transfer-cpu-test:
#            nv30 texture uploads and reads by the CPU when the GART is full), mesa-point-smooth
#            (Mesa 0050's round GL_POINT_SMOOTH points: the fragment program variant on Mesa's
#            TGSI interpreter over each point's square, and its NV40 translation),
#            ssdimg (the SSD test
#            drive's stamp.py, ps3-imgmap on a fake UFS layer, ps3-vswap's device-mapper backing;
#            ps3-imgmap-boot's maps and tables against ps3-imgmap's; both tools' write guard
#            registration against a model of the kernel's side), ps3-boot (ps3-update-kboot's
#            entries and its /etc/kboot.d extras on a fake root), leak-check (tools/leak-check.py on
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
#            pages, drafts left out, links, the benchmark tables, the public-safe scan), walnut-games (the app and its root
#            helper: validation, remove-cascade refusal, Exec parsing, a window smoke test), walnut-data (the data import:
#            the helper's import-data against tar fixtures and its lock, remove-data, install-repo-data, walnut-data
#            with a stubbed game-data-packager), walnut-rate (its root helper's refusals and protocol, ps3-bench.sh's
#            helper mode with a fake sudo, the clean-system check and the watch on made-up process lists and windows,
#            the export's scrub and an export -> import-run.py round trip), ps3disk-guard (kernel local/0045's
#            range checks, packages/patched/kernel/linux/tests/ps3disk-guard.sh), game-data-packager (our patches'
#            --progress-fd and --no-launchers on the built deb, in the deb builder image: packages/patched/tools/
#            game-data-packager/tests; skipped without build/debs/game-data-packager)
#   --slow   also autogm (packages/walnut/system/ps3-desktop/tests/autogm-check.sh: needs Xvfb+openbox or docker/podman) and
#            ssdimg-vm (tools/ssdimg/vm-e2e.sh: ps3-imgmap on a real big-endian UFS2 image in a KVM
#            guest of the host's kernel, then Phase B: ps3-imgmap-boot's devices, the initramfs
#            booting root from the image, refused boots; the second Phase B geometry behind the
#            kernel guard, simulated by a module built from local/0045 for the host kernel (needs its
#            headers, else that part is skipped); needs docker, qemu-system-x86_64, /dev/kvm)
set -u
cd "$(dirname "$0")/.."
slow=0; only=""
while [ $# -gt 0 ]; do
	case $1 in --slow) slow=1;; --only) only=$2; shift;; *) echo "usage: $0 [--slow] [--only NAME]" >&2; exit 2;; esac
	shift
done
L=build/tmp/tests; mkdir -p "$L"
names=(); res=(); secs=(); fail=0

run() {	# run NAME CMD...: skip when the command is 77 (autotools convention for "skipped")
	local n=$1; shift
	[ -z "$only" ] || [ "$only" = "$n" ] || return 0
	local t0=$SECONDS rc
	printf '== %s\n' "$n"
	"$@" > "$L/$n.log" 2>&1; rc=$?
	names+=("$n"); secs+=($((SECONDS - t0)))
	case $rc in
	0) res+=(pass);;
	77) res+=(skip); sed -n '$p' "$L/$n.log";;
	*) res+=(FAIL); fail=1; tail -n 25 "$L/$n.log";;
	esac
}
need() { command -v "$1" >/dev/null || { echo "skip: no $1"; exit 77; }; }

t_ps3rsx() {
	O=$PWD/build/tmp/ps3rsx-test
	need gcc
	make -C packages/walnut/drivers/ps3rsx/test O="$O" "$O/shadow-equiv" "$O/shadow-equiv-fast" "$O/zero-test" "$O/validate-test" || exit 1
	"$O/shadow-equiv" 200000 20000 1 || exit 1
	"$O/shadow-equiv-fast" 1000000 100000 2 || exit 1
	"$O/zero-test" 20000 1 || exit 1
	"$O/validate-test" || exit 1
	# big-endian run under qemu, when the cross compiler, the sysroot and qemu are there
	if command -v powerpc64-linux-gnu-gcc >/dev/null && command -v qemu-ppc64-static >/dev/null &&
	   [ -d build/mesa/sysroot ]; then
		make -C packages/walnut/drivers/ps3rsx/test O="$O" "$O/shadow-equiv-ppc64" "$O/zero-test-ppc64" || exit 1
		qemu-ppc64-static "$O/shadow-equiv-ppc64" 100000 10000 3 || exit 1
		qemu-ppc64-static "$O/zero-test-ppc64" 20000 2 || exit 1
	else echo "(ppc64 run skipped: no cross gcc, qemu or sysroot)"; fi
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
		echo "ppc64 part skipped: needs docker with a ps3-deb-builder image and qemu-ppc64-static"; return 0
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
	$T/query-steal-test || exit 1
	if grep -q 'reallocate_or_keep' $M/src/gallium/drivers/nouveau/nouveau_buffer.c; then
		gcc -g -O1 -std=gnu11 -Wall -Werror -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE \
			-DHAVE_TIMESPEC_GET -DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$G \
			-I$M/include -I$M/src -I$M/src/gallium/include -I$M/src/gallium/auxiliary \
			-I$M/src/gallium/drivers/nouveau -I$M/src/util -I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir \
			-I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm -I$M/src/nouveau/headers \
			-o $T/alloc-fail-test packages/patched/graphics/mesa/tests/alloc-fail-test.c || exit 1
		$T/alloc-fail-test || exit 1
	else echo "(alloc-fail part skipped: build/work/mesa without patch 0048)"; fi
	if grep -q 'nv30_transfer_map_cpu' $M/src/gallium/drivers/nouveau/nv30/nv30_miptree.c; then
		gcc -g -O1 -std=gnu11 -Wall -Werror -fsanitize=address,undefined -DHAVE_PTHREAD -D_GNU_SOURCE \
			-DHAVE_TIMESPEC_GET -DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_BIG_ENDIAN=0 -DUTIL_ARCH_LITTLE_ENDIAN=1 -I$G \
			-I$M/include -I$M/src -I$M/src/gallium/include -I$M/src/gallium/auxiliary \
			-I$M/src/gallium/drivers/nouveau -I$M/src/util -I$M/src/mapi -I$M/src/mesa -I$M/src/compiler/nir \
			-I$M/src/compiler -I$M/src/gallium/winsys/nouveau/drm -I$M/src/nouveau/headers \
			-o $T/transfer-cpu-test packages/patched/graphics/mesa/tests/transfer-cpu-test.c || exit 1
		$T/transfer-cpu-test
	else echo "(transfer-cpu part skipped: build/work/mesa without patch 0049)"; fi
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
run ps3rsx-shadow bash -c "$(declare -f need t_ps3rsx); t_ps3rsx"
run ps3-spu make -C packages/walnut/tools/ps3-spu test
run game-menu sh packages/walnut/system/ps3-desktop/tests/game-menu-check.sh
run xmoto-glbatch bash -c "$(declare -f need t_xmoto); t_xmoto"
run mesa-forcedxt bash -c "$(declare -f need t_forcedxt); t_forcedxt"
run mesa-staging-pool bash -c "$(declare -f need t_stagingpool); t_stagingpool"
run mesa-vtx-snorm8 bash -c "$(declare -f need t_vtxsnorm8); t_vtxsnorm8"
run mesa-minmax-vmx bash -c "$(declare -f need t_minmaxvmx); t_minmaxvmx"
run mesa-query-steal bash -c "$(declare -f need t_querysteal); t_querysteal"
run mesa-point-smooth bash -c "$(declare -f need t_pointsmooth); t_pointsmooth"
run ssdimg python3 tools/ssdimg/test_ssdimg.py
run ps3-boot python3 packages/walnut/system/ps3-boot/tests/test_kboot.py
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
run walnut-games python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_games.py
run walnut-data python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_data.py
run walnut-rate python3 -I packages/walnut/apps/walnut-rate/tests/test_walnut_rate.py
run ps3disk-guard packages/patched/kernel/linux/tests/ps3disk-guard.sh
run game-data-packager packages/patched/tools/game-data-packager/tests/run-in-builder.sh
[ $slow = 1 ] && run autogm bash -c "$(declare -f t_autogm); t_autogm"
[ $slow = 1 ] && run ssdimg-vm tools/ssdimg/vm-e2e.sh

echo; printf '%-18s %-6s %s\n' suite result seconds
for i in "${!names[@]}"; do printf '%-18s %-6s %s\n' "${names[$i]}" "${res[$i]}" "${secs[$i]}"; done
[ $fail = 0 ] && echo "all passed" || echo "FAILED (logs in $L)"
exit $fail
