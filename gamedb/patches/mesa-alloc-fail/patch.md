+++
component = "mesa"
patches = ["patches/0048-nouveau-a-buffer-allocation-that-fails-fails-the-cal.patch"]
aliases = ["Mesa 0048", "Mesa 0002"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: nouveau's shared buffer code, every nouveau driver"
forwarded = "no"
tests = ["mesa-alloc-fail:mm-fallback", "mesa-alloc-fail:mm-nomem"]
batches = []
+++

# A buffer allocation that fails fails the call, and needs less contiguous memory

On the PS3 the RSX's VRAM fills up in big games, their buffers then go to GART, and GART allocations
start to fail. nouveau ignored most of those failures and went on with a NULL buffer object. Four
paths change:

- `nouveau_mm_allocate()`: when a bucket had no free chunk it called `mm_slab_new()` and went on as if
  that had worked; when the slab allocation failed, the bucket stayed empty and the allocator
  dereferenced it. A failed slab allocation now unlocks the bucket, frees the allocation record and
  returns NULL. Before that, a buffer of the request's own size is tried: slabs for 1 and 2 MiB chunks
  are 4 MiB, so the stream uploader's 1 MiB buffer failed with 1-3 MiB of GART still free.
- `nouveau_buffer_transfer_map()` for VRAM buffers: a staging area that can't be had fails the map,
  instead of returning NULL with the transfer leaked, copying from a NULL object, or handing out memory
  whose writes never reach the GPU.
- Discards (the GART map path and `nouveau_buffer_invalidate()`): the new storage is allocated before
  the old one is let go; without it the buffer keeps its storage and the map syncs.
- `nouveau_buffer_migrate()`: if neither VRAM nor GART has room, the buffer keeps its storage and the
  call fails.

It came with [mesa-upload-null](../mesa-upload-null/patch.md) (0047).
[mesa-out-of-gart](../mesa-out-of-gart/patch.md) (0049) extends the same rule to fences and texture
uploads. The NULL return was the first of the out-of-memory fixes, Mesa 0002 (2026-10-15), until it was
folded into this patch on 2026-10-31.

## Investigation

- **What crashed:** first, in the first days of GL on the RSX, a process crashed inside
  `nouveau_mm_allocate()` when a slab could not be allocated (which program is not recorded; the NULL
  return is commit 291f8fc, 2026-10-15); the GART was then a small fixed boot reservation shared by
  every process. Then the same Sauerbraten session as 0047 (2026-10-25): `out of GART for a 4096 KiB
  BO`, then a NULL write. The slab fallback is the part that touched that crash: the 4 MiB slab was
  refused while a 1 MiB buffer would have fitted. The map, discard and migrate paths were found NULL-unsafe by
  reading the code after it.
- **Why:** nouveau treated a failed `nouveau_bo_new()` as impossible on most paths; on the PS3 the GART
  is a 64 MB CMA area shared by everything, so it happens in big games.
- **How it was found:** gamedb:sauerbraten/online-crashes and
  [LOG-FINDINGS.md item 2](../../../docs/games/LOG-FINDINGS.md#2-a-failed-buffer-allocation-ends-in-a-segfault).
- **Minimal repro:** `packages/patched/graphics/mesa/tests/alloc-fail-test.c` parts `mm-nomem` (a fake
  `nouveau_bo_new` with no memory at all: neither an allocation nor a buffer may come back; the old code
  handed out a chunk of the empty bucket's list head, `FAIL ... no memory: alloc 0x...`) and `mm-fallback`
  (`tools/run-tests.sh --only mesa-alloc-fail`): room for 3 MiB but not the 4 MiB slab, and a 1 MiB
  request must still get a buffer of its own. Against `nouveau_mm.c` as it was before this patch it
  must fail, and does (`FAIL ... a buffer of its own comes without a slab allocation`). The map,
  discard and migrate paths in `nouveau_buffer.c` have no host case.

## Validation

Not yet validated on the PS3.
