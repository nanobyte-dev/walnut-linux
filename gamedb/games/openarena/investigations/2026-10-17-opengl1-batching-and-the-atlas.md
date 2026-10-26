+++
id = "opengl1-batching-and-the-atlas"
games = ["openarena"]
driver_patches = []
game_patches = ["ioquake3/0019", "ioquake3/0020", "ioquake3/0021", "ioquake3/0022", "ioquake3/0023", "ioquake3/0024"]
sessions = []
batches = ["2026-10-17-ioquake3-362-batching", "2026-10-17-ioquake3-378-atlas"]
+++

# OpenArena opengl1: batching, and why the lightmap atlas went off (2026-10-17/18)

**Symptom.** Game-side batching had tripled X-Moto, so the same was tried for OpenArena's fixed-function
renderer: ioquake3 0019-0022 (the lightmap atlas `r_mergeLightmaps`, a GL state cache, no
`glLockArraysEXT` around batches, the sky from vertex arrays). On llvmpipe at GL 2.1 the bench demo went
from 3022 GL calls and 198 draws a frame to 377 and 79, with the pixels within 1-4 levels. On the RSX it
measured slower: steady 51.8 fps with all four on against 56.4 with all off
(gamedb:batch/2026-10-17-ioquake3-362-batching).

**Evidence.** The frame cost itself didn't change: smooth fps was 63.7-65.7 either way. What changed was
the hitches. With 0023's atlas variants (gamedb:batch/2026-10-17-ioquake3-378-atlas) the stall seconds were
34.1 with the base atlas, 23.9 with the atlas off, 35.6 with `mergelm2` and 36.2 with `lmsize256`: any
atlas page cost about 10 s of extra hitches a run, about 5% of the steady fps.

**Cause.** The atlas is built at the load and its pages are big uploads that the RSX's memory pressure
makes expensive to move; OpenArena on the PPE is not limited by its GL calls here, so the saved calls
bought nothing (the same lesson as ClassiCube: batching pays only where the PPE is the limit).

**Fix.** 0024 makes `r_mergeLightmaps 0` the default. The rest of the batching (state cache, no lock
arrays, sky arrays) is neutral on the RSX and stays on, since it costs nothing and cuts the CPU.
