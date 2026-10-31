+++
component = "sdl3"
patches = ["patches/0003-blit-a-faster-scalar-8888-alpha-blit-between-differe.patch"]
aliases = ["libsdl3 0003"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = []
+++
# SDL3: a faster scalar 8888 alpha blit between different formats

Without SIMD (PowerPC has no SSE4.1, AVX2, LSX or NEON path) every 32-bit sprite with per-pixel alpha blitted
onto a surface of another 8888 format goes through `Blit8888to8888PixelAlphaSwizzle`, whose macro read the
four shifts and the alpha mask from both formats for every pixel and blended fully transparent and fully
opaque pixels like any other. On the PS3 that loop was 55% of Warmux's frame (SDL 1.2 through sdl12-compat:
ARGB8888 sprites onto the XRGB8888 screen). The shifts are locals now, the RGB bytes move only when their
places differ, and alpha 0 and 255 skip the blend. Bit-identical: `packages/patched/graphics/libsdl3/tests/blit-swizzle-test.c`
compares the two loops on 11 million random pixels (a host test, not yet a `tools/run-tests.sh` suite).

On the PS3 (2026-10-29, same boot) Warmux went from 7.0 to 9.8 fps on its counter, and to 17.5 with
[sdl12-compat-window-texture-format](../sdl12-compat-window-texture-format/patch.md) as well
([Warmux](../../games/warmux/technical.md)).
