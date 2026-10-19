# Backlog: stalls, loads, the disk and the measuring tools

What is left to try for smoothness: the disk behind every load, the gap between how we measure and
how players play, long sessions, and the tools that tell a stall's cause. The stalls inside
gameplay are mostly gone (the heavy games at 0.3-2.9 s of gameplay stalls in batch 17 and ssd1,
fps.csv rows 69-70); what remains is loads, the first minute of play, and memory. Items are in rank order; the
rank is the one in [the ranked list](README.md). Terms: [glossary](../README.md#glossary).

Effort: S = a day or less, M = days, L = weeks. "PS3" means a run with nobody at the console (the
measurement protocol), "user" means the user at the console or TV.

## Loads and the disk

### SSD root against USB root

Rank 2. The whole system on the SSD image (Phase B), measured against the USB root in alternating
boots.

**Why.** The disk is the largest single item in every load: disk and I/O wait are 34-41% of the
load's wall time in Sauerbraten, OpenArena gl2min, Luanti and SuperTuxKart, 35% in ClassiCube,
40-51% with the page faults (batch 17, [archive/PROFILING.md, "Where the time goes"](../archive/PROFILING.md#where-the-time-goes-measured)).
The USB disk reads 27.8 MB/s and 20 ms a random read; the internal SSD 100 MB/s and 0.1 ms. Phase
A (only the game data on the SSD, batch ssd1, row 70) left the load's USB wait at 4.8-5.5 s in
Sauerbraten, because only 16-50 MB of the 76-144 MB a load reads are game data; the binary,
libraries, Mesa and the shader cache stayed on USB. OpenArena's load still fell 28% (10.7 to
7.7 s, n=1-3). Phase B moves all of it. PROFILING.md's estimate per game: Sauerbraten L 4-6 s and
W 5-8 s, OpenArena L 6-8 and W 6-9, Luanti L 9-12, SuperTuxKart L 10-13 and W 3-4, ClassiCube L
2.6-3.5 (L = off the load, W = off the warm-up).

- **Payoff:** high for loads and the warm-up's disk stalls; the VRAM swap spills to the SSD too.
  The internal disk of this console is an upgraded SSD: a stock PS3 has a 2.5" HDD, so the gain
  doesn't carry over to stock consoles as measured.
- **Effort:** M (built; the runs and four boots).
- **Risk:** the SSD root writes inside GameOS's disk through dm-linear over the image's blocks;
  every block is stamped and rechecked at boot, and the kernel guard (local/0045) is not booted yet
  (see [platform.md, "The kernel guard on the PS3"](platform.md#the-kernel-guard-on-the-ps3)). A
  failed check stops in the rescue shell; the USB entry stays as the fallback.
- **Needs:** the user (reboots), then PS3 runs.
- **Status:** the SSD root booted on 2026-10-19 (kernel .472) and its baseline ran (batches ssdroot1
  and ssdroot2: loads 39-61% shorter, Sauerbraten's gameplay stalls 1.6 to 0.05 s;
  [GAMEOS-HDD.md](../GAMEOS-HDD.md#result-the-ssd-root-batches-ssdroot1-and-ssdroot2)). The USB/SSD comparison is scheduled: TESTS.md "Next" (needs the user; the USB root
  first needs ps3-gpu-tools 0.477~dev for the disk sampling).
- **Source:** PLAN 1.14; [archive/GAMEOS-HDD.md, "The benchmark: USB root against SSD root"](../archive/GAMEOS-HDD.md#the-benchmark-usb-root-against-ssd-root).

**To start.** Boots alternate USB, SSD, USB, SSD, with the same commands in the same order in each
boot, the measurement protocol, `tools/bench-cam.sh` on the PC, the same package versions on both
roots, nothing deployed between the boots:

    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench sauerbraten base 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench openarena gl2min 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench luanti base 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench supertuxkart lean 3
    STALLS=1 PERF_LOAD=1 /var/tmp/bench/run.sh bench classicube base 3
    STALLS=1 /var/tmp/bench/run.sh bench openarena gl2min 3
    STALLS=1 /var/tmp/bench/run.sh bench sauerbraten base 3
    ps3-bench.sh desktop

bench.txt's `root=` tells the boots apart. This is a cross-boot comparison (the exception to the
same-boot rule, kept as small as it can be): a difference counts when both SSD boots agree against
both USB boots.

### Start-up and warm-up after the disk

Rank 13. The CPU side of loads and of the first minute, once the disk is fast.

**Why.** Batch 17's start-up profiles (one run per game, kernel .433, Mesa .466) put every CPU
item at 1-3 s per game except SuperTuxKart (5-8 s). The warm-up's stall frames are large: 21.1 s in
53 frames in Sauerbraten, 25.5 s in 30 in OpenArena, 18.9 s in 24 in SuperTuxKart (one frame of
10 s). Decoders on the load's main thread: SuperTuxKart's png, zlib, jpeg and audio 12.4 of 24.7 s
(50%), Sauerbraten's zlib and audio 1.6 of 5.3 s, OpenArena's inflate, jpeg, TGA and audio 2.6 of
10.4 s. With the disk fixed, PROFILING.md expects the loads to become CPU-bound at roughly 15 s
(Sauerbraten), 21 (OpenArena), 33 (Luanti, a mapgen wait), 43 (SuperTuxKart), 11 (ClassiCube).

The candidates, estimates from batch 17's pools (none run as an A/B):

| Win | Estimate | Basis |
|---|---|---|
| zlib-ng (built: `libz-ng-compat`, opt-in `ZLIB=ng`) | SuperTuxKart L 1.8-2.7 s, Sauerbraten L 0.5-0.7 s, OpenArena 0 (already libdeflate) | zlib's part 0.31-0.56 of Debian zlib's estimated PPE cycles under qemu ([PROFILING.md](../PROFILING.md#load-time-decoders-and-linking)) |
| libpng's filters without load-hit-stores, then in AltiVec (not built: a `libpng1.6` package) | SuperTuxKart L 1.5-3 s | the filters are 57% of a PNG decode once zlib-ng is in, Paeth 47%; a register-carried Paeth cut its load-hit-stores from 1.6 to 0.12-0.29 a byte |
| libjpeg-turbo's AltiVec | already used (source checked; `JSIMD=none` on the PS3 confirms) | AltiVec halves the instructions on SuperTuxKart's JPEGs |
| Vorbis's 16-bit packing in AltiVec (built: our `libvorbis`) | SuperTuxKart L 0.6-0.9 s, part of Sauerbraten's audio thread | a third of a decode's estimated cycles was `ov_read`'s conversion |
| DT_RELR for Mesa's libraries (not built) | tens of ms per GL start | libgallium's 1.5 MB of relocations to a few KB; `-z now` is there already, so `BINDNOW=1` should change nothing |
| OpenArena's pk3s repacked stored (no inflate) and its mipmaps off the CPU | L 1.0 + 1.3 s, W 1.8 + 2.0 s | inflate 0.96 s (load) and 1.82 s (warm-up); `R_MipMapsRGB` 1.34 and 2.0 s; more bytes to read, so only after the SSD root |
| Audio decoded on first use or off the main thread | Sauerbraten W ~1.9 s, SuperTuxKart L 2.2 s | `ov_read_filter` and imdct on the main thread |
| Game-level work: Luanti's mapgen and lighting threads, SuperTuxKart's skinned meshes (`skinJoint` 1.3 s), OpenArena's `COM_ParseExt` (1.3 s, the .shader scripts) | OpenArena L ~1 s, SuperTuxKart L 1-2 s | per game |

- **Payoff:** medium: shorter loads and fewer warm-up stalls, a few seconds per game.
- **Effort:** S per measurement, M per change.
- **Risk:** low (library swaps and game patches, each behind its own A/B).
- **Needs:** host for the builds, PS3 for the runs.
- **Status:** zlib-ng (`libz-ng-compat` 2.3.3-1+ps3.549) and our libvorbis (1.3.7-3+ps3.549) are
  built with their A/Bs in [TESTS.md](../TESTS.md) "Next"; libjpeg-turbo's AltiVec selection checked
  in its source; the libpng filter patch and Mesa's DT_RELR not built. The start-up profiles on the SSD root ran (ssdroot2 P1a): the disk is
  11-26% of a load now and the CPU items are what is left ([PROFILING.md](../PROFILING.md#start-up-profiles-on-the-ssd-root-batch-ssdroot2)):
  zlib, PNG, JPEG and audio decoders in SuperTuxKart, the map inflate in Sauerbraten, dynamic linking
  at 3-9% of every load. They were taken without Mesa symbols; a symbolised re-run restores the shader and
  mipmap groups.
- **Source:** PLAN 1.7 (its follow-up); [archive/PROFILING.md, "Ranked wins"](../archive/PROFILING.md#ranked-wins-separating-measured-from-estimated).

**To start.** The A/Bs in TESTS.md "Next" (`ZLIB=ng`, `JSIMD=none`, `LDPATH=` with the stock
libvorbis), three runs each, because the load's wall time moves by 1.5-2.8 s between identical
runs. Then the libpng package: `tools/ppecost/paeth.c` is the register-carried Paeth, checked
bit-identical on SuperTuxKart's PNGs.

### Bigger reads on the USB disk

Rank 47. The kernel's readahead and `readahead(2)` on the packs, while the USB disk is the root.

**Why.** During loads the USB disk is busy 57-72% of the time but moves only 6-8 MB/s: the average
request is 28-40 KB (Luanti 28 KB) against 27.8 MB/s sequential, so it seeks between small reads
(batch 17). PROFILING.md calls `read_ahead_kb` and `readahead(2)` on the pk3s "the cheap version"
of reading ahead in big blocks. The expensive version, the game prefetch, gave no gain (batch 16,
row 68: its sorted reads compete with the game's own on this disk), so the expected payoff is
small.

- **Payoff:** low; it matters only for USB roots and stock consoles.
- **Effort:** S. **Risk:** low (a sysctl-like setting; more page cache read that may not be used).
- **Needs:** PS3. **Status:** never tried.
- **Source:** [archive/PROFILING.md, "Ranked wins"](../archive/PROFILING.md#ranked-wins-separating-measured-from-estimated), rank 2.

## What players see

### A benchmark round through game mode

Rank 7. The suite started from the desktop as players start it, with game mode, against the
desktop-stopped baselines in the same boot.

**Why.** Every number in the results was taken with the desktop stopped (`ps3-bench.sh`, the
measurement protocol), so without game mode. Players start games from the desktop and get game
mode instead, which has never been benchmarked. The numbers compare builds correctly; whether a
player sees the same, and whether game mode gives back the RAM the baselines assume, is not known.

- **Payoff:** high as a check: it says whether the measured smoothness reaches players.
- **Effort:** M. **Risk:** low (a deliberate exception to the desktop-stopped protocol, same boot).
- **Needs:** PS3 (the desktop up, nobody at it).
- **Status:** never tried; not in TESTS.md yet.
- **Source:** PLAN 2.2; [archive/PLAN.md, "A gap in how we measure"](../archive/PLAN.md#summary-2026-10-18).

**To start.** The harness has no game-mode arm yet: `client.sh` runs games under a bare X with the
desktop stopped. A round would start each recipe's game through `ps3-game` (or let
`ps3-game-detect` catch it) on the running desktop, interleaved with the plain arm, and add game
mode's own journal lines (`journalctl --user -u ps3-game-mode`) and `games/bench/gamemode-check.sh`'s
memory numbers to the run directory.

### Long-session soak

Rank 14. 30 minutes or more of one heavy game, supervised.

**Why.** The bench window is 3 minutes; players play for an hour, and nothing cools the console but
SYSCON. Two growths are known: Extreme Tux Racer's GART went 8.8 -> 12.8 -> 16.8 MB in 117 s (two
new ~4 MB buffers, one a minute; candidate: the track marks in the snow) and its heap 17.5 -> 19.9
MB; Luanti's malloc free memory grew 12.8 -> 22.9 -> 28.2 MB over three minutes (blocks and meshes
freed into the arenas and kept there). A soak tells a cap from a leak.

- **Payoff:** medium: finds stalls and swap growth that only long play shows.
- **Effort:** S-M. **Risk:** heat: the session must be supervised (temperatures through hwmon,
  the bench's `BENCH_TEMP_MAX` stop at 80 C, stop on any "PS3 Thermal Alert").
- **Needs:** PS3, the user nearby.
- **Status:** never tried.
- **Source:** PLAN 2.7; [archive/MEMORY.md, "Growth over the window"](../archive/MEMORY.md#growth-over-the-window).

**To start.** One ETR run of 10 minutes or more with `MEM=1` (the three snapshots show whether the
+4 MB a minute of GART and +1.2 MB of heap stop), then Luanti and a heavy game for 30 minutes with
`STALLS=1`; read `temps` in the run directory.

## Tools and counters

### The interference detector's own checks

Rank 25. Six deliberate touches to see that `ps3-intfwatch` catches them.

**Why.** The detector runs in every bench run since batch 15 and decides which rows count, but
nobody has checked that it catches a real key press, an SSH login or a mouse bump, or looked at
its tty8 screen and its PPE cost on the PS3 (on the host: 3 ms of CPU a second with 570 processes,
2 MB RSS). A side question from the retro-scan: if the PC's mouse is paired with the PS3's Logitech
receiver, the user's own mouse reaches the PS3.

- **Payoff:** confidence in every row since batch 15. **Effort:** S. **Risk:** none.
- **Needs:** the user at the PS3.
- **Status:** scheduled: TESTS.md "Next" (with the user).
- **Source:** PLAN 1.13; [archive/STALLS.md, "Checking it on the PS3"](../archive/STALLS.md#checking-it-on-the-ps3).

**To start.** The six steps in the source: a key press 30 s into `run.sh smoke openarena` (expect
`!! INTERFERENCE: 1 key presses`, `interfered=1`); the same run untouched (only `-` lines and the
two expected VT lines); `ssh ps3 true` during a run (a login); `run.sh smoke armagetronad` (the
evemu keyboard as a `-` line); the BENCHMARK RUNNING screen on the TV; a brief mouse move (a bump)
against 3 s of motion (counted). The detector's own cost is the log's `# self cpu=` line.

### Kernel stat options and ps3rsx counters

Rank 30. Exact stall causes instead of 10 ms samples.

**Why.** The stall recorder decides a cause from samples every 10 ms. The 6.12 kernel here has no
PSI, no delay accounting, no SCHEDSTATS and no TASK_IO_ACCOUNTING. Each would turn a sampled cause
into a measured one:

- `CONFIG_TASK_DELAY_ACCT` (with `delayacct` on the command line): per task, the exact time in
  block I/O, swap-in, direct reclaim, thrashing and compaction.
- `CONFIG_PSI`: system-wide memory, io and cpu stall time as cheap cumulative counters.
- `CONFIG_SCHED_INFO` (through SCHEDSTATS): run-queue wait per thread in `/proc/<tid>/schedstat`.
- ps3rsx: a debugfs `ps3rsx_stats` with lockless counters the recorder reads every slow tick: GART
  chunks backed and released with the ns in `chunk_back` (cma_alloc plus the 1 MiB clear), fence
  waits in GEM_CPU_PREP and the pushbuf's sync wait, waits for the primitive owner (`prim_wq`),
  VRAM swap shrink passes. The vswap `stats` file could read its counters with `READ_ONCE` instead
  of `vs->lock`, so it can be sampled every 10 ms.

- **Payoff:** medium: the VRAM budget item and every stall analysis get exact numbers.
- **Effort:** S-M. **Risk:** the options' RAM cost needs measuring
  ([KERNEL-MEMORY.md](../KERNEL-MEMORY.md)); a debugfs file needs `DEBUG_FS`, which the
  "last few hundred KB" memory item would drop.
- **Needs:** host, then a reboot.
- **Status:** never tried (proposals only).
- **Source:** PLAN 3.11; [archive/STALLS.md, "What the kernel and the driver could add"](../archive/STALLS.md#what-the-kernel-and-the-driver-could-add).

### Mesa 0039: nv30's shader cache A/B

Rank 35. nv30's own translations (`nir_to_tgsi` and the nvfx code) in the disk shader cache.

**Why.** Built (Mesa 0039, `NV30_SHADER_CACHE=on`, default off; compile-tested only). Batch 17
bounds what it can save: the `shader.backend` seconds over load and warm-up, all threads, are 0.28
(OpenArena), 0.39 (Sauerbraten), 0.11 (Luanti), 0.11 (SuperTuxKart), 0.02 s (ClassiCube), below the
run-to-run noise. Mesa's GLSL disk cache, which is on, saved 1.2 and 0.8 s of main-thread CPU and
no wall time. Cheap to run; it also tests that a hit gives the same code (any picture difference is
a bug).

- **Payoff:** low (at most 0.4 s). **Effort:** S. **Risk:** low: a wrong cached program shows as
  wrong rendering; the cache is keyed by the driver's build id.
- **Needs:** PS3. **Status:** built, switch `NV30_SHADER_CACHE`, scheduled: TESTS.md "Next" (it was
  skipped in ssd1 for its 0.4 s upper bound).
- **Source:** PLAN 1.7; [archive/PROFILING.md, "nv30's shader cache"](../archive/PROFILING.md#nv30s-shader-cache-mesa-0039-nv30_shader_cache-2026-10-18).

**To start.** One priming run, then the A/B with the warm NIR cache in both arms:

    T=SHADERCACHE=warm+NV30_SHADER_CACHE_STATS=1
    PERF_LOAD=1 ENVS="$T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench openarena gl2min 1   # prime
    PERF_LOAD=1 STALLS=1 ENVS="$T $T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench openarena gl2min 2

Read `load`, `lp_shbe`, `shc_tgsi_hit`/`_miss`, `shc_vp_miss`, `stalls_warm`, `shadercache_kb`.

### Gaps in the measurement tools

Rank 48. Small fixes and checks that make the numbers trustworthy.

- **Run logs not read automatically.** `games/bench/logscan.py` exists now; two gaps it found
  ([LOG-FINDINGS.md](../games/LOG-FINDINGS.md)): `PERF_LOAD` runs print `client.sh: line N:
  /dev/shm/ps3-bench-<stamp>/.pid: No such file or directory` 4-9 times (16 runs; check the load
  profile has its pid), and bench.txt should carry the scan's verdict (a crash, a GART failure, an
  OpenAL wedge) so report.py can mark such a run like `!INTERFERED`.
- **perf's own cost.** The rule is a same-boot A/B (`PERF=0` against `PERF=1`, three rounds) before
  reading fps off profiled runs at all; it has never run. One profiling batch had OpenArena at
  50.7, 50.5, 52.7 fps profiled against 52.7 and 50.2 without: one game, not interleaved.
  `PERF_HZ=199` cuts the cost by 2.5. ([archive/PROFILING.md, "Overhead"](../archive/PROFILING.md#overhead))
- **The recorder's own cost on the fixed kernel.** Before local/0009 the recorder removed stalls
  (its 10 ms wakeups acted as a kicker), so `STALLS=1` runs are their own variant. A same-boot
  `STALLS=0` against `STALLS=1` A/B on a kernel with the fix has never run; until it does the
  recorder stays off by default ([STALLS.md](../STALLS.md#the-recorder-removes-stalls)).
- **Luanti's warm-up CPU.** The recorder counts 35.6 s of main-thread CPU, perf 16.6 s for the same
  thread (the load phase agrees: 11.5 against 8.8; Sauerbraten's warm-up agrees exactly). Not
  resolved; Luanti's warm-up CPU split should not be quoted until it is.
- **Which mappings Luanti faults.** About 13 of its 17 s of load disk wait are major faults, and the
  recorder has no file for a fault. Its fd for page faults would name them (the binary and libraries
  are the candidates).
- **OpenArena's shader cache read cold.** Inferred, not measured: its shader loop's own timer is
  1.41 s warm against 0.57 s of CPU; the gap fits 55-odd small cache files read cold from the USB
  disk at ~15 ms a seek. Test: the recorder's states in that phase, or a warm run with the cache
  directory in the page cache.
- **The Luanti bench mod** (`assets/luanti-ps3bench`) parses `/proc/self/status` wrongly
  (`ps3bench: mem start lua=3966 VmHWM: vmhwm=106952 VmRSS: 103840 kB`): `vmrss`, `rssanon`,
  `rssfile` and `vmswap` never come out.
- **ps3rsx's census** lists the first eight distinct buffer sizes above 64 KiB per client, so new
  buffers of other sizes (ETR's growth) don't show. Listing the largest instead is a small change in
  `file_usage` (`ps3rsx/src/mem.c`).

- **Effort:** S each. **Risk:** none. **Needs:** host; PS3 for the perf A/B.
- **Status:** never tried.
- **Source:** [archive/PROFILING.md, "Start-up profiles, batch 17"](../archive/PROFILING.md#start-up-profiles-batch-17-2026-10-18), [archive/MEMORY.md, "Proposed, not implemented"](../archive/MEMORY.md#proposed-not-implemented).

## Measured since, and not backlog any more

- The game prefetch (batch 16, row 68): no gain on the USB disk, Luanti worse (load +1 to +4 s, two
  runs with 3 s hitches); `PS3_GAME_PREFETCH_GAME` defaults to off since b85d0b9. The code stays
  for a faster disk. See [STALLS.md](../STALLS.md).
- The lost-decrementer fix on and off, clean (batch 16, row 68): gameplay stalls 0.4 and 0.4 s
  against 9.0 and 7.9 s.
- The internal disk's speed (100 MB/s, 0.1 ms against the USB disk's 27.8 MB/s, 20 ms) and the
  start-up profiles of five games (batch 17): [PROFILING.md](../PROFILING.md),
  [GAMEOS-HDD.md](../GAMEOS-HDD.md).

Older versions and the dated history: [archive/STALLS.md](../archive/STALLS.md),
[archive/PROFILING.md](../archive/PROFILING.md), [archive/PLAN.md](../archive/PLAN.md),
[archive/IDEAS.md](../archive/IDEAS.md).
