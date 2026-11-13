#!/bin/bash
# Suite ps3rsx-shadow: ps3rsx's host tests (packages/walnut/drivers/ps3rsx/test): the shadow state, clear_dcbz zeroing,
# the validator, blame, pushbuf, vclear, the VRAM swap init's unwind, the push dump and the address checks, then each rule switched off in a copy.
set -u
cd "$(dirname "$0")/../.."
. tools/suites/lib.sh
O=$PWD/build/tmp/ps3rsx-test
T=packages/walnut/drivers/ps3rsx/test
need gcc
make -C $T O="$O" "$O/shadow-equiv" "$O/shadow-equiv-fast" "$O/zero-test" "$O/validate-test" \
	"$O/blame-test" "$O/pushbuf-test" "$O/vclear-test" "$O/validate-capture" "$O/pushdump-test" \
	"$O/p4-test" "$O/p4-test-count" "$O/p4-test-dump" "$O/p4-fuzz" || exit 1
"$O/shadow-equiv" 200000 20000 1 || exit 1
"$O/shadow-equiv-fast" 1000000 100000 2 || exit 1
"$O/zero-test" 20000 1 || exit 1
"$O/validate-test" || exit 1
"$O/blame-test" || exit 1
"$O/pushbuf-test" || exit 1
"$O/vclear-test" || exit 1
# the VRAM swap's init unwinds every failure (the GART bounce out of rsx->gart before vs is freed), and its fix-off
bash $T/vswap-init-test.sh "$O/vswap-init" || exit 1
# the address checks: their rules (as released, and in the count-only build), the fuzz
"$O/p4-test" || exit 1
"$O/p4-test-count" || exit 1
"$O/p4-test-dump" || exit 1
for seed in 1 2 3; do "$O/p4-fuzz" "$seed" 30000 || exit 1; done
# the push dump's scanner and records, a sample split into validate-capture's inputs
need python3
$T/pushdump-test.sh "$O" || exit 1
# each rule switched off in a copy of validate.c (or the submit model's old behaviour): its case must fail
make -C $T O="$O" fix-off || exit 1
# big-endian run under qemu, when the cross compiler, the sysroot and qemu are there
if command -v powerpc64-linux-gnu-gcc >/dev/null && command -v qemu-ppc64-static >/dev/null &&
   [ -d build/mesa/sysroot ]; then
	make -C $T O="$O" "$O/shadow-equiv-ppc64" "$O/zero-test-ppc64" || exit 1
	qemu-ppc64-static "$O/shadow-equiv-ppc64" 100000 10000 3 || exit 1
	qemu-ppc64-static "$O/zero-test-ppc64" 20000 2 || exit 1
else skip_part ppc64 "no cross gcc, qemu or sysroot"; fi
