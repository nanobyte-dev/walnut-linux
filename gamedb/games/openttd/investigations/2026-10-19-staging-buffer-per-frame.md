+++
id = "staging-buffer-per-frame"
games = ["openttd"]
driver_patches = ["mesa/0040"]
game_patches = []
sessions = []
batches = ["2026-10-19-ssdroot2-gameplay-profiles", "2026-10-19-ssdroot4-staging-pool", "2026-10-19-ssdroot5-openttd-views"]
+++

# An 8 MB staging buffer every frame (2026-10-19)

Moved on 2026-10-26 from docs/HOTPATHS.md ("OpenTTD: an 8 MB staging buffer every frame" and the
OpenTTD rows of the SSD-root profiles). The driver side, what a soft-masked sample is and the three
switches built against it (Mesa 0040 `NV30_STAGING_POOL_MB`, kernel local/0046
`ps3_htab.insert_readback`, ps3rsx `clear_dcbz`), stays in
[docs/HOTPATHS.md](../../../../docs/HOTPATHS.md#soft-masked-time-and-the-staging-buffer-per-frame).

## Symptom

The gameplay profile of batch ssdroot2 P1b (2026-10-19, kernel .472, Mesa ps3.476, OpenTTD's OpenGL
driver, 18 fps; the webcam showed the title screen with its menu open, so it is the title screen's
redraw and music, not play) spent 49.5% of OpenTTD's samples in the kernel, 37.6% of them booked as
`__replay_soft_interrupts` (time with interrupts soft-masked), against 1.7-5.7% in the six other games
profiled. Mesa's own share was 1.1%, but its GL API entry hot path was 33.2% (`_mesa_memcpy_texture`, the
CPU copy of its PBO upload) and buffer allocation and free 17.0%.

## Evidence

`bench/irqoff.py` splits the soft-masked samples by where interrupts came back on: hash faults on
return to user mode 27.1 points (26.7 of them under one libc memcpy, against 0.1 under the blitter,
which runs 8.7% of the time), `ps3_hpte_invalidate`'s unlock at munmap 8.4, the rest 2.1. The zeroing
(`memset` under `ps3rsx_gart_clear` < `ps3rsx_bo_new`) was another 5.3%. FluidSynth 10.2% and libspeq
4.2% on the audio thread and `Blitter_40bppAnim::Draw` 8.5% were the rest of note.

## Cause

OpenTTD's OpenGL driver draws the screen on the CPU into two pixel buffers (`GL_PIXEL_UNPACK_BUFFER`,
the 8.3 MB video buffer and the 2.1 MB animation buffer), unmaps them and uploads the dirty rectangle
with `glTexSubImage2D` from the buffer (`OpenGLBackend::ReleaseVideoBuffer`). At the title screen the
dirty rectangle is the whole 1080p screen. nv30 has no texture buffer objects, so Mesa's state tracker
takes its fallback: it maps the PBO and the texture and copies on the CPU. nv30's texture map
(`nv30_miptree_transfer_map`) gets a staging buffer in GART for the box and the GPU copies it into the
texture after the unmap. Every frame, for each of the two:

1. `GEM_NEW` of 8.3 MB: ps3rsx zeroes it (`memset` under `ps3rsx_gart_clear`, 5.3%).
2. `mmap`: `remap_pfn_range` sets the Linux PTEs, but the hash table gets nothing yet.
3. Mesa's memcpy into it: a hash fault on every 4 KiB page, 2540 a frame. Each makes two lv1 calls,
   `lv1_insert_htab_entry` and `lv1_read_htab_entries` (only to learn whether the entry landed in
   the secondary group), interrupts soft-masked throughout: 27.1%.
4. Once the copy's fence signalled (the fence work, from a later GL call, here `UpdatePalette`):
   `munmap`, one `lv1_write_htab_entry` per page (8.4%), and `GEM_CLOSE`.
