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
| 1 | Yamagi Quake II | default 4096 (`YQ2_KISS_QSIZE=2097152`, variant `kissbig`, restores the old table) | **32** (measured: 70 MB less RAM, gameplay stalls 22.6 -> 0.05 s) | pkg, now the default | yquake2 0002 (default changed, rebuilt) | client-side random effects differ (the demo itself doesn't) |
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

**Measured since (2026-10-18, same-boot rows of games/bench/results/, row = fps.csv row):** #1 is the
default (row 57: RSS 125 -> 56 MB, stalls 22.6 -> 0.05 s). #2 as `trim16`: no gain and more stalls
(3.35 -> 8.6 s, row 57), rejected. #3, #4 (`MALLOC=trim`, `mmap` on Sauerbraten and OpenArena, x2 in
batch 8): the rounds disagreed with each other, read as noise (row 61); not shipped, and the MB they
free were not read out of those runs' `ps3-malloctrim:` lines here. #7: the clean Luanti re-runs (row
67, interference detector on) keep base; `cliunload` has more stalls (3.8 and 5.2 s against 1.5 and
2.1 s) and `lean` draws a black void. `vm.watermark_boost_factor=0` on Luanti: no clear gain (row 62).
None of the malloc presets is a default.

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
   use, sized by the variable (a power of two, 64 to 2^21), 4096 entries (32 KiB per module) when unset since
   the PS3 measurement of 2026-10-18 (70 MB less RAM, gameplay stalls 22.6 -> 0.05 s); `YQ2_KISS_QSIZE=2097152`
   gives the old size and sequence (checked on the host: the same million draws); recipe variant
   `kissbig` is that, `base` is the small table. In the built package the .bss is 7.7 MB in `quake2` (was 23.7) and 81 KiB in
   `game.so` (was 16 MB); with `YQ2_KISS_QSIZE=2097152` the 16 MiB come from calloc instead, the same Rss as before.
5. **dsda-doom's recipe**: `LOAD_MARK` also matches `Playing demo:`, so its window, perf and the
   snapshots start.

Selftest (`python3 games/bench/selftest.py`, all pass): section 8 runs client.sh with
`BENCH_MALLOC=arena2,trim` and checks both preloads' threads in the game, the trim lines in
game.log, `malloc=` and the variant tag; section 10 runs ps3-malloctrim.so on a C program that
leaves 88 MB of holes in its main heap and a thread arena (over 60 MB given back) and checks that a
process of another name is left alone.

## Packages (built, not deployed, not published)

(2026-10-18 evening: ps3-gpu-tools 0.403 or later, luanti 405 and yamagi-quake2 408 are installed on
the PS3 and were measured, above. ioquake3 ps3.462, Mesa ps3.442~dev and cube2 ps3.442~dev are in
ps3-dev, not installed; their A/Bs below are pending.)

| Package | Version | What changed |
|---|---|---|
| ps3-gpu-tools | 0.403 | `/usr/lib/ps3/lib/ps3-malloctrim.so` |
| luanti, luanti-data | 5.15.2+dfsg-2+ps3.405 | 0007 `LU_TRIM_MB` (unit tests pass in the build) |
| yamagi-quake2, yamagi-quake2-core | 8.70+dfsg-1+ps3.408 | 0002 `YQ2_KISS_QSIZE` |
| ioquake3 | 1.36+u20260719.58839361+dfsg-1+ps3.462 | 0027-0029, OpenArena opengl2 (section below) |
| Mesa | 26.2.4-1+ps3.442~dev20261018140130 (build/debs/mesa) | 0037 NV30_SYSMEM_COPY, NV30_INVALIDATE_BUFFER, NV30_SYSMEM_STATS (section "nouveau's system memory copies") |
| cube2, cube2-server | 0.0.20201229+dfsg-1+ps3.442~dev20261018143125 (build/debs/cube2) | 0011 `vanormalshort` (section "Sauerbraten's world normals as shorts") |

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
    MEM=1 /var/tmp/bench/run.sh bench yquake2 "base kissbig" 1
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
    /var/tmp/bench/run.sh bench yquake2 "base kissbig" 3
    # the whole suite with the winner, once the above picked one (~5 h for 3 rounds)
    ENVS="- MALLOC=tune" /var/tmp/bench/run.sh suite base 3

