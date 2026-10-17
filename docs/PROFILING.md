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
  `BENCH_PERF`): `make ps3-gpu-tools-deb`, then `make deploy` of it.
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
| `perf-threads.txt` | samples by pid, thread name and DSO: the game's threads, Xorg, kswapd0 |
| `perf.folded` | folded stacks, `thread;outer;...;leaf COUNT` (kernel frames end in `_[k]`, frames without a symbol are `[their DSO]`), for `flamegraph.pl`, `inferno-flamegraph` or speedscope |
| `perf-leaf.txt` | samples by process, DSO and symbol of the sampled instruction: report.py's columns |
| `perf-pkgs.txt` | the package of every DSO with samples (dpkg -S): tells the game's own libraries |
| `perf-buildids.txt` | the build ids of the DSOs with samples |
| `perf-sys-report.txt`, `perf-sys-leaf.txt` | `PERF_ALL=1`: the system-wide record by comm and DSO |
| `perf-syms.tar`, `perf-kallsyms` | `PERF_SYMS=1`: the DSOs with samples, their debug files and the ps3rsx module laid out as a root, and the kernel's symbols |
| `perf.data`, `perf-sys.data`, `perf.err`, `perf.targets`, `perf.exe` | the record, perf's messages, the pids recorded, the game's executable |

The collapser is `perfprof.py collapse` (no stackcollapse-perf.pl on the PS3; it reads `perf script
-F comm,pid,tid,time,period,ip,sym,dso`). 100k samples of 20 frames take 2.2 s on a Zen 2 host,
so about 30-45 s on the PPE; perf script itself takes longer.

### report.py's perf columns

Percent of the game process's samples in the window, by the DSO (and for the kernel the symbol) of
the sampled instruction, self time:

| Column | Class |
|---|---|
| `perf_game` | the game's executable and its own libraries (same package, same directory, `/usr/lib/<name>`) |
| `perf_mesa` | libgallium, `*_dri.so`, libGLX_mesa, libEGL_mesa, libgbm, libglapi, glvnd's libGL/libGLX/libGLdispatch |
| `perf_drm` | libdrm, libdrm_nouveau (pushbuf, fences, buffer objects) |
| `perf_kgpu` | the `[ps3rsx]` module, and `drm_*`, `dma_fence*`, `dma_resv*`, `ttm_*`, `nouveau*` in the kernel image |
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

## Overhead

Measured on the host (Zen 2), estimated for the PPE; every run states its own numbers.

- perf in the kernel: one hrtimer interrupt and a back-chain walk (8 bytes a frame read from the
  user stack) per sample, at 499 Hz for each running thread. Estimated 1-3% of the PPE for a
  CPU-bound game, charged to the game's threads, so it shows only as lower fps. perf's own reader:
  `perf_self` (0.25% of a Zen 2 thread for two busy processes in the container test). Data: about
  250-400 bytes a sample to the disk. Measure it once with a same-boot A/B (`PERF=0` against
  `PERF=1`, three rounds) before reading fps off profiled runs at all; `PERF_HZ=199` cuts it by 2.5.
- perf-post.sh: after the run, nothing during it (perf script and the collapser: a minute or two
  on the PPE).
- MEM: three snapshots. Reading smaps walks the game's page tables under its mmap lock: under 1 ms
  for an 18 MiB process on the host; `mem_smaps_ms` says what the PPE takes. A page fault or mmap of
  the game waits meanwhile, so a stall may coincide with a snapshot (`mem_snaps` gives their times).
  ps3-mallocstat.so: one sleeping thread, 64 KiB of stack, a few KiB of code; `malloc_info` takes
  each arena's lock in turn for a moment.

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

perf reads ppc64's big-endian perf.data on x86 (it swaps), but this path has not been tried yet; the
powerpc call chain check above does not run there.

## Validation

`python3 games/bench/selftest.py`, sections 7-9:

- perfprof.py on a made-up `perf script` output (call chains: a game, its renderer library,
  libgallium, libdrm_nouveau, ps3rsx and drm in the kernel, a page fault, memcpy, OpenAL, an audio
  thread, an unknown sample, Xorg and kswapd0 alongside; and the one-line form of the system-wide
  record): the folded stacks and every report.py column as computed by hand.
- client.sh with a fake game: a fake perf on the control fifos gets enable at load + WARMUP and
  disable at + MEASURE, each acknowledged, at the times bench.txt says; ps3-mallocstat.so (built with
  the host's cc) preloaded, the three snapshots at window + 1, middle and end, each with all its
  files and a malloc dump; the classes add up to smaps_rollup's Rss.
- memprof.py on a real C process that reports its own thread stacks and malloc arenas: all found.

Also checked in a Debian container with Debian's perf 7.2.9 (x86): the exact record command above
with `-D -1` and the control fifo (samples only between enable and disable, ack in 2.5 ms), and
perf-post.sh end to end. Not checked: perf on the PS3 itself (the user runs it).
