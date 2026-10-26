#!/bin/bash
# ps3disk-guard.sh [ROUNDS]: the ps3disk write guard's range logic (packages/patched/kernel/linux/patches/local/0045) in
# userspace. drivers/block/ps3disk_guard.h is a new file in the patch, so it is taken straight from
# the patch (no kernel tree needed) and compiled with ps3disk-guard-test.c: once plain with -Werror,
# once with ASan/UBSan when the compiler has them. Output in build/tmp/tests/ps3disk-guard/.
set -euo pipefail
cd "$(dirname "$0")/../../../../.."
P=packages/patched/kernel/linux/patches/local/0045-ps3disk-write-guard.patch
O=build/tmp/tests/ps3disk-guard
command -v gcc >/dev/null || { echo "skip: no gcc"; exit 77; }
mkdir -p "$O"
packages/patched/kernel/linux/tests/guard-header.sh "$P" > "$O/ps3disk_guard.h"
CF=(-std=gnu11 -O2 -Wall -Wextra -Werror -I "$O")
gcc "${CF[@]}" -o "$O/test" packages/patched/kernel/linux/tests/ps3disk-guard-test.c
"$O/test" "${1:-2000}"
if echo 'int main(void){return 0;}' | gcc -fsanitize=address,undefined -x c -o "$O/probe" - 2>/dev/null; then
	gcc "${CF[@]}" -g -fsanitize=address,undefined -fno-sanitize-recover=all -o "$O/test-san" \
		packages/patched/kernel/linux/tests/ps3disk-guard-test.c
	"$O/test-san" "${1:-500}" 12345
else echo "(sanitizer run skipped: gcc has no ASan/UBSan here)"; fi
