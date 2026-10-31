+++
component = "mesa"
patches = ["patches/0016-nv30-validate-fence-unchanged.patch"]
aliases = ["Mesa 0016"]
category = "performance"
switch = "none"
default = "always on"
gating = "none: every nv30, same reference counts and buffer status"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-246"]
+++

# nv30: no fence call for buffers already fenced in this batch

After validation every draw walks the buffers bound to it and references the context's current fence on
each one that lives in a `nouveau_mm` slab (vertex buffers, the fragment program), through an
out-of-line `nouveau_fence_ref`. The fence only changes at a kick, so from the second draw of a batch on
every one of those calls is a no-op. nv30 now compares the pointers first and calls only when they
differ; the fence is read once, after the last point in the function that can kick. An immediate-mode
draw has two to four such buffers, so this saves as many calls per draw.

It is the caller-side half of [mesa-fence-ref-unchanged](../mesa-fence-ref-unchanged/patch.md) (0015),
which makes the call itself cheap when it does happen. Measured together with 0015 and 0017 in
gamedb:batch/2026-10-17-mesa-246.
