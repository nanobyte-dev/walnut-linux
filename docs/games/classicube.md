# ClassiCube

ClassiCube, a Minecraft Classic client, is ours from upstream source and was the first game to run
on the RSX. It flies its timedemo at 45.9 steady fps with no stalls to speak of (row 68), 48.1 with
`CC_GL_CHUNKS=1` (+4.7%). It is the one suite game that is GPU-bound now: our batching halved its CPU
but made the RSX the limit. Next: `chunks1` as the default, then tiling and Z-cull (PLAN 3.2;
[backlog/games.md](../backlog/games.md)).

## Packages and patches

`classicube` (the engine, `1.3.8~git20261014.aa47929-1+ps3.N`, package directory `gamedb/games/classicube/packages/classicube/`,
`TYPE=upstream` from a pinned tarball) and `classicube-data` (Mojang's texture pack, sounds and music,
the licensed suite only; `sounds.sh` fetches the sounds and music from Mojang by SHA-1, the
`build-containers/Dockerfile.games` image converts them). Game data lives per user in
`~/.local/share/classicube`. The engine is in `ps3-games`, the data in `ps3-games-licensed`. The
release candidate is ps3.374; the split of the bundled .152 into engine and data was checked with dpkg
in the chroot only ([RELEASE-CANDIDATES.md](../RELEASE-CANDIDATES.md)).

| Patch | What | Switch |
|---|---|---|
| 0001 | the OpenGL backend sets vertex pointers once per buffer, buffer binds and face culling only on change | `CC_GL=0` all off; `CC_GL_PTR=0`, `CC_GL_STATE=0`; `CC_GL_STATS=1` counts |
| 0002 | the map renderer draws a chunk part in one draw, neighbouring ranges merged | `CC_GL_CHUNKS=0` (upstream: a draw per face direction), `1` (the same faces, neighbouring ranges merged, about 770 draws a frame), `2` (default: all faces of a chunk part, about 290 draws, 75% more indices) |

On llvmpipe the bench path went from 5127 GL calls and 1107 draws a frame to 1524 and 294.

## The recipe

`bench/recipes/classicube.sh`: a timedemo through a plugin. `assets/classicube-bench.so`
(source in `bench/classicube-bench/`, `make classicube-bench`) flies the camera along
`assets/classicube-bench.path` over the saved map `assets/classicube-bench.cw`: 3000 frames, the
same ones every run, after a warm-up that builds every chunk, then prints the frame rate and the
slowest frame and closes the game. The plugin flies the path up to 6 times (`CC_BENCH_PASSES`); a
pass is about 60 s at 50 fps, the first is the warm-up, and the client stops the game once the
counted passes cover about 120 s. `load` is the seconds to the first measured frame. The plugin does
nothing without `CC_BENCH`, and the run removes it again.

Variants: `vsync`, `shots` (keyframe screenshots), `still` (the old 60 s at the spawn), the OpenAL
Soft variants, `nosound`, `kick100`, `autooff`, `idxoff` (Mesa switches), `gloff`, `glstats`,
`noptr`, `nostate`, `chunks0`, `chunks1`, `chunks2`, `view64`, `view32`.

## Current results

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Row |
|---|---|---|---|---|
| base (`CC_GL_CHUNKS=2`) | 45.9 | 0.1 s / 30.1 / 99 ms | 50 / 60 MB | 68 (kernel .433, no kicker) |
| `chunks1` | 48.1 | 0.0 s / 29.4 / 61 ms | 53 / 60 MB | 68 |
| base / `chunks1`, one run each | 48.4 / 50.9 | 0.0 s; 1% low 31.6 / 31.5 | 58 / 54 MB | 64 (kicker) |
| base, SSD root (x3, then x2 later in the boot) | 48.1, then 46.1 | 0.0 s / 30.7 / 65 ms | 53 / 60 MB | 71, 75 (kernel .472) |

