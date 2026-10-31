+++
component = "mesa"
patches = ["patches/0048-nouveau-a-buffer-allocation-that-fails-fails-the-cal.patch"]
aliases = ["Mesa 0048"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: nouveau's shared buffer code, every nouveau driver"
forwarded = "no"
tests = ["mesa-query-steal"]
batches = []
+++

# A buffer allocation that fails fails the call, and needs less contiguous memory

On the PS3 the RSX's VRAM fills up in big games, their buffers then go to GART, and GART allocations
start to fail. nouveau ignored most of those failures and went on with a NULL buffer object. Four
paths change:

- `nouveau_mm_allocate()`: when no new slab can be had, a buffer of the request's own size is tried.
  Slabs for 1 and 2 MiB chunks are 4 MiB, so the stream uploader's 1 MiB buffer failed with 1-3 MiB of
  GART still free.
- `nouveau_buffer_transfer_map()` for VRAM buffers: a staging area that can't be had fails the map,
  instead of returning NULL with the transfer leaked, copying from a NULL object, or handing out memory
  whose writes never reach the GPU.
- Discards (the GART map path and `nouveau_buffer_invalidate()`): the new storage is allocated before
  the old one is let go; without it the buffer keeps its storage and the map syncs.
- `nouveau_buffer_migrate()`: if neither VRAM nor GART has room, the buffer keeps its storage and the
  call fails.

It came with [mesa-upload-null](../mesa-upload-null/patch.md) (0047) and builds on
[mesa-mm-slab-oom](../mesa-mm-slab-oom/patch.md) (0002), which made a failed slab return NULL in the
first place. [mesa-out-of-gart](../mesa-out-of-gart/patch.md) (0049) extends the same rule to fences
and texture uploads.

## Investigation

- **What crashed:** the same Sauerbraten session as 0047 (2026-10-25): `out of GART for a 4096 KiB BO`,
  then a NULL write. The slab fallback is the part that touched that crash: the 4 MiB slab was refused
  while a 1 MiB buffer would have fitted. The map, discard and migrate paths were found NULL-unsafe by
  reading the code after it.
- **Why:** nouveau treated a failed `nouveau_bo_new()` as impossible on most paths; on the PS3 the GART
  is a 64 MB CMA area shared by everything, so it happens in big games.
- **How it was found:** gamedb:sauerbraten/online-crashes and
  [LOG-FINDINGS.md item 2](../../../docs/games/LOG-FINDINGS.md#2-a-failed-buffer-allocation-ends-in-a-segfault).
- **Minimal repro:** `packages/patched/graphics/mesa/tests/alloc-fail-test.c`, part of
  `tools/run-tests.sh --only mesa-query-steal`.

## Validation

Not yet validated on the PS3.
