#!/bin/bash
# run-tests.sh [--slow] [--only NAME]: every host test suite (no PS3 involved), then a table of
# suite, result and seconds; exit 1 when any failed. Output of each suite: build/tmp/tests/<suite>.log
# (shown for failures). A suite whose prerequisites are missing is "skip", not a failure.
#   default  quick suites: bench-selftest (bench/tests/selftest.py), bench-launcher (bench/tests/test_launcher.py:
#            launcher.py and session.sh's choice of a menu launcher), gprefetch, rt-audio (ps3-rt-audio's choice of
#            threads, a stand-in game: packages/walnut/system/ps3-desktop/tests/rt-audio-check.sh), ps3rsx-shadow (and ps3rsx's clear_dcbz zeroing,
#            the validator's vertex format checks, blame-test: the recovery's choice of the trapping commit,
#            pushbuf-test: GEM_PUSHBUF's offset and length checks, vclear-test: freed VRAM cleared
#            before reuse, on a model heap and GPU, pushdump-test: the push dump's scanner and records,
#            p4-test and p4-fuzz: the address checks),
#            gpu-recovered (ps3-desktop's recovery dialog: its stamp only after the window maps on a live X, a
#            failed recovery's dialog only on the new X), gpu-crash-restart (ps3-system's crash report: the
#            display-only desktop restart once per boot through logind, the old X waited for, the user's own X only),
#            render-node (ps3-system's udev rule: the RSX's render node for the render group only),
#            ps3-firstboot (walnut-firstboot's setup file in a dry run on a fake root: where it came from, the question, a decline or timeout,
#            the confirm helper's rules),
#            ps3-system-r3 (ds3-pair restarting bluetooth only for a new pairing, ps3-vswap.service's order, the three sudoers-file postinst blocks, the escape hatch's docs line), migrate-home (ps3-migrate-home dropping to the user with setpriv), root-seams (the root helpers' test seams ignored by a root process), game-mode-on (ps3-game-mode on: only the guard's own service, once), xwrapper (ps3-desktop's Xwrapper.config: console only),
#            apt-update-notice (ps3-system's notice after apt update: fixture lists, os-release, fake dpkg; newer release, same release, garbage, missing),
#            rsx-ab (tools/rsx-ab: the same-boot ps3rsx A/B batch against stubs, the mpv counter script),
#            ps3-spu, xmoto-glbatch,
#            mesa-forcedxt (Mesa 0038's NV30_FORCE_DXT decision; every mesa-* suite is packages/patched/graphics/mesa/tests/
#            host-tests.sh against build/work/mesa, or MESA_WORK), mesa-staging-pool
#            (Mesa 0040's NV30_STAGING_POOL_MB decisions and a model of the pool, packages/patched/graphics/mesa/tests),
#            mesa-vtx-snorm8 (Mesa 0043's NV30_VTX_SNORM8 vertex program normalization against
#            util_format's unpack, every byte; the per-input key; the translator's output run on
#            an NV40 vertex program interpreter with and without it), mesa-minmax-vmx (Mesa 0044's
#            AltiVec index min/max against the scalar loops, on the host and as ppc64 under qemu),
#            mesa-query-steal (Mesa 0046: nv30's queries when the report slots run out, against a
#            fake GPU, under AddressSanitizer), mesa-query-lost (Mesa 0056: a query whose report never
#            lands after a cancelled submission, or ever: its wait must end, not hang; a late one keeps
#            its value), mesa-alloc-fail (0047-0049's alloc-fail-test: u_upload_mgr,
#            nouveau_mm and nouveau_fence when buffers cannot be had; 0003's context-pushbuf-test: nv30's flush
#            before any draw and destroy after a failed init; each fix off too, the file as it was before
#            its patch, which must fail: 0002, 0003, 0047, 0048; 0053's fence wait after a cancelled submission), mesa-blit-state (Mesa 0058: nv30_transfer_rect_blit's window offset, clip, COORD_CONVENTIONS and zeta against a fake push buffer, and the golden diff of its stream with the file before the patch),
#            mesa-placement (Mesa 0059: NV30_PS3_IDX_GART and NV30_PS3_FP_GART place index buffers and fragment programs in GART, off they change nothing; a busy GART index buffer is waited for, never GPU-copied into),
#            mesa-fb-window (Mesa 0060: nv30_validate_fb's small-surface hack clips to its 16-wide block; the golden diff of four targets' streams against the file before the patch),
#            mesa-transfer-cpu (0049's
#            transfer-cpu-test: nv30 texture uploads and reads by the CPU when the GART is full), mesa-point-smooth
#            (Mesa 0050's round GL_POINT_SMOOTH points: the fragment program variant on Mesa's
#            TGSI interpreter over each point's square, and its NV40 translation),
#            sdl2-compat-close-locked (sdl2-compat 0001: SDL_CloseAudio while locked, SDL 1.2's and
#            SDL2's API, dummy driver, a timeout; our build passes, the stock tarball's hangs), luajit-low-2gb
#            (luajit 0001: luaL_newstate() 100 times under qemu-ppc64, ours never fails, stock does),
#            walnut-install (system/installer/test_walnut_install.py: the release checks, the USB install's plan, and that
#            only the typed model name confirms a live USB install),
#            ssdimg (the SSD test
#            drive's stamp.py, ps3-imgmap on a fake UFS layer, ps3-vswap's device-mapper backing;
#            ps3-imgmap-boot's maps and tables against ps3-imgmap's; both tools' write guard
#            registration against a model of the kernel's side), ps3-boot (ps3-update-kboot's
#            entries and its /etc/kboot.d extras on a fake root), fix-ladder (tools/ladder.py --top: the fix ladder's top rung, every Mesa
#            stability fix in, each test of the register must pass; make ladder FULL=1 is every rung, docs/VALIDATION.md),
#            agent-guard (tools/hooks/agent-guard.py, the PreToolUse hook: refused and allowed commands), commit-tests (tools/commit_tests/tests: each commit check on good and bad cases, then the hooks end to end on a clone in build/tmp), commit-hooks (make hooks's stubs are installed in .git/hooks), ps3-lease (tools/ps3's lease, and deploy.sh and play-test.sh refusing under someone else's), leak-check (tools/leak-check.py on
#            synthetic packages and sources), image-check (tools/image-check.py on fabricated rootfs tars), repo-tools (tools/test_repo_tools.py: pkg-dir.sh,
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
#            range checks, packages/patched/kernel/linux/tests/ps3disk-guard.sh), kernel-config-check (tools/linux-config-check.sh: a dropped
#            or changed config option, the hardening and must-stay-off lists of the kernel's package.conf, the research drivers per flavour),
#            kernel-lockdep-variant (LINUX_VARIANT=lockdep: a test-build version of its own, the release flavour unaffected, PROVE_LOCKING in the debug config; the config half needs a patched tree in build/tmp/lane-km/t115 or LINUX_TREE),
#            kernel-gart-parse (local/0007's size parser against the memparse wraps, no panic on an oversized reservation, local/0049's re-probe
#            cancel in remove), game-data-packager (our patches'
#            --progress-fd and --no-launchers on the built deb, in the deb builder image: packages/patched/tools/
#            game-data-packager/tests; skipped without build/debs/game-data-packager)
#   --ci     for GitHub's runner (.github/workflows/walnut-tests.yml): a skip fails unless tools/ci-skips.txt lists it
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
slow=0; only=""; ci=0
while [ $# -gt 0 ]; do
	case $1 in --slow) slow=1;; --only) only=$2; shift;; --ci) ci=1;; *) echo "usage: $0 [--slow] [--ci] [--only NAME]" >&2; exit 2;; esac
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

