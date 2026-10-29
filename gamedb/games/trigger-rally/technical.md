# Trigger Rally: technical notes

## Status

Debian's Trigger Rally 0.6.6.1 (`trigger-rally`, `-data`, 116 MB). Smoke runs on 2026-10-28 (kernel .874, Mesa
ps3.924, the VRAM swap on): the menus are mouse-only (Return and the arrows do nothing there), so the recipe
([benchmark/recipe.sh](benchmark/recipe.sh)) clicks through them by XTEST (`xkeys.py @X,Y`): single race,
Aegyptian, race, the car (Return), then holds the throttle a few times. The race runs, the picture right
(desert, sky, the car, the HUD, the offroad sign; xwd and webcam):

| Settings | Steady fps | 1% low | Frames over 100 ms in 90 s | Peak RSS | Run |
|---|---|---|---|---|---|
| the recipe's cheap ones (no foliage, weather, dirt, anisotropy) | 65 | 47 | 4 (the stage load, 12.9 s, and 3 short) | 52 MB | `ps3-bench-20261028T213802Z` |
| the game's defaults (fresh configuration: foliage, weather, anisotropy 4) | 58 | 30 | 4 (the stage load, 12.4 s) | 51 MB | `...-smoke-trigger-rally-freshrace` |

A fresh configuration is already fullscreen at the screen's size (`automatic="yes"` in
/etc/trigger-rally.config.defs: `geom=1920x1080+0+0 fs=1`), so no console default needs a patch. Sound running,
no audible dropouts. The game found the DualShock 3 ("Found 1 joystick: Sony PLAYSTATION(R)3 Controller");
whether its own joystick mapping drives the car is not tried. Not checked: the sound by ear, a whole stage.

## Menus flashing: a stale back buffer from Mesa (session 2026-10-29)

In the first played session (`ps3-bench-20261029T205633Z-session-trigger-rally-base`, Mesa ps3.924, vsync as
shipped) the user found the menus unusable: after opening a menu, the TV alternated between it and the menu
before it. The xwd at 60 s is a clean main menu, since it reads one buffer.

The game is not the cause. In 0.6.6.1 the level screen (`MainApp::renderStateLevel`) draws a full-screen
background quad and the whole GUI every frame, `PApp::run` swaps every frame (`SDL_GL_SwapWindow` after
`glFinish`), and nothing draws to `GL_FRONT` or skips frames. The session's dmesg has
`ps3rsx: out of VRAM for a 8100 KiB BO` about 58 times a second for the whole session: one 1920x1080 back
buffer per frame. X page-flips the fullscreen window, so Mesa's DRI3 loader wants a third back buffer
(`dri3_update_max_num_back`: 3 with vsync, 4 without), and with the VRAM full of the game's textures it never
gets one. `dri3_find_back` picks the empty slot every frame and the allocation fails. `getBuffers` returns false,
so the frontend keeps drawing into the back buffer it validated last, while the swap presents whichever buffer is
idle, one of which holds an old frame. A static menu shows it plainly. In a race it would be a frame from the past
every other frame. The smoke runs (`vblank_mode=0`) logged the same failures in bursts and drew right in their
screenshots.

The fix is in Mesa, for every game that fills the VRAM: patch 0054 (docs/DRIVER.md's table) caps the loader at the
two back buffers it has once a third can't be allocated, and waits for one of them. There is no per-game switch:
Mesa 26 has no DRI2 for GLX, and the only way to stop the flips is xorg's `Option "PageFlip" "off"`, which applies
to every game. The two `Race finished in invalid state 30` lines are from quitting (state 30 is the quit dialog,
and `endGame` runs at shutdown). They are harmless.

## Played session 2026-10-29: textures missing, the pad unused

The first imported session (`ps3-bench-20261029T221408Z-session-trigger-rally-base`, rating/sessions/2026-10-29-session;
Mesa ps3.1139~dev with 0052 and 0054 but not 0053, ps3rsx 0.924, kernel .874): 5.3 min, three Trigger Cup stages
(warmup, operation, old), quit from the menu. Rust 84 (the player saw the picture broken; provisional), 58 fps median,
1% low 24, longest frame 448 ms, fullscreen 1920x1080, sound heard (no music in the race), 0 audible dropouts. The
menus held still: 0054 works (`dri3: no memory for back buffer 3, keeping 2` once in the game log, and only 10
8100 KiB `out of VRAM` lines in dmesg instead of a stream).

The player's notes: no textures, a grey gradient sky, a black car, no gamepad at all, the menus mouse-only. The
screenshots agree: the terrain, the HUD, the minimap and the co-driver signs are textured; the car, the buildings
and rocks and the sky are not. dmesg has a steady stream of `ps3rsx: out of VRAM` (about 400 a second; printed:
252 of 5464 KiB, a 1024x1024 RGBA texture with its mips, 144 of 1368 KiB, 104 of 344 KiB). Why, and the
fixes: the two sections below.

## Textures missing: the menu thumbnails fill the VRAM (Mesa 0055, package patch 0002)

Not a regression of Mesa 0052 or 0054. 0052 only refuses the 16-bit L16/A16/I16/R16 sampler formats, and the
game's textures are L8, L8A8, RGB8 and RGBA8 (`PTexture::load`); 0054 saves VRAM. The game simply wants more
VRAM than a session leaves it:

- At startup it loads every level's menu screenshot and minimap (`main.cpp` `loadLevel`, about 380 images of
  256x256, all with mipmaps, kept for the whole run), then the fonts, HUD, co-driver signs, and at a race the
  stage's maps, sky, vegetation and car. The session's 427 texture loads come to 174 MiB with mipmaps (RGB is
  stored as X8R8G8B8 on the RSX), plus two 8 MB back buffers and the depth buffer.
