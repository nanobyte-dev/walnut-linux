+++
component = "mesa"
patches = ["patches/0002-nouveau-mm-slab-oom.patch"]
aliases = ["Mesa 0002"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: a generic nouveau_mm bug, fixed for every nouveau driver"
forwarded = "no"
tests = ["mesa-alloc-fail"]
batches = []
+++

# nouveau_mm: return NULL when no slab can be allocated

`nouveau_mm_allocate()` takes small buffers from slabs. When a bucket had no free chunk it called
`mm_slab_new()` and went on as if that had worked; when the slab allocation failed (out of GART), the
bucket stayed empty and the allocator dereferenced it. Now a failed slab allocation unlocks the bucket,
frees the allocation record and returns NULL, and the caller sees no buffer object.

It is the first of the out-of-memory fixes. [mesa-alloc-fail](../mesa-alloc-fail/patch.md) (0048)
later made such a failure fall back to a buffer of the request's own size and made the callers fail
cleanly, and [mesa-out-of-gart](../mesa-out-of-gart/patch.md) (0049) covered fences and texture
uploads.

## Investigation

- **What crashed:** the process using Mesa crashed inside `nouveau_mm_allocate()` when a slab could not
  be allocated. Which program showed it first is not recorded.
- **Why:** an empty bucket was dereferenced after `mm_slab_new()` failed. The GART was then a small
  fixed boot reservation shared by every process, so allocations failing there was ordinary.
- **How it was found:** in the first days of GL on the RSX; the fix is in commit 291f8fc
  (2026-10-15, "nouveau_mm no longer crashes when a slab can't be allocated").
- **Minimal repro:** `packages/patched/graphics/mesa/tests/alloc-fail-test.c` part `mm-nomem`
  (`tools/run-tests.sh --only mesa-alloc-fail`): a fake `nouveau_bo_new` with no memory at all, and
  `nouveau_mm_allocate()` must give neither an allocation nor a buffer. The suite also runs it against
  `nouveau_mm.c` as it was before this patch, where it must fail: the allocator hands out a chunk of
  the empty bucket's list head and a garbage buffer (`FAIL ... no memory: alloc 0x...`).

## Validation

Not yet validated on the PS3.
