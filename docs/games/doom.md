# Doom: dsda-doom, Chocolate, Crispy and Woof

dsda-doom (the PrBoom+ successor, with the same GL renderer) plays Freedoom's DEMO1 on its GL renderer
at 55.9 steady fps with no stalls (row 61), after big-endian and GL fixes of ours. Its music still
chops: fluidsynth's voice DSP on the SPEs halved the underruns, but 465 a minute remain (row 52), and
the music default (D3) waits on finding the cause (PLAN 2.8). Chocolate Doom, Crispy Doom and Woof
are smoke-tested: they run at the vanilla 35 Hz cap.

## dsda-doom

Our build of Debian's `dsda-doom` 0.30.0 (`0.30.0+dfsg-1+ps3.N`, package directory `dsda-doom/`),
replacing Debian's, with Debian's `freedoom` 0.13.0 (`/usr/share/games/doom/freedoom2.wad`). Both are
in `ps3-games`. The release candidate is ps3.361.

| Patch | What | Switch |
|---|---|---|
| 0001 | dsda-doom.wad's DMX sound lumps read little-endian | |
| 0002 | GL renderer: indexed textures as GL_LUMINANCE_ALPHA. Stock GL mode uploads every indexed texture (walls, sprites, menus) as GL_RG, which nv30 lacks (no ARB_texture_rg): missing or black textures | |
| 0003 | the software renderer's SDL buffer in the texture's own format (the colours were wrong; the 960x540 frame was green-tinted through SDL's GL renderer) | |
| 0004 | screenshots in RGB24 | |
| 0005 | batched GL draws: on llvmpipe the gl timedemo went from about 1960 to 390 GL calls and 205 to 44 draws a frame | `DSDA_GL_BATCH`, `DSDA_GL_FLATS`, `DSDA_GL_HUD`, `DSDA_GL_VLIGHT`, `DSDA_GL_STATS` |
| 0006 | `DSDA_BENCH_LOOPS=N` repeats the timedemo N times, each with its own "Timed ..." line (stock exits at the end) | |

Music is MIDI through SDL_mixer and our fluidsynth (`libfluidsynth3` `2.6.1+dfsg-1+ps3.N`, its voice
DSP on the SPEs through libspeq, `SPEQ_SYNTH=auto|spu|ppe|off`; [SPU-AUDIO.md](../SPU-AUDIO.md)).

**Recipe** `games/bench/recipes/dsda-doom.sh`: `-timedemo demo1` (Freedoom's own DEMO1 lump, 2664
gametics, 76 s at 35 Hz), 6 passes (`DSDA_BENCH_LOOPS=6`, `PASSES=1`), parsed from `Timed N gametics
in N realtics = F frames per second`. The load ends at `Playing demo:` (0.30 never prints the
`R_Init` lines on the PS3, so before this the window, `PERF=1` and `MEM=1` never started). Base is the
software renderer at 960x540 scaled by SDL (at 1920x1080 the PPE managed about 5 fps). Freedoom 0.13's
own demos desync (freedoom issue #1532), which doesn't matter for a timedemo: the same tics play
every run. Variants: `gl` (the GL renderer
at 1920x1080), `sw1080`, `gles`, `sdlsw` (SDL's GLES2 or software renderer for the software frame),
`play` (the demo at normal speed with GL, 60 s, for the sound), `playnomusic`, `tuned` (OPL music,
48 kHz, a 2048 buffer, 60 fps cap), `playopl`, `playcap35`, `play48k`, `playspu`, `playppe`,
`playoff`, `glstats`, `glnobatch`, `glnoflats`, `glnohud`, `glnovlight`, `gloff`.

**Results**:

| Configuration | Steady fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Row |
|---|---|---|---|---|
| `gl` (batching on) | 55.9 | 0.0 s / 40.3 / 34 ms | 43 / 65 MB | 61 |
| `gloff` (batching off, same boot) | 54.2 | 0.0 s / 36.4 / 68 ms | 44 / 65 MB | 61 |
| software, profiled | 40.3 | 0.0 s / 21.8 / 97 ms | 45 / 26 MB | 58 |

The batching gave +3% and better lows (CPU 118 against 161 s). Picture right (our fixes).

**The music** ([SPU-AUDIO.md, "Results on the PS3"](../SPU-AUDIO.md#results-on-the-ps3)). fluidsynth took 48% of
dsda-doom's CPU, about 40% of a PPE thread, interpolation alone 31%. On the SPEs:

| Run (`play`, 60 s, GL) | CPU a minute | Underruns | Row |
|---|---|---|---|
| no music | about 29 s | 18 | 50 |
| stock fluidsynth on the PPE | 44.5-53 s | 1095-1335 | 44, 50, 52 |
| version 1 on the SPEs (libspeq 0.288) | 31.7 s | 1097 | 44 |
| version 2 (libfluidsynth3 +ps3.309, libspeq 0.318) | 31.8 s | 588 and 709 (stock 1231 and 1331 the same session) | 50 |
| version 2 on the shared deadline pool (libspeq 0.367) | 26.5 s | 465 (PPE 1095) | 52 |

The user listened to version 2: "night and day, spu makes a huge difference"; what glitches remain
line up with the game's own frame stalls. Earlier sound-settings tries (row 33, Mesa ps3.225): `tuned`
340 underruns and 46 s of CPU in one run, OPL 1312 in its first. The decision on the default (D3:
fluidsynth on the SPEs, OPL, or off) waits on the cause (PLAN 2.8).

History: 2026-10-16 the software renderer's colours fixed (+ps3.231, row 28); 2026-10-17 the GL
renderer runs (+ps3.247, row 32), 48.8 fps on the GL timedemo, software 33 with the demo unfinished on
Mesa ps3.246 and 38 finished on ps3.262 (+15%).

## Chocolate Doom, Crispy Doom, Woof

Smoke tests (`SUITE=0`), each with Freedoom 2 (the `freedoom` package) and level 1 started at once
(`-warp 1 -skill 3`) for a player. Installed from Debian ports with `--no-install-recommends`. All three
ran on 2026-10-18 with the right picture:

| Game | Path | Result |
|---|---|---|
| Chocolate Doom 3.1.1 | software renderer scaled by SDL2's renderer (sdl2-compat over our SDL3) to fullscreen 1920x1080; OPL music; SDL_mixer | MAP01, right colours, 35 fps (the vanilla cap) |
| Crispy Doom 7.1 (live ports have 7.2) | the same engine path; vanilla resolution (`crispy_hires 0`), 8-bit colour, the 35 Hz cap kept | MAP01, right picture, 35 fps |
| Woof 16.0.0 | software frame through SDL3's renderer, 400 lines scaled up; OpenAL; MIDI through fluidsynth (on the SPEs through our libfluidsynth3) | MAP01, right picture, 34 fps |

The recipes (`chocolate-doom.sh`, `crispy-doom.sh`, `woof.sh`) write the config (restored after the
run): fullscreen at the desktop size, the scaling buffer capped at 1 M pixels (the default 16 M means
an intermediate texture of up to 3200x2000 uploaded every frame), no ENDOOM, no start-up delay, no
disk icon. Variants: `demo` (the WAD's DEMO1, exits at its end), `attract` (the title and demo loop,
unattended), `nomusic`, `sdlsw`, `gles`; Crispy also `hires`, `uncapped`; Woof `uncapped`. Sound was
not judged by ear.

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md),
[archive/SPU-AUDIO.md](../archive/SPU-AUDIO.md).
