#!/bin/sh
# extract.sh OUTDIR [PATCH]: the sources of ps3guard_sim (vm-e2e.sh) into OUTDIR: ps3guard_sim.c and
# Kbuild from here, and from local/0045 itself: ps3disk_guard.h (packages/patched/kernel/linux/tests/guard-header.sh), then
# the patch's own ps3disk.c code the simulation runs unchanged: guard-struct.inc (struct
# ps3disk_guard) and guard-patch.inc (ps3disk_region_idx, ps3disk_guard_of, ps3disk_region_policy,
# ps3disk_set_read_only and the whole sysfs interface: write_guard/'s show and store functions, the
# attributes, is_visible, the groups).
set -eu
H=$(cd "$(dirname "$0")" && pwd)
O=$1
P=${2:-$H/../../../linux/patches/local/0045-ps3disk-write-guard.patch}
mkdir -p "$O"
cp "$H/ps3guard_sim.c" "$H/Kbuild" "$O/"
"$H/../../../linux/tests/guard-header.sh" "$P" > "$O/ps3disk_guard.h"
# the added lines of the ps3disk.c diff, without the '+'
awk '/^diff --git a\/drivers\/block\/ps3disk\.c / {on=1; next} /^diff --git / {on=0}
     on && /^\+/ && !/^\+\+\+ / {print substr($0, 2)}' "$P" > "$O/added.c"
# FROM..TO (awk regexps, brackets for metacharacters; the first TO at or after FROM, inclusive) of the added lines
take() { awk -v a="$1" -v b="$2" '!on && $0 ~ a {on=1} on {print} on && $0 ~ b {exit}' "$O/added.c"; }
{
	echo "/* from $(basename "$P") (extract.sh): the patch's ps3disk.c code, unchanged */"
	take '^struct ps3disk_guard [{]$' '^[}];$'
} > "$O/guard-struct.inc"
{
	echo "/* from $(basename "$P") (extract.sh): the patch's ps3disk.c code, unchanged */"
	take '^static unsigned int ps3disk_region_idx[(]' '^[}]$'
	take '^static struct ps3disk_guard [*]ps3disk_guard_of[(]' '^[}]$'
	take '^static enum ps3disk_guard_policy ps3disk_region_policy[(]' '^[}]$'
	take '^static int ps3disk_set_read_only[(]' '^[}]$'
	# the sysfs interface: its comment up to the groups array
	awk '/^ \* \/sys\/block\/ps3dX\/write_guard\/:$/ {print "/*"; on=1} on {print}
	     on && /^static const struct attribute_group \*ps3disk_disk_groups\[\] = \{$/ {g=1}
	     g && /^\};$/ {exit}' "$O/added.c"
} > "$O/guard-patch.inc"
rm "$O/added.c"
grep -qF 'struct ps3disk_guard {' "$O/guard-struct.inc" || { echo "extract.sh: no struct ps3disk_guard in $P" >&2; exit 1; }
for f in 'ps3disk_region_idx(' 'ps3disk_guard_of(' 'ps3disk_region_policy(' \
	'ps3disk_set_read_only(' 'ranges_store(' 'sealed_store(' 'ps3disk_disk_groups[]'; do
	grep -qF "$f" "$O/guard-patch.inc" || { echo "extract.sh: no $f in $P" >&2; exit 1; }
done
