+++
component = "mesa"
patches = ["patches/0007-nv30-texture-uploads.patch"]
aliases = ["Mesa 0007"]
category = "stability"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = ["ps3:gl-texsub", "ps3:egl-thin"]
batches = []
+++

# nv30: texture uploads that fit the PS3 GART, and render targets the RSX can draw into

The texture and render-target rules the PS3 needed in its first week, most of them crashes:

- **Banded uploads.** A 1080p window texture (SDL's desktop-GL renderer streams the whole window, as
  ClassiCube's launcher did) asked for 8100 KiB of contiguous GART, which often didn't fit: the upload
  failed and the window kept stale content. Uploads bigger than 1 MB now go in bands of rows.
- **At most 4 MB of a context's staging in flight.** A staging buffer is freed only once the fence after
  its copy signals. A burst of uploads in one frame (OpenArena loading player models mid-game) queued
  more staging than the whole GART, and other clients' allocations failed: Xorg crashed. Past 4 MB a
  context waits for its uploads, and waits again before giving up on an allocation.
- **1-pixel-wide render targets stay linear.** A swizzled render target 1 pixel wide raises graph
  exception 0x106.
- **Textures in CPU-visible VRAM.** nv30's CPU copy path maps textures, and VRAM above 224 MB has no CPU
  mapping; mapping one there gave NULL and X crashed on xclock's antialiased drawing (commit 60c57f7).
- **Big-endian program words.** The blit's fragment program constants are halfword-swapped like every
  RSX fragment program, and a GART blit destination stays in GART (the kernel's buffers are pinned).
- **A surface releases its texture.** `nv30_miptree_surface_new` took a reference nothing dropped, so
  every window resize leaked three window-sized buffers until VRAM ran out (commit 750d40a). This one is a
  plain nv30 bug and is not gated on the PS3.

[mesa-thin-mip-levels](../mesa-thin-mip-levels/patch.md) (0013) extends the linear rule to mipmap
levels, [mesa-out-of-gart](../mesa-out-of-gart/patch.md) (0049) reworks the staging wait for a full
GART, and the staging pools ([mesa-staging-pool](../mesa-staging-pool/patch.md),
[mesa-staging-pool-big](../mesa-staging-pool-big/patch.md)) reuse what this patch allocates.

## Investigation

- **What crashed:** on 2026-10-14 xeglgears and LXQt raised graph exception 0x106 (window resizes and
  the LXQt start; dumps `docs/dumps/exception-0x106-resize.txt`, `exception-0x106-lxqt-start.txt`,
  `exception-0x106-lxqt-xorg.txt`). On 2026-10-15 Xorg crashed when an upload burst exhausted the then
  16 MB GART, and X crashed on a NULL map of a GPU-only texture.
- **Why:** 0x106 is rendering to a swizzled surface 1 pixel wide (RT_FORMAT type swizzled, log2 width 0):
  glamor draws into 1-pixel-wide pixmaps. 2x64, 64x1 and linear 1427x1 targets render correctly, only
  the swizzled 1-wide one faults ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)). The
  Xorg crash was GART exhaustion by one client's staging; the NULL map was VRAM above 224 MB, which the
  CPU can't map.
- **How it was found:** the LXQt desktop and window resizes (repro scripts `rsx-resize-repro.sh`,
  `rsx-lxqt-drag-repro`), then egl-thin isolated the size (commits 7da1479, 1214001); OpenArena's
  mid-game model loads (commit 82b6b5f); xclock (commit 60c57f7).
- **Minimal repro:** `egl-thin` in `packages/walnut/tools/ps3-gpu-tools/mesa-tests` renders into a
  W x H target (`egl-thin 1 64` is the faulting case; on a ps3rsx since 2026-10-14 the validator refuses
  it instead of wedging). `gl-texsub` covers the banded window-texture uploads. The staging burst has
  none yet: proposed a GL test that uploads more than the GART in one frame from one context while a
  second process allocates.

## Validation

Not yet validated on the PS3.
