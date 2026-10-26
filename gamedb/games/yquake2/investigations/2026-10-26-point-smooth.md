+++
id = "point-smooth"
games = ["yquake2"]
driver_patches = ["mesa/0050"]
game_patches = []
sessions = ["2026-10-26-session"]
batches = []
+++

# Square particles: nv30 ignored GL_POINT_SMOOTH (2026-10-26)

Found in the played session of 2026-10-26; the driver mechanism of the fix, Mesa 0050, is in
[docs/DRIVER.md](../../../../docs/DRIVER.md#the-mesa-side).

## Symptom

In the session the player saw Quake II's round particles (blood, sparks, bullet trails) drawn as
squares on the PS3, against round ones on the PC (the session's review: "missing: ps3: round
particles ... draw as squares").

## Cause

Yamagi Quake II's GL1 renderer draws its particles as GL_POINTS with GL_POINT_SMOOTH. nv30 ignored
`point_smooth`, and NV3x/NV4x have no known smoothing switch, so every point was a square.

## Fix

Mesa 0050 (`NV30_POINT_SMOOTH`, on by default; 0: the old squares): points drawn with GL_POINT_SMOOTH
run a variant of the bound fragment program (`nv30_point_smooth.c`, a TGSI transform like
`nir_lower_point_smooth`) that puts the point sprite's replaced coordinate in a free texture
coordinate, kills the fragment outside the circle and multiplies alpha by a one-pixel edge's coverage,
about 20 instruction slots more.

The first build (Mesa ps3.860) passed `gl-pointsmooth` on the RSX (lit/S^2 0.29-0.75 for point sizes
2-48, squares with `NV30_POINT_SMOOTH=0`) but segfaulted in Yamagi Quake II at its first particles (core:
`nv30_fragprog_validate`, `PUSH_RESRC` with the smooth point variant's buffer NULL). The variant is
translated when it is made, so validate's upload for a program not yet translated never ran for it; with
no constants (Quake II's particle colours come from an array) nothing else uploaded it. ps3.863:
`nv30_fragprog_validate` uploads any program that has no VRAM buffer, and `gl-pointsmooth`'s
point-parameter row now draws like Quake II (vertex and colour arrays, glDrawArrays); its glColor rows
had a constant colour, which went through the stream copy and hid the bug (commit 2b2ecad). The PS3
check, `gl-pointsmooth` and then Quake II's particles against the PC shots, is docs/TESTS.md's batch
mesa-0050.