What would make each a default: kiss4k is the default now (measured, in our yquake2 build);
`LU_TRIM_MB` as a Luanti default in our package; the malloc preset that wins
through `ps3-game` (game mode's launcher sets the environment for every game) or per-game wrappers.

## OpenArena opengl2 (gl2min): the 16 s freezes and the memory (2026-10-18)

The user: gl2min's profiled run (`PERF=1 MEM=1`, ioquake3 ps3.396, Mesa ps3.408~dev,
`build/bench/ps3-bench-20261018T080250Z-openarena-gl2min`) had stall_play_s 54.7 s, a 16148 ms
longest frame, 66.5 MB swapped and 117 MB of VRAM. No stall record (`STALLS=1` was off), so the
causes below come from the 0.5 s samples, out.log's stamps, the MEM=1 snapshots and perf. The
second run (`20261018T114707Z`, GAMEPREFETCH=learn, no perf) agrees.

### What the 16 s freeze is: the load's last frame, counted as play

Each demo pass reloads the map. report.py cut the reload at 1 s after `CL_InitCGame:`, but in
opengl2 the load ends in one long frame with no SwapBuffers in it: the cgame load,
`cl_preloadLate` (35 models, skins and shaders: 7-12 s in opengl2, the PPE swapping), then
`RB_ShowImages` (0.1-1.5 s, every texture drawn once) and the first demo frame. The frame starts
inside the reload segment and ends 4-5 s after the cut, so it counted as play:

| Pass 3 of the profiled run (s from start) | |
|---|---|
| 131.5 | RE_Shutdown, R_Init: 55 GLSL programs kept, 0.27 s |
| 137.1 | world loaded |
| 143.06 - 159.20 | **the 16148 ms frame** |
| 154.0 | `cl_preloadLate: 35 models, skins and shaders ... in 10315 msec`, `CL_InitCGame: 19.13 seconds` |
| 155.06 | report.py's old cut |
| 156.5 | `1533 msec to draw all images` |

Of the 54.7 s, 38.8 s were such frames (the 16148, 12985 and 9640 ms ones); the other run's
11459 ms longest is the same thing. Fixed in report.py: a frame that starts inside a reload
segment is the reload's, and the segment ends with it (passsplit.py already went by the frame's
start). The profiled run is now stall_play_s **16.0 s**, longest_play 6263 ms; the other 9.7 s,
2125 ms. Over every OpenArena run in build/bench the opengl1 numbers move by 0-4 s (e.g.
20261018T001126Z 19.2 -> 15.1 s).

### The play stalls that are left: memory

The remaining stall frames (1.0-6.3 s) are the first seconds after each load, plus single
0.5-1 s frames when a bot joins. The samples during them show a machine out of memory, not a
compile: MemAvailable 13-60 MB, swap-ins of 25-260 pages and VRAM swap write-backs of 256-1024
pages every 0.5 s, swap-out bursts of 1.2-1.7k pages, and the game's VRAM going 70 -> 114 MB
while GART staging peaks at 48 MB during the loads. The game holds Rss 79-104 MB plus 58-68 MB
swapped (opengl1: 71 + 20).

