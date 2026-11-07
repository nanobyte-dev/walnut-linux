+++
component = "mesa"
patches = ["patches/0070-nv30-on-the-PS3-index-buffers-are-in-GART-by-default-one-found-in-VRAM-moves-there.patch"]
aliases = ["Mesa 0070"]
category = "correctness"
switch = "NV30_PS3_IDX_GART"
default = "on on the PS3 (NV30_PS3_IDX_GART=0 turns it off); the switch does nothing elsewhere"
gating = "is_ps3"
forwarded = "no"
tests = ["mesa-placement:default", "mesa-placement:migrate", "mesa-placement:fix-off"]
batches = []
+++

# Index buffers in GART by default on the PS3

ps3rsx (from the release with the index copies, slice d) fetches a draw's indices only from its own copies of index ranges in
GART and refuses an index buffer in VRAM. [mesa-ps3-gart-placement](../mesa-ps3-gart-placement/patch.md) (0059) put index
buffers in GART behind `NV30_PS3_IDX_GART=1`; this makes that the PS3's default.

0059 places a buffer made as an index buffer (and not also as a vertex buffer). A buffer used for indices that went to VRAM
because it was made for another use first (a vertex buffer, a buffer with no bind) now moves to GART at its first indexed draw
on the GPU fetch path (`nv30_idxbuf_to_gart`), for good: one GPU copy of its contents, written by the CPU from then on like the
others, the arrays validated again (it may be one of them). The copy is kicked in a submission of its own, because the kernel's
copy of the indices waits for a GPU write into them still in flight but refuses a draw whose indices a GPU write earlier in the
same submission touched. Without room in GART the buffer stays in VRAM and the kernel refuses the draw.

It ships with that ps3rsx: the ps3rsx-modules package `Breaks:` the Mesa before it, and `NV30_PS3_IDX_GART=0` is only for a
measurement against an older ps3rsx.

## Validation

Host: the default for both machines (`placement-test default`); the move (`migrate`: a vertex-and-index buffer and one with no
bind in VRAM go to GART with one GPU copy, cpu_only, the push buffer kicked right after the copy, the arrays validated again; a
GART buffer is left alone); runs without the kick, without cpu_only and with the defaults as before must fail (`fix-off`). On
the PS3 the session 4 Mesa A/B (2026-11-06) measured GART index buffers with no frame-time or memory cost; the next session's
count-only capture with this build must show 0 index buffers in VRAM before the enforcing ps3rsx ships.
