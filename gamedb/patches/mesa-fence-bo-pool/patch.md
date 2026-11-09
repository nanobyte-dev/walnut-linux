+++
component = "mesa"
patches = ["patches/0034-nouveau-recycle-the-wait-buffers-of-deleted-fences-N.patch",
           "patches/0077-nv30-on-the-PS3-the-fence-wait-buffer-pool-is-on-by-default.patch"]
aliases = ["Mesa 0034", "Mesa 0077"]
category = "performance"
switch = "NV30_FENCE_BO_POOL"
default = "16 on the PS3 (0077), 0 (off) elsewhere"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = ["2026-10-18-mesa-407-switches"]
+++

# Recycle the wait buffers of deleted fences

Every nouveau fence gets a 4 KiB GART buffer, which the fence's emit references in its batch so that a
wait can block on it in the kernel (`nouveau_bo_wait`). nv30 makes a fence per submission, so each one
cost a buffer allocation and a free: two ioctls with a GART placement in ps3rsx. Across nine games the
profile had 0.7% of the CPU in `nouveau_bo_new` under `nouveau_fence_new` and about as much again in the
frees.

With `NV30_FENCE_BO_POOL=N` (up to 32) the buffers of up to N deleted fences are kept and handed to the
next fences. The design point is when a fence may give its buffer back: only once it is signalled (the
fence list holds a reference from its emit until then) or if it was never emitted, so no batch the GPU
still runs references a recycled buffer. Only the kernel's fences on the buffer matter, never its
contents. The pool is drained before the screen's device goes away.

On the PS3 it is on by default with 16 buffers (0077); elsewhere it stays off. It first measured neutral on
ClassiCube (48.4 fps both ways, gamedb:batch/2026-10-18-mesa-407-switches,
[HOTPATHS.md, target 4](../../../docs/HOTPATHS.md#target-4-a-buffer-per-fence-mesa-0034)). Later profiles
(2026-11-09) showed each make and close of a wait buffer costing kernel time: with 16, BananaBread's kernel time
fell by about 1 ms a frame and Trigger Rally's by about 0.35 ms. X can't be given the variable, so the default is
what it runs with. The pool pins 16 x 4 KiB of GART. It came before the staging pool,
[mesa-staging-pool-big](../mesa-staging-pool-big/patch.md) (0040), which follows the same pattern for staging
buffers.
