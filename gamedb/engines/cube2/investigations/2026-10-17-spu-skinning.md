+++
id = "spu-skinning"
games = ["bananabread"]
driver_patches = []
game_patches = ["cube2/0002", "cube2/0006"]
sessions = []
batches = ["2026-10-17-luanti-neverball-batching"]
+++

# Skeletal models skinned on the SPEs (2026-10-17)

Moved on 2026-10-26 from docs/SPU.md ("BananaBread's skinning"), where libspeq's skinning op stays
described. Measured in BananaBread; the patches are the Cube 2 engine's.

## Symptom

The engine skins its skeletal models (players, the hud gun, monsters) either on the GPU (`gpuskel 1`)
or on the CPU. GPU skinning runs on nv30 without a wedge but at 11 fps against 28 with CPU skinning:
nv30 turns the vertex program's bone lookups into if/else trees. So cube2 0002 makes CPU skinning the
default and drops the GPU skinning shaders, and the skinning becomes PPE time.

## Evidence and fix

libspeq has a skinning op (`speq_skin_*_v1`, `SPEQ_SKIN_ABI` 1, dual-quaternion palettes, one SPU pass
per mesh, run on the pool; design and tests in
[packages/walnut/tools/ps3-spu/doc/skin.md](../../../../packages/walnut/tools/ps3-spu/doc/skin.md)), and
cube2 patch `0006-CPU-skinning-through-libspeq-on-the-PS3-s-SPEs.patch` hooks it in. The modes are
`SPEQ_SKIN=auto|spu|ppe|off`; the engine's own loop remains the default in the package.

Measured (BananaBread spawn and gun, one run each, same boot, gamedb:batch/2026-10-17-luanti-neverball-batching):

| Mode | steady fps | stalls |
|---|---|---|
| `ppe` (libspeq's vector code on the calling thread) | 28.1 | none |
| `spu` | 23.6 | 21 (4.3 s) |
| engine's own loop (`off`) | 23.0 | none |

So libspeq's PPE path is the fastest, and the SPU path adds hitches. The expected gain was small anyway
(about 1% on map cave, where only the hud gun is skinned, around 3200 vertices). Skinning stays
selectable and off the default; it is one of the switch-gated changes in docs/TESTS.md's keep-or-drop
re-test.
