+++
id = "opengl2-fits-in-memory"
games = ["openarena"]
driver_patches = []
game_patches = ["ioquake3/0001", "ioquake3/0002", "ioquake3/0003", "ioquake3/0004", "ioquake3/0005", "ioquake3/0006", "ioquake3/0007", "ioquake3/0008", "ioquake3/0009", "ioquake3/0010"]
sessions = []
batches = ["2026-10-17-ioquake3-285"]
+++

# OpenArena opengl2: making the GLSL renderer fit in memory (2026-10-15 to 10-17)

**Symptom.** On 2026-10-15 the GLSL renderer was tried once and dropped: it "ran out of RAM compiling
its shaders" (commit 82b6b5f), before the VRAM swap and game mode existed. On 2026-10-17 it ran on nv30
(e708512: all 97 programs link), but compiled them twice at about 25 s each, and Debian's build played
the bench demo at 10.7 fps (`gl2`, Debian's opengl2 defaults: 15.9).

**Evidence.** Where the memory went, from the hunk counters and Mesa's allocations: Mesa kept every
shader's source and IR after its program linked; 27 bone-animation programs indexed a 105-matrix array,
which nv30 (no indirect constant addressing) turns into compare trees; 15 shadow and post-processing
programs needed framebuffer objects gl2min never uses; a renderer restart at the map load compiled
everything again; the VAO cache held 21 MB that were never drawn from; and the renderer kept a CPU copy
of every MD3 vertex frame, about 87 MB of hunk for the demo's models.

**Fix.** ioquake3 0001-0010 (ps3.285), each behind a cvar ([the engine's notes](../../../engines/ioquake3/technical.md#renderergl2-memory-0001-0010)):
gl2min went from 14.8 to 26.1-31.8 fps, the longest stall from 24.7 to 6 s, the CPU of the run from
103 to 55 s (gamedb:batch/2026-10-17-ioquake3-285). 0007 is most of it: without it 17.6 fps.
`gl2min_cpuanim` (0010) gave 30.5 fps at 41 MB of VRAM. What was left were memory stalls, fixed later by
the vertex formats ([2026-10-18 opengl2 freezes](2026-10-18-opengl2-freezes.md)).
