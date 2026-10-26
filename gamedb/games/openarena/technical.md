# OpenArena: technical notes

OpenArena 0.8.8 (Debian's data) runs on our [ioquake3](../../engines/ioquake3/technical.md) with both
of its renderers. The fixed-function `opengl1` renderer is the suite's baseline: 70.6 steady fps with
no gameplay stalls. The GLSL `opengl2` renderer, in its lean `gl2min` settings, reaches 56-60 fps with
0-2.4 s of gameplay stalls since ioquake3 0027 sends its vertex colours in a format nv30 fetches. In
played sessions the GL 2.0 launcher did better than GL 1.2 (54 against 43 fps median), so the game's
medal comes from GL 2.0.

## Packages and settings

- `ioquake3`, `ioquake3-server`: our engine build, replacing Debian's; the patches and their cvars are
  on the [engine's page](../../engines/ioquake3/technical.md). Debian's `openarena` and its data are
  used as they are.
- Two menu entries ([desktop/launchers](desktop/launchers/)): "OpenArena (GL 1.2)" starts `opengl1`,
  "OpenArena (GL 2.0)" starts `opengl2` with the `gl2min` set on its command line (no normal, specular
  or deluxe mapping, sun, dynamic lights, depth prepass, HDR, post-processing, tone mapping, auto
  exposure, SSAO, cube maps or framebuffer objects).
- The PS3 defaults, [autoexec.cfg](desktop/files/usr/lib/openarena/baseoa/autoexec.cfg): fullscreen
  at the desktop's resolution, vsync on with a 60 fps cap, the game's own texture compression
  (`r_ext_compressed_textures 1`: Mesa makes DXT1 of the opaque textures, on the SPEs through
  libspeq, `SPEQ_S3TC`; `SPEQ_S3TC_RGB=ppe` sends RGB sources back to the PPE), and a DualShock 3
  bound through SDL's game controller (triggers fire and zoom, Cross jumps, the shoulders change
  weapon, Start the menu, Select the scores).

## How it performs

| Renderer | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Batch |
|---|---|---|---|---|
| opengl1 (base, tc1) | 70.6 | 0.0 s / 34.6 / 56 ms | 26 / 39 MB | gamedb:batch/2026-10-18-ioquake3-396 |
| opengl2 `gl2min` | 59.5 | 2.4 s / 21.9 / 868 ms | 15 / 117 MB | gamedb:batch/2026-10-18-batch17 |
| opengl2 `gl2min`, USB / SSD data / SSD data and swap | 55.9 / 56.1 / 56.1 | 0.3 / 0.0 / 0.1 s; longest 111 / 96 / 105 ms | 13 / 12 / 14 MB; 117 MB | gamedb:batch/2026-10-18-ssd1 |
| opengl2 `gl2min`, SSD root (n=3) | 60.4 (60.3-63.3) | 0.2 s / 25.6 / 199 ms | 15 / 119 MB | gamedb:batch/2026-10-19-ssdroot1-baseline |
| opengl2 `gl2min_md3pack` | 59.0 | 1.3 s / 23.5 / 943 ms | 17 / 98 MB | gamedb:batch/2026-10-18-batch17 |

Later SSD-root batches put `gl2min` at 57-63 fps (gamedb:batch/2026-10-19-ssdroot5-openal,
gamedb:batch/2026-10-19-ssdroot6-fence-work-kick, gamedb:batch/2026-10-20-ssdroot9-fence-work-kick).
Picture and sound are right in both renderers. opengl2 holds an Rss of 79-104 MB plus 58-68 MB swapped
(opengl1: 71 + 20).

- **The load.** opengl2's load takes 28.1 s on the USB root from exec to the end of the loading screens
  (10.2 s by the game's own `CL_InitCGame`), 10.9 s of it disk wait, and the demo's second pass reads
  the map and players again (131 MB) because memory pressure evicted them. With the whole root on the
  SSD it takes 6.9 s by the game's number and 14.2 s to the end of the loading screens, the disk wait
  2.5 s against 6.3 s ([PROFILING.md](../../../docs/PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)).
  Gameplay was already clean on USB, so the SSD root changed the reloads, not the frame rate. Its CPU in
  the load, all threads (gamedb:batch/2026-10-19-ssdroot2-startup-profiles): the game 3.9 s (the shader
  scripts, mipmaps and pk3 scans; in batch 17's profile files 1.95, game 1.91, mipmap 1.34), Mesa 0.55,
  linking 0.5, jpeg 0.29, DXT 0.27; 10.4 s in all. The lever is the game's own load path (2.0 s of the
  main thread if it were twice as fast).
- **Where the CPU goes** in play (gamedb:batch/2026-10-19-ssdroot2-gameplay-profiles, gl2min, 44 fps
  profiled): the renderer `renderer_opengl2.so` 24.1%, Mesa 23.1%, the kernel 14.2%, OpenAL 12.4% (the
  game mixes its sounds in OpenAL Soft's mixer on the PPE), libc 8.6%, ps3rsx 5.7%, libdeflate 3.9% (the
  demo's map reloads) ([HOTPATHS.md](../../../docs/HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).
- **Memory** (opengl1, [MEMORY.md](../../../docs/MEMORY.md)'s `MEM=1` batch of 2026-10-18, Mesa ps3.299):
  malloc used 21.4 / 33.5 / 20.8 MB at the window's start, middle and end, a 12 MB transient around the
  middle whose pages are kept (free 15.9 / 3.8 / 16.5 MB); 13.9 MB of heap went to swap while 15.9 MB sat
  free in its arenas. The hunk holds 30.7 MB (low permanent 16.1, high 14.7; temp high-waters 17.7 and
  15.7), so 2.7 MB of pages touched by loading are never used again ("unused highwater" in its own
  meminfo, in game.log). The hunk (128 MB) and the zone (48 MB) are one 176 MB mapping with 34.6 MB
  resident; the rest is never touched and costs nothing. Giving back the unused high-water after a map
  load (`com_hunkTrim`, 2.7 MB) is in the game's todo.
- **What used to stall.** In opengl1 the map reload at each pass and bots joining mid-match: 0025 and
  0026 took gameplay stalls from 9-10 s to 0. Many of the remaining "random" stalls were the kernel's
  lost decrementer, gone with the kernel fix
  ([the investigation](investigations/2026-10-17-reload-stalls.md)). In opengl2 they were memory:
  0027 took them from 9-11 s to 0.7-2.6 s
  ([the investigation](investigations/2026-10-18-opengl2-freezes.md)).

The benchmark ([recipe](benchmark/recipe.sh)) loops the timedemo `bench.dm_71` in one session
(`PASSES=1`; `bench_loop` and `bench_again` aliases in autoexec.cfg, since `nextdemo` is cleared before
each pass); every pass reloads the map, which the gameplay columns leave out. OpenArena keeps only 32
`+` commands on its command line (Debian's wrapper passes about 10), so the recipe writes its cvars into
autoexec.cfg. Its variants turn each engine patch back one at a time.

## Known problems

- The gameplay options menu can draw wrong the first time it opens (the GL 1.2 session); Escape and
  opening it again fixes it.
- opengl2 runs with about 15 MB of RAM free at its tightest.
- Six `Failed to load sound ...wav` / `Using default sound` pairs in almost every run (143 of 147: vulcan
  impacts, nailgun, yousuck, cl_ammoregen), probably gaps in the upstream data (not yet compared with a
  PC run); the engine plays a default sound. `Couldn't read q3history` is a first-run file.
- About one run in six played through OpenAL lost its sound to the sound card's DMA stopping (the
  OpenAL "Wait timeout" wedge), fixed by the snd_ps3 watchdog, kernel local/0047
  (gamedb:batch/2026-10-20-ssdroot12-dma-watchdog).

## What was tried

| Change | Result | Verdict |
|---|---|---|
| Mesa 0038 `NV30_FORCE_DXT=opaque` on gl2min | forced 0 textures: every one of 2238-2593 uploads was `skip_format` (sRGB or already compressed), fps, stalls and VRAM equal (gamedb:batch/2026-10-19-ssdroot2-force-dxt, gamedb:batch/2026-10-20-ssdroot8-force-dxt) | does nothing here |
| `gl2min_tc0` (compression left to Mesa) with force-DXT | 145-149 MB of VRAM either way: the textures are made with a NULL glTexImage2D | nothing forced |
| Mesa 0039 `NV30_SHADER_CACHE=on`, cold and warm | an upper bound of 0.24 s of the load ([PROFILING.md](../../../docs/PROFILING.md#nv30s-shader-cache)) | not worth it |
| Mesa 0040 staging pool | 57.65 off, 56.95 on (gamedb:batch/2026-10-19-ssdroot5-staging-pool): the pool handles uploads over 256 KiB only | no regression |
| Mesa 0041 `NV30_FENCE_WORK_KICK=1024` | 62.9 against 62.0 (+1.5%), CPU not lower (gamedb:batch/2026-10-20-ssdroot9-fence-work-kick) | neutral, off |
| Mesa 0044 `NV30_MINMAX_VMX=1` | 61.7 against 62.7 (gamedb:batch/2026-10-20-ssdroot11-minmax-vmx) | neutral here |
| our openal-soft +ps3.549 against Debian's | 57.3 against 56.3 (+2%) (gamedb:batch/2026-10-19-ssdroot5-openal) | kept |
| `r_md3PackedFrames 1` (0029) | 20 MB less VRAM, the same stalls (gamedb:batch/2026-10-18-batch17) | opt-in so far; a default candidate |
| Mesa `kick100` (ps3.282) | no default: it cost X-Moto 9% (gamedb:batch/2026-10-17-mesa-282-kick100) | rejected |

Open: whether 0029 becomes the default, why force-DXT skips every format here, and the proposals
(`com_hunkTrim`, `r_keepImages` for renderergl2, ADPCM sound effects) in the game's todo.

## Investigations

- [2026-10-17 opengl2 fits in memory](investigations/2026-10-17-opengl2-fits-in-memory.md): from
  "ran out of RAM compiling its shaders" to 26-32 fps with ioquake3 0001-0010.
- [2026-10-17 opengl1 batching and the atlas](investigations/2026-10-17-opengl1-batching-and-the-atlas.md):
  0019-0023 on the RSX, why the lightmap atlas went off by default (0024).
- [2026-10-17 reload stalls](investigations/2026-10-17-reload-stalls.md): the map reload and
  bots joining as stalls, 0025-0026, the kicker and the kernel's lost decrementer.
- [2026-10-18 opengl2 freezes](investigations/2026-10-18-opengl2-freezes.md):
  the "16 s freezes", the memory behind them, 0027-0029 and Mesa 0037.

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": listed first of the strong candidates (GL 1.x/2.1, light, a big-endian history from PowerPC Macs): the Quake III engine, which ran on G4/G5 Macs; a classic GPU test that should hit 60 fps at 1080p; RAM roughly 100-150 MB, tight but plausible.
