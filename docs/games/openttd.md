# OpenTTD

OpenTTD 15.1 runs with the right picture through both of its video drivers: SDL's software path and
the OpenGL driver, which our patches made start on nv30. The benchmark is our 1962 savegame
(820 vehicles, 30 towns, the camera on a city of 5000), sound and music on, played through the GL
driver. With Debian's default 32bpp-anim blitter it runs at 11 fps with 52 s of gameplay stalls in the
120 s window. With the 8bpp-optimized blitter it runs at 34-43 fps with under half a second of
stalls and the same picture, so since patch 0003 that is our package's default (not yet on the PS3).
The music costs about a fifth of the frame rate and chops (3500-4700 underruns a run). Open items:
[backlog/games.md](../backlog/games.md).

## Packages and patches

`openttd`, `openttd-data` (`15.1-1+ps3.N`, package directory `openttd/`), replacing Debian's, with
Debian's `openttd-opengfx`, `openttd-opensfx`, `openttd-openmsx`; all in `ps3-games`. The release
candidate is ps3.297. OpenTTD passes its 97 ctest tests on ppc64 in Debian's build.

Stock `-v sdl-opengl` refuses to start on nv30 ("Single element texture formats not supported"): it
needs GL 3.0 or ARB_texture_rg, its VAO check wants ARB *and* APPLE vertex array objects below GL 3.0
(an upstream bug; Mesa has no APPLE one), and its sprite shaders use `texture2DLod` in the fragment
stage, which needs ARB_shader_texture_lod (nv30: off, no TXL).

| Patch | What |
|---|---|
| 0001 | the OpenGL driver on GL 2.1 without ARB_texture_rg or ARB_shader_texture_lod (GL_LUMINANCE8 for the single-channel textures, the VAO check fixed, no LOD in the shader); checked on llvmpipe forced to GL 2.1 |
| 0002 | the sprite recolour applied in the GLSL 1.10 sprite shader |
| 0003 | `support8bpp` defaults to `system`, so the blitter OpenTTD picks for an 8bpp base set such as OpenGFX is 8bpp-optimized instead of 32bpp-anim. NewGRFs that ask for 32bpp (or font anti-aliasing) still switch to a 32bpp blitter, and `-b 32bpp-anim` or `blitter = 32bpp-anim` in `openttd.cfg` brings the old one back. An existing `openttd.cfg` that already holds `support8bpp = no` (OpenTTD writes every setting) keeps it |

The software path needed our SDL3 fix: SDL3's accelerated window framebuffer was BGRA8888 on
big-endian, a strong blue cast for OpenTTD's 0xAARRGGBB words. libsdl3 +ps3.242 fixed the colours but
converted every frame twice on the CPU (about 2 fps, audio starved); +ps3.254 uploads ARGB/XRGB8888
natively (8 fps).

## The recipe

