+++
component = "mesa"
patches = ["patches/0062-nv30-queries-a-late-report-is-read-again-an-unkicked-begin-is-not-waited-for.patch"]
aliases = ["Mesa 0062"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: same code everywhere; every case needs a cancelled or late submission"
forwarded = "no"
tests = ["mesa-query-lost:no-slot", "mesa-query-lost:disowned", "mesa-query-lost:deadline-edge", "mesa-query-lost:begin-sent", "mesa-query-lost:two-ctx", "mesa-query-lost:own-begin", "mesa-query-lost:stale", "mesa-query-lost:fix-off"]
batches = []
+++

# nv30 queries: how the report slots are used

Four cases in `nv30_query.c`:

- A wait that gives up (the PS3's second-long deadline, or a cancelled submission) read the slot, decided, and gave up;
  a report that landed in between was taken as lost and its slot reused. The slot is read once more before the object is
  given up.
- A `TIME_ELAPSED` begin was never marked sent, so a later cancellation of some other submission made it count as lost while
  its report was on its way (the query read 0, and the slot could be handed out before the late write). The kick of the
  query's end also sent the begin queued before it, unless a cancellation came between, and marks it.
- A context out of slots took the oldest back and waited for its report: for another context's `TIME_ELAPSED` begin that was
  not kicked yet, never (a second on the PS3). Another context's unkicked begin is passed over, the context's own is kicked
  and then waited for, and a begin that finds no slot reads as lost.
- `begin` and `end` queued their words on a push buffer a cancelled submission had made stale (the next kick drops its words)
  before the context recovered at its next draw: the counter reset or the report went nowhere. They call
  `nv30_state_check_lost` first, as a draw does.

- A query remembers the count at its begin: if a submission was cancelled or failed before its end, it reads as lost (the
  counter was not reset), as does an end that finds no slot; a disowned unkicked begin is passed over as well.

## Investigation

- **How it was found:** the round 3 reviews of 0056 (the late-report and unkicked-begin cases) and of this patch (the begin lost to a cancelled submission).
- **Minimal repro:** the parts of `packages/patched/graphics/mesa/tests/query-lost-test.c` named above (`tools/run-tests.sh --only mesa-query-lost`).

## Validation

Host (`query-lost-test.c`, five new parts, each failing on the file before the patch): a report landing in the clock call that
passes the deadline (fake clock); a destroyed `TIME_ELAPSED` query whose slot must stay out of reach until its late reports
are in; two contexts with the slots run out; one context's 130 unkicked begins; a begin and an end on a stale push buffer.
The PS3 stream does not change in a run where no submission was cancelled or failed (no new word is written; the recovery
runs only when `cancel_seen` differs). Later on the PS3: one A/B on a game that uses occlusion or time queries, if one is
found, otherwise the corpus replay must show no change.
