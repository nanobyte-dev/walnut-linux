+++
component = "ps3rsx"
patches = []
aliases = ["ps3rsx 1214001"]
category = "stability"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3rsx-shadow"]
batches = []
+++
# The validator refuses a swizzled render target 1 pixel wide

RT_FORMAT with the swizzled type and a log2 width of 0 raises graph exception 0x106. Commit 1214001
(2026-10-14) made the validator refuse that word. Mesa keeps such render targets linear on the PS3
([mesa-texture-uploads-gart](../mesa-texture-uploads-gart/patch.md), Mesa 0007, which absorbed the old
`0012-nv30-ps3-no-swizzled-render-targets-1-pixel-wide.patch`), and
[mesa-thin-mip-levels](../mesa-thin-mip-levels/patch.md) (0013) does the same for the small levels of mipmap
generation. Linear surfaces 1 pixel wide are fine.

## Investigation

- **What crashed:** xeglgears and LXQt sessions wedged the RSX with graph exception 0x106; the dumps are
  `docs/dumps/exception-0x106-lxqt-start.txt`, `exception-0x106-lxqt-xorg.txt` and
  `exception-0x106-resize.txt`.
- **Why:** glamor draws into 1-pixel-wide pixmaps, and the RSX can't render into a swizzled surface 1 pixel
  wide. A 1x64 target raises it; 2x64, 64x1 and a linear 1427x1 render correctly
  ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)).
- **How it was found:** the crashes of 2026-10-14, isolated with egl-thin
  (`packages/walnut/tools/ps3-gpu-tools/scripts/rsx-thin-test.sh`) and a window resize repro.
- **Minimal repro:** `validate-test.c` sends the 1-pixel-wide swizzled RT_FORMAT and expects the refusal
  (`tools/run-tests.sh --only ps3rsx-shadow`); with the rule off (`fix-off.sh`, variant `thin-swizzled-rt`,
  `make -C packages/walnut/drivers/ps3rsx/test fix-off`) the case fails. `rsx-thin-test.sh` is the PS3-side
  test of the shapes.

## Validation

Not yet validated on the PS3.
