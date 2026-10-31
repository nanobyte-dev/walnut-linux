+++
component = "sdl3"
patches = ["patches/0002-render-opengl-upload-ARGB8888-ABGR8888-natively-on-b.patch"]
aliases = ["libsdl3 0002"]
category = "performance"
switch = "none"
default = "always on"
forwarded = "no"
tests = []
batches = ["2026-10-17-ps3rsx-249-first-boot"]
+++
# SDL3's OpenGL renderer uploads ARGB8888/ABGR8888 natively on big-endian

The OpenGL renderer listed only byte-order formats and uploaded them with GL_UNSIGNED_BYTE, so on big-endian
ARGB8888/XRGB8888 textures (the window framebuffer, most software-rendered frames) got a native texture in
another format and SDL converted every update on the CPU. At 1920x1080 OpenTTD's title screen ran at about
2 fps through the window framebuffer, with audio starving. On big-endian the renderer now also lists
ARGB8888/XRGB8888 (with GL_BGRA) and ABGR8888/XBGR8888 (with GL_RGBA) and uploads them with
GL_UNSIGNED_INT_8_8_8_8_REV, which reads each pixel as a native word: no conversion, the way SDL2 did it.

libsdl3 +ps3.254 took OpenTTD's title game to 8 fps (gamedb:batch/2026-10-17-ps3rsx-249-first-boot,
[gamedb:openttd/gl-driver-on-gl21](../../games/openttd/investigations/2026-10-17-gl-driver-on-gl21.md)).
