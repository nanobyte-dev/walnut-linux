#!/bin/bash
# Builds the PS3 kernel from a pristine tree. Runs inside the builder container (see Makefile).
# Inputs (env): KERNEL_VERSION KERNEL_TARBALL KERNEL_SERIES KERNEL_DEFCONFIG KERNEL_FRAGMENTS
#               KERNEL_LOCALVERSION SOURCE_DATE_EPOCH JOBS
# Outputs: build/kernel/obj (build dir), build/kernel/stage/{boot,lib/modules}
set -euo pipefail

B=${KERNEL_BUILD_DIR:-build/kernel}
SRC=$B/src
OBJ=$B/obj
STAGE=$B/stage

export ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu-
export KBUILD_BUILD_TIMESTAMP="$(date -u -d "@$SOURCE_DATE_EPOCH")"
export KBUILD_BUILD_USER=ps3 KBUILD_BUILD_HOST=ps3-builder KBUILD_BUILD_VERSION=1
export LOCALVERSION="$KERNEL_LOCALVERSION"
MAKE=(make -C "$SRC" O="$PWD/$OBJ" -j"$JOBS")

echo ">> Unpacking linux-$KERNEL_VERSION"
rm -rf "$SRC"
mkdir -p "$B"
tar -xf "$KERNEL_TARBALL" -C "$B"
mv "$B/linux-$KERNEL_VERSION" "$SRC"

echo ">> Applying patches from $KERNEL_SERIES"
PATCHDIR="$(dirname "$KERNEL_SERIES")"
grep -v -e '^\s*#' -e '^\s*$' "$KERNEL_SERIES" | while read -r p; do
	patch -d "$SRC" -p1 --batch --forward --no-backup-if-mismatch --quiet < "$PATCHDIR/$p" ||
		{ echo "!! patch failed: $p"; exit 1; }
done

echo ">> Configuring ($KERNEL_DEFCONFIG + $KERNEL_FRAGMENTS)"
mkdir -p "$OBJ"
"${MAKE[@]}" -s "$KERNEL_DEFCONFIG"
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$OBJ" "$OBJ/.config" $KERNEL_FRAGMENTS >/dev/null
"${MAKE[@]}" -s olddefconfig
# Fail loudly if a fragment option didn't stick (e.g. a missing dependency)
for frag in $KERNEL_FRAGMENTS; do
	grep -E '^(CONFIG_[A-Z0-9_]+=|# CONFIG_[A-Z0-9_]+ is not set)' "$frag" | while read -r line; do
		grep -qxF "$line" "$OBJ/.config" || echo "!! not applied: $line"
	done
done

echo ">> Building"
"${MAKE[@]}" vmlinux modules

echo ">> Staging"
KREL="$("${MAKE[@]}" -s kernelrelease)"
rm -rf "$STAGE"
mkdir -p "$STAGE/boot"
"${MAKE[@]}" -s INSTALL_MOD_PATH="$PWD/$STAGE" INSTALL_MOD_STRIP=1 modules_install
powerpc64-linux-gnu-strip -o "$STAGE/boot/vmlinux-$KREL" "$OBJ/vmlinux"
cp "$OBJ/.config" "$STAGE/boot/config-$KREL"
cp "$OBJ/System.map" "$STAGE/boot/System.map-$KREL"
ln -sf "vmlinux-$KREL" "$STAGE/boot/vmlinux"
echo ">> Done: $KREL"
