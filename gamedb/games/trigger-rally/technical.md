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
252 of 5464 KiB, a 1024x1024 RGBA texture with its mips, 144 of 1368 KiB, 104 of 344 KiB). Both are open
items in todo.toml.

## Threads in the stall frames

Checked on 2026-10-29 after gamedb:luanti's stalls turned out to be its render thread waiting for the CPU its own worker threads had (luanti 0010 puts them at nice 10). From the stall record (stalls.py's `stall_main_pct` and `stall_rest_pct`: the CPU of the main thread and of the game's other threads in the stall frames, in % of their time, 100 being one PPE thread; Luanti before its fix: 22 and 83) and the profile (`bench/analysis/stallperf.py`). Run `ps3-bench-20261029T171249Z-smoke-trigger-rally-freshrace` (STALLS=1 PERF=1, the game's defaults; gamedb:batch/2026-10-29-worker-threads): 59 fps steady, 1% low 32, 3 frames over 100 ms in the 90 s smoke window, one of them the stage load (13.9 s: `glTexImage2D`, PhysFS reads, libjpeg), which the smoke window doesn't leave out. In those frames the main thread ran 74% of the time and the other threads 8% (OpenAL's mixer); the race itself has no worker threads that compete with the drawing.
