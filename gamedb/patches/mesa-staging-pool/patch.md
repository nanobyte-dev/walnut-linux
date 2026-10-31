+++
component = "mesa"
patches = ["patches/0036-nv30-texture-uploads-reuse-idle-mapped-staging-buffe.patch"]
aliases = ["Mesa 0036"]
category = "performance"
switch = "NV30_STAGING_POOL"
default = "0 (off)"
gating = "switch NV30_STAGING_POOL"
forwarded = "no"
tests = []
batches = ["2026-10-18-mesa-416-hot-spots"]
+++

# Small texture uploads reuse idle, mapped staging buffers

Every texture upload had a staging buffer of its own: a GEM create, an mmap, then a munmap and a GEM
close once the fence after its copy signalled. ClassiCube updates its animated water and lava tiles
with `glTexSubImage2D` every frame, and on the PS3 that was 8.6% of its CPU (mmap 3.0, munmap 2.4,
GEM create 1.6, close 0.7); QuakeSpasm and Yamagi Quake II spent about 2%.

With `NV30_STAGING_POOL=N` (up to 32) write-only uploads up to 256 KiB take a buffer of their
power-of-two size class (4 KiB at least) from a pool of idle buffers that are still mapped, and the
fence work that used to free a buffer gives it back to the pool. Only buffers whose fence signalled are
in the pool, so none is in a batch the GPU still runs and each is written without a wait. Uploads that
read keep a buffer of their own, since their map waits for the GPU's copy into it. The pool holds at
most N buffers and 2 MiB of GART, oldest freed first, and is emptied when the screen goes away.

Measured on ClassiCube's `chunks1`: 50.9 fps both ways with 4% less CPU
(gamedb:batch/2026-10-18-mesa-416-hot-spots), so it stays off. It follows
[mesa-fence-bo-pool](../mesa-fence-bo-pool/patch.md) (0034); [mesa-staging-pool-big](../mesa-staging-pool-big/patch.md)
(0040) extends the same idea to uploads over 256 KiB, and [mesa-out-of-gart](../mesa-out-of-gart/patch.md)
(0049) drains both pools when GART runs out.
