# Games on the PS3

These are the games we run on the RSX and how well each plays today, at 1920x1080 through Mesa nv30
and our `ps3rsx` driver ([glossary](../README.md#glossary)). All 13 games of the benchmark suite run
with the right picture. Six reach 60 fps or more with little stalling (QuakeSpasm, Yamagi Quake II,
X-Moto, Armagetron, Neverball, OpenArena opengl1), Extreme Tux Racer and OpenArena opengl2 sit at
56-60, and the rest (ClassiCube, dsda-doom, Luanti, BananaBread, SuperTuxKart, OpenTTD) are held
back by the PPE or by memory. The per-game files below hold the packages, patches and history; the suite
runner is in [SUITE.md](SUITE.md), what is still to do in [backlog/games.md](../backlog/games.md).

## Every game at a glance

Numbers are from `bench/results/` (row N = fps.csv row N, the newest same-configuration row
that measured the game; TESTS.md names the batch). "Stalls" is gameplay stall time in the 120 s
window (frames over 100 ms), "1% low" is 1000 / the 99th percentile frame time, "longest" the longest
gameplay frame. Memory is the lowest MemAvailable during the run and the game's peak VRAM.

### The suite (13 games, OpenArena's two renderers as two rows)

| Game | Runs | Steady fps | Stalls / 1% low / longest | Free RAM / VRAM (MB) | Picture, sound | Next |
|---|---|---|---|---|---|---|
| [QuakeSpasm](quake.md) | yes | 91.0 (row 58) | 2.5 s / 48.6 / 393 ms | 44 / 49 | right, right | nothing open |
| [Yamagi Quake II](quake.md) | yes | 68.7 (row 57, the small PRNG table, now the default) | 0.1 s / 31.1 / 91 ms | 46 / 48 | right, right (our fix) | nothing open |
| [X-Moto](light-games.md) | yes | 160.7; 162.1 with the stream buffer (row 68) | 0.0 s / 106.7 / 43 ms | 51 / 37 | right, right (engine quiet under the music) | stream buffer default ([backlog](../backlog/games.md)) |
| [Armagetron](light-games.md) | yes | 83, live AI rounds (row 60, one run) | 0.1 s / 24.1 / 127 ms | 58 / 28 | right, right | a live repeat (PLAN 2.6) |
| [Neverball](light-games.md) | yes | 66.5 (row 54) | 51 frames over 100 ms / 41.3 / 3.9 s (a 20 s replay, looped) | 58 / 34 | right, 118 underruns | a longer replay (PLAN 3.10) |
| [OpenArena, opengl1](openarena.md) | yes | 70.6 (row 59) | 0.0 s / 34.6 / 56 ms | 26 / 39 | right, right | nothing open |
| [Extreme Tux Racer](light-games.md) | yes | 59 (row 69; not a vsync cap) | 2.9 s / 27.3 / 483 ms (the window holds the game-over screen and a reload) | 29 / 58 | right, right | GART growth in a soak (PLAN 2.7) |
| [ClassiCube](classicube.md) | yes | 46.1-48.1 on the SSD root (rows 71, 75); 45.9 on USB, 48.1 with `chunks1` (row 68) | 0.0 s / 30.7 / 65 ms | 53 / 60 | right, state RUNNING, mic hears the room | `chunks1` default; GPU-bound (PLAN 3.2) |
| [dsda-doom, GL](doom.md) | yes | 55.9 (row 61) | 0.0 s / 40.3 / 34 ms | 43 / 65 | right, **music chops** (465 underruns a minute with the synth on the SPEs, row 52) | the chopping's cause (PLAN 2.8, D3) |
| [Luanti](luanti.md) | yes | 46 on the SSD root (row 71); 45.5 on USB (row 67) | 3.5 s (2.4-5.8) / 19.1 / 724 ms (SSD root) | 19 / 63 | right, plays with no underruns (not judged by ear yet) | `view40` as an option (PLAN 2.9) |
| [BananaBread](sauerbraten.md) | yes | 39.1 on the SSD root (rows 71, 75); 38.5 on USB (row 70); 45.1 with `shortnormals` (row 69) | 0.05 s / 25.6 / 103 ms (SSD root); 1.6 s / 24.1 / 459 ms (USB) | 25 / 117 (73-78 with force-DXT `opaque`, row 74) | right, 60-181 underruns a run, mic hears only the room | `shortnormals` default, compression at upload (PLAN 2.4) |
| [SuperTuxKart](supertuxkart.md) `lean` | yes | 20-22 by the HUD (rows 61, 68, 69, 71, 74) | 0.2-3.6 s / 10.2-12.8 / 108-513 ms | 26 / 97 | track draws; `sound=RUNNING` since the SSD root, OpenAL buffer warnings in half the runs | sound by ear (PLAN 2.1), then `lean` as default (D4) |
| [OpenArena, opengl2](openarena.md) `gl2min` | yes | 60.4 on the SSD root (row 71), 56 later in that boot (row 74); 55.9 on USB (row 70) | 0.2 s / 25.6 / 199 ms (SSD root) | 15 / 119 (98 with `md3pack`) | right, right (mic hears it) | compression at upload (a no-op here), `md3pack` default |
| [OpenTTD](openttd.md) | yes (our GL driver) | 11 on our 1962 savegame (rows 78-80); 34-43 with the 8bpp-optimized blitter (now the package default); 21 at the title screen (row 61) | 52 s / 5.7 / 311 ms (32bpp-anim); 0.4 s / 13.4 / 210 ms (8bpp) | 17 / 18 (41 free with 8bpp) | right (same picture either blitter), **choppy** (3500-4700 underruns; the music costs about 20% of the fps) | the 8bpp package on the PS3, Mesa 0040 A/B, the music (PLAN 2.8) |

SuperTuxKart's own "Average FPS" line (105-111 for `lean`) is its physics tick rate, 120 Hz times the
race time over the wall time, not a frame rate; the Gallium HUD's 19-22 frames a second is. fps.csv and
fps_steady.csv hold the HUD's median for it ([supertuxkart.md](supertuxkart.md#the-two-frame-rates)).

### Smoke-tested (one launch each, `SUITE=0`, no benchmark)

| Game | Result | File |
|---|---|---|
| Teeworlds 0.7.5 | runs with our big-endian fix (+ps3.422): in game on dm1, 183 fps (row 62) | [smoke-tested.md](smoke-tested.md) |
| SuperTux 0.6.3 | runs: in the level, keys typed through XTEST, 63-66 fps | [smoke-tested.md](smoke-tested.md) |
| Red Eclipse 1.6.0 | runs at lean settings and 11 kHz mono sound: in the match on torus, no OOM, 86 fps HUD peak | [sauerbraten.md](sauerbraten.md) |
| Frozen Bubble 2.212 | runs: level 1, right picture, 45 fps | [smoke-tested.md](smoke-tested.md) |
| Chocolate Doom 3.1.1, Crispy Doom 7.1, Woof 16 | run: Freedoom 2 MAP01, right colours, 35 / 35 / 34 fps (the vanilla 35 Hz cap) | [doom.md](doom.md) |
| Pingus 0.7.6 | runs: the tutorial level's start screen, 39 fps | [smoke-tested.md](smoke-tested.md) |
| TORCS 1.3.7, Trigger Rally 0.6.6, Chromium B.S.U. | menu only: installed on the PS3, waiting for the user at the controller (PLAN 1.11) | [smoke-tested.md](smoke-tested.md) |
| Nexuiz Classic 2.5.2 on our `darkplaces-gl2` | built, not installed (about 800 MB with its data; D12, PLAN 1.12) | [smoke-tested.md](smoke-tested.md) |

Sound was not confirmed by ear in any smoke run (PLAN 2.1).

### New games: first runs (batch ssdroot3, 2026-10-19)

Seven of the nine recipes of 6d9fcaf ran once (120 s, 1920x1080, SSD root): all start.

| Game | Result | Next |
|---|---|---|
| [Simutrans](smoke-tested.md#new-games-first-runs) | the new map, a credits dialog on top, HUD 25 fps, 34 MB free | dismiss the dialog (recipe fixed), then play |
| Freeciv | starts, shows the pregame chat window instead of the map, 15 MB free | the server's start script was wrong (fixed), re-run |
| Dungeon Crawl Stone Soup | still on "Loading maps..." at 60 s | a 300 s run |
| LinCity-NG | main menu, fullscreen | menu only: needs the user |
| ScummVM | Beneath a Steel Sky's intro, 56 MB free, sound plays | the intro skip (recipe fixed), then queen, drascula, lure |
| Flare | title scene, HUD 59 fps, language dialog | menu only: needs the user |
| Warmux | main menu in a small window | fullscreen in the config (recipe sets it), needs the user |
| Widelands | exited at start (`--fullscreen` takes no value, fixed) | re-run |

Cataclysm: Dark Days Ahead was dropped (its data package is not installable on ports). OpenRCT2 has a
data package tool (`gamedb/games/openrct2/licensed/rct2-data/rct2-data.sh` makes `rct2-data` from the user's own RCT2 CD, the licensed
suite only) and a recipe; its baseline is in [openrct2.md](openrct2.md#on-the-ps3-batch-ssdroot8). Details in [smoke-tested.md](smoke-tested.md#new-games-first-runs).

## How to read the numbers

Every number is measured the same way (the measurement protocol; [SUITE.md](SUITE.md) has the
commands):

- **1920x1080, picture checked.** A run with a black or wrong picture doesn't count. Since
  2026-10-20 the check is the PS3's own screenshots: smoke runs take them by default (`xwd` at the
  recipe's shot points after the load or the recipe's own marker), and `make bench-pull` puts each run's
  captures next to the PC reference of the same recipe (llvmpipe at GL 2.1) in `<run>/shots-vs-pc.png`,
  which someone reviews ([SUITE.md, "Screenshots"](SUITE.md#screenshots)). Bench runs take none unless
  `SHOTS` is given, until the cost of a capture is measured; a run with a capture in its window is
  `@shot` and not pooled with the others. Before that, and still for the TV and the sound,
  `tools/bench-cam.sh` photographs the screen 30 s and 60 s into every run. Step 0's first numbers
  (rows 1-10) were taken with nothing on screen and are void. Since 2026-10-19 04:25 `tools/bench-audio.sh` also
  records 8 s of the webcam microphone after every photo (`build/bench-cam/audio.log`); whether a
  game sounds right is still the user's call by ear. The room's floor in those clips is rms about
  -50.5 dBFS, spectral flatness 0.03, a peak at 445 Hz and the bands 11/83/5/0%; a clip with that
  signature has no game audible above the hum (BananaBread's 20 of 20, most of SuperTuxKart's,
  Luanti's and ClassiCube's), clips of OpenArena, X-Moto and dsda-doom sit 2-7 dB above it with another
  spectrum. The mic is a floor check, not a judge. bench.txt's `sound=` and `underruns=` lines are
  what the PS3 itself sees.
- **Desktop stopped, swaps emptied.** `ps3-bench.sh` stops the desktop before each run, so no game
  mode. Players start games from the desktop and get game mode instead, which has not been
  benchmarked yet (PLAN 2.2). The numbers compare builds; whether a player sees the same is open.
- **Warm-up and window.** The first 60 s after the load ends are left out (`WARMUP`), then at least
  120 s are measured (`MEASURE`). Steady fps is the median over the window: the game's own counter
  over its counted passes where it has one, else the HUD's per-second fps. A stall is a frame over
  100 ms. For a game that loops a demo, the gameplay columns leave out the reload at each pass
  (OpenArena's map reload, Neverball's restart; [STALLS.md, "Gameplay-only columns"](../STALLS.md#gameplay-only-columns)).
- **Same-boot A/B only.** ClassiCube gave 50.1 fps on one boot and 46.5-46.9 on another with the
  same Mesa builds, and 48.1 then 46.1 inside the SSD boot of 2026-10-19 with nothing changed. A change is measured against its off switch in the same session, interleaved;
  numbers from different boots are a regression check, not a comparison.
- **The kicker.** Rows 56-67 carry `wkkick10` in their column names: a 10 ms cross-CPU kick
  (`WAKER=10`, `WAKER_MODE=kick`, ps3-waker) that works around the lost decrementer of an idle PPE
  thread, the kernel bug behind many "random" stalls. Batches 16 onward (rows 68-70) run kernel
  .433 or later with the fix (local/0009) and no kicker. `rec10` means the stall recorder ran
  (`STALLS=1`), which itself shortens some stalls ([STALLS.md, "The recorder removes stalls"](../STALLS.md#the-recorder-removes-stalls)).
- **Interference.** Every run since batch 15 has the interference detector: a key press, a VT
  switch or an SSH login during a run marks it, and report.py and matrix.py leave it out. Runs
  before it were scanned afterwards and 10 were dropped.
- **USB root against SSD root.** Rows up to 70 ran with the root on the USB disk (28 MB/s, about
  20 ms a random read). This console's internal disk is an upgraded SSD (a stock PS3 has a 2.5" HDD);
  the batches of 2026-10-19 onward (rows 71-75) run with the root on it (`root=/dev/mapper/ps3root` in
  bench.txt), which helps the disk-bound phases more than a stock console could: loads 39-61% shorter,
  BananaBread's gameplay stalls 1.6 s to 0.05 s, frame rates unchanged
  ([GAMEOS-HDD.md](../GAMEOS-HDD.md#result-the-ssd-root-batches-ssdroot1-and-ssdroot2)). The
  comparison is cross-boot until the alternating boots are run.
- **Profiled runs** (`PERF=1`, `MEM=1`, `PERF_LOAD=1`) are for profiles, not for fps.
- Single runs vary: OpenArena gave 23 to 54 fps and QuakeSpasm 66 to 84 in single runs on Mesa
  ps3.262, and BananaBread's first-person demo swung 23-38 by itself in rows 64-65. A few fps between
  single runs is noise.

## What holds for every game

**Fewer GL calls in the game beat driver work.** Batching inside X-Moto took it from 44.5 to 166.5
fps (18,500 GL calls and 1,960 draws a frame down to 165 and 40) and Extreme Tux Racer from 52 to
59 (19,400 calls to 850). The Mesa rounds 0015-0031 gave +5 to +24% in most games. Batching only
pays where the PPE is the limit: ClassiCube's batching halved its CPU but lost 4% because the RSX
became the limit, and OpenArena opengl1's was neutral, its lightmap atlas even costing about 10 s
of hitches a run. So a profile comes first ([HOTPATHS.md](../HOTPATHS.md)).

**Memory limits the heavy games.** Linux has 217 MB of RAM; the VRAM swap (ps3vswap) holds about
96 MB of the RSX's VRAM, so a game's textures push swap pages out to the USB disk, and every page
read back is a stall. The fixes that removed the most stalls were in the games' own memory use:
OpenArena's texture keep and preload (gameplay stalls 9-10 s to 0), Yamagi's PRNG table (70 MB less
RAM, stalls 22.6 s to 0.05 s), OpenArena opengl2's vertex colours (stalls 9-11 s to 1-2 s),
BananaBread (Debian's cube2) no longer OOM-killed. Where each game's memory goes is in [MEMORY.md](../MEMORY.md).

**Vertex formats nv30 can't fetch cost twice.** u_vbuf converts them on the CPU at every draw, and
nouveau then keeps a malloc copy of every buffer it read. Sending formats nv30 fetches natively fixed
OpenArena opengl2 (ioquake3 0027) and gave BananaBread +17% (cube2 0011, `vanormalshort`).

**The CPU goes to the driver stack.** Every game is PPE-bound, and in the first profiles (Mesa
ps3.223) Mesa took 28-56% of a game's CPU and ps3rsx 6-16%, against 4-11% for the game itself.
The shared costs were ps3rsx's validation and state shadow, fence reference counting, nv30's
per-draw validation and relocation bookkeeping; the cross-game ranking is in
[HOTPATHS.md](../HOTPATHS.md).

**The disk is a third of every load.** Disk wait is 34-41% of the load's wall time in the four
bigger games (35% in ClassiCube), in reads of 28-40 KB at 6-8 MB/s
([PROFILING.md, "Start-up profiles, batch 17"](../PROFILING.md#start-up-profiles-batch-17)).

**Big-endian bugs live in games and libraries, not the driver.** Seven so far, all fixed in our
packages: dsda-doom (colours, its sound lumps, screenshots), Yamagi Quake II (OpenAL white noise),
SDL3 (window framebuffer colours), LuaJIT (state allocation), SuperTuxKart (.spm meshes), Teeworlds
(`FIONBIO` given a long), Luanti (Ogg Vorbis byte order).

## Driver bugs the games found

All fixed; no open GPU bug blocks a game (the TXB fix is built, its PS3 check pending).

| Bug | Games it blocked | Fix |
|---|---|---|
| L8A8 (GL_LUMINANCE_ALPHA) sampled with L and A swapped (gl-texfmt) | Neverball, fonts in many games | Mesa 0012 (ps3.223) |
| GPU mipmap generation of non-square textures: 1-pixel levels are swizzled render targets the validator refuses, the push is lost (gl-genmip) | Neverball, SuperTuxKart, Luanti | Mesa 0013 (ps3.223) |
| Graph exception 0x105: nv30's 3D blit sampled a mip level at an offset not 128-byte aligned (Mesa's CPU mipmap generation reading back the 2x2 level of a 32x32 L8A8 texture) | Neverball; any game generating mipmaps for L8A8, L8, A8, I8, 4444, 5551 | Mesa 0014 (ps3.225), gl-mipread passes all 70 cases; ps3rsx 0.249 refuses texture offsets not 128-byte aligned (8e99b76) |
| SDL3's accelerated window framebuffer was BGRA8888 on big-endian: a blue cast for apps writing 0xAARRGGBB words | OpenTTD, any SDL window-surface app | libsdl3 +ps3.242 (colours), then +ps3.254 (0002: ARGB/XRGB8888 uploaded natively with GL_BGRA + 8_8_8_8_REV; OpenTTD 2 to 8 fps) |
| Debian's LuaJIT on ppc64 allocated between 2 and 4 GB, which it then refuses: `luaL_newstate` failed about 40% of the time | Luanti | our luajit (331c539); Luanti's 330 unit tests pass |
| No ARB_texture_rg on nv30 | dsda-doom GL, OpenTTD GL | a GL_LUMINANCE_ALPHA fallback in our dsda-doom, GL_LUMINANCE8 in our openttd |
| Every X after the first started black: ps3rsx's reflip muted the video for an active CRTC with no framebuffer, and ps3fb's console blank muted it before X owned the GPU | every bench run | a1f1b91, 8baa747 (ps3rsx 0.219) |
| No window manager under the bench's bare X: SDL fullscreen windows were never shown | all SDL games | client.sh starts Openbox (fa723f9) |
| The RSX's fragment TXB (texture2D with a bias argument) samples wrong whatever the bias: dark, flat 3D (Warzone's stripes diagnostic: the same lookups as TEX right, as TXB with bias 0 or -20 wrong) | Warzone 2100 (all its 3D shaders); any GLSL or ARB program that passes a bias | Mesa 0045 (TXB emitted as TEX, the bias dropped; `NV30_TXB_HW=1` for tests), and Warzone patch 0009 (ps3.633) for Mesa builds without it; not yet on the PS3 |

Silent failures count as failures: a fragment program over nv30's limits keeps the previous one
bound ("out of temps" on stderr) and a vertex program over them falls back to the CPU, so smoke runs
grep the game's stderr for `nouveau` and watch for a sudden slowdown. Driver features games would
use (ARB_texture_rg, indirect constant addressing for GPU skinning, ARB_shader_texture_lod,
ARB_framebuffer_object) are in [backlog/graphics.md](../backlog/graphics.md).

## The per-game files

- [openarena.md](openarena.md): OpenArena on our ioquake3, both renderers.
- [quake.md](quake.md): QuakeSpasm and Yamagi Quake II.
- [sauerbraten.md](sauerbraten.md): BananaBread (our cube2) and Red Eclipse, the same engine family.
- [luanti.md](luanti.md): Luanti with LuaJIT and Minetest Game.
- [supertuxkart.md](supertuxkart.md): SuperTuxKart's legacy renderer.
- [classicube.md](classicube.md): ClassiCube and its timedemo plugin.
- [doom.md](doom.md): dsda-doom with Freedoom, plus Chocolate, Crispy and Woof.
- [openttd.md](openttd.md): OpenTTD and its GL driver on GL 2.1.
- [light-games.md](light-games.md): X-Moto, Extreme Tux Racer, Neverball, Armagetron.
- [smoke-tested.md](smoke-tested.md): Teeworlds, SuperTux, Frozen Bubble, Pingus, the menu-only games,
  Nexuiz Classic, the new games, and the games we skipped.
- [SUITE.md](SUITE.md): the runner, recipes, assets, reports and how a game joins the suite.
- [LOG-FINDINGS.md](LOG-FINDINGS.md): the errors and warnings in the run logs, by game and by cause, and the tool that finds them (`bench/logscan.py`).
- [gamedb/research/game-candidates.md](../../gamedb/research/game-candidates.md): the 2026-10-16 survey that picked
  the suite.

## History

- **2026-10-16, stage 0.** The runner and its first two recipes (OpenArena, ClassiCube). Two display
  bugs went first, both found because the screen stayed black (the missing window manager, ps3rsx's
  muted video). Validation: OpenArena tc1 42.2 fps, ClassiCube 50.5 HUD median, picture on camera.
- **2026-10-16/17, first runs.** Every game of the suite ran within two days, after six big-endian
  bugs in games and libraries and five driver bugs. Most played at 35-50 fps, CPU-bound in the driver
  stack; four rendered smoothly between multi-second memory stalls.
- **2026-10-17, Mesa rounds.** ps3.246 (0015-0017): +2 to +9%. ps3.262 (0019-0025, fewer words per
  draw): OpenArena tc1 +23%, Yamagi +14-24%, X-Moto +21%, ETR +15%, QuakeSpasm +11%, Armagetron -13%
  (no CPU/GPU overlap without relocations). ps3.299 (0027-0031): 0027's adaptive submission took
  Armagetron back to 61.5. A fixed `kick100` was rejected because it cost X-Moto 9%.
- **2026-10-17 evening.** Game-side batching beat the driver: X-Moto 3.7x, ETR +13%.
- **2026-10-18.** The stall recorder, the lost-decrementer kernel fix, the memory patches in the games
  and the interference detector; the steady-state protocol replaced the old 90 s cap.

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md),
[archive/PLAN.md](../archive/PLAN.md) ("Per game status").