- ps3rsx's CPU-visible VRAM is 215 MB; in a session X keeps 19 MB of it and the VRAM swap is down to 3 MB
  (it gave the rest back, as designed). The game peaked at 181 MB, and the textures loaded last (the
  car's 1024x1024 RGB, the 512x512 sky, the buildings) found no room. nv30 asked for VRAM only, so they
  had no storage, drew untextured, and the state tracker retried them at every draw (the dmesg stream).
- The smoke runs stop the desktop, so X held less and the game's 197-205 MB fitted: last night's desert
  race was fully textured with Mesa ps3.924 for that reason, not because of the stage.

Reproduced on the PC with the mesa-shim, whose heaps now behave like ps3rsx's (first fit, `ENOSPC` when full,
`PS3SHIM_VRAM_CPU_KB` and friends shrink them) and `texload.c`, which replays the session's 427 loads in order
(list made from the game log and the data's image sizes) and draws the last 40 for 20 frames, the CPU-visible
VRAM held to 190,000 KiB: with Mesa ps3.1139 (the PS3's), 2 textures fail at load (5464 and 1368 KiB, the car
and the sky) and 80 retries fail in the frames, `GL_OUT_OF_MEMORY` every frame.

Two fixes, each enough for this game:

- **Mesa 0055** (generic, docs/DRIVER.md's table): a texture whose VRAM allocation fails goes to GART (main
  memory, RAM while it lives) instead of failing; the RSX samples it through TEX_FORMAT's DMA1, and renders
  into it with the surface DMAs pointed at GART. `NV30_GART_TEXTURES=0` turns it off (the same-boot A/B). In
  the shim with the same limit: 0 failed textures, both sampled from GART, GART's peak 4.8 to 11.6 MB, every
  push accepted by ps3rsx's validator; with `TEXLOAD_GENMIP=6` the mipmap generation renders into the GART
  textures (DMA_COLOR0 0xfeed0001) and is accepted too. Built as Mesa ps3.1148~dev (0052, 0054, 0055;
  0053 left out like the PS3's ps3.1139).
- **Our package's patch 0002** (packages/trigger-rally): the menu keeps only the thumbnails' paths and loads a
  level's pair when its screen shows it, freeing the pair before; a race frees them. About 127 MiB of VRAM
  less, and 380 fewer images decoded at startup. Nothing else used those textures (the race's map is the
  terrain's own). No quality change.

## The gamepad: a built-in DualShock scheme (package patch 0001)

Trigger Rally 0.6.6.1 reads joysticks only through the `<joystick>` blocks of its configuration, and the default
configuration ships both with `enable="no"`; its menus are mouse-only. On top of that, with SDL3 under
sdl2-compat the joystick events' `which` is an instance id counted from 1, which the game used as an index
into its joystick array (out of bounds for the first pad). Patch 0001 (gamedb:trigger-rally's package) adds a
fixed scheme through SDL's game controller API (the DS3 by button names, no raw indices), pad 1, hot-plug,
keyboard and mouse working alongside, nothing written to the user's configuration:

| Pad | Race | Menus |
|---|---|---|
| left stick, d-pad | steer (stick analog) | move the focus to the nearest widget that way (repeats when held) |
| R2 (analog), Cross | throttle | Cross: press the focused widget |
| L2 (analog), Square | brake, reverse |  |
| R1, Circle | handbrake | Circle: back (No on the quit questions, where the focus starts) |
| Triangle | recover the car |  |
| R3 | back to the last checkpoint |  |
| L1 | camera | L1/R1: previous/next page |
| Select | map on/off |  |
| right stick | look left/right |  |
| Start | pause (the pause screen lists the scheme; Circle there leaves the race) | press the screen's default (race), else the focus |

The focus shows in the mouse-hover colour, and the pointer hides while the pad is used. The car choice takes
the d-pad or stick, Cross or Start, Circle back. Tested on the PC (an amd64 build under Xvfb with SDL's virtual
game controller, scripted presses, screenshots looked at): hot-plug, the focus on every screen tried, the car
choice, driving at 44 mph with R2 and the stick, the pause text, leaving a race, the mouse and Escape still
working; the startup loaded no thumbnails. Not tested: a real DS3 (hid-sony's trigger rest values, Cross and
Circle not swapped), the right stick's direction, keyboard driving (that code is unchanged).

## Threads in the stall frames

Checked on 2026-10-29 after gamedb:luanti's stalls turned out to be its render thread waiting for the CPU its own worker threads had (luanti 0010 puts them at nice 10). From the stall record (stalls.py's `stall_main_pct` and `stall_rest_pct`: the CPU of the main thread and of the game's other threads in the stall frames, in % of their time, 100 being one PPE thread; Luanti before its fix: 22 and 83) and the profile (`bench/analysis/stallperf.py`). Run `ps3-bench-20261029T171249Z-smoke-trigger-rally-freshrace` (STALLS=1 PERF=1, the game's defaults; gamedb:batch/2026-10-29-worker-threads): 59 fps steady, 1% low 32, 3 frames over 100 ms in the 90 s smoke window, one of them the stage load (13.9 s: `glTexImage2D`, PhysFS reads, libjpeg), which the smoke window doesn't leave out. In those frames the main thread ran 74% of the time and the other threads 8% (OpenAL's mixer); the race itself has no worker threads that compete with the drawing.
