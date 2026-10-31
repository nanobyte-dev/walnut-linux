+++
component = "mesa"
patches = ["patches/0012-nv30-two-byte-texels-big-endian.patch"]
aliases = ["Mesa 0012"]
category = "correctness"
switch = "none"
default = "always on"
gating = "none: big-endian builds only (UTIL_ARCH_BIG_ENDIAN); little-endian is unchanged"
forwarded = "no"
tests = ["ps3:gl-texfmt"]
batches = ["2026-10-16-mesa-186-checks", "2026-10-16-mesa-223-checks"]
+++

# nv30: L8A8 and R8G8 texels in big-endian byte order

Gallium stores L8A8 and R8G8 as byte arrays (L or R first), and nv30 samples them as one 16-bit A8L8
texel. A big-endian host's GPU reads 16-bit texels as big-endian halfwords, so the first byte is the high
one: on the RSX luminance and alpha came out swapped on every texel. Fonts drawn with
`GL_LUMINANCE_ALPHA` had wrong alpha in many games, [Neverball](../../games/neverball/)'s HUD first. On
big-endian the texture swizzle now takes L or R from the high byte and A or G from the low one. Packed
565, 4444 and 1555 are host-order integers and were already right.

`gl-texfmt` showed the swap (gamedb:batch/2026-10-16-mesa-186-checks) and passes since Mesa ps3.223
(gamedb:batch/2026-10-16-mesa-223-checks;
[gamedb:neverball/driver-bugs](../../games/neverball/investigations/2026-10-16-driver-bugs.md)).
