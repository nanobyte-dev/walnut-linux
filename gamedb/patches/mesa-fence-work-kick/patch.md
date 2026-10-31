+++
component = "mesa"
patches = ["patches/0041-nouveau-submit-for-deferred-frees-only-past-a-set-co.patch"]
aliases = ["Mesa 0041"]
category = "performance"
switch = "NV30_FENCE_WORK_KICK"
default = "64 (as before)"
gating = "switch NV30_FENCE_WORK_KICK"
forwarded = "no"
tests = []
batches = ["2026-10-19-ssdroot6-fence-work-kick", "2026-10-20-ssdroot9-fence-work-kick"]
+++

# Submit for deferred frees only past a set count

`nouveau_fence_work()` queues a function to run once a fence signals, and submits the fence's batch as
soon as more than 64 of them wait on it, to bound the memory they hold. Every `glBufferSubData` of a
VRAM buffer queues two (the staging buffer and its suballocation), so a game that streams its vertices
that way submitted every 32 updates. On the PS3 each submission is a pushbuf ioctl through ps3rsx's
validator and shadow, a new fence with its wait buffer, and the buffer list referenced again.
OpenArena's opengl2 renderer spent 12.5% of its CPU in exactly those submissions (Mesa ps3.476).

The threshold is now a field of the fence list, 64 by default so nv50 and nvc0 are unchanged; nv30
takes it from `NV30_FENCE_WORK_KICK=N` (0 never submits for that reason). With a large N the frees wait
for the next submission there would be anyway (an adaptive kick from
[mesa-kick-auto](../mesa-kick-auto/patch.md), a flush, a full push buffer), holding at most a frame's
worth of staging copies and orphaned buffers.

Measured neutral: +5% at n=2 that shrank to +1.5% at n=5, inside the spread, and the CPU the profile
promised was not saved (gamedb:batch/2026-10-19-ssdroot6-fence-work-kick,
gamedb:batch/2026-10-20-ssdroot9-fence-work-kick; [HOTPATHS.md](../../../docs/HOTPATHS.md#measured-0041-0042-0044)).
It stays at the old default. Its sibling from the same round is [mesa-query-kick](../mesa-query-kick/patch.md)
(0042).
