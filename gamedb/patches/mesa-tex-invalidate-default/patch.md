+++
component = "mesa"
patches = ["patches/0066-nv30-the-texture-cache-is-invalidated-before-every-draw-off-the-PS3.patch"]
aliases = ["Mesa 0066"]
category = "correctness"
switch = "none"
default = "NV30_TEX_INVALIDATE_ALWAYS: off on the PS3, on elsewhere"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-bo-domain:state"]
batches = []
+++

# The texture cache is invalidated before every draw off the PS3

[mesa-tex-invalidate](../mesa-tex-invalidate/patch.md) (0020) invalidates the texture cache only after something changed
texture memory, because ps3rsx invalidates both caches whenever another channel ran. Nothing does that on other kernels, so
there the driver keeps its invalidate before every draw: `NV30_TEX_INVALIDATE_ALWAYS` defaults to off on the PS3 and to on
elsewhere (`nv30_tex_invalidate_always_default`).

## Validation

Host: the default for both machines (a state check in `bo-domain-test.c`; no file-before run for a one-line default). The PS3
does not change.
