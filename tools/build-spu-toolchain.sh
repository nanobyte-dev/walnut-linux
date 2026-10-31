#!/bin/bash
# Builds the spu-elf cross toolchain for the Cell's SPEs: binutils (packages/patched/toolchain/spu/binutils/*.patch), GCC (C
# only, with our port of the SPU backend: packages/patched/toolchain/spu/gcc/*.patch) and newlib (packages/patched/toolchain/spu/newlib-*/*.patch), installed under
# build/spu/install/opt/spu. Runs as the user inside the SPU builder container.
# Inputs (env): SPU_GCC_VERSION SPU_BINUTILS_VERSION SPU_NEWLIB_VERSION SOURCE_DATE_EPOCH JOBS
set -euo pipefail
{
D=build/downloads
OUT=${SPU_OUT:-build/spu}	# a separate tree for experiments (SPU_OUT=build/spu-<name>)
SRC=$OUT/src
PREFIX=/opt/spu
DEST="$PWD/$OUT/install"
TARGET=spu-elf
export SOURCE_DATE_EPOCH

# Previous outputs are moved aside, never overwritten
if [ -e "$OUT" ]; then
	OLD="build/old/$(date +%Y%m%d-%H%M%S)-$(basename "$OUT")"
	mkdir -p "$OLD" && mv "$OUT" "$OLD/"
	echo ">> Previous SPU toolchain moved to $OLD"
fi
mkdir -p "$SRC"
tar -xf "$D/binutils-$SPU_BINUTILS_VERSION.tar.xz" -C "$SRC"
tar -xf "$D/gcc-$SPU_GCC_VERSION.tar.xz" -C "$SRC"
tar -xf "$D/newlib-$SPU_NEWLIB_VERSION.tar.gz" -C "$SRC"
for p in packages/patched/toolchain/spu/gcc/*.patch; do patch -d "$SRC/gcc-$SPU_GCC_VERSION" -p1 -s < "$p"; done
for p in packages/patched/toolchain/spu/binutils/*.patch; do [ -e "$p" ] || continue; patch -d "$SRC/binutils-$SPU_BINUTILS_VERSION" -p1 -s < "$p"; done
# Newlib patches: packages/patched/toolchain/spu/newlib-<version>/, else newlib-<major.minor>/, else newlib/ (1.20.0)
NLPATCHES=packages/patched/toolchain/spu/newlib
for d in "packages/patched/toolchain/spu/newlib-$SPU_NEWLIB_VERSION" "packages/patched/toolchain/spu/newlib-$(echo "$SPU_NEWLIB_VERSION" | cut -d. -f1,2)"; do
	[ -d "$d" ] && { NLPATCHES=$d; break; }
done
echo ">> newlib patches: $NLPATCHES"
for p in "$NLPATCHES"/*.patch; do [ -e "$p" ] || continue; patch -d "$SRC/newlib-$SPU_NEWLIB_VERSION" -p1 -s < "$p"; done
export PATH="$DEST$PREFIX/bin:$PATH"
export CFLAGS="-O2" CXXFLAGS="-O2"

echo ">> binutils"
mkdir -p "$OUT/obj-binutils" && cd "$OUT/obj-binutils"
"$PWD/../src/binutils-$SPU_BINUTILS_VERSION/configure" --target=$TARGET --prefix=$PREFIX \
	--disable-nls --disable-werror --disable-gdb --disable-sim > ../binutils-configure.log
make -j"$JOBS" > ../binutils-build.log
make install DESTDIR="$DEST" > /dev/null
cd - > /dev/null

echo ">> GCC, stage 1 (compiler only)"
mkdir -p "$OUT/obj-gcc" && cd "$OUT/obj-gcc"
"$PWD/../src/gcc-$SPU_GCC_VERSION/configure" --target=$TARGET --prefix=$PREFIX \
	--with-build-sysroot="$DEST$PREFIX/$TARGET" --enable-languages=c --with-newlib \
	--disable-nls --disable-shared --disable-threads --disable-libssp --disable-werror \
	--with-as="$DEST$PREFIX/bin/$TARGET-as" --with-ld="$DEST$PREFIX/bin/$TARGET-ld" \
	> ../gcc-configure.log
make -j"$JOBS" all-gcc > ../gcc-stage1.log
make install-gcc DESTDIR="$DEST" > /dev/null
cd - > /dev/null

echo ">> newlib"
mkdir -p "$OUT/obj-newlib" && cd "$OUT/obj-newlib"
# 1.20.0 is K&R-era C that needs -std=gnu17 -fpermissive with GCC 16; 4.x builds as is (patched)
case $SPU_NEWLIB_VERSION in 1.*) NLFLAGS="-O2 -std=gnu17 -fpermissive";; *) NLFLAGS="-O2";; esac
CFLAGS_FOR_TARGET="$NLFLAGS" "$PWD/../src/newlib-$SPU_NEWLIB_VERSION/configure" --target=$TARGET \
	--prefix=$PREFIX --disable-nls > ../newlib-configure.log
make -j"$JOBS" > ../newlib-build.log
make install DESTDIR="$DEST" > /dev/null
cd - > /dev/null

echo ">> GCC, libgcc"
cd "$OUT/obj-gcc"
make -j"$JOBS" all-target-libgcc > ../gcc-libgcc.log
make install-target-libgcc DESTDIR="$DEST" > /dev/null
cd - > /dev/null

"$DEST$PREFIX/bin/$TARGET-gcc" --version | head -1
echo ">> Done: $DEST$PREFIX"
exit
}
