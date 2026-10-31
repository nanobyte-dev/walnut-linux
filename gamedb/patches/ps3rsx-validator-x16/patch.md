+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 0.1036", "ps3rsx 0a43c01"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# The validator refuses the X16 texture format

Since ps3rsx 0.1036 (commit 0a43c01, 2026-10-29) TEX_FORMAT with format 0x14 (nouveau's A16, libgcm's X16)
is refused in fragment texture units 0 to 15 and vertex texture units 0 to 3. Mesa stopped offering the
formats that map to it on the PS3 the same day ([mesa-no-x16](../mesa-no-x16/patch.md), Mesa 0052). The
same commit also refused a channel for good after any rejected push; that rule is now the fallback for
clients that don't recover ([ps3rsx-rejected-submit-cancel](../ps3rsx-rejected-submit-cancel/patch.md)).

## Investigation

- **What crashed:** mpv `--vo=gpu` (0.41, `--hwdec=no`, a 360p H.264 clip) wedged the RSX with graph
  exception 0x105 at its first frame, on 2026-10-28 (kernel .874, Mesa ps3.924, ps3rsx 0.924).
- **Why:** mpv's dither texture is `GL_LUMINANCE16`, which nv30 stores as L16 in the A16 format (0x14),
  swizzled, sampled NEAREST with GL_REPEAT. Nothing had sampled that format on the RSX before. The draw's
  other new feature, `gl_FragCoord.xy`, is the second suspect until the PS3 check in
  [TESTS.md](../../../docs/TESTS.md) Next.
- **How it was found:** the PFIFO cache in the dump lined up word for word with mpv's first frame
  captured on the PC (our ppc64 Mesa under qemu with the nouveau drm-shim answering like ps3rsx): PGRAPH had
  taken the draw and the fence's release, so the draw raised it
  ([analysis](../../../docs/dumps/exception-0x105-mpv-vo-gpu.md), `docs/dumps/exception-0x105-mpv-vo-gpu.txt`).
- **Minimal repro:** `validate-test.c` runs mpv's first frame as captured
  (`test/fixtures/mpv-vo-gpu-0x105.h`: refused; with the dither texture as L8, accepted) and every texture
  format in every unit (`tools/run-tests.sh --only ps3rsx-shadow`); with the rule off (`fix-off.sh`, variant
  `x16`, `make -C packages/walnut/drivers/ps3rsx/test fix-off`) the case fails.

## Validation

Not yet validated on the PS3.
