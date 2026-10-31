+++
component = "mesa"
patches = ["patches/0051-nv30-point-sizes-from-the-vertex-program-POINT_PARAM.patch"]
aliases = ["Mesa 0051"]
category = "correctness"
switch = "none"
default = "always on"
gating = "none: every nv30 sets POINT_PARAMS_ENABLE; on the PS3 only when ps3rsx's PS3_ABI is 2 or more"
forwarded = "no"
tests = ["ps3:gl-pointsmooth"]
batches = []
+++

# Point sizes from the vertex program

Yamagi Quake II's gl1 particles were discs of one size at every distance on the PS3, about 40 pixels
where the PC draws them a few pixels wide far away. Mesa's fixed-function vertex program writes the
attenuated size to PSIZ and nvfx routes it to the PSZ output, but nv30 never set
`POINT_PARAMS_ENABLE` (0x1ee4), and ps3fb's init leaves it 0, so the RSX took the `glPointSize` for
every point. `gl_PointSize` and `GL_VERTEX_PROGRAM_POINT_SIZE` were ignored the same way.

The rasterizer state now sets it from `point_size_per_vertex`. The design point is the PS3 check: the
validator of ps3rsx ABI 1 rejects the method, and a rejected push was lost, so on the PS3 the method
is sent only when ps3rsx's `PS3_ABI` getparam is 2 or more (ps3rsx 0.875); an older module keeps the
old sizes. The RSX drops the fraction of a per-vertex size: 19.98 draws 19 pixels
([RSX.md](../../../docs/RSX.md#mesa-kms-and-x-on-the-driver)).

Checked on the RSX with `gl-pointsmooth`'s attenuation rows (sizes 40, 19, 9, 7, 3, 2 where llvmpipe
draws 40, 20, 10, 8, 4, 2) and in Quake II's demo. It follows [mesa-point-smooth](../mesa-point-smooth/patch.md)
(0050). The investigation is
[gamedb:yquake2/point-size-attenuation](../../games/yquake2/investigations/2026-10-27-point-size-attenuation.md).
