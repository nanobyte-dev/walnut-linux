# Luanti

Luanti 5.15.2 with Minetest Game runs on our own build, since Debian builds none for ppc64. It flies
the bench path at 45.5 steady fps with 1.5-2.1 s of gameplay stalls (row 67, clean runs) and keeps
only 13-21 MB of RAM free: it is the suite's most memory-hungry engine on the CPU side. None of the
memory variants beat the default on clean runs; `view40` (59 fps with closer fog) is the one worth
offering. Open items: [backlog/games.md](../backlog/games.md).

## Packages and patches

- `luanti`, `luanti-data`, `luanti-server` (`5.15.2+dfsg-2+ps3.N`, package directory `gamedb/games/luanti/packages/luanti/`):
  Debian's source with ppc64 added to the Architecture of `luanti` and `luanti-server` (Debian's
  control lists only LuaJIT architectures). LuaJIT is kept. `luanti-data` is ours too, because
  `luanti` depends on its exact version; `luanti-game-minetest` is Debian's. `CHECK=1` in
  package.conf runs `--run-unittests` in the build (under qemu: a failure may be qemu's). The 330
  unit tests pass. The release candidate is ps3.405.
- `libluajit-5.1-2`, `luajit` (`2.1.0+openresty20251030-1+ps3.N`, `packages/patched/libs/luajit/`): 0001 makes ppc64
  big-endian allocate in the low 2 GB where LuaJIT checks the state. Debian's allocated between 2
  and 4 GB, which it then refused: `luaL_newstate()` failed about 40% of the time (331c539).

| Patch | What | Switch |
|---|---|---|
| 0002 | a unit test: Address::Resolve's fallback of the same family | |
| 0003 | Ogg Vorbis decoded to host byte order on big-endian: the sounds were noise (7b5b968) | |
| 0004 | Irrlicht's OpenGL driver: fewer GL calls per draw | `LU_GL` (`LU_GL_BIND`, `_FOG`, `_PTR`, `_UPLOAD`, `_XFORM`) |
| 0005 | SkinnedMesh skinned again only when the pose changed | `LU_GL_SKIN`, `LU_GL_SKIN_EPS` |
| 0006 | the sky quads in one draw; equal materials set once | `LU_GL_SKY`, `LU_GL_MATREUSE` |
| 0007 | the `malloc_trim` threshold in MiB (Luanti trims only after 256 MiB of blocks and meshes were freed, which a 217 MB machine rarely reaches) | `LU_TRIM_MB` (256 when unset) |

Luanti is not in `ps3-games`' Depends: it deploys by name.

## The recipe

`bench/recipes/luanti.sh`: a server-side mod can't count rendered frames, so the world mod
`ps3bench` (`bench/assets/luanti-ps3bench`) only makes every run the same. It holds the player
at the start for 15 s while the first blocks are generated and meshed, logs `ps3bench: path start`,
flies 45 s (240 nodes north looking down, then a full turn) four times (`ps3bench_loops`, "path loop
K"), logs `ps3bench: path end` and shuts the game down. fps comes from the Gallium HUD. `WARMUP=45`
from "path start" (the hold and the first loop don't count), `MEASURE=135` (loops 2-4, the same
terrain flown again). The world is made new every run from `fixed_map_seed = 20261017` (mapgen v7),
so each run generates the same terrain, and world and config live in `/var/tmp/ps3-bench/luanti`
(the player's `~/.minetest` is untouched). Settings: `video_driver = opengl`, 1920x1080 fullscreen,
`viewing_range = 60` (`LU_VIEW`), no vsync, `fps_max = 1000`, creative, no damage. The mod logs the
process's memory every 5 s into `mem_*` keys. The PS3 already runs one emerge and one mesh thread.

Variants, one lever each: `saved` (the previous map), `nopp` (no post-processing), `mip`, `chunk3`
(`chunksize = 3`), `srvunload` (`server_unload_unused_data_timeout = 8`), `active2`
(`active_block_range = 2`), `gen3`, `io` (`sqlite_synchronous = 1`, saves every 15 s), `cliunload`
(`client_unload_unused_data_timeout = 15`, `client_mapblock_limit = 800`), `leaves`, `tradeoff`,
`flatlight`, `nominimap`, `view40`, `view90`, `nomerge`, `merge100`, `meshdelay`, `meshchunk2`,
`noraycull`, `malloc`, `trim16`, `trim64`; the bundles `lean` (nopp, chunk3, srvunload, active2,
cliunload, io, malloc) and `leaner`; the batching switches `allbatchoff`, `nodupup`, `nobind`,
`noxform`, `noptr`, `nofog`, `noskin`, `nosky`, `nomatreuse`; the OpenAL Soft variants and `nosound`.

## Current results

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Row |
|---|---|---|---|---|
| base (x2, clean) | 45.5 (45-46) | 1.8 s (1.5, 2.1) / 18.4 / 185 ms (167, 203) | 17 / 62 MB | 67 (kernel .433, kicker) |
| base, USB root | 46 | 2.5 s / 17.5 / 358 ms | 17 / 63 MB | 68 (no kicker) |
| base, SSD root (x3) | 46 (44-46) | 3.5 s (2.4-5.8) / 19.1 / 724 ms | 19 / 63 MB | 71 (kernel .472, Mesa .476) |
| `view40` | 59 | 0.9 s / 29.2 / 362 ms | 19 / 55 MB | 63 (one run, clean) |

Picture right (the user; the webcam photos at 60 s and 150 s fall in the path's turn over the sea
and show a plain cyan view, which is the flight, not a missing world). Sound plays with no underruns;
it was not judged by ear yet (PLAN 2.1), and the webcam mic hears only the room.
With the root on the SSD the load is 25.0 s (24.5-25.1; 26.1 s from exec to the end of the loading
screens) against 40.8 s on the USB root: the main thread's disk wait went from 9.8 s to 3.4 s, and
what is left is the mapgen: the 15 s hold, Emerge-0's 8.6 s of CPU, the main thread 15.4 s of CPU and
2.8 s waiting for the GPU. The window's stalls did not improve (3.5 s, longest 724 ms, against 2.5 s
and 358 ms on USB; direct reclaim 1.6 s against 0.4 s, n=3, and the USB runs were an older Mesa and
kernel): the cost is CPU and reclaim, not the disk. On USB the load had been 43.1 s with 16.5 s of
disk wait, about 13 s of it major page faults on mapped files, and the most major faults (1420) and
small reads (6540) of the five profiled games ([PROFILING.md, "Start-up profiles, batch 17"](../PROFILING.md#start-up-profiles-batch-17),
[the SSD root](../PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)).
Gameplay CPU: the game 47.2% of the samples (the MeshUpdate thread alone 20.7% of all, Emerge-0
6.6%), Mesa 14.6%, libc 13.7%, kernel 13.6%, zstd 1.2%; no single driver path stands out
([HOTPATHS.md](../HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).

Memory ([MEMORY.md](../MEMORY.md)): 102 MB Rss plus 47.1 MB swapped (28.5 MB of it heap), malloc
96.8 MB used and 22.9 MB free across **14 arenas** (glibc gives each thread that mallocs under
contention its own, up to 16 on the PPE). Blocks and meshes unloaded during the flight are freed
into the arenas and stay there: free grows 12.8 to 22.9 to 28.2 MB over three minutes. The VRAM swap
had 88 MB in VRAM and 54 MB on the disk at the middle snapshot.

## What was tried

| Change | Result | Row | Verdict |
|---|---|---|---|
| GL batching (0004-0006) against `allbatchoff` | steady 45.0 / 46.0, stall_s 6.25 / 9.1 s, longest 1.7 / 3.4 s | 54 | kept on (one allbatchoff run had 3 digs in its warm-up) |
| `trim16` (`LU_TRIM_MB=16`) | no gain, more stalls (3.35 to 8.6 s) | 57 | rejected |
| `vm.watermark_boost_factor=0` | stall_play_s 12.5 / 0.8 / 2.0 / 2.1 (15000/0/15000/0; the first an outlier) | 62 | no clear gain, not shipped |
| `cliunload` | worse: 3.8 and 5.2 s of stalls, 2 s hitches when unloaded blocks come back | 67 | rejected |
| `lean` | 56-57 fps, 0.4 and 0.9 s of stalls, but a **black void** on the webcam at 150 s in both runs (no sky, no terrain) | 67 | not a win: renders nothing |
| `chunk3` alone | renders the black void at 150 s: `lean`'s culprit | 68 | rejected |
| `srvunload`, `active2` | render, but stall more (6.3 and 6.7 s against about 2.5) | 68 | rejected |
| `view40` | 59 fps, closer fog, 0.9 s of stalls | 63 | the one variant to offer (PLAN 2.9) |
| `nomerge` | 2.5 s of stalls | 63 | no gain |
| Game prefetch | load +1-4 s, two runs with 3 s hitches | 68 | worse, default off |

The earlier "cliunload 6.5 to 0.8 s" and "lean renders less terrain" (row 63) came from runs somebody
played in: the interference retro-scan found digs and clicks during base, lean and tradeoff, so those
cells are invalid ([STALLS.md, "Interference"](../STALLS.md#interference)). The clean re-runs of row 67 replaced
them and base stayed the package default.

**Built, not measured yet:** Mesa 0038 `NV30_FORCE_DXT=opaque` (and `+NV30_FORCE_DXT_NEAREST=1`),
which should show the most here since Luanti uploads everything plain (TESTS.md "Next").

Next: a menu option for `view40`, a fix for the bench mod's memory lines (it reads
`/proc/self/status` wrongly, so `vmrss`, `rssanon`, `rssfile` and `vmswap` never come out), SPU
meshing; see [backlog/games.md](../backlog/games.md).

## History

- 2026-10-16 (research): no ppc64 binary in Debian; s390x builds 5.15.2 and passes the unit tests;
  a 2025 ppc64 attempt (5.10.0) aborted in TestSAO.
- 2026-10-17: first run, 67 fps HUD median in flight but p10 23 and stalls to 0 while chunks load;
  177 MB swapped out, 32 MB free. 20 memory variants built.
- 2026-10-18: batching (row 54), trim16 (57), the interfered round (63) and its clean repeat (67),
  the lean parts (68).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md),
[archive/MEMORY.md](../archive/MEMORY.md).
