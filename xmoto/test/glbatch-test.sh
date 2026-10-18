#!/bin/bash
# glbatch-test.sh [FRAMES]: xmoto's GL layer (drawlib/GLBatch, patches 0001/0004) on the PC's llvmpipe,
# EGL surfaceless into an FBO: glbatch-test.cpp draws a random scene of DrawLib primitives, state
# changes, the game's own draws and static ranges for FRAMES frames (default 400), with
# XMOTO_GL_STREAM=0 and =1, and compares every frame's pixels (a hash) and the GL errors. Needs the
# patched work tree (make xmoto-work), g++, the host's EGL and Mesa. Scratch in build/tmp/glbatch-test.
set -eu
top=$(cd "$(dirname "$0")/../.." && pwd)
X=$top/build/work/xmoto
T=$top/build/tmp/glbatch-test
[ -e "$X/src/drawlib/GLBatch.cpp" ] || { echo "!! no $X: make xmoto-work"; exit 1; }
rm -rf "$T"; mkdir -p "$T"/stub/{helpers,common,include}
cat > "$T/stub/helpers/Log.h" <<'H'
#pragma once
#include <stdio.h>
#define LogError(f, ...) fprintf(stderr, "E " f "\n", ##__VA_ARGS__)
#define LogWarning(f, ...) fprintf(stderr, "W " f "\n", ##__VA_ARGS__)
#define LogInfo(f, ...) fprintf(stderr, "I " f "\n", ##__VA_ARGS__)
H
printf '#pragma once\n#ifndef ENABLE_OPENGL\n#define ENABLE_OPENGL\n#endif\n' > "$T/stub/common/BuildConfig.h"
: > "$T/stub/include/xm_SDL.h"
printf '#pragma once\n#include <glad.h>\n#include "drawlib/GLBatch.h"\n' > "$T/stub/include/xm_OpenGL.h"
g++ -O1 -g -w -DENABLE_OPENGL -I"$T/stub" -I"$X/src" -I"$X/vendor/glad/include" "$(dirname "$0")/glbatch-test.cpp" \
	"$X/src/drawlib/GLBatch.cpp" -x c "$X/vendor/glad/src/glad.c" -o "$T/glbatch-test" -lEGL -ldl -lm
export __EGL_VENDOR_LIBRARY_FILENAMES=${__EGL_VENDOR_LIBRARY_FILENAMES:-/usr/share/glvnd/egl_vendor.d/50_mesa.json}
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
rc=0
for m in 0 1; do
	XMOTO_GL_STREAM=$m XMOTO_GL_STATS=1 "$T/glbatch-test" "${1:-400}" > "$T/stream$m.txt" 2> "$T/stream$m.err" || rc=1
done
grep -h '^GL \|GL stats' "$T/stream1.err" | head -2
grep -h 'GL error' "$T"/stream*.err | head -5
if cmp -s "$T/stream0.txt" "$T/stream1.txt" && [ $rc = 0 ]; then
	echo "ok: $(grep -c '^frame' "$T/stream1.txt") frames, the same pixels with XMOTO_GL_STREAM=0 and 1, no GL errors"
	rm -rf "$T"
else
	echo "!! differs or GL errors: $T"; exit 1
fi
