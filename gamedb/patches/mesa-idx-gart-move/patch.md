+++
component = "mesa"
patches = ["patches/0071-nv30-on-the-PS3-an-index-buffer-s-move-to-GART-keeps-it-coherent-and-drops-one-draw-not-a-submission.patch"]
aliases = ["Mesa 0071"]
category = "correctness"
switch = "NV30_PS3_IDX_GART (0070); NV30_PS3_IDX_STATS=1 counts the moves"
default = "on on the PS3, with NV30_PS3_IDX_GART"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-placement:migrate", "mesa-placement:wiring", "mesa-placement:fix-off"]
batches = []
+++

# An index buffer's move to GART keeps it coherent and costs at most one draw

Fixes the review of [mesa-idx-gart-default](../mesa-idx-gart-default/patch.md) (0070). Before every indexed draw on the PS3
(`nv30_idxbuf_to_gart`):

- A buffer already in GART becomes CPU-written (cpu_only) whatever made it (stream usage, no bind, a VRAM allocation that fell
  back to GART), and a GPU write into it still queued in this submission (a staging copy, a CopyBufferSubData) is kicked first:
  ps3rsx waits for an earlier submission's write but refuses a draw whose indices a write earlier in the same submission touched.
- A VRAM buffer with a current system memory copy is filled into GART by the CPU (no GPU copy, no kick); the copy is freed.
- Otherwise the GPU copies it in a submission of its own. Until that has run the buffer counts as GPU-written (all of it valid,
  its fences that submission's, GPU_WRITING), so a CPU write or read right after it waits; a stale system memory copy is freed.
  If that submission is cancelled the buffer stays in VRAM and moves later.
- No room in GART: one wait for the GPU and another try. A buffer that can't move (no room, a lost copy, a shared buffer) drops
  its own draw, not the submission.
- The software TNL path maps a buffer the GPU still writes synchronized.

`NV30_PS3_IDX_STATS=1` prints a stderr line each time the CPU moves, GPU moves, kicks or failures pass a power of two (with the
seconds since the screen was made), for the measurement session. `NV30_PS3_IDX_GART=0` or `NV30_PS3_FP_GART=0` on the PS3 prints
a line that ps3rsx refuses that placement.

Known limits: another context sharing a moved vertex buffer keeps its old address until it validates its arrays again (as for any
reallocation in nouveau; shared buffers used for indices across contexts are rare). A vertex buffer orphaned every frame
(glBufferData) and used for indices moves every frame; the CPU fill makes that a memcpy when its system memory copy is current,
otherwise a GPU copy and a submission each time. Left to the measurement (the stats line shows the rate). Vertex data in a
combined buffer is fetched from GART after the move, and cpu_only makes a busy update wait instead of staging: both measured in
the next ABBA (stalls, 1% lows).

## Validation

Host: `placement-test migrate` checks the state after a GPU move (range, fences, GPU_WRITING, a write right after it waits), the
CPU fill (contents, no copy, no kick, the copy freed), a stream GART buffer made cpu_only with a queued GPU write kicked and an
earlier one not, GART full (one wait, then the draw dropped), a cancelled copy (VRAM kept, moved later), a shared buffer;
`wiring` greps the draw hook, both screen defaults and both swtnl maps; `fix-off` turns each rule off (10 variants in
nv30_resource.c, the defaults, 4 call sites), each must fail. Not measured on the PS3 yet.
