+++
component = "mesa"
patches = ["patches/0015-nouveau-fence-ref-unchanged.patch"]
aliases = ["Mesa 0015", "Mesa 0016", "Mesa 0017"]
category = "performance"
switch = "none"
default = "always on"
gating = "none: generic nouveau and its winsys; the same reference counts, relocations and buffer list on every driver"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-246"]
+++

# nouveau: fewer fence and kref operations per draw

Three changes from the profiles of Mesa ps3.223 (gamedb:batch/2026-10-16-mesa-223), written in one
round and folded into one patch on 2026-10-31 (they were 0015, 0016 and 0017). The reference counts,
the relocations, the kernel's buffer list and the command stream stay the same. Together they gave
X-Moto 37 against 35 fps and ClassiCube 44.9 against 43.2 (gamedb:batch/2026-10-17-mesa-246, single
runs).

- **`nouveau_fence_ref`.** `nv30_state_validate` re-references the context's current fence on every
  buffer bound to a draw, once per draw, and `nouveau_transfer_write` and `nouveau_copy_buffer` do the
  same per buffer write. Between two kicks the fence is the same, so nearly all of these calls find
  `*ref` already pointing at it, yet `nouveau_fence_ref` still took the screen's fence lock and did an
  increment and a decrement that cancel out: four atomic read-modify-writes, two of them with a full
  barrier. On the PS3's in-order PPE, where `lwarx/stwcx.` and `sync` are slow, `nouveau_fence_ref` and
  `_nouveau_fence_ref` took 2 to 11% of the CPU in games, most in immediate-mode draws (X-Moto,
  ClassiCube). Both now return at once when `*ref` already holds the fence.
- **nv30's validation.** After validation every draw walks the buffers bound to it and references the
  current fence on each one that lives in a `nouveau_mm` slab, through the out-of-line call. The fence
  only changes at a kick, so nv30 now compares the pointers first and calls only when they differ; the
  fence is read once, after the last point in the function that can kick. An immediate-mode draw has
  two to four such buffers.
- **The winsys's `pushbuf_validate`.** For each buffer a bufctx gained it called `pushbuf_kref`, then
  `pushbuf_krel` twice, each looking up both krefs again. nv30 adds one bufref per vertex attribute (all
  in the same buffer) and two per texture, so a typical draw repeated the same `pushbuf_kref` two to four
  times. A call for the same buffer with the same domain and access flags as the call just before
  changes nothing, so it is skipped, and the push buffer's kref is looked up once. On the PS3 most of
  this work went away a day later with [mesa-no-relocations](../mesa-no-relocations/patch.md) (0019),
  which sends no relocations at all; the part still applies to the buffer list and to other kernels.
