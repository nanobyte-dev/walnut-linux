+++
component = "mesa"
patches = ["patches/0037-nv30-optional-limits-on-nouveau-s-system-memory-copi.patch"]
aliases = ["Mesa 0037"]
category = "performance"
switch = "NV30_SYSMEM_COPY, NV30_INVALIDATE_BUFFER, NV30_SYSMEM_STATS"
default = "NV30_SYSMEM_COPY=1 (as before), buffer invalidation off, stats off"
gating = "switch NV30_SYSMEM_COPY"
forwarded = "no"
tests = []
batches = ["2026-10-18-batch17"]
+++

# Optional limits on nouveau's system memory copies of VRAM buffers

When the CPU maps an idle VRAM buffer, nouveau keeps a malloc'ed copy of the whole buffer for as long
as the buffer lives. u_vbuf maps such buffers for every draw whose vertex formats the RSX can't fetch,
and after four flushes in a row with such a readback nouveau sets a screen-wide hint that is never
cleared: from then on every new VRAM buffer gets a copy at creation, read or not. In OpenArena's
opengl2 renderer the orphaned tess buffer sets the hint in the first frames, and the 62 MB of static
MD3 vertex buffers loaded afterwards get 62 MB of copies nothing reads.

Three switches. `NV30_SYSMEM_COPY`: 1 as before; 0 never sets the hint (a buffer gets its copy at its
first CPU read); 2 lets the hint give copies at creation only to `GL_DYNAMIC_DRAW` buffers, the
streaming ones it is meant for. `NV30_INVALIDATE_BUFFER=1` turns on buffer invalidation as nv50 and
nvc0 have it, so orphaning with `glBufferData(NULL)` keeps the buffer and its copy. `NV30_SYSMEM_STATS=N`
counts the copies and readbacks. The counters live in the shared nouveau screen; for nv50 and nvc0 the
hint mode stays as before.

In batch 17 the switches removed the copies and the stalls of ioquake3's old colour path
(gamedb:batch/2026-10-18-batch17). The switches and their measurements are in
[MEMORY.md](../../../docs/MEMORY.md#nouveaus-system-memory-copies-of-vram-buffers-mesa-0037); the
investigation is [gamedb:openarena/opengl2-freezes](../../games/openarena/investigations/2026-10-18-opengl2-freezes.md).
Still off by default.
