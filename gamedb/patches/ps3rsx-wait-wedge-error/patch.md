+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.1210"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# A wait ended by a wedge returns the error, not success

When the RSX wedges, ps3rsx ends every pending fence at once with -EIO (`fences_update()` in `src/exec.c`).
`dma_fence_wait_timeout()` then returns with time left, so GEM_PUSHBUF's SYNC returned 0 and GEM_CPU_PREP
returned 0 for work the GPU never ran. Now both read the fence's status after the wait
(`ps3rsx_wait_result()` in `src/wait.h`): SYNC returns the fence's error, and CPU_PREP the error of the BO's
write fences. An interrupted SYNC still returns -EBUSY, so the pushes are never submitted twice.

After a wedge, every BO with a pending write fence gets -EIO from CPU_PREP until the reboot: Mesa's map of it
fails instead of returning stale data, and its fence wait returns false, as it did on a timeout before.

## Investigation

- **What went wrong:** in PS3 session M (2026-10-31) the `rsxdrm-smoke quirks` push that wedged the RSX
  reported "ok", and the next one "NOT rejected", which hid which push raised the exception
  ([ps3rsx-validator-rt-enable](../ps3rsx-validator-rt-enable/patch.md)).
- **Why:** the waits checked only whether the fence signalled, never how it ended.
- **How it was found:** reading `submit.c` after session M, while replaying the quirks pushes on the PC.
- **Minimal repro:** `validate-test.c`'s `wait_cases()` runs `ps3rsx_wait_result()` on a fence that ended in
  an error, timed out, or was interrupted (`tools/run-tests.sh --only ps3rsx-shadow`).

## Validation

Not yet validated on the PS3: it needs a wedge on a ps3rsx with the change. `rsxdrm-smoke` now prints each
push's errno, so the next wedging run shows it.
