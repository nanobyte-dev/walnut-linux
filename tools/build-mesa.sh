#!/bin/bash
# Cross-builds patched Mesa for the PS3. Runs inside the Mesa builder container (see Makefile).
# Inputs (env): MESA_VERSION MESA_TARBALL MESA_FORK MESA_PREFIX SOURCE_DATE_EPOCH JOBS
# Outputs: build/mesa/{src,build}, build/mesa/install<MESA_PREFIX> (lib, share, env.sh)
set -euo pipefail

B=build/mesa
SRC=$B/src
OBJ=$B/build
INSTALL=$B/install
SYSROOT=$B/sysroot
LIBDIR=powerpc64-linux-gnu

# Previous outputs are moved aside, never deleted
if [ -e "$OBJ" ] || [ -e "$INSTALL" ]; then
	OLD="build/old/$(date +%Y%m%d-%H%M%S)-mesa"
	mkdir -p "$OLD"
	for d in "$OBJ" "$INSTALL"; do [ -e "$d" ] && mv "$d" "$OLD/"; done
	echo ">> Previous Mesa outputs moved to $OLD"
fi

echo ">> Unpacking mesa-$MESA_VERSION"
rm -rf "$SRC"
tar -xf "$MESA_TARBALL" -C "$B"
mv "$B/mesa-$MESA_VERSION" "$SRC"

# Our fork: every file we change, whole (mesa/fork mirrors Mesa's tree; its first commit is
# the pristine release, so git diff shows our changes)
echo ">> Laying $MESA_FORK over the release"
cp -r "$MESA_FORK/src/." "$SRC/src/"
# A version suffix keeps libgallium-<version>.so apart from Debian's Mesa of the same release
echo "$MESA_VERSION-ps3" > "$SRC/VERSION"

echo ">> Configuring"
# -O2 with debug info (for perf), without assertions: they sit in Mesa's per-draw paths, and the
# PPE is CPU-bound in games
meson setup "$OBJ" "$SRC" --cross-file sysroot/mesa-cross-ppc64.ini --wrap-mode=nodownload \
	--prefix="$MESA_PREFIX" --libdir=lib --buildtype=debugoptimized -Db_ndebug=true \
	-Dgallium-drivers=nouveau,softpipe -Dvulkan-drivers= -Dllvm=disabled \
	-Dgallium-va=disabled -Dvideo-codecs= -Dplatforms=x11 -Dglx=dri -Degl=enabled \
	-Dgbm=enabled -Dglvnd=enabled -Dgles1=disabled -Dgles2=enabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled

echo ">> Building"
ninja -C "$OBJ" -j"$JOBS"
DESTDIR="$PWD/$INSTALL" meson install -C "$OBJ" --no-rebuild --quiet
P="$INSTALL$MESA_PREFIX"

# The PS3 root filesystem has no GL/X client libraries: bundle every library the installed
# files (and glvnd's dispatch libraries, which the tests link against) need from the sysroot,
# except glibc and the compiler runtime, which are part of the base system.
echo ">> Bundling runtime libraries from the sysroot"
needed() { powerpc64-linux-gnu-readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
for l in libEGL.so.1 libGLESv2.so.2 libGL.so.1 libGLX.so.0 libOpenGL.so.0; do
	cp "$SYSROOT/usr/lib/$LIBDIR/$l" "$P/lib/$l"
done
queue=($(find "$P" -type f -name '*.so*'))
while [ ${#queue[@]} -gt 0 ]; do
	f=${queue[0]}; queue=("${queue[@]:1}")
	for lib in $(needed "$f"); do
		case $lib in
		libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|ld64.so.*|libgcc_s.so.*|libstdc++.so.*) continue ;;
		esac
		[ -e "$P/lib/$lib" ] && continue
		src="$SYSROOT/usr/lib/$LIBDIR/$lib"
		[ -e "$src" ] || { echo "!! $lib (needed by $f) not in the sysroot"; exit 1; }
		cp "$src" "$P/lib/$lib"
		queue+=("$P/lib/$lib")
	done
done

cat > "$P/env.sh" <<EOF
# Patched Mesa $MESA_VERSION-ps3: source this before running GL/EGL programs.
export PATH=$MESA_PREFIX/bin:\$PATH
export LD_LIBRARY_PATH=$MESA_PREFIX/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}
export LIBGL_DRIVERS_PATH=$MESA_PREFIX/lib/dri
export GBM_BACKENDS_PATH=$MESA_PREFIX/lib/gbm
export __EGL_VENDOR_LIBRARY_FILENAMES=$MESA_PREFIX/share/glvnd/egl_vendor.d/50_mesa.json
export __GLX_VENDOR_LIBRARY_NAME=mesa
export MESA_SHADER_CACHE_DISABLE=true
EOF
echo ">> Done: $(find "$P" -name 'libgallium-*.so')"
