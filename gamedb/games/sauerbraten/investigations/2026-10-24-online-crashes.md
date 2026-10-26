+++
id = "online-crashes"
games = ["sauerbraten"]
driver_patches = ["mesa/0046", "mesa/0047", "mesa/0048"]
game_patches = []
sessions = ["2026-10-24-session", "2026-10-25-session"]
batches = []
+++

# Two crashes in Mesa: a query's report slot taken back, then a NULL upload buffer (2026-10-24)

The real Sauerbraten (our cube2 on the 2020_12_29 release's data) crashed in both of its first played
sessions, each time in Mesa's nv30 driver. The fixes are Mesa 0046, 0047 and 0048, whose mechanisms
are in [docs/DRIVER.md](../../../../docs/DRIVER.md#the-mesa-side); upstream nv30 has the same code.

## Symptom

- Session of 2026-10-24 (an online insta-CTF match on eternal_valley, then berlin_wall; run
  `ps3-bench-20261024T233328Z-session-sauerbraten-edition-base`): 54 fps median, 14.7 s of stutter a
  minute, a segfault after 202 s of play. Occlusion-culled geometry popped (the player's "a bit
  glitchy").
- Session of 2026-10-25 (bots, about 6 minutes, Mesa ps3.819 with 0046, sauerbraten 0.819; run
  `ps3-bench-20261025T013202Z-session-sauerbraten-edition-base`): no query crash; a different one at
  400 s.

## Evidence and cause: the first crash (Mesa 0046)

`nip` resolves (stable-debug's `mesa-libgallium-dbgsym` ps3.721) to `nouveau_heap_free`
(nouveau_heap.c:106, `r->in_use = 0`), called from `nv30_query_object_del` (nv30_query.c:76). The heap
node it wrote was `nouveau_mm_free_work`'s function descriptor in libgallium's read-only `.opd`: the
query object had been freed and its memory reused for a fence work. nv30 has 124 report slots; the
engine ends up to 2048 occlusion queries a frame and reads them a frame later, so slots were taken
back from queries that still pointed at them: the query's next result check read freed memory and
freed it again. The same bug gave queries each other's results, which shows as geometry popping.

Not the cause: the `out of VRAM for a 8100 KiB BO` bursts (1920x1080x4, 135 failures each) at 61.9,
121.7 and 181.5 s, each 1.5 s into one of the bench's `xwd` screenshots, while VRAM was full (cube2_client
180-210 MB, Xorg 27-35 MB); the last burst was 54 s before the crash. They are Xorg answering the
bench's screenshot of a full VRAM (below).

## Evidence and cause: the second crash (Mesa 0047, 0048)

dmesg: three `out of GART for a 4096 KiB BO` lines, then `segfault at 0` in libc (a doubleword store of
a memcpy). The lr is in libgallium: its distance from libc's base in the crash (-0x4105ec4) put on the
library layout of a fresh cube2_client on the PS3 lands at libgallium offset 0x60a13c, which ps3.819's
symbols resolve to the memcpy in `st_setup_current()` (st_atom_array.cpp:343), inlined into
`st_update_array_templ`. That copies the current (zero-stride) vertex attributes into the stream
uploader without checking that `u_upload_alloc()` got anything. The uploader's next 1 MiB buffer is a
chunk of a 4 MiB nouveau_mm GART slab, which no longer fit: cube2_client held 186-194 MB of VRAM and
46-51 MB of the 63 MB GART (its buffers that found no VRAM went to GART), Xorg 7 MB. The same
signature (`out of GART for a 4096 KiB BO`, then a NULL write in libc) is BananaBread's crash in
docs/games/LOG-FINDINGS.md item 2.

The `out of VRAM for a 8100 KiB BO` bursts came again 1.4-1.5 s after the `xwd` shots at 300.08 and
360.09 s. Each burst is 135 failures (10 printed, 125 suppressed), and 135 = 1080 / 8: the X server's
GetImage answers in bands of 8 lines of 7680 bytes (64 KiB), and each band asks for a screen-sized
buffer (most likely the readback copy of the GL frontend in Xorg's glamor), which fails and falls back.
They come from Xorg, only while the bench takes a screenshot of a full VRAM, and are not the game's.

## Fix

- Mesa 0046: a query whose report slot is taken back keeps its result (host test
  `packages/patched/graphics/mesa/tests/query-steal-test.c`, `tools/run-tests.sh --only mesa-query-steal`).
  Commit cf855c2, 2026-10-25.
- Mesa 0047: an upload buffer that cannot be had is not written through (`u_upload_alloc()` also leaked
  the previous buffer at every failure and could hand a stale one back; nv30 reads an unbound
  zero-stride attribute as zero). Mesa 0048: nouveau buffer allocation failures fail the call (host test
  `alloc-fail-test.c`). Commit 1462313, 2026-10-25.
- The VRAM is full because the release's 512-pixel world textures stay plain at the engine's
  `texcompress 1024` (772 files, 76% of the texture bytes); Walnut's first-run default became
  `texcompress 512`, which the 2026-10-25 session didn't get because the player's
  `~/.sauerbraten/config.cfg` already existed (it stays the player's).
