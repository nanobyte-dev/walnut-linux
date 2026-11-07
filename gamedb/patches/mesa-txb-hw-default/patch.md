+++
component = "mesa"
patches = ["patches/0065-nv30-TXB-is-the-hardware-s-TXB-off-the-PS3.patch"]
aliases = ["Mesa 0065"]
category = "correctness"
switch = "none"
default = "NV30_TXB_HW: off on the PS3, on elsewhere"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-fragprog:txb", "mesa-fragprog:golden", "mesa-fragprog:fix-off"]
batches = []
+++

# TXB is the hardware's TXB off the PS3

[mesa-txb-as-tex](../mesa-txb-as-tex/patch.md) (0045) emitted fragment program TXB (texture2D with a bias) as a plain
TEX for every NV3x and NV4x, because the RSX's TXB samples wrong. Other hardware's TXB is right and the bias is part of the
shader. `NV30_TXB_HW` now defaults to off on the PS3 only (`nv30_screen::txb_hw`, `nv30_fragprog::txb_hw`) and to on
elsewhere. It is part of the disk shader cache's key as before; the PS3's key does not change (it was 0 and still is).

## Validation

Host: `fragprog-test.c txb` (the opcode with the field off and on), `golden` (the PS3 translation of eight programs is the same
words as before this patch, apart from 0067's one word) and `fix-off`. Nothing to measure on the PS3 (no change there).
