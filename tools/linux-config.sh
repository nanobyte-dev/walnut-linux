#!/bin/bash
# linux-config.sh <source tree> <flavour> <output>: the full kernel .config of a flavour (packages/patched/kernel/linux/package.conf):
# DEFCONFIG, then the flavour's packages/patched/kernel/linux/config/ fragments merged in order (merge_config.sh), then
# olddefconfig. Fails if a fragment option didn't stick.
# Runs in the deb builder (powerpc64-linux-gnu-gcc-16, the compiler Debian's packaging uses).
set -euo pipefail
{
SRC=$(realpath "$1") FLAVOUR=$2 OUTPUT=$3
. packages/patched/kernel/linux/package.conf
var=FRAGMENTS_${FLAVOUR//-/_}
[ -n "${!var:-}" ] || { echo "!! packages/patched/kernel/linux/package.conf: no $var" >&2; exit 1; }
frags=()
for f in ${!var}; do frags+=("$PWD/packages/patched/kernel/linux/config/$f"); done
O=$(mktemp -d "${TMPDIR:-/tmp}/linux-config.XXXXXX")
trap 'rm -rf "$O"' EXIT
MAKE=(make -s -C "$SRC" O="$O" ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu- CC=powerpc64-linux-gnu-gcc-16)
"${MAKE[@]}" "$DEFCONFIG" >/dev/null
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$O" "$O/.config" "${frags[@]}" >/dev/null
"${MAKE[@]}" olddefconfig >/dev/null
# every option as the fragments set it (a later fragment overrides an earlier one) should stick;
# reported, not fatal (an option whose dependencies are off is just absent)
while read -r line; do
	grep -qxF "$line" "$O/.config" && continue
	sym=$(echo "$line" | grep -oE 'CONFIG_[A-Z0-9_]+')
	now=$(grep -E "^$sym=|^# $sym is not set" "$O/.config" || echo absent)
	echo "!! $FLAVOUR: not applied: $line (is: $now)" >&2
done < <(cat "${frags[@]}" | sed -nE 's/^(CONFIG_[A-Z0-9_]+)=.*/\1 &/p; s/^# (CONFIG_[A-Z0-9_]+) is not set$/\1 &/p' \
	| awk '{k=$1; sub(/^[^ ]+ /, ""); v[k]=$0; if (!(k in o)) o[k]=n++} END {for (k in o) print v[k]}')
cp "$O/.config" "$OUTPUT"
exit
}