Picture right, sound state RUNNING with no underruns (the webcam mic hears the room's hum, not the
music, in 14 of 18 clips: not judged by ear yet). The load is 7.6 s on the SSD root (7.5-8.2) against
13.9 s on the USB root, its disk wait 1.3 s against 3.4-4.8; nothing else in the run changed: no
stalls, longest frame 65 ms. In the warm-up the main thread waits 35.7 s on the GPU
([PROFILING.md, "Start-up profiles, batch 17"](../PROFILING.md#start-up-profiles-batch-17), [on the SSD root](../PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)).
The steady fps was 48.0-48.1 in the first five runs of the SSD boot and 46.1 in the five after 02:00Z
with the same configuration (the USB root measured 45.9-46.0): an unexplained step inside one boot,
so the SSD root is not credited with the +4%. `PS3RSX_SHADOW_FAST=1` against off: 46.11 and 46.11
(1% low 29.7 against 30.1), no effect (row 75).
Gameplay CPU: Mesa 41.2% of the samples, kernel 20.1% (soft-interrupt replay 4.4%), ps3rsx 7.1%
(`ps3rsx_shadow_burst` and `validate` 3.6%), the game 15.6%, OpenAL and ALSA 9.9% (including the
float-to-integer conversion in `snd_pcm_lfloat_convert_float_integer`, 1.9%), Xorg 7.6% of the game's
([HOTPATHS.md](../HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).
Memory: 41 MB Rss, nothing swapped, 6 malloc arenas (one of 1.7 MB).

## Why the batching measured slower

Same boot (row 61): base 47.3 fps at 83 s of CPU, `gloff` (upstream drawing) 49.4 fps at 161 s: -4%
at half the CPU, though the 1% low improved (31 against 27.6). Profiled again (row 63, steady 48.4
against 50.3), in CPU time per frame the batching cut every class: 8.5 ms against 17.1 for the whole
process, Mesa 3.62 against 9.86, ps3rsx and drm 0.72 against 2.26, the game's own code 1.33 against
1.62. With the batching the process is busy 8.5 ms of a 20.7 ms frame and waits the rest, mostly on
the swap's fence; without it, busy 17.1 of 19.9. So base is GPU-bound, and what costs the GPU is
`CC_GL_CHUNKS=2` itself: drawing every face of a chunk part also draws the face directions upstream
skips on the CPU, 545k indices a frame against 310k, all transformed by the RSX's vertex units before
it culls them. 0001's pointer and state savings (2672 calls a frame instead of 5127) cost the GPU
nothing. Hence `chunks1`: upstream's faces with merged ranges. It measured +5% (row 64, one run) and
+4.7% (row 68); the default changes after the x3 repeat (PLAN 2.6). Details:
[HOTPATHS.md, "ClassiCube's batching measured slower"](../HOTPATHS.md#classicubes-batching-measured-slower-gpu-bound).

## What else was tried

| Change | Result | Row | Verdict |
|---|---|---|---|
| Mesa 0034 `NV30_FENCE_BO_POOL=16` | 48.4 both ways | 58 | neutral, off |
| Mesa 0036 `NV30_STAGING_POOL=8` (the animated water and lava tiles' uploads were 8.6% of its CPU) | same fps, CPU -4% | 64 | neutral in fps |
| Mesa 0026 `kick100` (ps3.282) | 48.9 against 48.3, +9% CPU | 45 | no default |
| Mesa ps3.299 against ps3.282, same boot | 46.5 against 46.9 | 47, 48 | neutral |

**Built, not measured yet:** Mesa 0038 `NV30_FORCE_DXT=opaque` (and `+NV30_FORCE_DXT_NEAREST=1`):
ClassiCube uploads everything plain, so it should show the most (TESTS.md "Next").

## History

- 2026-10-16, stage 0: 50.5 HUD median, p10 44, 60 s on a saved map, 51 MB peak RSS, 61 MB VRAM.
  An earlier "60 fps with vsync" was measured with nothing on screen and is void.
- 2026-10-17: Mesa ps3.246 43.2 to 44.9; ps3.262 50.1 with an 11.6 s load; 46.5-46.9 on a later boot
  with the same builds, the reason we compare only within a boot. In the first profile (Mesa ps3.223,
  54 fps) Mesa took 51% of the CPU, ps3rsx 16%, the game 8%.
- 2026-10-18: the batching measured (rows 61, 63), the GPU-bound finding, `chunks1` (64, 68).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md),
[archive/HOTPATHS.md](../archive/HOTPATHS.md).
