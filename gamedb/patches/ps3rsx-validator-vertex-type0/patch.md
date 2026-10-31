+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx c1bfee1"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow", "ps3:rsxdrm-smoke quirks"]
batches = []
+++
# The validator refuses vertex format type 0

A vertex format word (0x1740 and on) with type 0 raises a graph exception on the RSX. Commit c1bfee1
(2026-10-14) made the validator refuse it in every one of the 16 slots, so a stream that asks for it fails
its push and leaves the GPU running. Mesa never sends it on the PS3: type 0 is nouveau's B8G8R8A8 vertex
format, which [mesa-vbo-gpu-fetch](../mesa-vbo-gpu-fetch/patch.md) (Mesa 0005; the old file
`0002-nouveau-nv30-ps3-no-vertex-type-0.patch`, commit b591cfd) converts to floats instead.

## Investigation

- **What crashed:** a graph exception (no cause code noted) and a wedged RSX, at a draw whose vertex
  format had `data & 0xf == 0`.
- **Why:** the RSX has no vertex type 0. Where nouveau's table has B8G8R8A8 there, libgcm has nothing.
- **How it was found:** the Phase 2 bring-up on 2026-10-14, with `rsxdrm-smoke quirks` covering the formats
  Mesa uses and libgcm doesn't ([RSX.md](../../../docs/RSX.md#through-the-ps3rsx-driver)).
- **Minimal repro:** `validate-test.c` tries every vertex format type and size in every slot
  (`tools/run-tests.sh --only ps3rsx-shadow`); with the rule off (`fix-off.sh`, variant `vertex-type0`,
  `make -C packages/walnut/drivers/ps3rsx/test fix-off`) the case fails. On the PS3, `rsxdrm-smoke quirks`
  checks the refusal.

## Validation

Not yet validated on the PS3.
