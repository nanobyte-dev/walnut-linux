+++
component = "mesa"
patches = ["patches/0046-nv30-a-query-whose-report-slot-is-taken-back-keeps-i.patch"]
aliases = ["Mesa 0046"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: an upstream nv30 bug (the same code is in stock Mesa), fixed for every nv30"
forwarded = "no"
tests = ["mesa-query-steal"]
batches = []
+++

# A query whose report slot is taken back keeps its result

nv30 keeps query results in a notifier with room for 124 reports. When they run out,
`nv30_query_object_new()` frees the oldest report object, after waiting for the GPU to write it. The
query that owned that object still pointed at it, so its next result check used freed memory. The patch
gives every report object an owner: taking one back first copies the result into its query, and a
`TIME_ELAPSED` query that hasn't ended keeps its start time. A query begun again, ended again or
destroyed before its result was read lets its objects go (freed if their report is in, else disowned).
A render condition on a query whose report is gone draws.

The design point is that the slot had to be waited for anyway, so reading the result at that moment
costs nothing extra and the query never needs the object again.

It builds on nothing of ours. [mesa-query-kick](../mesa-query-kick/patch.md) (0042) later made the
recycling submit the batch that writes the oldest report before spinning on it.

## Investigation

- **What crashed:** real Sauerbraten segfaulted after 3.5 minutes of an online match on 2026-10-24:
  `nouveau_heap_free()` called from `nv30_query_object_del()`, writing to a "heap node" that was the
  function descriptor of `nouveau_mm_free_work` (the freed query object had been reused for a fence
  work). Before the crash, occlusion-culled geometry popped in and out, because queries read each
  other's results.
- **Why:** the engine ends up to 2048 occlusion queries a frame and reads them a frame later, so the
  124 slots were taken back all the time from queries that still pointed at them. The result check read
  the memory malloc had just handed to a newer query, then freed it a second time.
- **How it was found:** the played session of 2026-10-24, its crash address resolved with the
  debug symbols of `mesa-libgallium` ps3.721
  ([gamedb:sauerbraten/online-crashes](../../games/sauerbraten/investigations/2026-10-24-online-crashes.md)).
- **Minimal repro:** `packages/patched/graphics/mesa/tests/query-steal-test.c`
  (`tools/run-tests.sh --only mesa-query-steal`). The old code fails it under AddressSanitizer at the
  first result read.

## Validation

Not yet validated on the PS3.
