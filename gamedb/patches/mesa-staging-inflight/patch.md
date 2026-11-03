+++
component = "mesa"
patches = ["patches/0057-nv30-staging-inflight-counts-what-is-still-in-flight.patch"]
aliases = ["Mesa 0057"]
category = "performance"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-transfer-cpu"]
batches = []
+++

# Staging buffers in flight are counted down when their copies finish

On the PS3 the GART is a small reservation shared by every process, so `nv30_staging_bo_new()` waits
for the GPU once 4 MB of the context's texture uploads are in flight. The count it used
(`staging_inflight`) was only ever added to, and zeroed by that very wait. It counted every upload since
the last full idle, whether its copy had run long ago or not, so a load with many uploads stopped for a
full GPU idle every 4 MB, with the GPU's queue empty of staging in between: a periodic hitch.

Each upload now adds its buffer's size at the unmap and takes it off again in its fence work, when the
fence after its copy signals (the pooled, big-pool and plain buffers alike); a wait no longer resets
the count. Uploads whose copies have run don't count, and 4 MB really in flight still wait. The fence
work can run after its context is gone, so the count is a small reference-counted object that the
context and every pending upload hold; if it can't be allocated the uploads are not counted. On
other GPUs nothing waits (the wait is gated on `is_ps3`, as before).

## Investigation

- **How it was found:** the security and correctness inventory of round 2 (private, MS3), reading
  0007's staging code for the wait's trigger.
- **Expected effect:** fewer full GPU idles during texture-heavy loads (Luanti's textures, Trigger
  Rally's load, OpenArena's level reload), judged by stalls and the longest frame, not by average fps.
- **Minimal repro:** `packages/patched/graphics/mesa/tests/transfer-cpu-test.c` (`test_inflight`;
  `tools/run-tests.sh --only mesa-transfer-cpu`). nv30_miptree.c's own `nv30_staging_bo_new()` and
  `nv30_staging_finish()` against a fence queue the test signals: 200 uploads of 64 KiB with a fence
  signalled every 8 make no wait for the GPU (before: one every 64 uploads, 136 with the decrement
  removed), 4 MiB really in flight wait once, a context destroyed with uploads pending leaves the count
  to them (AddressSanitizer), and no count works.

## Validation

Not yet validated on the PS3. The plan: a same-boot Mesa A/B (this Mesa against the one before) of Luanti,
Trigger Rally's load and OpenArena's reload, 5 runs per arm, in the capture session (tools/rsx-ab's Mesa
arm), judged by stalls and the longest frame first.
