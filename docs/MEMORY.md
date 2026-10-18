# Where the games' memory goes, and what to cut

The user (2026-10-18): per game, where the memory goes, what is swapped out and how it grows, and the
cuts ranked by MB saved and effort; the cheap safe ones implemented behind switches, defaults
unchanged until the PS3 measures them. Stutters on the PS3 are often swap: ~217 MB of RAM, zram
(384 MB, empty in these runs) and the VRAM swap ps3vswap (prio 300, ~96 MB of VRAM, the rest of its
1 GB on the USB disk). Every MB a game holds beyond what fits goes to the disk, and every page
read back is a stall.

## The data

One same-boot batch, 2026-10-18 00:40-01:40 UTC, `PERF=1 MEM=1`, one run per game of the suite
(run directories in `build/tmp/prof-runs.txt`, pulled into `build/bench/`): Mesa ps3.299, the
snapshots at the window's start + 1 s, its middle and 2 s before its end (docs/PROFILING.md, "What
MEM=1 records"). Profiled runs: their fps don't count, their memory does. Numbers below are the
middle snapshot unless a column says otherwise; MB are MiB.

- **No snapshots: dsda-doom and OpenTTD.** dsda-doom's `LOAD_MARK` (`R_Init`...) never prints in
  0.30, so the client never planned the window and neither perf nor the snapshots started (fixed:
  the recipe now also takes `Playing demo:`). OpenTTD with SDL's software video never feeds the
  Gallium HUD, so the window (HUD-based) never opened either; it needs a `LOAD_MARK` or the `gl`
  variant to get one.
