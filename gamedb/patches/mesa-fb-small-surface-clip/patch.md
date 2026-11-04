+++
component = "mesa"
patches = ["patches/0060-nv30-the-small-surface-hack-s-window-clip-stays-inside-the-block.patch"]
aliases = ["Mesa 0060"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: the same hack on every GPU"
forwarded = "no"
tests = ["mesa-fb-window:state", "mesa-fb-window:golden"]
batches = []
+++

# The small-surface hack's window clip stays inside its block

`nv30_validate_fb` draws a swizzled render target whose offset is not 64-aligned (a 2x2 mip level, 16 bits a pixel)
as a 16x2 swizzled block and moves the window origin right by the offset's remainder (x = 8) to land on the
real pixels. The window clip stayed at the block's whole width (max 15), so the window origin plus the clip
spanned x more columns than the block: 24 against 16. The writes stay inside the 2x2 (viewport and scissor), but
ps3rsx's draw rule (P4 slice c1) bounds a draw by origin plus clip and refused the draw (Armagetron, 2 draws).

In the hack the window clip is now `w - x` columns wide (max 7 for x = 8). Every other target keeps its clip.
The linear sibling (an unaligned linear target grows its width by x) has the same shape and is left alone: no
refusal for it in the corpus.

## Investigation

- **How it was found:** the third wedge capture's replay through the slice c1 rule
  (round 3, lane A's note on Armagetron's two draw hits).
- **Minimal repro:** `packages/patched/graphics/mesa/tests/fb-window-test.c` (`tools/run-tests.sh --only
  mesa-fb-window`): nv30_validate_fb against a fake push buffer for the 2x2 hack, an ordinary swizzled target, an
  aligned 2x2 and a linear unaligned one; `golden` compares the stream of the four with the file before the patch:
  one word differs, the hack's clip.

## Validation

Not yet validated on the PS3: Armagetron's capture replayed through `corpus-replay.sh` with this Mesa.
