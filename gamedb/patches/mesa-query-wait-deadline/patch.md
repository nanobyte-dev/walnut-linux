+++
component = "mesa"
patches = ["patches/0056-nv30-on-the-PS3-a-query-report-that-never-comes-ends.patch"]
aliases = ["Mesa 0056"]
category = "stability"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "not-needed (ps3rsx's cancellation and the PS3's deadline only)"
tests = ["mesa-query-lost:end", "mesa-query-lost:steal", "mesa-query-lost:destroy", "mesa-query-lost:elapsed", "mesa-query-lost:deadline"]
batches = []
+++

# A query report that never comes ends its wait

When ps3rsx cancels a submission ([mesa-cancel-recover](../mesa-cancel-recover/patch.md), 0053), the
`QUERY_GET`s that were in it never reach the GPU and their notifier slots stay pending for good. 0053
noticed that in `nv30_query_read()` and gave the query a default result, but then freed the report
objects through `nv30_query_object_del()`, which spun on the pending slot until it landed. It never
lands, so the program hung on one PPE thread at 100% instead of recovering.

Each report object now remembers the push buffer's cancel count from when its `QUERY_GET` was queued,
and whether the kick that follows every query end got it through. A wait for its report stops at once
when a submission was cancelled since and the report wasn't sent. On the PS3 it also stops after a
second without the report, whatever the reason (a submission that failed another way, a GPU that
stopped), with one warning on stderr: `nv30: a query report didn't come in 1000 ms: taken as lost`. The
object is then lost for good and its slot reused. Three smaller pieces go with it: a lost object is
freed as soon as its query lets go of it, a `TIME_ELAPSED` query whose begin was lost reads 0 (it used
to read the end's raw timestamp), and a render condition on a lost report draws.

On other GPUs nothing changes. Only ps3rsx cancels (`nouveau_device::cancelled` stays 0 elsewhere), and
the one-second deadline is checked only when `is_ps3`. A lost report's slot can still get a late write
if a later, unrelated cancellation made a report that was on its way look lost (a `TIME_ELAPSED` begin,
which no kick follows at once). The worst case is a wrong result for a later query of that slot,
inside the notifier buffer.

## Investigation

- **What crashed:** nothing has hung in a played session yet. The hang is a hole in 0053's recovery:
  any submission ps3rsx rejects (an application, or a Mesa bug, sending a push the validator refuses)
  while an occlusion, timer or timestamp query is outstanding leaves the program spinning in
  `nv30_query_object_wait()` for good. The RSX is not touched and no other process is affected.
- **Why:** `nv30_query_object_wait()` spun on `ntfy[3] & 0xff000000` with no exit, and every path that
  frees a report object goes through it: `nv30_query_read()` after a lost result,
  `nv30_query_object_take()` when the 124 slots run out and the oldest is a lost one (a query left
  unread, or destroyed or begun again with its report pending), and the `TIME_ELAPSED` case where the
  begin's report was in the cancelled submission and the end's landed. The old tip's version had the
  same spin; its `nouveau_fence_flush()` only submitted a pending batch, which doesn't help a cancelled
  one.
- **How it was found:** the security review of the Mesa series merges on 2026-10-31 (private; its
  medium finding), reading `nv30_query.c` after 0042 was dropped.
- **Minimal repro:** `packages/patched/graphics/mesa/tests/query-lost-test.c`
  (`tools/run-tests.sh --only mesa-query-lost`, or `host-tests.sh query-lost`). nv30_query.c against a
  fake GPU and a fake winsys that cancels a chosen submission. Parts `end`, `steal`, `destroy`,
  `elapsed` (a cancelled submission) and `deadline` (a report sent and never written, nothing cancelled)
  must return; without this patch each hangs and fails by `alarm()` after 5 s (`FAIL end: hangs`).
  Parts `late` (a report 100 ms late on the PS3) and `x86` (1.2 s late, not the PS3) check that a late
  report keeps its value, and pass on any tree. On the fix ladder this is the top rung.

## Validation

Not yet validated on the PS3.
