# Armagetron Advanced: technical notes

Armagetron Advanced 0.2.9.3 is Debian's package with two patches of ours (fullscreen at the desktop's
resolution by default, a fixed game-controller scheme) and runs with the right picture and sound. Live AI
rounds play at 83 steady fps in the benchmark; its played session (2026-10-25) had a 104 fps median, quit
cleanly and was fully playable with the pad.

## Packages and patches

`armagetronad`, `armagetronad-common` (Debian's 0.2.9.3.0-1 with our patches,
[packages/armagetronad](packages/armagetronad/)). Its textures are RGBA8, and the big-endian risk (GL_RGBA or
GL_BGRA picked from the SDL surface's red mask) didn't bite.

- [0001](packages/armagetronad/patches/0001-Screen-a-fresh-configuration-starts-fullscreen-at-th.patch), fullscreen by default. The
  compiled-in default was already fullscreen at the desktop's resolution (read at run time, so a 720p TV
  gets 720p). The 640x480 window came from `lastSuccess`, the first fallback when a start fails or is cut
  short before the display is up: it began as a 640x480 window, and a fresh configuration saved that. It now
  starts as the default. Only defaults change: a saved mode in the player's `user.cfg` is read over them,
  and the file is never edited.
- [0002](packages/armagetronad/patches/0002-Game-controllers-a-fixed-built-in-scheme-next-to-the.patch), game controllers: a fixed
  scheme next to the keyboard, nothing bound by hand and nothing written to the configuration. Controller N
  plays local player N (up to 4): d-pad left/right or L1/R1 turn (one turn per press), R2 or Cross brake
  (held), the right stick glances, Triangle switches the camera, Select shows the score table (held), Start
  opens the game menu; in menus the d-pad moves and changes, Cross selects, Circle and Start go back. The
  buttons are read through SDL2's game controller API (a DualShock 3 is "PS3 Controller" there): the game
  is SDL 1.2 on sdl12-compat, whose SDL2 is already loaded, and `uGamepad.cpp` looks the dozen functions up
  with dlopen. The pads become key events at the input queue, so recordings and the keyboard are
  unaffected. Hot-plug: rescanned every second. Settings > Miscellaneous > Game Controllers shows the scheme.
- The quit: the played session quit cleanly once sdl2-compat's close deadlock was fixed (our sdl2-compat).

## How it performs

- Bench (gamedb:batch/2026-10-18-armagetron-live-ai, one run): 83 steady, 1% low 24.1, one 127 ms frame in
  2 minutes, 58 MB free, 28 MB of VRAM, 289 underruns. Not comparable with the playback numbers before it.
- Played: 104 fps median, 1% low 47, 0.3 s of stutter a minute.

The benchmark ([recipe](benchmark/recipe.sh)): a local game of 4 AI players with the player spectating,
round after round (`SPECTATOR_MODE_1` and the `SP_*` settings), fps from the HUD window (`WINDOW_STOP=1`).
On a first start 0.2.9 shows a language menu, player setup and welcome screen that wait for a key; the
live variants write `FIRST_USE 0` and the language, and `during()` types Return, Down, Return into a
virtual keyboard: "Play Game", then "Local Game". The rounds are alike but not identical (the AIs decide
live). Until 2026-10-18, base played back `assets/armagetronad-bench.aarec` (`--playback --benchmark`), a
90 s recording cut by the record run's stop: after its end the game draws nothing, so the window held
about 6 s of frames. That is variant `playback` now. A live repeat is in the game's todo.

## Known problems

- On a first start the game shows a language menu and a player setup that wait for a key press.

## Investigations

- [2026-10-17 the lost CPU/GPU overlap](investigations/2026-10-17-submission-overlap.md): Mesa ps3.262's
  -13% and the adaptive submission (Mesa 0027).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": a strong candidate: very light GL, good for long-running stability tests.
