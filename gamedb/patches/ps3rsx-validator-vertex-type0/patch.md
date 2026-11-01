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

- **What crashed:** a graph exception (no cause code noted) and a wedged RSX on 2026-10-14, blamed on a
  vertex format with `data & 0xf == 0`. Not proven: those runs sent RT_ENABLE 0x12 one push before, and that
  is what wedged the same test in 2026-10-31's session M
  ([ps3rsx-validator-rt-enable](../ps3rsx-validator-rt-enable/patch.md)).
- **Why:** the RSX has no vertex type 0 that we know of. Where nouveau's table has B8G8R8A8 there, libgcm has
  nothing. The rule stays as a guard.
- **How it was found:** the Phase 2 bring-up on 2026-10-14, with `rsxdrm-smoke quirks` covering the formats
  Mesa uses and libgcm doesn't ([RSX.md](../../../docs/RSX.md#through-the-ps3rsx-driver)).
- **Minimal repro:** `validate-test.c` tries every vertex format type and size in every slot
  (`tools/run-tests.sh --only ps3rsx-shadow`); with the rule off (`fix-off.sh`, variant `vertex-type0`,
  `make -C packages/walnut/drivers/ps3rsx/test fix-off`) the case fails. On the PS3, `rsxdrm-smoke quirks`
  checks the refusal.

## Validation

2026-11-01, kernel 6.12.111+ps3.1210~dev, ps3rsx 0.1210~dev, ps3-gpu-tools 0.1215~dev: `rsxdrm-smoke quirks`
gets the refusal and the GPU keeps running.

```
  vertex format type 0: rejected, errno 22
```

Fix off is not run on the PS3: it is not known whether type 0 traps at all. Session M's "NOT rejected"
(2026-10-31, ps3rsx 0.924) was the push after the wedge, refused by the wedged driver, not by the validator.
