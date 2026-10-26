# CPU and memory profiles of the games

The bench harness can profile a game while it runs: `PERF=1` records where the CPU goes during the
measured window, `MEM=1` where the game's memory sits, and `PERF_LOAD=1` the start-up (exec,
loading screens and the 60 s warm-up). All three are off by default, write into the run directory
and have columns in report.py, in the style of `STALLS=1` ([STALLS.md](STALLS.md)). This doc says how
to run them and read them, what the start-up profiles of batch 17 found (the disk is the biggest item
in every load; CPU items are 1-3 s per game), and what the shader caches do on nv30. The graphics
stack's hot paths across the games, read from these profiles, are in [HOTPATHS.md](HOTPATHS.md). Terms such as PPE, GART, nv30 and
warm-up are in the [glossary](README.md#glossary).

**Profiled runs are for profiles, not for fps.** perf adds work to every sample (see
[Overhead](#overhead)), and a memory snapshot holds the game's mmap lock while the kernel walks its
page tables. Fps and stall decisions come from runs without these switches, same-boot A/B as always.

## Running it

On the PS3 (`make bench-push` done; ps3-gpu-tools 0.391 or later for `PERF=1`'s perf control and
`MEM=1`'s malloc statistics, 0.412 or later for the load's mmaps, 0.469 or later for `PERF_LOAD=1`):

    PERF=1 /var/tmp/bench/run.sh smoke openarena              # prints the tables at the end
    PERF=1 MEM=1 /var/tmp/bench/run.sh bench openarena base 1   # both in one run
    MEM=1 /var/tmp/bench/run.sh suite base 1                  # memory of every suite game
    PERF=1 /var/tmp/bench/run.sh suite base 1                 # a profile of every suite game
    python3 /var/tmp/bench/report.py --perf --mem /var/tmp/ps3-bench/<run dir>...

| Variable | Default | What |
|---|---|---|
| `PERF=1` | off | perf record of the window (ps3-bench.sh `BENCH_PERF=1`) |
| `PERF_HZ` | 499 | samples a second per running thread |
| `PERF_ALL=1` | off | also a system-wide record without call graphs (`perf-sys.data`): kworkers, interrupts, idle. Off because `perf record -a -g` hung the PS3 once ([DESKTOP.md](DESKTOP.md)); this one has no `-g`, but no run has tried it yet: watch the first one |
| `PERF_SYMS=1` | off | perf-post.sh also packs the symbols for analysis on the PC (`perf-syms.tar`, `perf-kallsyms`) |
| `MEM=1` | off | the memory snapshots (client.sh `BENCH_MEM=1`) |
| `PERF_LOAD=1` | off | the start-up profile ([below](#start-up-profiling)) |

After every `PERF=1` run, run.sh calls `perf-post.sh` on it, outside the timed part (the next run's
quiet drops the caches and empties the swaps again). By hand, for a run made earlier:
`/var/tmp/bench/perf-post.sh /var/tmp/ps3-bench/<run dir>`.

### Packages on the PS3

- `linux-perf` is in the base image (Debian's 7.2.9). It needs `-D -1`, `--control` and
  `--buildid-mmap`; check `perf --version` and `perf.err` if a run's perf line looks wrong.
- `ps3-gpu-tools` (`ps3-mallocstat.so` in `/usr/lib/ps3/lib`, ps3-bench.sh's `BENCH_PERF`).
- Debug symbols, for profile runs only (they go to the disk, never into images; remove them
  afterwards). Without them perf names only exported functions, and static ones show as addresses
  inside the DSO (the class shares are right either way). Each must match the installed version
  exactly (`apt install P-dbgsym=$(dpkg-query -W -f '${Version}' P)`); our local builds' come with
  `make deploy` (they are in `build/debs`), testing's and stable's from `testing-debug` and `stable-debug`:
  - the driver, every game: `mesa-libgallium-dbgsym` (libgallium: nv30, the state tracker, u_vbuf),
    `libgl1-mesa-dri-dbgsym`, `libglx-mesa0-dbgsym`, `libegl-mesa0-dbgsym`, `libgbm1-dbgsym`;
  - libc: `libc6-dbg` (Debian's: memcpy and malloc by name);
  - audio and SDL where the game uses them: `libopenal1-dbgsym`, `libfluidsynth3-dbgsym`,
    `libsdl3-0-dbgsym`, `libspeq0-dbgsym`;
  - the game, when it is one of ours: `ioquake3-dbgsym`, `yamagi-quake2-dbgsym` and
    `yamagi-quake2-core-dbgsym`, `classicube-dbgsym`, `cube2-dbgsym`, `dsda-doom-dbgsym`,
    `extremetuxracer-dbgsym`, `neverball-dbgsym`, `openttd-dbgsym`, `supertuxkart-dbgsym`,
    `xmoto-dbgsym`, `luanti-dbgsym` with `libluajit-5.1-2-dbgsym`.
  - Not ours, so no dbgsym in our repository: libdrm and libdrm_nouveau (Debian's; their exported
    functions resolve from the dynamic symbol table), and the Debian games ([QuakeSpasm](../gamedb/games/quakespasm/), Armagetron,
    Chromium B.S.U., ...), whose code shows as addresses but still counts as `game`.
  - The kernel and ps3rsx need nothing: perf-post.sh runs perf as root, which reads
    `/proc/kallsyms` (module symbols included).

## What PERF=1 records

ps3-bench.sh (`BENCH_PERF=1`) starts, as root, once client.sh's `.pid` names the game:

    perf record -e cpu-clock -F 499 -g -k mono -N --buildid-mmap -D -1 \
        --control fifo:$D/.perf.ctl,$D/.perf.ack --proc-map-timeout=20000 \
        -p <game>,<Xorg>,<kswapd0> -o <disk>/perf.data

- **Started disabled** (`-D -1`) during the load, so perf has read the game's maps long before the
  window. client.sh writes `enable` into the control fifo WARMUP s after the load (0.9 x WARMUP for
  games that repeat a demo, `PASSES=1`, where the first counted pass may start that early; at every
  `WIN_ON_RE` line for games that mark their own window) and `disable` at the window's end (MEASURE s
  later, after the last counted pass, at every `WIN_OFF_RE` line). perf acknowledges each one (2.5 ms
  in the container test). bench.txt gets `perf_win` (on/off pairs, seconds from the client's start),
  `perf_ack` (ok, or missing when perf never answered: see `perf.err`) and `perf_cpu` (perf's own CPU
  seconds while enabled, from its `/proc` stat).
- **The load's mmaps** (ps3-gpu-tools 0.412). Right after starting perf, ps3-bench.sh writes `enable`
  and `disable cpu-clock` into the control fifo at once and reads both acks (`perf.track`: `ok`, or
  which ack never came; the result's perf line repeats it). The tracking event (perf's `dummy:u`,
  which carries the mmap, comm and task records) stays on and the samples stay off. Without this,
  perf with `-D -1` on running processes keeps the tracking event off until an exec that never comes,
  and libraries dlopened during the load (libGLX_mesa, libgallium at context creation) get no map.
  `enable dummy:u` through the fifo is acknowledged but brings no mmap records (tried in a container
  with Debian's perf 7.2.9, the PS3's version). The one perf loop between the two commands may take a
  sample of the load (one in the container test).
- **All the game's threads** (`-p` follows the threads it creates later: inherit), plus Xorg (its
  work for the game) and kswapd0 (background reclaim and zram compression).
- **cpu-clock**: perf_events has no driver for the Cell PMU, so the samples come from an hrtimer. On
  ppc64 an interrupt that arrives while the kernel has interrupts soft-disabled is replayed when they
  are enabled again, so kernel time in such regions lands on the function that re-enables them
  (`arch_local_irq_restore`, `_raw_spin_unlock_irqrestore`, `__replay_soft_interrupts`).
- **Call graphs by frame pointer, which on ppc64 is the ABI's stack back chain.** Every ppc64 frame
  stores its caller's stack pointer whatever the compiler flags, so Debian's builds unwind without
  `-fno-omit-frame-pointer`, and the kernel stores the link register for the leaf frame. perf's
  powerpc code checks each leaf against the DSO's CFI (`.eh_frame`, in every binary) to drop or keep
  that extra entry. DWARF unwinding (`--call-graph dwarf`) would copy 8 KiB of stack per sample
  (5 MB/s at this rate) for no gain here. On an x86 PC the same perf.data unwinds without that check,
  so a caller may appear twice: the folded stacks made on the PS3 are the reference.
- `--buildid-mmap`: build ids come with the mmap events, so perf doesn't read the whole file again at
  the end. `-N`: nothing copied into root's build-id cache. `-k mono`: sample times on CLOCK_MONOTONIC
  (the stall recorder's clock).
- **perf.data goes to the disk** (`/var/tmp/ps3-bench/.perf-<run>/`, moved into the run directory at
  the end), not /dev/shm with the rest of the run: about 250-400 bytes a sample, 20-30 MB for a 120 s
  window of a CPU-bound game, which in /dev/shm would be RAM taken from the game.

`perf-post.sh` writes into the run directory:

| File | What |
|---|---|
| `perf-report.txt` | `perf report --no-children --sort dso,sym` of the game process (all its threads), % of its samples |
| `perf-threads.txt` | samples (percent and count) by pid, thread name and DSO: the game's threads, Xorg, kswapd0 |
| `perf.folded` | folded stacks, `thread;outer;...;leaf COUNT` (kernel frames end in `_[k]`, frames without a symbol are `[their DSO]`), for `flamegraph.pl`, `inferno-flamegraph` or speedscope |
| `perf-leaf.txt` | samples by process, DSO and symbol of the sampled instruction: report.py's columns |
| `perf-pkgs.txt` | the package of every DSO with samples (dpkg -S): tells the game's own libraries |
| `perf-buildids.txt` | the build ids of the DSOs with samples |
| `perf-sys-report.txt`, `perf-sys-leaf.txt` | `PERF_ALL=1`: the system-wide record by comm and DSO |
| `perf-syms.tar`, `perf-kallsyms` | `PERF_SYMS=1`: the DSOs with samples, their debug files and the ps3rsx module laid out as a root, and the kernel's symbols |
| `perf.data`, `perf-sys.data`, `perf.err`, `perf.targets`, `perf.exe`, `perf.track` | the record, perf's messages, the pids recorded, the game's executable, whether the load's mmaps were tracked |

The collapser is `perfprof.py collapse` (the PS3 has no stackcollapse-perf.pl). It reads `perf script
--no-inline -F comm,pid,tid,time,period,ip,sym,dso`. 100k samples of 20 frames take 2.2 s on a Zen 2
host, so about 30-45 s on the PPE; perf script itself takes longer.

- `--no-inline` is needed: without it perf script names inlined functions through the binutils
  `addr2line` command, which the image lacks, and dies of SIGPIPE ([Pitfalls](#pitfalls)).
- After the collapse perf-post.sh compares its sample count with perf report's (the Samples column of
  `perf-threads.txt`) and prints `!! perf script exit ..., N of M samples collapsed` into its output
  and `perf-post.err` when they differ by more than 1%.
- **Frames perf left `[unknown]`** in the game process (records made before ps3-gpu-tools 0.412, or
  whenever perf missed a map) are filled from the run's MEM=1 smaps (`mem/mid.smaps`, else start or
  end). The mapping gives the library; the library and its debug file (`/usr/lib/debug/.build-id/`,
  by the build id in `perf-buildids.txt` or the library's note) give the symbol, through
  `hotpaths/elfsym.py` (it maps the files, so libgallium's 25 MB and its 25 MB debug file cost only
  the symbol table pages). The link register entry perf could not check against a leaf without a map
  is dropped by `hotpaths/analyze.py`'s rule (it repeats the next entry or lands in the leaf's
  function). `perf-leaf.txt` then has `# filled_from_smaps=<dso>:<samples>,...` and `perfprof.py`
  prints it; more than 3% `[unknown]` left gets a warning. Without MEM=1 those frames stay
  `[unknown]`. `PERFPROF_ROOT` puts a symbol root in front of the paths (for a run on the PC).

### report.py's perf columns

Percent of the game process's samples in the window, by the DSO (and for the kernel the symbol) of the
sampled instruction, self time:

| Column | Class |
|---|---|
| `perf_game` | the game's executable and its own libraries (same package, or the package of the recipe's command as run.sh's `versions` names it: [OpenArena](../gamedb/games/openarena/)'s cgame and ui are `openarena`'s, the engine `ioquake3`'s; same directory, `/usr/lib/<name>`) |
| `perf_mesa` | libgallium, `*_dri.so`, libGLX_mesa, libEGL_mesa, libgbm, libglapi, glvnd's libGL/libGLX/libGLdispatch |
| `perf_drm` | libdrm, libdrm_nouveau (pushbuf, fences, buffer objects) |
| `perf_kgpu` | the `[ps3rsx]` and `[drm*]` modules, and `drm_*`, `dma_fence*`, `dma_resv*`, `ttm_*`, `nouveau*`, `ps3rsx*` in the kernel image (perf on the PS3 names them with ppc64's dot, `.drm_ioctl`, and puts ps3rsx's functions in `[kernel.kallsyms]`; both handled since 2026-10-18) |
| `perf_kernel` | the rest of the kernel: system calls, page faults, reclaim and zram compression done by the game |
| `perf_libc` | libc, ld.so, libm (memcpy, memset, malloc, string functions) |
| `perf_other` | every other library: SDL, OpenAL, ALSA, X, LuaJIT, zlib, ... |
| `perf_unk` | samples in no DSO perf knows: above a few percent, the profile missed maps |
| `perf_xorg`, `perf_kswapd` | their samples as percent of the game's (CPU spent for the game elsewhere) |
| `perf_n`, `perf_self` | the game's samples; perf's own CPU while enabled, % of one thread |
| `perf_sys_*` | `PERF_ALL=1`: shares of both CPUs: game, idle, xorg, kswapd, kworker, vswap, other |

The driver stack of a frame is `perf_mesa + perf_drm + perf_kgpu`. Memory pressure shows as
`perf_kernel` (faults, reclaim) and `perf_kswapd`. `perfprof.py <run dir>` (or `report.py --perf`)
prints the class table, the top DSOs and the top 25 symbols with their class.

## What MEM=1 records

client.sh takes a snapshot at the window's start + 1 s, its middle and 2 s before its end (games
repeating a demo: at the last counted pass, while the game still runs), in the background so the
game's output keeps flowing. Each one writes `mem/<label>.*`:

| File | What |
|---|---|
| `.smaps`, `.rollup`, `.status` | `/proc/<pid>/smaps`, `smaps_rollup`, `status` of the game |
| `.tasks` | every thread's name and `/proc/<pid>/task/<tid>/syscall` (its stack pointer when blocked) |
| `.meminfo` | `/proc/meminfo` |
| `.malloc` | `ps3-mallocstat.so`: a `mallinfo2` line (all arenas, bytes) and `malloc_info()`'s XML |
| `.gpu` | ps3rsx's `ps3rsx_mm` (debugfs, root; read once a snapshot because it takes `mm_lock` and walks the buffers): the game's VRAM and GART buffers, GART backed by RAM, the VRAM swap |
| `.info` | label, pid, time from the client's start, ms spent reading smaps, executable |

bench.txt gets `mem_snaps` (label:seconds:ms per snapshot).

**ps3-mallocstat.so** (`packages/walnut/tools/ps3-gpu-tools/preload/`) is preloaded into the game (`LD_PRELOAD`, which
`stdbuf` and the launchers pass on). It acts only in the process named `COMM` (the recipe's): its
constructor starts one thread with a 64 KiB stack that blocks on a fifo, and every label written there
makes it dump the statistics. It has no signal handler (a game using SIGUSR1/2 or blocking signals is
untouched; `malloc_info` from a handler interrupting malloc could deadlock on an arena lock), and the
thread never calls malloc itself (the output stream is opened by the constructor and reused with a
static buffer), so glibc gives it no arena of its own to report. `BENCH_MALLOCSTAT=` (empty) in
client.sh's environment turns it off.

`memprof.py` classifies every mapping:

| Class | Mappings |
|---|---|
| `game` | the executable and what lives in its directory or `/usr/lib/<name>` |
| `mesa` | Mesa and the GL stack, libdrm included (code, data, relro of these libraries) |
| `libs` | every other shared library |
| `heap` | `[heap]` (the main arena) and glibc's other malloc arenas: 64 MiB-aligned anonymous mappings followed by their unused reserve. The kernel may merge a neighbouring anonymous mapping into an arena's start; that one then counts as heap |
| `anon` | other anonymous memory: malloc's blocks above its mmap threshold, the game's and the driver's own mmaps, JIT code |
| `stack` | `[stack]` and thread stacks: the mapping holding a blocked thread's stack pointer (`.tasks`), else an anonymous mapping of at most 16 MiB right above a guard page of at most 64 KiB |
| `gpu` | `/dev/dri/*` mappings. ps3rsx maps buffers `VM_PFNMAP`, so their Rss is 0 and only Size says how much the CPU has mapped; the buffers themselves are in `.gpu` |
| `shm` | `/dev/shm`, SysV (X's MIT-SHM), memfd |
| `files` | other mapped files (game data, fonts, the locale archive) |
| `other` | `[vdso]`, `[vvar]` and the rest |

The guard-page rule needs the kernel to keep the guard as its own mapping. glibc 2.42 on kernels 6.13+
uses `MADV_GUARD_INSTALL` instead, so on a current PC only the stack-pointer rule finds the stacks;
the PS3's 6.12 still has separate guards. With `CONFIG_ANON_VMA_NAME` (not in our kernel) glibc's
`glibc.mem.decorate_maps` tunable would name arenas and stacks exactly; memprof.py reads those names
when present.

### report.py's mem columns

At the middle snapshot (else the nearest): `mem_<class>_rss` and `mem_<class>_swap` (MiB) for every
class; the totals `mem_rss`, `mem_swap`, `mem_anon`; `mem_topswap` (the 5 mappings with the most swap,
`name=MiB`, anonymous ones as `class@address(size)`); `mem_grow` (Rss + swap at the end minus at the
start: a leak, or a cache that keeps growing); `mem_malloc_used` and `mem_malloc_free` (MiB malloc
hands out, and holds free: fragmentation, or memory `malloc_trim` could return); `mem_arenas`;
`mem_gpu_vram` and `mem_gpu_gart` (the game's ps3rsx buffers); `mem_smaps_ms` (the longest smaps
read: how long the game's mmap lock was held). `memprof.py <run dir>` (or `report.py --mem`) prints
every snapshot's table with Pss, Anonymous and Private_Dirty, the mappings with the most swap and Rss,
malloc's and ps3rsx's lines; `memprof.py --smaps FILE [EXE]` classifies any process's smaps.

How to read it: swap is what to cut first, since each swapped page is a later stall. `heap` and `anon`
with a large `mem_malloc_free` mean memory malloc keeps. `mesa` Private_Dirty is the driver's own
data. Texture copies kept by the game or Mesa land in `heap`/`anon`, the GPU's copies in `mem_gpu_*`.
What the games' memory turned out to be is in [MEMORY.md](MEMORY.md).

## Start-up profiling

The user (2026-10-18): find where game start-up time could be won, at the system and library level as
much as per game: the USB disk, decompression (zlib for pk3/zip, libpng, libjpeg-turbo, libvorbis),
Mesa's texture uploads, format conversion and mipmaps, DXT compression, shader compiles, dynamic
linking, kernel I/O, reclaim and swap during the load. The scope covers the whole span the measured
window leaves out: the game's exec, its loading screens **and the warm-up** (WARMUP s of play after
the load: late loads, shaders compiled on first use, textures streamed in). `PERF_LOAD=1` profiles
that span; `STALLS=1` alongside says what the main thread waited for.

### Running it

One run per game. The quiet before every run drops the page cache, so each start reads its files from
the disk as a first start after boot does. Leave `GAMEPREFETCH` unset (the game prefetch would read
the files ahead and hide the disk; client.sh then also turns game mode's own prefetch off). Without
`SHADERCACHE` the game uses the user's own Mesa cache (`~/.cache/mesa_shader_cache`), warm for any
game played before.

    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench bananabread base 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench openarena gl2min 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench luanti base 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench supertuxkart lean 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench classicube base 1
    python3 /var/tmp/bench/report.py --load --stalls /var/tmp/ps3-bench/<run dir>...

The same five with the game data on the internal SSD, same boot, against the USB disk:
`ENVS="- GAMEDATA=ssd GAMEDATA=ssd+SWAP=ssd"` ([GAMEOS-HDD.md](GAMEOS-HDD.md) "Test drive, Phase A").

Mesa's shader cache, cold against warm over the same span. `SHADERCACHE=warm` keeps a cache per game
in `/var/tmp/ps3-bench/shadercache/warm-<game>`; its first run fills it and is tagged `scprime`, so
prime once, then alternate in the same session:

    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=warm" /var/tmp/bench/run.sh bench openarena gl2min 1   # scprime
    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=cold SHADERCACHE=warm" /var/tmp/bench/run.sh bench openarena gl2min 1
    # the same for "bananabread base" and "luanti base"

Compare `lp_glsl`, `lp_shbe`, `lp_shcache`, `lp_wshader` and the `load` column of the `sccold` and
`scwarm` runs: the difference in `lp_glsl` (and in load time) is what Mesa's cache saves, `lp_shbe`
what it can't. `shadercache_kb` in bench.txt (before and after the run) is the cache's size; a cold
run's "after" is the whole cache of that game. A warm run whose cache grew a lot was not warm (the
cache is keyed by the driver's build id: a new Mesa build makes every entry miss).

| Variable | Default | What |
|---|---|---|
| `PERF_LOAD=1` | off | perf record of the start-up span (ps3-bench.sh `BENCH_PERF_LOAD=1`); with `PERF=1` both records, each its own perf |
| `SHADERCACHE` | the user's cache | `cold`: an empty `MESA_SHADER_CACHE_DIR` (removed after the run); `warm`: one per game kept in `SHADERCACHE_ROOT` (`/var/tmp/ps3-bench/shadercache`); `off`: `MESA_SHADER_CACHE_DISABLE=true` (compiles, no cache reads or writes). The user's own cache is never touched |
| `LDSTATS=0` | on with `PERF_LOAD` | no `LD_DEBUG=statistics` |
| `PERF_LOAD_WAIT` | 120 | seconds client.sh waits for perf to attach before it starts the game anyway |
| `LOADPROF_DISK_MBS` | 30 | loadprof.py: the disk's sequential read rate for the read-ahead estimate (USB 2.0) |

### What PERF_LOAD=1 records

- **From the exec.** client.sh starts the game through a gate: a bash that waits on a fifo and then
  execs the game command (same pid; perf's inherit follows whatever it forks). ps3-bench.sh's perf job
  finds the gate's pid in `$D/.launch.pid` and attaches (`perf record -e cpu-clock -F 499 -g -k mono
  -N --buildid-mmap -D -1 --control fifo:... -p <gate>,<Xorg>,<kswapd0> -o perf-load.data`).
  client.sh writes `enable` into the control fifo and waits for the ack (perf has the processes), then
  takes the first counters, starts its clock (`t0`) and opens the gate. perf's own start-up is outside
  the timed load. Attached before the exec, perf gets the exec's comm and every mmap of the load,
  dlopened libraries included (no `perf.track` step needed).
- **To the window's start.** `disable` where the measured window begins: load + WARMUP, 0.9 x WARMUP
  for games that repeat a demo (`PASSES=1`), the first `WIN_ON_RE` line for games that mark their own
  window ([BananaBread](../gamedb/games/bananabread/)). bench.txt: `perf_load_win` (on, off: seconds from t0), `perf_load_ack`,
  `perf_load_cpu` (perf's own CPU over the span), `mono_t0` (`/proc/uptime` read next to t0: perf's
  clock, CLOCK_MONOTONIC, equals CLOCK_BOOTTIME on a PS3 that never suspends; 10 ms resolution),
  `game_pid`.
- **The counters** (`load-io/`, with or without perf): `/proc/diskstats` and `/proc/vmstat` at the
  gate (`start`), at the load's end (`load`) and at the window's start (`warm`); the game's
  `/proc/<pid>/stat`, `io`, `status` and its main thread's stat at the last two (copied with bash's
  builtins: no fork while the game runs); `timebase` from `/proc/cpuinfo`.
- **ld.so's statistics**: `LD_DEBUG=statistics LD_DEBUG_OUTPUT=$D/ld/stats` in the game's
  environment, one file per pid (`ld/stats.<pid>`). Every exec in the pid appends a start-up block
  (env, stdbuf, then the game); loadprof.py takes the game's (the last) and its exit block (`final
  number of relocations`, dlopen's included). The times are in timebase ticks on ppc64 (glibc's
  hp-timing reads the timebase: 79.8 MHz on the PS3). Only the start-up is timed; dlopen's
  relocations show in perf as group `link`.
- **The stall recorder** (`STALLS=1`): stalls.py prints the warm-up (load end to the window's start,
  all of it) next to the load; report.py `warmup_disk_s`. The recorder finds the game through `.pid`
  (watch_game's poll, every 0.5 s), so its record starts up to a second after the exec: the first part
  of the dynamic linking has no main-thread states.

perf-post.sh (run.sh calls it after the run) writes `perf-load-report.txt` (the game process, dso,
sym), `perf-load-threads.txt`, `perf-load.folded` (the phase, `load` or `warmup`, as the outermost
frame, so one flame graph shows both side by side), `perf-load-leaf.txt`, `perf-load-stacks.tsv` (per
phase, process, thread and stack: samples, period, every frame's symbol and DSO), merges the build ids
and packages into `perf-buildids.txt`/`perf-pkgs.txt`, and prints loadprof.py's tables.
`perf-load.data` stays on the PS3 unless `make bench-pull PERFDATA=1`.

### loadprof.py

`python3 loadprof.py <run dir>...` or `report.py --load`. Per phase (load, warm-up): the wall time,
the main thread's and all threads' CPU, Xorg's and kswapd0's, the main thread's disk wait (stalls.rec:
disk, io and swap-in seconds), MB read from the disks and the disk's busy seconds, major faults,
swap-ins; then the CPU by group with each group's top functions, by the leaf's DSO class, the busiest
threads; ld.so's statistics; and the levers.

A sample's group is the innermost frame of its stack that matches a rule. First the umbrella rules
over every user frame (what runs under them is theirs: a page fault under `_dl_relocate_object` is
linking, zstd under the shader cache's writer is the cache), then for a kernel leaf the kernel's paths,
then the leaf rules over the user frames from the leaf outwards (memcpy under `_mesa_texstore` is a
texture upload), else the leaf's DSO class.

| Group | What | Lever |
|---|---|---|
| `link` | ld.so: mapping, relocation, symbol lookup, lazy binding, dlopen | fewer and smaller DSOs, `-z now` against lazy binding, prelinking |
| `init` | library and static constructors (`_dl_init`) | less work in constructors |
| `shader.glsl` | the GL front end: GLSL compile and link, NIR at link, ARB and fixed-function programs | Mesa's disk cache skips it on a hit |
| `shader.backend` | st variants, `nir_to_tgsi`, nvfx translate | runs at every start, warm or not (below) |
| `shader.cache` | Mesa's disk cache: hashing, (de)serialising, compression | what a hit costs instead |
| `dxt` | libspeq (the PPE's share), Mesa's s3tc, the games' own | precompressed textures |
| `mipmap` | Mesa's and the games' mipmap generation | precomputed or GPU mips |
| `tex` | Mesa texture upload: texstore, format conversion, miptree transfers | formats the RSX takes as they are |
| `buffers` | Mesa buffer uploads | fewer, larger |
| `zlib`, `png`, `jpeg`, `audio`, `zstd`, `image` | inflate (libz and the games' own), PNG filters, JPEG, Vorbis/Ogg/FLAC/Opus/SF2, other decompressors, other image decoders | zlib-ng (about 2x on inflate), stored pk3 members, another texture format, decoding on first use |
| `files` | the game's file system code (pk3 scans, path hashing) | fewer archives, an index |
| `lua`, `fonts` | LuaJIT; FreeType, fontconfig, HarfBuzz | less at start |
| `k.fault`, `k.read`, `k.open`, `k.mmap`, `k.exec`, `k.reclaim`, `kgpu`, `k.other` | the kernel by path (the I/O wait of file faults is the disk wait, not CPU) | |
| `game`, `mesa`, `drm`, `libc`, `cxx`, `other`, `unk` | the rest, by DSO | |

The lever table lists every group's main-thread seconds in the load and in the warm-up and all
threads' seconds. The main thread is the critical path of a load: a group's main-thread load seconds
are the most its removal can take off the load time, half of them if it ran twice as fast (work on
other threads only counts where the main thread waits for it). For the disk: the main thread's wait
against the same MB read sequentially at `LOADPROF_DISK_MBS`. The difference is what reading ahead in
big blocks could win; the whole wait is what having the files in RAM wins. report.py's columns:
`lp_cpu`, `lp_main` (the load's CPU, all threads and the main thread), `lp_wcpu` (the warm-up's),
`lp_link`, `lp_glsl`, `lp_shbe`, `lp_shcache`, `lp_tex`, `lp_mip`, `lp_dxt`, `lp_decomp` (all threads,
both phases), `lp_wshader` (the three shader groups in the warm-up alone), `lp_mb`, `lp_wmb`,
`lp_majflt`, `lp_ld`, `lp_disk`.

## Start-up profiles, batch 17

Five games, one run each, `PERF_LOAD=1 STALLS=1`, cold page cache, prefetch off, kernel
6.12.111+ps3.433, Mesa 26.2.4-1+ps3.442~dev (the runs' `versions`), USB root, 2026-10-18 evening
(gamedb:batch/2026-10-18-batch17, commit 3939a48). Three shader-cache runs each of [OpenArena](../gamedb/games/openarena/) gl2min and [BananaBread](../gamedb/games/bananabread/)
came with them (prime, cold, warm). Run list: `build/tmp/batch17-runs.txt` lines 2-12; raw tables in
`build/tmp/b17/` (both ephemeral). Everything below was read from those run directories with
loadprof.py and stalls.py.

**One run per game and arm.** The load's wall time moves by 1.5-2.8 s between identical runs
(OpenArena 28.1, 30.9, 28.8, 28.4 s; BananaBread (Debian's cube2) 19.8, 18.4, 17.4, 18.3 s), nearly all of it disk
wait. Differences under about 2 s of wall time are not resolved here; CPU seconds from perf are
steadier.

Two "load" numbers exist and differ. bench.txt `load=` (report.py's `load` column) is the game's own
number (OpenArena: the map load, 10.2 s). This section uses `load_end`: the wall time from the game's
exec to the end of the loading screens (28.1 s for the same run). The warm-up runs from `load_end`
until the window starts (60 s of play for most; BananaBread's window opens on its own mark, so its
"warm-up" is the whole 85 s up to the demo). Disk wait is the main thread's stalls.rec `disk` + `io`
seconds; `fault` is the major faults that did not coincide with disk reads and is mostly the same wait
(readahead locks), so the true disk share is somewhat above the disk column.

### Where the time goes

| Game | Load wall (s) | Main thread in the load: CPU / disk+io wait / fault / sleep (s) | Disk in the load: MB, reads, busy s | Warm-up wall (s) | Main thread in the warm-up: CPU / disk+io wait (s) | Disk in the warm-up: MB, busy s | Stall frames in the warm-up (s, n) |
|---|---|---|---|---|---|---|---|
| [BananaBread](../gamedb/games/bananabread/) base | 19.8 | 4.1 / 8.2 / 2.1 / 2.5 | 95.7, 2520, 14.3 | 85.6 | 67.1 / 9.6 | 111, 18.0 | 21.1, 53 |
| [OpenArena](../gamedb/games/openarena/) gl2min | 28.1 | 9.0 / 10.9 / 2.0 / 3.2 | 133.6, 3405, 17.1 | 54.3 | 33.2 / 11.1 | 131, 17.2 | 25.5, 30 |
| [Luanti](../gamedb/games/luanti/) base | 43.1 | 11.5 / 16.5 / 4.3 / 3.8 (+3.4 on the GPU) | 181.4, 6540, 30.0 | 45.3 | 35.6 (recorder) / 0.6 | 19, 7.3 | 2.2, 8 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean | 54.3 | 22.5 / 18.6 / 3.5 / 3.8 | 238.4, 6313, 30.8 | 60.2 | 49.4 / 5.7 | 67, 12.7 | 18.9, 24 (one frame of 10 s) |
| [ClassiCube](../gamedb/games/classicube/) base | 13.9 | 2.9 / 4.8 / 1.0 / 2.4 (+1.1 GPU) | 65.2, 1682, 8.9 | 54.4 | 11.0 / 0.04 (35.9 s waiting on the GPU) | 3, 1.3 | 0.1, 1 |

BananaBread's disk+io in the warm-up is 4.0 + 5.6 s, OpenArena's 4.0 + 7.1 s, STK's 5.5 + 0.2 s. The
rest of each load is `unknown` (1.0-3.3 s), `runq` (0.5-1.8 s) and, in BananaBread, direct reclaim
(0.8 s).

- **The disk is the largest single item in every load.** Disk+io wait is 34-41% of the wall time in
  the four bigger games and 35% in ClassiCube; with the faults, 40-51%. The main thread's CPU is
  21-42%. The disk is busy 57-72% of the load but reads only 6-8 MB/s: the average request is 28-40 KB
  (Luanti 28 KB, the rest 38-40 KB) against 27.8 MB/s measured sequentially. The reads are small and
  the disk seeks between them, the 20 ms random read [GAMEOS-HDD.md](GAMEOS-HDD.md) measured.
- **Page faults on mapped files are most of Luanti's disk wait**: of its 17 s about 13 s are major
  faults. The recorder has no file for a fault, so which mappings is unknown; the binary and libraries
  are the candidates, since only `~/.cache/minetest/media/*` (2.7 s for two files) and libogg show up
  as named reads. Luanti has the most major faults (1420) and the most small reads (6540).
- **Files that cost named wait** (syscall reads and opens only, the recorder's fd): OpenArena pak0.pk3
  1.2 s, cgameppc64.so 0.85 s, pak4-textures 0.47, pak6-patch088 0.32 in the load; in its warm-up
  pak2-players-mature 2.1 s, pak6-patch088 1.3, pak2-players 1.2, pak0 1.1 (the demo's second pass
  loads the map and the players again: 131 MB read, as much as the load). BananaBread: cave.ogz
  0.46 s in the load, md5anim/md5.cfg model files at about 1 s each in the warm-up. STK:
  tracks/abyss, a music file, thunder.ogg at 0.4 s each. ClassiCube: its audio zip, 0.18 s. Each is
  small; the wait is spread over hundreds of files and the page faults.
- **Memory pressure repeats the reads**: refaults 16.8k (OpenArena load) and 31.3k (STK load),
  swap-ins 138-605 in the load and 511-958 in the warm-up, kswapd 2.3-4.2 s of CPU in every load but
  ClassiCube's (1.2 s), allocstalls 72-141. OpenArena's and BananaBread's warm-ups read as much as
  their loads did (131 and 111 MB) because the pages were evicted again.
- **ld.so**: start-up linking takes 0.03-0.15 s (SB 0.103, OA 0.030, Luanti 0.129, STK 0.152, CB
  0.113) for 2.8k-20k relocations; with lazy binding and dlopen the `link` group is 0.29-0.62 s of the
  load per game. At most 1-2% of a load, so prelinking can't buy more than that.
- Kernel CPU by path is small (`k.read`, `k.open`, `k.fault` 0.1-0.6 s each). The big `k.other` is
  `__replay_soft_interrupts` (0.3-2.5 s): samples taken with interrupts soft-masked land there, so
  kernel time under irq-off sections is smeared into it and can't be given a path.

### Top CPU consumers per game

Seconds, from the load's perf, all threads (main thread where noted):

| Game | Load: biggest groups | What is in them | Warm-up: biggest groups |
|---|---|---|---|
| [BananaBread](../gamedb/games/bananabread/) | zlib 1.54, shader.glsl 1.11, k.other 0.80, link 0.46, game 0.30, libc 0.28, audio 0.27, shader.cache 0.21, shader.backend 0.20 | map .ogz inflate (libz without symbols 0.65, `inflate` 0.57, crc32 0.17), GLSL compile of its shaders (front end, lexer, NIR) | mesa 42.3 (`nv30_query_result` 13.1 and `generic_run` 12.8: occlusion-query spin and the draw module's software vertex path, i.e. play, not start-up), game 7.7, kgpu 5.3, audio 5.2 (Vorbis, 1.9 on the main thread), shader.glsl 0.77 on first use |
| [OpenArena](../gamedb/games/openarena/) | files 1.95, game 1.91, shader.glsl 1.37, mipmap 1.34, k.other 1.0, jpeg 0.36, libc 0.35, link 0.34, dxt 0.27, shader.backend 0.24 | libdeflate inflate of the pk3 members 0.96 + `fread` 0.35; `COM_ParseExt` 1.31 (the .shader scripts); `R_MipMapsRGB` 1.34 (the engine's own CPU mipmaps); GLSL compile of ~55 programs | game 9.2 (`R_RegisterMD3` 1.8, `COM_ParseExt` 1.6), other 6.4 (resampler, mixer), mesa 5.4, buffers 4.3, kgpu 4.0, mipmap 2.05, files 2.04 (inflate 1.8) |
| [Luanti](../gamedb/games/luanti/) | game 11.0 (main thread 1.25; mapgen on Emerge-0 8.4), k.other 3.8, libc 2.2, other 1.9, mesa 1.65, lua 1.07, link 0.57, cxx 0.53, shader.glsl 0.43, fonts 0.31, zstd 0.22 | mapgen (`spreadLight` 1.8, `valueMap3D` 1.6, biome noise), LuaJIT bytecode, FreeType hinting (`TT_RunIns`) | game 40.1 (Emerge-0 28, MeshUpdate 8.9, Server 6.8), libc 6.5, k.other 6.6, mesa 4.7, zstd 1.3 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) | game 5.15, png 4.11, zlib 4.03, k.other 2.2, audio 2.16, jpeg 2.11, dxt 1.68, libc 1.26, link 0.59, k.read 0.63 | `CSkinnedMesh` joints and finalize; libpng (no symbols: 3.90) and libz (2.86 + inflate 0.70 + adler32 0.25); Vorbis (`ov_read_filter`, imdct); libjpeg-turbo (2.04, no symbols, so AltiVec use unconfirmed); Mesa `convert_ubyte` 1.22 (upload conversion, filed as dxt) | game 19.5 (Irrlicht driver, Bullet BVH walk), mesa 13.3, buffers 9.4 (`memcpy` 5.2, `vbo_get_minmax_index_mapped` 3.0), kgpu 1.9 |
| [ClassiCube](../gamedb/games/classicube/) | game 1.66, mesa 0.54, k.other 0.48, link 0.29, libc 0.19, kgpu 0.19, zlib 0.15 (its own `Inflate_Process`), init 0.12 | `Builder_MakeChunk`; OpenAL's init constructors (`_GLOBAL__sub_I_bsinc_tables` 0.08, also 0.07-0.08 s in OA, Luanti, STK) | mesa 8.75, game 3.5, kgpu 2.6, k.other 2.6 |

Decoders as a share of the load's main-thread CPU: STK png+zlib+jpeg+audio 12.4 s of 24.7 s (50%),
BananaBread zlib+audio 1.6 of 5.3 s, OpenArena inflate+jpeg+TGA+audio 2.6 of 10.4 s, Luanti and
ClassiCube almost none. OpenArena already uses libdeflate for the pk3s. Luanti's main thread is not
its load: the mapgen thread burns 8.4 s while the main thread sleeps or waits on the GPU 3.4 s.

An unresolved discrepancy: for Luanti's warm-up the stall recorder counts 35.6 s of main-thread CPU,
perf 16.6 s for the same thread. The load phase agrees closely (11.5 against 8.8), the BananaBread
warm-up exactly (67.1 against 67.9). Don't quote Luanti's warm-up CPU split.

### What the profiles point at

The disk wait is one pool, and it decides the load times together with game mode's memory freeing
(the 17-31k refaults). Every CPU item together is 1-3 s per game, [SuperTuxKart](../gamedb/games/supertuxkart/)'s 5-8 s. With the disk
taken out, the loads would become CPU-bound at roughly SB 15 s, OA 21 s, [Luanti](../gamedb/games/luanti/) 33 s (a mapgen wait),
STK 43 s, CB 11 s; that is where decoders and CPU mipmaps begin to matter. What has been tried
against the pool since:

- The game prefetch (reading the files ahead in disk order): no gain, Luanti worse, default off
  ([STALLS.md](STALLS.md#game-prefetch), batch 16).
- The game data on the internal SSD: the load's disk wait stayed, because most of what a load reads is
  not game data ([the SSD load split](#the-ssd-load-split-batch-ssd1)).
- Mesa's GLSL disk cache: saves CPU, no resolvable wall time ([below](#the-shader-caches)).

The whole system on the SSD (Phase B, [GAMEOS-HDD.md](GAMEOS-HDD.md)) took the loads down by 39-61%
and the disk wait from 34-41% of a load to 11-26% ([below](#start-up-profiles-on-the-ssd-root-batch-ssdroot2);
the USB-against-SSD comparison in alternating boots is still to run). The rest of the estimated wins (bigger reads, faster
inflate/PNG/JPEG, [OpenArena](../gamedb/games/openarena/)'s pk3s stored and mipmaps off the CPU, audio decoded later, less dynamic
linking, game-level items) and the measurements that would turn them into facts are in
[backlog/smoothness.md](backlog/smoothness.md).

## Start-up profiles on the SSD root (batch ssdroot2)

The same five games as batch 17, one run each, `PERF_LOAD=1 STALLS=1`, cold page cache, kernel
6.12.111+ps3.472, Mesa 26.2.4-1+ps3.476, ps3-gpu-tools 0.477~dev (the stall recorder samples `ps3db`
now, so the disk wait of the SSD root is counted), root on the internal SSD image, 2026-10-19
01:34-02:00Z (gamedb:batch/2026-10-19-ssdroot2-startup-profiles; run directories `ps3-bench-20261019T0134` to `T0156`). The console's
disk is an upgraded SSD; a stock PS3's HDD would sit between these numbers and batch 17's USB disk.
The comparison is cross-boot (batch 17: kernel .433, Mesa .442~dev, USB root): indicative.

**Symbols.** These perf runs have no symbols for Mesa or the games' own binaries (the PC-side
symbolisation comes later), so loadprof.py's groups for GLSL compiles, texture upload, mipmaps and
the games' file code are inside `mesa` and `game` here. Decoders (zlib, png, jpeg, audio), ld.so and
the kernel keep their groups, and the totals compare with batch 17.

| Game | Load wall, USB (batch 17) / SSD (s) | Main thread in the load, SSD: CPU / disk (+io) / GPU / reclaim / other waits (s) | Disk in the load, SSD: MB, reads, busy s (USB: MB, busy s) | Main thread in the warm-up, SSD: CPU / disk (s) | Warm-up stall frames, USB / SSD (s) |
|---|---|---|---|---|---|
| [BananaBread](../gamedb/games/bananabread/) | 19.8 / **7.7** | 3.9 / 1.9 (+0.1) / 0.05 / 0.2 / 1.5 | 85.8, 2782, 1.4 (95.7, 14.3) | 67.9 / 0.9 | 21.1 / 6.1 |
| [OpenArena](../gamedb/games/openarena/) gl2min | 28.1 / **14.2** | 8.7 / 2.1 (+0.3) / 0.2 / 0.4 / 2.5 | 143.9, 4146, 2.2 (133.6, 17.1) | 43.7 / 1.2 | 25.5 / 19.0 |
| [Luanti](../gamedb/games/luanti/) | 43.1 / **26.1** | 15.4 / 3.0 (+0.2) / 2.8 / 1.7 / 3.0 | 189.0, 7174, 3.4 (181.4, 30.0) | 34.4 / 0.25 | 2.2 / 1.5 |
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean | 54.3 / **31.5** | 23.8 / 3.2 (+0.3) / 0.04 / 1.2 / 3.0 | 217.3, 6856, 3.2 (238.4, 30.8) | 55.9 / 1.6 | 18.9 / 11.2 |
| [ClassiCube](../gamedb/games/classicube/) | 13.9 / **7.5** | 3.2 / 1.0 (+0.05) / 1.2 / 0.3 / 1.8 | 64.7, 2089, 1.0 (65.2, 8.9) | 11.7 (35.7 waiting on the GPU) / 0.01 | 0.1 / 0.0 |

Load wall is `load_end`: the exec to the end of the loading screens (the game's own `load=` numbers
are in the results rows). "Other waits" are futex, poll and sleep, unknown, run queue and swap.

- **The disk share is mostly gone, the loads got 39-61% shorter.** The main thread's disk+io wait
  went from 4.8-18.6 s to 1.1-3.4 s (34-41% of a USB load, 11-26% of an SSD load: BananaBread 26%,
  OpenArena 17%, ClassiCube 15%, Luanti 12%, SuperTuxKart 11%). The same megabytes (65-217 MB)
  were read with a tenth of the busy time (1.0-3.4 s against 8.9-30.8 s) at the same request size
  (26-35 KB), so the SSD's gain is latency per request, about 0.5 ms a read. The warm-ups' disk wait
  fell the same way and the warm-up stall seconds of the heavy games fell by a third to a half
  (BananaBread 21.1 to 6.1 s, SuperTuxKart 18.9 to 11.2 s).
- **The loads are not CPU-bound yet.** The main thread is 47-77% on the CPU (BananaBread 56%,
  OpenArena 64%, Luanti 60%, SuperTuxKart 77%, ClassiCube 47%) and its CPU seconds did not change (BananaBread 3.9 s
  against 4.1, OpenArena 8.7 against 9.0, SuperTuxKart 23.8 against 22.5, ClassiCube 3.2 against 2.9).
  What is left besides CPU: 1-3 s of disk wait (a quarter of BananaBread's load), sleeps and unknown
  waits of 1.5-3.0 s, GPU waits (ClassiCube 1.2 s, Luanti 2.8 s), direct reclaim of 0.2-1.7 s
  (kswapd 1.0-4.0 s of CPU, allocstalls 69-321). Batch 17's estimate of the CPU-bound loads (15, 21,
  33, 43 and 11 s) was too high: it took out only the disk and io wait, and the loads shrank by more
  because the page-fault waits and the reclaim around them depended on the disk too.
- **What is on top of the CPU now** differs by game: decoders (PNG, zlib, audio, JPEG) are 53% of
  SuperTuxKart's main thread in the load, the `.ogz` map inflate is BananaBread's one item, the shader
  scripts, mipmaps and pk3 scans OpenArena's, mapgen Luanti's. Each game's split is on its technical
  page.
- **Linking is now 3-9% of a load** (0.37-0.86 s in every game: BananaBread 5%, ClassiCube 9%,
  OpenArena 4%, Luanti 3%, SuperTuxKart 3%) though ld.so's own start-up is 0.02-0.05 s: the rest is lazy
  binding and `dlopen` (`_dl_relocate_object`, `sinl` in the libm resolver). Prelinking or `-z now`
  now helps every short load by as much as batch 17's kernel numbers allowed.
- **The warm-up is play.** BananaBread's main thread is 97% on the CPU for 70 s (Mesa 43 s, the game
  8.2 s: the demo's frames and the occlusion-query spin), ClassiCube waits 35.7 s on the GPU and
  runs 11.7 s of CPU, Luanti's main thread is 76% CPU with Emerge-0 at 28.7 s. OpenArena's warm-up is
  the demo's second pass: the map and players load again, 146 MB read, 1.2 s disk wait now (11.1 s
  on USB), the game 13.5 s of CPU.
- **Levers on the SSD root** (twice as fast, main-thread seconds off the wall time): decoders matter
  for SuperTuxKart only (png, zlib, audio and jpeg together about 6 s), elsewhere the games' own load
  paths (0.8-2 s); BananaBread's one item is zlib-ng or stored `.ogz`. Per game in the technical pages
  and [backlog/smoothness.md](backlog/smoothness.md).

Not comparable with batch 17: the GLSL compile, DXT and mipmap groups (inside `mesa` and `game`
here); a symbolised re-run of these five would restore them.

## The shader caches

### What Mesa's shader cache covers on nv30

From the source, Mesa 26.2.4 with our patches:

- nv30 has a disk cache: `nouveau_screen_init()` (common to nv30, nv50, nvc0) creates
  `screen->disk_shader_cache` (keyed by the driver binary's build id, `nouveau_screen.c:177-198`) and
  hooks `get_disk_shader_cache`, so the state tracker's cache is on (`st_context.c:795`). In stock
  Mesa only nvc0 also stores its own machine code in it (`nvc0_program.c:576-665`).
- What the GL layer caches: the program's metadata and each stage's serialized NIR after GLSL link
  and `st_link_glsl_to_nir` (`st_shader_cache.c:110`, `shader_cache.cpp:84`). A hit skips the GLSL
  compile and link (LINKING_SKIPPED) and deserializes the NIR. ARB and fixed-function programs go the
  same way.
- What runs anyway, at every start: the variant (`st_create_fp_variant`, `st_finalize_nir`), nv30's
  `nir_to_tgsi` (`nv30_fragprog.c:145`, `nv30_vertprog.c:231`) and the nvfx translation to machine
  code, lazily at the first validate (`_nvfx_fragprog_translate`, `nvfx_fragprog.c:1072`;
  `_nvfx_vertprog_translate`, `nvfx_vertprog.c:956`, again whenever the bound fragment program's
  texcoord mapping or the enabled clip planes change). That is group `shader.backend`.
- Environment, all honoured in this version: `MESA_SHADER_CACHE_DIR`, `MESA_SHADER_CACHE_DISABLE`,
  `MESA_SHADER_CACHE_MAX_SIZE` (default 1 GiB), `MESA_SHADER_CACHE_SHOW_STATS`. The default backend is
  the multi-file one, `mesa_shader_cache/` in `$XDG_CACHE_HOME` or `~/.cache` (`mesa_shader_cache_sf`,
  `_db` only with `MESA_DISK_CACHE_SINGLE_FILE`/`_DATABASE`). `SHADERCACHE=` uses
  `MESA_SHADER_CACHE_DIR`, so no cache is moved aside.

### Mesa's cache measured: CPU saved, no load time

Batch 17, `SHADERCACHE=prime|cold|warm`, only the NIR cache (Mesa .442~dev has no 0039). Cache size
after one run: 2488 KiB (OpenArena, 55 programs), 2972 KiB (BananaBread).

| | load_end (s) | game's own load/GLSL phase (s) | lp_main (s) | lp_glsl | lp_shbe | lp_shcache | warm-up shader groups (s) | disk wait (s) | stall frames in the warm-up (s) |
|---|---|---|---|---|---|---|---|---|---|
| [OpenArena](../gamedb/games/openarena/) prime | 30.95 | glsl1 2.15 | 10.62 | 1.40 | 0.27 | 0.18 | 0.06 | 11.5 | 29.3 |
| [OpenArena](../gamedb/games/openarena/) cold | 28.76 | glsl1 2.11 | 10.52 | 1.44 | 0.28 | 0.18 | 0.06 | 10.9 | 26.8 |
| [OpenArena](../gamedb/games/openarena/) warm | 28.36 | glsl1 1.41 | 9.28 | 0.18 | 0.29 | 0.10 | 0.06 | 11.4 | 29.2 |
| [BananaBread](../gamedb/games/bananabread/) prime | 18.40 | - | 5.04 | 1.79 | 0.39 | 0.40 | 1.11 | 7.2 | 19.7 |
| [BananaBread](../gamedb/games/bananabread/) cold | 17.37 | - | 5.06 | 1.86 | 0.49 | 0.43 | 1.23 | 6.6 | 19.0 |
| [BananaBread](../gamedb/games/bananabread/) warm | 18.32 | - | 4.26 | 0.42 | 0.39 | 0.22 | 0.39 | 7.5 | 15.0 |

`lp_glsl`, `lp_shbe` and `lp_shcache` are all threads over load plus warm-up; `lp_main` is the load's
main thread. The game's own load (`load=`) was 10.3 s cold against 10.6 s warm for OpenArena and
17.4 against 18.4 s for BananaBread (batch 17).

- **What the cache does**: `lp_glsl` falls 1.44 -> 0.18 s (OpenArena) and 1.86 -> 0.42 s
  (BananaBread), the load's main-thread CPU 10.52 -> 9.28 s and 5.06 -> 4.26 s. The cache's own cost
  falls too (0.18 -> 0.10, 0.43 -> 0.22: a hit reads and inflates one blob instead of writing and
  compressing). BananaBread also compiles shaders on first use in its warm-up: 1.23 -> 0.39 s there.
- **Why the load did not get faster.** The CPU saved (1.2 and 0.8 s on the main thread) is smaller
  than the spread of the disk wait between identical runs (about 2.5 s): OpenArena's warm load is
  0.4 s shorter than cold, BananaBread's 1.0 s longer with 0.9 s more disk wait (6.6 -> 7.5). And
  `lp_shbe` does not move (0.28 -> 0.29, 0.49 -> 0.39): variants, `nir_to_tgsi` and nvfx run at every
  start. A third cause, inferred and not measured: OpenArena's own timer for the shader loop reads
  1.41 s warm against 0.57 s of CPU in glsl+backend+cache over the whole span; the missing ~0.8 s fits
  the cache being 55-odd small files read from a cold page cache on the USB disk at ~15 ms a seek
  (test: the recorder's states in that phase, or a warm run with the cache directory in the page
  cache).
- **Not a stall fix in the window.** OpenArena's warm-up stalls are 25-29 s in all three arms, and its
  shader groups in the warm-up are 0.06 s everywhere (it compiles everything at load). BananaBread's
  warm-up stalls go 19.0 -> 15.0 s warm (56 -> 51 stalls, longest 2.1 -> 1.7 s): a hint that the
  first-use compiles mattered, from one run each against a prime arm at 19.7.
- Shader time in the load: BananaBread 1.11 s on the main thread plus 0.77 s in the warm-up,
  OpenArena 1.37 s, [Luanti](../gamedb/games/luanti/) 0.43, STK 0.03, [ClassiCube](../gamedb/games/classicube/) 0.02 s. The cache is no lever for the last
  three.

Verdict: Mesa's cache stays on (it is the default) for the CPU it saves; it gives no measurable load
time on the USB root.

### nv30's shader cache

Mesa 0039 (`packages/patched/graphics/mesa/patches/0039-nv30-compiled-programs-in-the-disk-shader-cache-NV30.patch`,
`src/gallium/drivers/nouveau/nv30/nv30_shader_cache.c`) puts nv30's own half of `shader.backend` into
the screen's disk cache. **Built, off by default, never run on the PS3.** It is in Mesa from
.466~dev20261018175510 on, including the release build .476. With `NV30_SHADER_CACHE=on` it uses the
same `MESA_SHADER_CACHE_DIR` as the NIR, so `SHADERCACHE=cold|warm|off` covers it:

| Entry | Key | Value |
|---|---|---|
| TGSI | the serialized NIR handed to `create_fs_state`/`create_vs_state`, the stage | the tokens `nir_to_tgsi` made (a hit frees the NIR, as `nir_to_tgsi` would) |
| fragment program | the TGSI tokens, the 3D class, `NV40_FP_EXPORT32` | the code as translated (constants unpatched: validate patches them), constant slots, texcoord mapping, FP_CONTROL, coord conventions, RT_ENABLE, texcoords, vp_or, point sprite control |
| vertex program | the TGSI tokens, the class, the bound fragment program's texcoord mapping, the enabled clip planes (the translation's variant key) | the code before relocation, constants and immediates, branch and constant relocations, input/output masks |

Invalidation: nouveau's cache is created with the driver binary's build id and the chipset name
(`nouveau_screen.c` `nouveau_disk_cache_create`), and `disk_cache_compute_key` mixes those into every
key, so a new Mesa build (any patch, any compiler change) misses every old entry. Entries carry a magic
and their sizes and are dropped if they don't parse. Only a program in its initial state is looked up
(a failed translation leaves a partial one, which is translated as before). Not cached: the state
tracker's variant work (`st_create_fp_variant`, `st_finalize_nir`) and the NIR serialization the TGSI
key needs (cheaper than `nir_to_tgsi`'s passes but not free: the `shc_hit_ms` column says how much a
hit costs). The design estimate was 0.5-2 KiB a stage (16-byte instructions, a few tens of them), about
60-250 KiB for [OpenArena](../gamedb/games/openarena/) opengl2's ~55 programs.

`NV30_SHADER_CACHE_STATS=1` prints a `nv30-shcache:` line at context destruction and at exit with
hits, misses and the ms of each for TGSI, fragment and vertex programs (report.py `shc_*`). With the
cache off every creation counts as a miss, so an off run's `shc_miss_ms` is the most the cache could
save. Compile-tested only: nv30 code can't run on the host.

**Its upper bound, measured.** The cache can save at most the `shader.backend` seconds. Batch 17, load
plus warm-up, all threads: OpenArena 0.28, [BananaBread](../gamedb/games/bananabread/) 0.39, [Luanti](../gamedb/games/luanti/) 0.11, STK 0.11, [ClassiCube](../gamedb/games/classicube/) 0.02 s
(main-thread load share 0.24, 0.20, 0.08, 0.04, 0.02). That is before the cost of a hit and below the
run noise, so the A/B was skipped in batch ssd1. It is still queued because it is cheap
([TESTS.md](TESTS.md), "Next"):

    T=SHADERCACHE=warm+NV30_SHADER_CACHE_STATS=1
    PERF_LOAD=1 ENVS="$T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench openarena gl2min 1   # prime
    PERF_LOAD=1 STALLS=1 ENVS="$T $T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench openarena gl2min 2
    PERF_LOAD=1 ENVS="$T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench bananabread base 1   # prime
    PERF_LOAD=1 STALLS=1 ENVS="$T $T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench bananabread base 2
    python3 /var/tmp/bench/report.py --load /var/tmp/ps3-bench/<run dirs>

A new build misses everything, hence the priming run per game (it fills the NIR and the nv30 entries of
the warm cache); both arms then have the warm NIR cache. Read `load`; `lp_shbe` (expected down by
about the off arm's `shc_miss_ms` less the on arm's `shc_hit_ms`); `shc_tgsi_hit`/`_miss` (on: all
hits after the prime; misses mean the NIR differs between runs, worth a look); `shc_vp_miss` (variants
met for the first time); `stalls_warm` (programs first used in the warm-up: their translation is a
hitch the cache removes); `shadercache_kb` (the nv30 entries' size). Check game.log for "failed to
compile" lines and the webcam for wrong rendering: a hit gives the bytes a translation gives, so any
difference is a bug.

## Load-time decoders and linking

What the start-up profiles' decoder and `link` groups can lose without touching a game, costed on the
PC (2026-10-19). The PS3 runs are in [TESTS.md](TESTS.md) "History"; libpng1.6's is [below](#libpng16-on-the-ps3).

**How it was measured.** The decoders ran under qemu-ppc64 (10.2.2 built with TCG plugins) on a
ppc64 sid root, at `-cpu power7` (`-cpu 970`, the closest model to the PPE, crashes glibc's malloc
and libpng under qemu; zlib-ng and libjpeg-turbo choose the same AltiVec paths at power7 as on a
Cell, checked by attribution). The metric is a qemu plugin of ours, `ppecost` (`tools/ppecost/`, with
the harnesses and `attrib.py`): guest instructions, load-hit-stores (a load
from bytes a store wrote within the last 24 instructions: the PPE has no moves between its integer,
floating-point and vector registers, so every conversion goes through memory and its load waits
about 40 cycles for the store), multiplies and divides, per guest PC, and a crude PPE estimate
`insns + 40 x LHS + 8 x mul + 40 x div`. Neither number is PPE time (in-order issue, cache misses
and the slow multiplier aren't modelled); the ratios are what carries over.

| Workload (SuperTuxKart's and BananaBread's own data) | Debian zlib 1.3.2: insns / est | zlib-ng 2.3.3 compat: insns / est | ratio insns, est |
|---|---|---|---|
| 355 SuperTuxKart PNGs (60.5 MB in, 354 MB out), whole decode | 11.04 G / 32.6 G | 9.58 G / 27.9 G | 0.87, 0.86 |
| the same, zlib's part only | 4.75 G / 8.49 G | 2.52 G / 2.63 G | 0.53, 0.31 |
| 20 BananaBread `.ogz` maps (163 MB out), `gzread` | 2.26 G / 2.96 G | 1.53 G / 1.67 G | 0.68, 0.56 |
| OpenArena `pak0.pk3` (84 MB out), inflate + crc32 | 1.80 G / 2.31 G | 1.34 G / 1.44 G | 0.74, 0.62 |

- **zlib-ng halves zlib's part.** On a Cell it selects `adler32_vmx` and `slide_hash_vmx` (AltiVec;
  run-time detection from `AT_HWCAP`'s AltiVec bit) and never its POWER8/9 code, which needs
  `AT_HWCAP2` bits the Cell doesn't report. The AltiVec adler32 is 30% of the PNG gain (3.52 G
  instructions without it). libdeflate is level with it (1.46 G on the maps, 1.32 G on the pk3), so
  a per-game libdeflate patch for BananaBread's `.ogz` (like ioquake3's for pk3s) buys nothing over
  zlib-ng. `-fstack-protector-strong` (Debian's hardening, kept) costs 9% of the gzip case.
- **libpng's filters were the larger half of a PNG:** 57% of the decode's instructions and 97% of
  its load-hit-stores once zlib-ng is in, Paeth on 3- and 4-byte pixels alone 47%: libpng's C reads
  back `row[i-bpp]`, a byte it stored a few instructions before. Our `libpng1.6` package replaces
  them (below).
- **libjpeg-turbo uses its AltiVec code on the Cell.** Debian's `libjpeg62-turbo 1:3.1.3-4` has the
  AltiVec SIMD (no VSX) and picks it at run time from `/proc/cpuinfo`: a line starting `cpu` with
  the word `altivec`, which the PS3's "Cell Broadband Engine, altivec supported" has
  (`JSIMD_FORCEALTIVEC`/`JSIMD_FORCENONE` override it). On 49 SuperTuxKart JPEGs the AltiVec path is
  1.11 G instructions against 2.25 G, the same output; Huffman decoding stays scalar. qemu-user shows
  the host's `/proc/cpuinfo`, so only the PS3 can confirm the detection: a `JSIMD=none` run should
  add about 2 s to SuperTuxKart's load.
- **Linking.** Every game binary and Mesa's libraries are linked `-z now` already, with GNU hash and
  no DT_RELR: `LD_BIND_NOW=1` has nothing left to move (expected within 20 ms). libgallium has 62187
  relative relocations and 337 PLT slots. DT_RELR works on ppc64 big-endian ELFv1 with binutils 2.47
  (`-z pack-relative-relocs`, a test library ran under qemu) and would shrink libgallium's 1.5 MB
  relocation table to a few KB: less to read at each GL start, tens of ms cold. The rest of the
  0.37-0.86 s is probably relocating ELFv1's function descriptors and read-only data (about 1 MB of
  4 KB pages dirtied in every GL process, which is also anonymous memory per game); only ELFv2 or
  fewer DSOs would remove that. Inferred, not measured.

**What it is worth on the PS3** (the profile's seconds times the ratios above, uncertain by about
half: the test images split libpng to zlib 1.3 : 1 where SuperTuxKart's load has 4.2 : 3.9 s):

| Game | Change | Expected |
|---|---|---|
| [SuperTuxKart](../gamedb/games/supertuxkart/) | zlib-ng | 1.8-2.7 s off the load's 3.9 s of zlib (main thread) |
| [SuperTuxKart](../gamedb/games/supertuxkart/) | libpng1.6 unfilters | 2-3.3 s off its 4.2 s of png (measured: 2.4 s, [below](#libpng16-on-the-ps3)) |
| [BananaBread](../gamedb/games/bananabread/) | zlib-ng | 0.5-0.7 s off its 1.6 s of `.ogz` inflate |
| [OpenArena](../gamedb/games/openarena/) | zlib-ng | none: our ioquake3 reads pk3s through libdeflate |
| every GL game | DT_RELR in Mesa (not built) | tens of ms |

**Measured on the PS3, and the defaults since 2026-10-20.** SuperTuxKart's load: libpng1.6 30.3 to
27.6 s, zlib-ng -0.9 s, our libvorbis -0.7 s; BananaBread's and OpenArena's loads didn't move
(TESTS.md 10-19). Faster libraries become the defaults (the user, 2026-10-20). Our libpng16-16t64,
libvorbis0a/libvorbisfile3, libopenal1 and libfluidsynth3 replace Debian's as upgrades, on the PS3
and in the image. zlib-ng stays out of the system's libz.so.1 (dpkg, apt and sshd load it, and its
deflate output differs from zlib's byte for byte), but every game started through `ps3-game` gets
it: libz-ng-compat is a Recommends of ps3-desktop, and `PS3_ZLIB=stock` turns it off for a game.

**libpng1.6.** `packages/patched/graphics/libpng1.6/` (Debian's source, 1.6.59-1) is built `-O3 -mcpu=cell -mtune=cell`, with
Debian's `-DPNG_POWERPC_VSX=off` kept: libpng's own PowerPC option accepts AltiVec or VSX in its
run-time check and would run VSX code on a Cell. The patch is `powerpc/filter_cell.h`, included by
`pngrutil.c` through libpng's `PNG_FILTER_OPTIMIZATIONS` hook, and only compiled for big-endian
ppc64 with AltiVec and without VSX (`-DPNG_NO_CELL_FILTERS` turns it off). It replaces Up for every
pixel size, and Sub, Avg and Paeth for 3- and 4-byte pixels, which is 8-bit RGB and RGBA. Palette
and gray images are unfiltered at their stored pixel size (1 or 2 bytes, 16-bit RGB and RGBA 6 or 8)
by libpng's C, except Up; the 1-byte Paeth already keeps its left pixel in a register. Each byte depends on the byte `bpp` before it, so a 16-byte vector is
finished one group of `bpp` bytes at a time (Avg and Paeth: the group's left neighbours are the
previous group, a shift of the vector, or of the previous vector for the first group; Sub is a prefix
sum). Paeth works in 8 bits: `pa` and `pb` are absolute differences by max/min/subtract, and `pc =
|a+b-2c|` is `pa+pb` (saturating, which can't change the choice) when `a` and `b` lie on one side of
`c` and `|pa-pb|` otherwise. The first bytes up to an aligned address, and the last, are scalar. No
byte is stored and read back except once per row, at the seam between the scalar and vector parts.

| Per byte, one row filter (qemu-user, `pngfilt -b`, 64 rows of 4092 bytes) | generic C: insns, load-hit-stores | ours |
|---|---|---|
| Up | 6.66, 0.87 | 0.76, 0.003 |
| Sub, 3 / 4 byte pixels | 5.66, 1.87 | 0.94, 0.003 / 0.83, 0.004 |
| Avg, 3 / 4 | 9.17, 1.87 / 9.16, 1.75 | 3.27, 0.005 / 2.41, 0.005 |
| Paeth, 3 / 4 | 25.07, 1.62 / 25.05, 1.50 | 8.94, 0.002 / 6.46, 0.002 |

| 355 SuperTuxKart PNGs (354 MB out), whole decode with zlib-ng | insns / load-hit-stores / est | ratio est |
|---|---|---|
| Debian's libpng 1.6.59 (`-O2`) | 9.48 G / 451 M / 27.5 G | 1 |
| the same source `-O3 -mcpu=cell` | 9.03 G / 385 M / 24.4 G | 0.89 |
| `libpng1.6` of ours | 5.59 G / 22 M / 6.5 G | 0.24 |

libpng's own part (everything but zlib-ng and libc) goes from 5.56 G instructions and 436 M
load-hit-stores to 1.67 G and 7 M; what is left there is the transforms (palette and gray expansion,
16-bit stripping), the row copies and the pixels filtered with None. The estimate is the plugin's
crude cycles, in which 40 per load-hit-store carries the stock case; the instruction ratio, 0.59 of
the decode and 0.30 of libpng's part, is the cautious figure. One thing the model can't see: the
Avg and Paeth steps are a dependent chain of AltiVec operations (a rough 40 cycles per group for
Paeth at 4 cycles an operation), 4 or 3 bytes per step; the stock C needs about 100 instructions for
a 4-byte pixel even with no stalls, so the chain should still win, but only the PS3 will say.

Bit-identical output was checked four ways, all under qemu-ppc64 against the generic C or the stock
library: `tools/ppecost/pngfilt.c` (the Cell functions against libpng's, every row length up to 300
and 900 random ones up to 70000 bytes, all 16 alignments of the row and of the previous row, six kinds
of content, guard bytes around both rows: 238k checks per seed, three seeds, 0 differences; with Avg's
rounding and Paeth's `pc` broken on purpose, 29652 checks fail); `pngrt.c` (every colour type and depth, plain and Adam7,
widths 1 to 300, each filter forced on every row and the encoder's own choice, written by libpng and
read back: 41580 images, 0 mismatches, with the stock library and with ours); `pnghash.c` (the raw
decoded rows of all 2337 PNGs of Debian's `supertuxkart-data`, the 318 of `cube2-data` and libpng's
pngsuite and test images, hashed with both libraries: identical); and the package's own build, which
runs libpng's 37 tests (pngtest, pngstest, pngunknown, pngimage over the whole pngsuite) and
`pngtest-static`.

#### libpng1.6 on the PS3

Batch ssdroot6 (2026-10-19 16:51-18:10Z, kernel .472, SSD root, Mesa ps3.545~dev): libpng 1.6.59
+ps3.560 against Debian's copy (`LDPATH=/var/tmp/ps3-bench/stock-png`), zlib-ng on both arms,
rotated, webcam picture right in every run.

| Run (n per arm) | stock libpng | ours | Change |
|---|---|---|---|
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean, load profile `png` group (2) | 4.00, 4.05 s | 1.61, 1.61 s | -2.4 s, -60% |
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean, load (2) | 30.42, 30.25 s | 26.93, 28.20 s | -2.8 s, -9% |
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean, main thread's CPU over the load (2) | 23.8, 24.2 s | 21.1, 21.7 s | -2.5 s |
| [SuperTuxKart](../gamedb/games/supertuxkart/) lean, gameplay: steady fps, 1% low, frames over 100 ms (2) | 20, 11.9, 10 | 19.5, 11.5, 10 | same |
| [BananaBread](../gamedb/games/bananabread/) base, load (3) | 7.19 s (6.95-8.21) | 7.25 s (7.15-7.48) | same |
| [BananaBread](../gamedb/games/bananabread/) base, steady fps, 1% low (3) | 39.5, 27.1 | 39.5, 26.6 | same |
| [OpenArena](../gamedb/games/openarena/) gl2min, load (2), the control | 6.80 s | 6.62 s | -0.2 s, noise |

The png group moved by what the model predicted (2-3.3 s), and the load follows it, so the AltiVec
Paeth's dependent chain is not slower than the C it replaces. Round 1 of the SuperTuxKart pairs was
interfered (a login at 9 s, mouse moves) and is out of the medians; its png group, 4.17 against
1.67 s, agrees. The zlib group is 3.2-3.5 s in both arms, so nothing else in the load changed.
BananaBread had no load profile, and its load did not move: libpng is too small a part of its 7 s
for the timer. Gameplay is untouched, as a decoder that runs only during the load should leave it.
One stock-arm run (17:16Z) ended with SIGABRT at the exit after a full race; the cause was not
looked at. So `libpng16-16t64` +ps3.560 is a proven 2.8 s on SuperTuxKart's 30 s load and nothing
measurable elsewhere.

**The package.** `packages/repackaged/zlib-ng/` (TYPE upstream, the pinned 2.3.3 tarball, `packages/repackaged/zlib-ng/upstream.mk`) builds
`libz-ng-compat`: only `libz.so.1` (zlib-compat API, symbol versions to `ZLIB_1.2.12`) in
`/usr/lib/powerpc64-linux-gnu/zlib-ng`, which the linker never searches by itself, built
`-mcpu=cell -mtune=cell`. Games get it through `LD_LIBRARY_PATH`: `ps3-game` sets it for every game since 2026-10-20 (`PS3_ZLIB=stock` off), and so does the bench (`ZLIB=stock` off).
zlib-ng's 70 tests pass in the build (its googletest suite skips the VMX adler32 when POWER8 code is
compiled in; checked separately: adler32 and crc32 over 8 MB match Debian's zlib, every decode above
gives identical output). A binary that needs Debian 1.3.2's newer symbol versions (`ZLIB_1.3.1.2`,
`ZLIB_1.3.2`) would not load with it. Replacing zlib1g system-wide is not proposed: dpkg and apt load
it, and zlib-ng's compressed output differs byte for byte from zlib's (Fedora ships it as its system
zlib, so it can be done; per game gets the same load gain without the risk).

## The SSD load split (batch ssd1)

Batch ssd1 (2026-10-18 22:10-23:25Z, kernel .472, USB root, gamedb:batch/2026-10-18-ssd1, commit 9e6bc57) put the
game data (and in a third arm the VRAM swap's spill) on a disk image on the internal SSD. The load's
disk wait (the recorder's sda class) stayed at 4.8-5.5 s in [BananaBread](../gamedb/games/bananabread/) and 6.2-6.7 s in [OpenArena](../gamedb/games/openarena/):
only 16-50 MB of the load's 76-144 MB of sda reads are game data, and the rest (binary, libraries,
Mesa, the shader cache, Xorg) stays on USB. The load's CPU, fault and io shares did not change either.
OpenArena's own load was shorter by about 3 s (-28%) and its `load_end` by about 3.6 s; BananaBread
showed no proven gain. Tables and verdicts: [GAMEOS-HDD.md](GAMEOS-HDD.md), "Result, batch ssd1".

## Overhead

Measured on the host (Zen 2), estimated for the PPE; every run states its own numbers.

- perf in the kernel: one hrtimer interrupt and a back-chain walk (8 bytes a frame read from the user
  stack) per sample, at 499 Hz for each running thread. Estimated 1-3% of the PPE for a CPU-bound game,
  charged to the game's threads, so it shows only as lower fps. perf's own reader: `perf_self` (0.25%
  of a Zen 2 thread for two busy processes in the container test). Data: about 250-400 bytes a sample
  to the disk. The one PS3 data point: in the 2026-10-18 profiling batch [OpenArena](../gamedb/games/openarena/) ran at 50.7, 50.5
  and 52.7 fps profiled against 52.7 and 50.2 without ([HOTPATHS.md](HOTPATHS.md)), inside the
  noise, but that is one game and no interleaved A/B. Before reading fps off profiled runs at all,
  measure it with a same-boot A/B (`PERF=0` against `PERF=1`, three rounds); `PERF_HZ=199` cuts the
  cost by 2.5.
- perf-post.sh: after the run, nothing during it (perf script and the collapser take a minute or two
  on the PPE).
- MEM: three snapshots. Reading smaps walks the game's page tables under its mmap lock: under 1 ms for
  an 18 MiB process on the host; `mem_smaps_ms` says what the PPE takes. A page fault or mmap of the
  game waits meanwhile, so a stall may coincide with a snapshot (`mem_snaps` gives their times).
  ps3-mallocstat.so: one sleeping thread, 64 KiB of stack, a few KiB of code; `malloc_info` takes each
  arena's lock in turn for a moment.

## Getting the data to the PC

    make bench-pull                 # every run directory into build/bench, without perf.data
    make bench-pull PERFDATA=1      # with perf.data, perf-load.data, perf-sys.data and perf-syms.tar
    bench/report.py --perf --mem build/bench/<run dir>...
    bench/perfprof.py build/bench/<run dir>
    flamegraph.pl build/bench/<run dir>/perf.folded > flame.svg   # or speedscope, inferno

The text files and `perf.folded` are symbolised on the PS3 and are all most analysis needs. For
`perf report`/`perf script` on the PC (other sort keys, other questions), record with `PERF_SYMS=1`
and pull with `PERFDATA=1`, then:

    cd build/bench/<run dir> && mkdir -p sym && tar -xf perf-syms.tar -C sym
    perf report -i perf.data --symfs sym --kallsyms perf-kallsyms --no-children --sort sym

perf on x86 reads ppc64's big-endian perf.data (it swaps), but the powerpc call-chain check above does
not run there. For that, use the PS3's own perf under qemu (next section). The cross-game ranking in
[HOTPATHS.md](HOTPATHS.md) used a third way, `bench/hotpaths/`, which reads perf.data directly.

### Re-processing a run on the PC

`bench/hotpaths/perfq.sh` runs the PS3's own perf (Debian's 7.2.9 for ppc64) under qemu-user
from a root (`PERFQ_ROOT`, default `build/tmp/hotpaths-sym`), which gives the PS3's call-chain
handling on the PC:

    R=build/tmp/hotpaths-sym; mkdir -p $R
    tar -xf build/rootfs/base.tar -C $R ./usr/bin/perf ./usr/lib/powerpc64-linux-gnu ./usr/lib64 ./lib ./lib64 ./bin
    bench/hotpaths/symroot.sh     # Mesa and its dbgsym, the kernel's System.map, ps3rsx, libc6-dbg
    # the game's own binaries and dbgsym of the run's versions: unpack their debs into $R too
    PERFPOST_PERF=bench/hotpaths/perfq.sh PERFPROF_ROOT=$R bench/perf-post.sh <copy of the run dir>

Module symbols are not in System.map, so `[drm]` frames stay unnamed there (still kgpu). The host has
no dpkg database of the PS3, so `perf-pkgs.txt` comes out empty: write the game's libraries into it by
hand (`path<TAB>package`) and run `bench/perfprof.py <dir>` again. Delete the root afterwards.

To redo a run's text files on the PS3 itself instead: `make bench-push`, then
`/var/tmp/bench/perf-post.sh /var/tmp/ps3-bench/<run dir>` with the debug symbols of the run's
versions installed.

## Validation

`python3 bench/selftest.py`, sections 7-9:

- perfprof.py on a made-up `perf script` output (call chains: a game, its renderer library,
  libgallium, libdrm_nouveau, ps3rsx and drm in the kernel, a page fault, memcpy, OpenAL, an audio
  thread, an unknown sample, Xorg and kswapd0 alongside; and the one-line form of the system-wide
  record): the folded stacks and every report.py column as computed by hand. Then the gaps of the
  2026-10-18 batch: a library built here, mapped in a made-up smaps, its frames `[unknown]` in the perf
  script text: filled with its function names, the two kinds of unchecked link register entry
  dropped, a kernel sample's user address filled, an address in no mapping and another process's
  frames left alone, `.drm_ioctl` and `[drm]` as kgpu, a library of the `versions` package as game.
- client.sh with a fake game: a fake perf on the control fifos gets enable at load + WARMUP and
  disable at + MEASURE, each acknowledged, at the times bench.txt says; ps3-mallocstat.so (built with
  the host's cc) preloaded, the three snapshots at window + 1, middle and end, each with all its files
  and a malloc dump; the classes add up to smaps_rollup's Rss.
- memprof.py on a real C process that reports its own thread stacks and malloc arenas: all found.

Also checked in a Debian container with Debian's perf 7.2.9 (x86): the exact record command above with
`-D -1` and the control fifo (samples only between enable and disable, ack in 2.5 ms); perf-post.sh end
to end; the load's mmaps (a program that dlopens a library 4 s after perf attached: `[unknown]` with
plain `-D -1` and with `enable dummy:u`, the library's function by name with ps3-bench.sh's enable plus
disable cpu-clock, none of the samples before the window); and perf-post.sh on the 2026-10-18 batch's
perf.data with the PS3's perf under qemu. On the PS3 itself, perf has since run in batches 8, 17 and
the hot-path rounds ([HOTPATHS.md](HOTPATHS.md)).

## Pitfalls

What went wrong in the first profiling batch (2026-10-18, [HOTPATHS.md](HOTPATHS.md)), and what fixed
it. All are fixed for new runs.

- **perf script died early.** `perf.folded` and `perf-leaf.txt` held only the first 1-621 samples of
  every run, so that batch's report.py `perf_*` columns were wrong (`perf-report.txt` and
  `perf-threads.txt` were complete). Debian's perf 7.2 for ppc64 has neither libbfd nor LLVM, so to
  name inlined functions perf script runs the binutils `addr2line` command, which the image doesn't
  have: it wrote to the dead command and died of SIGPIPE (exit 141), and `perf-post.err` had
  `addr2line configuration failed` (not always: the [ClassiCube](../gamedb/games/classicube/), ETR, [Neverball](../gamedb/games/neverball/) and [BananaBread](../gamedb/games/bananabread/) runs
  died without the message). perf report with `-g none` never asks for inlines. Fix: `--no-inline`,
  plus the sample-count check after the collapse.
- **`[unknown]` libraries.** perf recorded no mmap while disabled, so libGLX_mesa and libgallium were
  `[unknown]`: 25.7% of [OpenArena](../gamedb/games/openarena/)'s samples. Fix: the tracking step at perf's start (ps3-gpu-tools
  0.412); for older records perfprof.py fills the frames from the MEM=1 smaps.
- **`--proc-map-timeout=20000`.** With perf's default of 500 ms for reading a running process's
  maps, most samples were `[unknown]`.
- **The kernel dot.** perf on the PS3 names kernel functions with ppc64's dot (`.drm_ioctl`) and puts
  ps3rsx's functions in `[kernel.kallsyms]`, which kept them out of `perf_kgpu` until the class rules
  learnt both.
- **Runs that never started their window or showed the wrong picture.** Armagetron's base played back
  a 90 s recording that draws nothing after its end (black screen with the cursor; the recipe now plays
  live AI rounds, the recording is variant `playback`). [OpenTTD](../gamedb/games/openttd/)'s base had no GL, so no HUD frame ever
  started the window (the recipe now starts it at the title game's load, `-d sl=1`), and its survey
  prompt covered the title game (answered in its config). [dsda-doom](../gamedb/games/dsda-doom/)'s window never started because its
  `LOAD_MARK` lines never print (fixed in 6a59892: `Playing demo:`). In all three the result's perf
  line said `window never enabled` or the HUD stopped early. Check every profiled run's webcam photos
  and its perf line.
- The batch's perf.data re-processed with the fixed perf-post.sh and the PS3's perf under qemu-user
  matches the PC reader's numbers: every sample collapsed (OpenArena 57510), the class shares equal to
  the tenth (OpenArena game 23.2, mesa 27.3, drm 0.0, kgpu 4.7, kernel 10.8, libc 6.9, other 27.0;
  BananaBread 7.3, 72.0, 0.1, 3.6, 6.2, 3.4, 7.5), the same leaf symbols with the same counts.

Older versions and the dated history: [archive/PROFILING.md](archive/PROFILING.md).
