+++
component = "mesa"
patches = ["patches/0034-nouveau-recycle-the-wait-buffers-of-deleted-fences-N.patch"]
aliases = ["Mesa 0034"]
category = "performance"
switch = "NV30_FENCE_BO_POOL"
default = "0 (off)"
gating = "switch NV30_FENCE_BO_POOL"
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

Measured neutral on ClassiCube (48.4 fps both ways, gamedb:batch/2026-10-18-mesa-407-switches), so it
stays off ([HOTPATHS.md, target 4](../../../docs/HOTPATHS.md#target-4-a-buffer-per-fence-mesa-0034)). It
is the first of three buffer pools: [mesa-staging-pool](../mesa-staging-pool/patch.md) (0036) and
[mesa-staging-pool-big](../mesa-staging-pool-big/patch.md) (0040) follow the same pattern for staging
buffers.
