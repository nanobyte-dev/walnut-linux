# Luanti: technical notes

Luanti 5.15.2 with LuaJIT and Minetest Game runs on our own build, since Debian builds none for ppc64.
At the package's viewing range of 120 it flies the bench path at about 38 steady fps with 1-2 s of
stalls in the 135 s window (since luanti 0010 and 0011, 2026-10-29: before them 20 fps and 30-36 s);
at the recipe's 60 at about 46 fps. It keeps only 11-15 MB of RAM free: the suite's most memory-hungry
engine on the CPU side. Its played session (2026-10-24, before 0010 and 0011) had a 35 fps median with
25 s of stutter a minute and frames up to 4.3 s, as new map blocks were generated and meshed.

## Packages and patches

- `luanti`, `luanti-data`, `luanti-server` (`5.15.2+dfsg-2+ps3.N`, [packages/luanti](packages/luanti/)):
  Debian's source with ppc64 added to the Architecture of `luanti` and `luanti-server` (Debian's control
  lists only LuaJIT architectures). `luanti-data` is ours too, because `luanti` depends on its exact
  version; `luanti-game-minetest` is Debian's. `CHECK=1` in package.conf runs `--run-unittests` in the
  build (under qemu: a failure may be qemu's); the 330 unit tests pass. The release candidate is ps3.405; the PS3 runs ps3.1124~dev (with 0010 and 0011, 2026-10-29).
  Luanti is in no suite: it deploys by name.
- `libluajit-5.1-2`, `luajit` (`2.1.0+openresty20251030-1+ps3.N`, packages/patched/libs/luajit/): 0001
  makes ppc64 big-endian allocate in the low 2 GB where LuaJIT checks the state
  ([the investigation](investigations/2026-10-17-luajit-state-allocation.md)). 0002 (2026-10-28) makes the
  interpreter's `string.lower`, `string.upper` and `string.reverse` reset their buffer with 64-bit
  stores: the 32-bit ones left the old contents in front of every result on big-endian ppc64, so the
  Settings menu built its page ids wrong and failed on every open
  ([the investigation](investigations/2026-10-28-settings-menu-string-lower.md)).

| Patch | What | Switch |
|---|---|---|
| [0002](packages/luanti/patches/0002-unittest-Address-Resolve-s-fallback-of-the-same-fami.patch) | a unit test: Address::Resolve's fallback of the same family | |
| [0003](packages/luanti/patches/0003-sound-decode-Ogg-Vorbis-to-host-byte-order-big-endia.patch) | Ogg Vorbis decoded to host byte order on big-endian: the sounds were noise | |
| [0004](packages/luanti/patches/0004-irr-OpenGL-driver-fewer-GL-calls-per-draw-LU_GL-swit.patch) | Irrlicht's OpenGL driver: fewer GL calls per draw | `LU_GL` (`LU_GL_BIND`, `_FOG`, `_PTR`, `_UPLOAD`, `_XFORM`) |
| [0005](packages/luanti/patches/0005-SkinnedMesh-skin-again-only-when-the-pose-changed-LU.patch) | SkinnedMesh skinned again only when the pose changed | `LU_GL_SKIN`, `LU_GL_SKIN_EPS` |
| [0006](packages/luanti/patches/0006-Sky-quads-in-one-draw-LU_GL_SKY-equal-materials-set-.patch) | the sky quads in one draw; equal materials set once | `LU_GL_SKY`, `LU_GL_MATREUSE` |
| [0007](packages/luanti/patches/0007-porting-malloc_trim-threshold-from-LU_TRIM_MB-defaul.patch) | the `malloc_trim` threshold in MiB (Luanti trims only after 256 MiB of blocks and meshes were freed, which a 217 MB machine rarely reaches) | `LU_TRIM_MB` (256 when unset) |
| [0008](packages/luanti/patches/0008-settings-fullscreen-by-default-Walnut-is-a-console-o.patch) | fullscreen by default, at the screen's mode (a fresh configuration started in a 1024x600 window) | the `fullscreen` setting |
| [0009](packages/luanti/patches/0009-settings-viewing-range-120-by-default-on-the-PS3-not-.patch) | a viewing range of 120 by default, not 190: at 190 the bench flight ran at 15 fps with 56 s of stalls in its 135 s window, at 120 at 20 fps with 36 s, at 60 at 46 fps with 4.5 s (before 0010 and 0011); 120 is the floor the user set (gamedb:batch/2026-10-29-luanti-view120) | the `viewing_range` setting |
| [0010](packages/luanti/patches/0010-threads-background-threads-at-nice-10-on-Linux-the-r.patch) | the threads that prepare later frames (MeshUpdate, Emerge-N, Server, MinimapUpdate, AsyncWorker-N) at nice 10, so the render thread runs whenever it is ready: at 120 it had run a fifth of the time in its stall frames ([the investigation](investigations/2026-10-29-view120-stalls.md)) | `LU_NICE` (10; 0 as upstream) |
| [0011](packages/luanti/patches/0011-settings-the-server-unloads-unused-map-blocks-after-.patch) | the built-in server unloads map blocks unused for 5 s, not 29: it held about 4000 of them at 120, half the heap, swapped out and back; the client keeps its own copies, so nothing visible changes | the `server_unload_unused_data_timeout` setting |

## How it performs

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Batch |
|---|---|---|---|---|
| base (x2, clean) | 45.5 (45-46) | 1.8 s (1.5, 2.1) / 18.4 / 185 ms | 17 / 62 MB | gamedb:batch/2026-10-18-luanti-clean-reruns |
| base, USB root | 46 | 2.5 s / 17.5 / 358 ms | 17 / 63 MB | gamedb:batch/2026-10-18-batch16 |
| base, SSD root (x3) | 46 (44-46) | 3.5 s (2.4-5.8) / 19.1 / 724 ms | 19 / 63 MB | gamedb:batch/2026-10-19-ssdroot1-baseline |
| base with force-DXT off, opaque, opaque+NEAREST (n=2 each) | 46.5 / 46.0 / 46.0 | 3.3 / 3.3 / 3.9 s | 61-62 MB | gamedb:batch/2026-10-20-ssdroot8-force-dxt |
| `view40` (one run) | 59 | 0.9 s / 29.2 / 362 ms | 19 / 55 MB | gamedb:batch/2026-10-18-luanti-memory |
| `view190`, Luanti's own default before 0009 (one run, against base 46 / 2.3 s the same boot) | 15 | 55.8 s / 3.0 / 642 ms | 11 / 93 MB | gamedb:batch/2026-10-28-luanti-view190 |
| `view120` before 0010 and 0011 (3 runs, one boot, 2026-10-29; the base run beside them, 60: 46 fps / 4.5 s / 16.8 / 500 ms) | 20 (19-20) | 36.2 s (35.7-41.0) / 4.0 (3.9-4.3) / 995-3825 ms | 11-13 MB free | gamedb:batch/2026-10-29-luanti-view120 |
| `view120` with 0010 only, against `v120nonice` (its off-switch) (3 runs each, one boot) | 30 against 20 | 10.7 s against 30.1 / 8.5 against 4.2 / 516 against 1218 ms | 11 MB free | gamedb:batch/2026-10-29-luanti-nice |
| **`view120` as built, 0010 and 0011 at 5 s, against `v120old` (both off)** (3 runs each, one boot, 2026-10-29) | **38 (38-39)** against 21 (19-21) | **1.8 s (1.3-2.4) / 16.7 / 210 ms** against 30.9 s (30.8-35.1) / 4.3 / 1409 ms | 12.6 MB free, 49-53 MB swapped at the end (97-105) / 72 MB VRAM (86) | gamedb:batch/2026-10-29-luanti-shipped |

- **View 120 is the default (the user, 2026-10-29: never below 120, lower graphics quality is not the
  fix), and its stalls had two causes** ([the investigation](investigations/2026-10-29-view120-stalls.md)).
  The render thread waited for the CPU: in its stall frames it ran 22% of the time while MeshUpdate,
  Emerge-0, Server and kswapd0 had both PPE threads (perf samples placed in the stall recorder's
  frames, `bench/analysis/stallperf.py`; the recorder itself said `cpu`). And memory: the built-in
  server kept about 4000 map blocks for 29 s after their last use, half the heap, and the game swapped
  80-105 MB. 0010 (threads at nice 10) took 120 from 20 to 30 fps and from 30 to 11 s of stalls; 0011
  (the server's timeout 5 s) from there to 38 fps and 1.8 s. A second mesh thread was worse (28 fps,
  12.8 s). The steady rate is now held by the render thread's own work: ~180 draw calls a frame and
  updateDrawList's 15-40 ms four times a second (todo.toml).
- **The flight path runs through a mountain** (found 2026-10-29): screenshots 15 s into a loop are black
  (the camera in rock), those in the first loop grey (blocks not generated yet), the 100 s one shows
  the world. The frames are drawn behind the black, a little faster; the A/B numbers hold, the pictures
  don't (todo.toml).
- Picture right. Sound plays with no underruns; it was not judged by ear in a bench run, and the webcam
  mic hears only the room.
- **The load** is 25.0 s on the SSD root against 40.8 s on USB: the main thread's disk wait went from
  9.8 s to 3.4 s, and what is left is the map generation (the 15 s hold, Emerge-0's 8.6 s of CPU, the
  main thread's 15.4 s of CPU and 2.8 s waiting for the GPU). The window's stalls did not improve on the
  SSD root (direct reclaim 1.6 s against 0.4 s): the cost is CPU and reclaim, not the disk
  ([PROFILING.md](../../../docs/PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)). Its CPU in
  the load, all threads (gamedb:batch/2026-10-19-ssdroot2-startup-profiles): the game 12.8 s (1.9 on the
  main thread; Emerge-0's map generation 8.6, MeshUpdate 2.9, Server 2.6), Mesa 2.2, Lua 1.0, linking 0.7,
  the C++ runtime 0.5, zstd 0.4, fonts 0.3; 28.9 s of CPU in a 26 s load, so the 15 s hold and the map
  generation decide it. Twice as fast, Mesa would take 1.1 s and the game 1.0 s off the main thread.
- **Gameplay CPU** (gamedb:batch/2026-10-19-ssdroot2-gameplay-profiles, 46 fps profiled): the game 47.2%
  of the samples, spread over threads (MeshUpdate 20.7% of all, the main thread 14.1% game code plus 13.3%
  Mesa, Emerge-0 6.6%), Mesa 14.6% in all, libc 13.7% (9.5% unsymbolised), kernel 13.6%, zstd 1.2%; no
  single driver path stands out
  ([HOTPATHS.md](../../../docs/HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).
- **Memory** ([MEMORY.md](../../../docs/MEMORY.md)): 102 MB Rss plus 47.1 MB swapped (28.5 MB of it heap),
  malloc 96.8 MB used and 22.9 MB free across 14 arenas (glibc gives each thread that mallocs under
  contention its own, up to 16 on the PPE). Blocks and meshes unloaded during the flight are freed into
  the arenas and stay there: free grows 12.8 to 22.9 to 28.2 MB over three minutes. The VRAM swap had
  88 MB in VRAM and 54 MB on the disk at the middle snapshot.
- Mesa 0038's force-DXT forces nothing here: every upload is a NULL `glTexImage2D` filled by
  sub-images (`skip_nodata` 516).

The benchmark ([recipe](benchmark/recipe.sh)): a server-side mod can't count rendered frames, so the
world mod `ps3bench` (`assets/luanti-ps3bench`) only makes every run the same. It holds the player at
the start for 15 s while the first blocks are generated and meshed, logs `ps3bench: path start`, flies
45 s (240 nodes north looking down, then a full turn) four times, logs `ps3bench: path end` and shuts
the game down; fps from the Gallium HUD, `WARMUP=45` from "path start", `MEASURE=135` (loops 2-4, the
same terrain flown again). The world is made new every run from `fixed_map_seed = 20261017` (mapgen v7)
in `/var/tmp/ps3-bench/luanti` (the player's `~/.minetest` untouched); `viewing_range = 60` (variant `view120` is the package default, 0009), no vsync,
`fps_max = 1000`, creative, no damage. The mod's memory lines (`vmrss`, `rssanon`, `rssfile`,
`vmswap`, every 5 s) become bench.txt's `mem_start_*`, `mem_end_*` and `mem_max_*`. Variant `prof`
(and `v120prof`) logs Luanti's own profiler every 5 s into `luanti-debug.txt.gz`.

## Known problems

- Starts in a window instead of fullscreen, and the options menu is broken (the played session). Both
  fixed on 2026-10-28: the menu by luajit 0002 (checked on the PS3 with the dev build: the Settings
  dialog opens from the main menu's gear), the window by luanti 0008; luanti ps3.1025 (0008, 0009) on an
  empty configuration starts fullscreen at 1920x1080 with the Settings dialog working (`run.sh smoke luanti
  freshmenu`, 2026-10-28). Irrlicht's "Could not create window and context!" ERROR line before that is its
  fallback from the GL profiles nv30 lacks (GLXBadFBConfig), not a failure.
- The stutter in play (the 2026-10-24 session, before 0010 and 0011): map generation and meshing on the
  PPE, with the RAM at its floor. Not played again yet (todo.toml).
- Log noise from minetest_game itself: `Maximum lag peaked`, `active block modifiers took Nms`, `Field
  "image" on TileDef is deprecated`.
- About one run in twenty played through OpenAL lost its sound to the sound card's DMA stopping, fixed
  by kernel local/0047 (gamedb:batch/2026-10-20-ssdroot12-dma-watchdog).

## What was tried

The memory variants: [2026-10-18 the memory variants](investigations/2026-10-18-memory-variants.md)
(`cliunload`, `lean`, `chunk3`, `srvunload`, `active2`, `view40`, the interfered round); `trim16`
([the malloc_trim investigation](investigations/2026-10-18-malloc-trim.md)); the GL batching 0004-0006
against `allbatchoff`: 45.0 against 46.0 steady, 6.25 against 9.1 s of stalls, kept on
(gamedb:batch/2026-10-17-luanti-neverball-batching); `vm.watermark_boost_factor=0`: 12.5 / 0.8 / 2.0 /
2.1 s of stalls in four runs (15000/0/15000/0), no clear gain (gamedb:batch/2026-10-18-smoke-reruns-watermark);
game prefetch: the load +1-4 s, worse (gamedb:batch/2026-10-18-batch16). At view 120 (2026-10-29): two
mesh threads (`v120mesh2`, with 0010) 28 fps and 12.8 s of stalls against 30 and 10.7 with one; the
server's timeout at 10 s 34 fps and 3.8 s, at 5 s 38 fps and 1.2 s, at 29 s 31 fps and 9.1-9.6 s
(gamedb:batch/2026-10-29-luanti-srvunload).

## Investigations

- [2026-10-17 LuaJIT's state allocation](investigations/2026-10-17-luajit-state-allocation.md).
- [2026-10-18 the memory variants](investigations/2026-10-18-memory-variants.md).
- [2026-10-18 malloc_trim](investigations/2026-10-18-malloc-trim.md).
- [the menu freeze](investigations/2026-10-18-menu-freeze.md).
- [2026-10-28 the Settings menu and string.lower](investigations/2026-10-28-settings-menu-string-lower.md).
- [2026-10-29 the stalls at viewing range 120](investigations/2026-10-29-view120-stalls.md).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": "Luanti (Minetest)" a stretch goal: has a GL 2.1 driver, but the Lua and mesh work is CPU-heavy on the PPE, and it's a heavier ClassiCube.
