#!/bin/bash
# run-tests.sh [--slow] [--only NAME]: every host test suite (no PS3 involved), then a table of
# suite, result and seconds; exit 1 when any failed. Output of each suite: build/tmp/tests/<suite>.log
# (shown for failures). A suite whose prerequisites are missing is "skip", not a failure.
#   default  quick suites: bench-selftest, gprefetch, ps3rsx-shadow, ps3-spu, xmoto-glbatch,
#            mesa-forcedxt (Mesa 0038's NV30_FORCE_DXT decision, mesa/tests), ssdimg (the SSD test
#            drive's stamp.py, ps3-imgmap on a fake UFS layer, ps3-vswap's device-mapper backing)
#   --slow   also autogm (ps3-desktop/tests/autogm-check.sh: needs Xvfb+openbox or docker/podman) and
#            ssdimg-vm (tools/ssdimg/vm-e2e.sh: ps3-imgmap on a real big-endian UFS2 image in a KVM
#            guest of the host's kernel; needs docker, qemu-system-x86_64, /dev/kvm)
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
	make -C ps3rsx/test O="$O" "$O/shadow-equiv" "$O/shadow-equiv-fast" || exit 1
	"$O/shadow-equiv" 200000 20000 1 || exit 1
	"$O/shadow-equiv-fast" 1000000 100000 2 || exit 1
	# big-endian run under qemu, when the cross compiler, the sysroot and qemu are there
	if command -v powerpc64-linux-gnu-gcc >/dev/null && command -v qemu-ppc64-static >/dev/null &&
	   [ -d build/mesa/sysroot ]; then
		make -C ps3rsx/test O="$O" "$O/shadow-equiv-ppc64" || exit 1
		qemu-ppc64-static "$O/shadow-equiv-ppc64" 100000 10000 3 || exit 1
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
run ssdimg python3 tools/ssdimg/test_ssdimg.py
[ $slow = 1 ] && run autogm bash -c "$(declare -f t_autogm); t_autogm"
[ $slow = 1 ] && run ssdimg-vm tools/ssdimg/vm-e2e.sh

echo; printf '%-18s %-6s %s\n' suite result seconds
for i in "${!names[@]}"; do printf '%-18s %-6s %s\n' "${names[$i]}" "${res[$i]}" "${secs[$i]}"; done
[ $fail = 0 ] && echo "all passed" || echo "FAILED (logs in $L)"
exit $fail
