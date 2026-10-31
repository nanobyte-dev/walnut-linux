+++
component = "mesa"
patches = ["patches/0035-nv30-sleep-on-the-query-s-fence-instead-of-spinning-.patch"]
aliases = ["Mesa 0035"]
category = "performance"
switch = "NV30_QUERY_WAIT"
default = "unset (spin, as before)"
gating = "switch NV30_QUERY_WAIT"
forwarded = "no"
tests = []
batches = ["2026-10-18-mesa-416-hot-spots", "2026-10-18-query-wait-repeat"]
+++

# Sleep on the query's fence instead of spinning for its result

A query result the caller waits for (`glGetQueryObject` with `GL_QUERY_RESULT`) was a busy loop on the
report word. BananaBread's water reflections test an occlusion query issued earlier in the same frame,
and that loop was 10% of its CPU on the PS3. A spinning PPE thread also takes issue slots from the
core's other hardware thread for the whole wait.

With `NV30_QUERY_WAIT=US` the end of a query holds the context's current fence, which the query's kick
emits after the report. A wait spins for at most US microseconds at low SMT priority (`or 1,1,1`, on
PowerPC only), then sleeps in the kernel on that fence, then spins on what is left while yielding the
CPU, because the kernel's fence follows the FIFO, which can be slightly ahead of the 3D back end
writing the report. The result is read from the report as before, so only the way the thread waits
changes.

One run gave BananaBread 45.6 against 25.0 fps (gamedb:batch/2026-10-18-mesa-416-hot-spots); the
interleaved repeat did not hold that up (gamedb:batch/2026-10-18-query-wait-repeat), so it stays off.
The game-side fix is cube2's `oqwaterlag`
([gamedb:cube2/water-query-wait](../../engines/cube2/investigations/2026-10-18-water-query-wait.md)).
[mesa-query-kick](../mesa-query-kick/patch.md) (0042) works on the same waits from the submission side.
