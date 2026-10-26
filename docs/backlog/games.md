# Backlog: per-game work

Per-game items not done: results that rest on one run, sound that is missing or chops, smoke runs
that need the user, engine patches described but not built. Each game's measured state is in
[games/README.md](../games/README.md) and its own page under [games/](../games/README.md). Items are
in rank order; the rank is the one in [the ranked list](README.md). Terms:
[glossary](../README.md#glossary).

Effort: S = a day or less, M = days, L = weeks.

### The single-run results, repeated

Rank 8. PLAN 2.6's repeats: each one decides a default or a table row on one run.

| Repeat | The single run | Row |
|---|---|---|
| BananaBread `oqlag` (cube2 0010 `oqwaterlag 1`: this frame's water query counts only once its result is in) x3 | 39.4 steady / 43.5 smooth against base 25.0 / 30.3 | 64 |
| Luanti `view40` x3 | 59 fps, closer fog, 0.9 s of stalls | 63 |
| Extreme Tux Racer's steady-state row with stalls | batch 17 has one uncapped run: 59 fps, 2.9 s of gameplay stalls, longest frame 483 ms, 1% low 27.3 | 69 |
| Teeworlds, a second run | 183 fps on dm1, smoke only | 62 |
| Armagetron live AI rounds | 83 fps, one 127 ms frame in 2 min | 60 |
| A BananaBread first-person baseline with the translate LUT re-A/B'd | the LUT's +53% (25.9 -> 39.6) was on the earlier spectator demo; the first-person demo swings 23-38 fps by itself | 57, 64-65 |

`oqlag` matters most: the water query wait was 10.3% of BananaBread's CPU, and Mesa's own fix for
it (0035 `NV30_QUERY_WAIT`) gave no fps in its repeat (row 65), so the engine-side change is the one
left. ClassiCube `chunks1` and X-Moto's stream buffer were repeated in batch 16.

- **Payoff:** medium: defaults and table rows on solid ground. **Effort:** M (runs only).
- **Risk:** none. `oqlag` changes pixels in one case (the first frame water shows again keeps its
  old reflection for that frame).
- **Needs:** PS3. **Status:** scheduled: TESTS.md "Next" (PLAN 2.6 repeats).
- **Source:** PLAN 2.6; [archive/HOTPATHS.md, "Measured on the PS3"](../archive/HOTPATHS.md#measured-on-the-ps3-2026-10-18-dated-note).

**To start.** (BananaBread's recipe is still called `sauerbraten`.) `/var/tmp/bench/run.sh bench sauerbraten "base oqlag oqnowait" 3` (`oqnowait` is the
stock engine's `oqwait 0`, for comparison), `/var/tmp/bench/run.sh bench luanti "base view40" 3`,
with `STALLS=1`; the first-person demo's spread needs three rounds or more for small differences.

### SuperTuxKart's missing sound

Rank 9. SuperTuxKart's runs logged `sound=none` until 2026-10-18 and `sound=RUNNING` on the SSD root.

**Why.** The `sound=` line is read at 15 s (70 s since 2026-10-19), and the game opens its audio device
late on a slow root, so the old `none` was probably that timing: the ten runs of 2026-10-19 (load
31 s) all say RUNNING, OpenAL logs its start-up messages, the profile has ALSA and OpenAL threads.
What is open is whether it sounds right: the webcam mic heard only the room's hum in 14 of 17 clips,
and OpenAL logs "Wait timeout... buffer size too low?" 83-244 times in half the runs. A game without
audible sound isn't playable as shipped, and decision D4 (SuperTuxKart's `lean`
as the package default: 21.4 smooth fps and 2.4 s of stalls against 13 fps and 13.5 s, row 61)
waits on it. The rest of its state: `lean` holds 95-96 MB of VRAM with "out of VRAM" lines left and
runs at a 1% low of 11.4 with a 686 ms longest frame (row 68); its warm-up had one 10 s frame
(batch 17).

- **Payoff:** medium (correctness). **Effort:** S to check, M if OpenAL fails on big-endian.
- **Risk:** low. **Needs:** the user by ear (a normal start, not profile mode).
- **Status:** the device state is RUNNING; the sound by ear is still unchecked; scheduled: TESTS.md
  "Next" (sound by ear, with the user).
- **Source:** PLAN 2.1, D4; [archive/RELEASE-CANDIDATES.md, "2.4 Games"](../archive/RELEASE-CANDIDATES.md#24-games).

**To start.** Start it from the desktop as a player would and listen; `tools/webcam-audio.sh` from
the PC when the user isn't at the TV (the bench's own microphone check, `tools/bench-audio.sh`, is
off until the user says the room is free for it). If it is silent outside profile mode too, OpenAL
Soft's log (`ALSOFT_LOGLEVEL`) and an `strace` of the device open say which side fails.

### Luanti: view40 as an option, and its memory

Rank 19. What is left of PLAN 2.9 after the clean runs.

**Why.** The clean re-runs kept `base` as the default (row 67: 1.5 and 2.1 s of gameplay stalls,
45-46 fps); `cliunload` is worse and `lean` draws a black void. Batch 16 found the cause in `lean`:
`chunk3` (`chunksize = 3`) empties the world, while `srvunload` and `active2` render but stall more
(6.3 and 6.7 s against ~2.5, row 68). Left:

- `view40` (59 fps with closer fog, one run, row 63) as a menu option, once its repeat holds;
- the memory: 102 MB resident plus 47 MB swapped, 14 malloc arenas with 22.9 MB free in them, 12.6-18
  MB of RAM free at the lowest (rows 63, 68). `LU_TRIM_MB=16` was rejected (more stalls, row 57);
  `MALLOC=arena2` and `MALLOC=tune` never had a clean run; of the ~20 recipe variants only base,
  cliunload, lean, chunk3, srvunload, active2 (clean) and view40, nomerge (one run each) have numbers;
- the load: about 13 of its 17 s of load disk wait are major faults on mappings the recorder can't
  name (see [smoothness.md, "Gaps in the measurement tools"](smoothness.md#gaps-in-the-measurement-tools)),
  and its bench mod reads `/proc/self/status` wrongly.

- **Payoff:** medium. **Effort:** S for the option, M for the memory.
- **Risk:** low. **Needs:** PS3, host for the menu entry.
- **Status:** `view40`'s repeat is scheduled: TESTS.md "Next"; the rest never tried.
- **Source:** PLAN 2.9; [archive/MEMORY.md, "Cuts, ranked"](../archive/MEMORY.md#cuts-ranked) #2, #6, #7.

### Red Eclipse engine patches

Rank 28. cube2's memory patches carried over to Red Eclipse, and its sounds loaded when first
played.

**Why.** Red Eclipse runs (86 fps HUD peak, no OOM, row 62) only at lean settings with 11 kHz mono
sound. Its memory is the sound: `addsound` loads each sample at registration and SDL_mixer decodes
it whole into PCM (the game's ~190 samples at the start, and every `mapsound` of a map: torus has 74
ambience files, 24 MB of .ogg). On the host (Debian's amd64 build, llvmpipe, which keeps textures in
RAM, so the numbers run high): 534 MB RSS after loading torus at 44.1 kHz stereo, 381 MB with no
sample loaded, 397 MB with `soundmono 1` + `soundfreq 11025`, 326 MB with `maxtexsize 256` +
`texreduce 3`. Described, not built: samples loaded when first played instead of at registration
(most ambience is never heard), cube2 0001 (compression; it applies to Red Eclipse's tree, and
`usetexcompress` is read-only without it), 0002/0003 (shader variants), 0005 (lightmap CPU copies),
the sky cube map shrunk while its six sides are decoded.

- **Payoff:** medium: full sound and settings. **Effort:** M.
- **Risk:** low. **Needs:** host, PS3. **Status:** never tried.
- **Source:** PLAN 3.8; [archive/GAME-PLAN.md, "Smoke fixes"](../archive/GAME-PLAN.md#smoke-fixes-2026-10-18).

### Extreme Tux Racer's last frame to 60

Rank 29. ETR sits at 59 fps, and batch 17 showed it is not a vsync cap.

**Why.** Uncapped (`vblank_mode=0`, 52477a2), ETR still runs at 59 (row 69). In the HOTPATHS
profile 45.6% of its CPU is outside the game and Mesa: freetype and harfbuzz 29%, SFML audio. Nobody
has looked at what draws text every frame. Its GART grows by ~4 MB a minute (the soak item in
[smoothness.md](smoothness.md#long-session-soak)).

- **Payoff:** low-medium (the 60 fps target for a light game). **Effort:** S to look.
- **Risk:** none. **Needs:** host, PS3. **Status:** never tried (a lead from the profiles, not a
  proposal from any doc).
- **Source:** [archive/HOTPATHS.md, "Per game: CPU by class"](../archive/HOTPATHS.md#per-game-cpu-by-class).

### OpenTTD's music and its gameplay benchmark

Rank 32. The choppy music, then the frame rate on a real savegame.

**Why.** OpenTTD with our GL driver draws right at 21 fps (software 8-12), with 6.3 s of stalls,
17 MB free and 3893 underruns: the music chops (row 61). Its MIDI goes through the same SDL_mixer
path as dsda-doom's ([spu.md, "dsda-doom's chopping music"](spu.md#dsda-dooms-chopping-music)). The
title-screen demo is a weak benchmark; a gameplay benchmark (a developed map, fixed camera, sound and
music on) is being built. Batching its GL driver is
[graphics.md, "Game-side batching in the remaining games"](graphics.md#game-side-batching-in-the-remaining-games).

- **Payoff:** medium. **Effort:** M. **Risk:** low.
- **Needs:** PS3, the user by ear.
- **Status:** the savegame benchmark ran (batch ssdroot3): the 8bpp-optimized blitter takes it from 11 to
  34-43 fps and is the package default since patch 0003 (not deployed); the music still costs about
  20% and chops. Left: TESTS.md Next (`blit8` repeats, Mesa 0040), the music (PLAN 2.8).
- **Source:** PLAN 2.8, 3.1; [archive/PLAN.md, "Per game status"](../archive/PLAN.md#per-game-status).

### Smoke runs: menu-only games, Nexuiz, the new games

Rank 33. Games that need someone at the controller, or that are new.

- **TORCS, Trigger Rally, Chromium B.S.U.**: installed on the PS3, they start in a menu. TORCS also
  tests DXT5. `SMOKE_SECS=300 BENCH_TIMEOUT=420 ./run.sh smoke torcs`.
- **Nexuiz Classic** on our `darkplaces-gl2` (the last DarkPlaces with GL 1.x/2.0 renderpaths):
  built, never run. Needs `darkplaces-gl2 nexuiz-data nexuiz-textures` (~800 MB on the
  disk, decision D12), then `run.sh smoke nexuiz`, then variants `gl20` and `hires`. Risks in order:
  the GL 2.0 path's shaders on nv30 (`vid_gl20 0` first), VRAM and RAM with its maps, sound byte
  order, the generic (non-SSE) skeletal code's CPU.
- **The new games** (recipes in 6d9fcaf): ScummVM (sky, queen, drascula, lure), Simutrans,
  Crawl tiles, Freeciv, LinCity-NG, Flare, Widelands, Warmux, Cataclysm tiles; `gamedb/games/openrct2/licensed/rct2-data/rct2-data.sh`
  builds a licensed `rct2-data` deb from the user's RCT2 CD (the licensed suite only).

- **Payoff:** more games for the suite. **Effort:** S. **Risk:** a graph exception ends the session:
  the heavy ones last.
- **Needs:** the user for the menu games and Nexuiz; PS3 for the new games (an apt install between
  batches).
- **Status:** the new games' first runs are done (smoke-tested.md "New games: first runs"); re-runs,
  the menu-only games and Nexuiz are in TESTS.md Next.
- **Source:** PLAN 1.11, 1.12; [archive/GAME-PLAN.md, "Smoke tests"](../archive/GAME-PLAN.md#smoke-tests-not-benchmarked), ["Nexuiz Classic"](../archive/GAME-PLAN.md#nexuiz-classic-on-darkplaces-gl2-built-2026-10-18-not-run-on-the-ps3).

### Games to smoke next, and Wesnoth's music

Rank 76. Novel games that have never been started on the PS3 (the user's order: novel first).

- **GAME-REEVAL's "try now" list** ([research/GAME-REEVAL.md](../research/GAME-REEVAL.md), "Merged
  ranking"): DevilutionX (shareware `spawn.mpq`), fheroes2 (HoMM2 demo data), UFO:AI (ppc64 binary
  in main, est. 400-900 MB, a candidate band), ET:Legacy (measure fps first), EDuke32 (Duke3D
  shareware), VVVVVV (private build only, licence). Also Micropolis (packaging only). With the
  user's own data: Exult, OpenXcom/OXCE (vanilla first), iortcw.
- **Xonotic 0.8.6**: the release engine still has the GL 2.0 path (the GLES2 path of engine master
  is no way out). Package it as `xonotic-gl2` after Nexuiz runs on `darkplaces-gl2`; 1.2 GB of
  data, RAM is the risk. Steps in [research/gl3-census/README.md](../research/gl3-census/README.md)
  ("2. Xonotic 0.8.6").
- **Wesnoth's music package** is not installed on the PS3: game.log is full of `cannot open track
  '*.ogg'` and there is only the effects' sound ([games/smoke-tested.md](../games/smoke-tested.md),
  Wesnoth). Install the music deb, then repeat the `ai` smoke run and listen (the user, by ear).

- **Payoff:** more games for the suite. **Effort:** S each. **Needs:** PS3; disk space first (see
  "The SSD root image is nearly full" in [platform.md](platform.md)). **Status:** not started.

### Neverball: a longer replay

Rank 40. A replay of about 60 s played by the user.

**Why.** The shipped replay is ~20 s, and its start-up stutters weigh too much in it: 51 frames over
100 ms in 20 s (row 54). Batching cut its CPU by 19% (80 -> 65 s); 66.5 fps steady, 118 underruns.

- **Effort:** S. **Needs:** the user (records the replay), then PS3. **Status:** never tried.
- **Source:** PLAN 3.10.

### BananaBread: texreduce

Rank 42. The game's own `texreduce 1` (recipe variant `tex1`).

**Why.** BananaBread (Debian's cube2) holds 117 MB of VRAM (rows 68-70), about 87 MB of it textures; DDS and JPG at a
quarter would take ~45-60 MB off, at the cost of blurrier textures. The variant exists since
2026-10-17 and has no row. It is the fallback if texture compression at upload
([memory.md](memory.md#texture-compression-at-upload-for-every-game)) takes little here (cube2
already compresses from 1024 up).

- **Effort:** S. **Risk:** the picture (the user decides). **Needs:** PS3.
- **Status:** never tried.
- **Source:** [archive/MEMORY.md, "Cuts, ranked"](../archive/MEMORY.md#cuts-ranked) #5.

### OpenAL's stuck mixer: the sound card's DMA stops

Rank 72. In about 10% of OpenAL runs the sound stops for the rest of the run.

**Why.** [LOG-FINDINGS.md, item 1](../games/LOG-FINDINGS.md#1-the-sound-cards-dma-stops-and-openals-mixer-waits-for-good):
snd_ps3's interrupt-driven DMA chain stops while the card stays RUNNING (dmix never lets it xrun), so
OpenAL Soft waits on dmix's period timer for good and logs `Wait timeout... buffer size too low?` once a
second (SuperTuxKart, OpenArena, ClassiCube, Luanti). It probably explains the silent microphone in
SuperTuxKart ([rank 9](#supertuxkarts-missing-sound)), and every OpenAL game's CPU number is lower in a
wedged run.

- **To start.** Kernel patch local/0047 (snd_ps3 DMA watchdog, `snd_ps3.dma_watchdog_ms`), the test
  module from `make snd-ps3-ko REL=6.12.111+ps3.472`, then the A/B in [TESTS.md](../TESTS.md) "Next".
  The harness records `snd_stall=`, `oal_wait=`, `oal_xrun=` and `snd_restarts=` in every run.
- **Payoff:** sound in four games (and any other game when the card stops); honest numbers.
  **Effort:** done on the PC; the PS3 test is about an hour.
- **Risk:** low (the watchdog acts only when the interrupts stopped). **Needs:** PS3, the user's OK
  to install the module (or a reboot into a kernel with the patch).
- **Status:** built, not run on the PS3.
- **Source:** logscan.py over 602 runs; the DUMPON captures of 2026-10-19.

### BananaBread data files, and a clean SuperTuxKart config

Rank 73. Two small data checks the logs found.

**Why.** [LOG-FINDINGS.md, items 6 and 7](../games/LOG-FINDINGS.md): BananaBread logs `could not load
texture packages/gk/fantasy/skyfantasy/skyfantasy` (73 of 74 runs) and `could not load envmap
skyboxes/morning` (65 runs); the benchmark map may draw without its sky and reflections. SuperTuxKart
prints 30 config errors a run because the recipe writes a partial `config.xml`.

- **To start.** List those paths in the installed cube2 data; if they are absent, find which package
  has them; look at a photo of the BananaBread run for the sky. For SuperTuxKart let the game write its
  default config once and keep that as the recipe's base.
- **Payoff:** a picture closer to the PC's; a log that shows real errors. **Effort:** S.
- **Risk:** the picture changes (the user decides). **Needs:** host, PS3 for the photo.
- **Status:** never tried.

### Mesa nv30 messages in the logs

Rank 74. Nothing in 602 runs reports a shader compile failure or a fallback, because Mesa is silent.

**Why.** [LOG-FINDINGS.md, "The tool"](../games/LOG-FINDINGS.md#the-tool): an "out of temps", TGSI or
draw-module fallback would cost fps without leaving a line. One run per game with `NV30_DEBUG`
(or `MESA_DEBUG=1`, `GALLIUM_DEBUG`) into game.log, then `logscan.py -c nouveau,gl`.

- **Payoff:** finds fallbacks we cannot see; upstream material. **Effort:** S. **Risk:** none.
- **Needs:** PS3. **Status:** never tried.

## Measured, waiting for a default decision

These ran and won (or lost) since the docs were written; what is left is to change a default, or
decide not to. They are not ranked.

| Switch | Result | Row |
|---|---|---|
| ClassiCube `CC_GL_CHUNKS=1` (the faces upstream draws, ranges merged) against 2 | 48.1 against 46.0 steady (+4.7%, x3), after +5% in a single run; HOTPATHS recommends it as the default | 64, 68 |
| X-Moto `XMOTO_GL_STREAM=1` | 162.1 against 160.6 (+1%); the longest gameplay frame went the other way in batch 16 (380 against 43 ms) after 146 against 404 ms in row 64 | 64, 68 |
| cube2 `vanormalshort 1` (world normals as shorts) | 45.1 against 38.8 steady in the matrix (the row's note: 38.7 -> 45.4, +17%), 1% low 30.4 against 25.0, stalls 1.6 against 1.2 s, VRAM +4 MB; pixels differ slightly from the stock build | 69 |
| ioquake3 `r_md3PackedFrames 1` (`gl2min_md3pack`) | VRAM 117.1 -> 97.7 MB, 59.0 fps, stalls 1.3 s against 2.4 | 69 |
| ioquake3 `r_mipMapPowf` (`gl2min_mippowf`, 0028 off) | the lowest stalls of the batch, 0.8 s, 60.0 fps, one run: 0028 should change only the load's CPU, so this needs a repeat before reading anything into it | 69 |
| Mesa `NV30_SYSMEM_COPY=2` + `NV30_INVALIDATE_BUFFER=1` on renderergl2's old 16-bit colours (`gl2min_color16`) | gameplay stalls 11.1 -> 1.4 s, swap stalls 22.9 -> 0.9 s, 53.7 -> 59.1 fps; gl2min itself no longer needs it (0027 is the default), so the question is a default for games we don't patch | 69 |

## Not pursued

- **dsda-doom's software renderer**: the GL renderer is the way (38 fps software).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md),
[archive/PLAN.md](../archive/PLAN.md), [archive/IDEAS.md](../archive/IDEAS.md),
[archive/MEMORY.md](../archive/MEMORY.md).
