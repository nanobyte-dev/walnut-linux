+++
id = "malloc-trim"
games = ["luanti"]
driver_patches = []
game_patches = ["luanti/0007"]
sessions = []
batches = ["2026-10-18-mesa-407-lut", "2026-10-18-luanti-memory", "2026-10-18-luanti-clean-reruns", "2026-10-18-batch16"]
+++

# Freed map blocks kept in malloc's arenas (2026-10-18)

Moved on 2026-10-26 from docs/MEMORY.md ("Growth over the window", "Malloc arenas", "Findings in the
games' code" and the Luanti rows of "Cuts, ranked"). The malloc presets and ps3-malloctrim.so, which
any game can use, stay in [docs/MEMORY.md](../../../../docs/MEMORY.md#the-malloc-presets-and-ps3-malloctrim).

## Symptom

In the MEM=1 batch of 2026-10-18 (00:40-01:40 UTC, Mesa ps3.299, one flight of three minutes) Luanti
was the biggest game: Rss 102 MB + 47.1 MB swapped, heap 74.5 MB resident (28.5 swapped), and 22.9 MB
free inside malloc's arenas at the middle snapshot, in 14 arenas. Of the swap, 28.5 MB were heap, 11.0
anonymous, 3.6 libraries, 2.7 stacks (16 threads: 1.2 MB + 2.7 swapped), 1.2 Mesa.

## Evidence

- Malloc's used memory falls 102.2 -> 96.8 -> 94.4 MB over the window while its free memory grows
  12.8 -> 22.9 -> 28.2 MB. Blocks and meshes unloaded are freed into the arenas and stay there (arena
  #4 holds 18.2-22.2 MB free of 31.2). The free part grows with play time.
- The arenas: #0 47.2 MB, #1 32.6, #4 31.2 (18.2 free), #5 3.1, #7 4.3, and nine at 0.1. glibc gives a
  thread that mallocs while another holds an arena its own arena, up to 8 x cores = 16 on the PPE's two
  threads. A free hole in one arena can't serve another thread, so fewer arenas mean less
  fragmentation, traded against lock contention between the renderer, server, emerge and mesh threads.
- The VRAM swap had 88 MB in VRAM and 54 MB on the disk at the middle snapshot.

## Cause

`porting::TriggerMemoryTrim()` (src/porting.cpp) calls `malloc_trim()` from the emerge and mesh
threads once 256 MiB of map blocks, voxel manipulators and meshes were freed (upstream's workaround for
exactly this fragmentation). On a 217 MB machine the count rarely gets there.

## What was tried

luanti 0007 `LU_TRIM_MB` makes the threshold a variable (MiB, 256 when unset; unit tests pass in the
build; the trims stay in Luanti's emerge and mesh threads); recipe variants `trim16` and `trim64`. The
cuts, from the 2026-10-18 batch:

| Cut | Saves (MB) | Result |
|---|---|---|
| `LU_TRIM_MB=16` or `64` (variants `trim16`, `trim64`), or `MALLOC=trim` | 17-24, growing with play time (16.4-19.7 in free chunks + 2.5-4.2 top) | `trim16`: no gain and more stalls (3.4 -> 8.6 s), rejected (table below). `trim64` and `MALLOC=trim` not run; a trim holds each arena's lock for its walk |
| `MALLOC=arena2` (or the recipe's `malloc` variant) | part of arena #4's 18-22 MB holes | not run: lock contention between 4 busy threads |
| settings variants (view range, client/server unload, active2, chunk3, lean) | heap here is 103 MB of map blocks and meshes | base stays: `cliunload` has more stalls in the clean re-runs (gamedb:batch/2026-10-18-luanti-clean-reruns), `lean` draws a black void, `chunk3` empties the world (gamedb:batch/2026-10-18-batch16), `view40` is to be repeated |

<!-- gamedb:batch-table 2026-10-18-mesa-407-lut games=luanti metrics=fps_steady,stall_play_s,ram_free root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `stall_play_s` (s) | `ram_free` (MiB) |
|---|---|---|---|---|
| [Luanti](../../../games/luanti/) | base@wkkick10 | 47 | 3.4 | 15.8 |
| [Luanti](../../../games/luanti/) | trim16@wkkick10 | 47.5 | 8.6 | 15.9 |

Numbers: gamedb:batch/2026-10-18-mesa-407-lut (Mesa ps3.407 NV30_TRANSLATE_LUT, yquake2 ps3.408 kiss4k, luanti ps3.405 trim16 (all with WAKER kick), 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

Still to run (same boot, a profiled round for the MB and three plain rounds for stalls and fps):

    MEM=1 ENVS="- MALLOC=trim MALLOC=mmap MALLOC=arena2 MALLOC=tune" /var/tmp/bench/harness/run.sh bench luanti base 1
    MEM=1 /var/tmp/bench/harness/run.sh bench luanti "base trim64" 1
    ENVS="- MALLOC=tune" /var/tmp/bench/harness/run.sh bench luanti base 3
    python3 /var/tmp/bench/analysis/report.py --mem /var/tmp/ps3-bench/<run dirs>

Read `mem_swap`, `mem_heap_rss`/`mem_heap_swap`, `mem_malloc_free`, `min_memavailable_kb` and the
`ps3-malloctrim:` lines. The Luanti bench mod's own memory lines are parsed wrongly (its `vmrss`,
`rssanon`, `rssfile` and `vmswap` never come out; still so in the runs of 2026-10-18).
