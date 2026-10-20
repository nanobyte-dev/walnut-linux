# Memory: where the games' RAM and VRAM go

This is where each suite game's memory goes on the PS3, what we found in the games' code and in
Mesa, and what each memory change measured. The state on 2026-10-19: the Yamagi Quake II table cut
is the default, and three memory fixes won their same-boot A/B in batch 17 (ioquake3's SNORM
colours, Mesa 0037's copy limit, Sauerbraten's short normals), though only the first is on by
default. Texture compression at upload (Mesa 0038) cut Sauerbraten's VRAM from the 117 MB cap to
73-78 MB without a resolved fps change and did nothing in SuperTuxKart and OpenArena (batch ssdroot2
P2, [below](#measured-on-the-ps3-batch-ssdroot2-p2)); batch ssdroot8 ran it on the rest of the suite: `all` took
SuperTuxKart from 95.5 to 58.1 MiB of VRAM at the same fps, and the other three games had nothing the
modes take ([below](#measured-on-the-ps3-batch-ssdroot8-the-rest-of-the-suite)). It stays off. The kernel's own share is in [KERNEL-MEMORY.md](KERNEL-MEMORY.md), and the cuts
nobody has tried yet are in [backlog/memory.md](backlog/memory.md).

## The console's memory budget

The PS3 has 256 MB of XDR RAM and 256 MB of VRAM. Linux gets 240 MiB of the XDR; with the release
kernel (.472) `MemTotal` is 222716 kB (217.5 MiB). The split, from the OpenArena measurement of
2026-10-15 with the kernel rows updated for the release kernel:

| Of 256 MB XDR | MB | Note |
|---|---:|---|
| Hypervisor (lv1, never reaches Linux) | 16 | |
| Kernel reserve | 22.5 | release kernel, `245760 - 222716` kB: image ~8.8, ps3fb console frame 9.0, `struct page` 3.75, hashes and stacks ~0.9 ([KERNEL-MEMORY.md](KERNEL-MEMORY.md) 1) |
| Slab, page tables, vmalloc | ~29 | 2026-10-15, debug kernel: modules 3.6, zram table 1.5, seccomp filters 1.0, `cfg80211` 0.5 |
| Kept free (reserve, watermarks) | ~20 | `vm.min_free_kbytes` 8 MB: two hangs without it |
| GART (CMA, on demand) | 6-14 | the rest of the 64 MB CMA area holds page cache |
| OpenArena (opengl1) | 74 (+10 swapped) | engine data: map, models, decoded sounds, hunk |
| X | 6 (+27 swapped) | 8 MB wallpaper segment, 16 MB heap (glamor, Mesa) |
| Services, desktop helpers, file cache | the rest | helpers frozen in game mode ([DESKTOP.md](DESKTOP.md)) |

Swap comes in three tiers: zram (384 MB, empty in the runs below), the VRAM swap ps3vswap
(priority 300, about 96 MB of VRAM, the rest of its 1 GB on the USB disk) and the USB swap
partition. Every MB a game holds beyond what fits goes to the USB disk, and every page read back
is a stall. That disk hurts more than its speed suggests: its bridge (JMS567) speaks only
Bulk-Only Transport, one command at a time, so a game's page fault queues behind swap writes.

The VRAM side matters as much: VRAM doubles as the swap's first tier, so a MB of textures is a MB
of swap moved to the disk. The PPE reads VRAM at 10 MB/s ([RSX.md](RSX.md)), which is why nouveau
keeps system-memory copies of the VRAM buffers it reads (below).

The goal stated on 2026-10-15: a game can use 2/3 of the RAM (~160 MB) without stalls, without
removing features.

## The data

One same-boot batch, 2026-10-18 00:40-01:40 UTC, `PERF=1 MEM=1`, one run per game, Mesa ps3.299.
Snapshots at the window's start + 1 s, its middle and 2 s before its end (what MEM=1 records and
how memprof.py classes the mappings: [PROFILING.md, "What MEM=1 records"](PROFILING.md#what-mem1-records)).
Profiled runs: their fps don't count, their memory does. The run directories are listed in
`build/tmp/prof-runs.txt` and were pulled into `build/bench/`. MB are MiB; numbers are the middle
snapshot unless a column says otherwise.

- dsda-doom and OpenTTD have no snapshots. dsda-doom's `LOAD_MARK` (`R_Init`...) never prints in
  0.30, so the window never started (fixed: the recipe now also takes `Playing demo:`). OpenTTD
  with SDL's software video never feeds the Gallium HUD, so its HUD-based window never opened; it
  needs a `LOAD_MARK` or the `gl` variant.
- Webcam (`build/bench-cam/<run>-60s.jpg`, `-150s.jpg`): every game shows its picture. OpenArena's
  60 s photo is its loading screen (the window starts later). Armagetron's 150 s photo is black
  with the mouse pointer: its recorded round ends about 65 s into the window, so its middle and end
  snapshots are the game idling after playback, with no ps3rsx client line (it had closed its GL
  context). Its start snapshot is the playing game, and the RAM numbers barely differ.

## Per game (middle snapshot)

"Code" is the file-backed Rss of the game's binary and libraries, of Mesa (libgallium, the GL
dispatch, libdrm) and of every other library: clean pages that the kernel drops and reads back
from the USB disk, not swap. "Malloc in arenas" is glibc's: used, free (fragmentation or a top
chunk not given back) and the number of arenas. "Mmapped" are blocks above the mmap threshold (the
engines' hunks among them, mostly never touched).

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

Yamagi Quake II's row is from before its PRNG fix (below), which took 70 MB off it.

Thread stacks are 0.1-0.4 MB in every game but Luanti (16 threads: 1.2 MB + 2.7 swapped). The GPU
mappings (`/dev/dri`) have no Rss because ps3rsx maps buffers `VM_PFNMAP`. Shared memory and other
files are under 1.3 MB everywhere.

### What is swapped out, by class

| Game | swap | heap | anon | libs | stacks | Mesa |
|---|---|---|---|---|---|---|
| Luanti | 47.1 | 28.5 | 11.0 | 3.6 | 2.7 | 1.2 |
| OpenArena | 19.7 | 13.9 | 2.0 | 2.6 | 0 | 1.1 |
| Sauerbraten | 12.3 | 9.6 | 0.6 | 1.0 | 0.3 | 0.8 |
| Yamagi Quake II | 3.4 | 1.5 | 0.9 | 1.0 | 0 | 0 |
| Armagetron | 1.8 | 0 | 0 | 1.7 | 0 | 0 |

The rest swap nothing. Heap dominates, and in the three big games it is about the size of malloc's
free memory. Sauerbraten's arena is 80.7 MB with 17.4 free, and its heap is 66.1 resident + 9.6
swapped = 75.7, so at least 12 MB of the free chunks are resident or swapped. OpenArena swapped
13.9 MB of heap while 15.9 MB sat free at the window's start. The kernel swaps out what nobody
touches, and nobody touches free chunks: that is garbage written to swap. Library swap is their
relocated data and .bss (private dirty pages); Mesa's is libgallium's 1 MB of relocations.

### Growth over the window

Anonymous memory (Anonymous + swap) is flat in every game across the two minutes (within 1 MB), so
no game leaks RAM in this window. Inside malloc it moves:

- **Luanti**: used 102.2 -> 96.8 -> 94.4 MB while free grows 12.8 -> 22.9 -> 28.2. Blocks and
  meshes unloaded are freed into the arenas and stay there (arena #4 holds 18.2-22.2 MB free of
  31.2). The free part grows with play time; this flight is three minutes.
- **OpenArena**: used 21.4 / 33.5 / 20.8 at start / middle / end, a 12 MB transient around the
  middle whose pages are kept (free 15.9 / 3.8 / 16.5).
- **Extreme Tux Racer**: used 17.5 -> 18.5 -> 19.9 MB (+2.4 MB in 117 s), and its GART 8.8 ->
  12.8 -> 16.8 MB with 161 -> 163 handles: two new ~4 MB GART buffers, one a minute. A candidate is
  the track marks in the snow (a growing list, drawn through our batching patch's stream buffers).
  Only a longer run tells a cap from a leak.
- File-backed Rss falls in most games (Sauerbraten 17.1 -> 9.3, Neverball 23.5 -> 15.6, ETR 20.3
  -> 14.1): code and data pages dropped from the page cache and read back when needed.

### Malloc arenas

glibc gives a thread that mallocs while another holds an arena its own arena, up to 8 x cores = 16
on the PPE's two threads. Most games have 3-4, all but the main one at 0.1 MB, so
`MALLOC_ARENA_MAX` saves under 0.3 MB there. ClassiCube has 6 (one of 1.7 MB). Luanti has 14: #0
47.2 MB, #1 32.6, #4 31.2 (18.2 free), #5 3.1, #7 4.3, and nine at 0.1. A free hole in one arena
can't serve another thread, so fewer arenas mean less fragmentation, traded against lock contention
between the renderer, server, emerge and mesh threads.

### The GPU side (ps3rsx's census)

- Every game at 1920x1080 has three 8100 KiB buffers: two back buffers and the depth/stencil buffer.
  Mesa's DRI3 loader keeps 2 back buffers when X copies rather than flips (3-4 only when page
  flipping: `dri3_update_max_num_back`). Xorg imports the back buffers too, so its 3 x 8100 KiB are
  its own front plus the game's two; the census counts shared buffers once per client (Armagetron's
  mid snapshot: Xorg drops from 25.4 to 9.5 MB once the game's buffers are gone). That is 24 MB of
  VRAM per game for the window, at minimum.
- The VRAM a game takes comes out of the VRAM swap. "VRAM cpu" was 99% used in Sauerbraten (218 of
  220 MB), and Luanti's VRAM swap had 88 MB in VRAM and 54 MB on the disk at the middle snapshot.
- GART (system RAM, taken from CMA on demand): 6.7-12.8 MB per game (push buffers, Mesa's upload
  and stream buffers), plus Xorg's 6.7. "GART backed by memory" is 17-27 MB system-wide.
- The census lists only the first eight distinct buffer sizes above 64 KiB per client, so new
  buffers of other sizes (ETR's growth) don't show which they are.

### Shared by every game

- **libgallium**: 24 MB mapped, 1.4-7.5 MB resident (only what runs), no LLVM (our packaging builds
  nouveau and softpipe only). Its writable segment is 1.0 MB of relocations (`.opd` 537 KiB of
  ELFv1 function descriptors, `.data.rel.ro` 466 KiB, 62722 relocations), private to each process,
  plus a 1.8 MB `.bss` of which only the touched part counts. Mesa's private dirty memory is
  1.4-1.5 MB per game. Part of those relocations is nv50/nvc0 and softpipe code.
- **Sound path**: every game maps libpulse, libpulsecommon and their codecs (libsndfile, FLAC,
  opus, vorbis, mpg123) and dbus/systemd through ALSA's pulse plugin: 0.5-3.1 MB Rss per game,
  mostly shared clean pages, a "threaded-ml" thread in the OpenAL games, and a pulse server process
  outside the game.

## Findings in the games' code

- **Yamagi Quake II: 32 MB in a random number generator.** `src/common/shared/rand.c` (the KISS
  PRNG, also in rogue/ and xatrix/) had `static uint64_t QARY[0x200000]`, 16 MiB, and
  `randk_seed()` writes every entry. The file is linked into the client (`quake2`, whose 23 MB .bss
  was 18 MB resident, 16 of it this table) and into `game.so` (16 MB .bss, all resident): 32 MB of
  the game's 77. A lag of 4096 entries keeps a period far beyond anything a game draws. Fixed by our
  yquake2 0002 (below).
- **Luanti trims only after 256 MB freed.** `porting::TriggerMemoryTrim()` (src/porting.cpp) calls
  `malloc_trim()` from the emerge and mesh threads once 256 MiB of map blocks, voxel manipulators and
  meshes were freed (upstream's workaround for exactly the fragmentation above). On a 217 MB
  machine the count rarely gets there. Our luanti 0007 makes the threshold a variable.
- **OpenArena's hunk**: 30.7 MB in use (low permanent 16.1, high 14.7), temp high-waters 17.7 and
  15.7, so 2.7 MB of pages touched by loading are never used again ("unused highwater" in its own
  meminfo, in game.log). The hunk (128 MB) and zone (48 MB) are one 176 MB mapping with 34.6 MB
  resident; the rest is never touched and costs nothing.
- **QuakeSpasm's 256 MB heap**: 24.8 MB touched. The cache inside it never needs flushing at this
  size, so sounds and models stay. Nothing to gain.
- **glibc's dynamic mmap threshold**: after the first free of a block mmapped above 128 KiB, glibc
  raises the threshold to that size (up to 32 MiB) and the trim threshold to twice it (up to 64
  MiB). From then on texture and map scratch buffers of a few MB come from the heap and leave holes,
  and up to 64 MB of free top is never given back. That is why OpenArena and Sauerbraten keep 16-17
  MB free in their main heap.

## Cuts, ranked

The MB are from the 2026-10-18 batch: upper bounds where the free memory's residency isn't known
page by page ("pages in free chunks" counts whole pages inside free chunks of 8 KiB and more, from
malloc_info's size lists, plus the top chunks). Effort: env = no rebuild, pkg = our package
rebuilt. Results are rows of `games/bench/results/fps.csv` ([glossary](README.md#glossary)).

| # | Game | Cut | Saves (MB) | Effort | Result | Risk |
|---|---|---|---|---|---|---|
| 1 | Yamagi Quake II | PRNG table of 4096 entries (`YQ2_KISS_QSIZE`) | **32** | pkg | measured, row 57: RSS 125 -> 56 MB, swap used 80 -> 9 MB, gameplay stalls 22.6 -> 0.05 s, steady 65.7 -> 68.7; **the default** since yquake2 .415 | client-side random effects differ (the demo itself doesn't) |
| 2 | Luanti | trim the arenas: `LU_TRIM_MB=16` or `64` (variants `trim16`, `trim64`), or `MALLOC=trim` | 17-24, growing with play time (16.4-19.7 in free chunks + 2.5-4.2 top) | pkg / env | `trim16` row 57: no gain and more stalls (3.35 -> 8.6 s), rejected. `trim64` and `MALLOC=trim` not run | a trim holds each arena's lock for its walk |
| 3 | Sauerbraten | `MALLOC=trim` or `MALLOC=mmap` | 17 (17.0-17.4 in free chunks of the main heap) | env | row 61: noise (below); not shipped | mmap: a syscall and zeroed pages per big alloc |
| 4 | OpenArena gl1 | `MALLOC=trim` or `MALLOC=mmap` | 12-16 at rest (11.7-12.2 in free chunks + 3.6 top), its 13.9 MB of swapped heap | env | row 61: noise (below); not shipped | as above |
| 5 | Sauerbraten | `texreduce 1` (variant `tex1`) | VRAM ~45-60 of its 117 (textures are ~87 MB of it; DDS and JPG at a quarter) | env | not run | blurrier textures |
| 6 | Luanti | `MALLOC=arena2` (or the recipe's `malloc` variant) | part of arena #4's 18-22 MB holes, overlaps #2 | env | not run | lock contention between 4 busy threads |
| 7 | Luanti | settings variants (view range, client/server unload, active2, chunk3, lean) | estimates in [games/README.md](games/README.md); heap here is 103 MB of map blocks and meshes | env | row 67: base stays. `cliunload` has more stalls (3.8 and 5.2 s against 1.5 and 2.1 s), `lean` draws a black void; `chunk3` empties the world (batch 16); `view40` is to be repeated (PLAN 2.6) | per variant |
| 8 | Armagetron, Neverball, Yamagi, ETR, ClassiCube, QuakeSpasm | `MALLOC=trim` | 4.2 / 2.3 / 2.7 / 0.8-1.9 / 1.0 / 1.3 (mostly top chunks) | env | not run | small |
| 9 | OpenArena | madvise the hunk's unused high-water after a map load | 2.7 (gl1) | pkg | not tried: [backlog/memory.md](backlog/memory.md) | none known |
| 10 | every game | Mesa with nv30 only (no nv50/nvc0, softpipe elsewhere) | ~0.3-0.5 private each (estimate) | pkg | not tried: backlog | softpipe is the mesa-tests reference |
| 11 | every game | sound straight to ALSA, no pulse plugin and server | 0.5-3 per game + the server | config | not tried: backlog (game-mode work) | underruns |
| - | most games | `MALLOC=arena1/arena2` | < 0.3 (extra arenas are 0.1 MB) | env | not worth it | - |

Not worth a change at these sizes: thread stacks, X-Moto's heap (0.3 MB free), QuakeSpasm's heap,
OpenArena's 176 MB hunk mapping (untouched pages). Also tried on Luanti:
`vm.watermark_boost_factor=0`, row 62: stalls 12.5 / 0.8 / 2.0 / 2.1 s alternating 15000 / 0
(first run an outlier), no clear gain, not shipped.

## The malloc presets and ps3-malloctrim

The bench gives every game a malloc preset (games/bench: client.sh, run.sh, report.py,
selftest.py). `BENCH_MALLOC` (run.sh `MALLOC=`, or an `ENVS` token) is a comma list:

| Preset | Environment of the game |
|---|---|
| `arena1`, `arena2` | `MALLOC_ARENA_MAX=1` / `2` |
| `mmap` | `MALLOC_MMAP_THRESHOLD_=262144 MALLOC_TRIM_THRESHOLD_=1048576`: blocks of 256 KiB and more mmapped and unmapped at free, the main heap's top given back past 1 MiB; fixing them stops the dynamic raise |
| `trim` | `ps3-malloctrim.so` preloaded, `PS3_MALLOC_TRIM=10` (`MALLOC_TRIM_S`), only in the recipe's `COMM` |
| `tune` | `arena2,mmap,trim` |

bench.txt gets `malloc=`, and report.py shows it after the variant (`base@mtune`,
`base@perf+marena2_trim`), so such runs never group with plain ones. `ENVS` works in `run.sh suite`
too (each game and variant once per token per round). A recipe's own `LD_PRELOAD` and MEM=1's
ps3-mallocstat are kept (client.sh `preload_add`). No preset is a default.

**ps3-malloctrim.so** (ps3-gpu-tools/preload/ps3-malloctrim.c, in ps3-gpu-tools since 0.403) is one
thread with a 64 KiB stack and all signals blocked that calls `malloc_trim(0)` every
`PS3_MALLOC_TRIM` seconds. It never mallocs, so glibc gives it no arena. With `PS3_MALLOC_TRIM_LOG=1`
(the bench sets it) it prints `ps3-malloctrim: <n> <ms> ms rss <kB> -> <kB> swap <kB> -> <kB> freed
<0|1>` per trim into game.log. One caveat: a log line written while the game has half a line
pending on stdout splits that line (games print whole lines through `stdbuf -oL`; the selftest's
fake game, which splits one on purpose, needed a 1 s period).

Measured in batch 8 (row 61, Mesa ps3.408, two rounds each against `-`):

| | steady fps | gameplay stalls (s) | lowest MemAvailable (MB) |
|---|---|---|---|
| Sauerbraten base / `MALLOC=trim` / `MALLOC=mmap` | 39.2 / 38.2 / 39.9 | 2.1 / 2.0 / 2.1 | 23.2 / 21.5 / 19.9 |
| OpenArena base / trim / mmap | 67.8 / 70.9 / 70.8 | 0.0 / 0.1 / 0.0 | 36.4 / 26.4 / 30.9 |

The rounds disagreed with each other, so the row reads as noise. The trim lines of those four
`trim` runs (`build/bench/ps3-bench-20261018T0{71150,72508,73839,75416}Z-*`) show what a trim does
on the PPE: the first big walk, about 30 s in, gave back 23.0-23.7 MB of Rss in Sauerbraten in
26.9-44.0 ms and 12.4-13.4 MB in OpenArena in 14.0-15.5 ms; every later trim took 0.1-3 ms and
freed little (42.9-45.5 MB of Rss over a Sauerbraten run, 13.1-38.1 MB over an OpenArena run).
Swap never shrank: a trim returns resident free pages, and pages already swapped stay swapped until
touched. On the host (Zen 2) the first trim of 80 MB of holes took 10-15 ms, so the PPE is about 3x
slower.

**Built, not measured yet**: `MALLOC=arena2` and `tune` on Luanti, `trim64`, and `MALLOC=trim` on
the small games. The commands, same boot, a profiled round for the MB and three plain rounds for
stalls and fps:

    MEM=1 ENVS="- MALLOC=trim MALLOC=mmap MALLOC=arena2 MALLOC=tune" /var/tmp/bench/run.sh bench luanti base 1
    MEM=1 /var/tmp/bench/run.sh bench luanti "base trim64" 1
    ENVS="- MALLOC=tune" /var/tmp/bench/run.sh bench luanti base 3
    python3 /var/tmp/bench/report.py --mem /var/tmp/ps3-bench/<run dirs>

Read `mem_swap`, `mem_heap_rss`/`mem_heap_swap`, `mem_malloc_free`, `min_memavailable_kb` and the
`ps3-malloctrim:` lines. MEM=1's snapshots hold the game's mmap lock for 30-600 ms, so fps and stalls
come from the runs without it. A preset that wins would become a default through `ps3-game` (game
mode's launcher sets the environment for every game) or per-game wrappers.

## OpenArena opengl2 (gl2min): the 16 s freezes and the memory

The profiled gl2min run of 2026-10-18 (`PERF=1 MEM=1`, ioquake3 ps3.396, Mesa ps3.408~dev,
`build/bench/ps3-bench-20261018T080250Z-openarena-gl2min`) had 54.7 s of gameplay stalls, a 16148 ms
longest frame, 66.5 MB swapped and 117 MB of VRAM. Two causes, one in our report and one in memory.
Both are settled now: since batch 17 gl2min runs at 57-60 steady fps with 1-2 s of gameplay stalls.

### The 16 s freeze was the load's last frame, counted as play

Each demo pass reloads the map. report.py cut the reload at 1 s after `CL_InitCGame:`, but in
opengl2 the load ends in one long frame with no SwapBuffers in it: the cgame load, `cl_preloadLate`
(35 models, skins and shaders, 7-12 s in opengl2 with the PPE swapping), then `RB_ShowImages`
(0.1-1.5 s, every texture drawn once) and the first demo frame. That frame starts inside the reload
and ends 4-5 s after the cut:

| Pass 3 of the profiled run (s from start) | |
|---|---|
| 131.5 | RE_Shutdown, R_Init: 55 GLSL programs kept, 0.27 s |
| 137.1 | world loaded |
| 143.06 - 159.20 | **the 16148 ms frame** |
| 154.0 | `cl_preloadLate: 35 models, skins and shaders ... in 10315 msec`, `CL_InitCGame: 19.13 seconds` |
| 155.06 | report.py's old cut |
| 156.5 | `1533 msec to draw all images` |

Of the 54.7 s, 38.8 s were such frames (16148, 12985 and 9640 ms). report.py now gives a frame that
starts inside a reload segment to the reload (passsplit.py already went by the frame's start). The
profiled run became 16.0 s of gameplay stalls with a 6263 ms longest frame; a second run
(`20261018T114707Z`) 9.7 s and 2125 ms. OpenArena opengl1 runs move by 0-4 s (20261018T001126Z:
19.2 -> 15.1 s).

### The play stalls that were left: memory

The remaining stall frames (1.0-6.3 s) were the first seconds after each load, plus single 0.5-1 s
frames when a bot joins. The samples during them show a machine out of memory, not a compile:
MemAvailable 13-60 MB, swap-ins of 25-260 pages and VRAM swap write-backs of 256-1024 pages every
0.5 s, swap-out bursts of 1.2-1.7k pages, the game's VRAM going 70 -> 114 MB while GART staging
peaks at 48 MB during the loads. The game held Rss 79-104 MB plus 58-68 MB swapped (opengl1: 71 +
20).

| MEM=1 snapshots (start / mid / end) | opengl2 gl2min | opengl1 |
|---|---|---|
| malloc in use (all arenas) | 56 MB (in a load) / **95** / 94 | 21-34 |
| heap Rss + swap | 51.3 + 43.7 | 22.8 + 13.9 |
| VRAM (ps3rsx, the game) | 100 / 111 / 66 MB, 117 peak | 36 |
| VRAM swap | 88 MB of it in VRAM, the rest written back to the USB disk all through play | |

Not the causes: GLSL compiles (the 55 programs are built once at the first start in 3.0-3.4 s, kept
across every renderer restart by 0004, 0.02-1.3 s to take them back, none built during play:
`glsl*_deferred=0`), and FBOs (gl2min sets `r_ext_framebuffer_object 0`). Mesa's disk shader cache
caches the GLSL front end's NIR for nv30; it shortens only the first start (batch 17 measured no
load gain: [PROFILING.md](PROFILING.md)).

### Why opengl2's malloc held 60 MB more

renderergl2 sent vertex colours as `GL_UNSIGNED_SHORT` normalized (R16G16B16A16_UNORM), a format
nv30 can't fetch (`nv30_vtxfmt_table`). Every draw from the tess VAO (the world, effects, the HUD;
everything but MD3 models, since `r_vaoCache` is 0) went through u_vbuf, which converts the colours
to floats on the CPU and maps the vertex and index buffers for reading (perf: 0.9% of the samples
in the conversion). A read map of a VRAM buffer makes nouveau keep a malloc'ed copy of the whole
buffer, and after four flushes in a row with a readback nouveau sets a hint that gives every new
VRAM buffer a copy (next section). The MD3 animation VBOs got copies too: 62.2 MB for the demo's
models (223 VBOs, counted on the host). That is about the 60 MB.

Where the load CPU went (perf, a window with three reloads): `R_MipMapsRGB`'s `powf` per texel
channel 7.8% of all samples (picmip and every mip level are made on the CPU at each load and for
every player a joining bot brings), `COM_ParseExt` 2.8% (shader scripts), `R_RegisterMD3` 2.4%,
libdeflate 2.7%. opengl2 has no `r_keepImages` (0025 is opengl1's), so every pass reloads every
texture.

### What was implemented (ioquake3 ps3.462, patches 0027-0029)

| Patch | Switch (default) | What | Host check (llvmpipe, GL 2.1, the bench demo, 30 frames) |
|---|---|---|---|
| 0027 | `r_vaoSnormColor 1` (latched) | colours as GL_SHORT normalized (v >> 1, 0 -> -1 so a zero alpha still fails an alpha test under either GL rule), which nv30 fetches (V16_SNORM, as the normals): no u_vbuf, no buffer reads, no copies, no hint | 17 pixels differ by 1 level |
| 0028 | `r_mipMapPowf 0` | the gamma-correct mipmap bytes from 255 thresholds found once by bisection with the same powf, plus a 4096-bin start table: the same bytes without powf | identical; every float in [0,1] equal on x86-64 glibc, every 16th + all within 200000 ulps of each threshold on ppc64 glibc (qemu) |
| 0029 | `r_md3PackedFrames 0` (opt-in, latched, needs `r_normalMapping 0`) | animated MD3 frames as MD3's own 16-bit positions + 4 unsigned-byte normals, 12 bytes a vertex instead of 20 (V16_SSCALED, U8_UNORM: both native on nv30), decoded in the 4 vertex-animation programs by `u_PackedFrames` (lightall draws single-frame models with that variant too, and they keep floats): the demo's model VBOs 62.2 -> 37.5 MB of VRAM | 1734 pixels differ, by at most 2 levels |

Recipe variants (games/bench/recipes/openarena.sh): `gl2min_color16` (0027 off), `gl2min_mippowf`
(0028 off), `gl2min_md3pack` (0029 on); `gl2min_up` turns 0027-0028 off too.

### Measured on the PS3 (batch 17)

Batch 17, 2026-10-18 17:38-20:40 UTC, kernel .433, ioquake3 .462, Mesa .442~dev, `STALLS=1`, fps.csv
row 69, results commit 3939a48. Each variant x2 or x3, interleaved:

| Variant | steady fps | gameplay stalls (s) | swap stall s | 1% low | peak VRAM (MB) | lowest MemAvailable (MB) | peak Rss (VmHWM, MB) | map load (s) |
|---|---|---|---|---|---|---|---|---|
| gl2min (0027 on, the default) | 59.5 | 2.4 | 0.9 | 21.9 | 117.1 | 15.2 | 86-89 | 10.1-10.3 |
| gl2min_color16 (0027 off) | 52.4 | 12.9 | 20.1 | 10.8 | 116.6 | 10.1 | 110-112 | 9.9-10.7 |
| gl2min_md3pack (0029 on) | 59.0 | 1.3 | 1.0 | 23.5 | **97.7** | 16.6 | 86-89 | 10.2-10.8 |
| gl2min_mippowf (0028 off) | 60.0 | 0.8 | 0.8 | 23.6 | 117.2 | 11.7 | 87-88 | 11.7-11.9 |

Batch 16, the same boot on ioquake3 .396 (row 68): gl2min 51.8 steady, 11.0 s of stalls, 20.1 s of
swap stalls.

- **0027 is the win**: 7 fps more, stalls 12.9 -> 2.4 s, swap stalls 20 -> 0.9 s, 21-26 MB lower
  peak Rss and 5 MB more MemAvailable. It is the default.
- **0029** frees 19.4 MB of VRAM (expected 24.7: the model VBOs) and leaves the stalls the same
  within the run spread. It stays opt-in (`r_md3PackedFrames 0`): the batch recorded no check of the players on
  the webcam, which a default change needs (a wrong vertex fetch loses the players, not the world).
- **0028** takes 1.5 s (13%) off each map load. The 0028-off arm had the lowest stalls (0.7-0.9 s),
  which reads as run spread at these levels; 0028 stays on.

## nouveau's system memory copies of VRAM buffers (Mesa 0037)

From the source we build (build/work/mesa, src/gallium/drivers/nouveau/nouveau_buffer.c,
nouveau_context.h, nv30/). Mesa 0037 adds switches that limit the copies; it is in every Mesa since
.442~dev (the PS3 runs .476), all off by default, and batch 17 measured them.

### How the copies work

- **Per buffer.** `nv04_resource.data` is a malloc'ed copy of the whole buffer (`width0` bytes). A
  map of an idle VRAM buffer that doesn't discard makes one if there is none
  (`nouveau_buffer_cache`): u_vbuf's `PIPE_MAP_READ` maps of the vertex buffers it converts (and of
  the index buffer when it unrolls indices), and also a write map of an already written range
  without an invalidate flag. Every later map of that buffer returns a pointer into it. If the
  buffer was written by the CPU without a copy (status `DIRTY`), making the copy is a **readback**:
  an M2MF copy of the whole buffer to a GART staging buffer, `BO_WAIT` on it (a kick and a wait for
  everything queued before it, which drains the GPU), then a memcpy. Once a copy exists the writes
  go into it first and from there to VRAM (staging + M2MF), so it never goes stale and that buffer
  needs no more readbacks.
- **Freed** only with the buffer (`nouveau_buffer_destroy`), or when the GPU writes the buffer
  (`GPU_WRITING`: copy_buffer or stream output, not vertex buffers). Invalidation, discard maps and
  reallocation keep it.
- **The hint is screen-wide.** `nouveau_context_update_frame_stats` runs at every `pipe->flush`
  (nv30_context_flush: SwapBuffers, glFlush, fences), shifts a bit history of "this flush had a
  readback", and after four in a row sets `screen->hint_buf_keep_sysmem_copy`. Nothing clears it.
  From then on `nouveau_buffer_create` gives every new VRAM buffer an (uninitialised, not dirty)
  copy at its creation, whatever its usage. Buffers made before the hint keep none.
- **What it buys.** CPU reads of VRAM run at 10 MB/s, so without a copy each converted draw would
  need a readback and a GPU drain: the copy is what makes u_vbuf usable at all for the buffers it
  reads. The hint covers apps that keep making new buffers that get read.
- **Why OpenArena's opengl2 set it in its first frames.** `RB_UpdateTessVao` orphans the tess
  buffers with `glBufferData(NULL)` before each tess flush. nv30 has no buffer invalidation
  (`caps->invalidate_buffer = false`), so Mesa's bufferobj_data makes a new pipe buffer each time
  (DYNAMIC, VRAM, no copy), BufferSubData writes it (`DIRTY`) and u_vbuf's colour conversion reads
  it: a readback and a drain at every tess flush. Then the MD3 models load (STATIC, VRAM): 223
  buffers, 62 MB, each with a copy nothing reads.
- **Sauerbraten** sends its world normals and tangents as GL_BYTE, which nv30 can't fetch
  (renderva.cpp), so u_vbuf reads its world VBOs on every draw and they need their copies. Batch 17
  showed the hint is never set there.

### The options weighed

| Option | Gain | Cost / risk | Done |
|---|---|---|---|
| Never set the hint (`NV30_SYSMEM_COPY=0`) | the copies of buffers never read (OpenArena gl2: ~62 MB) | an app that orphans read buffers pays a readback and drain per new buffer: OpenArena gl2 with 16-bit colours would drain at every tess flush, so only together with `NV30_INVALIDATE_BUFFER=1` | yes, off |
| Hint only for DYNAMIC buffers (`NV30_SYSMEM_COPY=2`) | the same 62 MB (MD3 VBOs are STATIC); the streaming case the hint is for keeps working | static buffers read later get one readback each at their first read | yes, off; measured |
| Buffer invalidation (`NV30_INVALIDATE_BUFFER=1`) | orphaning keeps the buffer and its copy: no new pipe buffer, copy, readback or vertex array revalidation per tess flush; the hint's trigger goes away for this pattern | the code is nv50/nvc0's (`nouveau_buffer_invalidate`; nv30 already reallocates GART buffers in place on discard maps); applies to every orphaning game (Sauerbraten's particles and grass: GART STREAM buffers, now reallocated in place when busy instead of replaced) | yes, off; measured |
| The game sends fetchable formats | no u_vbuf, no copies, no per-draw CPU conversion | per game | ioquake3 0027 (default), cube2 0011 (opt-in) |

Three more options were weighed and not built (free copies after N frames without reads, u_vbuf
reading from the CPU-side upload source, converting at upload into a fetchable format); their
reasons are in [backlog/memory.md](backlog/memory.md).

### Switches (Mesa 0037)

`mesa/patches/0037-nv30-optional-limits-on-nouveau-s-system-memory-copi.patch`:

| Switch | Values | What |
|---|---|---|
| `NV30_SYSMEM_COPY` | 1 (default): as upstream; 0: never the hint; 2: the hint, once set, gives copies at creation only to `PIPE_USAGE_DYNAMIC` (GL_DYNAMIC_DRAW) buffers | `nouveau_screen.hint_buf_mode` (0 for nv50/nvc0) |
| `NV30_INVALIDATE_BUFFER` | 0 (default), 1 | `caps->invalidate_buffer` and `pipe->invalidate_resource` (buffers only) -> `nouveau_buffer_invalidate` |
| `NV30_SYSMEM_STATS` | N frames (0: off) | a `nv30-sysmem:` line on stderr every N frames, when the hint is set and at context destruction: copies now (count, kb), made at creation / at a read (count, kb), at a GART->VRAM migration, readbacks (count, kb). report.py: `sysmem_mb` (peak), `sysmem_create_mb`, `sysmem_read_mb`, `sysmem_readbacks`, `sysmem_hint` (frame, 0: never) |

The copies are counted for every nouveau driver (screen atomics, a `SYSMEM_COPY` status bit kept
across reallocation); the counting is the only change with the switches off.

### Measured on the PS3 (batch 17)

Row 69, `gl2min_color16` (the 16-bit colours, 0027 off) with `NV30_SYSMEM_STATS=300` against
`NV30_SYSMEM_COPY=2+NV30_INVALIDATE_BUFFER=1`, x2. The `nv30-sysmem:` lines at frame 6000-7200:

| Arm | hint | copies now | copies made at creation | readbacks | steady fps | gameplay stalls (s) | swap stall s | lowest MemAvailable | peak Rss |
|---|---|---|---|---|---|---|---|---|---|
| stats only (upstream behaviour) | set | 450, 61.0 MB | 470,860 (17.7 GB in all: every orphaned tess buffer) | 142, 5.3 MB | 53.7 | 9.9 / 12.3 | 20-26 | 10.4 MB | 108-110 MB |
| `NV30_SYSMEM_COPY=2` + `NV30_INVALIDATE_BUFFER=1` | never | 2, 74 kB | 0 | 12, 445 kB | 59.1 | 1.5 / 1.4 | 0.6-1.1 | 16.5 MB | 86 MB |

The switches remove the 61 MB of copies and the churn, and the stalls follow. `NV30_SYSMEM_COPY=0`
without the invalidation was left out on purpose (a drain at every tess flush). With 0027 on (the
default) OpenArena doesn't need them, and Sauerbraten never sets the hint, so the switches matter
for games we don't patch that send formats nv30 can't fetch. They are still off by default; making
them the PS3 default needs a run over the rest of the suite (players and world drawn, no GL errors
in game.log).

## Sauerbraten's world normals as shorts (cube2 0011, `vanormalshort`)

The game-side fix for Sauerbraten. `engine/world.h`'s `vertex` (32 bytes) sends `norm` and
`tangent` as GL_BYTE normalized. nv30's vertex formats (nv30_format.c, `nv30_vtxfmt_table`) have
no signed 8-bit type (U8_UNORM, U8_USCALED, V16_SNORM, V16_SSCALED, V16_FLOAT, V32_FLOAT), so u_vbuf
converted R8G8B8_SNORM and R8G8B8A8_SNORM at every world draw and read the VBOs through their
malloc copies. R16G16B16_SNORM and R16G16B16A16_SNORM are in the table (V16_SNORM), and the same VBO
already feeds the lightmap coordinates as GL_SHORT (V16_SSCALED), so 16-bit fetch works on the
RSX's big-endian buffers.

With `vanormalshort 1` the world VBOs are built from `svertex` (40 bytes: position, normal as 3
shorts + padding, texture and lightmap coordinates, tangent as 4 shorts; s = b*32767/127 rounded,
-128 taken as -127 as GL does). Everything that reads the layout follows the cvar: `genverts` and
`addvbo` (octarender.cpp), `changevbuf` (normal and tangent as GL_SHORT for the lightmap pass, the
stride for every pass), the position-only passes (depth, occlusion, alpha outlines, sky:
`vavertsize()`), and `readva` (`writeobj` converts back, so the .obj is the same). The glass and
waterfall surfaces (material.cpp, through gle with signed byte normals of +-1) send shorts too,
exactly. A change rebuilds the world (`allchanged`). Models are not affected (float normals,
GL_SHORT tangents already; GPU skinning's bone weights and indices are U8, fetchable); grass keeps
GL_BYTE in its GART stream buffer.

On the PC (Debian trixie container, amd64 build of the work tree, llvmpipe forced to GL 2.1 / GLSL
1.20 with the extensions nv30 lacks removed, the benchmark demo at 960x540, SDL's clock advanced 33
ms per frame, 10 screenshots 3 s apart):

| | vanormalshort 0 | 1 |
|---|---|---|
| cave's world vertex buffers (`printvbo 1`) | 4.40 MB (137,532 vertices, 9 VBOs) | 5.50 MB (+1.1 MB of VRAM) |
| pixels against the unpatched build | the same (frames 2-10; frame 1 differs by 48 pixels between two runs of the unpatched build too) | ~170 of 518,400 differ, by at most 2 of 255 |

s/32767 can't equal b/127 except for 0 and +-1 (127 doesn't divide 32767), so the shading moves by
up to 1.5e-5 of a unit normal: invisible, but not the same pixels. That is why the default is 0.

On the PS3, batch 17 (row 69, cube2 .442~dev, `NV30_SYSMEM_STATS=300`, x3 interleaved, one
shortnormals run left out for a key press):

| | base (`vanormalshort 0`) | shortnormals |
|---|---|---|
| steady fps | 38.8 | **45.1** (+17%) |
| 1% low | 25.0 | 30.4 |
| gameplay stalls (s) | 1.2 | 1.6 (the same within the spread) |
| peak VRAM (MB) | 117.0 | 121.0 |
| nouveau copies now / made at a read / readbacks | 3 (783 kB) / 5 (1.5 MB) / 5 | 1 (1 kB) / 1 / 1 |
| hint | never set | never set |

The gain is CPU: no u_vbuf conversion or upload per world draw. The memory saved is small (the
copies were 1.5 MB, not the 4.4 MB feared, since the hint never fires), and the VRAM cost is 4 MB on
the demo's map. Package: cube2 since .442~dev, now .466; recipe variants `shortnormals`
(`vanormalshort 1`) and `bytenormals` (`vanormalshort 0`, the default spelled out). The default is
still 0; turning it on is a cube2 change waiting on the webcam check of the world's lighting.

## Texture compression at upload (Mesa 0038, `NV30_FORCE_DXT`)

PLAN 2.4. Most games upload plain RGB(A)8 textures, which the RSX stores at 4 bytes a texel (RGB as
X8R8G8B8). VRAM is also our swap tier, so every MB of textures not kept is memory. DXT1 takes an
eighth of that, DXT5 a quarter, and libspeq (Mesa 0011/0032) already compresses on the SPEs, RGB
sources included (1024x1024 in about 1 ms on 4 SPEs). Mesa 0038 is in Mesa .466~dev and .476 (on
the PS3) and off by default; its first A/B (Sauerbraten, SuperTuxKart lean, OpenArena gl2min) is
[below](#measured-on-the-ps3-batch-ssdroot2-p2).

### What it does

`mesa/patches/0038-mesa-texture-compression-at-upload-for-every-game-NV.patch`, core Mesa
(`src/mesa/main/texforcedxt.c`, the decision alone in `texforcedxt_policy.h`). A `glTexImage2D`
that the policy takes gets a DXT TexFormat while its InternalFormat stays the game's; Mesa's S3TC
texstore then compresses it like any compressed upload, through libspeq. Levels above 0 follow
level 0 (Mesa picks a level's format from the level below when their internal formats agree).

Taken: `GL_TEXTURE_2D` level 0, internal format `GL_RGB`, `GL_RGB8`, `3`, `GL_RGBA`, `GL_RGBA8` or
`4`, from `GL_RGB`/`GL_BGR` unsigned bytes or `GL_RGBA`/`GL_BGRA` unsigned bytes or 8888 packed
types, power-of-two sizes of at least 64x64.

- `opaque`: DXT1 when there is no alpha: an RGB internal format or source, or every alpha byte 255.
  The scan reads eight bytes at a time and stops at the first other value; a PBO source counts as
  having alpha (no map for a scan).
- `all`: the same, and DXT5 for images with alpha.

Not taken, and why:

| Case | Reason |
|---|---|
| `glTexImage2D(NULL)` without a PBO | render targets and textures filled later by `glTexSubImage` (font caches, lightmap atlases) |
| smaller than `NV30_FORCE_DXT_MIN` (64) in width or height | small UI pieces; little to win |
| sizes not powers of two | nv30 lays DXT out for POT mipmaps (`nv30_miptree.c`: compressed textures are neither swizzled nor linear-flagged); its NPOT layout with DXT has never run |
| magnification already `GL_NEAREST` at upload | pixel art and UI drawn 1:1 show DXT's 4x4 blocks most; `NV30_FORCE_DXT_NEAREST=1` takes them. Games that set filters after the upload (ioquake3) aren't caught by this |
| cube maps, rectangles, 1D/3D, arrays | every face of a cube would have to decide alike (one format, or the cube is incomplete) |
| an uncompressed level 1 already there with the same internal format | levels uploaded smallest first, or a chain made with `glTexImage2D(NULL)` given data later: one format for all levels |
| sRGB, 16-bit, float, luminance/alpha formats, borders, pixel transfer ops, swapped bytes | not 8-bit RGB(A), or the source isn't what the scan reads |

After the upload the texture behaves as the game uploaded it:

- `glTexSubImage` on whole 4x4 blocks (a partial block only at the right or bottom edge): those
  blocks are compressed again from the new pixels (libspeq again). This keeps ClassiCube-style
  animated tiles (16x16 at multiples of 16) compressed.
- Any other `glTexSubImage`, a `glCopyTexSubImage` and a framebuffer attachment turn the texture
  back into the format the game asked for, once: every compressed level is decoded
  (`st_GetTexSubImage`, a GPU blit on nv30) and specified again uncompressed, the game's unpack
  state set aside. Recompressing the touched blocks in place was rejected: it would need the old
  texels around an unaligned update (a GPU readback per update), and a texture updated off the
  block grid once is likely updated that way all the time (glyph caches, dynamic lightmaps).
- `glGenerateMipmap` and `GL_GENERATE_MIPMAP` take Mesa's path for compressed textures: the base
  read back decoded (a GPU blit), the levels made on the CPU (box filter) and compressed through
  libspeq. The GPU can't render into DXT, so this costs PPE time, counted in `fdxt_mipgen_ms`.
  SuperTuxKart and Luanti make their mipmaps this way.
- `glGetTexImage` returns the decoded blocks; `GL_TEXTURE_INTERNAL_FORMAT` reports the game's
  internal format and `GL_TEXTURE_COMPRESSED` is true (the spec allows storing a generic format
  compressed).

Quality: DXT1 is lossy (4 colours per 4x4 block from a 565 pair). Fonts and most UI have alpha, so
`opaque` leaves them alone; opaque UI backgrounds and menus are taken. `all` also takes alpha
textures as DXT5 (alpha in 8 steps per block: fine for white text, visible on gradients). Whether a
game looks right is for the webcam and the user, so the switch is per game, in the recipe's
environment.

### Switches and stats

| Switch | Values | What |
|---|---|---|
| `NV30_FORCE_DXT` | `off` (default), `opaque`, `all` | the mode |
| `NV30_FORCE_DXT_MIN` | 64 | smallest width and height taken |
| `NV30_FORCE_DXT_NEAREST` | 0 (default), 1 | take textures magnified with `GL_NEAREST` too |
| `NV30_FORCE_DXT_STATS` | N (0: off) | a `nv30-forcedxt:` line every N compressed textures, at every return to uncompressed, at context destruction and at exit: textures, DXT1/DXT5, levels, KiB before and after (uploads, not what is resident), sub-image updates recompressed, returns to uncompressed, mipmap generations and their ms, the alpha scans' ms, textures kept by reason (`skip_*`) |
| `NV30_FORCE_DXT_DEBUG` | 0, 1 | a line per decision: size, formats, kept (why) or compressed (dxt1/dxt5) |

report.py: `fdxt_tex`, `fdxt_dxt5`, `fdxt_raw_mb`, `fdxt_dxt_mb`, `fdxt_unforced`, `fdxt_sub`,
`fdxt_mipgen`, `fdxt_mipgen_ms`, `fdxt_scan_ms`, `fdxt_skip` (reasons by count). libspeq's own
`SPEQ_DEBUG=1` still prints one line per compressed image (SPU or PPE path).

### Host checks

- `make test` (suite `mesa-forcedxt`, `mesa/tests/forcedxt-policy-test.c`): the decision for every
  case above, the alpha scan (every position of one non-255 byte, odd widths, misaligned rows), the
  block-alignment test, the byte counts. 5105 checks.
- `ps3-gpu-tools/mesa-tests/gl-forcedxt.c` (installed with ps3-gpu-tools as `gl-forcedxt`): the
  whole path through GL, with images of one RGB565-exact colour per block so DXT stores them without
  loss and every check is exact: which uploads get compressed per mode, drawn and read back right,
  mip chains by level, `glGenerateMipmap` and `GL_GENERATE_MIPMAP`, sub-images on and off the block
  grid, a framebuffer attachment (other levels kept), `glCopyTexSubImage2D`, the queries. Core Mesa
  does the work, so softpipe takes the same paths as nv30 up to the driver's blits and transfers.
  On softpipe in the ppc64 chroot under qemu (big-endian, our debs) 128 checks pass in each of
  `off`, `opaque` and `all` (Mesa ps3.466, 2026-10-18). The first run found three bugs, all fixed
  before the build: `GL_TEXTURE_INTERNAL_FORMAT` reported the DXT enum, a return to uncompressed read
  levels above 0 wrong once level 0 had been specified again (all levels are read first now), and a
  `glTexImage2D(NULL)` chain given level 0 data made level 0 DXT under uncompressed levels (now
  kept).

### PS3 checks

The GL test on the PS3 is not run yet. First the GL test against softpipe as the reference:

    for m in off opaque all; do NV30_FORCE_DXT=$m gl-forcedxt; done
    for m in off opaque all; do NV30_FORCE_DXT=$m LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe gl-forcedxt; done

Then the bench, same boot, desktop stopped, webcam on every run (look for blockiness on UI and
banding on skies). Every arm carries the stats line so all get the fdxt columns; `MEM=1` gives
`mem_gpu_vram`, and the `vram` column is the peak in every run. ssdroot2 P2 ran the first three games
(the `opaque` against off lines) and ssdroot8 the rest (all of it is in History in TESTS.md); the
commands:

    S=NV30_FORCE_DXT_STATS=50
    # one run with the per-decision lines, to see what is taken and what is kept
    ENVS="$S+NV30_FORCE_DXT=opaque+NV30_FORCE_DXT_DEBUG=1" /var/tmp/bench/run.sh bench luanti base 1
    # off against opaque (2 rounds, interleaved)
    MEM=1 STALLS=1 ENVS="$S $S+NV30_FORCE_DXT=opaque" /var/tmp/bench/run.sh bench openarena gl2min 2
    MEM=1 STALLS=1 ENVS="$S $S+NV30_FORCE_DXT=opaque" /var/tmp/bench/run.sh bench sauerbraten base 2
    MEM=1 STALLS=1 ENVS="$S $S+NV30_FORCE_DXT=opaque" /var/tmp/bench/run.sh bench supertuxkart lean 2
    MEM=1 STALLS=1 ENVS="$S $S+NV30_FORCE_DXT=opaque $S+NV30_FORCE_DXT=opaque+NV30_FORCE_DXT_NEAREST=1" /var/tmp/bench/run.sh bench luanti base 2
    MEM=1 STALLS=1 ENVS="$S $S+NV30_FORCE_DXT=opaque $S+NV30_FORCE_DXT=opaque+NV30_FORCE_DXT_NEAREST=1" /var/tmp/bench/run.sh bench classicube base 2
    # alpha textures as DXT5 too, where opaque helped
    MEM=1 STALLS=1 ENVS="$S+NV30_FORCE_DXT=opaque $S+NV30_FORCE_DXT=all" /var/tmp/bench/run.sh bench supertuxkart lean 2
    # the game's own compression (r_ext_compressed_textures 0) replaced by Mesa's
    MEM=1 STALLS=1 ENVS="$S $S+NV30_FORCE_DXT=opaque" /var/tmp/bench/run.sh bench openarena "gl2min gl2min_tc0" 2
    python3 /var/tmp/bench/report.py --stalls --mem /var/tmp/ps3-bench/<run dirs>

Read `vram` and `mem_gpu_vram` (the win), `fdxt_raw_mb` against `fdxt_dxt_mb` (what was
compressed), `fdxt_skip` (what was left and why), `fdxt_unforced` (textures that went back:
expected a handful at most; many means a game updates its textures off the block grid and the mode
buys little there), `fdxt_mipgen_ms` and the `load` column (SuperTuxKart and Luanti make their
mipmaps on the CPU now), `stall_play_s`, `low1` and the fps (smaller textures are less bandwidth:
expected the same or better), `mem_swap` (VRAM freed for the VRAM swap). OpenArena gl2min and
SuperTuxKart already ask for compression themselves (`r_ext_compressed_textures 1`,
`enable_texture_compression`), and cube2 compresses from 1024 up, so there `opaque` takes the rest
only (lightmaps, small textures, UI); Luanti and ClassiCube upload everything plain and should show
the most. A game where `opaque` looks right and saves VRAM gets `NV30_FORCE_DXT=opaque` in its
launcher environment (a ps3-desktop change, after the user has seen it).

### Measured on the PS3 (batch ssdroot2 P2)

2026-10-19 02:35-03:25Z, one boot, kernel .472, root on the SSD image, Mesa .476, `MEM=1 STALLS=1`,
x2 per arm, the arms interleaved, `NV30_FORCE_DXT_STATS=50` in both (row 74, results commit named in
TESTS.md). Medians; n=2, so "equal" means inside the spread of the baseline runs of the same boot.

| Game | Arm | Textures forced | Peak VRAM (MB) | ps3rsx buffers, middle (MB) | Steady fps | 1% low | Gameplay stalls | RSS (MB) |
|---|---|---|---|---|---|---|---|---|
| Sauerbraten | off | 0 | 119.8 | 351 | 39.9 | 26.0 | 0.5 s | 93.3 |
| Sauerbraten | `opaque` | 73 (all DXT1) | 73.4-77.6 | 215 | 39.0 | 23.9 | 0.55 s | 89.7 |
| SuperTuxKart lean | off | 0 | 98.5 | 265 | 20 (HUD) | 10.7 | 2.8 s | 101 |
| SuperTuxKart lean | `opaque` | 0 | 97.3 | 261 | 21 (HUD) | 11.6 | 1.7 s | 105 |
| OpenArena gl2min | off | 0 | 120.1 | 343 | 56.3 | 23.0 | 0.0 s | 89 |
| OpenArena gl2min | `opaque` | 0 | 119.7 | 343 | 55.8 | 23.0 | 0.4 s | 86 |

- **Sauerbraten wins memory.** 73 textures went to DXT1, 88 MB of levels stored in 11 MB; 4 went
  back to uncompressed (1 MB), 17 stayed for alpha, 26 for size, 39 for format. Peak VRAM left the
  117 MB cap (119.8 MB in the column) for 73-78 MB, and the census of the game's ps3rsx buffers fell
  from 351 MB to 215 MB (the census counts buffers the VRAM swap has pushed out as well), RSS by 3.5 MB. Nothing else was
  compressed: `fdxt_sub` 0, `fdxt_mipgen` 0, so no CPU for mipmaps or sub-image updates; `scan_ms` 0.
- **The speed did not move beyond noise.** Steady 39.9 and 39.9 (off) against 38.9 and 39.1 (`opaque`);
  the unprofiled baseline runs of the boot span 39.06-40.3 and all SSD-root base runs 39.1-40.3. The
  1% low (26.2, 25.7 against 24.8, 22.9) and the stalls (0.4, 0.6 against 0.3, 0.8 s) overlap. Load
  7.7 against 7.4 s. Less VRAM did not buy fewer stalls here because on the SSD root there were few
  to remove.
- **The picture changes slightly.** The webcam frames of the demo differ from the off runs by a mean
  grey level of 10 (two off runs differ by 1-2 from each other): the walls are softer and a little
  darker, a DXT1 4-colour palette on rock detail. Not wrong or blocky at this camera's resolution,
  but lossy, which is why the default stays off and the user judges it at the TV.
- **SuperTuxKart and OpenArena: no textures forced.** STK's stats line: skip_alpha 182,
  skip_format 173, skip_small 22, skip_npot 6, tex 0 (the game's own compression, Mesa 0004,
  already covers its opaque textures, and everything left has alpha or an unsupported format). Its
  `all` mode (DXT5 for the alpha ones) is the next arm. OpenArena gl2min: skip_format 2238 and
  nothing else: every upload is in a format the switch does not take (the formats are not recorded; the
  game's own compressed or sRGB uploads are the likely ones, not confirmed), so `opaque` is a no-op
  there. Fps, stalls and VRAM differences are noise.
- OpenArena ran at 56 in both arms against 60-63 in the baseline earlier in the boot; the stats
  variable or the boot's drift, not separated (a profiled run an hour before also gave 56).

Verdict: Mesa 0038 `opaque` is worth turning on for Sauerbraten if the user accepts the softer
rock; it frees 40-45 MB of VRAM, which on a USB root or in game mode is what the VRAM swap needs.
The rest of the suite is in the next section.

### Measured on the PS3 (batch ssdroot8, the rest of the suite)

2026-10-19 23:01Z to 2026-10-20 00:39Z, one boot, kernel .472, SSD root, Mesa ps3.545~dev,
`MEM=1 STALLS=1`, 2 runs per arm rotated (24 runs, none interfered), `NV30_FORCE_DXT_STATS=50` in every
arm; the matrix row is "batch ssdroot8 Mesa 0038 force-DXT on the rest of the suite". VRAM is the peak
in MiB as vram.csv has it (the ssdroot2 table above divides KiB by 1000, so its numbers are about
2.4% higher than the same VRAM here). Medians of 2.

| Game | Arm | Textures forced | Peak VRAM (MiB) | Steady fps | 1% low | Frames over 100 ms (window) | Load (s) |
|---|---|---|---|---|---|---|---|
| Luanti | off | 0 | 62.4 | 46.5 (HUD) | 17.4 | 17.5 | 25.5 |
| Luanti | `opaque` | 0 | 60.9 | 46.0 | 17.2 | 17.5 | 25.2 |
| Luanti | `opaque` + NEAREST | 0 | 61.9 | 46.0 | 16.4 | 20.5 | 25.1 |
| ClassiCube | off | 0 | 60.1 | 47.3 | 30.0 | 0 | 7.8 |
| ClassiCube | `opaque` | 0 | 60.1 | 47.3 | 30.6 | 0 | 7.8 |
| ClassiCube | `opaque` + NEAREST | 0 | 60.1 | 47.3 | 31.0 | 0 | 8.0 |
| OpenArena gl2min | off | 0 | 115.3 | 61.8 | 7.1 | 60.5 (map reloads) | 6.5 |
| OpenArena gl2min | `opaque` | 0 | 117.0 | 62.1 | 7.4 | 61 | 6.8 |
| OpenArena gl2min_tc0 | off | 0 | 146.8 | 58.6 | 8.1 | 58 | 6.6 |
| OpenArena gl2min_tc0 | `opaque` | 0 | 148.3 | 58.9 | 7.7 | 60.5 | 6.8 |
| SuperTuxKart lean | `opaque` | 0 | 95.5 | 20 (HUD) | 11.3 | 12 | 28.1 |
| SuperTuxKart lean | `all` | 182 (all DXT5) | 58.1 | 20.5 (HUD) | 11.4 | 10.5 | 28.4 |

- **Luanti: nothing forced.** `skip_nodata` 516: every upload is `glTexImage2D(NULL)` followed by
  sub-images, the case the policy refuses (render targets, atlases). So the "Luanti should show the
  most" expectation does not hold with this design: taking it would need the sub-image upload to
  compress, which is the fragile path (off-grid updates). VRAM, fps and stalls are equal inside the
  spread.
- **ClassiCube: nothing forced.** 214 textures under 64 px, 10 kept for `GL_NEAREST` magnification in
  `opaque` and for alpha in `opaque` + NEAREST (the terrain atlas has alpha: it would be `all` with
  `NV30_FORCE_DXT_NEAREST=1`, a combination not run; DXT5 on a pixel-art atlas is the mode that shows
  blocks most). VRAM 60.1 MiB in all six runs.
- **OpenArena: nothing forced either.** gl2min: `skip_format` 2593 (every upload is a format the
  switch does not take: still not recorded which) and `skip_nodata` 8; gl2min_tc0 (the game's own
  compression off, about 30 MiB more VRAM): `skip_nodata` 2244. The first `opaque` run's CPU was 216 s
  against 187 s in the other seven runs, an outlier, not a cost.
- **SuperTuxKart: `all` is the win.** `opaque` forces nothing (the 182 textures of 64 px and up all
  carry alpha); `all` sends them as DXT5: 52-53 MB of levels become 13 MB, 5 textures (2.8 MB) go back
  to uncompressed, and the peak VRAM falls from 95.5 to 58.1 MiB (-37 MiB, -39%). fps (20 against
  20.5 on the HUD's integer), 1% low, frames over 100 ms (1.8 against 1.4 s), CPU (323 and 311 against
  307 and 326 s) and load (+0.3 s) did not move; `mipgen_ms` 0 and `scan_ms` 1-3 in the stats. The
  lowest MemAvailable is 26-32 MiB in both arms: this is VRAM, which the VRAM swap uses, not RAM.
- **Picture:** the webcam frames of the `all` runs show the same track, sky, kart and road as the
  `opaque` ones (a different moment of the race each), with no blocks or banding at the camera's
  resolution. DXT5's loss on fine detail is not judged by a webcam; the user decides.
- The OpenAL wedge hit 9 of these 24 runs (Wait timeout lines) in every arm, off included: it is not
  the compression ([LOG-FINDINGS.md](games/LOG-FINDINGS.md) item 1).

Verdict: Mesa 0038 `all` is a real memory cut for SuperTuxKart (-37 MiB of VRAM at equal speed) and
costs nothing measurable; `opaque` and `NEAREST` take nothing in Luanti, ClassiCube and OpenArena
because of how those games upload. If SuperTuxKart goes into game mode with VRAM short, its launcher
gets `NV30_FORCE_DXT=all` after the user has seen the picture at the TV. To save anything in Luanti
and OpenArena the policy would have to accept `NULL` uploads that sub-images fill, which is a design
change for another day, not a switch.

## Packages

Nothing here is in the stable `ps3` suite; the versions are in `ps3-dev` and installed on the PS3
(checked in the `versions` file of the 2026-10-18 batch ssd1 runs, kernel .472).

| Package | Version | Memory change in it |
|---|---|---|
| ps3-gpu-tools | 0.476 (malloctrim since 0.403) | `/usr/lib/ps3/lib/ps3-malloctrim.so`; `gl-forcedxt` since 0.476 |
| luanti, luanti-data | 5.15.2+dfsg-2+ps3.405 | 0007 `LU_TRIM_MB`: the malloc_trim threshold in MiB, 256 when unset (unit tests pass in the build); the trims stay in Luanti's emerge and mesh threads |
| yamagi-quake2, yamagi-quake2-core | 8.70+dfsg-1+ps3.415 | 0002 `YQ2_KISS_QSIZE`: the PRNG table allocated at first use, a power of two from 64 to 2^21 entries, 4096 (32 KiB per module) when unset. The .bss is 7.7 MB in `quake2` (was 23.7) and 81 KiB in `game.so` (was 16 MB). `YQ2_KISS_QSIZE=2097152` (recipe variant `kissbig`) gives the old size and sequence (the same million draws, checked on the host), with the 16 MiB from calloc. .408 had the switch, .415 made 4096 the default |
| ioquake3 | 1.36+u20260719.58839361+dfsg-1+ps3.462 | 0027-0029 |
| Mesa | 26.2.4-1+ps3.476 | 0037 (since .442~dev), 0038 and 0039 (since .466~dev), all off by default |
| cube2, cube2-server | 0.0.20201229+dfsg-1+ps3.466 | 0011 `vanormalshort` (since .442~dev), off by default |

dsda-doom's recipe also changed: `LOAD_MARK` matches `Playing demo:`, so its window, perf and the
snapshots start. Its first profile is in [HOTPATHS.md](HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b).

The selftest (`python3 games/bench/selftest.py`, all pass) covers this: section 8 runs client.sh
with `BENCH_MALLOC=arena2,trim` and checks both preloads' threads in the game, the trim lines in
game.log, `malloc=` and the variant tag; section 10 runs ps3-malloctrim.so on a C program that
leaves 88 MB of holes in its main heap and a thread arena (over 60 MB given back) and checks that a
process of another name is left alone.

## Proposed, not implemented

Moved to [backlog/memory.md](backlog/memory.md): OpenArena's hunk trim after a map load, ps3rsx's
census listing the largest buffers, an ETR soak, the nv30-only Mesa build, sound without the pulse
plugin, renderergl2's `r_keepImages`, Sauerbraten's grass normals, and a fix for the Luanti bench
mod, whose memory lines are parsed wrongly (its `vmrss`, `rssanon`, `rssfile` and `vmswap` never
come out; still so in the runs of 2026-10-18).

## History

- 2026-10-18 06:47 (row 57): Yamagi's small table measured (-70 MB, stalls 22.6 -> 0.05 s), later
  made the default; Luanti `trim16` rejected for its extra stalls.
- 2026-10-18 morning (row 61): the malloc presets on Sauerbraten and OpenArena read as noise, so no
  preset became a default.
- 2026-10-18: report.py stopped counting a reload's last frame as play (commit 3634dcc), which
  turned OpenArena opengl2's "16 s gameplay freeze" into a load-time frame.
- 2026-10-18 evening (batch 17, commit 3939a48): ioquake3 0027, Mesa 0037's switches and cube2 0011
  measured; 0027 already the default, the other two still off by default.
- 2026-10-19 (batch ssdroot2 P2): Mesa 0038 `opaque` measured on three games: Sauerbraten -42 MB of
  VRAM, no fps change; SuperTuxKart and OpenArena 0 textures forced.

Older versions and the dated history: [archive/MEMORY.md](archive/MEMORY.md).