t_ps3rsx() {
	O=$PWD/build/tmp/ps3rsx-test
	need gcc
	make -C packages/walnut/drivers/ps3rsx/test O="$O" "$O/shadow-equiv" "$O/shadow-equiv-fast" "$O/zero-test" "$O/validate-test" \
		"$O/blame-test" "$O/pushbuf-test" "$O/vclear-test" "$O/validate-capture" "$O/pushdump-test" \
		"$O/p4-test" "$O/p4-test-count" "$O/p4-test-dump" "$O/p4-fuzz" || exit 1
	"$O/shadow-equiv" 200000 20000 1 || exit 1
	"$O/shadow-equiv-fast" 1000000 100000 2 || exit 1
	"$O/zero-test" 20000 1 || exit 1
	"$O/validate-test" || exit 1
	"$O/blame-test" || exit 1
	"$O/pushbuf-test" || exit 1
	"$O/vclear-test" || exit 1
	# the address checks: their rules (as released, and in the count-only build), the fuzz
	"$O/p4-test" || exit 1
	"$O/p4-test-count" || exit 1
	"$O/p4-test-dump" || exit 1
	for seed in 1 2 3; do "$O/p4-fuzz" "$seed" 30000 || exit 1; done
	# the push dump's scanner and records, a sample split into validate-capture's inputs
	need python3
	packages/walnut/drivers/ps3rsx/test/pushdump-test.sh "$O" || exit 1
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
t_autogm() {
	if ! { command -v Xvfb >/dev/null && command -v openbox >/dev/null; } &&
	   ! command -v docker >/dev/null && ! command -v podman >/dev/null; then
		echo "skip: needs Xvfb+openbox or docker/podman"; exit 77; fi
	packages/walnut/system/ps3-desktop/tests/autogm-check.sh
}

run bench-selftest python3 bench/tests/selftest.py
run bench-launcher python3 -I bench/tests/test_launcher.py
run gprefetch python3 packages/walnut/system/ps3-desktop/tests/gprefetch-check.py
run rt-audio sh packages/walnut/system/ps3-desktop/tests/rt-audio-check.sh
run game-menu sh packages/walnut/system/ps3-desktop/tests/game-menu-check.sh
run gpu-recovered python3 -I packages/walnut/system/ps3-desktop/tests/gpu-recovered-test.py
run gpu-crash-restart bash packages/walnut/system/ps3-system/tests/gpu-crash-restart-test.sh
run render-node bash packages/walnut/system/ps3-system/tests/render-node-test.sh
run apt-update-notice bash packages/walnut/system/ps3-system/tests/apt-update-notice-test.sh
run ps3-firstboot python3 -I packages/walnut/system/ps3-system/tests/test_firstboot_preseed.py
run ps3-system-r3 python3 -I packages/walnut/system/ps3-system/tests/test_system_r3.py
run migrate-home python3 -I packages/walnut/system/ps3-desktop/tests/test_migrate_home.py
run game-mode-on python3 -I packages/walnut/system/ps3-desktop/tests/test_game_mode_on.py
run xwrapper python3 -I packages/walnut/system/ps3-desktop/tests/test_xwrapper.py
run root-seams python3 -I tools/test_root_seams.py
run rsx-ab bash tools/rsx-ab/test_rsx_ab.sh
run ps3rsx-shadow bash -c "$(declare -f need skip_part t_ps3rsx); t_ps3rsx"
run ps3-spu make -C packages/walnut/tools/ps3-spu test
run xmoto-glbatch bash -c "$(declare -f need t_xmoto); t_xmoto"
run mesa-forcedxt packages/patched/graphics/mesa/tests/host-tests.sh forcedxt
run mesa-staging-pool packages/patched/graphics/mesa/tests/host-tests.sh staging-pool
run mesa-vtx-snorm8 packages/patched/graphics/mesa/tests/host-tests.sh vtx-snorm8
run mesa-minmax-vmx packages/patched/graphics/mesa/tests/host-tests.sh minmax-vmx
run mesa-query-steal packages/patched/graphics/mesa/tests/host-tests.sh query-steal
run mesa-query-lost packages/patched/graphics/mesa/tests/host-tests.sh query-lost
run mesa-alloc-fail packages/patched/graphics/mesa/tests/host-tests.sh alloc-fail
run mesa-transfer-cpu packages/patched/graphics/mesa/tests/host-tests.sh transfer-cpu
run mesa-blit-state packages/patched/graphics/mesa/tests/host-tests.sh blit-state
run mesa-placement packages/patched/graphics/mesa/tests/host-tests.sh placement
run mesa-fb-window packages/patched/graphics/mesa/tests/host-tests.sh fb-window
run mesa-point-smooth packages/patched/graphics/mesa/tests/host-tests.sh point-smooth
run sdl2-compat-close-locked bash packages/patched/graphics/sdl2-compat/tests/close-locked-test.sh
run luajit-low-2gb packages/patched/libs/luajit/tests/low-2gb.sh
run ssdimg python3 tools/ssdimg/test_ssdimg.py
run ps3-boot python3 packages/walnut/system/ps3-boot/tests/test_kboot.py
run walnut-install python3 -I system/installer/test_walnut_install.py
run agent-guard python3 -I tools/hooks/test_agent_guard.py
run commit-tests bash -c "python3 -I tools/commit_tests/tests/test_checks.py && python3 -I tools/commit_tests/tests/test_e2e.py"
run commit-hooks tools/hooks/install.sh --check
run ps3-lease bash tools/test_ps3_lease.sh
run leak-check python3 tools/test_leak_check.py
run image-check python3 -I tools/test_image_check.py
run ps3-sudoers bash packages/walnut/system/ps3-desktop/tests/sudoers-check.sh
run repo-tools python3 -I tools/test_repo_tools.py
run licensed python3 -I tools/licensed.py check
run workflows python3 -I github-ci/gen-workflows.py --check
run blocks bash -c "[ -d gamedb/games/classicube/licensed ] || { echo 'skip: the licensed package directories are not in this checkout (the public export), and two generated blocks list them'; exit 77; }; python3 -I tools/gamedb/blocks.py --check"
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
run walnut-lists python3 -I github-ci/test_walnut_lists.py
run gen-workflows python3 -I github-ci/test_gen_workflows.py
run gamedb python3 -I tools/gamedb/test_gamedb.py
run gamedb-evidence bash -c "command -v magick >/dev/null || { echo 'skip: no ImageMagick 7 (magick)'; exit 77; }; python3 -I tools/gamedb/test_evidence.py && python3 -I tools/gamedb/reimport.py --check"
run gamedb-technical python3 -I tools/gamedb/test_technical.py
run gamedb-schema python3 -I tools/gamedb/test_schema.py
run patch-register python3 -I tools/gamedb/patches.py
run fix-ladder python3 -I tools/ladder.py --top
run walnut-games python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_games.py
run walnut-data python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_data.py
run walnut-rate python3 -I packages/walnut/apps/walnut-rate/tests/test_walnut_rate.py
run ps3disk-guard packages/patched/kernel/linux/tests/ps3disk-guard.sh
run kernel-config-check packages/patched/kernel/linux/tests/config-check.sh
run kernel-gart-parse packages/patched/kernel/linux/tests/ps3rsx-gart-parse.sh
run kernel-lockdep-variant packages/patched/kernel/linux/tests/lockdep-variant.sh
run game-data-packager packages/patched/tools/game-data-packager/tests/run-in-builder.sh
[ $slow = 1 ] && run autogm bash -c "$(declare -f t_autogm); t_autogm"
[ $slow = 1 ] && run ssdimg-vm tools/ssdimg/vm-e2e.sh

echo; printf '%-26s %-6s %s\n' suite result seconds
for i in "${!names[@]}"; do printf '%-26s %-6s %s\n' "${names[$i]}" "${res[$i]}" "${secs[$i]}"; done
[ ${#skips[@]} = 0 ] || { echo "skipped (not passes):"; printf '  %s\n' "${skips[@]}"; }
# --ci (GitHub's runner, github-ci/walnut-tests): a skip is a failure unless tools/ci-skips.txt lists its key
# (SKIP's second word: a suite, suite:part or suite:unittest) with a reason; a missing prerequisite never reads as
# a pass. An entry that no skip used is only mentioned: another runner may differ.
if [ $ci = 1 ]; then
	declare -A allowed=() used=()
	while read -r k _; do case $k in ''|'#'*) ;; *) allowed[$k]=1 ;; esac; done < tools/ci-skips.txt
	for line in "${skips[@]}"; do
		k=${line#SKIP }; k=${k%% *}
		if [ -n "${allowed[$k]:-}" ]; then used[$k]=1
		else echo "!! skipped in CI and not in tools/ci-skips.txt: $k"; fail=1; fi
	done
	for k in "${!allowed[@]}"; do [ -n "$only" ] || [ -n "${used[$k]:-}" ] || echo "-- tools/ci-skips.txt lists $k, which did not skip"; done
fi
[ $fail = 0 ] && echo "all passed" || echo "FAILED (logs in $L)"
exit $fail
