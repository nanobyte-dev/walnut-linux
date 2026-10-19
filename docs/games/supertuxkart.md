# SuperTuxKart

SuperTuxKart 1.5 runs on its legacy GL 2.1 renderer with our big-endian fixes: karts and track draw.
With the `lean` settings it races at 19-21 fps by the Gallium HUD with 1.3-2.7 s of gameplay stalls;
the game's defaults give about 13 fps and 13.5 s of stalls. Every run so far logged `sound=none`, so
the sound check (PLAN 2.1) comes before `lean` can become the package default (D4). Open items:
[backlog/games.md](../backlog/games.md).

## Packages and patches

`supertuxkart`, `supertuxkart-data` (`1.5-2+ps3.N`, package directory `supertuxkart/`), replacing
Debian's: Debian's quits while loading on big-endian. `supertuxkart-data` is Debian's data, rebuilt
because `supertuxkart` needs it at its own version. 839 MB with the data (752 MB of it data), so it
deploys by name (`make deploy PKGS="ps3-games supertuxkart supertuxkart-data"`) and isn't in
`ps3-games`. The release candidate is ps3.361.

| Patch | What |
|---|---|
| 0001 | the .spm mesh and armature loader byte-swaps the little-endian data instead of refusing to run (upstream #5578 was closed as not planned). All 1268 .spm files parse identically on amd64 and ppc64 |
| 0002 | textures and vertex colours in the right channel order on the OpenGL driver |
| 0003 | legacy renderer: no crash when render-to-texture fails, a power-of-two (512x512) minimap render target |
| 0004 | texture compression reaches the legacy renderer: opaque power-of-two textures as DXT1, compressed by Mesa at upload |
| 0005 | `STK_PROFILE_WARMUP=S`: profile mode restarts its frame count and clock once the race passes S seconds and ends the race at S + the profile time |

## The recipe

`games/bench/recipes/supertuxkart.sh`: profile mode (`--profile-time`) starts a race at once with
every kart driven by the AI, on lighthouse with 4 karts (`STK_TRACK`, `STK_KARTS`), fullscreen
1920x1080, `--no-start-screen`. With `STK_PROFILE_WARMUP=60` and `--profile-time=180` it drives 240 s
and prints `Number of frames: N time T, Average FPS: F` over the 180 s after the warm-up (`FPS_RE`,
`STEADY_RE`). The load ends at the "Track: Overall scene complexity" line. GL 2.1 picks the legacy
renderer by itself ("Using the fixed pipeline"). setup() writes the whole config
(`~/.config/supertuxkart/config-0.10/config.xml`) every run: base is the game's defaults with HD
textures on (up to 2048x2048, `max_texture_size` 512 when off), compression on, the minimap shown,
vsync and the old-driver popup off. The race is AI-driven (`rand()` seeded from the clock,
frame-time physics): close between runs, not identical. Its GPU timers are garbage on the RSX
(timer queries return a pixel count).

Variants: `nohd`, `tex256`, `notc`, `nomap`, `noparticles`, `static` (drivers not animated: the
legacy renderer skins every animated kart on the CPU), `geo0`, `vsync`, `karts1`, and `lean` (HD
textures off, no minimap, important particles only, static drivers: the candidate default).

## Current results

| Configuration | Steady fps (HUD) | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Row |
|---|---|---|---|---|
| base (defaults) | 12.6 | 13.5 s / 8.4 / 676 ms | 24 / 130 MB | 61 |
| `lean` | 21.4 | 2.4 s / 12.2 / 663 ms | 19 / 96 MB | 61 |
| `lean` (batch 16, 6 runs) | 19.1-21.0 | 1.3-2.7 s / 11.4-12.1 / 430-690 ms | 22-27 / 95-96 MB | 68 |
| `lean`, start-up profile | 19.4 | 2.0 s / 11.5 / 691 ms | 23 / 96 MB | 69 |

**The two frame rates.** The game's own "Average FPS" for `lean` is 106-111 (21602 frames in
202.6 s in run 20261018T180856Z), and that is what fps.csv and fps_steady.csv hold. The HUD's
per-second fps in the same window has a median of 19.4, and the 1% low, stall and longest-frame
cells come from the HUD's frame times. For base both agree (12 and 12.6). We quote the HUD; why
profile mode counts five times more frames than reach the screen with `lean` is not known.

Picture: the track draws (webcam, row 61); "out of VRAM" lines remain in the log. Sound: `sound=none`
in every run, either profile mode or OpenAL failing (PLAN 2.1).

The load is the longest of the suite: 54.3 s, 22.5 s of CPU and 18.6 s of disk wait on the main
thread, half of the load's CPU in decoders (png, zlib, jpeg and audio 12.4 s of 24.7 s), `skinJoint`
1.3 s ([PROFILING.md, "Start-up profiles, batch 17"](../PROFILING.md#start-up-profiles-batch-17)). Game prefetch left it
unchanged (row 68).

**Built, not measured yet:** Mesa 0038 `NV30_FORCE_DXT=opaque` and `=all` on `lean` (TESTS.md
batch ssdroot2 P2), for the alpha textures 0004 leaves plain.

Next: the sound, then the `lean` default, the legacy renderer's batching (PLAN 3.1), a VRAM
measurement (PLAN 2.3); see [backlog/games.md](../backlog/games.md).

## History

- 2026-10-16 (research): the FAQ asks for at least 1 GB of RAM against our 217 MB; the old powerpc
  colour bug (#714519).
- 2026-10-17: Debian's build stopped at its mesh loader refusing big-endian (e708512). Our +ps3.264
  loaded: karts rendered, the track didn't (VRAM exhausted, 210 failed buffer allocations, the minimap
  FBO incomplete, SIGSEGV at exit). ps3.306 with 0003-0004: VRAM 210 to 132 MB (98 MB with `nohd` or
  `lean`), HUD 13-19 fps.
- 2026-10-18: the track draws (row 61).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md).
