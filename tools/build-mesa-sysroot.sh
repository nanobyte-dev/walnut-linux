#!/bin/bash
# Builds the Debian ppc64 sysroot that Mesa is cross-compiled against. Runs as root inside the
# Mesa builder container. Packages are only extracted (no maintainer scripts), so no qemu needed.
# Inputs (env): DEBIAN_SUITE DEBIAN_MIRROR MESA_SYSROOT_PACKAGES SOURCE_DATE_EPOCH OWNER
# Output: build/mesa/sysroot
set -euo pipefail

OUT=build/mesa/sysroot
PACKAGES="$(grep -v -e '^\s*#' -e '^\s*$' "$MESA_SYSROOT_PACKAGES" | paste -sd,)"
export SOURCE_DATE_EPOCH

# The previous sysroot is moved aside, never deleted
if [ -e "$OUT" ]; then
	OLD="build/old/$(date +%Y%m%d-%H%M%S)-mesa-sysroot"
	mkdir -p "$OLD" && mv "$OUT" "$OLD/"
	echo ">> Previous sysroot moved to $OLD"
fi
mkdir -p build/mesa
rm -rf "$OUT.tmp"

mmdebstrap --mode=root --format=directory --arch=ppc64 --variant=extract \
	--keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
	--aptopt='Acquire::Check-Valid-Until "false"' \
	--include="$PACKAGES" \
	"$DEBIAN_SUITE" "$OUT.tmp" "$DEBIAN_MIRROR"

# glibc's libc.so linker script names /lib/powerpc64-linux-gnu/...: the linker prefixes the
# sysroot, so the merged-/usr links have to exist even though nothing was configured.
for d in bin lib lib64 sbin; do
	if [ -e "$OUT.tmp/usr/$d" ] && [ ! -e "$OUT.tmp/$d" ]; then ln -s "usr/$d" "$OUT.tmp/$d"; fi
done
grep -q '/lib/powerpc64-linux-gnu/libc.so.6' "$OUT.tmp/usr/lib/powerpc64-linux-gnu/libc.so"
[ -e "$OUT.tmp/lib/powerpc64-linux-gnu/libc.so.6" ] || { echo "!! /lib link missing"; exit 1; }

touch "$OUT.tmp/.done"
mv "$OUT.tmp" "$OUT"
chown -R "$OWNER" "$OUT"
chown "$OWNER" build/mesa
echo ">> Sysroot: $(du -sh "$OUT" | cut -f1) in $OUT"
