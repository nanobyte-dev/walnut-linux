#!/bin/sh
# guard-header.sh [PATCH]: prints drivers/block/ps3disk_guard.h, the write guard's range logic, as
# local/0045 adds it (a new file in the patch: its hunk without the hunk header and the '+'), so
# userspace tests compile the kernel's own code without a kernel tree: packages/patched/kernel/linux/tests/ps3disk-guard.sh,
# tools/ssdimg (the boot tool's guard model, test_ssdimg.py and vm-e2e.sh).
P=${1:-$(dirname "$0")/../patches/local/0045-ps3disk-write-guard.patch}
out=$(awk '/^\+\+\+ b\/drivers\/block\/ps3disk_guard\.h$/ {on=1; next}
     on && /^diff --git / {exit}
     on && /^@@/ {next}
     on {print substr($0, 2)}' "$P")
[ -n "$out" ] || { echo "no drivers/block/ps3disk_guard.h in $P" >&2; exit 1; }
printf '%s\n' "$out"
