+++
component = "mesa"
patches = ["patches/0076-nouveau-on-the-PS3-stream-storage-gets-a-GART-pool-of-its-own.patch"]
aliases = ["Mesa 0076"]
category = "performance"
switch = "NV30_PS3_GART_POOL"
default = "1 (on); NV30_PS3_MM_KEEP_SLAB -1 (every slab kept); NV30_PS3_GART_STATS 0 (off)"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-gart-pool:choice", "mesa-gart-pool:keep", "mesa-gart-pool:stats", "mesa-gart-pool:wiring", "mesa-gart-pool:fix-off"]
batches = []
+++

# A GART pool of its own for stream storage, slab retention and stats

On the PS3, buffers the CPU rewrites all the time are suballocated from a GART pool of their own: stream,
staging, persistent and coherent buffers, client arrays and transfer staging areas. They then share slab
objects only with each other, and long-lived GART buffers such as index buffers share theirs only with
long-lived storage. The pool is chosen once per allocation. `NV30_PS3_GART_POOL=0` keeps one pool, as before.

`NV30_PS3_MM_KEEP_SLAB=N` lets each slab size class keep N empty slabs and free the rest, so the GART a peak
took can be given back, for measuring. The default, -1, keeps every slab, as before. `NV30_PS3_GART_STATS=1`
prints each pool's slab bytes, peak and slab counts at exit.

The split costs at most one part-used slab per size class that both pools use at once.
