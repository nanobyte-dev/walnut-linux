#!/bin/bash
# linux-config.sh <source tree> <flavour> <output>: the full kernel .config of a flavour (packages/patched/kernel/linux/package.conf):
# DEFCONFIG, then the flavour's packages/patched/kernel/linux/config/ fragments merged in order (merge_config.sh), then
# olddefconfig. Fails if a fragment option didn't stick or a REQUIRE_* list of package.conf is not met
# (tools/linux-config-check.sh).
# Env: LINUX_VARIANT=<name> (debug flavour only): config/ps3-<name>.config is merged last, e.g. lockdep
# (docs/PACKAGING.md "Kernels"); LINUX_CONFIG_CC: the compiler (default powerpc64-linux-gnu-gcc-16, the
# deb builder's), for a host run of the configuration step.
# Runs in the deb builder (powerpc64-linux-gnu-gcc-16, the compiler Debian's packaging uses).
set -euo pipefail
{
SRC=$(realpath "$1") FLAVOUR=$2 OUTPUT=$3
. packages/patched/kernel/linux/package.conf
var=FRAGMENTS_${FLAVOUR//-/_}
[ -n "${!var:-}" ] || { echo "!! packages/patched/kernel/linux/package.conf: no $var" >&2; exit 1; }
frags=()
for f in ${!var}; do frags+=("$PWD/packages/patched/kernel/linux/config/$f"); done
if [ -n "${LINUX_VARIANT:-}" ]; then
	[ "$FLAVOUR" = debug ] || { echo "!! LINUX_VARIANT=$LINUX_VARIANT: only the debug flavour has variants" >&2; exit 1; }
	[[ $LINUX_VARIANT =~ ^[a-z]+$ ]] && [ -f "$PWD/packages/patched/kernel/linux/config/ps3-$LINUX_VARIANT.config" ] || \
		{ echo "!! LINUX_VARIANT=$LINUX_VARIANT: no packages/patched/kernel/linux/config/ps3-$LINUX_VARIANT.config" >&2; exit 1; }
	frags+=("$PWD/packages/patched/kernel/linux/config/ps3-$LINUX_VARIANT.config")
fi
O=$(mktemp -d "${TMPDIR:-/tmp}/linux-config.XXXXXX")
trap 'rm -rf "$O"' EXIT
MAKE=(make -s -C "$SRC" O="$O" ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu- CC=${LINUX_CONFIG_CC:-powerpc64-linux-gnu-gcc-16})
"${MAKE[@]}" "$DEFCONFIG" >/dev/null
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$O" "$O/.config" "${frags[@]}" >/dev/null
"${MAKE[@]}" olddefconfig >/dev/null
# the checks (tools/linux-config-check.sh): every fragment option stuck, and the package.conf
# REQUIRE_* lists (hardening options kept, the must-stay-off drivers off) hold; a failure stops the build
tools/linux-config-check.sh "$FLAVOUR" "$O/.config" "${frags[@]}" || { echo "!! $FLAVOUR: the .config is not what its fragments and package.conf say" >&2; exit 1; }
cp "$O/.config" "$OUTPUT"
exit
}