- **Webcam** (`build/bench-cam/<run>-60s.jpg`, `-150s.jpg`): every game shows its picture. Two
  notes: OpenArena's 60 s photo is its loading screen (the window starts later), and Armagetron's
  150 s photo is black with the mouse pointer: its recorded round ends about 65 s into the window
  (the HUD's fps file stops there), and its middle and end snapshots are the game idling after the
  playback, with no ps3rsx client line of its own (it had closed its GL context). Its start
  snapshot is the playing game; the RAM numbers barely differ.
- Tables made with memprof.py's classes (docs/PROFILING.md); scratch scripts were in
  `build/tmp/memana` (deleted; the numbers are here).

## Per game (middle snapshot)

"Code" is the file-backed Rss of the game's own binary and libraries, Mesa (libgallium, the GL
dispatch, libdrm) and every other library: clean pages the kernel drops and reads back from the
USB disk when it needs the RAM, not swap. Malloc "in arenas" is glibc's: used, free (fragmentation
or a top chunk not given back) and the number of arenas; "mmapped" are blocks above the mmap
threshold (the engines' hunks among them, mostly never touched).

| Game | Rss + swap | code: game / Mesa / libs | heap Rss (swap) | anon Rss (swap) | malloc in arenas: used / free, arenas | mmapped | VRAM / GART (ps3rsx) |
|---|---|---|---|---|---|---|---|
| Luanti | 102 + 47.1 | 4.0 / 2.2 / 5.9 | 74.5 (28.5) | 14.0 (11.0) | 96.8 / **22.9**, **14** | 23.6 | 61 / 12.7 |
| OpenArena (gl1) | 71 + 19.7 | 0.7 / 1.8 / 5.7 | 22.8 (13.9) | 39.4 (2.0) | 33.5 / 3.8 (start, end: 21 / **16**), 4 | 176.8 (hunk 128 + zone 48) | 36 / 6.7 |
| Sauerbraten | 78 + 12.3 | 2.2 / 2.7 / 5.3 | 66.1 (9.6) | 0.7 (0.6) | 63.4 / **17.6**, 3 | 4.8 | **117** / 9.6 |
| Yamagi Quake II | 77 + 3.4 | 1.7 / 4.8 / 8.5 | 19.3 (1.5) | **42.1** (0.9) | 18.6 / 2.8, 4 | 0 | 48 / 6.9 |
| QuakeSpasm | 58 + 0.1 | 0.8 / 7.1 / 9.0 | 13.6 | 27.1 | 12.9 / 1.3, 3 | 256.3 (its heap) | 47 / 8.3 |
| X-Moto | 49 + 0 | 2.6 / 7.5 / 18.2 | 19.0 | 0.7 | 19.7 / 0.3, 3 | 0.4 | 37 / 6.7 |
| ClassiCube | 41 + 0 | 1.0 / 8.9 / 14.7 | 11.6 | 4.3 | 11.3 / 1.1, 6 | 2.3 | 60 / 8.5 |
| Extreme Tux Racer | 39 + 0 | 0.7 / 6.1 / 10.6 | 18.5 | 3.0 | 18.5 / 1.4, 3 | 6.3 | 58 / 12.8 |
| Armagetron | 33 + 1.8 | 2.3 / 6.4 / 15.1 | 7.9 | 1.4 | 3.4 / 4.8, 3 | 0 | 28 / 6.8 (start) |
| Neverball | 30 + 0 | 0.4 / 7.3 / 12.5 | 8.5 | 0.6 | 6.3 / 2.5, 3 | 0.3 | 34 / 8.3 |

Thread stacks are 0.1-0.4 MB in every game but Luanti (16 threads: 1.2 MB + 2.7 swapped). The GPU
mappings (`/dev/dri`) have no Rss (ps3rsx maps buffers `VM_PFNMAP`); shared memory and other files
are under 1.3 MB everywhere.

### What is swapped out, by class

| Game | swap | heap | anon | libs | stacks | Mesa |
|---|---|---|---|---|---|---|
| Luanti | 47.1 | 28.5 | 11.0 | 3.6 | 2.7 | 1.2 |
| OpenArena | 19.7 | 13.9 | 2.0 | 2.6 | 0 | 1.1 |
| Sauerbraten | 12.3 | 9.6 | 0.6 | 1.0 | 0.3 | 0.8 |
| Yamagi Quake II | 3.4 | 1.5 | 0.9 | 1.0 | 0 | 0 |
| Armagetron | 1.8 | 0 | 0 | 1.7 | 0 | 0 |

The rest swap nothing. Heap dominates, and in the three big ones it is about the size of malloc's
free memory: Sauerbraten's arena is 80.7 MB with 17.4 free, its heap 66.1 resident + 9.6 swapped =
75.7, so at least 12 MB of the free chunks are resident or swapped; OpenArena swapped 13.9 MB of
heap while 15.9 MB sat free at the window's start. The kernel swaps out what nobody touches, and
nobody touches free chunks: garbage written to swap. Library swap is their relocated data and
.bss (private dirty pages); Mesa's is libgallium's 1 MB of relocations (below).

### Growth over the window

Anonymous memory (Anonymous + swap) is flat in every game across the two minutes (within 1 MB),
so no game leaks RAM in this window. Inside malloc it moves:

- **Luanti**: used 102.2 -> 96.8 -> 94.4 MB while free grows 12.8 -> 22.9 -> 28.2: blocks and
  meshes unloaded are freed into the arenas and stay there (arena #4 holds 18.2-22.2 MB free of
  31.2). The free part grows with play time; this flight is three minutes.
- **OpenArena**: used 21.4 / 33.5 / 20.8 at start / middle / end: a 12 MB transient around the
  middle, its pages kept (free 15.9 / 3.8 / 16.5).
- **Extreme Tux Racer**: used 17.5 -> 18.5 -> 19.9 MB (+2.4 MB in 117 s) and its GART 8.8 ->
  12.8 -> 16.8 MB with 161 -> 163 handles: two new ~4 MB GART buffers, one a minute. Candidate: the
  track marks in the snow (a growing list, drawn through our batching patch's stream buffers).
  A longer run (a soak) tells a cap from a leak.
- File-backed Rss falls in most games (Sauerbraten 17.1 -> 9.3, Neverball 23.5 -> 15.6, ETR 20.3
  -> 14.1): code and data pages dropped from the page cache, read back from the disk when needed.

### Malloc arenas

glibc gives a thread that mallocs while another holds an arena its own, up to 8 x cores = 16 on the
PPE's two threads. Most games have 3-4, all but the main one at 0.1 MB: `MALLOC_ARENA_MAX` saves
under 0.3 MB there. ClassiCube has 6 (one 1.7 MB). **Luanti has 14**: #0 47.2 MB, #1 32.6, #4 31.2
(18.2 free), #5 3.1, #7 4.3, nine at 0.1. A free hole in one arena can't serve another thread, so
fewer arenas means less fragmentation, against lock contention between the renderer, the server,
emerge and mesh threads.

### The GPU side (ps3rsx's census)

- Every game at 1920x1080 has three 8100 KiB buffers, consistent with two back buffers (Mesa's
  DRI3 loader keeps 2 when X copies rather than flips, 3-4 only when page flipping:
  `dri3_update_max_num_back`) and the depth/stencil buffer. The
  back buffers are imported by Xorg too, so its 3 x 8100 KiB are its own front plus the game's two:
  the census counts shared buffers once per client (Armagetron's mid snapshot: Xorg drops from 25.4
  to 9.5 MB once the game's buffers are gone). 24 MB of VRAM per game for the window, at minimum.
- VRAM the games take comes out of the VRAM swap: "VRAM cpu" was 99% used in Sauerbraten
  (218/220 MB), and Luanti's VRAM swap had 88 MB in VRAM and 54 MB on the disk at the middle
  snapshot. A MB of textures is a MB of swap moved to the disk.
- GART (system RAM, taken from CMA on demand): 6.7-12.8 MB per game (pushbuffers, Mesa's upload and
  stream buffers), plus Xorg's 6.7; "GART backed by memory" 17-27 MB system-wide.
- The census lists only the first eight distinct buffer sizes above 64 KiB per client, so new
  buffers of other sizes (ETR's growth) don't show which they are. Listing the largest instead
  would be a small ps3rsx change (`file_usage`, ps3rsx/src/mem.c).

### Shared by every game

- **libgallium**: 24 MB mapped, 1.4-7.5 MB resident (only what runs), no LLVM (our packaging: nouveau
  and softpipe only). Its writable segment is 1.0 MB of relocations (`.opd` 537 KiB of ELFv1
  function descriptors, `.data.rel.ro` 466 KiB, 62722 relocations), private to each process, plus a
  1.8 MB `.bss` of which only the touched part counts: Mesa's private dirty is 1.4-1.5 MB per game.
  nv50/nvc0 code and softpipe make part of those relocations; a build with nv30 alone would need a
  Mesa meson patch for maybe 0.3-0.5 MB per game (estimate, not measured).
- **Sound path**: every game maps libpulse, libpulsecommon and its codecs (libsndfile, FLAC, opus,
  vorbis, mpg123) and dbus/systemd, through ALSA's pulse plugin: 0.5-3.1 MB Rss per game (mostly
  shared clean pages), a "threaded-ml" thread in the OpenAL games, and a pulse server process
  outside the game. Going straight to ALSA would also take the server out; its memory belongs to
  the game-mode work (docs/DESKTOP-PLAN.md), not measured here.

## Findings in the games' code

- **Yamagi Quake II: 32 MB in a random number generator.** `src/common/shared/rand.c` (the KISS
  PRNG, also in rogue/ and xatrix/) has `static uint64_t QARY[0x200000]`: 16 MiB, and
  `randk_seed()` writes every entry. The file is linked into the client (`quake2`, whose 23 MB
  .bss is 18 MB resident, 16 of it this table) and into `game.so` (16 MB .bss, all resident). Of the
  game's 77 MB, 32 MB is this table. A lag of 4096 entries keeps a period far beyond anything a game
  draws. Implemented: our yquake2 0002, `YQ2_KISS_QSIZE`.
- **Luanti trims only after 256 MB freed.** `porting::TriggerMemoryTrim()` (src/porting.cpp)
  calls `malloc_trim()` from the emerge and mesh threads once 256 MiB of map blocks, voxel
  manipulators and meshes were freed (upstream's workaround for exactly the fragmentation above).
  On a 217 MB machine the count rarely gets there. Implemented: our luanti 0007, `LU_TRIM_MB`.
- **OpenArena's hunk**: 30.7 MB in use (low permanent 16.1, high 14.7), temp high-waters 17.7 and
  15.7: 2.7 MB of pages touched by loading and never used again ("unused highwater" in its own
  meminfo, in game.log). The hunk (128 MB) and zone (48 MB) are one 176 MB mapping with 34.6 MB
  resident: the rest is never touched and costs nothing.
- **QuakeSpasm's 256 MB heap**: 24.8 MB touched; the cache inside it never needs flushing at this
  size, so sounds and models stay. Nothing to gain at this footprint.
- glibc's dynamic mmap threshold: after the first free of a block mmapped above 128 KiB, glibc
  raises the threshold to that size (up to 32 MiB) and the trim threshold to twice it (up to 64
  MiB). From then on texture and map scratch buffers of a few MB come from the heap and leave
  holes, and up to 64 MB of free top is never given back. That is why OpenArena and Sauerbraten
  keep 16-17 MB free in their main heap.

## Cuts, ranked

MB are from this batch (upper bounds where the free memory's residency isn't known page by page:
"pages in free chunks" counts whole pages inside free chunks of 8 KiB and more, from malloc_info's
size lists, plus the top chunks). Effort: env = no rebuild; pkg = our package rebuilt (done where
it says built).

| # | Game | Cut | Saves (MB) | Effort | Status | Risk |
|---|---|---|---|---|---|---|
| 1 | Yamagi Quake II | `YQ2_KISS_QSIZE=4096` (variant `kiss4k`) | **32** | pkg | yquake2 0002 built (+ps3.408) | client-side random effects differ (the demo itself doesn't) |
| 2 | Luanti | trim the arenas: `LU_TRIM_MB=16` (variants `trim16`, `trim64`) or `MALLOC=trim` | **17-24**, growing with play time (16.4-19.7 in free chunks + 2.5-4.2 top) | pkg / env | luanti 0007 built; ps3-malloctrim in ps3-gpu-tools 0.403 | a trim holds each arena's lock for its walk (log says how long) |
| 3 | Sauerbraten | `MALLOC=trim` or `MALLOC=mmap` | **17** (17.0-17.4 in free chunks of the main heap) | env | ready | mmap: a syscall and zeroed pages per big alloc |
| 4 | OpenArena gl1 | `MALLOC=trim` or `MALLOC=mmap` | **12-16** at rest (11.7-12.2 in free chunks + 3.6 top); its 13.9 MB of swapped heap | env | ready | as above |
| 5 | Sauerbraten | `texreduce 1` (variant `tex1`, exists) | VRAM ~45-60 of its 117 (textures are ~87 MB of it; DDS and JPG at a quarter) | env | ready since 2026-10-17 | blurrier textures |
| 6 | Luanti | `MALLOC=arena2` (or the recipe's `malloc` variant) | part of #4 arena's 18-22 MB holes, overlaps #2 | env | ready | malloc lock contention between 4 busy threads |
| 7 | Luanti | settings variants (view range, client/server unload, active2, chunk3, lean) | GAME-PLAN.md's estimates; heap here is 103 MB of map blocks and meshes | env | ready since 2026-10-17 | per variant |
| 8 | Armagetron, Neverball, Yamagi, ETR, ClassiCube, QuakeSpasm | `MALLOC=trim` | 4.2 / 2.3 / 2.7 / 0.8-1.9 / 1.0 / 1.3 (mostly top chunks) | env | ready | small |
| 9 | OpenArena | madvise the hunk's unused high-water after a map load | 2.7 (gl1) | pkg | proposed (below) | none known |
| 10 | every game | Mesa with nv30 only (no nv50/nvc0, softpipe elsewhere) | ~0.3-0.5 private each (estimate) | pkg, Mesa meson patch | proposed | softpipe is the mesa-tests reference |
| 11 | every game | sound straight to ALSA, no pulse plugin and server | 0.5-3 per game + the server (system) | config | proposed, game-mode work | underruns: measure |
| - | most games | `MALLOC=arena1/arena2` | < 0.3 (extra arenas are 0.1 MB) | env | ready | - |

Not worth a change at these sizes: thread stacks, X-Moto's heap (0.3 MB free), QuakeSpasm's heap,
OpenArena's 176 MB hunk mapping (untouched pages).

## What was implemented

All defaults unchanged; nothing deployed or published.

1. **Malloc presets for every game, from the bench** (games/bench: client.sh, run.sh, report.py,
   selftest.py). `BENCH_MALLOC` (run.sh `MALLOC=`, or an `ENVS` token) is a comma list:

   | Preset | Environment of the game |
   |---|---|
   | `arena1`, `arena2` | `MALLOC_ARENA_MAX=1` / `2` |
   | `mmap` | `MALLOC_MMAP_THRESHOLD_=262144 MALLOC_TRIM_THRESHOLD_=1048576`: blocks of 256 KiB and more mmapped and unmapped at free, the main heap's top given back past 1 MiB; fixing them stops the dynamic raise |
   | `trim` | `ps3-malloctrim.so` preloaded, `PS3_MALLOC_TRIM=10` (`MALLOC_TRIM_S`), only in the recipe's `COMM` |
   | `tune` | `arena2,mmap,trim` |

   bench.txt gets `malloc=`, report.py shows it after the variant (`base@mtune`,
   `base@perf+marena2_trim`), so such runs never group with plain ones. `ENVS` now works in
   `run.sh suite` too (each game and variant once per token per round). A recipe's own
   `LD_PRELOAD` and MEM=1's ps3-mallocstat are kept (client.sh `preload_add`).
2. **ps3-malloctrim.so** (ps3-gpu-tools/preload/ps3-malloctrim.c, in ps3-gpu-tools 0.403): one
   thread, 64 KiB stack, all signals blocked, calls `malloc_trim(0)` every `PS3_MALLOC_TRIM`
   seconds. It never mallocs (so glibc gives it no arena), and with `PS3_MALLOC_TRIM_LOG=1` (the
   bench sets it) prints `ps3-malloctrim: <n> <ms> ms rss <kB> -> <kB> swap <kB> -> <kB> freed
   <0|1>` per trim into game.log: the saving and the lock time, run by run. On the host (Zen 2) the
   first trim of 80 MB of holes took 10-15 ms, later ones 0.1 ms; the PPE will be several times
   slower: read the ms column before trusting the stall numbers. Caveat: a log line written while
   the game has half a line pending on stdout splits that line (games print whole lines through
   `stdbuf -oL`; the selftest's fake game, which splits one on purpose, needed a 1 s period).
3. **Luanti 0007** (`LU_TRIM_MB`, luanti/patches): the malloc_trim threshold in MiB, 256 when
   unset; recipe variants `trim16`, `trim64`. The trims stay in Luanti's emerge and mesh threads.
4. **Yamagi Quake II 0002** (`YQ2_KISS_QSIZE`, yquake2/patches): the PRNG table allocated at first
   use, sized by the variable (a power of two, 64 to 2^21), the same size and sequence as before when
   unset (checked on the host: the same million draws); recipe variant `kiss4k` (4096: 32 KiB per
   module). In the built package the .bss is 7.7 MB in `quake2` (was 23.7) and 81 KiB in
   `game.so` (was 16 MB); unset, the 16 MiB come from calloc instead, the same Rss as before.
5. **dsda-doom's recipe**: `LOAD_MARK` also matches `Playing demo:`, so its window, perf and the
   snapshots start.

Selftest (`python3 games/bench/selftest.py`, all pass): section 8 runs client.sh with
`BENCH_MALLOC=arena2,trim` and checks both preloads' threads in the game, the trim lines in
game.log, `malloc=` and the variant tag; section 10 runs ps3-malloctrim.so on a C program that
leaves 88 MB of holes in its main heap and a thread arena (over 60 MB given back) and checks that a
process of another name is left alone.

## Packages (built, not deployed, not published)

| Package | Version | What changed |
|---|---|---|
| ps3-gpu-tools | 0.403 | `/usr/lib/ps3/lib/ps3-malloctrim.so` |
| luanti, luanti-data | 5.15.2+dfsg-2+ps3.405 | 0007 `LU_TRIM_MB` (unit tests pass in the build) |
| yamagi-quake2, yamagi-quake2-core | 8.70+dfsg-1+ps3.408 | 0002 `YQ2_KISS_QSIZE` |

## On the PS3

Deploy (check first that nobody is playing on it), then push the bench:

    make deploy PKGS="ps3-gpu-tools luanti luanti-data yamagi-quake2 yamagi-quake2-core"
    make bench-push

Memory first (one profiled round shows the MB; MEM=1's snapshots hold the game's mmap lock for
30-600 ms, so fps and stalls come from runs without it), same boot, the switch alternated with
its off state:

    # every preset on the three games that swap, one round (~2 h)
    MEM=1 ENVS="- MALLOC=trim MALLOC=mmap MALLOC=arena2 MALLOC=tune" /var/tmp/bench/run.sh bench luanti base 1
    MEM=1 ENVS="- MALLOC=trim MALLOC=mmap MALLOC=tune" /var/tmp/bench/run.sh bench sauerbraten base 1
    MEM=1 ENVS="- MALLOC=trim MALLOC=mmap MALLOC=tune" /var/tmp/bench/run.sh bench openarena base 1
    # the package switches
    MEM=1 /var/tmp/bench/run.sh bench luanti "base trim16 trim64" 1
    MEM=1 /var/tmp/bench/run.sh bench yquake2 "base kiss4k" 1
    # dsda-doom's first profile now that its window starts
    PERF=1 MEM=1 /var/tmp/bench/run.sh bench dsda-doom base 1
    python3 /var/tmp/bench/report.py --mem /var/tmp/ps3-bench/<run dirs>

Look at `mem_swap`, `mem_heap_rss`/`mem_heap_swap`, `mem_malloc_free`, `min_memavailable_kb`
(Luanti, Sauerbraten) and the `ps3-malloctrim:` lines (ms per trim, rss before and after).

Then the effect on stalls and fps, three rounds without MEM (report.py's `stall_play_s`,
`longest_play`, `low1_play`, median fps), webcam on every run:

    ENVS="- MALLOC=tune" /var/tmp/bench/run.sh bench luanti base 3
    ENVS="- MALLOC=trim" /var/tmp/bench/run.sh bench sauerbraten base 3
    ENVS="- MALLOC=trim" /var/tmp/bench/run.sh bench openarena base 3
    /var/tmp/bench/run.sh bench yquake2 "base kiss4k" 3
    # the whole suite with the winner, once the above picked one (~5 h for 3 rounds)
    ENVS="- MALLOC=tune" /var/tmp/bench/run.sh suite base 3

What would make each a default: kiss4k in our yquake2 build (the define, or the env set by the
package's launcher); `LU_TRIM_MB` as a Luanti default in our package; the malloc preset that wins
through `ps3-game` (game mode's launcher sets the environment for every game) or per-game wrappers.

## Proposed, not implemented

- **OpenArena (ioquake3)**: after a map load (the end of `CL_InitCGame`, next to
  `Com_TouchMemory`), `madvise(MADV_DONTNEED)` the page-aligned gap between `hunk_low.temp` and
  `s_hunkTotal - hunk_high.temp` in `s_hunkData` (code/qcommon/common.c), behind a cvar
  (`com_hunkTrim`, default 0): 2.7 MB in gl1. Not done here: ioquake3 was being rebuilt by another
  session (+ps3.396, 03:59 today).
- **ps3rsx census**: list the largest buffers per client, not the first eight sizes, so growth like
  ETR's names its buffers.
- **Extreme Tux Racer**: a soak (10 minutes) with MEM=1 to see whether the +4 MB/min GART and
  +1.2 MB/min heap stop.
- **Mesa nv30-only build** and **ALSA without the pulse plugin**: rows 10 and 11 above.
- The Luanti bench mod (assets/luanti-ps3bench) reads /proc/self/status wrongly: its lines are
  `ps3bench: mem start lua=3966 VmHWM: vmhwm=106952 VmRSS: 103840 kB`, so the recipe's `parse()`
  gets `lua` and `vmhwm` and junk keys (`mem_start_VmHWM:=VmHWM:`); vmrss, rssanon, rssfile and
  vmswap never come out.
