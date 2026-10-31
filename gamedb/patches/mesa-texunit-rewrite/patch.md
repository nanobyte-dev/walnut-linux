+++
component = "mesa"
patches = ["patches/0024-nv30-no-texture-unit-rewrite-when-its-view-and-sampl.patch"]
aliases = ["Mesa 0024"]
category = "performance"
switch = "NV30_TEXUNIT_ALWAYS"
default = "off (1 marks every rebound unit as before; the context switch fix stays)"
gating = "none: every nv30"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-262"]
+++

# nv30: no texture unit rewrite when its view and sampler stay the same

The state tracker passes nv30 all its sampler views, and binds all its samplers, when any of them changes.
nv30 marked every unit dirty and dropped every unit's buffer reference, so `nv30_fragtex_validate`
rewrote each one: 13 words and two buffer references per unit, for units that kept what they had. A
lightmapped game (OpenArena) switching one of its two textures between surfaces wrote both. A unit is now
marked only when its view or its sampler is a different object; the context holds a reference to each
bound view, so an equal pointer is the same texture.

It also fixes a latent bug: a switch between two contexts of a process on the same channel marked
everything dirty but validated only the units changed since the context last drew, so a context could
sample the other one's textures. It now marks all of the context's units, whatever the switch says.
Part of Mesa ps3.262 (gamedb:batch/2026-10-17-mesa-262).
