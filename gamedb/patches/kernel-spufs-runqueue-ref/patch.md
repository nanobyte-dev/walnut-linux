+++
component = "kernel"
patches = ["patches/local/0023-spufs-grab-runnable-context-reference.patch"]
aliases = ["local/0023", "kernel 0023"]
category = "stability"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18, spufs is orphaned upstream)"
tests = []
batches = []
+++

# spufs: take a reference on contexts pulled off the runqueue

`grab_runnable_context()` unlinks a context from the SPU scheduler's runqueue and returns it after
dropping the runqueue lock, but the runqueue holds no reference of its own, so the caller has a bare
pointer. The fix takes a reference while the lock is still held, where a queued context is known to be
alive, and both callers (`__spu_deactivate()` and `spusched_tick()`) drop it when they are done, as
`find_victim()` already does. The final `put_spu_context()` can now run in the scheduler;
`destroy_spu_context()` deactivates the context itself, so that is safe.

Posted by Junrui Luo on 2026-08-12 (reported by Yuhao Jiang) and not merged, since spufs has no
maintainer. It matters to us because libspeq runs 8 contexts on 6 SPEs, so contexts are time-sliced all
the time ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).

## Investigation

- **What crashed:** nothing seen on our PS3. The bug is a use-after-free: a context closed while the
  scheduler holds its pointer is freed under it.
- **Why:** once `grab_runnable_context()` unlinked the context, the owner's `spu_del_from_rq()` finds
  it already off the queue, so the owner can leave `spu_run()` and close the context directory while
  the scheduler still uses it; the window spans a whole SPU context save. The `BUG_ON` in
  `destroy_spu_context()` can't see it because the list entry is already empty.
- **How it was found:** code reading, by the patch's author; we checked its premises against our 6.12
  code before taking it in the survey of 2026-10-18.
- **Minimal repro:** None yet: a race found by reading code. A stress test could raise the odds (more
  SPU contexts than SPEs, each closed right after its `spu_run` returns, under KASAN), but none is
  planned.

## Validation

Not yet validated on the PS3.
