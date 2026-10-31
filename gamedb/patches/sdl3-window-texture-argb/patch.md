+++
component = "sdl3"
patches = ["patches/0001-video-window-framebuffer-texture-in-ARGB8888-XRGB888.patch"]
aliases = ["libsdl3 0001"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-16-openttd-smokes"]
+++
# SDL3: the window framebuffer texture in ARGB8888/XRGB8888 on big-endian too

The accelerated window framebuffer creates the window surface in the format of its texture, the
renderer's first plain format. The OpenGL and GLES2 renderers list only byte-order formats, so on big-endian
the first is BGRA8888. Applications that write 0xAARRGGBB words into the window surface without reading its
format (OpenTTD through sdl2-compat; SDL2 always gave them ARGB8888) then showed their alpha byte as blue: a
strong blue cast on the PS3, right colours with `SDL_FRAMEBUFFER_ACCELERATION=0`. SDL now prefers ARGB8888 or
XRGB8888 from the renderer's list, and when the renderer has neither, makes the texture in XRGB8888 and lets
the renderer convert.

Found on OpenTTD's first runs (gamedb:batch/2026-10-16-openttd-smokes,
[gamedb:openttd/gl-driver-on-gl21](../../games/openttd/investigations/2026-10-17-gl-driver-on-gl21.md)). The
colours were right with this alone, but every frame was converted on the CPU, which
[sdl3-gl-native-argb](../sdl3-gl-native-argb/patch.md) removed.
