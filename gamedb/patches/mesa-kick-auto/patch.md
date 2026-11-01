+++
component = "mesa"
patches = ["patches/0027-nv30-submit-when-the-GPU-caught-up-and-a-share-of-a-.patch"]
aliases = ["Mesa 0027", "Mesa 0026"]
category = "performance"
switch = "NV30_PS3_KICK_AUTO, NV30_PS3_KICK_MIN_BYTES, NV30_PS3_KICK_SPLIT"
default = "on (16384 bytes, a quarter of the last frame; 0 restores ps3.282's behaviour)"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-299", "2026-10-17-mesa-282-ab", "2026-10-17-mesa-282-armagetron", "2026-10-17-mesa-282-kick100"]
+++

# nv30: submit when the GPU caught up and a share of a frame is queued (PS3)

Without relocations ([mesa-no-relocations](../mesa-no-relocations/patch.md), 0019) a context submits only
when its 256 KB push buffer fills, on a flush or for a query, so a frame that fits reaches the GPU in one
piece at SwapBuffers: the GPU no longer works on the start of a frame while the CPU builds the rest.
Submitting mid-frame gives that overlap back. The first try (Mesa 0026, `NV30_PS3_KICK_DRAWS=N`)
submitted every N draws: on Armagetron every 50, 100 or 150 draws brought 50 fps back to 58.5 to 60.5
(gamedb:batch/2026-10-17-mesa-282-armagetron), but a fixed count didn't suit every game. At 100 it gained
OpenArena 3%, cost X-Moto 9% and left ClassiCube flat with 9% more CPU
(gamedb:batch/2026-10-17-mesa-282-kick100): X-Moto's many tiny immediate-mode draws made dozens of
submissions per frame, each an ioctl, the kernel's buffer list and fences. A draw count is the wrong
measure. `NV30_PS3_KICK_AUTO`, on by default on the PS3, submits at the start of a draw only when both
hold, checked without an ioctl:

- the work queued since the last submission is at least `NV30_PS3_KICK_MIN_BYTES` (16384) and at least
  1/`NV30_PS3_KICK_SPLIT` (4) of the words the last frame queued, so a frame is cut into at most about four
  parts whatever its draw count;
- the GPU has finished everything submitted before (one plain read of the mapped fence notifier). While
  the GPU still works, a submission adds only its cost.

Mesa ps3.299 got Armagetron from 50 to 61.5 fps, left OpenArena and ClassiCube unchanged, and X-Moto lost
a real 5% that switching this off didn't restore (gamedb:batch/2026-10-17-mesa-299, against
gamedb:batch/2026-10-17-mesa-282-ab on the same boot;
[gamedb:armagetronad/submission-overlap](../../games/armagetronad/investigations/2026-10-17-submission-overlap.md)).
0026 was folded into this patch on 2026-10-31, and its switch, off since the kick100 batch, went with it.
