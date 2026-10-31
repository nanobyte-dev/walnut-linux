+++
component = "mesa"
patches = ["patches/0021-nv30-no-second-vertex-cache-invalidate-in-draws-afte.patch"]
aliases = ["Mesa 0021"]
category = "performance"
switch = "NV30_VTX_INVALIDATE_DRAW"
default = "off (1 emits the draw-time invalidate as before)"
gating = "none: every nv30"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262"]
+++

# nv30: no second vertex cache invalidate in draws after buffer writes

`nv30_state_validate` invalidates the vertex cache (`0x1710`) in every draw, after the last write any
vertex or index buffer of the draw could have had. `nv30_draw_vbo` then emitted another one right before
the primitive whenever a buffer had been written since the last draw, or a bound buffer was mapped
coherently, and for the latter walked the vertex buffers every draw to find out. Nothing writes vertex
memory between the two, so the second invalidated an empty cache. It is dropped, with the walk. That
saves two push words, and the kernel's copy and check of them, in most draws of GL 1.x games (u_vbuf's
uploads and Mesa's immediate-mode buffers write a buffer before nearly every draw).

`NV30_VTX_INVALIDATE_DRAW=1` emits it as before. A wrong call here would show as stale or corrupt
vertices. Part of Mesa ps3.262 (gamedb:batch/2026-10-17-mesa-262).
