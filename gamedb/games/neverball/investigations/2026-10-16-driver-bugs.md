+++
id = "driver-bugs"
games = ["neverball", "supertuxkart", "luanti"]
driver_patches = ["mesa/0012", "mesa/0013", "mesa/0014"]
game_patches = []
sessions = []
batches = ["2026-10-16-mesa-186-checks", "2026-10-16-mesa-223-neverball-wedge", "2026-10-16-mesa-223-checks", "2026-10-16-mesa-225"]
+++

# Neverball: L8A8 swapped, 1-pixel mipmap levels, and graph exception 0x105 (2026-10-16)

**Symptom.** Neverball was the first game to show three driver bugs in one day: its fonts and HUD drew with
wrong alpha, its non-square textures broke the GPU's mipmap generation, and on Mesa ps3.223 it wedged the
RSX with a graph exception (gamedb:batch/2026-10-16-mesa-223-neverball-wedge: `nouveau: kernel rejected
pushbuf`, 89 lines and 35 000 dump lines, `ps3rsx: graph exception`).

**Evidence and cause.** Each was reduced to a test in ps3-gpu-tools' mesa-tests:

- **gl-texfmt**: GL_LUMINANCE_ALPHA (L8A8) textures were sampled with L and A swapped
  (gamedb:batch/2026-10-16-mesa-186-checks). Fonts in many games use it.
- **gl-genmip**: GPU mipmap generation of non-square textures: the 1-pixel levels are swizzled render
  targets the ps3rsx validator refuses, so the push was lost (`rejected push: swizzled render target 1 pixel
  wide`). SuperTuxKart and Luanti hit it too.
- **0x105**: nv30's 3D blit sampled a mip level at an offset not 128-byte aligned: Mesa's CPU mipmap
  generation reading back the 2x2 level of a 32x32 L8A8 texture. Any game generating mipmaps for L8A8, L8,
  A8, I8, 4444 or 5551 could hit it.

**Fix.** Mesa 0012 (L8A8) and 0013 (genmip) in ps3.223 (gamedb:batch/2026-10-16-mesa-223-checks); Mesa 0014
in ps3.225, with gl-mipread passing all 70 cases (gamedb:batch/2026-10-16-mesa-225); ps3rsx 0.249 refuses
texture offsets that are not 128-byte aligned (8e99b76), so a bad blit is refused instead of wedging the
GPU. No wedge in the Neverball runs since. The mechanisms are in [DRIVER.md](../../../../docs/DRIVER.md)
and [RSX.md](../../../../docs/RSX.md).
