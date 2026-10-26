# ClassiCube: technical notes

## Status

A draft: in the benchmark suite since 2026-10-16 and in `ps3-games`, not published, no played session yet.
ClassiCube, a Minecraft Classic client, is ours from upstream source and was the first game to run on the
RSX. It flies its timedemo at 46-48 steady fps with no stalls to speak of; it is the one suite game that is
GPU-bound now: our batching halved its CPU but made the RSX the limit, so `CC_GL_CHUNKS=1` (+4.7%) is the
next default ([the investigation](investigations/2026-10-18-batching-gpu-bound.md)).

## Packages and patches

`classicube` (the engine, `1.3.8~git20261014.aa47929-1+ps3.N`, [packages/classicube](packages/classicube/),
`TYPE=upstream` from a pinned tarball; paths left out of it in its `dfsg-exclude.txt`) and `classicube-data`
(Mojang's texture pack, sounds and music, the licensed suite only, `licensed/classicube-data`:
`sounds.sh` fetches the sounds and music from Mojang by SHA-1, the `build-containers/Dockerfile.games` image
converts them). Game data lives per user in `~/.local/share/classicube`. The engine is in `ps3-games`, the
data in `ps3-games-licensed`. The release candidate is ps3.374.

| Patch | What | Switch |
|---|---|---|
| [0001](packages/classicube/patches/0001-OpenGL-backend-CC_GL-switches-vertex-pointers-set-on.patch) | the OpenGL backend sets vertex pointers once per buffer, buffer binds and face culling only on change | `CC_GL=0` all off; `CC_GL_PTR=0`, `CC_GL_STATE=0`; `CC_GL_STATS=1` counts |
| [0002](packages/classicube/patches/0002-map-renderer-a-chunk-part-in-one-draw-neighbouring-r.patch) | the map renderer draws a chunk part in one draw, neighbouring ranges merged | `CC_GL_CHUNKS=0` (upstream: a draw per face direction), `1` (the same faces, neighbouring ranges merged, about 770 draws a frame), `2` (default: all faces of a chunk part, about 290 draws, 75% more indices) |
| [0003](packages/classicube/patches/0003-SDL3-window-fullscreen-by-default-option-win-fullscr.patch) | the SDL3 window fullscreen at the desktop's resolution by default (option `win-fullscreen`); this replaced an Openbox window rule in the desktop package | |

On llvmpipe the bench path went from 5127 GL calls and 1107 draws a frame to 1524 and 294.

### The data (`classicube-data`)

The `classicube` source package built both binaries until 2026-10-17 (`+ps3.366`); since then
`classicube-data` (all, Section `local/games`, so the licensed suite) is a source package of its own in
`licensed/classicube-data/` (`upstream.mk`; its `orig.mk` makes the tarball from the texture pack and
`sounds.sh`'s audio). It holds two things, both Mojang's:

- the texture pack, `static.classicube.net/default.zip`: the files ClassiCube's launcher otherwise extracts
  from Mojang's Minecraft Classic 0.30 and 1.6.2 client jars (`terrain.png`, `char.png`, the mob skins; the
  "classic jar files" in the pinned `src/Resources.c`) with ClassiCube's patches;
- the sounds and music from Mojang's resource server
  ([packages/classicube/sounds.sh](packages/classicube/sounds.sh)).

Paths are `/usr/lib/classicube/texpacks` and `audio/`. `classicube` only `Suggests` `classicube-data` (a
free package never depends on licensed data); `classicube-data` has `Breaks`/`Replaces: classicube (<<
1.3.8~git20261014.aa47929-1+ps3.366~)` for the upgrade from the bundled package, which dpkg handled in
either unpack order (checked 2026-10-17). Each new `classicube-data` (15 MB) goes to the licensed suite and
is kept there.

Without the data the engine still works: `/usr/games/classicube` runs from `~/.local/share/classicube`, and
the launcher (`Resources_CheckExistence` in the pinned source) finds `texpacks/classicube.zip` missing and
offers to fetch it and the audio (BearSSL is built in), writing only under that directory; our patches
leave this alone. The steps for a user, or for copying the files by hand, are in
`/usr/share/doc/classicube/README.Debian`
([debian/classicube.README.Debian](packages/classicube/debian/classicube.README.Debian)).

