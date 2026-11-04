+++
component = "mesa"
patches = ["patches/0058-nv30-the-blit-draws-with-its-own-window-state-and-zeta.patch"]
aliases = ["Mesa 0058"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: the blit is the same on every GPU"
forwarded = "no"
tests = ["mesa-blit-state:state", "mesa-blit-state:golden"]
batches = []
+++

# The 3D copy sets the window state and zeta it draws with

`nv30_transfer_rect_blit` (nv30_transfer.c) is the copy done by drawing a quad into a render target of
the destination's size: texture uploads and copies the M2MF and SIFM paths can't do. It set the target
and what the draw reads, and left the rest as the last draw had it: the window offset
(`VIEWPORT_TX_ORIGIN`), the window clip (the two words after it), `COORD_CONVENTIONS` and the zeta
surface. The blit's writes were bounded by the previous framebuffer's window, and its zeta was another
surface of another size, in another place. The kernel's draw checks (slice c1 of P4) refuse a draw whose
stale window reaches outside the surface it draws to: 173 blits in a Quakespasm session, 5 in ETR and 6
in Armagetron.

The blit now sets what `nv30_validate_fb` and `nv30_validate_fragment` set for a framebuffer: window
offset 0, the window clip (w-1, h-1) of its destination, `COORD_CONVENTIONS` with the destination's height,
and zeta = the destination (offset, pitch, and on the PS3 the zeta context DMA beside the colour one), as
`nv30_validate_fb` does when there is no depth buffer. Depth, stencil and alpha test are off in the blit,
so zeta is never touched. `NV30_NEW_FRAMEBUFFER | NV30_NEW_FRAGPROG` are marked, so the next normal
draw restores the framebuffer state and `COORD_CONVENTIONS`. A one-byte destination keeps the Z16 zeta
format (no smaller one exists).

On other GPUs the stream gains the same words (the window block and the COORD word are what any
framebuffer has; no behaviour changes), and the DMA words exist only on the PS3.

## Investigation

- **How it was found:** the security review of P4's slice c1 (private, round 3), replaying the private
  corpus through the kernel's draw rule: every refusal was a blit.
- **Minimal repro:** `packages/patched/graphics/mesa/tests/blit-state-test.c` (`tools/run-tests.sh
  --only mesa-blit-state`): nv30_transfer.c against a fake push buffer; the decoded stream of a swizzled
  and a linear blit, a GART destination and the NV30 class. Part `golden` runs the same three blits with
  nv30_transfer.c as it was before the patch and checks that only the words above differ (27 more
  words in three blits).

## Validation

Not yet validated on the PS3: a capture with this Mesa, replayed with `corpus-replay.sh`, should
show no "drawing outside a swizzled surface" in Quakespasm, ETR and Armagetron, and the same Mesa A/B as
any Mesa change (stalls and the longest frame first; blits are rare).
