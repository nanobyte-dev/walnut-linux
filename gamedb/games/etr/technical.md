# Extreme Tux Racer: technical notes

Extreme Tux Racer 0.8.4 runs with the right picture and sound at about 59 fps in the benchmark, not a
vsync cap (59 uncapped too), after our patches cut a frame from 19,400 GL calls to 850. Its played session
(2026-10-24, with the controller patch) had a 58 fps median, the controller fully usable, and 608 sound
xruns the player didn't hear, which the new SFML audio period is meant to remove.

## Packages and patches

`extremetuxracer`, `extremetuxracer-data` (`0.8.4-2+ps3.N`, [packages/etr](packages/etr/)), replacing
Debian's. SFML 3.1 already has its big-endian Ogg fix.

- [0001](packages/etr/patches/0001-ETR_GL-switches-no-repeated-glBindTexture-the-HUD-nu.patch)-[0006](packages/etr/patches/0006-snow-spray-falling-snow-and-snow-curtains-in-one-dra.patch)
  send fewer, larger GL submissions, each behind a switch (`ETR_GL=0` turns all off): a bind cache and the
  HUD numbers (`ETR_GL_BIND`, `ETR_GL_HUD`), the terrain from buffer objects in one quadtree walk
  (`ETR_GL_TERRAIN`), Tux's spheres from cached vertex arrays and the shadow in one draw (`ETR_GL_TUX`,
  `ETR_GL_SHADOW`), track marks as arrays of what the camera sees (`ETR_GL_TRACKS`), trees and items in one
  draw per texture (`ETR_GL_TREES`, `ETR_GL_TREES_GROUP`), snow spray, falling snow and curtains in one draw
  (`ETR_GL_SNOW`, `ETR_GL_PARTICLES`). A frame went from 19,400 GL calls and 697 draws to 850 and 72
  ([the investigation](investigations/2026-10-17-game-side-batching.md)).
- [0007](packages/etr/patches/0007-Game-controllers-menus-and-a-fixed-race-scheme-next-.patch) and
  [0008](packages/etr/patches/0008-Game-controllers-Start-opens-the-race-menu-Escape-Se.patch), game controllers: SFML 3
  reads `/dev/input/js*`, so a button is the joydev index (DualShock 3 through hid-sony: 0 Cross, 1 Circle,
  2 Triangle, 3 Square, 4 L1, 5 R1, 6 L2, 7 R2, 8 Select, 9 Start, 13-16 the d-pad; L2/R2 are buttons, not
  axes). In menus the d-pad and left stick are the arrow keys, Cross and Start Enter, Circle Escape; in a
  race the left stick (analog) or d-pad steer, R2/R1 paddle, L2/L1 brake, Cross jumps (hold to charge),
  Square tricks, Triangle changes the view, Select pauses, Start opens the race's menu. The keys keep
  working and nothing is written to the options file; the scheme is on the Help screen in a second column.
- `libsfml` (packages/patched/audio/libsfml/, `3.1.0+dfsg-4+ps3.N`): SFML 3.1 (miniaudio) opened the ALSA
  device with a 64-frame period and 3 periods, a 4 ms buffer, which ETR can't change; ours opens it with a
  1024-frame period ([the investigation](investigations/2026-10-24-sound-buffer.md)).

## How it performs

- Bench (gamedb:batch/2026-10-18-batch17): 59 steady, 2.9 s of stalls (11 frames over 100 ms), 1% low
  27.3, longest 483 ms, 29 MB free, 58 MB of VRAM, no underruns; the window holds the game-over screen and
  a reload. Picture and sound right.
- Its CPU outside Mesa is mostly freetype and harfbuzz (29% of the samples) and SFML audio.
- **Its memory grows**: over a 117 s window the heap went 17.5 to 19.9 MB and its GART 8.8 to 12.8 to
  16.8 MB (161 to 163 handles), two new buffers of about 4 MB, one a minute. The candidate is the track
  marks in the snow, a growing list drawn through our stream buffers; a soak would tell a cap from a leak
  (the game's todo).

The benchmark ([recipe](benchmark/recipe.sh)): ETR has no demo mode, so the user's race was recorded as
keyboard events (`evemu-record`, variant `record`) and is replayed into a virtual keyboard from when ETR's
window appears. `assets/etr-race.evemu`: Practice, one course, 69 s, the menus up to 24.7 s; the race is
raced again after the game-over screen from `ETR_LOOP_FROM` up to `ETR_LOOPS` times. fps from the HUD
window (`WINDOW_STOP=1`). ETR's physics take the frame time as their step and the inputs replay at
wall-clock times, so at another frame rate Tux's line can drift from the recorded one, more in later
races.

## Investigations

- [2026-10-17 game-side batching](investigations/2026-10-17-game-side-batching.md): 52 to 59 fps, the CPU
  83.6 to 53 s.
- [2026-10-24 the 4 ms sound buffer](investigations/2026-10-24-sound-buffer.md).

## Research

What the surveys and plans said about it, newest first; each line names its source, the section and the date.

- 2026-10-15, the user's first wish list (backlog.txt, commit 7d58e3d), "2. GPU driver and game support": a strong candidate: GL 1.x and simple, a nice controller game.
