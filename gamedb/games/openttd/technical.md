# OpenTTD: technical notes

OpenTTD 15.1 runs with the right picture through both of its video drivers: SDL's software path and the
OpenGL driver, which our patches made start on nv30. With the 8bpp-optimized blitter (our package's
default since patch 0003) and Mesa 0040's staging pool, the 1962 benchmark savegame plays at about 52
steady fps; its OpenGL played session (2026-10-25) had a 60 fps median with clean music. The software
driver plays too, but gives the frame timer nothing to measure.

## Packages and settings

- `openttd`, `openttd-data` (`15.1-1+ps3.N`, [packages/openttd](packages/openttd/)), replacing Debian's,
  with Debian's `openttd-opengfx`, `openttd-opensfx`, `openttd-openmsx`; all in `ps3-games`. The release
  candidate is ps3.297. OpenTTD passes its 97 ctest tests on ppc64 in Debian's build.
- Stock `-v sdl-opengl` refuses to start on nv30 ("Single element texture formats not supported"); our
  [0001](packages/openttd/patches/0001-OpenGL-run-on-GL-2.1-without-ARB_texture_rg-or-ARB_s.patch) and
  [0002](packages/openttd/patches/0002-OpenGL-apply-the-sprite-recolour-in-the-GLSL-1.10-sp.patch) make the OpenGL driver run
  on GL 2.1 ([the investigation](investigations/2026-10-17-gl-driver-on-gl21.md)).
- [0003](packages/openttd/patches/0003-default-support8bpp-system-so-the-8bpp-optimized-blitte.patch): `support8bpp` defaults to
  `system`, so the blitter OpenTTD picks for an 8bpp base set such as OpenGFX is 8bpp-optimized instead of
  32bpp-anim. NewGRFs that ask for 32bpp (or font anti-aliasing) still switch to a 32bpp blitter, and
  `-b 32bpp-anim` or `blitter = 32bpp-anim` in `openttd.cfg` brings the old one back. An existing
  `openttd.cfg` that already holds `support8bpp = no` (OpenTTD writes every setting) keeps it
  ([the investigation](investigations/2026-10-19-savegame-blitters.md)).
- Two menu entries of ours ([desktop/launchers](desktop/launchers/)) replace Debian's (diverted): "OpenTTD
  (OpenGL)", `-v sdl-opengl -r 1920x1080`, and "OpenTTD (software)", `-v sdl` with
  `SDL_FRAMEBUFFER_ACCELERATION=0`.
- The software path needed our SDL3 fix (libsdl3 +ps3.254): SDL3's accelerated window framebuffer was
  BGRA8888 on big-endian, a strong blue cast.

## How it performs

The benchmark ([recipe](benchmark/recipe.sh)) is a real game: `assets/openttd-bench.sav`, a 256x256 map
played by six AIs for 12 years on the PC, then every company made a human one so no script runs (made
with [assets-src](benchmark/assets-src/)), loaded with `-g`, 1920x1080 fullscreen, the camera on
Taningville at the normal zoom, the game at normal speed, the GL driver, sound and music on (SDL sound at
44.1 kHz, FluidSynth with the first soundfont, OpenMSX). OpenTTD has no timedemo: the vehicles drift with
the wall clock, the work per frame is the same. fps from the Gallium HUD (60 s warm-up, 120 s window).

| Configuration | Steady fps | 1% low | Gameplay stalls | Batch |
|---|---|---|---|---|
| 32bpp-anim (the old default), 9 runs | 10-14, mostly 11 | 5.5-7.5 | 22-54 s | gamedb:batch/2026-10-19-ssdroot3-openttd-sound (and -buffers, -blitters) |
| 8bpp-optimized (`blit8`) | 34, 35, 43 | 13-15 | 0.3-0.5 s | gamedb:batch/2026-10-19-ssdroot3-openttd-blitters |
| 32bpp-anim with Mesa 0040 `NV30_STAGING_POOL_MB=24` | 20 (11 off) | 9.4 | 4.3 s (43 off) | gamedb:batch/2026-10-19-ssdroot4-staging-pool |
| 8bpp with Mesa 0040 | 52 (34 off) | 18.4 | 0.1 s | gamedb:batch/2026-10-19-ssdroot4-staging-pool |
| 8bpp with Mesa 0040, `zoomout` / `scroll` | 23 / 31 (7 / 7-8 without the pool) | 11.1 / 14.3 | | gamedb:batch/2026-10-19-ssdroot5-openttd-views |

