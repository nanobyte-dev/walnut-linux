+++
component = "mesa"
patches = ["patches/0064-nv30-textures-and-render-targets-are-addressed-in-VRAM-off-the-PS3.patch"]
aliases = ["Mesa 0064"]
category = "correctness"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-bo-domain:state", "mesa-bo-domain:fix-off"]
batches = []
+++

# Textures and render targets are addressed in VRAM off the PS3

`nv30_bo_domain` (0055) names a buffer's domain from `bo->flags`, VRAM or else GART, for the PS3, where an overflowed or
imported texture can be in GART. Off the PS3 a buffer whose flags were not filled in yet, or one imported from system memory,
was then addressed as GART in the texture, surface and clear state, where the stock driver says VRAM and leaves the placement
to the kernel's relocation. `nouveau_device::ps3` (set by the screen next to `is_ps3`) tells it which machine it is. On the
PS3 the rule is the same as before.

## Validation

Host: `bo-domain-test.c` runs every flag combination: on the PS3 each equals 0055's rule (the golden check), elsewhere always
VRAM; it fails against `nv30_winsys.h` as it was. The PS3 stream does not change by construction. Nothing to measure on the PS3.
