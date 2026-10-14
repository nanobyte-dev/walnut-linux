#!/bin/bash
# Cross-compiles ClassiCube for the PS3 (SDL3 window/gamepad, desktop GL1, OpenAL) in the Mesa
# builder container against the Mesa sysroot (libsdl3-dev is in mesa/sysroot-packages.txt).
# Inputs (env): CC_TARBALL CC_TEXPACK  Output: build/classicube/install (ClassiCube + texpacks/)
set -euo pipefail

SYSROOT=/work/build/mesa/sysroot
SRC=build/classicube/src
OUT=build/classicube/install

rm -rf "$SRC" "$OUT"
mkdir -p "$SRC" "$OUT/texpacks" "$OUT/audio"
tar -xzf "$CC_TARBALL" -C "$SRC" --strip-components=1

# linux.mk sets CFLAGS itself, so the cross flags go into CC; LIBS without X11/Xi (SDL3 window).
# The sysroot's libraries first: the cross toolchain's own glibc is older than the snapshot's
# (libSDL3 needs GLIBC_2.43 libm symbols).
make -C "$SRC" -f misc/makefiles/linux.mk -j"$(nproc)" RELEASE=1 BUILD_SDL3=1 \
	CC="powerpc64-linux-gnu-gcc --sysroot=$SYSROOT -mcpu=cell -mtune=cell" \
	LDFLAGS="-rdynamic -L$SYSROOT/usr/lib/powerpc64-linux-gnu" \
	LIBS="-lSDL3 -lpthread -lGL -ldl -lm"

install -m 755 "$SRC/ClassiCube" "$OUT/"
# the required texture pack (the game would download it on first start otherwise)
install -m 644 "$CC_TEXPACK" "$OUT/texpacks/classicube.zip"
echo ">> ClassiCube: $(du -sh "$OUT" | cut -f1) in $OUT"
