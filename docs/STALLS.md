# Stalls: what they are, why they happen, how we catch them

A stall is a frame that takes longer than 100 ms. The user ranks them above average fps: a game at
40 fps that never hitches plays better than one at 60 that freezes for a second now and then. This
doc covers how the bench counts stalls, the stall recorder that says what the game was waiting for,
what we learned with it (the big one: a kernel bug that lost the timer interrupt of an idle PPE
thread, fixed by `local/0009`), the game prefetch we tried against disk stalls, and the
interference detector that throws out runs someone disturbed. Profiles of where the CPU goes are in
[PROFILING.md](PROFILING.md); per-game numbers in [games/README.md](games/README.md). Terms such as
PPE, decrementer, steady fps and 1% low are in the [glossary](README.md#glossary).

## Counting stalls: report.py

`bench/report.py` reads the HUD's frame times of a run's measured window (60 s of warm-up
left out, then at least 120 s measured) and gives:

| Column | What |
|---|---|
| `stalls` | frames over 100 ms in the window |
| `stall_s` | the seconds those frames took |
| `longest` | the longest frame |
| `low1` | the 1% low: 1000 / the 99th percentile frame time |

The matrices in `bench/results/` have one file per number (`stalls.csv`, `longest_ms.csv`,
`low1.csv`; README.md there says how to read them).

### Gameplay-only columns

A demo that loops reloads its map or replay at every pass, and those reload frames are not play.
In OpenArena they were 10 of a run's 18 stall seconds. So report.py computes the numbers a second
time over the window less the reload segments: `stalls_play`, `stall_play_s`, `longest_play`,
`low1_play`, plus `win_play` (seconds of play), `reload_s` and `reload_n` (what was left out). The
whole-window columns stay as they were, for history. matrix.py writes `stalls_play.csv`,
`stall_play_s.csv`, `low1_play.csv` and `longest_play_ms.csv`, each created by the first run that has
the number. Only HUD frame times give these (not the per-second form).

A reload segment is found per pass (the `pass<N>` stamps in bench.txt) from the game's entry in
`report.RELOADS`. A frame that ends inside the segment belongs to the reload, and so does a frame
that starts inside it; the segment then ends with that frame. That second rule matters for
OpenArena's opengl2 renderer, whose load ends in one 8-16 s frame (the cgame load, `cl_preloadLate`
and `RB_ShowImages`) that outlasts "CL_InitCGame:" + 1 s by 4-5 s. Before the rule it counted as
play: gl2min's "16 s gameplay freeze" was that frame, and its `stall_play_s` went from 54.7 to
16.0 s once it was left out (opengl1 runs moved by 0-4 s; [MEMORY.md](MEMORY.md)).

| Game | Reload | Segment | Basis |
|---|---|---|---|
| OpenArena | map reload at every demo pass | pass start to 1 s after its first out.log line `CL_InitCGame:` (needs bench.txt `t0`) | measured on 20261017T2337 and T2345: 10.2-10.8 s of reload, 7.1-7.3 s of demo |
| Neverball | the replay restarts at every pass | pass start + 1 s | measured: the load frames (0.15-2 s) end 0.4 s after the pass line; a fixed time, so runs from before `t0` work |
| dsda-doom | level load after each timed pass | pass start + 1 s | a guess; check the stalls' seconds into each pass with `passsplit.py -v` |
| X-Moto | a new replay state per pass | pass start + 2 s | a guess, same |
| BananaBread | demoend plays the demo again | none: its `WIN_ON_RE`/`WIN_OFF_RE` window already leaves the reload out | play equals the old numbers |
| SuperTuxKart and the rest | one race or session | none | play equals the old numbers |

A run whose game needs the load line but has no `t0` (OpenArena before 2026-10-17 23:00) gets `-` in
the play columns rather than a wrong number. A recipe can set its own segment: `parse()` prints
`reload_re=`, `reload_after=` or `reload_fixed=` into bench.txt, which win over the table.
`passsplit.py` uses the same code (`report.reload_segments`) to split the recorder's stalls with
their causes; `--re` and `--after` override the load line. Tested in selftest.py section 1 (a
synthetic looped demo: OpenArena by load line, Neverball by fixed time, bench.txt overrides, a game
with none, a missing `t0`).

What the split changed (2026-10-17 evening, whole window -> play, `stall_s`): OpenArena base
41.4 -> 28.9 s and 34.9 -> 20.7 s without the recorder, 17.9 -> 7.1 s and 17.6 -> 7.3 s with it.
Neverball base/gloff: 17.2 -> 14.0, 19.6 -> 17.2, 25.6 -> 18.8, 22.7 -> 17.2 s, so Neverball's stalls
are mostly mid-replay, not the restart.

### The first minute

The window leaves out the warm-up, where late loads and first-use shader compiles happen. report.py
covers it separately: `stalls_warm`, `stall_warm_s` and `longest_warm` over the WARMUP seconds after
`load_end`. With a stall record it adds `load_disk_s` (all of the load's seconds waiting on the disk,
not only stall frames) and `warm_disk_s` (the warm-up's stall frames' disk seconds); stalls.py prints
the warm-up's causes.

## The stall recorder

`ps3-stallrec` (`packages/walnut/tools/ps3-gpu-tools/src/ps3-stallrec.c`, installed in `/usr/lib/ps3/bin`) samples the
game's main thread and the system's memory and disk counters every 10 ms. `stalls.py` then gives
every stall frame a cause: disk, swap, reclaim, GPU, CPU and so on.

### Running it

On the PS3, with `ps3-gpu-tools` installed and `make bench-push` done:

    STALLS=1 /var/tmp/bench/run.sh smoke openarena          # prints the stall tables at the end
    STALLS=1 /var/tmp/bench/run.sh bench openarena "base tc1" 3
    python3 /var/tmp/bench/stalls.py /var/tmp/ps3-bench/<run dir>...   # the tables for any run
    python3 /var/tmp/bench/report.py [--stalls] <run dir>...          # stall_*_s columns
    python3 /var/tmp/bench/passsplit.py [-v] <run dir>...  # looped demos: the reloads' stalls apart (*_play columns)

`STALLS=1` sets `BENCH_STALLS=1` for `ps3-bench.sh`. `BENCH_STALLS_MS` changes the period (default
10 ms; 20 halves the cost), `BENCH_STALLS_CPU=LIST` pins the recorder (taskset) and
`BENCH_STALLS_NOWRITE=1` samples without writing the record. On the PC, after `make bench-pull`, the
same commands work on `build/bench/...`. The record is `stalls.rec` in the run directory, about 30
bytes a sample (about 1 MB for a 5-minute run, kept in /dev/shm while the game runs); `stallrec.err`
holds its errors. Off by default.

