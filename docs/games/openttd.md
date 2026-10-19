# OpenTTD

OpenTTD 15.1 runs with the right picture through both of its video drivers: SDL's software path at
8-12 fps (9.5 and 12 in row 35), and the OpenGL driver, which our patches made start on nv30, at 21 (row 61). It is far
under the 60 fps target, and its MIDI music is choppy (3893 underruns in a run). The current
benchmark is the title screen's demo game; a gameplay benchmark on a real savegame is being built
(TESTS.md "openttd-play"). Open items: [backlog/games.md](../backlog/games.md).

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

The software path needed our SDL3 fix: SDL3's accelerated window framebuffer was BGRA8888 on
big-endian, a strong blue cast for OpenTTD's 0xAARRGGBB words. libsdl3 +ps3.242 fixed the colours but
converted every frame twice on the CPU (about 2 fps, audio starved); +ps3.254 uploads ARGB/XRGB8888
natively (8 fps).

## The recipe

`games/bench/recipes/openttd.sh`, three modes:
- **base**: the title screen's demo game (trains running, the camera panning by itself) at 1920x1080
  fullscreen with SDL's software video through plain X11 (`SDL_FRAMEBUFFER_ACCELERATION=0`). No GL,
  so no HUD fps: CPU profiles, stalls and memory only. The window starts 60 s after "Loading savegame
  version" (`-d sl=1`) and runs 120 s. The title game's ticks and pans run on the wall clock, so the
  view at a given second differs a little between runs. setup() writes a config with
  `participate_survey = no`, or OpenTTD 14+ asks over the title game.
- **sim**: simulation only, null video, sound and music: a new game from seed 12345, 20000 ticks,
  timed. It ran in 13.4 s (2026-10-17).
- **gl**: the sdl-opengl driver with its default 40bpp-anim blitter (sprites still drawn on the CPU,
  palette animation and the screen upload on the GPU), `-d driver=2`, HUD on.

Variants `accel` (SDL3's default accelerated framebuffer) and `accel8` (with the 8bpp-optimized
blitter). `vblank_mode=0` in every mode.

## Current results

| Mode | fps | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Underruns | Row |
|---|---|---|---|---|---|
| `gl` | 21 steady | 6.3 s / 9.2 / 642 ms | 17 / 26 MB | 3893 | 61 |
| software, SDL3's accelerated framebuffer (`accel`, SDL3 ps3.254, HUD median) | 9.5; 12 with the 8bpp blitter (`accel8`) | | 29 / 32 MB; 42 / 32 MB | 1037; 1098 | 35 |
| software through X11 (`base`, profiled) | no counter | | 18 MB free | 3759 | 58 |
| `gl`, profiled (PERF=1 MEM=1), SSD root; the title screen, not a game | 18 | 14.7 s (101 frames) / 6.5 / 662 ms | 26 / 26 MB | 4172 | 73 |

Picture right in both (webcam, no survey dialog). Sound is choppy either way: about 1000-1170
underruns a minute. Its music shares the SDL_mixer MIDI path with dsda-doom's (PLAN 2.8). The
`base` window has no fps by design.

The profile run of 2026-10-19 (row 73) sat at the main menu (the webcam shows the title screen with the
menu open), so it is a profile of the title screen's redraw and its music, not of play: the process
spent 49.5% of its samples in the kernel, and over 40% of them on one thing. Each frame's upload of the
screen from its pixel buffers (`OpenGLBackend::ReleaseVideoBuffer`, 8.3 + 2.1 MB) got new GART staging
buffers in Mesa's nv30: zeroed by ps3rsx (`memset` 5.3%), faulted in page by page through lv1 (27.1%,
booked as `__replay_soft_interrupts`) and unmapped page by page (8.4%), with a wait for the GPU before
each. Mesa 0040 (`NV30_STAGING_POOL_MB=24`) reuses them instead ([HOTPATHS.md](../HOTPATHS.md#soft-masked-time-and-the-staging-buffer-per-frame)).
The game itself took 21.9% (`Blitter_40bppAnim::Draw` 8.7%); the MIDI music through FluidSynth 11%
and libspeq 4.2% on the audio thread, with 4172 underruns.

Next: the gameplay benchmark (a developed map, a fixed camera, sound and music on; smoke, then bench
x3, GL against software), Mesa 0040's A/B on it and on the title screen (TESTS.md batch irqoff), the
sound, then the GL driver's batching (PLAN 3.1).

History: 2026-10-16 (research) the GL mode was expected to fail at init with no GPU risk.
2026-10-17: the simulation benchmark and the software title game ran; SDL3's blue cast found; our
OpenGL patches built. 2026-10-18: the GL driver ran on the PS3 (row 61). 2026-10-19: a profile of
the title screen on the SSD root (row 73).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md).
