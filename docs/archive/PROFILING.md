> Superseded on 2026-10-19 by docs/PROFILING.md; kept unchanged for its history.

# CPU and memory profiles of the games

The user (2026-10-18): find more optimisations through CPU and memory profiles across all the
games. Stutters are often swap, so less memory per game means fewer of them, and the GPU driver
(Mesa nv30, libdrm_nouveau, ps3rsx) is a hot path in many games. Two switches of the bench harness
record what that takes, in the same style as `STALLS=1` (docs/STALLS.md): set in run.sh's
environment, off by default, the data in the run directory, columns in report.py, covered by
selftest.py.

- `PERF=1`: a perf profile (call graphs) of the game's threads over the measured window, plus Xorg
  and kswapd0 alongside.
- `MEM=1`: the game's memory by mapping class, glibc's malloc numbers and its GPU buffers at the
  window's start, middle and end.
- `PERF_LOAD=1`: a perf profile of the start-up (exec, loading screens, warm-up), the load's I/O
  and ld.so's statistics; `SHADERCACHE=cold|warm|off` for Mesa's shader cache (below, "Start-up
  profiling").

**Profiled runs are for profiles, not for fps.** perf adds work to every sample (see Overhead), and
a memory snapshot holds the game's mmap lock while the kernel walks its page tables. Base fps and
stall decisions on runs without these switches (same-boot A/B as always).

## Running it

On the PS3 (`make bench-push` done, ps3-gpu-tools 0.391 or later for `PERF=1`'s perf control and
`MEM=1`'s malloc statistics):

    PERF=1 /var/tmp/bench/run.sh smoke openarena              # prints the tables at the end
    PERF=1 MEM=1 /var/tmp/bench/run.sh bench openarena base 1   # both in one run
    MEM=1 /var/tmp/bench/run.sh suite base 1                  # memory of every suite game
    PERF=1 /var/tmp/bench/run.sh suite base 1                 # a profile of every suite game
    python3 /var/tmp/bench/report.py --perf --mem /var/tmp/ps3-bench/<run dir>...

| Variable | Default | What |
|---|---|---|
| `PERF=1` | off | perf record of the window (ps3-bench.sh `BENCH_PERF=1`) |
| `PERF_HZ` | 499 | samples a second per running thread |
| `PERF_ALL=1` | off | also a system-wide record without call graphs (`perf-sys.data`): the kworkers, interrupts, idle. Off because `perf record -a -g` hung the PS3 once (docs/PERF.md); this one has no `-g`, but no run has tried it yet: watch the first one |
| `PERF_SYMS=1` | off | perf-post.sh also packs the symbols for analysis on the PC (`perf-syms.tar`, `perf-kallsyms`) |
| `MEM=1` | off | the memory snapshots (client.sh `BENCH_MEM=1`) |

After every `PERF=1` run, run.sh calls `perf-post.sh` on it (outside the timed part: the next run's
quiet drops the caches and empties the swaps again). By hand, for a run made earlier:
`/var/tmp/bench/perf-post.sh /var/tmp/ps3-bench/<run dir>`.

### Packages on the PS3

- `linux-perf` is in the base image (Debian's; it needs `-D -1`, `--control` and `--buildid-mmap`,
  which Debian's 7.x has: check `perf --version` and `perf.err` after the first run).
- ps3-gpu-tools with this change (`ps3-mallocstat.so` in `/usr/lib/ps3/lib`, ps3-bench.sh's
  `BENCH_PERF`): `make ps3-gpu-tools-deb`, then `make deploy` of it. 0.412 or later for the mmaps
  of the load (below); older ones leave libraries dlopened during the load `[unknown]`.
- Debug symbols, only for the profile runs (they go to the disk, never into images; remove them
  afterwards). Without them perf names only exported functions; static ones show as addresses
  inside the DSO (the class shares are right either way). Each must match the installed version
  exactly (`apt install P-dbgsym=$(dpkg-query -W -f '${Version}' P)`); our dev builds' are in the
  `ps3-dev` suite, stable releases' in `ps3-dbgsym`:
  - the driver, every game: `mesa-libgallium-dbgsym` (libgallium: nv30, the state tracker, u_vbuf),
    `libgl1-mesa-dri-dbgsym`, `libglx-mesa0-dbgsym`, `libegl-mesa0-dbgsym`, `libgbm1-dbgsym`;
  - libc: `libc6-dbg` (Debian's: memcpy and malloc by name);
  - audio and SDL where the game uses them: `libopenal1-dbgsym`, `libfluidsynth3-dbgsym`,
    `libsdl3-0-dbgsym`, `libspeq0-dbgsym`;
  - the game, when it is one of ours: `ioquake3-dbgsym`, `yamagi-quake2-dbgsym` and
    `yamagi-quake2-core-dbgsym`, `classicube-dbgsym`, `cube2-dbgsym`, `dsda-doom-dbgsym`,
    `extremetuxracer-dbgsym`, `neverball-dbgsym`, `openttd-dbgsym`, `supertuxkart-dbgsym`,
    `xmoto-dbgsym`, `luanti-dbgsym` with `libluajit-5.1-2-dbgsym`.
  - Not ours, no dbgsym in our repository: libdrm and libdrm_nouveau (Debian's; their exported
    functions resolve from the dynamic symbol table), and the Debian games (QuakeSpasm, Armagetron,
    Chromium B.S.U., ...): their own code shows as addresses but still counts as `game`.
  - The kernel and ps3rsx need nothing: perf-post.sh runs perf as root, which reads
    `/proc/kallsyms` (module symbols included).

## What PERF=1 records

ps3-bench.sh (`BENCH_PERF=1`) starts, as root, once client.sh's `.pid` names the game:

    perf record -e cpu-clock -F 499 -g -k mono -N --buildid-mmap -D -1 \
        --control fifo:$D/.perf.ctl,$D/.perf.ack --proc-map-timeout=20000 \
        -p <game>,<Xorg>,<kswapd0> -o <disk>/perf.data

- **Started disabled** (`-D -1`) during the load, so perf has read the game's maps long before the
  window (`--proc-map-timeout=20000`: with the default 500 ms most samples were `[unknown]`).
  client.sh writes `enable` into the control fifo WARMUP s after the load (0.9 x WARMUP for games
  that repeat a demo, `PASSES=1`, where the first counted pass may start that early; at every
  `WIN_ON_RE` line for games that mark their own window) and `disable` at the window's end (MEASURE
  s later, after the last counted pass, at every `WIN_OFF_RE` line). perf acknowledges each one
  (2.5 ms in the container test). bench.txt gets `perf_win` (on/off pairs, seconds from the client's
  start), `perf_ack` (ok, or missing when perf never answered: see `perf.err`) and `perf_cpu`
  (perf's own CPU seconds while enabled, from its `/proc` stat).
- **The load's mmaps** (ps3-gpu-tools 0.412): right after starting perf, ps3-bench.sh writes
  `enable` and `disable cpu-clock` into the control fifo at once and reads both acks (`perf.track`:
  `ok`, or which ack never came; the result's perf line repeats it). The tracking event (perf's
  `dummy:u`, which carries the mmap, comm and task records) stays on, the samples stay off. Without
  it, perf with `-D -1` on running processes keeps the tracking event off until an exec that never
  comes, so libraries dlopened during the load (libGLX_mesa, libgallium at context creation) got no
  map: 25.7% of OpenArena's samples were `[unknown]` in the 2026-10-18 batch. `enable dummy:u`
  through the fifo is acknowledged but brings no mmap records (tried in a container with Debian's
  perf 7.2.9, the PS3's version, on a test program that dlopens a library after perf attached:
  `[unknown]` as before, while enable plus disable cpu-clock named the library's function and kept
  every sample of the code before out). The one perf loop between the two commands may take a
  sample of the load (one in the container test).
- **All the game's threads** (`-p` follows the threads it creates later: inherit), plus Xorg (its
  work for the game) and kswapd0 (background reclaim and zram compression).
- **cpu-clock**: perf_events has no driver for the Cell PMU, so the samples come from an hrtimer.
  On ppc64 an interrupt that arrives while the kernel has interrupts soft-disabled is replayed when
  they are enabled again, so kernel time in such regions lands on the function that re-enables them
  (`arch_local_irq_restore`, `_raw_spin_unlock_irqrestore`).
- **Call graphs by frame pointer, which on ppc64 is the ABI's stack back chain**: every ppc64 frame
  stores its caller's stack pointer whatever the compiler flags, so Debian's builds unwind without
  `-fno-omit-frame-pointer`, and the kernel stores the link register for the leaf frame. perf's
  powerpc code checks each leaf against the DSO's CFI (`.eh_frame`, in every binary) to drop or keep
  that extra entry. DWARF unwinding (`--call-graph dwarf`) would copy 8 KiB of stack per sample
  (5 MB/s at this rate) for no gain here. On an x86 PC the same perf.data unwinds without that check,
  so a caller may appear twice: the folded stacks made on the PS3 are the reference.
- `--buildid-mmap`: build ids come with the mmap events, so perf doesn't read the whole file again
  at the end; `-N`: nothing copied into root's build-id cache; `-k mono`: sample times on
  CLOCK_MONOTONIC (the stall recorder's clock).
- **perf.data goes to the disk** (`/var/tmp/ps3-bench/.perf-<run>/`, moved into the run directory at
  the end), not /dev/shm with the rest of the run: about 250-400 bytes a sample, 20-30 MB for a
  120 s window of a CPU-bound game, which in /dev/shm would be RAM taken from the game.

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

The collapser is `perfprof.py collapse` (no stackcollapse-perf.pl on the PS3; it reads `perf script
--no-inline -F comm,pid,tid,time,period,ip,sym,dso`). 100k samples of 20 frames take 2.2 s on a Zen 2
host, so about 30-45 s on the PPE; perf script itself takes longer.

- `--no-inline`: perf script names inlined functions in call chains through addr2line. Debian's
  perf 7.2 for ppc64 has neither libbfd nor LLVM, so it runs the binutils `addr2line` command, and
  the image has no binutils: perf wrote to the dead command, died of SIGPIPE (exit 141) after the
  first few samples, and `perf-post.err` had `addr2line configuration failed` (not always: the
  ClassiCube, ETR, Neverball and Sauerbraten runs died without the message). perf report with
  `-g none` never asks for inlines, so `perf-report.txt` and `perf-threads.txt` were complete.
- After the collapse perf-post.sh compares its sample count with perf report's (the Samples column
  of `perf-threads.txt`) and prints `!! perf script exit ..., N of M samples collapsed` into its
  output and `perf-post.err` when they differ by more than 1%.
- **Frames perf left `[unknown]`** in the game process (records made before ps3-gpu-tools 0.412, or
  whenever perf missed a map) are filled from the run's MEM=1 smaps (`mem/mid.smaps`, else start
  or end): the mapping gives the library, the library and its debug file
  (`/usr/lib/debug/.build-id/`, by the build id in `perf-buildids.txt` or the library's note) the
  symbol, through `hotpaths/elfsym.py` (it maps the files, so libgallium's 25 MB and its 25 MB debug
  file cost only the symbol table pages). The link register entry perf could not check against a
  leaf without a map is dropped by `hotpaths/analyze.py`'s rule (it repeats the next entry or lands
  in the leaf's function). `perf-leaf.txt` then has `# filled_from_smaps=<dso>:<samples>,...` and
  `perfprof.py` prints it; more than 3% `[unknown]` left gets a warning. Without MEM=1 those
  frames stay `[unknown]`. `PERFPROF_ROOT` puts a symbol root in front of the paths (for a run on
  the PC).

### report.py's perf columns

Percent of the game process's samples in the window, by the DSO (and for the kernel the symbol) of
the sampled instruction, self time:

| Column | Class |
|---|---|
| `perf_game` | the game's executable and its own libraries (same package, or the package of the recipe's command as run.sh's `versions` names it: OpenArena's cgame and ui are `openarena`'s, the engine `ioquake3`'s; same directory, `/usr/lib/<name>`) |
| `perf_mesa` | libgallium, `*_dri.so`, libGLX_mesa, libEGL_mesa, libgbm, libglapi, glvnd's libGL/libGLX/libGLdispatch |
| `perf_drm` | libdrm, libdrm_nouveau (pushbuf, fences, buffer objects) |
| `perf_kgpu` | the `[ps3rsx]` and `[drm*]` modules, and `drm_*`, `dma_fence*`, `dma_resv*`, `ttm_*`, `nouveau*`, `ps3rsx*` in the kernel image (perf on the PS3 names them with ppc64's dot, `.drm_ioctl`, and puts ps3rsx's functions in `[kernel.kallsyms]`: until 2026-10-18 the dot kept them out of this class) |
| `perf_kernel` | the rest of the kernel: system calls, page faults, reclaim and zram compression done by the game |
| `perf_libc` | libc, ld.so, libm (memcpy, memset, malloc, string functions) |
| `perf_other` | every other library: SDL, OpenAL, ALSA, X, LuaJIT, zlib, ... |
| `perf_unk` | samples in no DSO perf knows: above a few percent, the profile missed maps |
| `perf_xorg`, `perf_kswapd` | their samples as percent of the game's (CPU spent for the game elsewhere) |
| `perf_n`, `perf_self` | the game's samples; perf's own CPU while enabled, % of one thread |
| `perf_sys_*` | `PERF_ALL=1`: shares of both CPUs: game, idle, xorg, kswapd, kworker, vswap, other |

The driver stack of a frame is `perf_mesa + perf_drm + perf_kgpu`; memory pressure shows as
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
| `.gpu` | ps3rsx's `ps3rsx_mm` (debugfs, root: read once a snapshot, it takes `mm_lock` and walks the buffers): the game's VRAM and GART buffers, GART backed by RAM, the VRAM swap |
| `.info` | label, pid, time from the client's start, ms spent reading smaps, executable |

bench.txt gets `mem_snaps` (label:seconds:ms per snapshot).

**ps3-mallocstat.so** (ps3-gpu-tools/preload/) is preloaded into the game (`LD_PRELOAD`, which
`stdbuf` and the launchers pass on). It acts only in the process named `COMM` (the recipe's): its
constructor starts one thread with a 64 KiB stack that blocks on a fifo, and every label written
there makes it dump the statistics. No signal handler (a game using SIGUSR1/2 or blocking signals
is untouched; `malloc_info` from a handler interrupting malloc could deadlock on an arena lock), and
the thread never calls malloc itself (the output stream is opened by the constructor and reused with
a static buffer), so glibc gives it no arena of its own to report. Turn it off with
`BENCH_MALLOCSTAT=` (empty) in client.sh's environment.

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

The guard-page rule needs the kernel to keep the guard as its own mapping: glibc 2.42 on kernels
6.13+ uses `MADV_GUARD_INSTALL` instead, so on a current PC only the stack-pointer rule finds the
stacks; the PS3's 6.12 still has separate guards. With `CONFIG_ANON_VMA_NAME` (not in our kernel)
glibc's `glibc.mem.decorate_maps` tunable would name arenas and stacks exactly; memprof.py reads
those names when present.

### report.py's mem columns

At the middle snapshot (else the nearest): `mem_<class>_rss` and `mem_<class>_swap` (MiB) for
every class, the totals `mem_rss`, `mem_swap`, `mem_anon`, `mem_topswap` (the 5 mappings with the
most swap, `name=MiB`, anonymous ones as `class@address(size)`), `mem_grow` (Rss + swap at the end
minus at the start: a leak or a cache that keeps growing), `mem_malloc_used` and `mem_malloc_free`
(MiB malloc hands out, and holds free: fragmentation, or memory `malloc_trim` could return),
`mem_arenas`, `mem_gpu_vram` and `mem_gpu_gart` (the game's ps3rsx buffers), `mem_smaps_ms` (the
longest smaps read: how long the game's mmap lock was held). `memprof.py <run dir>` (or `report.py
--mem`) prints every snapshot's table with Pss, Anonymous and Private_Dirty, the mappings with the
most swap and Rss, malloc's and ps3rsx's lines; `memprof.py --smaps FILE [EXE]` classifies any
process's smaps.

How to read it: swap is what to cut first (each swapped page is a later stall). `heap` and `anon`
with a large `mem_malloc_free` mean memory malloc keeps; `mesa` Private_Dirty is the driver's own
data; texture copies kept by the game or Mesa land in `heap`/`anon`, the GPU's copies in
`mem_gpu_*`.

## Start-up profiling

The user (2026-10-18): find where game start-up time could be won, at the system and library level
as much as per game: the USB disk, decompression (zlib for pk3/zip, libpng, libjpeg-turbo,
libvorbis), Mesa's texture uploads, format conversion and mipmaps, DXT compression, shader
compiles, dynamic linking, kernel I/O, reclaim and swap during the load. Then the scope widened to
the whole span the measured window leaves out: the game's exec, its loading screens **and the
warm-up** (WARMUP s of play after the load: late loads, shaders compiled on first use, textures
streamed in). `PERF_LOAD=1` profiles that span; `STALLS=1` alongside says what the main thread
waited for.

### Running it

On the PS3, ps3-gpu-tools with `BENCH_PERF_LOAD` (0.469 or later, `make ps3-gpu-tools-deb` and a
deploy) and this games/bench (`make bench-push`). One run per game: the quiet before every run
drops the page cache, so each start reads its files from the disk as a first start after boot
does. Leave `GAMEPREFETCH` unset (the game prefetch would read the files ahead and hide the disk:
client.sh then also turns game mode's own prefetch off). Without `SHADERCACHE` the game uses the
user's own Mesa cache (`~/.cache/mesa_shader_cache`): warm for any game played before.

    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench sauerbraten base 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench openarena gl2min 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench luanti base 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench supertuxkart lean 1
    PERF_LOAD=1 STALLS=1 /var/tmp/bench/run.sh bench classicube base 1
    python3 /var/tmp/bench/report.py --load --stalls /var/tmp/ps3-bench/<run dir>...

The same five from the internal SSD, same boot, against the USB disk: `ENVS="- GAMEDATA=ssd
GAMEDATA=ssd+SWAP=ssd"` (docs/GAMEOS-HDD.md "Test drive, Phase A": the game data and the VRAM swap's
spill on a disk image in dev_hdd0).

The shader cache, cold against warm over the same span (OpenArena gl2min is the clearest case: about
55 GLSL programs, ~3 s at its first start; Sauerbraten and Luanti use GLSL too). `SHADERCACHE=warm`
keeps a cache per game in `/var/tmp/ps3-bench/shadercache/warm-<game>`; its first run fills it and
is tagged `scprime`, so prime once, then alternate in the same session:

    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=warm" /var/tmp/bench/run.sh bench openarena gl2min 1   # scprime
    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=cold SHADERCACHE=warm" /var/tmp/bench/run.sh bench openarena gl2min 1
    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=warm" /var/tmp/bench/run.sh bench sauerbraten base 1
    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=cold SHADERCACHE=warm" /var/tmp/bench/run.sh bench sauerbraten base 1
    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=warm" /var/tmp/bench/run.sh bench luanti base 1
    PERF_LOAD=1 STALLS=1 ENVS="SHADERCACHE=cold SHADERCACHE=warm" /var/tmp/bench/run.sh bench luanti base 1

Compare `lp_glsl`, `lp_shbe`, `lp_shcache`, `lp_wshader` and the `load` column of the `sccold` and
`scwarm` runs: the difference in `lp_glsl` (and in load time) is what Mesa's cache saves, `lp_shbe`
is what it can't. `shadercache_kb` in bench.txt (before and after the run) is the cache's size: a
cold run's "after" is the whole cache of that game. A warm run whose cache grew a lot was not warm
(the cache is keyed by the driver's build id: a new Mesa build makes every entry miss).

| Variable | Default | What |
|---|---|---|
| `PERF_LOAD=1` | off | perf record of the start-up span (ps3-bench.sh `BENCH_PERF_LOAD=1`); with `PERF=1` both records, each its own perf |
| `SHADERCACHE` | the user's cache | `cold`: an empty `MESA_SHADER_CACHE_DIR` (removed after the run), `warm`: one per game kept in `SHADERCACHE_ROOT` (`/var/tmp/ps3-bench/shadercache`), `off`: `MESA_SHADER_CACHE_DISABLE=true` (compiles, no cache reads or writes). The user's own cache is never touched |
| `LDSTATS=0` | on with `PERF_LOAD` | no `LD_DEBUG=statistics` |
| `PERF_LOAD_WAIT` | 120 | seconds client.sh waits for perf to attach before it starts the game anyway |
| `LOADPROF_DISK_MBS` | 30 | loadprof.py: the disk's sequential read rate for the read-ahead estimate (USB 2.0) |

### What PERF_LOAD=1 records

- **From the exec.** client.sh starts the game through a gate: a bash that waits on a fifo and then
  execs the game command (same pid; perf's inherit follows whatever it forks). ps3-bench.sh's perf
  job finds the gate's pid in `$D/.launch.pid` and attaches (`perf record -e cpu-clock -F 499 -g
  -k mono -N --buildid-mmap -D -1 --control fifo:... -p <gate>,<Xorg>,<kswapd0> -o perf-load.data`).
  client.sh writes `enable` into the control fifo and waits for the ack (perf has the processes),
  then takes the first counters, starts its clock (`t0`) and opens the gate. perf's own start-up
  is outside the timed load. Attached before the exec, perf gets the exec's comm and every mmap of
  the load, dlopened libraries included (no `perf.track` trick needed).
- **To the window's start.** `disable` where the measured window begins: load + WARMUP, 0.9 x
  WARMUP for games that repeat a demo (`PASSES=1`), the first `WIN_ON_RE` line for games that mark
  their own window (Sauerbraten). bench.txt: `perf_load_win` (on, off: seconds from t0),
  `perf_load_ack`, `perf_load_cpu` (perf's own CPU over the span), `mono_t0` (`/proc/uptime` read
  next to t0: perf's clock, CLOCK_MONOTONIC, equals CLOCK_BOOTTIME on a PS3 that never suspends;
  10 ms resolution), `game_pid`.
- **The counters** (`load-io/`, with or without perf): `/proc/diskstats` and `/proc/vmstat` at the
  gate (`start`), at the load's end (`load`) and at the window's start (`warm`); the game's
  `/proc/<pid>/stat`, `io`, `status` and its main thread's stat at the last two (copied with bash's
  builtins: no fork while the game runs); `timebase` from `/proc/cpuinfo`.
- **ld.so's statistics**: `LD_DEBUG=statistics LD_DEBUG_OUTPUT=$D/ld/stats` in the game's
  environment, one file per pid (`ld/stats.<pid>`). Every exec in the pid appends a start-up block
  (env, stdbuf, then the game); loadprof.py takes the game's (the last), and its exit block (`final
  number of relocations`, dlopen's included). The times are in timebase ticks on ppc64 (glibc's
  hp-timing reads the timebase: 79.8 MHz on the PS3). Only the start-up is timed; dlopen's
  relocations show in perf as group `link`.
- **The stall recorder** (`STALLS=1`): stalls.py now prints the warm-up (load end to the window's
  start, all of it) next to the load; report.py `warmup_disk_s`. The recorder finds the game
  through `.pid` (watch_game's poll, every 0.5 s), so its record starts up to a second after the
  exec: the first part of the dynamic linking has no main-thread states.

perf-post.sh (run.sh calls it after the run) writes `perf-load-report.txt` (the game process, dso,
sym), `perf-load-threads.txt`, `perf-load.folded` (the phase, `load` or `warmup`, as the outermost
frame: one flame graph shows both side by side), `perf-load-leaf.txt`, `perf-load-stacks.tsv`
(per phase, process, thread and stack: samples, period, every frame's symbol and DSO), merges the
build ids and packages into `perf-buildids.txt`/`perf-pkgs.txt`, and prints loadprof.py's tables.
`perf-load.data` stays on the PS3 unless `make bench-pull PERFDATA=1`.

### loadprof.py

`python3 loadprof.py <run dir>...` or `report.py --load`. Per phase (load, warm-up): the wall time,
the main thread's and all threads' CPU, Xorg's and kswapd0's, the main thread's disk wait
(stalls.rec: disk, io and swap-in seconds), MB read from the disks and the disk's busy seconds,
major faults, swap-ins; then the CPU by group with each group's top functions, by the leaf's DSO
class, the busiest threads; ld.so's statistics; and the levers.

A sample's group is the innermost frame of its stack that matches a rule: first the umbrella rules
over every user frame (what runs under them is theirs: a page fault under `_dl_relocate_object` is
linking, zstd under the shader cache's writer is the cache), then for a kernel leaf the kernel's
paths, then the leaf rules over the user frames from the leaf outwards (memcpy under
`_mesa_texstore` is a texture upload), else the leaf's DSO class.

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
threads' seconds. The main thread is the critical path of a load: the group's main-thread load
seconds are the most its removal can take off the load time, half of them if it ran twice as fast
(work on other threads only counts where the main thread waits for it). For the disk: the main
thread's wait against the same MB read sequentially at `LOADPROF_DISK_MBS`: the difference is what
reading ahead in big blocks could win, the whole wait what having the files in RAM wins (the game
prefetch's job). report.py's columns: `lp_cpu`, `lp_main` (the load's CPU, all threads and the main
thread), `lp_wcpu` (the warm-up's), `lp_link`, `lp_glsl`, `lp_shbe`, `lp_shcache`, `lp_tex`,
`lp_mip`, `lp_dxt`, `lp_decomp` (all threads, both phases), `lp_wshader` (the three shader groups
in the warm-up alone), `lp_mb`, `lp_wmb`, `lp_majflt`, `lp_ld`, `lp_disk`.

### What Mesa's shader cache covers on nv30 (from the source, Mesa 26.2.4 with our patches)

- nv30 has a disk cache: `nouveau_screen_init()` (common to nv30, nv50, nvc0) creates
  `screen->disk_shader_cache` (keyed by the driver binary's build id, `nouveau_screen.c:177-198`)
  and hooks `get_disk_shader_cache`, so the state tracker's cache is on (`st_context.c:795`). Only
  nvc0 also stores its own machine code in it (`nvc0_program.c:576-665`); nv30 never touches it.
- What the GL layer caches: the program's metadata and each stage's serialized NIR after GLSL
  link and `st_link_glsl_to_nir` (`st_shader_cache.c:110`, `shader_cache.cpp:84`). A hit skips the
  GLSL compile and link (LINKING_SKIPPED) and deserializes the NIR. ARB and fixed-function programs
  go the same way.
- What runs anyway, at every start: the variant (`st_create_fp_variant`, `st_finalize_nir`), nv30's
  `nir_to_tgsi` (`nv30_fragprog.c:145`, `nv30_vertprog.c:231`) and the nvfx translation to machine
  code, lazily at the first validate (`_nvfx_fragprog_translate`, `nvfx_fragprog.c:1072`;
  `_nvfx_vertprog_translate`, `nvfx_vertprog.c:956`, again whenever the bound fragment program's
  texcoord mapping or the enabled clip planes change). That is group `shader.backend`.
- An nv30-side cache of the final code would wrap those two translate calls: key
  `disk_cache_compute_key()` over the TGSI tokens, the stage, nv40-or-not, `NV40_FP_EXPORT32` and,
  for vertex programs, `texcoord[10]` and the enabled clip planes; store the fragment program's
  `insn[]` with the constants unpatched (they are patched in at validate) and its control words, the
  vertex program's unrelocated `insns[]` with its branch and constant relocations. `disk_cache_get`
  / `disk_cache_put` as nvc0 does. Size: instructions are 16 bytes, programs a few tens of them:
  0.5-2 KiB a stage, about 60-250 KiB for OpenArena opengl2's ~55 programs (an estimate: a cold
  run's `shadercache_kb` gives the NIR cache's real size, which is larger). Whether it pays: the
  warm runs' `lp_shbe` says; build it only if that is a real share of the load.
- Environment, all honoured in this version: `MESA_SHADER_CACHE_DIR`, `MESA_SHADER_CACHE_DISABLE`,
  `MESA_SHADER_CACHE_MAX_SIZE` (default 1 GiB), `MESA_SHADER_CACHE_SHOW_STATS`. The default backend
  is the multi-file one, `mesa_shader_cache/` in `$XDG_CACHE_HOME` or `~/.cache`
  (`mesa_shader_cache_sf`, `_db` only with `MESA_DISK_CACHE_SINGLE_FILE`/`_DATABASE`).
  `SHADERCACHE=` uses `MESA_SHADER_CACHE_DIR`, so no cache is moved aside.

### nv30's shader cache (Mesa 0039, `NV30_SHADER_CACHE`, 2026-10-18)

The cache sketched above, built (`mesa/patches/0039-nv30-compiled-programs-in-the-disk-shader-cache-NV30.patch`,
`src/gallium/drivers/nouveau/nv30/nv30_shader_cache.c`), off by default. With
`NV30_SHADER_CACHE=on` both halves of `shader.backend` that nv30 owns go into the screen's disk
cache (the same `MESA_SHADER_CACHE_DIR` as the NIR, so `SHADERCACHE=cold|warm|off` covers it):

| Entry | Key | Value |
|---|---|---|
| TGSI | the serialized NIR handed to `create_fs_state`/`create_vs_state`, the stage | the tokens `nir_to_tgsi` made (a hit frees the NIR, as `nir_to_tgsi` would) |
| fragment program | the TGSI tokens, the 3D class, `NV40_FP_EXPORT32` | the code as translated (constants unpatched: validate patches them), constant slots, texcoord mapping, FP_CONTROL, coord conventions, RT_ENABLE, texcoords, vp_or, point sprite control |
| vertex program | the TGSI tokens, the class, the bound fragment program's texcoord mapping, the enabled clip planes (the translation's variant key) | the code before relocation, constants and immediates, branch and constant relocations, input/output masks |

Invalidation: nouveau's cache is created with the driver binary's build id and the chipset name
(`nouveau_screen.c` `nouveau_disk_cache_create`), and `disk_cache_compute_key` mixes those into
every key, so a new Mesa build (any patch, any compiler change) misses every old entry; entries
carry a magic and their sizes and are dropped if they don't parse. Only a program in its initial
state is looked up (a failed translation leaves a partial one, which is translated as before). Not
cached: the state tracker's variant work (`st_create_fp_variant`, `st_finalize_nir`) and the
NIR serialization the TGSI key needs (cheaper than `nir_to_tgsi`'s passes, but not free: the
`shc_hit_ms` column says how much a hit costs).

`NV30_SHADER_CACHE_STATS=1`: a `nv30-shcache:` line at context destruction and at exit with hits, misses
and the ms of each for TGSI, fragment and vertex programs (report.py `shc_*`). With the cache off
every creation counts as a miss, so `shc_miss_ms` of an off run is what the cache could save at
most. Compile-tested only: nv30 code can't run on the host.

On the PS3, after deploying the Mesa debs (26.2.4-1+ps3.466~dev20261018175510): a new build misses everything, so one
priming run per game first (it fills the NIR and the nv30 entries of the warm cache), then the
A/B with the warm NIR cache in both arms:

    T=SHADERCACHE=warm+NV30_SHADER_CACHE_STATS=1
    PERF_LOAD=1 ENVS="$T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench openarena gl2min 1   # prime
    PERF_LOAD=1 STALLS=1 ENVS="$T $T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench openarena gl2min 2
    # the other GLSL games the same way, if OpenArena shows a gain
    PERF_LOAD=1 ENVS="$T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench sauerbraten base 1   # prime
    PERF_LOAD=1 STALLS=1 ENVS="$T $T+NV30_SHADER_CACHE=on" /var/tmp/bench/run.sh bench sauerbraten base 2
    python3 /var/tmp/bench/report.py --load /var/tmp/ps3-bench/<run dirs>

Read `load`, `lp_shbe` (expected down by about the off arm's `shc_miss_ms` less the on arm's
`shc_hit_ms`), `shc_tgsi_hit`/`_miss` (on: all hits after the prime; misses mean the NIR differs
between runs, which would be worth a look), `shc_vp_miss` (variants met for the first time),
`stalls_warm` (programs first used during the warm-up: their translation is a hitch the cache
removes), and `shadercache_kb` (the nv30 entries' size: an estimated 60-250 KiB for OpenArena
opengl2's programs). Check game.log for "failed to compile" lines and the webcam for wrong
rendering; a hit gives the bytes a translation gives, so any difference is a bug.

### libjpeg-turbo's SIMD on the Cell

Debian's `libjpeg62-turbo 1:3.1.3-4` for ppc64 is built with its AltiVec code: the library has
2273 AltiVec instructions (vperm, vmladduhm, lvx, ...) and no VSX ones, so it runs on the PPE, and
the `JSIMD_FORCEALTIVEC`/`JSIMD_FORCENONE` switches. The choice is made at run time from
`/proc/cpuinfo`: the PPE's line says "altivec supported", so the SIMD paths should be taken. Its
SIMD functions are static, so perf names them only with `libjpeg62-turbo-dbgsym` (loadprof.py then
counts the `*altivec*` share of the jpeg group); without it, an A/B with `JSIMD_FORCENONE=1` in the
game's environment shows what the SIMD saves. Which of the five games decode JPEG at all at start
the first runs' `jpeg` group says.

## Start-up profiles, batch 17 (2026-10-18)

Five games, one run each, `PERF_LOAD=1 STALLS=1`, cold page cache, prefetch off, kernel
6.12.111+ps3.433, Mesa 26.2.4 .466 (`build/tmp/batch17-runs.txt` lines 2-6), and three shader-cache
runs each of OpenArena gl2min and Sauerbraten (lines 7-12: prime, cold, warm). Everything below is read from
those run directories with loadprof.py and stalls.py (`build/tmp/b17/` has the raw tables).
**One run per game and arm**: the load's wall time moves by 1.5-2.8 s between identical runs (OpenArena
28.1, 30.9, 28.8, 28.4 s; Sauerbraten 19.8, 18.4, 17.4, 18.3 s), nearly all of it disk wait. Differences
under about 2 s of wall time are not resolved here; CPU seconds from perf are steadier.

Two "load" numbers exist and differ. bench.txt `load=` (report.py's `load` column) is the game's own
number (OpenArena: the map load, 10.2 s). `load_end` is what this section uses: the wall time from the
game's exec to the end of the loading screens (28.1 s). The warm-up is `load_end` until the window starts
(60 s of play for most; Sauerbraten's window opens on its own mark, so its "warm-up" is the whole 85 s up to
the demo). Disk wait is the main thread's stalls.rec `disk` + `io` seconds (what loadprof.py calls diskwait);
`fault` is the major faults that did not coincide with disk reads and is mostly the same wait
(readahead locks), so the true disk share is somewhat above the disk column.

### Where the time goes (measured)

| Game | Load wall (s) | Main thread in the load: CPU / disk+io wait / fault / sleep (s) | Disk in the load: MB, reads, busy s | Warm-up wall (s) | Main thread in the warm-up: CPU / disk+io wait (s) | Disk in the warm-up: MB, busy s | Stall frames in the warm-up (s, n) |
|---|---|---|---|---|---|---|---|
| Sauerbraten base | 19.8 | 4.1 / 8.2 / 2.1 / 2.5 | 95.7, 2520, 14.3 | 85.6 | 67.1 / 9.6 | 111, 18.0 | 21.1, 53 |
| OpenArena gl2min | 28.1 | 9.0 / 10.9 / 2.0 / 3.2 | 133.6, 3405, 17.1 | 54.3 | 33.2 / 11.1 | 131, 17.2 | 25.5, 30 |
| Luanti base | 43.1 | 11.5 / 16.5 / 4.3 / 3.8 (+3.4 on the GPU) | 181.4, 6540, 30.0 | 45.3 | 35.6 (recorder) / 0.6 | 19, 7.3 | 2.2, 8 |
| SuperTuxKart lean | 54.3 | 22.5 / 18.6 / 3.5 / 3.8 | 238.4, 6313, 30.8 | 60.2 | 49.4 / 5.7 | 67, 12.7 | 18.9, 24 (one frame of 10 s) |
| ClassiCube base | 13.9 | 2.9 / 4.8 / 1.0 / 2.4 (+1.1 GPU) | 65.2, 1682, 8.9 | 54.4 | 11.0 / 0.04 (35.9 s waiting on the GPU) | 3, 1.3 | 0.1, 1 |

(Sauerbraten's disk+io in the warm-up is 4.0 + 5.6 s; OpenArena 4.0 + 7.1 s; STK 5.5 + 0.2 s.) The rest of each
load is `unknown` (1.0-3.3 s), `runq` (0.5-1.8 s) and, in Sauerbraten, direct reclaim (0.8 s).

- **The disk is the largest single item in every load**: 34-41% of the wall time is disk+io wait in the four
  bigger games, 35% in ClassiCube, and with the faults 40-51%. The CPU share on the main thread is
  21-42%. The disk is busy 57-72% of the load, but only at 6-8 MB/s: the average request is 28-40 KB (Luanti
  28 KB, the rest 38-40 KB) against 27.8 MB/s measured sequentially. So the reads are small and the disk
  seeks between them; this is the case docs/GAMEOS-HDD.md measured at 20 ms per random read.
- **Page faults on mapped files are the biggest part of the disk wait in Luanti**: of its 17 s of disk
  wait about 13 s are major faults (the recorder has no file for a fault, so which mappings is not known;
  the binary and libraries are the candidates, since only `~/.cache/minetest/media/*` (2.7 s for two
  files) and libogg show up as named reads). Luanti has the most major faults (1420) and the most
  small reads (6540).
- **Files that cost named wait** (syscall reads and opens only, recorder's fd): OpenArena
  pak0.pk3 1.2 s, cgameppc64.so 0.85 s, pak4-textures 0.47, pak6-patch088 0.32 in the load; in its warm-up
  pak2-players-mature 2.1 s, pak6-patch088 1.3, pak2-players 1.2, pak0 1.1 (the demo's second pass
  loads the map and the players again: 131 MB read, the same as the load). Sauerbraten: cave.ogz 0.46 s in
  the load; in the warm-up md5anim/md5.cfg model files at about 1 s each. STK: tracks/abyss, a music file,
  thunder.ogg at 0.4 s each. ClassiCube: its audio zip 0.18 s. Every one of them is small; the wait is
  spread over hundreds of files and the page faults, not a few big files.
- **Memory pressure repeats the reads**: refaults 16.8k (OpenArena load), 31.3k (STK load), swap-ins 138-605 in
  the load and 511-958 in the warm-up, kswapd 2.3-4.2 s of CPU in every load but ClassiCube's (1.2 s),
  allocstalls 72-141. The warm-up of OpenArena and Sauerbraten reads as much as their load did
  (131 and 111 MB) because the pages were evicted again.
- **ld.so**: start-up linking is 0.03-0.15 s (SB 0.103, OA 0.030, Luanti 0.129, STK 0.152, CB 0.113), 2.8k-20k
  relocations; with lazy binding and dlopen the `link` group is 0.29-0.62 s of the load per game. At most 1-2%
  of a load. Prelinking cannot buy more than that.
- Kernel CPU by path is small (`k.read`, `k.open`, `k.fault` 0.1-0.6 s each). The big `k.other` is
  `__replay_soft_interrupts` (0.3-2.5 s): perf samples taken with interrupts soft-masked land there, so
  kernel time under irq-off sections is smeared into it and is not attributable to a path.

### Top CPU consumers per game (seconds, all threads / main thread, from the load's perf)

| Game | Load: biggest groups | What is in them | Warm-up: biggest groups |
|---|---|---|---|
| Sauerbraten | zlib 1.54, shader.glsl 1.11, k.other 0.80, link 0.46, game 0.30, libc 0.28, audio 0.27, shader.cache 0.21, shader.backend 0.20 | map .ogz inflate (libz without symbols 0.65, `inflate` 0.57, crc32 0.17), GLSL compile of its shaders (glsl front end, lexer, NIR) | mesa 42.3 (`nv30_query_result` 13.1 and `generic_run` 12.8: occlusion-query spin and the draw module's software vertex path, i.e. play, not start-up), game 7.7, kgpu 5.3, audio 5.2 (Vorbis, 1.9 on the main thread), shader.glsl 0.77 on first use |
| OpenArena | files 1.95, game 1.91, shader.glsl 1.37, mipmap 1.34, k.other 1.0, jpeg 0.36, libc 0.35, link 0.34, dxt 0.27, shader.backend 0.24 | libdeflate inflate of the pk3 members 0.96 + `fread` 0.35; `COM_ParseExt` 1.31 (the .shader scripts); `R_MipMapsRGB` 1.34 (the engine's own CPU mipmaps); GLSL compile of ~55 programs | game 9.2 (`R_RegisterMD3` 1.8, `COM_ParseExt` 1.6), other 6.4 (resampler, mixer), mesa 5.4, buffers 4.3, kgpu 4.0, mipmap 2.05, files 2.04 (inflate 1.8) |
| Luanti | game 11.0 (main 1.25; Emerge-0 mapgen 8.4 s on its own thread), k.other 3.8, libc 2.2, other 1.9, mesa 1.65, lua 1.07, link 0.57, cxx 0.53, shader.glsl 0.43, fonts 0.31, zstd 0.22 | mapgen (`spreadLight` 1.8, `valueMap3D` 1.6, biome noise), LuaJIT bytecode, FreeType hinting (`TT_RunIns`) | game 40.1 (Emerge-0 28 s, MeshUpdate 8.9, Server 6.8), libc 6.5, k.other 6.6, mesa 4.7, zstd 1.3 |
| SuperTuxKart | game 5.15, png 4.11, zlib 4.03, k.other 2.2, audio 2.16, jpeg 2.11, dxt 1.68, libc 1.26, link 0.59, k.read 0.63 | `CSkinnedMesh` joints and finalize; libpng (no symbols: 3.90) and libz (2.86 + inflate 0.70 + adler32 0.25); Vorbis (`ov_read_filter`, imdct); libjpeg-turbo (2.04, no symbols so AltiVec use is unconfirmed); Mesa `convert_ubyte` 1.22 (upload conversion, filed as dxt) | game 19.5 (Irrlicht driver, Bullet BVH walk), mesa 13.3, buffers 9.4 (`memcpy` 5.2, `vbo_get_minmax_index_mapped` 3.0), kgpu 1.9 |
| ClassiCube | game 1.66, mesa 0.54, k.other 0.48, link 0.29, libc 0.19, kgpu 0.19, zlib 0.15 (its own `Inflate_Process`), init 0.12 | `Builder_MakeChunk`; the OpenAL init constructors (`_GLOBAL__sub_I_bsinc_tables`, 0.08, also seen in OA, Luanti, STK at 0.07-0.08 s each) | mesa 8.75, game 3.5, kgpu 2.6, k.other 2.6 |

Decoders as a share of the load's main-thread CPU: STK png+zlib+jpeg+audio 12.4 s of 24.7 s (50%), Sauerbraten zlib+audio
1.6 s of 5.3 s, OpenArena inflate+jpeg+TGA+audio 2.6 s of 10.4 s, Luanti and ClassiCube almost none. OpenArena already
uses libdeflate for the pk3s. Luanti's main thread is not the load: its mapgen thread burns 8.4 s while the main thread
sleeps or waits on the GPU 3.4 s.

A discrepancy to keep in mind: for Luanti's warm-up the stall recorder counts 35.6 s of main-thread CPU, perf
16.6 s for the same thread. The load phase agrees closely (11.5 against 8.8), the Sauerbraten warm-up
exactly (67.1 against 67.9). Not resolved; the Luanti warm-up CPU split should not be quoted.

### The shader caches (measured)

`SHADERCACHE=warm|cold`, Mesa 26.2.4 with the nv30 cache (0039) not yet on the PS3, so the only cache is the
NIR one. Cache size 2488 KiB (OpenArena, 55 programs) and 2972 KiB (Sauerbraten) after one run.

| | load_end (s) | game's own load/GLSL phase (s) | lp_main (s) | lp_glsl | lp_shbe | lp_shcache | warm-up shader groups (s) | disk wait (s) | stall frames in the warm-up (s) |
|---|---|---|---|---|---|---|---|---|---|
| OpenArena prime | 30.95 | glsl1 2.15 | 10.62 | 1.40 | 0.27 | 0.18 | 0.06 | 11.5 | 29.3 |
| OpenArena cold | 28.76 | glsl1 2.11 | 10.52 | 1.44 | 0.28 | 0.18 | 0.06 | 10.9 | 26.8 |
| OpenArena warm | 28.36 | glsl1 1.41 | 9.28 | 0.18 | 0.29 | 0.10 | 0.06 | 11.4 | 29.2 |
| Sauerbraten prime | 18.40 | - | 5.04 | 1.79 | 0.39 | 0.40 | 1.11 | 7.2 | 19.7 |
| Sauerbraten cold | 17.37 | - | 5.06 | 1.86 | 0.49 | 0.43 | 1.23 | 6.6 | 19.0 |
| Sauerbraten warm | 18.32 | - | 4.26 | 0.42 | 0.39 | 0.22 | 0.39 | 7.5 | 15.0 |

(`lp_glsl`, `lp_shbe` and `lp_shcache` are all threads over load plus warm-up; `lp_main` is the load's main thread.)

- **What the cache does, measured**: `lp_glsl` falls 1.44 -> 0.18 s (OpenArena) and 1.86 -> 0.42 s
  (Sauerbraten), and the load's main-thread CPU 10.52 -> 9.28 s and 5.06 -> 4.26 s. The cache's own cost
  falls too (0.18 -> 0.10, 0.43 -> 0.22: a hit reads and inflates one blob instead of writing and
  compressing). Sauerbraten also compiles shaders on first use in its warm-up: 1.23 -> 0.39 s there.
- **Why the load did not get faster, as PROFILING.md predicted plus one cause it did not**:
  1. The CPU saved (1.2 and 0.8 s on the main thread) is smaller than the spread of the disk wait between
     identical runs (about 2.5 s). OpenArena's warm load is 0.4 s shorter than cold, Sauerbraten's 1.0 s
     longer, and Sauerbraten's disk wait is 0.9 s longer in that run (6.6 -> 7.5). Both are inside the noise.
  2. `lp_shbe` does not move (0.28 -> 0.29, 0.49 -> 0.39): the variants, `nir_to_tgsi` and nvfx still run at
     every start, 0.3-0.5 s over the whole span.
  3. Inferred, not measured: OpenArena's own timer for the shader loop is 1.41 s warm against 0.57 s of
     CPU in glsl+backend+cache over the whole span; the missing ~0.8 s fits the cache being 55-odd
     small files read from the cold page cache on the USB disk at ~15 ms a seek. Test: the recorder's states
     in that phase, or a warm run with the cache directory in the page cache.
- **Not a stall fix in the window**: OpenArena's warm-up stalls are 25-29 s in all three arms, and its
  shader groups in the warm-up are 0.06 s everywhere (it compiles everything at load). Sauerbraten's
  warm-up stalls go 19.0 -> 15.0 s warm, with 56 -> 51 stalls and the longest from 2.1 to 1.7 s, a hint of
  something (the first-use compiles are gone) but from one run each against a prime arm at 19.7.
- Sauerbraten's shader time in the load is 1.11 s on the main thread and 0.77 s more in the warm-up; OpenArena
  1.37 s in the load. Luanti 0.43, STK 0.03, ClassiCube 0.02 s: the cache is not a lever for the last three.
- **nv30's cache (0039) can save at most the `shader.backend` seconds**: load plus warm-up, all threads:
  OpenArena 0.28, Sauerbraten 0.39, Luanti 0.11, STK 0.11, ClassiCube 0.02 (main-thread load share 0.24,
  0.20, 0.08, 0.04, 0.02). That is an upper bound before the cost of a hit (`shc_hit_ms`) and below the run
  noise. The A/B is still worth running because it is cheap, but it is not where start-up time is.

### Ranked wins (separating measured from estimated)

The unit is main-thread wall seconds off the load (L) and the warm-up (W) of that run. All of these are
estimates built on the measured pools above; none has been run as an A/B except the GLSL cache.

| Rank | Win | Sauerbraten | OpenArena | Luanti | SuperTuxKart | ClassiCube | Basis |
|---|---|---|---|---|---|---|---|
| 1 | **Game data on the internal SSD** (docs/GAMEOS-HDD.md Phase A; 100 MB/s, 0.1 ms) | L 4-6, W 5-8 | L 6-8, W 6-9 | L 9-12, W 0.5 | L 10-13, W 3-4 | L 2.6-3.5 | 55-72% of disk+io wait. Not measured yet; GAMEOS-HDD.md's own table gives lower end values (e.g. Luanti 17-24% of a 41 s load = 7-10 s) |
| 2 | **Reading ahead in big blocks / game prefetch** (built, not in these runs; kernel `read_ahead_kb` and the pk3s' `readahead(2)` are the cheap version) | L 5.1 of 8.3 | L 6.5 of 11.0 | L 11.1 of 17.1 | L 11.2 of 19.1 | L 2.7 of 4.9 | loadprof's number: wait minus the same MB at 30 MB/s. Whole wait if the files are in RAM (the second number), which is the prefetch's ceiling. The 28-40 KB average request is the evidence |
| 3 | **Faster inflate / PNG / JPEG (zlib-ng, libpng, libjpeg-turbo with symbols to check AltiVec)** | L 0.8 (zlib 2x) | L 0 (already libdeflate) | ~0 | L 4-6 (2x on libz 4.0 + libpng 4.1; jpeg 1 if AltiVec is not used) | 0.1 | loadprof's 2x column: STK png 2.05, zlib 2.01, jpeg 1.06. libpng has no VSX path on the Cell, so 2x is a guess, not a measurement |
| 4 | **OpenArena pk3 repacked stored (no inflate) and mipmaps off the CPU** | - | L 1.0 + 1.3, W 1.8 + 2.0 | - | - | - | inflate 0.96 s (load), 1.82 s (warm-up); `R_MipMapsRGB` 1.34 and 2.0 s. More bytes to read, so only with rank 1 or 2 in place |
| 5 | **Audio decoded on first use or off the main thread** (Vorbis on the SPEs is in IDEAS.md) | W ~1.9 | ~0 | ~0 | L 2.2 | ~0 | `ov_read_filter` + imdct on the main thread |
| 6 | **Mesa's GLSL disk cache** (`MESA_SHADER_CACHE_*`, already on by default) | L 0.8, W 0.8 | L 1.2 | L 0.4 | 0 | 0 | measured as CPU, below the load noise; stays on |
| 7 | **nv30 backend cache (0039)** | <=0.2 + 0.2 | <=0.24 | <=0.08 | ~0 | ~0 | upper bound from `shader.backend` |
| 8 | **Less dynamic linking** (prelink, `-z now` instead of lazy, fewer DSOs) | <=0.2 | <=0.2 | <=0.3 | <=0.3 | <=0.15 | half of the `link` group |
| 9 | Game-level: Luanti mapgen/lighting threads, STK skinned mesh work (`skinJoint` 1.3 s), OpenArena `COM_ParseExt` (1.3 s, .shader scripts) | - | L ~1 | W (Emerge) | L 1-2 | - | outside system/library level |

Reading the ranking: ranks 1 and 2 are the same pool (the disk wait) and do not add. Together with game
mode's memory freeing (less refault and swap-in traffic: the 17-31k refaults) they decide the load time;
every CPU item together is 1-3 s per game except SuperTuxKart (5-8 s). After the disk is fixed, the loads become CPU
bound at roughly SB 15 s, OA 21 s, Luanti 33 s (a mapgen wait), STK 43 s, CB 11 s, which is where decoders and CPU mipmaps begin to matter.

Next measurements that would turn estimates into facts: GAMEDATA=ssd and GAMEPREFETCH A/B on these five
(same boot, three runs each, because of the 2.5 s load noise); `JSIMD_FORCENONE=1` on STK; libz/libpng
symbols (dbgsym) for the 2.9+3.9 s of `[unknown]`; the recorder's fd for page faults, to name the
Luanti mappings; the shader cache A/B with 0039 on, with `shc_*` columns.

## Overhead

Measured on the host (Zen 2), estimated for the PPE; every run states its own numbers.

- perf in the kernel: one hrtimer interrupt and a back-chain walk (8 bytes a frame read from the
  user stack) per sample, at 499 Hz for each running thread. Estimated 1-3% of the PPE for a
  CPU-bound game, charged to the game's threads, so it shows only as lower fps. perf's own reader:
  `perf_self` (0.25% of a Zen 2 thread for two busy processes in the container test). Data: about
  250-400 bytes a sample to the disk. Measure it once with a same-boot A/B (`PERF=0` against
  `PERF=1`, three rounds) before reading fps off profiled runs at all; `PERF_HZ=199` cuts it by 2.5.
  (2026-10-18: the profiling batch had OpenArena at 50.7, 50.5 and 52.7 fps profiled against 52.7 and
  50.2 without, inside the noise; docs/HOTPATHS.md. That is one game and no interleaved A/B, so the
  rule above still holds.)
- perf-post.sh: after the run, nothing during it (perf script and the collapser: a minute or two
  on the PPE).
- MEM: three snapshots. Reading smaps walks the game's page tables under its mmap lock: under 1 ms
  for an 18 MiB process on the host; `mem_smaps_ms` says what the PPE takes. A page fault or mmap of
  the game waits meanwhile, so a stall may coincide with a snapshot (`mem_snaps` gives their times).
  ps3-mallocstat.so: one sleeping thread, 64 KiB of stack, a few KiB of code; `malloc_info` takes
  each arena's lock in turn for a moment.

## The 2026-10-18 batch (docs/HOTPATHS.md): what went wrong, and the fixes

- `perf.folded` and `perf-leaf.txt` held only the first 1-621 samples of every run (perf script
  died of SIGPIPE on the missing addr2line, above), so report.py's `perf_*` columns were wrong;
  `perf-report.txt` and `perf-threads.txt` were complete. Fixed by `--no-inline`.
- perf recorded no mmap while disabled, so libGLX_mesa and libgallium were `[unknown]` in the PS3's
  report. Fixed for new records by the tracking step (ps3-gpu-tools 0.412); for this batch's
  records perfprof.py fills them from the MEM=1 smaps.
- The batch's perf.data re-processed on the host with the fixed perf-post.sh and the PS3's own perf
  7.2.9 under qemu-user (below) gives the PC reader's (`games/bench/hotpaths/`) numbers: every
  sample collapsed (OpenArena 57510), the class shares equal to the tenth (OpenArena game 23.2,
  mesa 27.3, drm 0.0, kgpu 4.7, kernel 10.8, libc 6.9, other 27.0; Sauerbraten 7.3, 72.0, 0.1, 3.6,
  6.2, 3.4, 7.5), the same leaf symbols with the same counts. To redo the batch's text files on the
  PS3: `make bench-push`, then `/var/tmp/bench/perf-post.sh /var/tmp/ps3-bench/<run dir>` for each
  (the debug symbols of the runs' versions installed, as for the runs).
- Runs without samples or with the wrong picture: Armagetron's base played back a 90 s recording
  that draws nothing after its end (black screen with the cursor; the recipe now plays live AI
  rounds, the recording is variant `playback`); OpenTTD's base has no GL, so no HUD frame ever
  started the window (the recipe now starts it at the title game's load, `-d sl=1`) and its survey
  prompt covered the title game (answered in its config); dsda-doom's window never started because
  its `LOAD_MARK` lines never print (fixed in 6a59892, after the batch: `Playing demo:`). In all
  three the result's perf line said `window never enabled` or the HUD stopped early.

### Re-processing a run on the PC

perf on the PC can parse the PS3's perf.data, but the PS3's own perf under qemu-user gives the
PS3's call chain handling as well. `games/bench/hotpaths/perfq.sh` runs it from a root
(`PERFQ_ROOT`, default `build/tmp/hotpaths-sym`):

    R=build/tmp/hotpaths-sym; mkdir -p $R
    tar -xf build/rootfs/base.tar -C $R ./usr/bin/perf ./usr/lib/powerpc64-linux-gnu ./usr/lib64 ./lib ./lib64 ./bin
    games/bench/hotpaths/symroot.sh     # Mesa and its dbgsym, the kernel's System.map, ps3rsx, libc6-dbg
    # the game's own binaries and dbgsym of the run's versions: unpack their debs into $R too
    PERFPOST_PERF=games/bench/hotpaths/perfq.sh PERFPROF_ROOT=$R games/bench/perf-post.sh <copy of the run dir>

Module symbols are not in System.map: `[drm]` frames stay unnamed there (still kgpu). The host has
no dpkg database of the PS3, so `perf-pkgs.txt` comes out empty: write the game's libraries into it
by hand (`path<TAB>package`) and run `games/bench/perfprof.py <dir>` again. Delete the root after.

## Getting the data to the PC

    make bench-pull                 # every run directory into build/bench, without perf.data
    make bench-pull PERFDATA=1      # with perf.data, perf-sys.data and perf-syms.tar
    games/bench/report.py --perf --mem build/bench/<run dir>...
    games/bench/perfprof.py build/bench/<run dir>
    flamegraph.pl build/bench/<run dir>/perf.folded > flame.svg   # or speedscope, inferno

The text files and `perf.folded` are symbolised on the PS3 and are all most analysis needs. For
`perf report`/`perf script` on the PC (a different question, other sort keys), record with
`PERF_SYMS=1` and pull with `PERFDATA=1`, then:

    cd build/bench/<run dir> && mkdir -p sym && tar -xf perf-syms.tar -C sym
    perf report -i perf.data --symfs sym --kallsyms perf-kallsyms --no-children --sort sym

perf reads ppc64's big-endian perf.data on x86 (it swaps), but the powerpc call chain check above
does not run there; the PS3's perf under qemu (above, `perfq.sh`) does it.

## Validation

`python3 games/bench/selftest.py`, sections 7-9:

- perfprof.py on a made-up `perf script` output (call chains: a game, its renderer library,
  libgallium, libdrm_nouveau, ps3rsx and drm in the kernel, a page fault, memcpy, OpenAL, an audio
  thread, an unknown sample, Xorg and kswapd0 alongside; and the one-line form of the system-wide
  record): the folded stacks and every report.py column as computed by hand. Then the gaps of the
  2026-10-18 batch: a library built here, mapped in a made-up smaps, its frames `[unknown]` in the
  perf script text: filled with its function names, the two kinds of unchecked link register entry
  dropped, a kernel sample's user address filled, an address in no mapping and another process's
  frames left alone, `.drm_ioctl` and `[drm]` as kgpu, a library of the `versions` package as game.
- client.sh with a fake game: a fake perf on the control fifos gets enable at load + WARMUP and
  disable at + MEASURE, each acknowledged, at the times bench.txt says; ps3-mallocstat.so (built with
  the host's cc) preloaded, the three snapshots at window + 1, middle and end, each with all its
  files and a malloc dump; the classes add up to smaps_rollup's Rss.
- memprof.py on a real C process that reports its own thread stacks and malloc arenas: all found.

Also checked in a Debian container with Debian's perf 7.2.9 (x86): the exact record command above
with `-D -1` and the control fifo (samples only between enable and disable, ack in 2.5 ms), and
perf-post.sh end to end. 2026-10-18: the load's mmaps in the same container (a program that
dlopens a library 4 s after perf attached: `[unknown]` with plain `-D -1` and with `enable
dummy:u`, the library's function by name with ps3-bench.sh's enable plus disable cpu-clock, none
of the samples before the window), and perf-post.sh on the batch's perf.data with the PS3's perf
under qemu (above). Not checked: perf on the PS3 itself (the user runs it).

### Load split with the game data on the SSD (batch ssd1, 2026-10-18)

The load's disk wait (the recorder's sda class) stayed at 4.8-5.5 s in Sauerbraten and 6.2-6.7 s in
OpenArena with `GAMEDATA=ssd`: only 16-50 MB of the load's 76-144 MB of sda reads are game data, the rest
(binary, libraries, Mesa, shader cache, Xorg) stays on USB. The load's CPU, fault and io shares did not
change either. Numbers and verdicts: docs/GAMEOS-HDD.md "Result, batch ssd1".
