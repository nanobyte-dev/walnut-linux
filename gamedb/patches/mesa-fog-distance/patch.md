+++
component = "mesa"
patches = ["patches/0010-ffvertex-fog-distance.patch"]
aliases = ["Mesa 0010"]
category = "correctness"
switch = "none"
default = "always on"
gating = "none: core Mesa's fixed-function vertex program, every driver"
forwarded = "no"
tests = ["ps3:gl-sky", "ps3:gl-fog"]
batches = []
+++

# mesa: fixed-function fog distance -Ze instead of abs(Ze)

Mesa's fixed-function vertex program computes the fog coordinate as abs(Ze), the eye-space depth. abs()
is not affine: across a triangle whose vertices lie partly behind the eye, the interpolated fog
coordinate is wrong. [ClassiCube](../../games/classicube/)'s sky is one big quad reaching far behind the
camera, and it came out fogged white in every direction except those where no corner was behind. -Ze
equals abs(Ze) at every visible fragment (they are all in front of the eye, Ze < 0) and interpolates
exactly, so the fog is the same wherever it was right and right where it wasn't. The change is in core
Mesa (`ffvertex_prog.c`), so softpipe gives the same picture (commit 7d11f9d, 2026-10-15, with the
`gl-sky` test).