## How it performs

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Batch |
|---|---|---|---|---|
| base (`CC_GL_CHUNKS=2`) | 45.9 | 0.1 s / 30.1 / 99 ms | 50 / 60 MB | gamedb:batch/2026-10-18-batch16 |
| `chunks1` | 48.1 | 0.0 s / 29.4 / 61 ms | 53 / 60 MB | gamedb:batch/2026-10-18-batch16 |
| base, SSD root (x3, then x2 later in the boot) | 48.1, then 46.1 | 0.0 s / 30.7 / 65 ms | 53 / 60 MB | gamedb:batch/2026-10-19-ssdroot1-baseline, gamedb:batch/2026-10-19-ssdroot2-shadow-fast |

- Picture right, the sound state RUNNING with no underruns (the webcam mic hears the room's hum, not the
  music, in 14 of 18 clips: not judged by ear yet).
- The load is 7.6 s on the SSD root against 13.9 s on the USB root; nothing else changed. The steady fps was
  48.0-48.1 in the first five runs of the SSD boot and 46.1 in the five after 02:00Z with the same
  configuration: an unexplained step inside one boot, the reason a change is compared only within a boot.
  Its CPU in the load, all threads (gamedb:batch/2026-10-19-ssdroot2-startup-profiles): the game 1.8 s (1.5
  on the main thread), linking 0.65, Mesa 0.63, ps3rsx 0.25; 4.9 s in all. The one lever there is the
  game's own load path (0.8 s of the main thread), then linking (0.3 s)
  ([PROFILING.md](../../../docs/PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)).
- Gameplay CPU (gamedb:batch/2026-10-19-ssdroot2-gameplay-profiles): Mesa 41.2%, the kernel 20.1% (a
  quarter of it soft-interrupt replay; 4.5% of all samples under page-table flushes from unmapping, 1.7% in
  `ps3rsx_gart_clear`), the game 15.6%, ps3rsx 7.1%, OpenAL and ALSA 9.9% (1.9% in
  `snd_pcm_lfloat_convert_float_integer`: the ALSA plug converting OpenAL's float output), and Xorg spends
  7.6% of the game's CPU besides
  ([HOTPATHS.md](../../../docs/HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)). Memory: 41
  MB Rss, nothing swapped, 6 malloc arenas.
- Neutral: Mesa 0034 `NV30_FENCE_BO_POOL=16` (48.4 both), 0036 `NV30_STAGING_POOL=8` (same fps, CPU -4%),
  0040 (46.25 both, gamedb:batch/2026-10-19-ssdroot5-staging-pool), `kick100` (+1%, +9% CPU), ps3rsx
  `shadow_fast` (46.11 both), our openal-soft (46.2, GPU-bound), force-DXT (nothing forced: 214 textures
  under 64 px, gamedb:batch/2026-10-20-ssdroot8-force-dxt).

The benchmark ([recipe](benchmark/recipe.sh)) is a timedemo through a plugin: `assets/classicube-bench.so`
(source in [assets-src](benchmark/assets-src/), `make classicube-bench`) flies the camera along
`assets/classicube-bench.path` over the saved map `assets/classicube-bench.cw`: 3000 frames, the same ones
every run, after a warm-up that builds every chunk, up to 6 passes (`CC_BENCH_PASSES`). The plugin does
nothing without `CC_BENCH`, and the run removes it again. The first stage-0 run (2026-10-16) gave a 50.5 HUD
median.

## Known problems

- About one OpenAL run in ten lost its sound to the sound card's DMA stopping (the wedge), fixed by kernel
  local/0047 (gamedb:batch/2026-10-20-ssdroot12-dma-watchdog).

## Investigations

- [2026-10-18 the batching measured slower: GPU-bound](investigations/2026-10-18-batching-gpu-bound.md).
