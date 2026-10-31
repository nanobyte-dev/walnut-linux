+++
component = "mesa"
patches = ["patches/0020-nv30-invalidate-the-texture-cache-only-after-texture.patch"]
aliases = ["Mesa 0020"]
category = "performance"
switch = "NV30_TEX_INVALIDATE_ALWAYS"
default = "off (invalidate only after texture memory may have changed; 1: before every draw)"
gating = "none: every nv30; on other kernels it relies on the same events, and ps3rsx adds its own invalidate at channel switches"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262", "2026-10-17-mesa-262-repeats"]
+++

# nv30: invalidate the texture cache only after texture memory may have changed

`nv30_state_validate` ended every draw with the texture cache invalidate the NVIDIA blob was traced doing
(`TEX_CACHE_CTL` 2, then 1, then three `0x1718` NOPs): ten push words, ten more for the kernel to copy and
check, and a flush of the GPU's texture caches before each draw. libgcm leaves the invalidate to the
application, after it changed texels; binding other textures needs none.

The screen now counts the events after which texture memory may hold something the cache doesn't
(`tex_gen`, bumped atomically by any context): a copy into or out of a texture (uploads, readbacks, the 3D
blit), a new framebuffer, and a flush. A context emits the same sequence as before, at the same place,
before its first draw after the count moved. ps3rsx already invalidates both caches whenever another
channel ran, so other processes' rendering and uploads are covered. `NV30_TEX_INVALIDATE_ALWAYS=1`
restores the invalidate before every draw, to tell a stale texture from another bug.

Part of Mesa ps3.262 (gamedb:batch/2026-10-17-mesa-262). Armagetron's 13% loss on that build was first
suspected here; it ran 52-53 fps with or without the patch, so it wasn't this one
(gamedb:batch/2026-10-17-mesa-262-repeats).
