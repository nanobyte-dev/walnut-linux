+++
component = "mesa"
patches = ["patches/0028-nv30-reference-the-index-buffer-before-validation-so.patch"]
aliases = ["Mesa 0028"]
category = "correctness"
switch = "NV30_IDXBUF_LATE_REF"
default = "off (1 restores the old order)"
gating = "none: every nv30 (a race in stock nv30)"
forwarded = "no"
tests = []
batches = ["2026-10-17-mesa-299"]
+++

# nv30: reference the index buffer before validation, so it is fenced

`nv30_draw_elements` added the index buffer to the buffer context only while emitting the draw, after
`nv30_state_validate` had already put the context's buffers in the kernel's list and fenced them, and
reset it right after. So the buffer the GPU fetched indices from was in neither: nouveau saw it idle (an
in-place `glBufferSubData` of indices could overwrite them while the GPU still read them), a slab
allocation was released at once when the buffer was freed (and reused for other data under the GPU's
fetch), and the kernel attached no fence to it, though ps3rsx's `cpu_prep` and buffer lifetime rely on
those. The buffer is now referenced before validation, in the same bin, and still reset after the draw,
on the software TNL and inline push paths too. A draw whose index buffer is busy may now wait or take a
staging copy where it used to race.

The race was found by reading the code (Mesa ps3.299, round 3 of the per-draw work, commit a9dc99a); no
failure on the PS3 was traced to it. It costs X-Moto about one frame a second
(gamedb:batch/2026-10-17-mesa-299: 46 fps with `NV30_IDXBUF_LATE_REF=1` against 44.5).
[mesa-index-upload](../mesa-index-upload/patch.md) (0030) fences its uploaded indices the same way.
