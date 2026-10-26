# SuperTuxKart

SuperTuxKart 1.5 runs on its legacy GL 2.1 renderer with our big-endian fixes: karts and track draw.
With the `lean` settings it races at 19-22 fps by the Gallium HUD (the frame rate: the game's own
"Average FPS" of 105-111 is its physics tick rate, [below](#the-two-frame-rates)) with 0.2-3.6 s of
gameplay stalls; the game's defaults give about 13 fps and 13.5 s of stalls. Runs from 2026-10-17
to 10-18 logged `sound=none`, the ten of 2026-10-19 on the SSD root `sound=RUNNING`: the read at 15 s
came before the game opened its audio device on the slow USB root (the recipe reads at 70 s now).
Whether it sounds right is for the user's ear; the webcam mic heard only the room's hum in 14 of 17
clips, and OpenAL logs "Wait timeout... buffer size too low?" 83-244 times in half the runs, so the
sound check (PLAN 2.1) still comes before `lean` can become the package default (D4). Open items:
[backlog/games.md](../backlog/games.md).

## Packages and patches

`supertuxkart`, `supertuxkart-data` (`1.5-2+ps3.N`, package directory `gamedb/games/supertuxkart/packages/supertuxkart/`), replacing
Debian's: Debian's quits while loading on big-endian. `supertuxkart-data` is Debian's data, rebuilt
because `supertuxkart` needs it at its own version. 839 MB with the data (752 MB of it data), so it
deploys by name (`make deploy PKGS="ps3-games supertuxkart supertuxkart-data"`) and isn't in
`ps3-games`. The release candidate is ps3.361.

| Patch | What |
|---|---|
| 0001 | the .spm mesh and armature loader byte-swaps the little-endian data instead of refusing to run (upstream #5578 was closed as not planned). All 1268 .spm files parse identically on amd64 and ppc64 |
| 0002 | textures and vertex colours in the right channel order on the OpenGL driver |
| 0003 | legacy renderer: no crash when render-to-texture fails, a power-of-two (512x512) minimap render target |
| 0004 | texture compression reaches the legacy renderer: opaque power-of-two textures as DXT1, compressed by Mesa at upload |
| 0005 | `STK_PROFILE_WARMUP=S`: profile mode restarts its frame count and clock once the race passes S seconds and ends the race at S + the profile time |
| 0006 | defaults of a fresh configuration (a key already in `config.xml` keeps its value): the `lean` graphics (HD textures off, important particles only, drivers not animated, no minimap), texture compression on (the game's default), old-driver popup off, fullscreen at the desktop's resolution (`real_width`/`real_height` 0 = the desktop, resolved in `IrrDriver::initDevice`) |
| 0007 | game controllers like a console game: a fixed scheme as the default bindings of any pad with the usual buttons, a second binding per action, a hot-plug scan |

## Controls

The bindings of a pad come from its SDL game controller mapping (`GamepadConfig::initSDLMapping`),
once, when STK first sees that pad; 0007 rewrites that table to the scheme below. A pad that already
has an entry in `~/.config/supertuxkart/config-0.10/input.xml` keeps it (delete the entry, or the
file, to get the new scheme). The keyboard is its own device and works at the same time.

| Pad (DualShock 3) | Race | Menus |
|---|---|---|
| R2 or R1 | accelerate | |
| L2 or L1 | brake / reverse | |
| left stick or d-pad | steer | move |
| Cross | skid | select |
| Square | fire the item | |
| Circle | nitro | back |
| Triangle | look back | |
| Select | rescue | |
| Start | pause | select |

STK allows one binding per action, so 0007 adds a second one (`DeviceConfig::setAltBinding`; saved
in `input.xml` as a second `<action ... alt="true">`, listed as "Right trigger / Right shoulder" in
Options > Input; setting a binding by hand clears the second). A pad with fewer buttons gets the
game's older default. STK's own multiplayer screen assigns devices as before, one pad per player.
Hot-plug: `InputManager::update` looks every 2 s for a pad SDL lists that isn't open (the "added"
event could be lost, or the open could fail at that moment), and the added event skips one already
open. Not yet tried on the PS3.

## The recipe

`gamedb/games/supertuxkart/benchmark/recipe.sh`: profile mode (`--profile-time`) starts a race at once with
every kart driven by the AI, on lighthouse with 4 karts (`STK_TRACK`, `STK_KARTS`), fullscreen
1920x1080, `--no-start-screen`. With `STK_PROFILE_WARMUP=60` and `--profile-time=180` it drives 240 s
and prints `Number of frames: N time T, Average FPS: F` over the 180 s after the warm-up (a tick
count, not frames: [below](#the-two-frame-rates)). The load ends at the "Track: Overall scene complexity" line. GL 2.1 picks the legacy
renderer by itself ("Using the fixed pipeline"). setup() writes the whole config
(`~/.config/supertuxkart/config-0.10/config.xml`) every run: base is the game's defaults with HD
textures on (up to 2048x2048, `max_texture_size` 512 when off), compression on, the minimap shown,
vsync and the old-driver popup off. The race is AI-driven (`rand()` seeded from the clock,
frame-time physics): close between runs, not identical. Its GPU timers are garbage on the RSX
(timer queries return a pixel count).

Variants: `nohd`, `tex256`, `notc`, `nomap`, `noparticles`, `static` (drivers not animated: the
legacy renderer skins every animated kart on the CPU), `geo0`, `vsync`, `karts1`, and `lean` (HD
textures off, no minimap, important particles only, static drivers: the candidate default).

## Current results

| Configuration | Steady fps (HUD) | Gameplay stalls / 1% low / longest | Free RAM / VRAM | Row |
|---|---|---|---|---|
| base (defaults) | 12.6 | 13.5 s / 8.4 / 676 ms | 24 / 130 MB | 61 |
| `lean` | 21.4 | 2.4 s / 12.2 / 663 ms | 19 / 96 MB | 61 |
| `lean` (batch 16, 6 runs) | 19.1-21.0 | 1.3-2.7 s / 11.4-12.1 / 430-690 ms | 22-27 / 95-96 MB | 68 |
| `lean`, start-up profile | 19.4 | 2.0 s / 11.5 / 691 ms | 23 / 96 MB | 69 |
| `lean`, SSD root (n=3: one baseline, two off arms of the force-DXT A/B) | 20 (20-22) | 2.0 s (0.2-3.6) / 11.1 / 448 ms | 26 / 97 MB | 71, 74 |
| `lean`, `NV30_FORCE_DXT=opaque`, SSD root (x2) | 21 | 1.7 s / 11.6 / 385 ms | 25 / 97 MB | 74 |

Three more SSD-root runs (0101, 0106, 0112) were excluded as interfered (an SSH login and a polling
loop); the one clean run of that batch had 22 fps, 0.2 s of stalls and a 108 ms longest frame, the
two force-DXT off arms 20 fps and 2.0-3.6 s, so STK's stalls vary more than the n allows to rank.

### The two frame rates

The game's own "Average FPS" for `lean` (105-111) is not a frame rate; the HUD's 19-22 is. The
`--profile-time` race prints `Number of frames: 21602 time 196.2, Average FPS: 110.1`, and the
number of frames is 21602 in every run: patch 0005 counts `ProfileWorld::update` calls, one per
physics tick, and STK steps physics at 120 Hz, so 180 s of race is 21600 ticks whatever the renderer
does. The "Average FPS" is therefore 120 times the race time over the wall time (0.88-0.92 of real
time: the race runs a little slower than a clock when frames are long), i.e. a simulation speed.
The HUD counts frames that reached the screen: 5436-5692 swaps in a 300 s run, 3.8-4.0 physics ticks
per frame, 19-22 fps. The recipe no longer reads the game's line as fps
(`FPS_RE` and `STEADY_RE` removed, 2026-10-19): fps and steady come from the HUD (as the `base` row
always had them), and `parse()` keeps
`stk_ticks`, `stk_tick_s`, `stk_tickrate` in bench.txt. The `fps` and `fps_steady` values of
gamedb:batch/2026-10-18-batch8, gamedb:batch/2026-10-18-batch16 and gamedb:batch/2026-10-18-batch17 were
corrected to the HUD medians (they held 106-111).

Picture: the track draws (webcam, rows 61 to 75: the kart, the lighthouse track, the HUD); "out of
VRAM" lines remain in the log.

The load is the longest of the suite: 54.3 s on the USB root (22.5 s of CPU and 18.6 s of disk wait on
the main thread, half of the CPU in decoders: png, zlib, jpeg and audio 12.4 s of 24.7 s). On the SSD
root it is 31.1 s (30.5-31.5; 31.5 s to the end of the loading screens): the main thread's disk wait
fell from 10.9 s to 3.3 s, its CPU stayed at 23.8 s, of which png 4.2, zlib 3.9, audio 2.2, jpeg 2.1
and the game's own code 5.4 s, so decoders are now 53% of the load's CPU
([PROFILING.md](../PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)). The first minute's
stalls fell from 18.5 s to 8.8 s. Game prefetch left the USB load unchanged (row 68).

Gameplay profile (row 73): the game 36.5% (the main thread: Irrlicht's driver, Bullet), Mesa 35.5%,
libc 15.1%, the kernel 5.1%, ps3rsx 2.6%, ALSA and OpenAL about 3% ([HOTPATHS.md](../HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b)).

**Mesa 0038 on `lean`** (row 74, x2 against off): `opaque` forced 0 textures (182 kept for alpha, 173
for their format, 22 small, 6 non-power-of-two; the game's own compression already takes its opaque
ones), VRAM 98.5 against 97.3 MB, fps and stalls equal inside the noise. `=all` (DXT5 for the alpha
ones) is not run yet ([MEMORY.md](../MEMORY.md#measured-on-the-ps3-batch-ssdroot2-p2)).

Next: the sound by ear, then the `lean` default, the legacy renderer's batching (PLAN 3.1), a VRAM
measurement (PLAN 2.3); see [backlog/games.md](../backlog/games.md).

## History

- 2026-10-16 (research): the FAQ asks for at least 1 GB of RAM against our 217 MB; the old powerpc
  colour bug (#714519).
- 2026-10-17: Debian's build stopped at its mesh loader refusing big-endian (e708512). Our +ps3.264
  loaded: karts rendered, the track didn't (VRAM exhausted, 210 failed buffer allocations, the minimap
  FBO incomplete, SIGSEGV at exit). ps3.306 with 0003-0004: VRAM 210 to 132 MB (98 MB with `nohd` or
  `lean`), HUD 13-19 fps.
- 2026-10-18: the track draws (row 61).
- 2026-10-19: the SSD root (rows 71-75): load 54 to 31 s; the HUD's frame rate confirmed against the
  game's tick rate; `sound=RUNNING`.

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md).
