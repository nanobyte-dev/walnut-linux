+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.249", "ps3rsx 8e99b76"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = ["2026-10-16-mesa-223-neverball-wedge", "2026-10-17-ps3rsx-249-first-boot"]
+++
# The validator refuses texture offsets that aren't 128-byte aligned

Since version 0.249 (commit 8e99b76, 2026-10-17) the validator refuses a TEX_OFFSET of fragment texture
units 0 to 15 and vertex texture units 0 to 3 that isn't 128-byte aligned, libgcm's texture alignment. Mesa
never sends one after [mesa-blit-source-alignment](../mesa-blit-source-alignment/patch.md) (0014), so
correct streams never meet the rule; it is there so that a stream like Neverball's fails its push instead
of wedging the GPU.

## Investigation

- **What crashed:** Neverball on Mesa ps3.223 wedged the RSX with graph exception 0x105
  (gamedb:batch/2026-10-16-mesa-223-neverball-wedge); only a console reboot cleared it.
- **Why:** Mesa's blit read back a 2x2 mip level of a 32x32 L8A8 texture by sampling it at the level's
  offset, +0xaa0, which is only 32-byte aligned. The RSX can't sample a texture at such an offset.
- **How it was found:** counting the PFIFO cache entries in the exception dump put the fault in that blit
  ([gamedb:neverball/driver-bugs](../../games/neverball/investigations/2026-10-16-driver-bugs.md)).
- **Minimal repro:** `validate-test.c` replays Neverball's misaligned blit source and expects the refusal
  (`tools/run-tests.sh --only ps3rsx-shadow`); with the rule off (`fix-off.sh`, variant `tex-offset`, `make
  -C packages/walnut/drivers/ps3rsx/test fix-off`) the case fails. On the PS3, `gl-mipread` covers the Mesa
  side.

## Validation

Not yet validated on the PS3.