- **The blitter is the big lever** (frame rate x3, the 1% low from 5.7 to 13-15) and **the staging pool the
  second**: each frame's upload of the screen (8.3 + 2.1 MB) had taken new GART staging buffers, zeroed,
  faulted in page by page through lv1 and unmapped page by page, with a wait for the GPU before each
  ([the investigation](investigations/2026-10-19-staging-buffer-per-frame.md)).
- **The simulation is not the limit**: 6000 ticks of the save run in 82.7 s with no video and no sound
  (about 2.2x real time) (gamedb:batch/2026-10-19-ssdroot3-openttd-smoke).
- **The music costs about a fifth of the frame rate** (`nomusic` 13, `nosound` and `quiet` 14 against 11
  on 32bpp-anim), FluidSynth on the PPE; effects alone cost almost nothing. `hz22` and larger SDL buffers
  don't help by themselves: each run is either 10-12 fps with 1800-4700 underruns or 14 with 80-700, in
  every arm, following how much the music costs in that run (a random track; not confirmed)
  (gamedb:batch/2026-10-19-ssdroot3-openttd-buffers). Our FluidSynth with a divide-free reverb: 35 against
  34 fps; effects off (`FLUIDFX=off`) 40 (gamedb:batch/2026-10-19-ssdroot5-fluidsynth).
- The title screen (the benchmark until 2026-10-18, a lighter scene): `gl` 21 steady with 6.3 s of stalls
  (gamedb:batch/2026-10-18-batch8); software with SDL3's accelerated framebuffer 9.5, 12 with 8bpp
  (gamedb:batch/2026-10-17-ps3rsx-249-first-boot). Its CPU profile (gamedb:batch/2026-10-19-ssdroot2-gameplay-profiles,
  18 fps, before Mesa 0040; a menu, not a measure of play) was half kernel, 49.5%, nearly all of it the new
  8 MB staging buffer per frame for the screen upload: hash page faults on it 27.1% and its unmapping 8.4%
  (both booked as `__replay_soft_interrupts`), its zeroing (`memset` under `ps3rsx_gart_clear`) 5.3%; then
  FluidSynth 10.2% and libspeq 4.2% on the audio thread and `Blitter_40bppAnim::Draw` 8.5%
  ([HOTPATHS.md](../../../docs/HOTPATHS.md#soft-masked-time-and-the-staging-buffer-per-frame)).

## Known problems

- With the OpenGL driver, water pulses to white or very bright (its palette animation; the software driver
  and the PC don't): the played session.
- With the 32bpp-anim blitter the same game runs at 11 fps; NewGRFs that ask for 32bpp bring it back.
- The music still underruns (2600-4500 a run with `blit8`); nobody has judged it by ear in a bench run.
- Once in 13 runs with the staging pool, the kernel couldn't back the GART
  ([the investigation](investigations/2026-10-19-gart-nearly-full.md)).
- Log noise: `OpenGL: Single element texture formats not supported, using luminance textures` in every
  GL run (nv30 lacks `GL_ARB_texture_rg`; our fallback works).

## Investigations

- [2026-10-17 the OpenGL driver on GL 2.1](investigations/2026-10-17-gl-driver-on-gl21.md): what stock
  refuses, 0001-0002, and SDL3's blue cast on the software path.
- [2026-10-19 the savegame and the blitters](investigations/2026-10-19-savegame-blitters.md): batch
  ssdroot3, why the 8bpp-optimized blitter became the default (0003).
- [2026-10-19 the staging buffer per frame](investigations/2026-10-19-staging-buffer-per-frame.md) (Mesa 0040).
- [2026-10-19 GART nearly full](investigations/2026-10-19-gart-nearly-full.md): the one kernel message new
  with the staging pool.
