+++
component = "kernel"
patches = ["patches/local/0024-spufs-acquire-saved-state-race.patch"]
aliases = ["local/0024", "kernel 0024"]
category = "stability"
switch = "none"
default = "always on"
authors = ["Junrui Luo"]
forwarded = "not-needed (posted to linuxppc-dev by its author; not merged as of 2026-10-18)"
tests = []
batches = []
+++

# spufs: the context state race in spu_acquire_saved()

`spu_acquire_saved()` must return with the context saved and its mutex held. It tested the state once,
and if the context was running deactivated it, but the deactivation drops the mutex around
`spu_schedule()`, so the test was stale by the time it returned. The fix tests the state again after
`spu_deactivate()`, which also sets `SPU_SCHED_WAS_ACTIVE` again so the caller keeps its own
reactivation token. Posted by Junrui Luo on 2026-08-12 (reported by Yuhao Jiang), not merged.

## Investigation

- **What crashed:** nothing seen on our PS3. The failure is the
  `BUG_ON(ctx->state != SPU_STATE_SAVED)` in `spu_release_saved()`, with the state mutex left held.
- **Why:** a second reader of a saved-state file of the same context can take the mutex in that window,
  see the context saved and skip its own deactivation. `SPU_SCHED_WAS_ACTIVE` is one bit, not a
  per-caller token, so that reader's release consumes it and binds the context back onto an SPU; the
  first reader then reads a save image the SPU is writing and trips the `BUG_ON`.
- **How it was found:** code reading, by the patch's author; premises checked against our code in the
  survey of 2026-10-18 ([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md#what-was-ported)).
- **Minimal repro:** None yet: proposed: two threads reading one running context's saved-state files
  (`regs`, `mem`) in a loop while it runs; without the fix it should end in the `BUG_ON`.

## Validation

Not yet validated on the PS3.
