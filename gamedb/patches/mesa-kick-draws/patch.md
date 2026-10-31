+++
component = "mesa"
patches = ["patches/0026-nv30-optional-submission-every-N-draws-on-the-PS3-NV.patch"]
aliases = ["Mesa 0026"]
category = "performance"
switch = "NV30_PS3_KICK_DRAWS"
default = "0 (off)"
gating = "switch NV30_PS3_KICK_DRAWS"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-282-armagetron", "2026-10-17-mesa-282-kick100"]
+++

# nv30: optional submission every N draws on the PS3

Without relocations ([mesa-no-relocations](../mesa-no-relocations/patch.md), 0019) a context submits only
when its 256 KB push buffer fills, on a flush or for a query, so a frame that fits reaches the GPU in one
piece at SwapBuffers: the GPU no longer works on the start of a frame while the CPU builds the rest.
`NV30_PS3_KICK_DRAWS=N` (PS3 only, read once at screen creation) submits what a context queued every N
draws, at the start of a draw with no primitive open; the counter restarts at each flush. The command
stream is the same, cut into more submissions.

On Armagetron every 50, 100 or 150 draws brought 50 fps back to 58.5 to 60.5
(gamedb:batch/2026-10-17-mesa-282-armagetron). But a fixed count didn't suit every game: at 100 it gained
OpenArena 3%, cost X-Moto 9% and left ClassiCube flat with 9% more CPU
(gamedb:batch/2026-10-17-mesa-282-kick100). So it stayed off, and
[mesa-kick-auto](../mesa-kick-auto/patch.md) (0027) submits by queued work and GPU progress instead; a
set `NV30_PS3_KICK_DRAWS` still overrides 0027.
