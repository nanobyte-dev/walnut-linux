+++
component = "mesa"
patches = ["patches/0008-nvfx-fp-export32.patch"]
aliases = ["Mesa 0008"]
category = "correctness"
switch = "NV40_FP_EXPORT32"
default = "on (0 turns it off)"
gating = "none: every NV4x (the RSX and PC NV40 cards alike)"
forwarded = "no"
tests = ["ps3:egl-gears"]
batches = []
+++

# nvfx: 32-bit colour exports from fragment programs (NV40)

nv30 exports a fragment program's colour from the fp16 register H0. `FP_CONTROL` bit 6 (libgcm's
"32-bit exports") takes it from the fp32 register R0 instead. That works on the RSX and rounds like an
fp32 pipeline: egl-gears against softpipe went from 2193 bytes off to 282 bytes off by 1, the rest being
the hardware's attribute interpolation ([DRIVER.md](../../../docs/DRIVER.md#phase-results-2026-10-14)).
It applies to every NV4x; `NV40_FP_EXPORT32=0` turns it off. The setting is part of the fragment
program key of the disk shader cache ([mesa-shader-cache](../mesa-shader-cache/patch.md)).

It was "Mesa 0009" in the numbering of 2026-10-14 (commit 9f3b37a).
