# The light games: X-Moto, Extreme Tux Racer, Neverball, Armagetron

Four GL 1.x games from Debian, three of them with our batching patches. They are where game-side
batching paid most: X-Moto went from 44.5 to 166.5 fps and Extreme Tux Racer from 52 to 59. X-Moto,
Neverball and Armagetron play above 60 fps today, Extreme Tux Racer at 59 (not a vsync cap). Their
open items (repeats, a longer Neverball replay, ETR's GART growth) are in
[backlog/games.md](../backlog/games.md). All four are in `ps3-games`.

## X-Moto

`xmoto`, `xmoto-data` (`0.6.3+repack-1+ps3.N`, package directory `gamedb/games/xmoto/packages/xmoto/`), replacing Debian's.

| Patch | What | Switch |
|---|---|---|
| 0001 | DrawLib's GL state set only when drawing; its primitives batched per state run | `XMOTO_GL_BATCH` |
| 0002 | the level's blocks and edges drawn from one buffer | `XMOTO_GL_BLOCKS` |
| 0003 | `XMOTO_BENCH_LOOPS=N` plays the benchmark replay N times | |
| 0004 | batches written straight into a mapped 2 MiB GL_STREAM_DRAW buffer instead of client memory Mesa copies per draw (`u_upload_data` was 5.5% of the CPU, 3.9 of it the minimap's circles) | `XMOTO_GL_STREAM=1` (off by default); `XMOTO_GL_STATS` |
| 0005 | game controllers: a fixed scheme next to the keyboard (below) | |

Controllers (0005): controller N (the N-th SDL game controller) plays player N, no binding needed, at
the same time as the keys. Right trigger or R1 accelerate, left trigger or L1 brake, d-pad or left
stick lean, Cross turns around, Triangle restarts the level, Square restarts from the checkpoint,
Start pauses. The table is `padBindings` in `Input.cpp`; Options > Controls > Controller lists it.
Pads plugged in later take the first free player; controller events are always on, whatever
"Enable joysticks" says. Menus take the d-pad, left stick, Cross and Circle (X-Moto's own).

0001-0002 took a frame from 18,500 GL calls and 1,960 draws to 165 and 40 (host count), picture
identical. `gamedb/games/xmoto/packages/xmoto/test/glbatch-test.sh` checks the GL layer on the PC's llvmpipe: 400 frames of a
random scene, the same pixels with and without the stream buffer, no GL errors (`make test
ONLY=xmoto-glbatch`, needs `make xmoto-work`).

**Recipe** `gamedb/games/xmoto/benchmark/recipe.sh`: the "credits" replay, which ships inside `xmoto.bin` where
`--replay` doesn't read it, so `assets/xmoto-credits.rpl` is it extracted (MD5 05a7200d...). `xmoto
--nowww -fs -res 1920x1080 --replay ... --timedemo --benchmark` draws every frame, prints `Average
framerate` and exits; it sets no swap interval itself, so `vblank_mode=0`. The replay runs in real
time (about 144 s; `--timedemo` only lifts the frame cap), so two passes (`XMOTO_BENCH_LOOPS=2`): the
first is the warm-up, the second covers the window. Variants: `kick100`, `autooff`, `idxoff`,
`fpvram`, `idxlate` (Mesa switches), `glstats`, `nobatch`, `noblocks`, `stream`, `streamstats`.

**Results** (row 68, kernel .433, x3): base 160.7 steady, 1% low 106.7, no gameplay stalls, longest
43 ms, 51 MB free, 37 MB of VRAM, 148 underruns; `stream` 162.1 (+1%), 106.3, 0.4 s, 380 ms. Row 64
(one run each): base 160.4 against `stream` 163.1, longest 404 against 146 ms, CPU 298 against
282 s. So the stream buffer gives about 1% and less CPU, and the hitches move from run to run: no
default change yet. Picture right; the engine sound is quiet under the music, not missing.
Memory: 49 MB Rss, nothing swapped, 0.3 MB free in its heap (nothing to cut).

History: Mesa ps3.246 35 to 37 (HUD median, underruns 208 to 126); ps3.262 39 to 47 (+21%); ps3.299
44.5 against 47.0 on ps3.282 the same boot (-5%: neither 0027 nor 0030 off restored it; 0031 off
(`fpvram`) and 0028 off (`idxlate`) gave 46 each); `kick100` cost 9% on ps3.282. Then the batching:
44.5 to 166.5 (row 49, `nobatch` against base). Since the steady state, X-Moto's `fps` value is the
game's average over the whole replay, not the HUD median (`--hud xmoto` keeps the old meaning).

On the SSD root with ps3rsx's `shadow_fast` (row 75, x2 against off, kernel .472): 159.0 against 158.4
steady, 1% low 97.5 against 98.1, no stalls in either, longest frame 59 against 44 ms, client CPU
301 against 303 s: no effect. The microphone hears the game's music and engine in all 8 clips
(-45 to -48 dBFS, the band under 300 Hz 38-47%, against the room's -50.5), with 200-218 ALSA
underruns a run.

## Extreme Tux Racer

`extremetuxracer`, `extremetuxracer-data` (`0.8.4-2+ps3.N`, package directory `gamedb/games/etr/packages/etr/`), replacing
Debian's. SFML 3.1 already has its big-endian Ogg fix. Patches 0001-0006 send fewer, larger GL
submissions, each behind a switch (`ETR_GL=0` turns all off): a bind cache and the HUD numbers
(`ETR_GL_BIND`, `ETR_GL_HUD`), the terrain from buffer objects in one quadtree walk
(`ETR_GL_TERRAIN`), Tux's spheres from cached vertex arrays and the shadow in one draw (`ETR_GL_TUX`,
`ETR_GL_SHADOW`), track marks as arrays of what the camera sees (`ETR_GL_TRACKS`), trees and items in
one draw per texture (`ETR_GL_TREES`, `ETR_GL_TREES_GROUP`), snow spray, falling snow and curtains in
one draw (`ETR_GL_SNOW`, `ETR_GL_PARTICLES`). A frame went from 19,400 GL calls and 697 draws to 850
and 72.

**Game controller** (patches 0007 and 0008, tried on the PS3 by the user): SFML 3 reads
`/dev/input/js*`, so a button is the joydev index. DualShock 3 (hid-sony): 0 Cross, 1 Circle,
2 Triangle, 3 Square, 4 L1, 5 R1, 6 L2, 7 R2, 8 Select, 9 Start, 13-16 d-pad up/down/left/right; axes X/Y are the
left stick; L2/R2 are buttons, not axes. `State::Manager` turns controller events into `State::Pad`;
the default is the menu (d-pad and left stick are the arrow keys, Cross and Start Enter, Circle
Escape), so every screen the keyboard reaches works with the pad. In a race: left stick (analog) or
d-pad steer, R2/R1 paddle, L2/L1 brake, Cross jump (hold to charge), Square trick, Triangle change
view, Select pause, Start the race's menu (what Escape does: the race ends), so the whole game is
navigable with the pad. The keys keep working and nothing is written to the options file. ETR has one
player, so any pad plays it; hot-plug is SFML's own events. The scheme is on the Help screen (main
menu, "Keyboard functions") in a second column. The old code took any axis but X for paddle/brake
and buttons 0-3 for the race; both are gone.

**Recipe** `gamedb/games/etr/benchmark/recipe.sh`: ETR has no demo mode, so the user's race was recorded as
keyboard events (`evemu-record`, variant `record`) and is replayed into a virtual keyboard
(`evemu-device`, `evemu-play`) from when ETR's window appears (a marker event starts the recording
there). `assets/etr-race.evemu`: Practice, one course, 69 s, the menus up to 24.7 s. The race is raced
again after the game-over screen from `ETR_LOOP_FROM` (24.7 s) up to `ETR_LOOPS` times. fps from the
HUD window (60 s from the first frame, then 120 s, `WINDOW_STOP=1`). ETR's physics take the frame time
as their step and the inputs replay at wall-clock times, so at another frame rate Tux's line can drift
from the recorded one, more in later races; the window also holds the game-over and selection
screens and the reload between races. Variants: `gloff`, `noterrain`, `notux`, `noshadow`,
`notracks`, `notrees`, `treesorder`, `noparticles`, `nosnow`, `nohud`, `nobind`.

**Results** (row 69, batch 17): 59 steady, 2.9 s of stalls (11 frames over 100 ms), 1% low 27.3,
longest 483 ms, 29 MB free, 58 MB of VRAM, no underruns. Run uncapped it is still 59, so it is not a
vsync cap. The batching: 52 to 59, CPU 83.6 to 53 s (row 49, `gloff` against base, same boot).
Picture and sound right.

**Sound buffer**: ETR plays through SFML 3.1 (miniaudio), which opens the ALSA device with a 64 frame
period and 3 periods, a 4 ms buffer; ETR cannot change it (SFML does not expose it). The played
session (2026-10-24) logged 608 xruns, 7.5 audible-dropout marks a minute, which the player did not
hear. `libsfml` (package directory `packages/patched/audio/libsfml/`, `3.1.0+dfsg-4+ps3.N`) now opens it with a 1024 frame
period: about 64 ms of buffer at 48 kHz, 24 KB, a sound effect or the music starting at most 23 ms
late in the mix. `SFML_AUDIO_PERIOD_FRAMES=<32..16384>` overrides it (64 is upstream's). Not yet
tried on the PS3: the next played session should show the xrun count.

ETR's memory grows: over a 117 s window its heap went 17.5 to 19.9 MB and its GART 8.8 to 12.8 to
16.8 MB (161 to 163 handles), two new buffers of about 4 MB, one a minute. The candidate is the track
marks in the snow, a growing list drawn through our stream buffers; a soak tells a cap from a leak
(PLAN 2.7). Its CPU outside Mesa is mostly freetype and harfbuzz (29% of samples) and SFML audio.

History: Mesa ps3.246 46 to ps3.262 53 HUD (+15%); ps3.299 52; first runs 2026-10-16 (row 19).

## Neverball

`neverball`, `neverputt` and their data (`1.6.0+git20180603-3+ps3.N`, package directory
`gamedb/games/neverball/packages/neverball/`; Debian's 2018 snapshot of 1.6), replacing Debian's.

| Patch | What | Switch |
|---|---|---|
| 0001 | bench mode: a replay prints its frames, seconds, fps and longest frame at its end and the game quits | `NEVERBALL_BENCH=1` |
| 0002 | the replay played N times, one line per pass | `NEVERBALL_BENCH_LOOPS=N` |
| 0003 | fewer GL calls and draws: one vertex and element buffer per solid file and one for the five item files, items drawn in a row, the background billboards transformed on the CPU and drawn per material run (76 to 13 draws), no depth toggles around empty passes, one texture unit where one is used | `NEVERBALL_GL_MESH`, `_ITEMS`, `_BACK`, `_STATE`; `NEVERBALL_GL=0` all off |

On llvmpipe at GL 2.1 with a fixed 33 ms clock, 0003 took `gui/demo1.nbr` from 2154 to 844 calls and
137 to 74 draws a frame (demo2 2916 to 1285; the level intros greed 6289 to 1915 and mover 8205 to
2463; Neverputt 565 to 431). Pixels identical except BACK's rounding at cloud edges (demo1: 163
pixels over 7 frames, 1 level, one pixel 11); all switches off is the old build's exact call stream.

Neverball found three of the driver bugs: GL_LUMINANCE_ALPHA sampled with L and A swapped, GPU
mipmap generation into 1-pixel levels, and the 0x105 graph exception from an unaligned mip readback
that wedged the RSX ([README.md](README.md#driver-bugs-the-games-found)). VBOs and point parameters
are required with no fallback.

**Recipe** `gamedb/games/neverball/benchmark/recipe.sh`: the shipped replay `gui/demo1.nbr`, real-time paced,
about 20 s, played 20 times (`NEVERBALL_BENCH_LOOPS=20`; passes take 12-16 s on the PS3 and 12 once
ended before the window). `~/.neverball/neverballrc`: fullscreen 1920x1080, vsync off, the fps
counter on. Variants: `nomip`, `gloff`, `nomesh`, `noitems`, `noback`, `nostate`.

**Results** (row 54, batching on against `gloff`, same boot): steady 66.5 against 62.4, smooth fps
78.7 against 79.1, CPU 65 against 80 s, stalls similar (51 frames over 100 ms against 39.5, 1% low
41.3 against 48.1, longest 3.9 and 3.7 s). 58 MB free, 34 MB of VRAM, 118 underruns. Neverball's
stalls are mostly mid-replay, not the restart ([STALLS.md, "Gameplay-only columns"](../STALLS.md#gameplay-only-columns)).
A 20 s replay is too short a window to trust; the user plays about 60 s for a longer one (PLAN 3.10).

History: before the steady state, 70-76 fps by its own count with 320-380 ms stalls at the start;
Mesa ps3.246 78.2, ps3.262 75.9 (74, 78: noise). No wedge since ps3rsx 0.249 (row 35).

## Armagetron

Debian's `armagetronad` 0.2.9.3 with two patches of ours (`gamedb/games/armagetronad/packages/armagetronad/`): its textures are RGBA8 and the
big-endian risk (GL_RGBA or GL_BGRA picked from the SDL surface's red mask) didn't bite. Picture and sound right.

- **0001, fullscreen by default.** The compiled-in default was already fullscreen at the desktop's
  resolution (read at run time, so a 720p TV gets 720p). The 640x480 window came from `lastSuccess`, the
  first fallback when a start fails or is cut short before the display is up: it began as a 640x480
  window, and a fresh configuration saved that. It now starts as the default. Only defaults change: a
  saved mode in the player's `user.cfg` is read over them as before, and the file is never edited. The
  cause on the PS3 is read from the code, not yet seen running: if a first start still opens a window,
  the first attempt at the desktop mode is failing and the game's log says why.
- **0002, game controllers.** A fixed scheme next to the keyboard, nothing bound by hand and nothing
  written to the configuration: controller N plays local player N (up to 4). D-pad left/right or L1/R1
  turn (one turn per press), R2 or Cross brake (held), right stick left/right glance, Triangle switches
  the camera, Select shows the score table (held), Start opens the game menu. In menus the D-pad moves
  and changes, Cross selects, Circle and Start go back. The buttons are read through SDL2's game
  controller API (a DualShock 3 is "PS3 Controller" there, so no raw button numbers are relied on): the
  game is SDL 1.2 on sdl12-compat, whose SDL2 is already loaded, and `uGamepad.cpp` looks the dozen
  functions up with dlopen. The pads become key events at the input queue, so recordings and the
  keyboard are unaffected. Hot-plug: rescanned every second, a new pad takes the first free player.
  Settings > Miscellaneous > Game Controllers shows the scheme (read-only). Not yet tried on the PS3.

**Recipe** `gamedb/games/armagetronad/benchmark/recipe.sh`: a local game of 4 AI players with the player
spectating, round after round (`SPECTATOR_MODE_1` and the `SP_*` settings), fps from the HUD window
(60 s from the first frame, then 120 s, `WINDOW_STOP=1`). The match limits are raised. On a first
start 0.2.9 shows a language menu, player setup and welcome screen that wait for a key (the run of
2026-10-18, row 58, sat at the language menu: its 183 fps were the menu). The live variants write
`FIRST_USE 0` and the language, and `during()` types Return, Down, Return into a virtual keyboard:
"Play Game", then "Local Game". The rounds are alike but not identical (the AIs decide live). Until
2026-10-18, base played back `assets/armagetronad-bench.aarec` (`--playback --benchmark`), a 90 s
recording cut by the record run's stop: after its end the game draws nothing, so the window held
about 6 s of frames. That is variant `playback` now. Variants: `playback`, `record`, `live`, `texinv`,
`relocs`, `kick150`, `kick100`, `kick50`.

**Results** (row 60, one run): 83 steady, 1% low 24.1, one 127 ms frame in 2 minutes, 58 MB free,
28 MB of VRAM, 289 underruns. Not comparable with the playback numbers before it.

History, all on the playback (HUD median): 60 on Mesa ps3.246 (59, 61 same boot), 52.5 on ps3.262
(-13%; not 0020: `texinv` 52), 50 on ps3.282, 60.5 with relocations or `kick50`/`kick100` on ps3.282,
61.5 on ps3.299 (row 47). The ps3.262 loss was the missing CPU/GPU overlap once 0019 dropped the
relocation-limit submissions (a frame reached the GPU only at swap); 0027's adaptive submission
fixed it. In the first profile (Mesa ps3.223) "other kernel" was 21% of its CPU.

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md).