Not the causes:
- **GLSL compiles.** The 55 programs (gl2min) are built once at the first start, 3.0-3.4 s, kept
  across every renderer restart (0004: 0.02-1.3 s to take them back), none built during play
  (`glsl*_deferred=0`, no lazy builds). Mesa's disk shader cache is on for nv30: the nouveau
  screen creates one (`nouveau_disk_cache_create`, it caches the GLSL front end's NIR); the nv30
  code generation (nvfx) isn't cached but takes milliseconds per program at its first draw.
  A warm cache only shortens the first start.
- **FBOs**: none (gl2min sets r_ext_framebuffer_object 0).

Where the memory goes (MEM=1 snapshots, start/mid/end):

| | opengl2 gl2min | opengl1 (MEMORY.md above) |
|---|---|---|
| malloc in use (all arenas) | 56 MB (in a load) / **95** / 94 | 21-34 |
| heap Rss + swap | 51.3 + 43.7 | 22.8 + 13.9 |
| VRAM (ps3rsx, the game) | 100 / 111 / 66 MB, 117 peak | 36 |
| VRAM swap | 88 MB of it in VRAM, the rest written back to the USB disk all through play | |

**Why opengl2's malloc holds 60 MB more (from Mesa's source; the A/B below measures it).**
renderergl2 sends vertex colours as `GL_UNSIGNED_SHORT` normalized (R16G16B16A16_UNORM), a format
nv30 can't fetch (`nv30_vtxfmt_table`). Every draw from the tess VAO (the world, effects, the HUD;
everything but MD3 models: r_vaoCache is 0) goes through u_vbuf, which converts the colours to
floats on the CPU and maps the vertex and index buffers for reading (perf: 0.9% of the samples in
the conversion itself). A read map of a VRAM buffer makes nouveau keep a malloc'ed copy of the
whole buffer (`nouveau_buffer_cache`: a GPU copy to GART, a wait, a memcpy). After four flushes in
a row with such a copy `nouveau_context_update_frame_stats` sets `hint_buf_keep_sysmem_copy`, which
is never cleared, and from then on every new VRAM buffer gets a malloc'ed copy at its creation:
the MD3 animation VBOs too, **62.2 MB** for the demo's models (counted on the host, 223 VBOs).
That is about the 60 MB.

**Where the load CPU goes** (perf, window incl. three reloads): R_MipMapsRGB's `powf` per texel
channel 7.8% of all samples (picmip and every mip level are made on the CPU at each load and for
every player a joining bot brings), COM_ParseExt 2.8% (shader scripts), R_RegisterMD3 2.4%,
libdeflate 2.7%. opengl2 has no r_keepImages (0025 is opengl1's), so every pass reloads every
texture.

### What was implemented (ioquake3 ps3.462, patches 0027-0029; not deployed, not published)

| Patch | Switch (default) | What | Host check (llvmpipe, GL 2.1, the bench demo, 30 frames) |
|---|---|---|---|
| 0027 | `r_vaoSnormColor 1` (latched) | colours as GL_SHORT normalized (v >> 1, 0 -> -1 so a zero alpha still fails an alpha test under either GL rule), which nv30 fetches (V16_SNORM, as the normals): no u_vbuf, no buffer reads, no copies, no hint | 17 pixels differ by 1 level |
| 0028 | `r_mipMapPowf 0` | the gamma-correct mipmap bytes from 255 thresholds found once by bisection with the same powf, plus a 4096-bin start table: the same bytes without powf | identical; every float in [0,1] equal on x86-64 glibc, every 16th + all within 200000 ulps of each threshold on ppc64 glibc (qemu) |
| 0029 | `r_md3PackedFrames 0` (opt-in, latched, needs r_normalMapping 0) | animated MD3 frames as MD3's own 16-bit positions + 4 unsigned-byte normals, 12 bytes a vertex instead of 20 (V16_SSCALED, U8_UNORM: both native on nv30); decoded in the 4 vertex-animation programs by `u_PackedFrames` (lightall draws single-frame models with that variant too, and they keep floats): the demo's model VBOs **62.2 -> 37.5 MB** of VRAM | 1734 pixels differ, by at most 2 levels |

Recipe variants (games/bench/recipes/openarena.sh): `gl2min_color16` (0027 off),
`gl2min_mippowf` (0028 off), `gl2min_md3pack` (0029 on); `gl2min_up` now turns 0027-0028 back too.

Expected on the PS3 (to be measured): gl2min's malloc in use down by up to ~60 MB (no copies), and
with md3pack another ~25 MB of VRAM free for the VRAM swap; fewer swap stalls after loads and at
joins; loads ~8% less CPU.

### The A/B on the PS3

Same boot, desktop stopped, nobody playing; webcam on every run (players must show in
`gl2min_md3pack`: a wrong vertex fetch would lose them, not the world).

    make deploy PKGS="ioquake3"
    make bench-push
    # memory and stall causes, every switch against gl2min, 3 rounds (~12 runs, ~75 min)
    STALLS=1 MEM=1 ENVS="-" /var/tmp/bench/run.sh bench openarena "gl2min gl2min_color16 gl2min_md3pack gl2min_mippowf" 3
    # the malloc trim on top (the heap's free chunks), if the first shows the heap down
    STALLS=1 MEM=1 ENVS="- MALLOC=trim" /var/tmp/bench/run.sh bench openarena "gl2min gl2min_md3pack" 3
    python3 /var/tmp/bench/report.py --stalls --mem /var/tmp/ps3-bench/<run dirs>
    python3 /var/tmp/bench/passsplit.py -v /var/tmp/ps3-bench/<run dirs>

Read `mem_malloc_used`, `mem_heap_rss`/`mem_heap_swap`, `mem_swap`, `mem_gpu_vram` (and the `vram`
peak), `stall_play_s`/`longest_play`, `stall_swap_s` and `load=`. gl2min against gl2min_color16 is
0027 (the copies), gl2min_md3pack against gl2min is 0029; gl2min_mippowf shows 0028 in `load=` and
the join stalls. MEM=1's snapshots pause the game 0.2-0.6 s three times a run, the recorder
changes the stalls it records (STALLS.md); both hit every variant alike, but take the final fps
and stall numbers from 3 more rounds without MEM:

    STALLS=1 /var/tmp/bench/run.sh bench openarena "gl2min gl2min_color16 gl2min_md3pack" 3

If md3pack shows the models right and helps, its default can go to 1.

## nouveau's system memory copies of VRAM buffers (Mesa 0037, 2026-10-18)

Follow-up to "Why opengl2's malloc holds 60 MB more" above, from the source we build
(build/work/mesa, src/gallium/drivers/nouveau/nouveau_buffer.c, nouveau_context.h, nv30/).
Built, not measured: the switches are off by default.

### How the copies work

- **Per buffer.** `nv04_resource.data` is a malloc'ed copy of the whole buffer (`width0`
  bytes). A map of an idle VRAM buffer that doesn't discard makes one if there is none
  (`nouveau_buffer_cache`): u_vbuf's `PIPE_MAP_READ` maps of the vertex buffers it converts (and
  of the index buffer when it unrolls indices), but also a write map of an already written range
  without an invalidate flag. Every later map of that buffer returns a pointer into it.
  If the buffer was written by the CPU without a copy (status `DIRTY`), making the copy is a
  **readback**: an M2MF copy of the whole buffer to a GART staging buffer, `BO_WAIT` on it (a kick
  and a wait for everything queued before it: a GPU drain), then a memcpy. Once a copy exists the
  writes go into it first and from there to VRAM (staging + M2MF), so it never goes stale and
  there are no more readbacks for that buffer.