`games/bench/recipes/openttd.sh`. The default `base` is a real game: `assets/openttd-bench.sav`
(a 256x256 map played by six AIs for 12 years on the PC, then every company made a human one so no
script runs; made with `games/bench/openttd-savegen/`), loaded with `-g`, 1920x1080 fullscreen, the camera
on Taningville at the normal zoom, the game at normal speed. The GL driver (`-v sdl-opengl`, our package),
fps from the Gallium HUD (the load ends at the HUD's first frame, 60 s warm-up, 120 s measured), sound
and music on (SDL sound driver at 44.1 kHz, FluidSynth with the first soundfont, OpenMSX playing).
OpenTTD has no timedemo: the vehicles drift with the wall clock, the work per frame is the same.
setup() writes a config with autosave, news popups and the survey off. `vblank_mode=0` everywhere.

Variants: `nosound`, `nomusic`, `quiet` (effects and music off), `hz22`, `buf2048`, `buf4096` (the
SDL sound driver's rate and buffer), `blit8`, `blit32`, `blit32anim` (8bpp-optimized,
32bpp-optimized and 32bpp-anim; `blit32anim` is the old default, for comparisons with the batches
before patch 0003), `zoomout` (one zoom step out, 4x the sprites), `scroll` (the keyboard pans the
camera, Right 3 s and Left 3 s over and over), `sw` (software video through X11, no HUD), `swaccel`,
`simplay` (the save's simulation only, null video, 6000 ticks). `title`, `gl`, `accel`, `accel8`
are the title screen's demo game, the benchmark until 2026-10-18; `sim` is a new game from seed
12345 for 20000 ticks with no video (13.4 s on 2026-10-17).

## Results on the 1962 savegame

Batch ssdroot3 (SSD root, kernel .472, Mesa ps3.476, openttd ps3.297, three rounds per group, rows
78-80 of the matrix). Steady fps is the HUD's median in the 120 s window.

| Variant | Steady fps | 1% low | Gameplay stalls | Underruns | Free RAM |
|---|---|---|---|---|---|
| `base` (32bpp-anim), 9 runs | 10-14, mostly 11 | 5.5-7.5 | 22-54 s | 700-4700 | 13-22 MB |
| `blit8` (8bpp-optimized) | 34, 35, 43 | 13-15 | 0.3-0.5 s | 2600-4000 | 41 MB |
| `blit32` (32bpp-optimized) | 13-14 | 6-6.5 | 26-35 s | 3400-4400 | 18-26 MB |
| `nosound` / `nomusic` / `quiet` | 14 / 13 / 14 | 7-8 | 19-21 / 24-28 / 19-21 s | 0 / 63-107 / 0 | |
| `hz22`, `buf2048`, `buf4096` | 11-14, as `base` | 5.7-7.5 | 21-46 s | 80-3500 | |
| `zoomout` | 7 | 4.0-4.3 | 115-120 s | 3200-4400 | 9-18 MB |
| `scroll` | 7-8 | 3.9-4.4 | 100-116 s | 2700-4600 | |
| `sw` (software, X11, smoke) | no HUD | | | 3991 | 16 MB |
| `simplay` (smoke) | 6000 ticks in 82.7 s | | | | 51 MB |

- **The blitter is the big lever.** 32bpp-anim draws every sprite into a 32-bit buffer and keeps an
  8-bit animation buffer next to it; the 8bpp-optimized blitter writes one byte a pixel and the GL driver
  applies the palette on the GPU. Frame rate triples, the 1% low goes from 5.7 to 13-15 and the stalls
  nearly vanish, since a frame no longer takes 100 ms or more. 32bpp-optimized (no palette
  animation) gains only 2 fps.
- **The picture is the same.** Webcam pairs of `base` and `blit8` at 60 s and 150 s (rounds 2 and 3)
  show the same sprites, town labels, bridges, water and date bar. The speckled water is the webcam
  catching the palette animation mid-cycle, in both; the label that looks garbled is two station names
  overlapping, also in both. OpenGFX is an 8bpp base set, so nothing 8bpp loses shows; 32bpp-only
  NewGRFs would, and patch 0003 leaves those on a 32bpp blitter.
- **The music costs about 3 fps (-20%) and 25-30 s of stalls** (`nomusic` 13, `nosound` and
  `quiet` 14 against 11), FluidSynth on the PPE; effects alone cost almost nothing. `hz22` (half the
  synthesis) and larger buffers (46 and 93 ms) do not help by themselves: 4 of the 12 runs of the buffer
  group ran at 14 fps with 80-700 underruns, one in each arm including `base`, and the rest at 10-12
  with 1800-4700. What decides is how much the music costs in that run (OpenTTD plays a random track; not
  confirmed), not the option. The audio agent's reverb and chorus work is the lever
  ([SPU-AUDIO.md](../SPU-AUDIO.md)).
- **Zooming out and scrolling are the worst case**: 7 fps with the stalls filling the window (115 s of
  120): the sprite count per frame, not the blitter's per-pixel cost. They are not run with `blit8` yet.
- **The simulation is not the limit**: 6000 ticks run in 82.7 s with no video and no sound (about
  2.2x real time, 33 ticks a second are needed), so the drawing and the music take the rest.
- The microphone is no judge here (the mic hears the room, which was loud after about 06:00Z), and
  nobody has listened to the music (TESTS.md "Next").

## Results on the title screen

From before the savegame: the demo game, a different and lighter scene.

| Mode | fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Underruns | Row |
|---|---|---|---|---|---|
| `gl` | 21 steady | 6.3 s / 9.2 / 642 ms | 17 / 26 MB | 3893 | 61 |
| software, SDL3's accelerated framebuffer (`accel`, SDL3 ps3.254, HUD median) | 9.5; 12 with the 8bpp blitter (`accel8`) | | 29 / 32 MB; 42 / 32 MB | 1037; 1098 | 35 |
| software through X11 (`title`, profiled) | no counter | | 18 MB free | 3759 | 58 |
| `gl`, profiled (PERF=1 MEM=1), SSD root | 18 | 14.7 s (101 frames) / 6.5 / 662 ms | 26 / 26 MB | 4172 | 73 |

The profile run of 2026-10-19 (row 73) sat at the main menu (the webcam shows the title screen with the
menu open), so it is a profile of the title screen's redraw and its music, not of play: the process
spent 49.5% of its samples in the kernel, and over 40% of them on one thing. Each frame's upload of the
screen from its pixel buffers (`OpenGLBackend::ReleaseVideoBuffer`, 8.3 + 2.1 MB) got new GART staging
buffers in Mesa's nv30: zeroed by ps3rsx (`memset` 5.3%), faulted in page by page through lv1 (27.1%,
booked as `__replay_soft_interrupts`) and unmapped page by page (8.4%), with a wait for the GPU before
each. Mesa 0040 (`NV30_STAGING_POOL_MB=24`) reuses them instead ([HOTPATHS.md](../HOTPATHS.md#soft-masked-time-and-the-staging-buffer-per-frame)).
The game itself took 21.9% (`Blitter_40bppAnim::Draw` 8.7%); the MIDI music through FluidSynth 11%
and libspeq 4.2% on the audio thread, with 4172 underruns. Sound is choppy either way, and its music
shares the SDL_mixer MIDI path with dsda-doom's (PLAN 2.8).

Next: Mesa 0040's A/B (`NV30_STAGING_POOL_MB=24`) on `base` and `blit8` (TESTS.md irqoff-A), `blit8`
three more times with a webcam check, the new package on the PS3 (default 8bpp-optimized, `blit32anim`
for the old one), `zoomout` and `scroll` with the 8bpp blitter, the music's cost
(PLAN 2.8), then the GL driver's batching (PLAN 3.1).

## History

2026-10-16 (research): the GL mode was expected to fail at init with no GPU risk. 2026-10-17: the
simulation benchmark and the software title game ran; SDL3's blue cast found; our OpenGL patches built.
2026-10-18: the GL driver ran on the PS3 (row 61). 2026-10-19: a profile of the title screen on the SSD
root (row 73); the savegame benchmark and batch ssdroot3 (rows 77-80): the 8bpp-optimized blitter
triples the frame rate, patch 0003 makes it the default.

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md).
