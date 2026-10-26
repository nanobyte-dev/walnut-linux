+++
id = "prng-table"
games = ["yquake2"]
driver_patches = []
game_patches = ["yquake2/0002"]
sessions = []
batches = ["2026-10-18-mesa-407-lut"]
+++

# 32 MB in a random number generator (2026-10-18)

Moved on 2026-10-26 from docs/MEMORY.md ("Findings in the games' code", "Cuts, ranked" row 1 and the
packages table), where the cross-game memory picture stays.

## Symptom

The MEM=1 batch of 2026-10-18 (00:40-01:40 UTC, Mesa ps3.299, one run per game) had Yamagi Quake II at
77 MB of Rss + 3.4 MB swapped, with 42.1 MB of anonymous memory (the most of the ten games) and a
23 MB .bss in the client, 18 MB of it resident.

## Cause

`src/common/shared/rand.c` (the KISS PRNG, also in rogue/ and xatrix/) had
`static uint64_t QARY[0x200000]`, 16 MiB, and `randk_seed()` writes every entry. The file is linked
into the client (`quake2`, whose 23 MB .bss was 18 MB resident, 16 of it this table) and into
`game.so` (16 MB .bss, all resident): 32 MB of the game's 77. A lag of 4096 entries keeps a period far
beyond anything a game draws.

## Fix

yquake2 0002 `YQ2_KISS_QSIZE`: the PRNG table allocated at first use, a power of two from 64 to 2^21
entries, 4096 (32 KiB per module) when unset. The .bss is 7.7 MB in `quake2` (was 23.7) and 81 KiB in
`game.so` (was 16 MB). `YQ2_KISS_QSIZE=2097152` (recipe variant `kissbig`) gives the old size and
sequence (the same million draws, checked on the host), with the 16 MiB from calloc. Package
yamagi-quake2 8.70+dfsg-1+ps3.408 had the switch, .415 made 4096 the default. Client-side random
effects differ from the old table (the demo itself doesn't).

Measured on the PS3 (`kiss4k`, the recipe variant before it became the default):

<!-- gamedb:batch-table 2026-10-18-mesa-407-lut games=yquake2 metrics=fps_steady,stall_play_s,ram_free,vram root=../../.. -->
| Game | Variant | `fps_steady` (fps) | `stall_play_s` (s) | `ram_free` (MiB) | `vram` (MiB) |
|---|---|---|---|---|---|
| [Yamagi Quake II](../../../games/yquake2/) | base@wkkick10 | 65.7 | 22.6 | 12.2 | 48.2 |
| [Yamagi Quake II](../../../games/yquake2/) | kiss4k@wkkick10 | 68.7 | 0.1 | 46.4 | 48.2 |

Numbers: gamedb:batch/2026-10-18-mesa-407-lut (Mesa ps3.407 NV30_TRANSLATE_LUT, yquake2 ps3.408 kiss4k, luanti ps3.405 trim16 (all with WAKER kick), 2026-10-18; medians over the runs of each game and variant).
<!-- /gamedb:batch-table -->

Same boot, x2: RSS 125 -> 56 MB, swap used 80 -> 9 MB (about 70 MB less RAM), gameplay stalls 22.6 ->
0.05 s, longest frame 4.5 s -> 91 ms, steady 65.7 -> 68.7 fps, 1% low 20.1 -> 31.1. The two resident
copies had pushed the game's other pages to swap. The upstream offer (a smaller static table) is in
[upstream.md](../upstream.md).