5. And because each buffer is over the 4 MB of staging Mesa 0007 lets a context have in flight,
   `nv30_staging_bo_new()` waited for the GPU before allocating it: the CPU and the GPU in lockstep.

From the shares, 18 fps and 2540 pages a frame, a hash fault costs about 6.6 us and an invalidation
about 2.1 us. Over 40% of the process's CPU went to buffers that live for one frame.

## Fix

Mesa 0040 (`NV30_STAGING_POOL_MB=M`, off by default): uploads over 256 KiB take an idle staging buffer
of their MiB class from a pool of buffers that are still mapped, so steps 1-5 go. Its host test
replays OpenTTD's two uploads a frame for 1000 frames (`tools/run-tests.sh mesa-staging-pool`: at most
4 buffers made, at least 1996 of the 2000 uploads reused). On the PS3 with `NV30_STAGING_POOL_MB=24`
(the 1962 savegame, the stall recorder on):

<!-- gamedb:batch-table 2026-10-19-ssdroot4-staging-pool games=openttd variants=blit8*,base@rec10* metrics=fps_steady,low1,stall_play_s,cpu root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `low1` (fps) | `stall_play_s` (s) | `cpu` (s) |
|---|---|---|---|---|---|
| [OpenTTD](../../../games/openttd/) | blit8@rec10 | 34 | 13 | 0.6 | 264.4 |
| [OpenTTD](../../../games/openttd/) | blit8@rec10+staging_pool_mb24 | 52 | 18.4 | 0.1 | 229.9 |
| [OpenTTD](../../../games/openttd/) | base@rec10 | 11 | 5.8 | 43.7 | 271.5 |
| [OpenTTD](../../../games/openttd/) | base@rec10+staging_pool_mb24 | 20 | 9.4 | 5.1 | 256.4 |

Numbers: gamedb:batch/2026-10-19-ssdroot4-staging-pool (batch ssdroot4 Mesa 0040: NV30_STAGING_POOL_MB=24 against off on OpenTTD base (32bpp-anim) and blit8 (8bpp-optimized) x3, the 1962 savegame (SSD root .472 Mesa ps3.544), 2026-10-19; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

<!-- gamedb:batch-table 2026-10-19-ssdroot5-openttd-views games=openttd metrics=fps_steady,low1,stall_play_s root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `low1` (fps) | `stall_play_s` (s) |
|---|---|---|---|---|
| [OpenTTD](../../../games/openttd/) | base@rec10+staging_pool_mb24 | 52 | 18.6 | 0.1 |
| [OpenTTD](../../../games/openttd/) | scroll@rec10+staging_pool_mb24 | 31 | 14.3 | 0.8 |
| [OpenTTD](../../../games/openttd/) | zoomout@rec10+staging_pool_mb24 | 23 | 11.1 | 1.5 |

Numbers: gamedb:batch/2026-10-19-ssdroot5-openttd-views (batch ssdroot5 OpenTTD views with Mesa 0040 on: base, zoomout, scroll x2 (SSD root .472 Mesa ps3.544, 8bpp-optimized), 2026-10-19; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

8bpp (`blit8`) 34 -> 52 fps (+53%, 1% low 13 -> 18, stalls 0.6 -> 0.1 s), 32bpp-anim (`base`) 11 -> 20
fps (stalls 43.7 -> 5.1 s); with 0040, zoomed out 7 -> 23 and scrolling 7 -> 31 fps (the views without
it are in batch ssdroot3's blitter table). Not tried yet: a GPU copy straight from the PBO into the
texture (no staging, no CPU copy; but the next frame's `glMapBuffer(GL_READ_WRITE)` would then wait for
that copy, so CPU and GPU would serialise again). Once in 13 runs since ps3.544 dmesg had `no memory to
back the GART` (docs/games/LOG-FINDINGS.md item 3), which a `blit8` x6 run is to look at
(docs/TESTS.md).
