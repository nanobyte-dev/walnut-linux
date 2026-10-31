+++
component = "mesa"
patches = ["patches/0027-nv30-submit-when-the-GPU-caught-up-and-a-share-of-a-.patch"]
aliases = ["Mesa 0027"]
category = "performance"
switch = "NV30_PS3_KICK_AUTO, NV30_PS3_KICK_MIN_BYTES, NV30_PS3_KICK_SPLIT"
default = "on (16384 bytes, a quarter of the last frame; 0 restores ps3.282's behaviour)"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-299", "2026-10-17-mesa-282-ab"]
+++

# nv30: submit when the GPU caught up and a share of a frame is queued (PS3)

[mesa-kick-draws](../mesa-kick-draws/patch.md) (0026) showed that submitting mid-frame gives back the
CPU/GPU overlap that [mesa-no-relocations](../mesa-no-relocations/patch.md) (0019) took away, but a draw
count is the wrong measure: X-Moto's many tiny immediate-mode draws made dozens of submissions per frame,
each an ioctl, the kernel's buffer list and fences. `NV30_PS3_KICK_AUTO`, on by default on the PS3,
submits at the start of a draw only when both hold, checked without an ioctl:

- the work queued since the last submission is at least `NV30_PS3_KICK_MIN_BYTES` (16384) and at least
  1/`NV30_PS3_KICK_SPLIT` (4) of the words the last frame queued, so a frame is cut into at most about four
  parts whatever its draw count;
- the GPU has finished everything submitted before (one plain read of the mapped fence notifier). While
  the GPU still works, a submission adds only its cost.

Mesa ps3.299 got Armagetron from 50 to 61.5 fps, left OpenArena and ClassiCube unchanged, and X-Moto lost
a real 5% that switching this off didn't restore (gamedb:batch/2026-10-17-mesa-299, against
gamedb:batch/2026-10-17-mesa-282-ab on the same boot;
[gamedb:armagetronad/submission-overlap](../../games/armagetronad/investigations/2026-10-17-submission-overlap.md)).
The fence-work and query submissions of [mesa-fence-work-kick](../mesa-fence-work-kick/patch.md) (0041)
and [mesa-query-kick](../mesa-query-kick/patch.md) (0042) lean on these submissions.
