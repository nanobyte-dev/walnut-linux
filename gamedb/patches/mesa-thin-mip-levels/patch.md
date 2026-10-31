+++
component = "mesa"
patches = ["patches/0013-nv30-thin-mip-levels.patch"]
aliases = ["Mesa 0013"]
category = "stability"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = ["ps3:gl-genmip"]
batches = ["2026-10-16-mesa-186-checks", "2026-10-16-mesa-223-checks"]
+++

# nv30: no swizzled render target 1 pixel wide in small mip levels

Mipmap generation renders every level with a u_blitter draw, and in a power-of-two texture that isn't
square the small levels are 1 pixel wide or high. A swizzled render target 1 pixel wide raises graph
exception 0x106 on the RSX. [mesa-texture-uploads-gart](../mesa-texture-uploads-gart/patch.md) (0007)
had kept only 1-pixel-wide textures linear; levels deep in a mipmap chain still hit it. On the PS3 now:

- a render-target or depth texture with a level 1 pixel wide but taller (any width < height texture whose
  chain reaches width 1) stays linear;
- a swizzled level 1 pixel high (1x1 included) is laid out as a plain row of texels and rendered and
  cleared as a linear surface, with the offset's remainder below 64 bytes as the window x origin.

That also fixes a correctness bug: small levels at offsets not 64-byte aligned went through
`nv30_validate_fb`'s 16x2 trick, which hits the right texels only for square levels (in a 256x16 texture
the 4x1 and 1x1 levels landed on the wrong ones). Every path that renders into a level is covered: blits
(mipmap generation, `glCopyTexImage`), render to texture, and `clear_render_target`/`clear_depth_stencil`,
which now set the window origin and clip themselves.

## Investigation

- **What crashed:** nothing wedged, because ps3rsx's validator already refused the render target: the
  whole push was lost (`rejected push: swizzled render target 1 pixel wide`), level 0's upload with it,
  and the texture showed wrong levels. Without the validator the same stream raises 0x106 and wedges the
  RSX until a reboot.
- **Why:** the RSX can't render to a swizzled surface of log2 width 0
  ([RSX.md](../../../docs/RSX.md#errors-and-what-wedges-the-gpu)); Mesa's GPU mipmap generation renders
  every level, including the 1-wide ones of non-square textures.
- **How it was found:** `gl-genmip` on Mesa ps3.186 (gamedb:batch/2026-10-16-mesa-186-checks), after
  [Neverball](../../games/neverball/)'s non-square textures; SuperTuxKart and Luanti hit it too
  ([gamedb:neverball/driver-bugs](../../games/neverball/investigations/2026-10-16-driver-bugs.md)).
- **Minimal repro:** `gl-genmip` in `packages/walnut/tools/ps3-gpu-tools/mesa-tests` draws every level of
  non-square textures and renders small levels through a framebuffer; a refused submit shows as wrong
  levels there and as a validator line in dmesg. It passes since Mesa ps3.223
  (gamedb:batch/2026-10-16-mesa-223-checks).

## Validation

Not yet validated on the PS3.
