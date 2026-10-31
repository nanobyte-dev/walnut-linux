+++
component = "mesa"
patches = ["patches/0052-nv30-no-A16-X16-textures-on-the-PS3.patch"]
aliases = ["Mesa 0052"]
category = "stability"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++

# No A16 (X16) textures on the PS3

On the PS3, `is_format_supported` now refuses sampler views of the formats nv30 maps to NV40's A16
(format 0x14, libgcm's X16): L16, A16, I16 and R16, unorm and snorm. The state tracker then takes an
8-bit format of the same kind, so `GL_LUMINANCE16` becomes L8. For mpv this means its dither texture
has 8 bits of resolution instead of 16, which an 8-bit display hardly shows. Games rarely ask for the
sized 16-bit formats, and R16 needs `ARB_texture_rg`, which nv30 doesn't have.

It goes with ps3rsx refusing the format ([ps3rsx-validator-x16](../ps3rsx-validator-x16/patch.md)), so a
Mesa without this patch gets its submit rejected instead of wedging the GPU, and with
[mesa-cancel-recover](../mesa-cancel-recover/patch.md) (0053) such a program keeps running and draws
nothing.

## Investigation

- **What crashed:** `mpv --vo=gpu --hwdec=no` on a 360p H.264 clip wedged the RSX at its first frame on
  2026-10-28 (kernel .874, Mesa ps3.924, ps3rsx 0.924): graph exception 0x105, which nothing short of a
  console reboot recovers.
- **Why:** mpv's renderer on GL 2.1 draws each frame in one pass that samples the three planes and a
  64x64 `GL_LUMINANCE16` dither texture, which nv30 stores in format 0x14, swizzled, NEAREST, REPEAT.
  Nothing had sampled that format on the RSX before. The PFIFO cache in the dump shows PGRAPH took the
  whole submit up to the fence's release, so the draw raised the exception. Why the RSX faults on it is
  not known; the draw's other new feature, `gl_FragCoord.xy`, stays the second suspect until the PS3
  check in [TESTS.md](../../../docs/TESTS.md).
- **How it was found:** the video baseline for the browser; the dump
  (`docs/dumps/exception-0x105-mpv-vo-gpu.txt`) lined up word for word with mpv's first frame captured
  on the PC through the mesa-shim ([the analysis](../../../docs/dumps/exception-0x105-mpv-vo-gpu.md)).
- **Minimal repro:** on the host, the captured frame as a validator fixture
  (`packages/walnut/drivers/ps3rsx/test/fixtures/mpv-vo-gpu-0x105.h`, `tools/run-tests.sh --only
  ps3rsx-shadow`): refused with the X16 dither texture, accepted as 0052 sends it (L8). It shows the
  stream, not the wedge itself. On the PS3: mpv on a Mesa without 0052, where the expected result is
  the validator's rejection in dmesg, not a wedge.

## Validation

Not yet validated on the PS3.
