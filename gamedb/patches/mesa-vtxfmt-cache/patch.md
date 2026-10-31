+++
component = "mesa"
patches = ["patches/0022-nv30-no-vertex-format-rewrite-when-only-the-vertex-b.patch"]
aliases = ["Mesa 0022"]
category = "performance"
switch = "NV30_VTXFMT_ALWAYS"
default = "off (1 writes the VTXFMT burst every time)"
gating = "none: every nv30"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262"]
+++

# nv30: no vertex format rewrite when only the vertex buffers change

`nv30_vbo_validate` runs whenever the vertex buffers or elements change, and wrote the whole VTXFMT burst
each time: one word per attribute plus the header. The formats depend only on the vertex elements and on
whether vertices are pushed inline, not on the buffers, and Mesa's state tracker rebinds buffers far more
often than it changes the layout (u_vbuf binds a new upload buffer for user arrays in every draw). The
context now keeps the VTXFMT words the GPU holds (`state.vtxfmt`, carried over a context switch with the
rest of the state) and leaves the burst out when it would write the same words. ps3rsx shadows VTXFMT and
restores it after another channel ran, so the GPU state is the same.

A later fix in the same patch forgets the cached formats when the context that drew last is destroyed
(commit 5fe2290); [mesa-ctx-state-reset](../mesa-ctx-state-reset/patch.md) (0029) generalised that to all
of the cached state. Part of Mesa ps3.262 (gamedb:batch/2026-10-17-mesa-262).
