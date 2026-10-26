# dsda-doom: technical notes

dsda-doom 0.30.0 (the PrBoom+ successor, with the same GL renderer) plays Freedoom's DEMO1 on its GL
renderer at 55.9 steady fps with no stalls, after big-endian and GL fixes of ours. Its OpenGL played
session (2026-10-25) had a 58 fps median and was smooth; the software launcher 44. What is left is the
MIDI music, which still chops: FluidSynth's voice DSP on the SPEs halved the underruns, but hundreds a
minute remain.

## Packages and settings

Our build of Debian's `dsda-doom` (`0.30.0+dfsg-1+ps3.N`, [packages/dsda-doom](packages/dsda-doom/)),
replacing Debian's, with Debian's `freedoom` 0.13.0 (`/usr/share/games/doom/freedoom2.wad`); both in
`ps3-games`. The release candidate is ps3.361.

| Patch | What | Switch |
|---|---|---|
| [0001](packages/dsda-doom/patches/0001-rdatawad-dmx-format-little-endian.patch) | dsda-doom.wad's DMX sound lumps read little-endian | |
| [0002](packages/dsda-doom/patches/0002-gl-indexed-textures-luminance-alpha.patch) | GL renderer: indexed textures as GL_LUMINANCE_ALPHA. Stock GL mode uploads every indexed texture (walls, sprites, menus) as GL_RG, which nv30 lacks (no ARB_texture_rg): missing or black textures | |
| [0003](packages/dsda-doom/patches/0003-sdl-buffer-in-texture-format.patch) | the software renderer's SDL buffer in the texture's own format (the colours were wrong; the 960x540 frame was green-tinted through SDL's GL renderer) | |
| [0004](packages/dsda-doom/patches/0004-screenshot-rgb24.patch) | screenshots in RGB24 | |
| [0005](packages/dsda-doom/patches/0005-gl-batched-draws.patch) | batched GL draws: on llvmpipe the gl timedemo went from about 1960 to 390 GL calls and 205 to 44 draws a frame | `DSDA_GL_BATCH`, `DSDA_GL_FLATS`, `DSDA_GL_HUD`, `DSDA_GL_VLIGHT`, `DSDA_GL_STATS` |
| [0006](packages/dsda-doom/patches/0006-timedemo-DSDA_BENCH_LOOPS-N-repeats-the-demo-N-times.patch) | `DSDA_BENCH_LOOPS=N` repeats the timedemo N times, each with its own "Timed ..." line | |

- Two menu entries of ours ([desktop/launchers](desktop/launchers/)) replace Debian's (diverted):
  "DSDA-Doom (OpenGL)" at 1920x1080 and "DSDA-Doom (software)" at 960x540 scaled (at 1920x1080 the PPE
  manages about 5 fps in software).
- Music is MIDI through SDL_mixer and our FluidSynth (`libfluidsynth3` `2.6.1+dfsg-1+ps3.N`, its voice
  DSP on the SPEs through libspeq, `SPEQ_SYNTH=auto|spu|ppe|off`; [SPU-AUDIO.md](../../../docs/SPU-AUDIO.md)).

## How it performs

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Batch |
|---|---|---|---|---|
| `gl` (batching on) | 55.9 | 0.0 s / 40.3 / 34 ms | 43 / 65 MB | gamedb:batch/2026-10-18-batch8 |
| `gloff` (batching off, same boot) | 54.2 | 0.0 s / 36.4 / 68 ms | 44 / 65 MB | gamedb:batch/2026-10-18-batch8 |
| software, profiled | 40.3 | 0.0 s / 21.8 / 97 ms | 45 / 26 MB | gamedb:batch/2026-10-18-mesa-407-switches |
| `gl`, profiled, SSD root | 55.9 | 0.0 s / 39.7 / 42 ms | 43 / 66 MB | gamedb:batch/2026-10-19-ssdroot2-gameplay-profiles |

The batching gave +3% and better lows (CPU 118 against 161 s). The gameplay profile
(gamedb:batch/2026-10-19-ssdroot2-gameplay-profiles): Mesa 22% of the samples, the game 20.5%, the music
about 31% on the audio thread (FluidSynth 14.2%, libspeq 11.9%: `speq_synth_add_v2` 7.4%,
`speq_synth_render_v2` 2.0%; ALSA 3.4%), kernel 12.7%, ps3rsx 3.3%, Xorg 8.7% of the game's; 2793
underruns in the run. The music is
the largest share of the seven profiles
([HOTPATHS.md](../../../docs/HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).

The benchmark ([recipe](benchmark/recipe.sh)): `-timedemo demo1` (Freedoom's own DEMO1 lump, 2664
gametics, 76 s at 35 Hz), 6 passes (`DSDA_BENCH_LOOPS=6`, `PASSES=1`). The load ends at `Playing demo:`
(the recipe's `LOAD_MARK`; 0.30 never prints the `R_Init` lines on the PS3), where the window, perf and the
memory snapshots start. Base is the software renderer at 960x540 scaled by SDL;
the bench row of the suite is `gl`. Freedoom 0.13's own demos desync (freedoom issue #1532), which
doesn't matter for a timedemo: the same tics play every run. Variants include `play` (the demo at normal
speed with GL, 60 s, for the sound), `playnomusic`, `tuned` (OPL music, 48 kHz, a 2048 buffer, 60 fps
cap), `playopl`, `playspu`, `playppe`, `playoff` and the batching switches.

## Known problems

- **The MIDI music chops**: about 465 sound underruns a minute with the synthesizer on the SPEs; the
  OpenGL session counted 7.5 audible dropouts a minute, the software one 35.9 (the CPU renders there).
  The decision on the default (FluidSynth on the SPEs, OPL, or no music) waits on the cause
  ([the investigation](investigations/2026-10-17-music-on-the-spes.md)).

## Investigations

- [2026-10-16 the big-endian and GL fixes](investigations/2026-10-16-big-endian-and-gl-fixes.md) (0001-0004).
- [2026-10-17 the music on the SPEs](investigations/2026-10-17-music-on-the-spes.md).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-16, [game-candidates.md](../../research/game-candidates.md), "The recommended suite and why": PrBoom+ GL (on the user's list) is covered by dsda-doom, its successor with the same GL renderer, packaged for ppc64.
- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": "PrBoom+, with its GL renderer" a stretch goal: easy, a light test of the 2D/texture paths more than real 3D.
