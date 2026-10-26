# Games on the PS3

These are the games we run on the RSX, at 1920x1080 through Mesa nv30 and our `ps3rsx` driver
([glossary](../README.md#glossary)), and what holds across them. Everything about one game lives in its
GameDB bundle ([gamedb/README.md](../../gamedb/README.md)): its packages and patches, its settings, how it
performs and what was tried in `gamedb/games/<id>/technical.md` (the published games' notes are on the
site, `https://walnut.nanobyte.dev/gamedb/technical/`), its dated investigations in `investigations/`, its
open work in `todo.toml`. This page keeps the cross-game findings; the suite runner is
[SUITE.md](SUITE.md), the errors in the run logs [LOG-FINDINGS.md](LOG-FINDINGS.md).

## Every game at a glance

The benchmark suite's steady state (desktop stopped, vsync off, a 60 s warm-up and a 120 s window): the
summary batch `suite-summary-2026-10` (gamedb:batch/suite-summary-2026-10) gathers each game's newest
same-configuration numbers, every value with its source batch; the medal comes from the game's played
sessions. "Stalls" is gameplay stall time in the 120 s window (frames over 100 ms), "1% low" is 1000 / the
99th percentile frame time, "longest" the longest gameplay frame. Memory is the lowest MemAvailable during
the run and the game's peak VRAM.

<!-- gamedb:suite-table -->
| Game | Medal (played) | Steady fps | Stalls / 1% low / longest | Free RAM / VRAM (MB) | Picture, sound |
|---|---|---|---|---|---|
| [X-Moto](https://walnut.nanobyte.dev/gamedb/games/xmoto/) | Platinum · 96 | 160.7 | 0 s / 106.7 / 43 ms | 51 / 37 | right, right |
| [OpenArena, opengl1](https://walnut.nanobyte.dev/gamedb/games/openarena/) | Gold · 82 | 70.6 | 0 s / 34.6 / 56 ms | 26 / 39 | right, right |
| [Yamagi Quake II](https://walnut.nanobyte.dev/gamedb/games/yquake2/) | Gold · 88 | 68.7 | 0.1 s / 31.1 / 91 ms | 46 / 48 | right, right (our fix) |
| [OpenArena, opengl2](https://walnut.nanobyte.dev/gamedb/games/openarena/) | Gold · 82 | 60.4 | 0.2 s / 25.6 / 199 ms | 15 / 119 | right, right |
| [QuakeSpasm](https://walnut.nanobyte.dev/gamedb/games/quakespasm/) | Platinum · 93 | 91 | 2.5 s / 48.6 / 393 ms | 44 / 49 | right, right |
| [Armagetron](https://walnut.nanobyte.dev/gamedb/games/armagetronad/) | Platinum · 95 | 83 | 0.1 s / 24.1 / 127 ms | 58 / 28 | right, right |
| ClassiCube | not rated yet | 47 | 0 s / 30.7 / 65 ms | 53 / 60 | right, right |
| [Extreme Tux Racer](https://walnut.nanobyte.dev/gamedb/games/etr/) | Gold · 80 | 59 | 2.9 s / 27.3 / 483 ms | 29 / 58 | right, right |
| [Luanti](https://walnut.nanobyte.dev/gamedb/games/luanti/) | Scrap · 56 | 46 | 3.5 s / 19.1 / 724 ms | 19 / 63 | right, no underruns |
| [dsda-doom](https://walnut.nanobyte.dev/gamedb/games/dsda-doom/) | Gold · 90 | 55.9 | 0 s / 40.3 / 34 ms | 43 / 65 | right, music chops |
| [BananaBread](https://walnut.nanobyte.dev/gamedb/games/bananabread/) | Scrap · 51 | 39.1 | 0.05 s / 25.6 / 103 ms | 25 / 117 | right, 60-181 underruns a run |
| [Neverball](https://walnut.nanobyte.dev/gamedb/games/neverball/) | Gold · 87 | 66.5 | 2 s / 41.3 / 3.9 s | 58 / 34 | right, 118 underruns |
| [OpenTTD, 8bpp](https://walnut.nanobyte.dev/gamedb/games/openttd/) | Gold · 83 | 38.3 | 0.4 s / 13.4 / 210 ms | 41 / 18 | right, choppy (3500-4700 underruns) |
| [SuperTuxKart lean](https://walnut.nanobyte.dev/gamedb/games/supertuxkart/) | Silver · 69 | 21 | 1.8 s / 11.8 / 513 ms | 26 / 97 | track draws, sound running |
| [OpenTTD, 32bpp](https://walnut.nanobyte.dev/gamedb/games/openttd/) | Gold · 83 | 11 | 52 s / 5.7 / 311 ms | 17 / 18 | right, choppy (3500-4700 underruns) |
<!-- /gamedb:suite-table -->

Light games (the Quakes, X-Moto, Armagetron, Neverball, OpenArena opengl1) reach 60 fps or more with
little stalling; Extreme Tux Racer and OpenArena opengl2 sit at 56-60; the rest are held back by the PPE or
by memory, and [ClassiCube](../../gamedb/games/classicube/) by the RSX itself. SuperTuxKart's own "Average FPS" is its physics tick rate; the
table holds the HUD's frame rate (gamedb:supertuxkart/two-frame-rates).

Games outside the suite (smoke runs, first runs, the games tried and parked) are draft bundles in
`gamedb/games/` with `status = "tried"` or `"blocked"`; each one's technical.md has what its runs showed.

## How to read the numbers

Every number is measured the same way (the measurement protocol; [SUITE.md](SUITE.md) has the
commands). "Row N" below is the batch with `order = N` in gamedb/benchmarks/batches.toml:

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
  signature has no game audible above the hum ([BananaBread](../../gamedb/games/bananabread/)'s 20 of 20, most of [SuperTuxKart](../../gamedb/games/supertuxkart/)'s,
  [Luanti](../../gamedb/games/luanti/)'s and [ClassiCube](../../gamedb/games/classicube/)'s), clips of [OpenArena](../../gamedb/games/openarena/), [X-Moto](../../gamedb/games/xmoto/) and [dsda-doom](../../gamedb/games/dsda-doom/) sit 2-7 dB above it with another
  spectrum. The mic is a floor check, not a judge. bench.txt's `sound=` and `underruns=` lines are
  what the PS3 itself sees.
- **Desktop stopped, swaps emptied.** `ps3-bench.sh` stops the desktop before each run, so no game
  mode. Players start games from the desktop and get game mode instead, which has not been
  benchmarked yet (PLAN 2.2). The numbers compare builds; whether a player sees the same is open.
- **Warm-up and window.** The first 60 s after the load ends are left out (`WARMUP`), then at least
  120 s are measured (`MEASURE`). Steady fps is the median over the window: the game's own counter
  over its counted passes where it has one, else the HUD's per-second fps. A stall is a frame over
  100 ms. For a game that loops a demo, the gameplay columns leave out the reload at each pass
  (OpenArena's map reload, [Neverball](../../gamedb/games/neverball/)'s restart; [STALLS.md, "Gameplay-only columns"](../STALLS.md#gameplay-only-columns)).
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
- Single runs vary: OpenArena gave 23 to 54 fps and [QuakeSpasm](../../gamedb/games/quakespasm/) 66 to 84 in single runs on Mesa
  ps3.262, and BananaBread's first-person demo swung 23-38 by itself in rows 64-65. A few fps between
  single runs is noise.

## What holds for every game

**Fewer GL calls in the game beat driver work.** Batching inside X-Moto took it from 44.5 to 166.5
fps (18,500 GL calls and 1,960 draws a frame down to 165 and 40) and Extreme Tux Racer from 52 to
59 (19,400 calls to 850) (gamedb:xmoto/game-side-batching, gamedb:etr/game-side-batching). The Mesa rounds 0015-0031 gave +5 to +24% in most games. Batching only
pays where the PPE is the limit: ClassiCube's batching halved its CPU but lost 4% because the RSX
became the limit, and OpenArena opengl1's was neutral, its lightmap atlas even costing about 10 s
of hitches a run (gamedb:classicube/batching-gpu-bound, gamedb:openarena/opengl1-batching-and-the-atlas). So a profile comes first ([HOTPATHS.md](../HOTPATHS.md)).

**Memory limits the heavy games.** Linux has 217 MB of RAM; the VRAM swap (ps3vswap) holds about
96 MB of the RSX's VRAM, so a game's textures push swap pages out to the USB disk, and every page
read back is a stall. The fixes that removed the most stalls were in the games' own memory use:
OpenArena's texture keep and preload (gameplay stalls 9-10 s to 0), Yamagi's PRNG table (70 MB less
RAM, stalls 22.6 s to 0.05 s), OpenArena opengl2's vertex colours (stalls 9-11 s to 1-2 s),
BananaBread (Debian's cube2) no longer OOM-killed (gamedb:openarena/reload-stalls, gamedb:yquake2/prng-table,
gamedb:openarena/opengl2-freezes, gamedb:bananabread/oom-and-the-memory-patches). Where each game's memory goes is in [MEMORY.md](../MEMORY.md).

**Vertex formats nv30 can't fetch cost twice.** u_vbuf converts them on the CPU at every draw, and
nouveau then keeps a malloc copy of every buffer it read. Sending formats nv30 fetches natively fixed
OpenArena opengl2 (ioquake3 0027) and gave BananaBread +17% (cube2 0011, `vanormalshort`; gamedb:cube2/byte-normals).

**The CPU goes to the driver stack.** Every game is PPE-bound, and in the first profiles (Mesa
ps3.223) Mesa took 28-56% of a game's CPU and ps3rsx 6-16%, against 4-11% for the game itself.
The shared costs were ps3rsx's validation and state shadow, fence reference counting, nv30's
per-draw validation and relocation bookkeeping; the cross-game ranking is in
[HOTPATHS.md](../HOTPATHS.md).

**The disk is a third of every load.** Disk wait is 34-41% of the load's wall time in the four
bigger games (35% in ClassiCube), in reads of 28-40 KB at 6-8 MB/s
([PROFILING.md, "Start-up profiles, batch 17"](../PROFILING.md#start-up-profiles-batch-17)).

**Big-endian bugs live in games and libraries, not the driver.** All fixed in our packages:
dsda-doom (colours, its sound lumps, screenshots: gamedb:dsda-doom/big-endian-and-gl-fixes), Yamagi
Quake II (OpenAL white noise: gamedb:yquake2/openal-white-noise), SDL3 (window framebuffer colours:
gamedb:openttd/gl-driver-on-gl21), LuaJIT (state allocation) and Luanti (Ogg Vorbis byte order:
gamedb:luanti/luajit-state-allocation), SuperTuxKart (.spm meshes: gamedb:supertuxkart/big-endian-port),
Teeworlds (`FIONBIO` given a long: gamedb:teeworlds/fionbio-blocking-sockets), Warzone 2100 (its DXT
encoder and its Opus samples: gamedb:warzone2100/black-3d-view, gamedb:warzone2100/opus-white-noise),
OpenRCT2 (a whole port: gamedb:openrct2/big-endian-port), Mr.Boom (its emulated 8086 registers:
gamedb:mrboom/big-endian-registers). One was the compiler: GCC 16.2's AltiVec loop vectorizer
miscompiles stb_image_resize at `-O3 -mcpu=cell` (Warzone 2100's black textures;
[PACKAGING.md](../PACKAGING.md)).

<a id="driver-bugs-the-games-found"></a>

## What the games found in the driver stack

Every investigation of a game that ended in a driver or library patch, from the investigations'
`driver_patches` (oldest first). No open GPU bug blocks a game.

<!-- gamedb:driver-bugs docs/games -->
| Bug (the investigation) | Found by | Fixed by |
|---|---|---|
| [Neverball: L8A8 swapped, 1-pixel mipmap levels, and graph exception 0x105 (2026-10-16)](../../gamedb/games/neverball/investigations/2026-10-16-driver-bugs.md) | Neverball, SuperTuxKart, Luanti | mesa 0012, mesa 0013, mesa 0014 |
| [Armagetron: Mesa ps3.262's -13%, the lost CPU/GPU overlap (2026-10-17)](../../gamedb/games/armagetronad/investigations/2026-10-17-submission-overlap.md) | Armagetron Advanced | mesa 0027 |
| [Luanti: LuaJIT's state allocated where it refuses it (2026-10-16/17)](../../gamedb/games/luanti/investigations/2026-10-17-luajit-state-allocation.md) | Luanti | luajit 0001 |
| [OpenTTD: the OpenGL driver on GL 2.1, and SDL3's blue cast (2026-10-16 to 10-18)](../../gamedb/games/openttd/investigations/2026-10-17-gl-driver-on-gl21.md) | OpenTTD | libsdl3 0001, libsdl3 0002 |
| [What stalls in OpenArena: map reloads, bots joining and the lost decrementer (2026-10-17)](../../gamedb/games/openarena/investigations/2026-10-17-reload-stalls.md) | OpenArena | linux local-0009 |
| [OpenArena opengl2 (gl2min): the 16 s freezes and the memory (2026-10-18)](../../gamedb/games/openarena/investigations/2026-10-18-opengl2-freezes.md) | OpenArena | mesa 0037 |
| [The water's occlusion query: the frame waits for the GPU (2026-10-18)](../../gamedb/engines/cube2/investigations/2026-10-18-water-query-wait.md) | BananaBread, Sauerbraten | mesa 0035, mesa 0042 |
| [The world's normals as signed bytes: a vertex conversion at every draw (2026-10-18)](../../gamedb/engines/cube2/investigations/2026-10-18-byte-normals.md) | BananaBread, Sauerbraten | mesa 0033, mesa 0037, mesa 0043 |
| [An 8 MB staging buffer every frame (2026-10-19)](../../gamedb/games/openttd/investigations/2026-10-19-staging-buffer-per-frame.md) | OpenTTD | mesa 0040 |
| [The dark 3D view, the RSX's fragment TXB, and what came after (2026-10-20)](../../gamedb/games/warzone2100/investigations/2026-10-20-dark-3d-view.md) | Warzone 2100 | mesa 0045 |
| [Extreme Tux Racer: a 4 ms sound buffer (2026-10-24)](../../gamedb/games/etr/investigations/2026-10-24-sound-buffer.md) | Extreme Tux Racer | libsfml 0001 |
| [Two crashes in Mesa: a query's report slot taken back, then a NULL upload buffer (2026-10-24)](../../gamedb/games/sauerbraten/investigations/2026-10-24-online-crashes.md) | Sauerbraten | mesa 0046, mesa 0047, mesa 0048 |
| [Garbage textures and a crash in Mesa after GART ran out (2026-10-26)](../../gamedb/games/supertuxkart/investigations/2026-10-26-fence-segfault.md) | SuperTuxKart | mesa 0049 |
| [Square particles: nv30 ignored GL_POINT_SMOOTH (2026-10-26)](../../gamedb/games/yquake2/investigations/2026-10-26-point-smooth.md) | Yamagi Quake II | mesa 0050 |
| [Particles of one size at every distance: nv30 never took the vertex program's point size (2026-10-27)](../../gamedb/games/yquake2/investigations/2026-10-27-point-size-attenuation.md) | Yamagi Quake II | mesa 0051 |
<!-- /gamedb:driver-bugs -->

Two display bugs went first, found by the harness itself at stage 0 because the screen stayed black:
every X after the first started black (ps3rsx's reflip muted the video for an active CRTC with no
framebuffer, and ps3fb's console blank muted it before X owned the GPU: a1f1b91, 8baa747, ps3rsx 0.219),
and SDL fullscreen windows were never shown under the bench's bare X (client.sh starts Openbox, fa723f9).

Silent failures count as failures: a fragment program over nv30's limits keeps the previous one bound
("out of temps" on stderr) and a vertex program over them falls back to the CPU, so smoke runs grep the
game's stderr for `nouveau` and watch for a sudden slowdown. Driver features games would use
(ARB_texture_rg, indirect constant addressing for GPU skinning, ARB_shader_texture_lod,
ARB_framebuffer_object) are in [backlog/graphics.md](../backlog/graphics.md).

## History

- **2026-10-16, stage 0.** The runner and its first two recipes ([OpenArena](../../gamedb/games/openarena/), [ClassiCube](../../gamedb/games/classicube/)). Two display
  bugs went first, both found because the screen stayed black (the missing window manager, ps3rsx's
  muted video). Validation: OpenArena tc1 42.2 fps, ClassiCube 50.5 HUD median, picture on camera.
- **2026-10-16/17, first runs.** Every game of the suite ran within two days, after six big-endian
  bugs in games and libraries and five driver bugs. Most played at 35-50 fps, CPU-bound in the driver
  stack; four rendered smoothly between multi-second memory stalls.
- **2026-10-17, Mesa rounds.** ps3.246 (0015-0017): +2 to +9%. ps3.262 (0019-0025, fewer words per
  draw): OpenArena tc1 +23%, Yamagi +14-24%, [X-Moto](../../gamedb/games/xmoto/) +21%, ETR +15%, [QuakeSpasm](../../gamedb/games/quakespasm/) +11%, Armagetron -13%
  (no CPU/GPU overlap without relocations). ps3.299 (0027-0031): 0027's adaptive submission took
  Armagetron back to 61.5. A fixed `kick100` was rejected because it cost X-Moto 9%.
- **2026-10-17 evening.** Game-side batching beat the driver: X-Moto 3.7x, ETR +13%.
- **2026-10-18.** The stall recorder, the lost-decrementer kernel fix, the memory patches in the games
  and the interference detector; the steady-state protocol replaced the old 90 s cap.

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md),
[archive/PLAN.md](../archive/PLAN.md) ("Per game status").