### What it records

It is one root process, started by `ps3-bench.sh` next to its 0.5 s sampler and stopped by the same
stop file. It finds the game through client.sh's `$BENCH_DIR/.pid` and the frames through the HUD's
dump file. Every period, with a CLOCK_MONOTONIC stamp:

| What | Source | Rate |
|---|---|---|
| main thread state, wchan, syscall (number, and the target of its fd: `/dev/dri/...`, a socket, a file) | `/proc/<pid>/task/<tid>/stat`, `wchan`, `syscall` | every sample; wchan and syscall only when not running |
| main thread major/minor faults, user/sys ticks | `task/<tid>/stat` | every sample |
| process major/minor faults, CPU time (exact, ns) | `/proc/<pid>/stat`, the process CPU clock | every sample |
| frames written by the HUD | new lines in `hud/frametime_(ms)` | every sample |
| pswpin/out, pgmajfault, allocstall, pgscan/pgsteal direct and kswapd, compact_*, pgmigrate, workingset_refault/activate/restore, cma_alloc_*, ... | `/proc/vmstat` deltas | every sample |
| reads, sectors, writes, io ticks, in flight of sda, zram0, ps3vswap0, the VRAM swap's partition, and the disks under the root and the VRAM swap's backing | `/sys/class/block/*/stat` | every sample |
| Xorg and kswapd0: state, major faults, CPU ticks, wchan in D | their `/proc/<pid>/stat` | every sample |
| VRAM swap reads and writes, VRAM against disk, written back | `/sys/block/ps3vswap0/stats` (takes the driver's lock) | every 10th sample |
| CPU busy/idle/iowait, free pages | `/proc/stat`, `/proc/vmstat` | every 10th sample |
| the main thread's kernel stack | `task/<tid>/stack` | once blocked 50 ms (D, or asleep outside futex/poll/sleep), every 250 ms of the episode, 1000 at most |

The extra disks (commit 13734bb, ps3-gpu-tools 0.477~dev) come from `lsblk` over the root's source
and `/sys/block/ps3vswap0/backing`: on the SSD root that is `ps3db` under `/dev/mapper/ps3root` and
`ps3swap`. Without them a load's faults on the SSD root counted as `fault` instead of `disk`.

The per-thread CPU split is tick-sampled (TICK_CPU_ACCOUNTING, HZ 250, shown in USER_HZ); the process
CPU time is exact. ps3rsx's GART counters in `ps3rsx_mm` are not read: that debugfs file takes
`mm_lock` and walks every buffer, and ps3-bench.sh's 0.5 s samples have them already.

**Time alignment.** The HUD dump is line-buffered (`_IOLBF` in hud_context.c), so a frame's line
appears when the frame ends. Counting new lines every sample places each frame on the recorder's
clock to within one period. stalls.py picks the stall frames exactly as report.py does
(`report.frame_ends`, the window from bench.txt) and takes their timing from the recorder. client.sh
writes `t0` (its start, `$EPOCHREALTIME`) into bench.txt, and the recorder writes a
monotonic/realtime pair at its start and every 10 s; stalls.py prints how far the two alignments
agree ("t0 says +0.001 s"). The load (client start to `load_end`) is placed through the frames, or
through `t0` when there are none.

**Side finding.** `/proc/stat` says both CPUs are 99% busy in every recorded run, also during disk
waits and after the game has exited. With TICK_CPU_ACCOUNTING the tick that wakes an idle PPE thread
is replayed inside `arch_local_irq_restore()`'s `irq_enter()`, so `timer_interrupt()`'s own
`irq_enter()` nests and `account_process_tick()` books the tick as system time. The record's `cpu=`
busy/idle means nothing on this kernel. No cause uses it (`runq` uses the process's CPU time).

### How a cause is decided

Each sample gets one cause, from the main thread's state and the deltas since the previous sample.
A stall's seconds are split over the samples that cover it (they add up to `stall_s`), and the
stall's cause is the largest share. The rules, in order:

1. A major fault in the sample, or D state outside a syscall (a page fault): `swap_vswap` if
   ps3vswap0 read, `swap_zram` if zram0 read or pswpin moved, `disk` if a disk read (or had I/O in
   flight), else `fault`.
2. Running or in D, not in a GPU wait: `gart` if cma_alloc moved (ps3rsx growing the GART),
   `reclaim` if allocstall, pgscan_direct, pgsteal_direct or compact_stall moved.
3. Asleep in an ioctl on `/dev/dri/*`, or a wchan matching dma_fence, dma_resv, ps3rsx, drm_ or
   nouveau: `gpu` (Mesa's fence waits are GEM_CPU_PREP, wchan `dma_fence_default_wait`).
4. D state in read, pread64, readv, openat, statx, getdents64, fsync and the like: `io`.
5. Running: `runq` if the process got under a quarter of the sample's CPU time (runnable but not
   running: both PPE threads busy with something else), else `cpu`. Shader compiles land here.
6. Asleep (S): `wait` (futex: another thread; poll or recvmsg: X; nanosleep: a frame limiter).
7. Anything else, or no sample: `unknown`.

report.py groups them into columns: `stall_disk_s`, `stall_swap_s` (both swaps), `stall_reclaim_s`
(reclaim and gart), `stall_gpu_s`, `stall_cpu_s` (cpu and runq), `stall_io_s`, `stall_wait_s`,
`stall_other_s` (fault and unknown), and `stall_why` (the largest). matrix.py has
`stall_{disk,swap,reclaim,gpu,cpu}_s.csv`, each created by the first run that has the number.

Limits. The direct reclaim counters are global, so a reclaim by Xorg while the game runs its own code
also counts as `reclaim` (Xorg's state is in the evidence). A sample is the state at one instant: a
sample in R state may have spent part of its 10 ms waiting. `runq` uses the whole process's CPU
time, so a busy second thread hides it.

### What it costs, and why recorder runs are their own variant

On the PPE the recorder takes 5.6-6.4% of one hardware thread at 10 ms (row 55 of the results, and
the `stallrec: N samples, own CPU X s (Y% of a thread)` line of every run's `result`; batch ssd1's
runs read 5.7-6.4%). On a Zen 2 host it is 1.2% (about 70 us of work a sample plus the wakeup;
`/proc/vmstat` alone takes 37 us there, mostly summing per-CPU counters over 24 CPUs). stalls.py
prints the cost per sample of each part.

On the kernels before the idle fix the recorder also *removed* stalls: OpenArena's gameplay stall
time fell from 21-29 s to 7 s with it running (below, "The recorder removes stalls"). So report.py
shows recorder runs as their own variant (`base@rec10`), and a decision compares recorder runs only
with recorder runs. Nobody has yet run the same-boot `STALLS=0` against `STALLS=1` A/B on the fixed
kernel; until it shows no change in fps and stalls, the recorder stays off by default and the runs
a decision rests on are made for that purpose.

### Validation

`python3 bench/selftest.py`, sections 5 and 6:

- A hand-made `stalls.rec` with one 200 ms stall of each cause the PC can't produce (a fence wait,
  zram and VRAM swap-ins, direct reclaim, GART growth, runnable without CPU, disk and read waits, a
  futex): each classified right, frames aligned to the made-up 0.5 s offset, report.py's columns and
  matrix.py's file right.
- The real recorder, built with the host's cc, on a fake game that stalls 300 ms three ways: major
  faults on a 64 MB file dropped from the page cache (POSIX_FADV_DONTNEED, MADV_RANDOM, on a disk
  directory: /var/tmp), a busy loop, a futex wait. They come out as disk (4294 major faults, 4294
  disk reads), cpu (290 ms user) and wait (futex), with the frames and `t0` agreeing within 1 ms.

Kernel stacks need root, so the PC test never reads them; the PS3 runs do.

## Why frames stalled: the lost decrementer

The short version: on our kernel an idle PPE thread could miss its timer interrupt (the decrementer)
and sleep for up to 1.4 s. Any game waiting on a GPU fence from that thread froze for as long.
`packages/patched/kernel/linux/patches/local/0009-ps3-idle-no-pause-with-irq-pending.patch` fixes it, confirmed on the PS3
twice (kernels .419 and .433), and it is in every kernel since, including the release `.472`. The
rest of this section is how we found it, kept because the reasoning is not obvious and the upstream
draft (`docs/upstream/linux-ps3-idle.md`) points here.

### The recorder removes stalls

Same boot, 2026-10-18, OpenArena base, the timedemo looped, the recorder off and on in turn. All four
runs had the same ioquake3 (ps3.379), Mesa, kernel and packages (`versions`):

| | T2332 off | T2337 on | T2341 off | T2345 on |
|---|---|---|---|---|
| stall_play_s | 28.9 | 7.1 | 20.7 | 7.3 |
| low1_play | 21.1 | 27.2 | 24.6 | 28.5 |
| stall_s (window) | 41.4 | 17.9 | 34.9 | 17.6 |
| steady fps | 53.0 | 61.3 | 54.4 | 61.4 |
| smooth fps (frames under 100 ms) | 63.2 | 62.7 | 63.3 | 62.5 |
| client CPU per HUD frame | 18.9 ms | 18.5 ms | 18.7 ms | 18.6 ms |
| wall time per HUD frame | 26.6 ms | 22.5 ms | 25.8 ms | 22.9 ms |
| frames over 1 s / over 0.5 s in play | 6 / 13 | 0 / 2 | 3 / 11 | 0 / 2 |

A frame that doesn't stall costs the same either way (smooth fps, CPU per frame). The off runs lose
3-4 ms a frame off the CPU: the game waits, it isn't slower. Their extra stalls are long (4382,
4138, 4046, 3231, 3030 ms ...) and fall on random frames of a pass (97, 291, 356, 465, 890, 1200),
not on the bot joins (frames 348, 948) or the reloads, which both kinds of run have. Swap was quiet
during them (no pswpin/pswpout in ps3-bench.sh's 0.5 s samples, 55 MB available). Every run of
2026-10-17 without the recorder had frames over 2 s after its first 400 frames (OpenArena 1-6 a run,
Neverball 1-4, Luanti 1-3).

The recorder itself went late the same way. It sleeps to an absolute 10 ms deadline at nice -10 and
still missed by up to 879 ms (13 gaps over 30 ms in each run). The longest gaps (T2337: 245, 879,
376, 604 ms; T2345: 464, 165, 592 ms) start with the game's main thread asleep in
`dma_fence_default_wait` (an ioctl on /dev/dri/card0) and end with a frame of the same length: two
waits that end by a timer, the recorder's sleep and ps3rsx's fence poll, stopped and resumed
together. ps3rsx signals fences from an hrtimer (exec.c `fence_poll`, every 250 us while someone
waits), and an hrtimer fires on the CPU that started it, here the waiter's.

### The mechanism

From the kernel source. The idle loop calls `ps3_power_save()`
(arch/powerpc/platforms/ps3/setup.c) soft-disabled, MSR[EE] still on, and it called `lv1_pause(0)`
without `prep_irq_for_idle()`, unlike the other powerpc idle states. A decrementer that fires
between the idle loop's `local_irq_disable()` and the hcall is masked: `masked_interrupt`
(exceptions-64s.S) sets DEC to 0x7fffffff, 27 s at the 79.8 MHz timebase, and leaves PACA_IRQ_DEC
for the replay. The thread then pauses with no decrementer to wake it, and every timer of that thread
waits for the next external interrupt: the tick, hrtimers (the fence poll, nanosleep), timer-wheel
timeouts.

`ps3_virq_setup()` binds every PS3_BINDING_CPU_ANY interrupt to thread 0 (USB, gelic, the RSX's
vsync, the SPEs), so thread 0 recovers within milliseconds. Thread 1 gets nothing but IPIs and sleeps
until something is woken onto it. The window is short, but a fence wait enters idle 4000 times a
second (the 250 us poll), and each entry is a chance for the 250 Hz tick to land in it. The periodic
tick (HZ_PERIODIC, no NO_HZ, no cpuidle) can't help: it *is* that decrementer.

Why the recorder helped: 100 wakeups a second at nice -10, 0.5 ms of work each. While it runs on the
awake thread, other wakeups (and its own, when its timer fires on a busy thread) get placed on the
stuck thread, which looks idle. That takes an IPI, the thread wakes, replays its decrementer, and the
fence poll runs. The outage shrinks from seconds to about a period. It stays when the recorder's own
timer is on the stuck thread: the 879 ms gap.

Ruled out:

- ps3-bench.sh itself: `BENCH_STALLS=1` only starts the recorder (sudo, nice -10, no affinity,
  cgroup or mlock) and waits up to 3 s for it after the run. Quiet, caches, swaps and timers take the
  same path either way.
- The second PPE thread kept busy (SMT sharing, the idle thread's priority): that would change the
  cost of a frame, and smooth fps and CPU per frame are the same.
- NO_HZ: not configured.
- The recorder's reads (/proc/<tid>/stat, wchan, syscall, stack, /proc/vmstat, diskstats, the VRAM
  swap's stats): none sends an IPI or touches the game's memory. Its /dev/shm writes are 3 KB a
  second.

### The A/B on the PS3

ps3-gpu-tools 0.399 added `ps3-waker` (`packages/walnut/tools/ps3-gpu-tools/src/ps3-waker.c`) and bench switches for it
(ps3-bench.sh's header; run.sh takes them without the `BENCH_` prefix, and `ENVS` interleaves them
within each round). report.py shows each run's switches after the variant (`base@wksleep10`), from
bench.txt's `harness=`.

| Switch | What runs alongside | Expected if the decrementer is lost |
|---|---|---|
| `-` | nothing | stall_play_s 20-29 s, frames over 1 s |
| `STALLS=1` | the recorder | 7 s |
| `STALLS=1+STALLS_NOWRITE=1` | the recorder without its output | 7 s |
| `WAKER=10` | a bare 10 ms sleep loop, unpinned, nice -10 | about 7 s (wakeups alone, no reads) |
| `WAKER=10+WAKER_CPU=0`, `WAKER=10+WAKER_CPU=1` | the same pinned to one thread | as off: a pinned sleeper wakes only its own thread, by the timer that is lost |
| `WAKER=10+WAKER_MODE=kick` | an IPI to each thread every 10 ms | 7 s or less |
| `WAKER=10+WAKER_MODE=kick+WAKER_CPU=1` / `...CPU=0` | an IPI to thread 1 / thread 0 only | thread 1: as kick; thread 0: as off |
| `WAKER=1`, `WAKER=50` | other periods | 1 ms: as 10; 50 ms: between |
| `PROBE=1` | pinned SCHED_FIFO sleepers timing each thread's timers (20 ms) | stalls as off; probe.txt has thread 1's wakeups late by 0.3-4 s at the long frames |

`probe.txt`'s `late` lines are CLOCK_MONOTONIC seconds; its `# clock` pair puts them on the client's
clock as stalls.py does (real = mono - mono0 + real0, then minus bench.txt's `t0`).

Commands (desktop session over, `make deploy PKGS=ps3-gpu-tools` and `make bench-push` done; each
batch has its own off runs):

    # 1. no game: does a pinned FIFO sleeper on an idle thread oversleep? (2 min each)
    ps3-bench.sh run probe-idle -- sudo ps3-waker -m probe -i 1.37 -t 120
    ps3-bench.sh run probe-kick -- sh -c 'sudo ps3-waker -m kick -i 10 -t 125 -o /dev/null &
        sudo ps3-waker -m probe -i 1.37 -t 120'
    ps3-bench.sh desktop
    # 2. the mechanism in a game (10 runs, about 45 min)
    ENVS="- STALLS=1 WAKER=10 WAKER=10+WAKER_MODE=kick PROBE=1" /var/tmp/bench/run.sh bench openarena base 2

A pinned FIFO thread late by 100 ms or more on an otherwise idle PS3 has hardly another explanation
than its timer not firing (a long stretch with interrupts off would show on both threads and in
dmesg's RCU or watchdog lines).

What they measured (2026-10-18, ps3-gpu-tools 0.399, kernel .178):

- Test 1 (runs ps3-bench-20261018T014733Z-probe-idle and T014942Z-probe-kick): with both threads
  idle, a SCHED_FIFO sleeper on thread 1 woke up to 1439 ms late (13 wakeups 100 ms or more late, 2
  over 1 s); thread 0 at most 16.7 ms. With the 10 ms kicker thread 1's worst was 19.4 ms, none over
  100 ms.
- Test 2 (results row 56, x2 interleaved, `stall_play_s`): plain 16.1 / 13.3 s (longest 2.8 / 1.8 s),
  recorder 7.7 / 8.1, bare sleeper 11.1 / 13.0, kick 6.5 / 6.2 (longest 0.45 / 0.42 s, `low1` 33.6 /
  33.2), probe 12.2 / 14.7. The kicker did as well as the recorder; the unpinned sleeper helped less
  than expected.

The finer split (the pinned wakers, kick to one thread, other periods, `STALLS_NOWRITE`) was planned
as a third batch and never run: the kernel fix made it moot.

### The fix

`local/0009` makes `ps3_power_save()` call `prep_irq_for_idle()` first, as pseries and the native
Cell's `cbe_power_save()` do. It hard-disables and returns without pausing when something was masked
since `local_irq_disable()` (the replay in `local_irq_enable()` then runs it); otherwise it marks the
thread soft-enabled, then `__hard_irq_enable()` and, unless `need_resched()`, `lv1_pause(0)` with
MSR[EE] on, as the hcall was entered before. From the check on, a decrementer or IPI is a normal
interrupt (it re-arms DEC) instead of a masked one.

A first draft paused with MSR[EE] off (`prep_irq_for_idle_irqsoff()`, powernv's way). Review dropped
it: that relies on `lv1_pause()` ending on a decrementer pending while MSR[EE] is off, and on the
hypervisor leaving that exception pending for the guest (the native Cell's wakeup handler has to
`set_dec(1)` because the wakeup consumes it). The only documentation is "MSR[EE] has no effect"; if
either part failed, every idle thread would lose its tick. With MSR[EE] on, the remaining race is an
interrupt handled between the hard-enable and the hcall that doesn't set need_resched; the pause then
ends at the next timer event, at most one 4 ms tick. Re-arming DEC before the pause instead would not
close the window: a decrementer masked after the re-arm is lost the same way.

The off switch, for same-boot A/B runs: `echo 0 | sudo tee /sys/module/kernel/parameters/ps3_idle_prep`
brings back the old pause at run time (`1` turns the fix on again); `ps3_idle_prep=0` on the command
line boots without it. The default is on.

### The fix on the PS3

Kernel 6.12.111+ps3.419, same boot, probe without a game (`ps3-waker -m probe -i 1.37 -t 120`, runs
ps3-bench-20261018T120716Z-probe-fixon and ...-probe-fixoff): with `ps3_idle_prep=Y` both threads'
worst wakeup was 3.6 / 4.0 ms (none 10 ms or later). With the switch at 0, thread 1 was up to 1103 ms
late (11 wakeups 100 ms or more, 1 over 1 s) and thread 0 16.7 ms. The fix does better than the 10 ms
kicker (19.4 ms worst).

In a game, two batches, no kicker and no recorder, OpenArena base, the switch alternated:

| Batch | Kernel | Results row | stall_play_s on / off | longest frame on / off | Notes |
|---|---|---|---|---|---|
| 2026-10-18 15:13-15:31 | .419 | 66 | 0.5, 0.1 / 8.2, 5.6 s | 285, 122 / 2598, 982 ms | steady 71.2 / 64.4 / 66.7 / 67.0; an agent's SSH wait loop polled the PS3 through all four runs, so they are in `results/interfered.txt` |
| batch 16, 2026-10-18 evening | .433 | 68 | 0.4, 0.4 / 9.0, 7.9 s | 0.22-0.38 / 2.2-2.8 s | clean (interference detector on) |

Watching for a side effect: had the pause been skipped every time, the idle threads would spin and
run hotter (nothing controls the fan's speed from Linux beyond SYSCON's automatic mode). No run has
compared idle temperatures with the fix on and off. Kernels .433 and later boot with it, and every
bench run since saves the System Manager's temperatures (`temps` in the run directory;
[THERMAL.md](THERMAL.md)).

**The kicker.** Before the fix, a stopgap went into game mode: `ps3-kick` (ps3-system,
`/usr/lib/ps3/sbin/ps3-kick`), a cross-CPU wakeup to thread 1 every 10 ms, started by
`ps3-game-mode on` as unit `ps3-game-kick` and stopped by thaw (`PS3_GAME_KICK=on|off|force`, in the
game's environment or `/etc/default/ps3-game-mode`; host test `packages/walnut/system/ps3-desktop/tests/kick-check.sh`). It
skips itself when `ps3_idle_prep` is `Y`, so on the fixed kernels it never runs unless forced. The
bench rows 57-65 and 67 were made with the kicker (`WAKER=10+WAKER_MODE=kick`, variant `@wkkick10`)
because their kernels predate the fix or were measured that way; compare them with each other, not
with later rows. A second stopgap, ps3rsx retiring fences from its 60 Hz vsync interrupt as well as from the
poll timer, was considered and is not needed with the fix.

## OpenArena

What stalls in OpenArena, and why.

The first recorder runs (ioquake3 ps3.379, the timedemo looped, 20261017T233701Z and T234549Z) put
81% of the window's stall time on the main thread's CPU. `passsplit.py` splits it by where it falls
in a pass:

- 10.2-10.8 s of the 17.6-17.9 s are the map reload at the start of every pass: three load-screen
  frames of 340, 700 and 380 ms, the world's textures read from pak4-textures.pk3, decoded and
  compressed to DXT on the PPE.
- 7.1-7.3 s are the demo itself: four stalls a pass of 130-400 ms, 6.9, 10.1, 13.5 and 16.7 s into
  it. Those are bots joining. The demo starts a match with bot_minplayers, which adds a bot every
  10 s of game time (every 200 timedemo frames), and OpenArena's qagame sends `loaddefered` with
  every bot added during play, so the cgame loads its models, skins and textures at once
  (cg_deferPlayers can't defer them). Real play does the same at every match with bots. A host trace
  (llvmpipe, the demo, three passes) has per pass 5 joins, 15 MD3 models, 15 skins, 35 image lookups
  and 23 texture uploads during the demo. Sounds load once per session (OpenAL keeps them across
  levels).

These runs had the recorder running, so they describe the stalls left once the lost decrementer was
masked. The reloads and the bot joins are real either way.

ioquake3 0025-0026 (+ps3.396, both on by default, host frames identical over three passes):

- `r_keepImages 1` keeps the textures of image files across a map change and takes them back by
  name when the next level is the same map (host reload 0.46 -> 0.17 s, the joins' uploads gone).
- `cl_preloadLate 1` registers what the cgame registered during the last level's play again at the
  end of the next load of the same map. From the second pass on nothing loads during the demo; the
  longest frame went 37 -> 14 ms on the host.

Both start working at the second pass, which the window never includes. `cl_deferPlayers 1`
(opt-in) drops `loaddefered`: the bots keep a substitute model, which changes the picture. Recipe
variants `keepimg0`, `preload0`, `loads0` (both off) and `defer` (`gamedb/games/openarena/benchmark/recipe.sh`).
On the PS3 (results row 59, TESTS.md 10-18 08:25, commit 2d3243c) gameplay stalls went 9-10 s -> 0
and steady fps 65 -> 70.6. OpenArena's current numbers (gl2min 57-60 fps with 1-2 s of stalls since
batch 17) are in [games/README.md](games/README.md).

## Game prefetch

`ps3-game-prefetch` (ps3-desktop; code in `files/libexec/ps3_game_prefetch.py`, its header has the
details) learns which files a game used and, at its next start, reads them back in one pass in disk
order while the game loads. It was built because the root disk then was a 2.5" hard disk on USB 2.0,
10-20 ms a random read, and the recorder put BananaBread's hitches and about a third of its 16.5 s
load on that disk (row 55). **Measured in batch 16: no gain, Luanti worse. `PS3_GAME_PREFETCH_GAME`
defaults to off** (ps3-desktop 0.474). The code stays, for a faster disk.

### How it works

**What is learnt.** For the first 180 s of a game (`PS3_GAME_PREFETCH_SAMPLE_S`) its processes are
looked at every 1 s, then every 3 s, then every 10 s: the files mapped or open, the working
directories, the game process itself (the largest RSS). When the game is over, a few more files are
added: every file under the game's data directories (`/usr/share/games/<name>`, `/usr/share/<name>`,
`~/.<name>`, `~/.local/share/<name>`, `~/.cache/<name>` such as Mesa's shader cache, the working
directory), which catches maps and textures read and closed again, plus the files of the old list.
Each file's resident pages (mincore) become ranges (holes under 128 KiB filled), and each range gets
its disk address (FIEMAP). The new list is the union with the old one; a file with no page in memory
for 3 sessions is dropped. The list is keyed by the game's executable (a script's path for an
interpreter), and its size and mtime stand for the package version: an upgraded binary makes the
list stale (not read, learnt anew). The command the game was started by is a symlink to the same
list. Lists live in `~/.cache/ps3-game-prefetch` (`PS3_GAME_PREFETCH_DIR`);
`ps3-game-prefetch game-list`, `game-show KEY` and `game-forget KEY` inspect and drop them.

**What is read.** The budget is min(`PS3_GAME_PREFETCH_MB`, default 64 MB; MemFree or MemAvailable
minus `PS3_GAME_PREFETCH_RESERVE_MB`, default 48), counted in pages not already in memory. It is
filled in the order the game first used its files (program and libraries first, then data in
first-seen order, files found only by the walk last), and the selection is then sorted by disk
address. Reads are 256 KiB `preadv`s with readahead off on our descriptors (POSIX_FADV_RANDOM), one in
flight, at I/O priority `PS3_GAME_PREFETCH_IO` (be7 by default; idle; be0-7) and nice 10, so a fault
of the game waits behind one of our reads at most (about 8 ms of transfer). The fetch stops early
when MemFree falls under the reserve, on direct reclaim or swapping (`/proc/vmstat` allocstall,
compact_stall, pswpin/out), after 60 s, or when the game exits. It logs one line: `game-fetch KEY:
read X MB from N files in T s (Y MB of the list in memory already, ...; budget B MB, L MB listed; R
reads, io be7)[, stopped: why]`.

**Game mode.** `PS3_GAME_PREFETCH_GAME`, from the game's own environment: `on` fetches and learns,
`fetch` only fetches, `learn` only learns, `off` (the default) does neither. Not with
`PS3_GAME_MODE=pads`. When it is on: once game mode has entered (after `ps3-game-mode on`, which
drops the unmapped page cache), ps3-game-guard starts `ps3-game-prefetch game-watch`. The watcher
claims each registered game, from ps3-game (key: the command after `ps3-game` in the launcher's
command line) or from ps3-game-detect (key: the detected process's executable), in a record
`$XDG_RUNTIME_DIR/ps3-game/gp/<id>.seen`. It starts the game's fetch as a process of its own and
looks at the game for its first minutes; once no game is left to look at, it exits, so nothing of it
stays in memory during play. The guard starts another watcher for a game registered later that no
record covers. At the end of `leave`, with the desktop back, `game-learn` turns every record into
its list. The program is a small stub that imports `ps3_game_prefetch.py` from the byte-code cache
postinst builds (`py3compile`, again from a python3 runtime.d hook when python3 changes). With that
cache the watcher costs 3.5 MB anonymous in Debian sid's python3 3.14, about the bare interpreter;
compiled at every start it would cost 8.8 MB (measured on the host).

**The bench harness.** Bench runs have no game mode, so `client.sh` runs the game through
`ps3-game-prefetch game-run` when `BENCH_GAMEPREFETCH` is set (run.sh `GAMEPREFETCH=`, an ENVS token):

- `fetch` (or `1`) reads the list a learn run left and learns nothing, so every measured run reads
  the same list. game-run forks a detached fetch and execs the game: same pid, same parent, nothing
  of Python left beside it.
- `learn` makes or grows the list and fetches nothing. Python stays as the game's parent (about
  4 MB), so these runs are not measured.
- `on` does both.

`GAMEPREFETCH_MB`, `_RESERVE_MB` and `_IO` set the fetch's knobs. Recipes with GAMEMODE=1 get
`PS3_GAME_PREFETCH_GAME=off` unless the token is given. Lists are kept in
`/var/tmp/ps3-bench/gprefetch`, apart from the desktop's. bench.txt gets `gprefetch=`, and report.py
shows it after the variant (`base@gpffetch`, `base@rec10+gpffetch_mb128`).

**Host tests.** `python3 packages/walnut/system/ps3-desktop/tests/gprefetch-check.py` (73 checks): the range and wrapper
logic and the data-directory rules; mincore after POSIX_FADV_DONTNEED, FIEMAP extents and addresses
(a sparse file too); learning over 5 made-up sessions (union, first-seen times, misses, a changed
file, an upgraded program, the command's symlink); the budget's residency-aware selection and its
order, exact reads, each early stop (with made-up /proc numbers); game-run end to end (learn, pages
dropped, fetch, pages back, exec keeping the pid, exit statuses). `packages/walnut/system/ps3-desktop/tests/autogm-check.sh`
(53 checks, 6 for the prefetch): a detected game's list learnt after leave, read by the next detected
start and by a ps3-game start through a second watcher, and `PS3_GAME_PREFETCH_GAME=off`. Under a
host load of 10-15, autogm-check failed one timing check in 3 of 6 runs, a different one each time;
the last 3 passed. `bench/selftest.py` covers client.sh with learn, then fetch, then a budget,
plus the warm-up columns.

### The prefetch A/B

Same boot, desktop stopped, the harness's quiet dropping the caches before every run so each starts
cold, base and prefetch runs interleaved, all with the recorder (`STALLS=1`) for `stall_disk_s` and
`load_disk_s` (the recorder changes the stalls it records, but for both sides alike):

    # 0. one learn run per game, cold, not measured
    for g in "sauerbraten base" "openarena gl2min" "luanti base" "supertuxkart lean"; do
        ENVS="GAMEPREFETCH=learn" /var/tmp/bench/run.sh bench $g 1; done
    /usr/lib/ps3/desktop/ps3-game-prefetch game-list --dir /var/tmp/ps3-bench/gprefetch
    # 1. base against prefetch, 3 rounds each
    STALLS=1 ENVS="- GAMEPREFETCH=fetch" /var/tmp/bench/run.sh bench bananabread base 3
    #    ... the same for "openarena gl2min", "luanti base", "supertuxkart lean"
    python3 /var/tmp/bench/report.py --stalls /var/tmp/ps3-bench/<the runs>

Batch 16 (2026-10-18 evening, kernel .433, USB root, results row 68, commit 296f99d), medians of 3:

| Game | load, base / fetch | stall_play_s, base / fetch | longest_play, base / fetch | steady fps, base / fetch |
|---|---|---|---|---|
| BananaBread base | 15.2-18.8 / 18.1-18.6 s | 1.4 / 1.0 s | 514 / 518 ms | 38.8 / 38.8 |
| OpenArena gl2min | about 0.5 s faster with fetch | 11.0 / 10.4 s | 2038 / 1782 ms | 51.8 / 53.1 |
| Luanti base | +1 to +4 s with fetch | 2.5 / 5.5 s | 358 / 3089 ms | 46 / 46 |
| SuperTuxKart lean | unchanged | 1.8 / 2.7 s | 686 / 682 ms | 107.3 / 106.7 |

BananaBread's load disk wait stayed at 4.5-5.5 s, and two Luanti prefetch runs had 3 s hitches in
play. On the USB hard disk the prefetch's sorted reads compete with the game's own reads. The
budget and I/O-priority step (step 2 of the plan) was not run, since step 1 showed no gain. The
desktop-side check (OpenArena started twice from the menu, `journalctl --user -u ps3-game-mode`
showing `nothing learnt yet`, then `game-learn`, then `game-fetch ... read`) has not been done.

## Interference

The user asked for runs disturbed by anyone to be invalidated automatically. Their child was in the
room on 2026-10-18, and on 2026-10-17 the user once hit Ctrl+Alt+F2 by accident in the middle of a
run. The user's rules since: trying to play the game instead of letting the demo play is
interference (no guessing at intent: any real input counts); so is running commands on the PS3
during a run (disk activity, swapped pages brought back). The mouse gets some tolerance: a phantom
movement or a bump is fine, deliberate motion and any button or wheel are not.

### The detector

`ps3-intfwatch` (`packages/walnut/tools/ps3-gpu-tools/src/ps3-intfwatch.c`, its header has every rule; ps3-gpu-tools
0.453 or later) runs as root next to every `ps3-bench.sh x` and `run`, on by default (`BENCH_INTF=0`
turns it off). It starts just before the command, so it sees the run's X server take vt1, and stops
with the stop file. What counts, each a `!` line in the run's `interference.log`:

- **Input** on every `/dev/input/event*` device, including devices connected mid-run (inotify on
  `/dev/input`): every key press (repeats and releases don't count; the key's code isn't logged, so
  the log is no key logger), controller and mouse buttons, wheel steps, a stick or trigger leaving
  its rest position by a quarter of its range (once per excursion; the DualShock 3's sticks jitter by
  a few units of 255 and stay quiet), any d-pad hat value. Mouse motion counts only when deliberate:
  one burst of 2 s or more, or 1500 counts within 10 s. A bump is recorded as a `-` line and doesn't
  count. The DS3's motion sensors (an accelerometer device) aren't watched. A DS3 already on before
  the run is watched like any other device; one switched on mid-run shows up as a new device.
- **Devices**: an input device appearing or going away, a kernel line of a USB device connected or
  disconnected (`/dev/kmsg`). A Bluetooth controller that drops its connection by itself counts too:
  the log can't tell that from someone switching it off.
- **VT switches** (`/sys/class/tty/tty0/active`, polled for POLLPRI): everything except the run's X
  server taking vt1 and giving the VT back after the game has exited.
- **Logins and commands**: a new logind session (inotify on `/run/systemd/sessions`; every SSH
  connection opens one, `ssh ps3 cmd` included), a new process in a login session that isn't the
  run's own, and a process of a login session that already existed and used 1 s of CPU or more
  during the run. The kernel has no proc connector (CONFIG_PROC_EVENTS is off), so `/proc` is
  scanned every 250 ms. Only the directory is read; a new pid's stat, cgroup, command line and, when
  needed, environment are read once. The run's own processes are ps3-bench.sh's descendants and
  process group, plus anything carrying `BENCH_DIR=<run dir>` in its environment (a daemon a game
  forks). The run's ancestors (run.sh, the SSH session that started it) belong to the harness, but
  what they start meanwhile doesn't.

Exempt, recorded as `-` lines: the bench's own evemu keyboards (uinput devices under
`/devices/virtual/input/` whose `/dev/uinput` is held by `evemu-device`; they carry the real
receiver's name "Logitech USB Receiver", so the name can't tell them apart), and
`tools/bench-cam.sh`'s watcher (its command line has `pgrep -x run.sh` and `/dev/shm/ps3-bench-`; it
and its session are exempt). XTEST (`xkeys.py`) makes no kernel input event at all, so nothing is
seen. System services' and the user manager's new processes are listed under `info`.

Not seen: a process outside a new session that lives less than one 250 ms scan. The PS3's power and
reset buttons aren't input devices: the System Manager sends init SIGINT (ctrl-alt-del), the reboot
ends the run, and its /dev/shm directory goes with it. Nothing before the detector's start (the
quiet phase) or after the stop is watched. The game window losing focus isn't checked.

Cost. On the host, 3 ms of CPU a second with 570 processes, 2 MB RSS. On the PS3 the log's
`# self cpu=` line reads 1.3-1.8 s of CPU per run of about 5 minutes (about 0.5% of one thread) and
3 MB maxrss (batch ssd1's runs).

### Results of a run

bench.txt gets `interfered=0|1` and `interference=<summary>` (for example "3 key presses, VT switch
1->2 at 85 s, 1 login(s): session 12 (tibi, sshd from 10.0.0.45) at 40 s"). The result file and the
terminal get a loud `!! INTERFERENCE: ... THIS RUN IS INVALID` line; a clean run's result says
`interference: none (4 input devices watched; ...)`. run.sh warns after such a run, and
`RERUN_INTERFERED=1` runs it once more right away. report.py has an `intf` column (1, 0, `-` for not
watched), marks the run `!INTERFERED`, leaves interfered runs out of the medians and lists them at the
end (`--include-interfered` keeps them in). matrix.py skips them and lists what it skipped
(`--include-interfered` too). Runs from before the detector are listed in
`bench/results/interfered.txt`, which report.py and matrix.py read the same way.

It has caught real disturbances since it went live with batch 15: 3 runs with SSH logins in batch 16,
a key press in batch 17 (BananaBread (Debian's cube2) shortnormals, round 3) and a key press 195 s into an OpenArena
run of batch ssd1 (230542Z). All were left out of their rows.

### The bench console

When the desktop stops, the TV used to keep the last frame: the user saw a frozen boot splash and
thought the PS3 had locked up. Now `quiet` writes a "BENCHMARK RUNNING" screen to tty8 (`BENCH_VT`,
0 turns it off) and switches to it. The screen says what runs (run.sh passes game and variant as
`BENCH_WHAT`), when it started, and that any input invalidates the run. tty8 has no getty (logind
starts autovt gettys on tty1-6 only). The run's X server (`xinit ... vt1`) switches to vt1 and gives
tty8 back when it exits, so the screen shows again between runs, and after an interfered run it says
so. `desktop` switches to vt1 and frees tty8. The detector expects both of the X server's switches
(`-x 1 -H 8`) and counts every other one. The kernel has only the 8x16 font, so the banner is drawn in
`#` blocks.

### The retro-scan of 2026-10-17/18

`bench/interference-scan.py` looks at what saved run directories still show: Xorg.0.log (VT
switches, input devices added or removed mid-run, the evemu keyboards left out), dmesg.log (USB
connects, non-virtual input devices, Bluetooth HID lines), and game.log for Luanti, which logs every
player action ("singleplayer digs/activates ..."). The benchmark's flight never digs or clicks, so
each such line is a mouse button pressed in the game. Run over the 339 pulled directories of
2026-10-17 and 18 (`--write` filled `results/interfered.txt`), it found six:

| Run | Evidence | Matrix row affected |
|---|---|---|
| 20261017T181626Z-supertuxkart-nohd | VT switch away 127 s after the X start, back 4 s later (the user's Ctrl+Alt+F2) | none (not in the matrices) |
| 20261017T224350Z-luanti-allbatchoff | 3 digs at 61 s, in the warm-up (stalls of 2.5 and 0.6 s at 59-62 s) | "Luanti ps3.364 batching ...": allbatchoff is the median of this run and one clean run |
| 20261018T081125Z-luanti-base | 1 activate (right click, empty hand) at 105 s, in the window | "batch 8 ...": luanti-base@wkkick10+perf |
| 20261018T093700Z-luanti-base | 4 activates at 142 s and 2 digs at 148 s, in the window; the run's longest frames right after the digs (698 ms at 147.6 s, 1943 ms at 150.1 s, 546 ms at 150.7 s) | "Luanti ps3.405 memory variants ...": base (its 6.5 s of gameplay stalls and 1943 ms longest frame) |
| 20261018T094136Z-luanti-lean | 72 actions: 11 digs in the warm-up, 30 digs and 31 activates in the window | the same row: lean |
| 20261018T094600Z-luanti-tradeoff | 3 activates at 109 s, in the window | the same row: tradeoff |

The lean run's webcam photos confirm it: at 60 s the dig crack and the raised hand are on screen and
the hotbar holds a block; at 150 s five blocks sit in the hotbar while the view points at the sky.
Clean runs have an empty hotbar and no hand. The clean re-runs (results row 67, commit 6880c07) then
settled the Luanti comparison: base 1.5 and 2.1 s of gameplay stalls, cliunload worse at 3.8 and
5.2 s, and lean draws a black void at 150 s in both clean runs (so lean does fail to draw the world;
batch 16 traced it to `chunk3`). The same photos show a solitaire game in progress on the PC's screen
at those minutes (12:39-12:44 local). If the mouse used there is also paired with the PS3's Logitech
receiver, the user's own mouse reaches the PS3; nobody has checked.

What saved files can't show: key presses, mouse motion and controller input that left no trace in a
game's log, the game window losing focus, and SSH logins or commands during a run. The journal isn't
saved per run. The PS3's kernel journal for 2026-10-18 has no input, USB or Bluetooth connect or
disconnect lines; its hci0 found/removed lines are mpris-proxy starting and stopping with each
desktop restart. So a run without evidence isn't proven clean. Four runs with no trace in their files
are known to be contaminated and are listed by hand: the OpenArena `ps3_idle_prep` on/off batch of
15:13-15:31 local on 2026-10-18 (20261018T121340Z, 121821Z, 122303Z, 122726Z; row 66, whose
observations carry the caveat). An agent's wait loop polled the PS3 over SSH every ~11 s through all
four, and SSH scans of the journal and Xorg logs ran in the last one. Batch 16 repeated them clean.

### Checking it on the PS3

Still to do. The detector has run with every bench run since batch 15 and the clean Luanti re-runs report
`interfered=0`, but the deliberate checks below need someone at the PS3 and have not been done
(PLAN 1.13). Needs ps3-gpu-tools 0.453 or later and `make bench-push`; nobody else using the PS3;
webMAN fan on SYSCON.

1. `run.sh smoke openarena`, then press one key on the PS3's keyboard about 30 s in. The run ends
   with `!! INTERFERENCE: 1 key presses ...`, bench.txt has `interfered=1`, and interference.log has
   a `! ... KEY dev="Logitech USB Receiver"` line with no key code. `report.py` on the directory
   shows `!INTERFERED` and `intf=1`.
2. The same run untouched: `interference: none (4 input devices watched; ...)` in the result, and
   only `-` lines in the log apart from the X server's two expected VT lines
   (`VT 8 -> 1 (the X server takes its VT)`, `VT 1 -> 8 (... after the game)`).
3. During a run, `ssh ps3 true` from the PC: counted as a login (session N, sshd from 10.0.0.45).
   With `tools/bench-cam.sh` running for the batch, its watcher shows only as `exempt` in the info
   line.
4. `run.sh smoke armagetronad` (the evemu keyboard): `DEVADD ... (the bench's virtual device ...)`
   as a `-` line, no interference from its key presses.
5. The TV shows the BENCHMARK RUNNING screen while the desktop is stopped and between runs, the game
   during the run, and the desktop after `ps3-bench.sh desktop` (webcam).
6. Move the mouse briefly: a `- ... MOUSE ... bump` line, not counted. Move it for 3 s: counted.

## What the kernel and the driver could add

The 6.12 kernel here has no PSI, no delay accounting (TASK_DELAY_ACCT/TASKSTATS), no SCHEDSTATS and no
TASK_IO_ACCOUNTING, so the recorder samples what those would measure exactly. Proposals for them, for
a debugfs `ps3rsx_stats` with lockless counters and for a lock-free vswap `stats` file are in
[backlog/smoothness.md](backlog/smoothness.md) (PLAN 3.11), as is the recorder's own A/B on the fixed
kernel.

## History

- 2026-10-17 evening: first recorder runs; the gameplay-only columns added after OpenArena's reloads
  turned out to be most of its stall time.
- 2026-10-18 ~02:40-03:20 (row 55, commit b9a4187): recorder off/on alternation shows it removes
  stalls; OpenArena's remaining stalls are CPU, BananaBread's disk.
- 2026-10-18 ~04:00 (row 56, commit fce0c5b): probe and wakers; the lost decrementer confirmed on the
  idle thread; the 10 ms kicker halves OpenArena's gameplay stalls and becomes the stopgap.
- 2026-10-18 15:13 (row 66, commit e59c09a): `local/0009` on .419, on/off in game (SSH-polled).
- 2026-10-18 evening, batch 16 (row 68, commit 296f99d): the clean on/off repeat on .433; the
  prefetch A/B shows no gain and its default goes to off.

Older versions and the dated history: [archive/STALLS.md](archive/STALLS.md).