- **Freed** only with the buffer (`nouveau_buffer_destroy`), or when the GPU writes the buffer
  (`GPU_WRITING`: copy_buffer/stream output, not vertex buffers). Invalidation, discard maps and
  reallocation keep it.
- **The hint is screen-wide.** `nouveau_context_update_frame_stats` runs at every `pipe->flush`
  (nv30_context_flush: SwapBuffers, glFlush, fences), shifts a bit history of "this flush had a
  readback", and after four in a row sets `screen->hint_buf_keep_sysmem_copy`. Nothing clears it.
  From then on `nouveau_buffer_create` gives every new VRAM buffer an (uninitialised, not dirty)
  copy at its creation, whatever its usage, read or not. Buffers made before the hint keep none.
- **What it buys.** CPU reads of VRAM run at 10 MB/s (RSX.md); without a copy each converted draw
  would need a readback (an M2MF copy plus a GPU drain), so the copy is what makes u_vbuf usable
  at all for the buffers it reads. The hint covers apps that keep making new buffers that get
  read: each new buffer would cost one readback and drain at its first read.
- **Why OpenArena's opengl2 sets it in its first frames.** `RB_UpdateTessVao` orphans the tess
  buffers with `glBufferData(NULL)` before each tess flush. nv30 has no buffer invalidation
  (`caps->invalidate_buffer = false`), so Mesa's bufferobj_data makes a new pipe buffer each time
  (DYNAMIC, VRAM, no copy), BufferSubData writes it (`DIRTY`) and u_vbuf's colour conversion reads
  it: a readback and a drain at every tess flush, so four flushes in a row within the first
  frames. Then the MD3 models load (STATIC, VRAM): 223 buffers, 62 MB, each with a copy nothing
  reads. Sauerbraten: its world VBOs (GL_STATIC_DRAW, normals and tangents GL_BYTE, which nv30
  can't fetch: renderva.cpp) are read by u_vbuf on every draw, so they need their copies; whether
  the hint gets set too (copies of its model and sky buffers) is what NV30_SYSMEM_STATS shows.

### The options weighed

| Option | Gain | Cost / risk | Done |
|---|---|---|---|
| Never set the hint (`NV30_SYSMEM_COPY=0`) | the copies of buffers never read (OpenArena gl2: ~62 MB) | an app that orphans read buffers pays a readback and drain per new buffer: **OpenArena gl2 with 16-bit colours would drain at every tess flush**, so only together with `NV30_INVALIDATE_BUFFER=1` | yes, off |
| Hint only for DYNAMIC buffers (`NV30_SYSMEM_COPY=2`) | the same 62 MB (MD3 VBOs are STATIC); the streaming case the hint is for keeps working | static buffers read later get one readback each at their first read (load time or first view) | yes, off |
| Buffer invalidation (`NV30_INVALIDATE_BUFFER=1`) | orphaning keeps the buffer and its copy: no new pipe buffer, copy, readback or vertex array revalidation per tess flush; the hint's trigger goes away for this pattern | the code is nv50/nvc0's (nouveau_buffer_invalidate; nv30 already reallocates GART buffers in place on discard maps); applies to every orphaning game (Sauerbraten's particles and grass: GART STREAM buffers, now reallocated in place when busy instead of replaced by a new buffer) | yes, off |
| Free copies after N frames without reads | buffers read once (at load) | a buffer read again later pays a readback and drain; Sauerbraten's world buffers are read every frame they are seen, so its copies would come back each time; per-buffer map counting needed to free safely | no |
| u_vbuf reads from the CPU-side upload source | no copies at all | the upload data is gone after BufferData; keeping it is the copy | no |
| Convert at upload time into a fetchable format | no copies, no per-draw conversion | the buffer's vertex format isn't known at upload (VAOs bind it later, one buffer can serve several layouts); needs a translated twin per layout, invalidated on writes, more VRAM | no |
| **The game sends fetchable formats** | no u_vbuf, no copies, no per-draw CPU conversion | per game | ioquake3 0027 (r_vaoSnormColor, the default); Sauerbraten: proposed below |

The game-side fix is the one that removes everything; the Mesa switches cover the games we
don't patch and the hint's collateral copies.

### What was implemented (Mesa 0037, built, not deployed, not published)

`mesa/patches/0037-nv30-optional-limits-on-nouveau-s-system-memory-copi.patch`, all off by default:

| Switch | Values | What |
|---|---|---|
| `NV30_SYSMEM_COPY` | 1 (default): as upstream; 0: never the hint; 2: the hint, once set, gives copies at creation only to `PIPE_USAGE_DYNAMIC` (GL_DYNAMIC_DRAW) buffers | `nouveau_screen.hint_buf_mode` (0 for nv50/nvc0) |
| `NV30_INVALIDATE_BUFFER` | 0 (default), 1 | `caps->invalidate_buffer` and `pipe->invalidate_resource` (buffers only) -> `nouveau_buffer_invalidate` |
| `NV30_SYSMEM_STATS` | N frames (0: off) | a `nv30-sysmem:` line on stderr every N frames, when the hint is set and at context destruction: copies now (count, kb), made at creation / at a read (count, kb), at a GART->VRAM migration, readbacks (count, kb). report.py: `sysmem_mb` (peak), `sysmem_create_mb`, `sysmem_read_mb`, `sysmem_readbacks`, `sysmem_hint` (frame, 0: never) |

The copies are counted for every nouveau driver (screen atomics, a `SYSMEM_COPY` status bit kept
across reallocation); the counting is the only change with the switches off. Built with
`make mesa-deb`; nv30 code can't run on the host, so the PS3 run is the first test.

### The A/B on the PS3

Deploy the Mesa debs (26.2.4-1+ps3.442~dev20261018140130: `make deploy` of build/debs/mesa, checking first that nobody is playing),
`make bench-push`. Same boot, desktop stopped, webcam on every run. The tokens join switches with
`+`; every arm carries the stats line (one fprintf per 300 frames) so all get the sysmem columns:

    S=NV30_SYSMEM_STATS=300
    ARMS="$S $S+NV30_SYSMEM_COPY=2 $S+NV30_INVALIDATE_BUFFER=1 $S+NV30_SYSMEM_COPY=0+NV30_INVALIDATE_BUFFER=1"
    # OpenArena opengl2: gl2min_color16 has the 16-bit colours u_vbuf converts (ioquake3 0027 off);
    # gl2min (0027 on, if ioquake3 ps3.462 is installed) is the control the switches shouldn't move
    STALLS=1 MEM=1 ENVS="$ARMS" /var/tmp/bench/run.sh bench openarena "gl2min_color16 gl2min" 2
    # Sauerbraten: its GL_BYTE normals go through u_vbuf on every draw
    MEM=1 ENVS="$ARMS" /var/tmp/bench/run.sh bench sauerbraten base 2
    python3 /var/tmp/bench/report.py --stalls --mem /var/tmp/ps3-bench/<run dirs>
    # then fps and stalls without MEM (3 rounds), the arms that helped against the plain one
    STALLS=1 ENVS="$S $S+NV30_SYSMEM_COPY=2+NV30_INVALIDATE_BUFFER=1" /var/tmp/bench/run.sh bench openarena gl2min_color16 3
    ENVS="$S $S+NV30_SYSMEM_COPY=2+NV30_INVALIDATE_BUFFER=1" /var/tmp/bench/run.sh bench sauerbraten base 3

Read `sysmem_mb` and `sysmem_create_mb` (what the hint cost), `sysmem_readbacks` (the drains the
switches add), `sysmem_hint`, against `mem_malloc_used`, `mem_heap_rss`/`_swap`, `mem_swap`,
`stall_play_s`, the fps. Expected: with 2 or 0+invalidate, OpenArena gl2min_color16's
`sysmem_create_mb` ~0 instead of ~62 and malloc in use down by about that; readbacks a handful (one
per static buffer read). `NV30_SYSMEM_COPY=0` without the invalidation is left out on purpose
(a drain at every tess flush in gl2min_color16); if wanted as a worst case, one run. If a switch
helps and shows nothing wrong (players and world drawn, no GL errors in game.log), it can become a
PS3 default.

### Sauerbraten's world normals as shorts (cube2 0011, `vanormalshort`, 2026-10-18)

The game-side fix from the table above. `engine/world.h`'s `vertex` (32 bytes) sends `norm` and
`tangent` as GL_BYTE normalized; nv30's vertex formats (nv30_format.c, `nv30_vtxfmt_table`) have
no signed 8-bit type (U8_UNORM, U8_USCALED, V16_SNORM, V16_SSCALED, V16_FLOAT, V32_FLOAT), so u_vbuf
converts R8G8B8_SNORM and R8G8B8A8_SNORM at every world draw and reads the VBOs through their
malloc copies. R16G16B16_SNORM and R16G16B16A16_SNORM are in the table (V16_SNORM), and the same
VBO already feeds the lightmap coordinates as GL_SHORT (V16_SSCALED), so 16-bit fetch works on the
RSX's big-endian buffers.

With `vanormalshort 1` the world VBOs are built from `svertex` (40 bytes: position, normal as 3
shorts + padding, texture and lightmap coordinates, tangent as 4 shorts; s = b*32767/127 rounded,
-128 taken as -127 as GL does). Everything that reads the layout follows the cvar: `genverts` and
`addvbo` (octarender.cpp), `changevbuf` (normal and tangent as GL_SHORT for the lightmap pass, the
stride for every pass), the position-only passes (depth, occlusion, alpha outlines, sky:
`vavertsize()`), `readva` (`writeobj` converts back: the same .obj in both). The glass and
waterfall surfaces (material.cpp, through gle with signed byte normals of +-1) send them as shorts
too, exactly. A change rebuilds the world (`allchanged`). Models are not affected (float normals,
GL_SHORT tangents already; GPU skinning's bone weights and indices are U8, fetchable); grass keeps
GL_BYTE in its GART stream buffer.

Measured on the PC (Debian trixie container, the work tree built for amd64, llvmpipe forced to GL
2.1 / GLSL 1.20 with the extensions nv30 lacks removed, the benchmark demo at 960x540, SDL's clock
advanced 33 ms per frame so runs repeat, 10 screenshots 3 s apart):

| | vanormalshort 0 | 1 |
|---|---|---|
| cave's world vertex buffers (`printvbo 1`) | 4.40 MB (137,532 vertices, 9 VBOs) | 5.50 MB (+1.1 MB of VRAM) |
| pixels against the unpatched build | the same (frames 2-10; frame 1 differs by 48 pixels between two runs of the unpatched build too) | ~170 of 518,400 differ, by at most 2 of 255 |

s/32767 can't equal b/127 except for 0 and +-1 (127 doesn't divide 32767), so the shading moves by
up to 1.5e-5 of a unit normal: invisible, but not the same pixels, so **the default stays 0**. The
RSX's own conversion and fp precision differ from llvmpipe's anyway; if the PS3 shows the world
right with it and the numbers below hold, it can become the default.

On the PS3 the cost is the +1.1 MB of VRAM (25% more in the world VBOs). The gain: no malloc
copies of the world VBOs (up to the 4.4 MB of them, those the demo draws), no u_vbuf conversion
or upload per world draw (CPU), and no readbacks for them. Whether the readbacks also set
nouveau's keep-copy hint in Sauerbraten (copies of every later buffer) is what `sysmem_hint` shows.

Package: cube2 0.0.20201229+dfsg-1+ps3.442~dev20261018143125 (build/debs/cube2), not deployed, not published.
Recipe variants: `shortnormals` (`vanormalshort 1`), `bytenormals` (`vanormalshort 0`, the default
spelled out).

The A/B on the PS3, same boot, desktop stopped, nobody playing on it; Mesa ps3.442 (0037, for the
stats) and the cube2 deb deployed first (`make deploy` of build/debs/mesa and build/debs/cube2),
`make bench-push`; `tools/bench-cam.sh` running on the PC for the whole batch, each run's photos
looked at before its numbers count (the cave tunnel and the bots drawn, lit as in base; a wrong
layout shows as black or garbled walls):

    tools/bench-cam.sh &      # on the PC
    S=NV30_SYSMEM_STATS=300
    # memory: the copies and readbacks per arm
    MEM=1 ENVS="$S" /var/tmp/bench/run.sh bench sauerbraten "base shortnormals" 3
    # fps and stalls without MEM
    STALLS=1 ENVS="$S" /var/tmp/bench/run.sh bench sauerbraten "base shortnormals" 3
    python3 /var/tmp/bench/report.py --stalls --mem /var/tmp/ps3-bench/<run dirs>

Read `sysmem_read_mb` and `sysmem_mb` (expected: base a few MB, shortnormals ~0 unless other
buffers are read), `sysmem_readbacks`, `sysmem_hint`, against `mem_malloc_used`, `mem_heap_rss` /
`_swap`, `stall_play_s` and the steady fps (expected up: no per-draw conversion). Check game.log
for GL errors. Optional, for the CPU: one `PERF=1` run per arm, `util_format_r8g8b8*_snorm` and
translate should leave Sauerbraten's profile.

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
- **Mesa (nouveau)**: the hint's switches are Mesa 0037 (section above). Left: whole-buffer
  copies of buffers u_vbuf reads (needed while it reads them).
- **Sauerbraten (cube2)**: done, cube2 0011 `vanormalshort` (section "Sauerbraten's world
  normals as shorts" above). Left: grass (`grassvert.bounds`, GL_BYTE in a GART stream buffer: a
  per-draw conversion but no copy) and the edit-mode prefab preview.
- **OpenArena opengl2**: r_keepImages for renderergl2 (0025 is opengl1's): every pass and map
  change reloads, decodes and mipmaps every texture; and the shader script parsing (COM_ParseExt
  2.8% of a profile) at every load.
