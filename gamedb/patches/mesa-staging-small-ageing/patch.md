+++
component = "mesa"
patches = ["patches/0063-nv30-the-small-staging-pool-gives-back-what-it-held-idle-for-a-second.patch"]
aliases = ["Mesa 0063"]
category = "stability"
switch = "none"
default = "NV30_STAGING_POOL_MB unset: off; set: idle small buffers go back after a second"
gating = "none: the pool and the option are off by default"
forwarded = "no"
tests = ["mesa-transfer-cpu", "mesa-transfer-cpu:fix-off", "mesa-query-lost:wait-option"]
batches = []
+++

# The small staging pool gives back what it held idle; NV30_QUERY_WAIT is clamped

The pool of small staging buffers (`NV30_STAGING_POOL_MB`, off by default) gave a buffer back only when it was full; the
pool of big ones gives back what was idle for a second, at the next put and at every flush. The small one now does too
(its entries carry the time they went in).

`NV30_QUERY_WAIT` was clamped on its upper side only; it is now clamped to -1..1000000 (`nv30_query_wait_option`).

## Investigation

- **How it was found:** the round 2 security audit's merge review (no ageing in the small pool, an unclamped NV30_QUERY_WAIT).
- **Minimal repro:** `packages/patched/graphics/mesa/tests/transfer-cpu-test.c` (`tools/run-tests.sh --only mesa-transfer-cpu`).

## Validation

Host: `transfer-cpu-test.c` (a fake clock: a buffer idle for 1.2 s goes at the next put, one at 1.5 s at the flush's ageing; it
fails on the nv30 files before the patch) and `mesa-query-lost:wait-option` (state only). Off by default: no PS3 measurement.
