+++
component = "mesa"
patches = ["patches/0005-nv30-vbo-gpu-fetch.patch"]
aliases = ["Mesa 0005"]
category = "stability"
switch = "NV30_PS3_VBO_FIFO"
default = "off (the GPU fetches; 1 pushes vertices inline)"
gating = "is_ps3"
forwarded = "no"
tests = ["ps3:egl-short", "ps3:rsxdrm-smoke quirks"]
batches = []
+++

# nv30: the RSX fetches big-endian vertex and index buffers itself; no vertex type 0

Three vertex fixes for the RSX in one patch:

- **No vertex format type 0.** Type 0 (B8G8R8A8_UNORM) in a VTXFMT word raises a graph exception on the
  RSX, which wedges the whole GPU until the console reboots. On the PS3 such attributes are converted to
  float like any format the hardware can't fetch.
- **A lone inline attribute is pushed twice.** Inline vertex data with a single attribute draws nothing,
  with no exception and no fragments ([RSX.md](../../../docs/RSX.md#mesa-kms-and-x-on-the-driver)). That
  hit glamor's fills and every position-only draw, so the attribute goes out as attributes 0 and 1; the
  vertex program never reads the second.
- **GPU vertex fetch.** nv30 pushed every vertex inline on big-endian, because NV4x cards in big-endian
  Macs fetched vertex buffers wrongly. The RSX fetches big-endian vertex and index data natively (libgcm
  feeds it that way), so on the PS3 the hardware fetches from the buffers. ClassiCube went from 6.5 to
  79 fps at 1080p uncapped (60 with vsync). `NV30_PS3_VBO_FIFO=1` brings back the inline pushes, with
  the buffers in GART ([mesa-rsx-screen-quirks](../mesa-rsx-screen-quirks/patch.md)).

The register files it under stability for the type-0 rule; the GPU fetch is the bigger win in speed. The
type-0 conversion was its own patch first (commit b591cfd, 2026-10-14), the GPU fetch came the next day
(commit b39689e).

## Investigation

- **What crashed:** a draw whose vertex format had type 0 (`0x1740+` with `data & 0xf == 0`) raised a
  graph exception; ps3fb's dump gave no cause code ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)).
  The GPU stayed wedged until a console reboot, ps3fb's own channel included.
- **Why:** the RSX has no vertex type 0. Stock nv30 has it in its format table (NV4x cards take it);
  why the RSX faults on it rather than ignoring it is unknown.
- **How it was found:** phase 2 of the driver bring-up on 2026-10-14, with raw command streams from
  `rsxdrm-smoke` (commit c1bfee1: "vertex format type 0 wedges the GPU; validator rejects it"). ps3rsx's
  validator refuses type 0 since then
  ([ps3rsx-validator-vertex-type0](../ps3rsx-validator-vertex-type0/patch.md)).
- **Minimal repro:** the kernel side is covered: `rsxdrm-smoke quirks` on the PS3 sends type 0 and expects
  the validator's rejection, and `packages/walnut/drivers/ps3rsx/test/validate-test.c` (suite
  `ps3rsx-shadow`) checks every vertex format. Mesa's side: none yet; proposed a mesa-shim capture of a
  draw with a B8G8R8A8_UNORM vertex element, run through `validate-capture`, which must find no type-0
  VTXFMT word.

## Validation

Not yet validated on the PS3.
