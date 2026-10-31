+++
component = "mesa"
patches = ["patches/0047-u_upload-st-nv30-an-upload-buffer-that-cannot-be-had.patch"]
aliases = ["Mesa 0047"]
category = "stability"
switch = "none"
default = "always on"
gating = "none: generic fixes in u_upload, u_vbuf and st/mesa, plus nv30's zero-stride attributes, for every driver"
forwarded = "no"
tests = []
batches = []
+++

# An upload buffer that cannot be had is not written through

When `u_upload_alloc()` can't get a buffer it returns a NULL pointer, and `st_setup_current()` copied
the current (zero-stride) vertex attributes there without looking. The patch closes that and the
helpers around it: `st_setup_current()` makes no copy without a buffer and leaves the vertex buffer
unbound; `u_upload_alloc()` keeps the previous upload buffer for the caller to release on a failure
(every failure leaked one before, so under memory pressure the next failure came sooner), sets the
output buffer to NULL, and no longer leaves a buffer whose map failed in place to be handed out again;
`u_vbuf_upload_buffers()` releases what it got on its error path; and nv30 reads a zero-stride
attribute without a buffer as zero instead of mapping NULL. The draw then runs with those attributes at
zero for a frame instead of crashing.

It is the first of the out-of-memory fixes from Sauerbraten's sessions; [mesa-alloc-fail](../mesa-alloc-fail/patch.md)
(0048) came in the same commit (1462313), [mesa-out-of-gart](../mesa-out-of-gart/patch.md) (0049) a day
later for SuperTuxKart. The early [mesa-mm-slab-oom](../mesa-mm-slab-oom/patch.md) (0002) and
[mesa-context-pushbuf](../mesa-context-pushbuf/patch.md) (0003) cover neighbouring paths.

## Investigation

- **What crashed:** real Sauerbraten's second played session (2026-10-25, Mesa ps3.819) segfaulted at
  address 0 in libc's memcpy, right after three `ps3rsx: out of GART for a 4096 KiB BO` lines. The same
  signature had ended five BananaBread runs on 2026-10-18 ([LOG-FINDINGS.md item 2](../../../docs/games/LOG-FINDINGS.md#2-a-failed-buffer-allocation-ends-in-a-segfault)).
- **Why:** the RSX's VRAM was full (186-194 MB of the game's), its buffers had gone to GART (46-51 MB of
  63), and the stream uploader's next 1 MiB buffer, a chunk of a 4 MiB nouveau_mm slab, didn't fit.
  `u_upload_alloc()` returned NULL and the memcpy in `st_setup_current()` (st_atom_array.cpp:343) wrote
  through it.
- **How it was found:** the session's crash, the link register placed on a fresh process's library
  layout and resolved with ps3.819's symbols
  ([gamedb:sauerbraten/online-crashes](../../games/sauerbraten/investigations/2026-10-24-online-crashes.md)).
- **Minimal repro:** None yet: proposed a host test in the style of
  `packages/patched/graphics/mesa/tests/alloc-fail-test.c` that makes the upload buffer allocation fail
  and checks that `u_upload_alloc()` returns no stale buffer, leaks nothing and that the current
  attributes are not copied.

## Validation

Not yet validated on the PS3.
