+++
component = "mesa"
patches = ["patches/0050-nv30-GL_POINT_SMOOTH-points-are-round-NV30_POINT_SMO.patch"]
aliases = ["Mesa 0050"]
category = "correctness"
switch = "NV30_POINT_SMOOTH"
default = "on"
gating = "none: every nv30 (no NV3x/NV4x has a known point smoothing switch); NV30_POINT_SMOOTH=0 gives the old square points"
forwarded = "no"
tests = ["mesa-point-smooth", "ps3:gl-pointsmooth"]
batches = []
+++

# GL_POINT_SMOOTH points are round

Yamagi Quake II's gl1 renderer draws its particles as `GL_POINTS` with `GL_POINT_SMOOTH`, and on the
PS3 they came out as squares (round on llvmpipe). nv30 ignored the rasterizer's `point_smooth`:
NV3x/NV4x have no known smoothing switch (neither nouveau's method list nor libgcm has one), and the
state tracker leaves smoothing to the driver.

nv30 now does what radeonsi and zink do with `nir_lower_point_smooth`, on its TGSI: points drawn with
`point_smooth` run a variant of the bound fragment program, made at the first such draw
(`nv30_point_smooth.c`). The variant takes the point sprite's replaced coordinate in a free texture
coordinate and adds about 20 instruction slots: the distance from the centre, the fragment killed
outside the circle, and alpha times a one-pixel edge's coverage. The application's blending does the
rest. Triangles keep the plain program even with `GL_POINT_SMOOTH` left on. A program that can't take
the variant (no colour output, RET/CAL, all ten texture coordinates used) keeps square points.

History: the first build (ps3.860) bound a variant without constants with a NULL buffer and segfaulted
in `PUSH_RESRC` at Yamagi Quake II's first particles on 2026-10-26; the patch was revised (2b2ecad) so
`nv30_fragprog_validate()` uploads a program that has no VRAM buffer yet. Tested on the host
(`mesa-point-smooth`: the variant on Mesa's TGSI interpreter and its NV40 translation) and on the PS3
with `gl-pointsmooth`. The investigation is
[gamedb:yquake2/point-smooth](../../games/yquake2/investigations/2026-10-26-point-smooth.md). The
particles' sizes came next: [mesa-point-size](../mesa-point-size/patch.md) (0051).
