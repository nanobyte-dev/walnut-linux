# BomberClone: technical notes

BomberClone 0.11.9, an Atomic Bomberman clone (SDL 1.2 on sdl12-compat, up to 8 local or network players),
is Debian's 0.11.9-9 with two patches of ours. Its played session (2026-10-26, about 5 minutes against bots)
had a 50 fps median, no crash, the picture right (4:3 with bars at the sides) and the sound effects and music
heard; the gamepad works in matches but not in the menus.

## Packages and patches

`bomberclone`, `bomberclone-data` (`0.11.9-9+ps3.N`, [packages/bomberclone](packages/bomberclone/)),
replacing Debian's; in no suite.

- [0001](packages/bomberclone/patches/0001-main-close-the-audio-and-SDL-before-exiting.patch): `main()` returned after `gfx_shutdown()`
  without `snd_shutdown()` or `SDL_Quit()`, so SDL_mixer's audio thread was still running while `exit()`
  ran the libraries' destructors. With SDL 1.2 provided by sdl12-compat (over SDL2 and SDL3, as in Debian now)
  the thread then called into torn-down state, and the game segfaulted on every quit, from the menu or on
  SIGTERM. Now it closes the audio and SDL first.
- [0002](packages/bomberclone/patches/0002-config-full-screen-by-default.patch): full screen by default (it started in a 640x480 window
  on the desktop); the saved config and the Options menu still change it.
- [0003](packages/bomberclone/patches/0003-Game-pads-work-the-menus-too-and-a-DualShock-3-s-D-p.patch): the pads work the menus
  (they read the keyboard only): every menu and wait-for-a-key screen reads events through
  `s_fetchevent`, which now hands them a pad's hat or first stick as the arrow keys, the first button as
  Return and the second as Escape (Cross and Circle on a DualShock 3), and a DualShock 3's D-pad buttons
  and Start too. In a match a DualShock 3's Start opens the game menu (which the pad then works) and its
  D-pad moves as the stick does (any pad's hat too). Tried on the PC with a virtual DS3 (evemu, the
  hid-sony layout): the main menu, the manual's pages and back. Pending its PS3 test.

## How it performs

Played (rules 2.0, Mesa ps3.860, kernel .786, bomberclone ps3.758): 50 fps median, 1% low 28, 2.1 s of
stutter a minute with a 2.4 s longest frame, a 16.5 s load, 62 MB RSS. The sound: 219 xruns, one audible
dropout in the play (9 more around the screenshots, the recorder's own). The player quit from the menu, cleanly.
The played test ([rating/test.toml](rating/test.toml)) sets its settings file `~/.bomberclone.cfg` aside per
session; the recipe ([benchmark/recipe.sh](benchmark/recipe.sh)) is for sessions only, no benchmark.

## Known problems

- The gamepad worked in matches but not in the menus: BomberClone's menus read the keyboard only (as on the
  PC). 0003 (pending its PS3 test) makes the pads work them.
- The HUD text is small at 1080p (12 px: Player1, Bombs, Range, Speed at the top).
