+++
component = "mesa"
patches = ["patches/0045-nv30-TXB-as-TEX-on-the-RSX-NV30_TXB_HW-1-keeps-the-h.patch"]
aliases = ["Mesa 0045"]
category = "correctness"
switch = "NV30_TXB_HW"
default = "0 (TXB emitted as TEX)"
gating = "none: nvfx emits TXB as TEX on every nv30, not only the RSX; NV30_TXB_HW=1 keeps the hardware TXB"
forwarded = "no"
tests = []
batches = []
+++

# TXB as TEX on the RSX

`texture2D(s, uv, bias)` becomes TGSI TXB, which nvfx emitted as the fragment program's TXB (opcode
0x31) with the bias in .w. On the RSX that lookup samples wrong whatever the bias: Warzone 2100 4.3.5's
3D shaders (terrain, decals, models, all with a bias of 0) came out dark and flat. A shader mod that
drew the same lookups in stripes showed TEX right and TXB wrong with bias 0 and with bias -20 alike
(2026-10-20, checked on the webcam).

The bias is now dropped and the lookup emitted as TEX: exact for a bias of 0, the hardware's own level
choice for any other bias. `NV30_TXB_HW=1` emits TXB as before, for tests, and is part of
[mesa-shader-cache](../mesa-shader-cache/patch.md)'s fragment program key. An exact bias would need
another route (TXD with scaled derivatives, untried). The investigation is
[gamedb:warzone2100/dark-3d-view](../../games/warzone2100/investigations/2026-10-20-dark-3d-view.md);
the fact is in [RSX.md](../../../docs/RSX.md#mesa-kms-and-x-on-the-driver).
