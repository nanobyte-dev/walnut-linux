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

## Threads in the stall frames

Checked on 2026-10-29 after gamedb:luanti's stalls turned out to be its render thread waiting for the CPU its own worker threads had (luanti 0010 puts them at nice 10). From the stall record (stalls.py's `stall_main_pct` and `stall_rest_pct`: the CPU of the main thread and of the game's other threads in the stall frames, in % of their time, 100 being one PPE thread; Luanti before its fix: 22 and 83) and the profile (`bench/analysis/stallperf.py`). Run `ps3-bench-20261029T171249Z-smoke-trigger-rally-freshrace` (STALLS=1 PERF=1, the game's defaults; gamedb:batch/2026-10-29-worker-threads): 59 fps steady, 1% low 32, 3 frames over 100 ms in the 90 s smoke window, one of them the stage load (13.9 s: `glTexImage2D`, PhysFS reads, libjpeg), which the smoke window doesn't leave out. In those frames the main thread ran 74% of the time and the other threads 8% (OpenAL's mixer); the race itself has no worker threads that compete with the drawing.
