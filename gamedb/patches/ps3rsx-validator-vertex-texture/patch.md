+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.1077", "ps3rsx 16aa1c9", "task #16"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# Every word of the vertex texture units checked

Since ps3rsx 0.1077 (commit 16aa1c9, 2026-10-29) every word written to the four vertex texture units
(0x900 + 0x20 per unit, up to 0x97c) is checked against what libgcm writes for NV40 vertex texture fetch:
2D fp32 textures (X32_FLOAT or W32_Z32_Y32_X32_FLOAT), no filtering, 128-byte aligned offsets below
256 MiB, wrap modes, levels, pitch and sizes in libgcm's ranges. The full rule is in
[DRIVER.md](../../../docs/DRIVER.md#the-validator).

Nothing sends these methods yet: Mesa's vertex sampler count is 0, and `nv40_verttex.c` writes only
CONTROL0 = 0. No vertex texture has been sampled on the RSX, so the rule is narrow on purpose: it is where
vertex texture fetch in Mesa would start ([backlog](../../../docs/backlog/graphics.md#vertex-texture-fetch-derivatives-instancing)).
The vertex units are off the validator's fast path, which costs nothing since no frame writes them.

## Investigation

- **What crashed:** nothing. This is a rule written ahead of use: the allowlist had the whole range open
  since [ps3rsx-validator-allowlist](../ps3rsx-validator-allowlist/patch.md), with only the offset
  alignment of [ps3rsx-validator-tex-offset](../ps3rsx-validator-tex-offset/patch.md) checked.
- **Why:** any texture setting the RSX can't sample risks a graph exception, which wedges the GPU; the
  known-good set is libgcm's.
- **How it was found:** task #16 (vertex texture fetch) in [backlog/graphics.md](../../../docs/backlog/graphics.md),
  by reading libgcm's NV40 vertex texture setup.
- **Minimal repro:** `validate-test.c` sends good and bad words of each vertex texture method in each unit
  (one write, a burst, non-incrementing; every FORMAT format byte and low byte), `tools/run-tests.sh --only
  ps3rsx-shadow`; with the rule off (`fix-off.sh`, variant `vertex-texture`, `make -C
  packages/walnut/drivers/ps3rsx/test fix-off`) the case fails.

## Validation

Not yet validated on the PS3.
