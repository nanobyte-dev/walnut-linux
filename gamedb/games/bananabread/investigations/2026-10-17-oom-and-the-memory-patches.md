+++
id = "oom-and-the-memory-patches"
games = ["bananabread"]
driver_patches = []
game_patches = ["cube2/0001", "cube2/0002", "cube2/0003", "cube2/0004", "cube2/0005"]
sessions = []
batches = ["2026-10-17-group4-smokes"]
+++

# BananaBread: OOM-killed, then the first memory patches (2026-10-16 to 10-17)

**Symptom.** The research of 2026-10-16 found that the Cube 2 engine (and Red Eclipse) turns texture
compression off on Mesa: a one-line patch would make it a third S3TC game. On 2026-10-17 the game was
OOM-killed while loading cave without the VRAM swap (smoke's default). With the swap it ran, holding
138 MB of VRAM and filling the swap. The first run with CPU skinning gave a 15 fps HUD median, including
a load of over 60 s. GPU skinning ran without a wedge, but at 11 fps and badly stuttering.

**Evidence.** Where the load's memory went: textures uploaded uncompressed (`if(!mesa) usetexcompress =
2` in `rendergl.cpp`: only DDS files went in compressed), sky boxes and cube maps decoded from six 2048
px PNGs, twice; every world shader variant built whether or not it could be drawn; the lightmaps kept
on the CPU after their upload. The GPU skinning's vertex shader indexes a bone array, which nv30 (no
indirect constant addressing) compiles into if/else trees.

**Fix.** cube2 +ps3.272 brought patches 0001-0005 ([the engine's notes](../../../engines/cube2/technical.md)):
compression at upload (`texcompress`, textures of 1024 and up), CPU skinning by default without the GPU
skinning shaders (`gpuskel 0`: 28 fps against 11), only the drawable world shader variants, the sky from
its 256 px .dds (`skydds 1`), the lightmaps' CPU copies freed (`lmfreedata 1`). With the VRAM swap the
game no longer runs out of memory; it still fills the swap (117 MB of VRAM, about 87 MB of it
textures), which Mesa 0038's force-DXT would cut to 73-78 MB at a softer picture.
