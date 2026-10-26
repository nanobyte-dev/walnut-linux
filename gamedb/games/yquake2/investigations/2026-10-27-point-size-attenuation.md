+++
id = "point-size-attenuation"
games = ["yquake2"]
driver_patches = ["mesa/0051"]
game_patches = []
sessions = []
batches = []
+++

# Particles of one size at every distance: nv30 never took the vertex program's point size (2026-10-27)

Found right after the round particles of [the point-smooth fix](2026-10-26-point-smooth.md) showed
up on the PS3; the driver mechanism of this fix, Mesa 0051 and ps3rsx's PS3_ABI 2, is in
[docs/DRIVER.md](../../../../docs/DRIVER.md#the-mesa-side).

## Symptom

With Mesa ps3.874 the particles (blood, sparks, bullet trails) were round, but every one was a disc
of about 40 pixels at 1080p, whether it was next to the player or across the room. Blood sprays
turned into big overlapping blobs. The user remembered them as "gradients on PC, or much smaller".

## What the PC draws

The gl1 renderer has two particle paths (`gl1_main.c` `R_DrawParticles`, Yamagi Quake II 8.70):

- `gl1_pointparameters 1`, the default, and what both our PC copy and the PS3 use ("Point parameters:
  Okay" in the console): GL_POINTS with GL_POINT_SMOOTH, `glPointSize(gl1_particle_size * height /
  480)` (90 at 1080p), `GL_POINT_SIZE_MIN` 2, `GL_POINT_SIZE_MAX` 40 and
  `GL_POINT_DISTANCE_ATTENUATION` (0.01, 0, 0.01). A particle d units from the eye is
  90 / sqrt(0.01 + 0.01 d²) pixels, clamped to 2..40: 40 up close, 9 at 100 units, 3 at 300.
- `gl1_pointparameters 0` (or a split-screen stereo mode): textured triangles with the 16x16
  `dottexture`, whose alpha falls off towards the edge. That is the soft "gradient" particle; the
  default setup never draws it.

Our PC copy (`tools/play-pc.sh`'s image under Xvfb, llvmpipe held to GL 2.1, the demo pak's
`q2demo1.dm2` played in real time and shot with xwd once a second) shows the first path: solid round
dots, big near the player and down to a few pixels far away. That is what the PS3 has to match; the
gradients the user remembered are the other path, or another renderer.

## Cause

Mesa did its part: with attenuation on, the fixed-function vertex program writes the attenuated and
clamped size to PSIZ, nvfx puts it in the vertex program's PSZ output (NV40's VP_RESULT_EN bit 5),
and the state tracker sets `point_size_per_vertex`. But nv30 never wrote
`POINT_PARAMETERS_ENABLE` (method 0x1ee4, libgcm's `NV4097_SET_POINT_PARAMS_ENABLE`), which ps3fb's
init sets to 0, so the RSX rasterised every point at `POINT_SIZE`, the glPointSize. The same
went for `gl_PointSize` with GL_VERTEX_PROGRAM_POINT_SIZE. ps3rsx's validator didn't allow the
method either, so Mesa couldn't simply start sending it: a rejected push is lost.

## Fix

- ps3rsx: 0x1ee4 in the 3D allowlist, and the PS3_ABI getparam answers 2 (it was 1).
- Mesa 0051: the rasterizer state sets `POINT_PARAMETERS_ENABLE` from `point_size_per_vertex`; on the
  PS3 only when PS3_ABI is 2 or more, so the new Mesa on an older module keeps the old sizes instead
  of losing pushes.

## Checks (2026-10-27, booted kernel .786)

- `gl-pointsmooth` got two rows with Quake II's settings (glPointSize 40, attenuation 0.01/0/0.01,
  min 2, max 40), points at eye distance 0, 20, 40, 50, 100 and 200: 40, 20, 10, 8, 4, 2 pixels
  expected, one row smooth and one square. llvmpipe: PASS. Mesa ps3.874 on the RSX: FAIL, every point
  40. Mesa 0051 (ps3.875~dev) on ps3rsx 0.788 (PS3_ABI 1): the same FAIL and no rejected push in
  dmesg. With ps3rsx 0.875~dev (PS3_ABI 2): PASS, sizes 40, 19, 9, 7, 3, 2. The RSX drops the
  fraction of a per-vertex size; the sizes there are 19.98, 9.997, 7.998, 3.9998. The older rows
  stayed the same (smooth 0.29-0.75, squares 1.00), and `NV30_POINT_SMOOTH=0` still gives squares
  in the smooth rows. dmesg clean.
- Quake II's demo on the live desktop, xwd every 2 s, next to the PC's shots: before, 40-pixel
  blobs everywhere; after, small dots far away and big ones near, the same sizes and look as
  llvmpipe in matching scenes (the red-lit room with the gunner's blood, the ceiling-vent room). No
  crash, no ps3rsx messages.
