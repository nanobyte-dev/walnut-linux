+++
id = "gl-driver-on-gl21"
games = ["openttd"]
driver_patches = ["libsdl3/0001", "libsdl3/0002"]
game_patches = ["openttd/0001", "openttd/0002"]
sessions = []
batches = ["2026-10-16-openttd-smokes", "2026-10-17-ps3rsx-249-first-boot", "2026-10-18-batch8"]
+++

# OpenTTD: the OpenGL driver on GL 2.1, and SDL3's blue cast (2026-10-16 to 10-18)

**Symptom.** The research expected the GL mode to fail at init with no GPU risk, and it did: stock
`-v sdl-opengl` refused to start on nv30 with "Single element texture formats not supported"
(gamedb:batch/2026-10-16-openttd-smokes). The software path started, but with a strong blue cast.

**Cause.**

- The OpenGL driver needs GL 3.0 or ARB_texture_rg for its single-channel textures; its VAO check wants
  ARB *and* APPLE vertex array objects below GL 3.0 (an upstream bug: Mesa has no APPLE one); and its
  sprite shaders use `texture2DLod` in the fragment stage, which needs ARB_shader_texture_lod (nv30 has
  no TXL).
- SDL3's accelerated window framebuffer was BGRA8888 on big-endian, so OpenTTD's 0xAARRGGBB words came out
  with red and blue swapped.

**Fix.**

- openttd 0001: the OpenGL driver on GL 2.1 without ARB_texture_rg or ARB_shader_texture_lod
  (GL_LUMINANCE8 for the single-channel textures, the VAO check fixed, no LOD in the shader), checked on
  llvmpipe forced to GL 2.1; 0002: the sprite recolour applied in the GLSL 1.10 sprite shader. The GL
  driver ran on the PS3 on 2026-10-18: 21 steady fps on the title screen (gamedb:batch/2026-10-18-batch8).
- libsdl3 +ps3.242 fixed the colours but converted every frame twice on the CPU (about 2 fps, the audio
  starved); +ps3.254 (0002) uploads ARGB/XRGB8888 natively with GL_BGRA and 8_8_8_8_REV: 8 fps on the
  title game (gamedb:batch/2026-10-17-ps3rsx-249-first-boot). The fix serves any SDL window-surface app.
