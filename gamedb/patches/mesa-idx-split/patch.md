+++
component = "mesa"
patches = ["patches/0072-nv30-on-the-PS3-an-indexed-draw-past-the-kernel-s-per-binding-cap-is-drawn-in-pieces.patch"]
aliases = ["Mesa 0072"]
category = "correctness"
switch = "NV30_PS3_IDX_SPLIT (0 as before); NV30_PS3_IDX_STATS=1 counts the splits"
default = "on on the PS3"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-idx-split:model", "mesa-idx-split:cap", "mesa-idx-split:wiring", "mesa-idx-split:fix-off"]
batches = []
+++

# An indexed draw past the kernel's per-binding cap is drawn in pieces

ps3rsx copies the indices each index buffer binding draws into a ring of its own and refuses a binding whose primitives span more
than 1 MiB of indices. midprim's `elements` case (524032 U32 indices, 2 MiB in one draw) was refused at every submission on arm B:
Mesa sent its state again and the draws were lost, with no GL error.

On the PS3 nv30 now draws such a draw as pieces (`nv30_idx_split.h`), each with its own IDXBUF_OFFSET/FORMAT at the 256-byte step at
or below its first index and at most 1 MiB - 4 KiB of indices, so with the kernel's skew (its copy starts up to 255 bytes early)
every binding stays under the cap:

- Lists are cut on primitive boundaries; line strips overlap by one index, triangle and quad strips by two, each piece starting an
  even number of indices into its strip so the winding holds.
- With primitive restart the cut goes right after the last restart index in the window when there is one, otherwise it counts
  from the last restart, where a list's grouping and a strip's alternation start again. The pieces keep their restart indices.
- Fans, polygons and line loops share their first vertex across any cut: they are written as a triangle (line) list into the
  upload buffer, restart indices consumed and each triangle's flat-shading vertex the one GL gives it (last or first
  convention), and that list is split.

The kernel also refuses a submission whose index copies pass the file's share of its ring (1.25 MiB), so two pieces never share
one: a piece that would pass it, counting the copies of the indexed draws since the last kick as the kernel lays them out, goes in
a submission of its own. The kernel reuses a copy for the same range within a submission, so the count only overestimates: an
extra kick, never a refusal. A kick that ends a submission inside an indexed draw's primitive (the push buffer full) leaves the
kernel holding up to 1 MiB for the continuation, so the next submission's count starts at 1 MiB. midprim's frame of 200 such
draws becomes about 600 submissions.

`NV30_PS3_IDX_SPLIT=0` draws as before (which the kernel refuses). `NV30_PS3_IDX_STATS=1` prints a line as the split, converted and
unsplittable draws and the kicks between pieces pass each power of two. The winsys's line for a cancelled submission now says that its draws are lost and that
dmesg says why: GL has no error for a lost submission, so the stderr line is all the app's user sees.

The cap is ps3rsx's IXS_MAX (`idxsnap.h`) as a constant here: no GETPARAM reports it. Known limits: a polygon over the cap drawn in
line mode shows its fan's inner edges, and a line loop's stipple restarts at each segment. Both are rare (a single primitive of
hundreds of thousands of vertices).
