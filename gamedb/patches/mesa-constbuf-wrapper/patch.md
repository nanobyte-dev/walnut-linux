+++
component = "mesa"
patches = ["patches/0023-nv30-no-constant-buffer-allocation-per-constant-upda.patch"]
aliases = ["Mesa 0023"]
category = "performance"
switch = "NV30_CONSTBUF_ALLOC"
default = "off (1 allocates a new wrapper per update)"
gating = "none: every nv30; same command stream"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262"]
+++

# nv30: no constant buffer allocation per constant update

The state tracker hands nv30 its shader constants as user memory, again whenever they change: in
fixed-function GL about every draw, each object with its own modelview matrix. `nv30_set_constant_buffer`
wrapped each pointer in a new `nv04_resource` (a zeroed allocation, a range, a reference count) and freed
the previous one, so every constant update cost an allocation and a free on the PPE. Only the wrapper's
data pointer and size are ever read, and nothing but the context's slot references it, so while the slot
still holds the wrapper the last call made, and nobody else holds it, the wrapper is pointed at the new
memory instead. A real constant buffer takes the old path. Constants are uploaded exactly as before.

Part of Mesa ps3.262 (gamedb:batch/2026-10-17-mesa-262).
