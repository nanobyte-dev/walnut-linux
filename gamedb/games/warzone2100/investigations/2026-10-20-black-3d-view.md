+++
id = "black-3d-view"
games = ["warzone2100"]
driver_patches = []
game_patches = ["warzone2100/0004", "warzone2100/0007"]
sessions = []
batches = []
+++

# Warzone 2100: the black 3D view, etcpak and a GCC miscompile (2026-10-19/20)

**Symptom.** The first PS3 smoke run (batch ssdroot6, warzone2100 ps3.566, 2026-10-19) had the HUD and the
minimap but a black 3D view, and garbled text.

**Evidence.** The amd64 build on llvmpipe, cut down to the PS3's GL 2.1 extension list
(`MESA_GL_VERSION_OVERRIDE=2.1`, `MESA_GLSL_VERSION_OVERRIDE=120`, `MESA_EXTENSION_OVERRIDE` minus
everything nv47 lacks, ARB_texture_rg, ARB_texture_float and EXT_texture_array among them), rendered the
skirmish normally: not a GL 2.1 limit. The ppc64 build under qemu-user with llvmpipe and with softpipe was
black, as on the PS3. Shader overrides as an autoload mod showed the lightmap (fog of war) right and the
textures black; logging each texture upload against the amd64 run showed level 0 identical and level 1
down from 103538663 to 465002 (the byte sum of a 512x512 level). The game's compiled
`stb_image_resize.cpp.o` reproduced it on its own; it takes `-mcpu=cell` (or 970, power6: AltiVec), `-O3`
and `-DNDEBUG` together.

**Cause.** Two, neither of them the GL 2.1 driver:

- the game's own DXT encoder (etcpak) wrote broken blocks on big-endian;
- GCC 16.2 miscompiles stb_image_resize at `-O3 -mcpu=cell`: the AltiVec loop vectorizer gets the
  4-channel horizontal downsampling loop wrong, so every mipmap level and every image that `texturesize`
  scales down came out nearly black and transparent. Terrain sampled black, the models' alpha test threw
  them away, the HUD (no mipmaps) was fine.

**Fix.** Patch 0004 (ps3.586) fixes etcpak's scalar paths; patch 0007 (ps3.617, and every later build) turns
loop vectorization off for that one file when the target has AltiVec but no VSX (`#pragma GCC novector`
on the one loop fixes it too). What this means for the other packages is in
[PACKAGING.md](../../../../docs/PACKAGING.md). The 3D view was then dark rather than black: the next
problem, the RSX's TXB ([2026-10-20 the dark 3D view](2026-10-20-dark-3d-view.md)).
