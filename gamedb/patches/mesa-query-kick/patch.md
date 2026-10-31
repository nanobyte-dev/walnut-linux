+++
component = "mesa"
patches = ["patches/0042-nv30-submit-a-query-s-batch-when-its-result-is-wante.patch"]
aliases = ["Mesa 0042"]
category = "performance"
switch = "NV30_QUERY_KICK"
default = "1 (as before)"
gating = "switch NV30_QUERY_KICK"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot7-query-kick"]
+++

# Submit a query's batch when its result is wanted, not at its end

`nv30_query_end()` submits the push buffer at the end of every query. Sauerbraten ends an occlusion
query for every visible piece of the world and every water surface, and those submissions were 5.6% of
its CPU in the profile of 2026-10-19, while most results are read a frame later, long after they were
written.

`NV30_QUERY_KICK=0` submits nothing at a query's end: the batch goes with the next submission there is
anyway, and a result check that finds the report not written yet submits it first, whether the caller
waits or only polls (nvc0 does the same). Mode 2 also submits at the end while the GPU has finished
everything before, the [mesa-kick-auto](../mesa-kick-auto/patch.md) test, so a query never leaves the
GPU idle. The key design point is the recycling of the 124 report slots: in modes 0 and 2 the batch
that writes the oldest report is submitted before spinning on it, so the spin can't wait for a report
that was never sent. Same commands in the same order, cut into fewer submissions.

Measured neutral on BananaBread: 39.8 and 39.6 fps against 39.4, the CPU unchanged at 231-232 s
(gamedb:batch/2026-10-19-ssdroot7-query-kick). HOTPATHS.md suggests dropping it unless another game
shows a gain. It works on the same waits as [mesa-query-wait](../mesa-query-wait/patch.md) (0035) and was
built with [mesa-fence-work-kick](../mesa-fence-work-kick/patch.md) (0041).
