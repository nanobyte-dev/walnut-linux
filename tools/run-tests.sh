#!/bin/bash
# run-tests.sh [--slow] [--only NAME]: every host test suite (no PS3 involved), then a table of
# suite, result and seconds; exit 1 when any failed. Output of each suite: build/tmp/tests/<suite>.log
# (shown for failures). A suite whose prerequisites are missing is "skip", not a failure.
#   default  quick suites: bench-selftest, gprefetch, ps3rsx-shadow (and ps3rsx's clear_dcbz zeroing
#            and the validator's vertex format checks),
#            ps3-spu, xmoto-glbatch,
#            mesa-forcedxt (Mesa 0038's NV30_FORCE_DXT decision, mesa/tests), mesa-staging-pool
#            (Mesa 0040's NV30_STAGING_POOL_MB decisions and a model of the pool, mesa/tests),
#            mesa-vtx-snorm8 (Mesa 0043's NV30_VTX_SNORM8 vertex program normalization against
#            util_format's unpack, every byte; the per-input key; the translator's output run on
#            an NV40 vertex program interpreter with and without it), mesa-minmax-vmx (Mesa 0044's
#            AltiVec index min/max against the scalar loops, on the host and as ppc64 under qemu),
#            ssdimg (the SSD test
#            drive's stamp.py, ps3-imgmap on a fake UFS layer, ps3-vswap's device-mapper backing;
#            ps3-imgmap-boot's maps and tables against ps3-imgmap's; both tools' write guard
#            registration against a model of the kernel's side), ps3-boot (ps3-update-kboot's
#            entries and its /etc/kboot.d extras on a fake root), leak-check (tools/leak-check.py on
#            synthetic packages and sources), ps3disk-guard (kernel local/0045's
#            range checks, linux/tests/ps3disk-guard.sh)
#   --slow   also autogm (ps3-desktop/tests/autogm-check.sh: needs Xvfb+openbox or docker/podman) and
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
	make -C ps3rsx/test O="$O" "$O/shadow-equiv" "$O/shadow-equiv-fast" "$O/zero-test" "$O/validate-test" || exit 1
	"$O/shadow-equiv" 200000 20000 1 || exit 1
	"$O/shadow-equiv-fast" 1000000 100000 2 || exit 1
	"$O/zero-test" 20000 1 || exit 1
	"$O/validate-test" || exit 1
	# big-endian run under qemu, when the cross compiler, the sysroot and qemu are there
	if command -v powerpc64-linux-gnu-gcc >/dev/null && command -v qemu-ppc64-static >/dev/null &&
	   [ -d build/mesa/sysroot ]; then
		make -C ps3rsx/test O="$O" "$O/shadow-equiv-ppc64" "$O/zero-test-ppc64" || exit 1
		qemu-ppc64-static "$O/shadow-equiv-ppc64" 100000 10000 3 || exit 1
		qemu-ppc64-static "$O/zero-test-ppc64" 20000 2 || exit 1
	else echo "(ppc64 run skipped: no cross gcc, qemu or sysroot)"; fi
}
t_xmoto() {
	[ -e build/work/xmoto/src/drawlib/GLBatch.cpp ] || { echo "skip: no build/work/xmoto (make xmoto-work)"; exit 77; }
	need g++
	xmoto/test/glbatch-test.sh 100
}
t_forcedxt() {
	[ -e build/work/mesa/src/mesa/main/texforcedxt_policy.h ] || { echo "skip: no build/work/mesa with patch 0038 (make mesa-work)"; exit 77; }
	need gcc
	mkdir -p build/tmp/tests
	gcc -O2 -Wall -Wextra -Werror -I build/work/mesa/include -I build/work/mesa/src \
		-o build/tmp/tests/forcedxt-policy-test mesa/tests/forcedxt-policy-test.c || exit 1
	build/tmp/tests/forcedxt-policy-test
}
t_stagingpool() {
	[ -e build/work/mesa/src/gallium/drivers/nouveau/nv30/nv30_staging_policy.h ] || { echo "skip: no build/work/mesa with patch 0040 (make mesa-work)"; exit 77; }
	need gcc
	mkdir -p build/tmp/tests
	gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -I build/work/mesa/src/gallium/drivers/nouveau \
		-o build/tmp/tests/staging-pool-test mesa/tests/staging-pool-test.c || exit 1
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
		-DUTIL_ARCH_BIG_ENDIAN=0 -o $T/vtx-snorm8-test mesa/tests/vtx-snorm8-test.c || exit 1
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
		-I$M/src/nouveau/headers -o $T/vtx-snorm8-vpsim mesa/tests/vtx-snorm8-vpsim.c mesa/tests/vtx-snorm8-stubs.c \
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
		-o $T/minmax-vmx-test mesa/tests/minmax-vmx-test.c || exit 1
	$T/minmax-vmx-test || exit 1
	# AltiVec itself: the deb builder's ppc64 cross compiler, run under qemu-user
	local img; img=$(docker images --format '{{.Repository}}:{{.Tag}}' ps3-deb-builder 2>/dev/null | head -1)
	if [ -z "$img" ] || ! command -v qemu-ppc64-static >/dev/null; then
		echo "ppc64 part skipped: needs docker with a ps3-deb-builder image and qemu-ppc64-static"; return 0
	fi
	docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/w" -w /w "$img" powerpc64-linux-gnu-gcc -O2 -mcpu=cell \
		-Wall -Wextra -Werror -static -I $M/src/mesa/vbo -o $T/minmax-vmx-test-ppc64 mesa/tests/minmax-vmx-test.c || exit 1
	qemu-ppc64-static $T/minmax-vmx-test-ppc64
}
t_autogm() {
	if ! { command -v Xvfb >/dev/null && command -v openbox >/dev/null; } &&
	   ! command -v docker >/dev/null && ! command -v podman >/dev/null; then
		echo "skip: needs Xvfb+openbox or docker/podman"; exit 77; fi
	ps3-desktop/tests/autogm-check.sh
}

run bench-selftest python3 games/bench/selftest.py
run gprefetch python3 ps3-desktop/tests/gprefetch-check.py
run ps3rsx-shadow bash -c "$(declare -f need t_ps3rsx); t_ps3rsx"
run ps3-spu make -C ps3-spu test
run xmoto-glbatch bash -c "$(declare -f need t_xmoto); t_xmoto"
run mesa-forcedxt bash -c "$(declare -f need t_forcedxt); t_forcedxt"
run mesa-staging-pool bash -c "$(declare -f need t_stagingpool); t_stagingpool"
run mesa-vtx-snorm8 bash -c "$(declare -f need t_vtxsnorm8); t_vtxsnorm8"
run mesa-minmax-vmx bash -c "$(declare -f need t_minmaxvmx); t_minmaxvmx"
run ssdimg python3 tools/ssdimg/test_ssdimg.py
run ps3-boot python3 ps3-boot/tests/test_kboot.py
run leak-check python3 tools/test_leak_check.py
run ps3disk-guard linux/tests/ps3disk-guard.sh
[ $slow = 1 ] && run autogm bash -c "$(declare -f t_autogm); t_autogm"
[ $slow = 1 ] && run ssdimg-vm tools/ssdimg/vm-e2e.sh

echo; printf '%-18s %-6s %s\n' suite result seconds
for i in "${!names[@]}"; do printf '%-18s %-6s %s\n' "${names[$i]}" "${res[$i]}" "${secs[$i]}"; done
[ $fail = 0 ] && echo "all passed" || echo "FAILED (logs in $L)"
exit $fail
