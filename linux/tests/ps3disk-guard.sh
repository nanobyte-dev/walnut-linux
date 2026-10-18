#!/bin/bash
# ps3disk-guard.sh [ROUNDS]: the ps3disk write guard's range logic (linux/patches/local/0045) in
# userspace. drivers/block/ps3disk_guard.h is a new file in the patch, so it is taken straight from
# the patch (no kernel tree needed) and compiled with ps3disk-guard-test.c: once plain with -Werror,
# once with ASan/UBSan when the compiler has them. Output in build/tmp/tests/ps3disk-guard/.
set -euo pipefail
cd "$(dirname "$0")/../.."
P=linux/patches/local/0045-ps3disk-write-guard.patch
O=build/tmp/tests/ps3disk-guard
command -v gcc >/dev/null || { echo "skip: no gcc"; exit 77; }
mkdir -p "$O"
# the new file's hunk: from its +++ line to the next diff, without the hunk header and the '+'
awk '/^\+\+\+ b\/drivers\/block\/ps3disk_guard\.h$/ {on=1; next}
     on && /^diff --git / {exit}
     on && /^@@/ {next}
     on {print substr($0, 2)}' "$P" > "$O/ps3disk_guard.h"
[ -s "$O/ps3disk_guard.h" ] || { echo "no drivers/block/ps3disk_guard.h in $P" >&2; exit 1; }
CF=(-std=gnu11 -O2 -Wall -Wextra -Werror -I "$O")
gcc "${CF[@]}" -o "$O/test" linux/tests/ps3disk-guard-test.c
"$O/test" "${1:-2000}"
if echo 'int main(void){return 0;}' | gcc -fsanitize=address,undefined -x c -o "$O/probe" - 2>/dev/null; then
	gcc "${CF[@]}" -g -fsanitize=address,undefined -fno-sanitize-recover=all -o "$O/test-san" \
		linux/tests/ps3disk-guard-test.c
	"$O/test-san" "${1:-500}" 12345
else echo "(sanitizer run skipped: gcc has no ASan/UBSan here)"; fi
