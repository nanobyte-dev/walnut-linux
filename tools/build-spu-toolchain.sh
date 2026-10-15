#!/bin/bash
# Builds the spu-elf cross toolchain for the Cell's SPEs: binutils, GCC (C only) and newlib,
# installed under build/spu/install/opt/spu. Runs as the user inside the SPU builder container.
# Inputs (env): SPU_GCC_VERSION SPU_BINUTILS_VERSION SPU_NEWLIB_VERSION SOURCE_DATE_EPOCH JOBS
set -euo pipefail
{
D=build/downloads
OUT=build/spu
SRC=$OUT/src
PREFIX=/opt/spu
DEST="$PWD/$OUT/install"
TARGET=spu-elf
export SOURCE_DATE_EPOCH

# Previous outputs are moved aside, never overwritten
if [ -e "$OUT" ]; then
	OLD="build/old/$(date +%Y%m%d-%H%M%S)-spu"
	mkdir -p "$OLD" && mv "$OUT" "$OLD/"
	echo ">> Previous SPU toolchain moved to $OLD"
fi
mkdir -p "$SRC"
tar -xf "$D/binutils-$SPU_BINUTILS_VERSION.tar.xz" -C "$SRC"
tar -xf "$D/gcc-$SPU_GCC_VERSION.tar.xz" -C "$SRC"
tar -xf "$D/newlib-$SPU_NEWLIB_VERSION.tar.gz" -C "$SRC"
export PATH="$DEST$PREFIX/bin:$PATH"
# GCC 9 marks SPU obsolete (removed in 10): --enable-obsolete. The old sources predate GCC 10's -fno-common default and newer C standards
export CFLAGS="-O2 -fcommon" CXXFLAGS="-O2"

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
	--with-build-sysroot="$DEST$PREFIX/$TARGET" --enable-languages=c --with-newlib --enable-obsolete \
	--disable-nls --disable-shared --disable-threads --disable-libssp --disable-werror \
	--with-as="$DEST$PREFIX/bin/$TARGET-as" --with-ld="$DEST$PREFIX/bin/$TARGET-ld" \
	> ../gcc-configure.log
make -j"$JOBS" all-gcc > ../gcc-stage1.log
make install-gcc DESTDIR="$DEST" > /dev/null
cd - > /dev/null

echo ">> newlib"
mkdir -p "$OUT/obj-newlib" && cd "$OUT/obj-newlib"
CFLAGS_FOR_TARGET="-O2" "$PWD/../src/newlib-$SPU_NEWLIB_VERSION/configure" --target=$TARGET \
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
