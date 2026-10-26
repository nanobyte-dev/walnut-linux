+++
id = "big-endian-and-gl-fixes"
games = ["dsda-doom"]
driver_patches = []
game_patches = ["dsda-doom/0001", "dsda-doom/0002", "dsda-doom/0003", "dsda-doom/0004"]
sessions = []
batches = ["2026-10-16-dsda-doom-smoke", "2026-10-16-dsda-doom-software", "2026-10-16-dsda-doom-colours", "2026-10-17-dsda-doom-gl-music"]
+++

# dsda-doom: wrong colours, silent sound lumps, black GL textures (2026-10-16/17)

**Symptom.** The first smoke at 1920x1080 in software ran at about 5 fps; at 960x540 the frame came out
in wrong colours, green-tinted through SDL's GL renderer (gamedb:batch/2026-10-16-dsda-doom-software).
The engine's own sound lumps didn't play right, screenshots had the wrong byte order, and the GL renderer
drew walls, sprites and menus missing or black.

**Cause.** Three big-endian assumptions and one missing GL feature:

- dsda-doom.wad's DMX sound lumps are little-endian and were read in host order;
- the software renderer's SDL buffer wasn't in the texture's own format, so the pixel words were read
  with their bytes the wrong way round; screenshots likewise;
- the GL renderer uploads every indexed texture as GL_RG, and nv30 has no ARB_texture_rg.

**Fix.** 0001 reads the DMX lumps little-endian; 0003 puts the software renderer's SDL buffer in the
texture's own format (+ps3.231, gamedb:batch/2026-10-16-dsda-doom-colours); 0004 writes screenshots as
RGB24; 0002 uploads the indexed textures as GL_LUMINANCE_ALPHA (+ps3.247: the GL renderer runs, 48.8 fps
on the GL timedemo, gamedb:batch/2026-10-17-dsda-doom-gl-music). The software renderer then gave 33 fps
with the demo unfinished on Mesa ps3.246 and 38 finished on ps3.262.
