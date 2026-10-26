# Luanti: technical notes

Luanti 5.15.2 with LuaJIT and Minetest Game runs on our own build, since Debian builds none for ppc64.
It flies the bench path at about 46 steady fps with 1.5-3.5 s of gameplay stalls and keeps only 13-21 MB
of RAM free: the suite's most memory-hungry engine on the CPU side. Its played session (2026-10-24) had a
35 fps median with 25 s of stutter a minute and frames up to 4.3 s, as new map blocks were generated and
meshed: that stutter is the real problem, more than the frame rate.

## Packages and patches

- `luanti`, `luanti-data`, `luanti-server` (`5.15.2+dfsg-2+ps3.N`, [packages/luanti](packages/luanti/)):
  Debian's source with ppc64 added to the Architecture of `luanti` and `luanti-server` (Debian's control
  lists only LuaJIT architectures). `luanti-data` is ours too, because `luanti` depends on its exact
  version; `luanti-game-minetest` is Debian's. `CHECK=1` in package.conf runs `--run-unittests` in the
  build (under qemu: a failure may be qemu's); the 330 unit tests pass. The release candidate is ps3.405.
  Luanti is in no suite: it deploys by name.
- `libluajit-5.1-2`, `luajit` (`2.1.0+openresty20251030-1+ps3.N`, packages/patched/libs/luajit/): 0001
  makes ppc64 big-endian allocate in the low 2 GB where LuaJIT checks the state
  ([the investigation](investigations/2026-10-17-luajit-state-allocation.md)).

| Patch | What | Switch |
|---|---|---|
| [0002](packages/luanti/patches/0002-unittest-Address-Resolve-s-fallback-of-the-same-fami.patch) | a unit test: Address::Resolve's fallback of the same family | |
| [0003](packages/luanti/patches/0003-sound-decode-Ogg-Vorbis-to-host-byte-order-big-endia.patch) | Ogg Vorbis decoded to host byte order on big-endian: the sounds were noise | |
| [0004](packages/luanti/patches/0004-irr-OpenGL-driver-fewer-GL-calls-per-draw-LU_GL-swit.patch) | Irrlicht's OpenGL driver: fewer GL calls per draw | `LU_GL` (`LU_GL_BIND`, `_FOG`, `_PTR`, `_UPLOAD`, `_XFORM`) |
| [0005](packages/luanti/patches/0005-SkinnedMesh-skin-again-only-when-the-pose-changed-LU.patch) | SkinnedMesh skinned again only when the pose changed | `LU_GL_SKIN`, `LU_GL_SKIN_EPS` |
| [0006](packages/luanti/patches/0006-Sky-quads-in-one-draw-LU_GL_SKY-equal-materials-set-.patch) | the sky quads in one draw; equal materials set once | `LU_GL_SKY`, `LU_GL_MATREUSE` |
| [0007](packages/luanti/patches/0007-porting-malloc_trim-threshold-from-LU_TRIM_MB-defaul.patch) | the `malloc_trim` threshold in MiB (Luanti trims only after 256 MiB of blocks and meshes were freed, which a 217 MB machine rarely reaches) | `LU_TRIM_MB` (256 when unset) |

## How it performs

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Batch |
|---|---|---|---|---|
| base (x2, clean) | 45.5 (45-46) | 1.8 s (1.5, 2.1) / 18.4 / 185 ms | 17 / 62 MB | gamedb:batch/2026-10-18-luanti-clean-reruns |
| base, USB root | 46 | 2.5 s / 17.5 / 358 ms | 17 / 63 MB | gamedb:batch/2026-10-18-batch16 |
| base, SSD root (x3) | 46 (44-46) | 3.5 s (2.4-5.8) / 19.1 / 724 ms | 19 / 63 MB | gamedb:batch/2026-10-19-ssdroot1-baseline |
| base with force-DXT off, opaque, opaque+NEAREST (n=2 each) | 46.5 / 46.0 / 46.0 | 3.3 / 3.3 / 3.9 s | 61-62 MB | gamedb:batch/2026-10-20-ssdroot8-force-dxt |
| `view40` (one run) | 59 | 0.9 s / 29.2 / 362 ms | 19 / 55 MB | gamedb:batch/2026-10-18-luanti-memory |

- Picture right. Sound plays with no underruns; it was not judged by ear in a bench run, and the webcam
  mic hears only the room.
- **The load** is 25.0 s on the SSD root against 40.8 s on USB: the main thread's disk wait went from
  9.8 s to 3.4 s, and what is left is the map generation (the 15 s hold, Emerge-0's 8.6 s of CPU, the
  main thread's 15.4 s of CPU and 2.8 s waiting for the GPU). The window's stalls did not improve on the
  SSD root (direct reclaim 1.6 s against 0.4 s): the cost is CPU and reclaim, not the disk
  ([PROFILING.md](../../../docs/PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)).
- **Gameplay CPU**: the game 47.2% of the samples (the MeshUpdate thread alone 20.7% of all, Emerge-0
  6.6%), Mesa 14.6%, libc 13.7%, kernel 13.6%, zstd 1.2%; no single driver path stands out
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
in `/var/tmp/ps3-bench/luanti` (the player's `~/.minetest` untouched); `viewing_range = 60`, no vsync,
`fps_max = 1000`, creative, no damage. The mod's memory lines (`vmrss`, `rssanon`, `rssfile`,
`vmswap`) never come out: it reads `/proc/self/status` wrongly.

## Known problems

- Starts in a window instead of fullscreen, and the options menu is broken (the played session); a
  package fix is in the game's todo.
- The stutter in play: map generation and meshing on the PPE, with the RAM at its floor.
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
game prefetch: the load +1-4 s, worse (gamedb:batch/2026-10-18-batch16).

## Investigations

- [2026-10-17 LuaJIT's state allocation](investigations/2026-10-17-luajit-state-allocation.md).
- [2026-10-18 the memory variants](investigations/2026-10-18-memory-variants.md).
- [2026-10-18 malloc_trim](investigations/2026-10-18-malloc-trim.md).
- [the menu freeze](investigations/2026-10-18-menu-freeze.md).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": "Luanti (Minetest)" a stretch goal: has a GL 2.1 driver, but the Lua and mesh work is CPU-heavy on the PPE, and it's a heavier ClassiCube.
