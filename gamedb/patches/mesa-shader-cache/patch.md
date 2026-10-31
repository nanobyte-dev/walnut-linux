+++
component = "mesa"
patches = ["patches/0039-nv30-compiled-programs-in-the-disk-shader-cache-NV30.patch"]
aliases = ["Mesa 0039"]
category = "performance"
switch = "NV30_SHADER_CACHE"
default = "off"
gating = "switch NV30_SHADER_CACHE"
forwarded = "no"
tests = []
batches = []
+++

# nv30's compiled programs in the disk shader cache

Mesa's GL layer caches a program's NIR, so a warm start skips the GLSL compile and link, but nv30 still
ran `nir_to_tgsi` at every shader creation and the nvfx translation to RSX code at the first validate
of each program, warm or not. With `NV30_SHADER_CACHE=on` both results go into the screen's disk
cache: the TGSI keyed by the serialized NIR and stage, fragment programs keyed by their TGSI, class and
export mode, vertex programs keyed by their TGSI, class, texcoord mapping and clip planes. The cache is
keyed by the driver binary's build id, so any Mesa rebuild misses every old entry. Only a program in
its initial state uses it, and an entry that doesn't parse is ignored. `NV30_SHADER_CACHE_STATS=1`
prints hits, misses and their milliseconds.

Built and compile-tested; never run on the PS3. Its upper bound is small: batch 17 measured at most
0.11 to 0.39 s of `shader.backend` per game, below the run noise, so the A/B was skipped
([PROFILING.md](../../../docs/PROFILING.md#nv30s-shader-cache)). Later patches add their switches to
its keys: [mesa-txb-as-tex](../mesa-txb-as-tex/patch.md) (0045) and
[mesa-vtx-snorm8](../mesa-vtx-snorm8/patch.md) (0043).
