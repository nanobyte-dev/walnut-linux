# Why frames stall: the stall recorder

The user (2026-10-18): stutters matter more than fps. A lower frame rate is fine as long as the
game plays smoothly. report.py already counts the stalls (frames over 100 ms in the measured
window: `stalls`, `stall_s`, `longest`, `low1`), but not why they happen. The stall recorder says
which part of the system the game's main thread was waiting on during each one.

## Running it

On the PS3 (`ps3-gpu-tools` with `ps3-stallrec` installed, `make bench-push` done):

    STALLS=1 /var/tmp/bench/run.sh smoke openarena          # prints the stall tables at the end
    STALLS=1 /var/tmp/bench/run.sh bench openarena "base tc1" 3
    python3 /var/tmp/bench/stalls.py /var/tmp/ps3-bench/<run dir>...   # the tables for any run
    python3 /var/tmp/bench/report.py [--stalls] <run dir>...          # stall_*_s columns
    python3 /var/tmp/bench/passsplit.py [-v] <run dir>...  # looped demos: the reloads' stalls apart (*_play columns)

`STALLS=1` sets `BENCH_STALLS=1` for `ps3-bench.sh`. `BENCH_STALLS_MS` changes the period
(default 10 ms; 20 halves the cost). On the PC after `make bench-pull`, run the same commands on
`build/bench/...`. The record is `stalls.rec` in the run directory, about 30 bytes per sample (about
1 MB for a 5-minute run, in /dev/shm while the game runs). `stallrec.err` holds its errors.

It is off by default. On the host it costs 1.2% of one Zen 2 thread at 10 ms (about 70 us of work
per sample plus the wakeup; /proc/vmstat alone takes 37 us, mostly summing per-CPU counters over 24
CPUs, which the PS3's 2 CPUs make cheaper). Nothing has measured it on the PPE yet. Every run's
`result` file says what it cost there ("stallrec: N samples, own CPU X s (Y% of a thread)"), and
stalls.py prints the cost per sample of each part. Turn the default on once a same-boot A/B
(`STALLS=0` against `STALLS=1`) shows no change in fps and stalls. Until then, record stalls in
runs made for that purpose, not in the runs a decision is based on.

**The first A/B failed the other way** (2026-10-18, below: "The recorder removes stalls"): on the
PPE the recorder costs 5.6% of a thread and takes OpenArena's gameplay stall time from 21-29 s to
7 s. It doesn't see the stalls it prevents. report.py now shows recorder runs as their own variant
(`base@rec10`).

## What it records

`ps3-stallrec` (ps3-gpu-tools/src/ps3-stallrec.c, in `/usr/lib/ps3/bin`) is one root process,
started by `ps3-bench.sh` next to its 0.5 s sampler and stopped by the same stop file. It finds
the game through client.sh's `$BENCH_DIR/.pid` and the frames through the HUD's dump file. Every
period it records, with a CLOCK_MONOTONIC stamp:

| What | Source | Rate |
|---|---|---|
| main thread state, wchan, syscall (number, and the target of its fd: `/dev/dri/...`, a socket, a file) | `/proc/<pid>/task/<tid>/stat`, `wchan`, `syscall` | every sample, wchan and syscall only when not running |
| main thread major/minor faults, user/sys ticks | `task/<tid>/stat` | every sample |
| process major/minor faults, CPU time (exact, ns) | `/proc/<pid>/stat`, the process CPU clock | every sample |
| frames written by the HUD | new lines in `hud/frametime_(ms)` | every sample |
| pswpin/out, pgmajfault, allocstall, pgscan/pgsteal direct and kswapd, compact_*, pgmigrate, workingset_refault/activate/restore, cma_alloc_*, ... | `/proc/vmstat` deltas | every sample |
| reads, sectors, writes, io ticks, in flight of sda, zram0, ps3vswap0 and the VRAM swap's partition | `/sys/class/block/*/stat` | every sample |
| Xorg and kswapd0: state, major faults, CPU ticks, wchan in D | their `/proc/<pid>/stat` | every sample |
| VRAM swap reads and writes, VRAM against disk, written back | `/sys/block/ps3vswap0/stats` (takes the driver's lock) | every 10th sample |
| CPU busy/idle/iowait, free pages | `/proc/stat`, `/proc/vmstat` | every 10th sample |
| the main thread's kernel stack | `task/<tid>/stack` | once blocked 50 ms (D, or asleep outside futex/poll/sleep), every 250 ms of the episode, 1000 at most |

The per-thread CPU split is tick-sampled (TICK_CPU_ACCOUNTING, HZ 250, shown in USER_HZ). The
process CPU time is exact. The GART counters in `ps3rsx_mm` are not read: that debugfs file takes
`mm_lock` and walks every buffer, and ps3-bench.sh's 0.5 s samples have them already.

**Time alignment.** The HUD dump is line-buffered (`_IOLBF` in hud_context.c), so a frame's line
appears when the frame ends. The recorder counts the new lines every sample, which places each
frame on its clock to within one period. stalls.py picks the stall frames exactly as report.py does
(`report.frame_ends`, the window from bench.txt) and takes their timing from the recorder. client.sh
now writes `t0` (its start, `$EPOCHREALTIME`) into bench.txt, and the recorder writes a
monotonic/realtime pair at its start and every 10 s. stalls.py prints how far the two alignments
agree ("t0 says +0.001 s"). The load (client start to `load_end`) is placed through the frames, or
through `t0` when there are none.

## The causes

Each sample gets one cause, from the main thread's state and the deltas since the previous
sample. A stall's seconds are split over the samples that cover it (they add up to `stall_s`), and
its cause is the largest share. The rules, in order:

1. A major fault in the sample, or D state outside a syscall (a page fault): `swap_vswap` if
   ps3vswap0 read, `swap_zram` if zram0 read or pswpin moved, `disk` if the disk read (or had I/O in
   flight), else `fault`.
2. Running or in D, not in a GPU wait: `gart` if cma_alloc moved (ps3rsx growing the GART), `reclaim`
   if allocstall, pgscan_direct, pgsteal_direct or compact_stall moved.
3. Asleep in an ioctl on `/dev/dri/*`, or a wchan matching dma_fence, dma_resv, ps3rsx, drm_ or
   nouveau: `gpu` (Mesa's fence waits are GEM_CPU_PREP, wchan `dma_fence_default_wait`).
4. D state in read, pread64, readv, openat, statx, getdents64, fsync and the like: `io`.
5. Running: `runq` if the process got under a quarter of the sample's CPU time (runnable, not
   running: both PPE threads were busy with something else), else `cpu`. Shader compiles land here.
6. Asleep (S): `wait` (futex: another thread; poll or recvmsg: X; nanosleep: a frame limiter).
7. Anything else, or no sample: `unknown`.

report.py's columns group them: `stall_disk_s`, `stall_swap_s` (both swaps), `stall_reclaim_s`
(reclaim and gart), `stall_gpu_s`, `stall_cpu_s` (cpu and runq), `stall_io_s`, `stall_wait_s`,
`stall_other_s` (fault and unknown), and `stall_why` (the largest). matrix.py has
`stall_{disk,swap,reclaim,gpu,cpu}_s.csv`, created by the first run that has the number.

Limits: direct reclaim counters are global, so a reclaim by Xorg while the game runs its own code
also counts as `reclaim` (Xorg's state is in the evidence). A sample is the state at one instant, so
a sample in R state may have spent part of its 10 ms waiting. `runq` uses the whole process's CPU
time, so a busy second thread hides it.

## Validation

`python3 games/bench/selftest.py` (sections 5 and 6):

- a hand-made `stalls.rec` with one 200 ms stall of each cause the PC can't make (a fence wait,
  zram and VRAM swap-ins, direct reclaim, GART growth, runnable without CPU, disk and read waits, a
  futex): each classified right, frames aligned to the made-up 0.5 s offset, report.py's columns and
  matrix.py's file right;
- the real recorder, built with the host's cc, on a fake game that stalls 300 ms three ways: major
  faults on a 64 MB file dropped from the page cache (POSIX_FADV_DONTNEED, MADV_RANDOM, on a disk
  directory: /var/tmp), a busy loop, a futex wait. It comes out as disk (4294 major faults, 4294
  disk reads), cpu (290 ms user) and wait (futex), with the frames and `t0` agreeing within 1 ms.

Kernel stacks need root, so the PC test never reads them; the first PS3 run will.

## OpenArena: what stalls and why (2026-10-18)

The first recorder runs (ioquake3 ps3.379, the timedemo looped, 20261017T233701Z and T234549Z)
put 81% of the window's stall time on the main thread's CPU. `passsplit.py` splits it by where it
falls in a pass: 10.2-10.8 s of the 17.6-17.9 s are the map reload at the start of every pass (the
loop reloads the map each time: three load-screen frames of 340, 700 and 380 ms, the world's
textures read from pak4-textures.pk3, decoded and compressed to DXT on the PPE), 7.1-7.3 s the
demo itself: four stalls a pass of 130-400 ms, 6.9, 10.1, 13.5 and 16.7 s into it. Those are bots
joining: the demo starts a match with bot_minplayers, which adds a bot every 10 s of game time (every
200 timedemo frames), and OpenArena's qagame sends `loaddefered` with every bot added during play,
so the cgame loads its models, skins and textures at once (cg_deferPlayers can't defer them). The
same happens in real play at every match with bots. Host trace (llvmpipe, the demo, three passes):
per pass 5 joins, 15 MD3 models, 15 skins, 35 image lookups and 23 texture uploads during the demo;
sounds are loaded once per session (OpenAL keeps them across levels).

ioquake3 0025-0026 (+ps3.396, both on by default, host frames identical over three passes):
`r_keepImages 1` keeps the textures of image files across a map change and takes them back by name
when the next level is the same map (host reload 0.46 -> 0.17 s, the joins' uploads gone);
`cl_preloadLate 1` registers what the cgame registered during the last level's play again at the
end of the next load of the same map (from the second pass on nothing loads during the demo, the
longest frame 37 -> 14 ms on the host). Both start working at the second pass, which the window
never includes. `cl_deferPlayers 1` (opt-in) drops `loaddefered`: the bots keep a substitute
model, which changes the picture. Recipe variants `keepimg0`, `preload0`, `loads0` (both off) and
`defer` (games/bench/recipes/openarena.sh).

## Gameplay-only stalls (2026-10-18)

The user cares about stutter during play, and a looped demo's reload frames are not play: for
OpenArena 10 of the 18 stall seconds of a run were the map reload at the start of each pass.
report.py therefore computes the stall numbers a second time over the window less the reload
segments (the old columns keep the whole window, for history): `stalls_play`, `stall_play_s`,
`longest_play`, `low1_play`, plus `win_play` (seconds of play), `reload_s` and `reload_n` (what was
left out). matrix.py: `stalls_play.csv`, `stall_play_s.csv`, `low1_play.csv`, `longest_play_ms.csv`,
each created by the first run that has the number. HUD frame times only (not the per-second form).

A reload segment is found per pass (the `pass<N>` stamps in bench.txt: start end ...), by the game's
entry in `report.RELOADS`: a frame that ends inside a segment is the reload's, and so is one that
starts inside it, the segment then ending with that frame (since 2026-10-18: OpenArena opengl2's
load ends in one 8-16 s frame, the cgame load, cl_preloadLate and RB_ShowImages, that outlasts
"CL_InitCGame:" + 1 s by 4-5 s and was counted as play: gl2min's 16 s "gameplay freeze". Its
stall_play_s went 54.7 -> 16.0 s; opengl1 runs change by 0-4 s, docs/MEMORY.md).

| Game | Reload | Segment | Basis |
|---|---|---|---|
| OpenArena | map reload at every demo pass | pass start to 1 s after its first out.log line `CL_InitCGame:` (needs bench.txt `t0`) | measured, 20261017T2337/T2345: same split as before (10.2-10.8 s reload, 7.1-7.3 s demo) |
| Neverball | the replay restarts at every pass | pass start + 1 s | measured: the load frames (0.15-2 s) end 0.4 s after the pass line; runs before t0 was recorded still work, it is a fixed time |
| dsda-doom | level load after each timed pass | pass start + 1 s | a guess, no PS3 run with frame times yet; check the stalls' seconds into each pass (`passsplit.py -v`) |
| X-Moto | a new replay state per pass | pass start + 2 s | a guess, same |
| Sauerbraten | demoend plays the demo again | none: WIN_ON_RE/WIN_OFF_RE pieces already leave the reload out | the play numbers equal the old ones |
| SuperTuxKart, the rest | one race or session | none | play = old |

A run whose game needs the load line but has no `t0` (OpenArena before 2026-10-17 23:00) has no
play columns (`-`), not a wrong number. A recipe can set its own with `parse()` printing
`reload_re=`, `reload_after=` or `reload_fixed=` into bench.txt, which win over the table.
`passsplit.py` is the same code (`report.reload_segments`), splitting the recorder's stalls with
their causes; `--re`/`--after` override the game's load line. Test: selftest.py section 1 (a
synthetic looped demo: OpenArena by load line, Neverball by fixed time, bench.txt overrides, a game
with none, a missing t0).

Values of 2026-10-17 evening (whole window -> play: stall_s, stalls, low1): OpenArena base T2332 /
T2337 / T2341 / T2345: 41.4 -> 28.9 s, 34.9 -> 20.7 s (no recorder, 80 -> 47 stalls) and 17.9 -> 7.1 s,
17.6 -> 7.3 s (with the recorder: 58 -> 27 and 52 -> 26 stalls, low1 11 -> 27 and 11 -> 28). All four
ran the same ioquake3 ps3.379 (their `versions` and game.log agree); the difference is the
recorder, see the next section. Neverball
base/gloff: 17.2 -> 14.0, 19.6 -> 17.2, 25.6 -> 18.8, 22.7 -> 17.2 s, so Neverball's stalls are mostly
mid-replay, not the restart. Sauerbraten (the window already leaves the reload out): unchanged.

## The recorder removes stalls (2026-10-18)

Same boot, OpenArena base, the timedemo looped, the recorder off and on in turn. All four runs had
the same ioquake3 (ps3.379), Mesa, kernel and packages (`versions`):

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

What the runs show:

- A frame that doesn't stall costs the same either way (smooth fps, CPU per frame). The off runs
  lose 3-4 ms a frame off the CPU: the game waits, it isn't slower.
- The extra stalls of the off runs are long (4382, 4138, 4046, 3231, 3030 ms ...) and fall on
  random frames of a pass (97, 291, 356, 465, 890, 1200), not on the bot joins (frames 348, 948)
  or the reloads, which both kinds of run have. Swap was quiet during them (no pswpin/pswpout in
  ps3-bench.sh's 0.5 s samples, 55 MB available). Every run of 2026-10-17 without the recorder has
  frames over 2 s after its first 400 frames (OpenArena 1-6 a run, one of them the second pass's
  reload; Neverball 1-4, Luanti 1-3); the two recorder runs have only that reload.
- The recorder goes late the same way. It sleeps to an absolute 10 ms deadline at nice -10 and
  still missed up to 879 ms (13 gaps over 30 ms in each run, 6 and 9 over 90 ms). The longest gaps (T2337: 245, 879,
  376, 604 ms; T2345: 464, 165, 592 ms) start with the game's main thread asleep in
  `dma_fence_default_wait` (an ioctl on /dev/dri/card0) and end with a frame of the same length:
  two waits that end by a timer, the recorder's sleep and ps3rsx's fence poll, stopped and resumed
  together. ps3rsx signals fences from an hrtimer (exec.c `fence_poll`, every 250 us while someone
  waits), and an hrtimer fires on the CPU that started it, here the waiter's.

**The mechanism (best explanation, from the kernel source; not yet measured on the PS3).** The idle
loop calls `ps3_power_save()` (arch/powerpc/platforms/ps3/setup.c) soft-disabled, MSR[EE] still on,
and it calls `lv1_pause(0)` without `prep_irq_for_idle()`, unlike the other powerpc idle states. A
decrementer that fires between the idle loop's `local_irq_disable()` and the hcall is masked:
`masked_interrupt` (exceptions-64s.S) sets DEC to 0x7fffffff, 27 s at the 79.8 MHz timebase, and
leaves PACA_IRQ_DEC for the replay. The thread then pauses with no decrementer to wake it, and every
timer of that thread waits for the next external interrupt: the tick, hrtimers (the fence poll,
nanosleep), timer-wheel timeouts. `ps3_virq_setup()` binds every PS3_BINDING_CPU_ANY interrupt to
thread 0 (USB, gelic, the RSX's vsync, the SPEs), so thread 0 recovers within milliseconds. Thread 1
gets nothing but IPIs and sleeps until something is woken onto it. The window is short, but a fence
wait enters idle 4000 times a second (the 250 us poll), and each entry is a chance for the 250 Hz
tick to land in it. The periodic tick (HZ_PERIODIC, no NO_HZ, no cpuidle) can't help: it is that
decrementer.

How the recorder helps: 100 wakeups a second at nice -10, 0.5 ms of work each. While it runs on the
thread that's awake, other wakeups (and its own, when its timer fires on a busy thread) are placed on
the stuck thread, which looks idle; that takes an IPI, the thread wakes, replays its decrementer and
the fence poll runs. The outage shrinks from seconds to about a period. It stays when the recorder's
own timer is on the stuck thread: the 879 ms gap.

Ruled out or unlikely:

- ps3-bench.sh: `BENCH_STALLS=1` only starts the recorder (sudo, nice -10, no affinity, cgroup or
  mlock) and waits up to 3 s for it after the run. Quiet, caches, swaps and timers take the same path.
- The second PPE thread kept busy (SMT sharing, the idle thread's priority): it would change the
  cost of a frame, and smooth fps and CPU per frame are the same.
- NO_HZ: not configured; see above for why the periodic tick doesn't cover a lost decrementer.
- The recorder's reads (/proc/<tid>/stat, wchan, syscall, stack, /proc/vmstat, diskstats, the VRAM
  swap's stats): none sends an IPI or touches the game's memory. Its /dev/shm writes are 3 KB a
  second. Both get their own A/B below.

Side finding: /proc/stat says both CPUs are 99% busy in every recorded run, also during the load's
disk waits and after the game exited. With TICK_CPU_ACCOUNTING the tick that wakes an idle PPE
thread is replayed inside `arch_local_irq_restore()`'s `irq_enter()`, so `timer_interrupt()`'s own
`irq_enter()` nests and `account_process_tick()` books the tick as system time. The record's `cpu=`
busy/idle means nothing on this kernel (no cause uses it; `runq` uses the process's CPU time).

### The A/B on the PS3

ps3-gpu-tools 0.399 has `ps3-waker` and the switches (ps3-bench.sh's header; run.sh takes them
without the `BENCH_` prefix, and `ENVS` interleaves them within each round). report.py shows each
run's switches after the variant (`base@wksleep10c1`), from bench.txt's `harness=`.

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

If the pinned wakers help as much as the unpinned one, the explanation is wrong and the effect is
the scheduler's (placement, SMT): then profile with `PERF_ALL=1`. `probe.txt`'s `late` lines are
CLOCK_MONOTONIC seconds; its `# clock` pair puts them on the client's clock as stalls.py does (real
= mono - mono0 + real0, then minus bench.txt's t0).

Commands (desktop session over, `make deploy PKGS=ps3-gpu-tools` and `make bench-push` done; each
batch has its own off runs, as the same-boot rule wants):

    # 1. no game: does a pinned FIFO sleeper on an idle thread oversleep? (2 min each)
    ps3-bench.sh run probe-idle -- sudo ps3-waker -m probe -i 1.37 -t 120
    ps3-bench.sh run probe-kick -- sh -c 'sudo ps3-waker -m kick -i 10 -t 125 -o /dev/null &
        sudo ps3-waker -m probe -i 1.37 -t 120'
    ps3-bench.sh desktop
    # 2. the mechanism (10 runs, about 45 min)
    ENVS="- STALLS=1 WAKER=10 WAKER=10+WAKER_MODE=kick PROBE=1" /var/tmp/bench/run.sh bench openarena base 2
    # 3. which part does it (16 runs, about 70 min)
    ENVS="- WAKER=10+WAKER_CPU=0 WAKER=10+WAKER_CPU=1 WAKER=10+WAKER_MODE=kick+WAKER_CPU=1 \
        WAKER=10+WAKER_MODE=kick+WAKER_CPU=0 STALLS=1+STALLS_NOWRITE=1 WAKER=1 WAKER=50" \
        /var/tmp/bench/run.sh bench openarena base 2
    # 4. another game with mid-play stalls (Neverball, Luanti)
    ENVS="- WAKER=10+WAKER_MODE=kick" /var/tmp/bench/run.sh bench neverball base 3
    ps3-bench.sh desktop

Test 1 expects thread 1 (`cpu 1`) to show wakeups late by 100 ms or more (`ge100`) and thread 0
only short ones; with the kicker, nothing over about 10 ms on either. A pinned FIFO thread late by
100 ms on an otherwise idle PS3 has hardly another explanation than its timer not firing (a long
stretch with interrupts off would show on both threads and in dmesg's RCU or watchdog lines).

### The fix

linux/patches/local/0009-ps3-idle-no-pause-with-irq-pending.patch (in the series since
2026-10-18, a test kernel; built, not yet booted; booted since in .419 and .433 and confirmed, see
"The fix on the PS3" below): `ps3_power_save()` calls `prep_irq_for_idle()`
first, as pseries and the native Cell's `cbe_power_save()` do. It hard-disables and returns without
pausing when something was masked since `local_irq_disable()` (the replay in `local_irq_enable()`
runs it), otherwise it marks the thread soft-enabled; then `__hard_irq_enable()` and, unless
`need_resched()`, `lv1_pause(0)` with MSR[EE] on, as the hcall is normally entered today. From the
check on, a decrementer or IPI is a normal interrupt (it re-arms DEC) instead of a masked one.

The first draft paused with MSR[EE] off (`prep_irq_for_idle_irqsoff()`, powernv's way). Review
dropped it: that relies on `lv1_pause()` ending on a decrementer pending while MSR[EE] is off, and on
the hypervisor leaving that exception pending for the guest (the native Cell's wakeup handler has to
`set_dec(1)` because the wakeup consumes it). The only documentation is "MSR[EE] has no effect"; if
either part fails, every idle thread loses its tick. With MSR[EE] on the remaining race is an
interrupt handled between the hard-enable and the hcall that doesn't set need_resched; the pause
then ends at the next timer event, at most one 4 ms tick. Re-arming DEC before the pause instead
wouldn't close the window: a decrementer masked after the re-arm is lost the same way.

To test it: install the kernel, boot it, and run test 1 and batch 2 again. The fix has an off
switch for same-boot A/B runs: `echo 0 | sudo tee /sys/module/kernel/parameters/ps3_idle_prep`
brings back the old pause at run time (`1` the fix again), `ps3_idle_prep=0` on the command line
boots without it. Fixed, the switches stop
mattering: `-`, `STALLS=1` and kick give the same stall_play_s (about 7 s), and test 1 shows no late
wakeups. Watch the fan and `uptime` on the first boot: if the pause were skipped every time, the
idle threads would spin and run hotter (nothing controls the fan under Linux).

Until a fixed kernel is in, a stopgap is the kicker during game mode (ps3-desktop's ps3-game could
start `ps3-waker -m kick -i 10 -c 1`, 100 wakeups a second on each side). ps3rsx could also retire
fences from its vsync interrupt (thread 0, 60 Hz) as well as from the poll timer, so that no fence
depends on one thread's timer; that bounds a fence wait to 17 ms but leaves every other timer of the
stuck thread waiting. Neither is done; the kernel fix comes first. (Later on 2026-10-18: the kicker is
done, `ps3-kick` in ps3-system, started by game mode as `ps3-game-kick` and skipped when
`ps3_idle_prep` is on; not yet seen running through game mode on the PS3. The vsync retire is not
done and is not needed with the fixed kernel.)

The OpenArena analysis above ("what stalls and why") came from recorder runs, so it describes the
stalls that are left with the recorder running: the reloads and the bot joins are real either way,
the long waits of the plain runs are not in it.

## What the kernel and the driver could add

The 6.12 kernel here has no PSI, no delay accounting (TASK_DELAY_ACCT/TASKSTATS), no SCHEDSTATS
(so no run queue time per thread) and no TASK_IO_ACCOUNTING (no `/proc/<pid>/io`). These are
proposals only; nothing has been built:

- `CONFIG_TASK_DELAY_ACCT` (+ `delayacct` on the command line): per task, the exact time spent in
  block I/O, swap-in, direct reclaim, thrashing and compaction. That would turn rules 1 and 2 from
  sampled into measured. `CONFIG_PSI`: the system-wide memory, io and cpu stall time as cheap
  cumulative counters. `CONFIG_SCHED_INFO` (via SCHEDSTATS): run queue wait per thread
  (`/proc/<tid>/schedstat`), for an exact `runq`. Their RAM cost would need measuring
  (docs/KERNEL-MEMORY.md).
- ps3rsx: a debugfs `ps3rsx_stats` with lockless counters, for the recorder to read every slow
  tick: GART chunks backed and released, with the nanoseconds spent in `chunk_back` (cma_alloc plus
  the 1 MiB clear); fence waits in GEM_CPU_PREP and the pushbuf's sync wait (count, ns); waits for
  the primitive owner (`prim_wq`); VRAM swap shrink passes (count, ns). The vswap `stats` file could
  read its counters with READ_ONCE instead of `vs->lock`, so it could be sampled every 10 ms.

### Confirmed on the PS3 (2026-10-18)

Probe without a game, ps3-gpu-tools 0.399, 120 s each (runs ps3-bench-20261018T014733Z-probe-idle and
T014942Z-probe-kick): with both threads idle, a SCHED_FIFO sleeper on thread 1 woke up to 1439 ms
late (13 wakeups 100 ms or more late, 2 over 1 s); thread 0 at most 16.7 ms. With a 10 ms kicker
(`ps3-waker -m kick -i 10`) thread 1's worst was 19.4 ms, none over 100 ms. The lost-decrementer
explanation holds for the idle thread; the game batch (ENVS) measures what it costs games.

## Game prefetch (2026-10-18)

The PS3's root file system is a 2.5" hard disk on USB 2.0, 10-20 ms a random read. A game starting
cold faults its program and libraries in page by page and reads its packs, maps and textures in
small pieces, and each of those is such a read. The recorder put Sauerbraten's hitches and about a
third of its 16.5 s load on the disk. ps3-desktop's `ps3-game-prefetch` (code in
`files/libexec/ps3_game_prefetch.py`, its header has the details) learns what a game used and, at
its next start, reads that back in one pass in disk order while the game loads.

**What is learnt.** For the first 180 s of a game (`PS3_GAME_PREFETCH_SAMPLE_S`) its processes are
looked at every 1 s, then every 3 s, then every 10 s: the files mapped or open, the working
directories, and the game process itself (the largest RSS). When the game is over, a few more files
are added: every file under the game's data directories (`/usr/share/games/<name>`,
`/usr/share/<name>`, `~/.<name>`, `~/.local/share/<name>`, `~/.cache/<name>` such as Mesa's shader
cache, the working directory), which catches maps and textures that were read and closed again,
plus the files of the old list. Each file's resident pages (mincore) become ranges (holes under 128
KiB filled), and each range gets its disk address (FIEMAP). The new list is the union with the old
one, and a file with no page in memory for 3 sessions is dropped. The list is keyed by the game's
executable (a script's path for an interpreter). Its size and mtime stand for the package version:
an upgraded binary makes the list stale (it is not read, and gets learnt anew). The command the game
was started by is a symlink to the same list. Lists live in `~/.cache/ps3-game-prefetch`
(`PS3_GAME_PREFETCH_DIR`); `ps3-game-prefetch game-list`, `game-show KEY` and `game-forget KEY`
inspect and drop them.

**What is read.** The budget is min(`PS3_GAME_PREFETCH_MB`, default 64 MB, and MemFree or
MemAvailable minus `PS3_GAME_PREFETCH_RESERVE_MB`, default 48), counted in pages not already in
memory. It is filled in the order the game first used its files (program and libraries first, then
data in first-seen order, files found only by the walk last), and the selection is then sorted by
disk address. Reads are 256 KiB `preadv`s with readahead off on our descriptors (POSIX_FADV_RANDOM),
one in flight, at I/O priority `PS3_GAME_PREFETCH_IO` (be7 by default; idle; be0-7) and nice 10.
A fault of the game therefore waits behind one of our reads at most (about 8 ms of transfer). The
fetch stops early when MemFree falls under the reserve, on direct reclaim or swapping
(`/proc/vmstat` allocstall, compact_stall, pswpin/out), after 60 s, or when the game exits. It logs
one line: `game-fetch KEY: read X MB from N files in T s (Y MB of the list in memory already, ...;
budget B MB, L MB listed; R reads, io be7)[, stopped: why]`.

**Game mode.** `PS3_GAME_PREFETCH_GAME`, from the game's own environment: on (the default) fetches
and learns, fetch only fetches, learn only learns, off does neither. Not with
`PS3_GAME_MODE=pads`. Once game mode has entered (after `ps3-game-mode on`, which drops the
unmapped page cache), ps3-game-guard starts `ps3-game-prefetch game-watch`. The watcher claims each
registered game, from ps3-game (key: the command after `ps3-game` in the launcher's command line)
or from ps3-game-detect (key: the detected process's executable), in a record
`$XDG_RUNTIME_DIR/ps3-game/gp/<id>.seen`. It starts the game's fetch as a process of its own and
looks at the game for its first minutes. Once no game is left to look at, it exits, so nothing of it
stays in memory during play. The guard starts another watcher for a game registered later that no
record covers. At the end of `leave`, with the desktop already back, `game-learn` turns every record
into its list. The program is a small stub that imports `ps3_game_prefetch.py` from the byte-code
cache that postinst builds (`py3compile`, and again from a python3 runtime.d hook when python3
changes). With that cache the watcher costs 3.5 MB anonymous in Debian sid's python3 3.14 (the
package installed in the deb builder), about what the bare interpreter costs. Compiled at every
start, it would cost 8.8 MB (measured on the host).

**The bench harness.** Bench runs normally have no game mode, so `client.sh` runs the game through
`ps3-game-prefetch game-run` when `BENCH_GAMEPREFETCH` is set (run.sh `GAMEPREFETCH=`, an ENVS token):
- `fetch` (or `1`) reads the list a learn run left and learns nothing, so every measured run reads
  the same list. game-run forks a detached fetch and execs the game: same pid, same parent, nothing
  of Python left beside it.
- `learn` makes or grows the list and fetches nothing. Python stays as the game's parent: about
  4 MB, so don't measure these runs.
- `on` does both.

`GAMEPREFETCH_MB`, `_RESERVE_MB` and `_IO` set the fetch's knobs. Recipes with GAMEMODE=1 get
`PS3_GAME_PREFETCH_GAME=off` unless the token is given. Lists are kept in
`/var/tmp/ps3-bench/gprefetch`, apart from the desktop's. bench.txt gets `gprefetch=`, and report.py
shows it after the variant (`base@gpffetch`, `base@rec10+gpffetch_mb128`).

report.py gained the first minute, which the window leaves out: `stalls_warm`, `stall_warm_s` and
`longest_warm` over the WARMUP seconds after `load_end`. With a stall record it also gives
`load_disk_s` (the load's seconds waiting on the disk, all of it, not only stall frames) and
`warm_disk_s` (the warm-up's stall frames' disk seconds); stalls.py prints the warm-up's causes.

**Host tests.** `python3 ps3-desktop/tests/gprefetch-check.py` (73 checks) covers:
- the range and wrapper logic, and the data-directory rules;
- mincore after POSIX_FADV_DONTNEED, FIEMAP extents and addresses (a sparse file too);
- learning over 5 made-up sessions: union, first-seen times, misses, a changed file, an upgraded
  program, the command's symlink;
- the budget's residency-aware selection and its order, exact reads, each early stop (with made-up
  /proc numbers);
- game-run end to end: learn, the pages dropped, fetch, the pages back, exec keeping the pid, exit
  statuses.

`ps3-desktop/tests/autogm-check.sh` (53 checks, 6 new) covers:
- a detected game's list learnt after leave;
- that list read by the next detected start and by a ps3-game start, through a second watcher;
- `PS3_GAME_PREFETCH_GAME=off`.

`games/bench/selftest.py` covers client.sh with learn, then fetch, then a budget, plus the warm-up
columns. Under a host load of 10-15, autogm-check failed one timing check in 3 of 6 runs, a
different one each time, and the last 3 passed. The unchanged HEAD version passed the one run made
at that load. Nothing has run on the PS3 yet. (2026-10-18 afternoon: ps3-desktop 0.446 installed and
step 0's learn runs made for Sauerbraten, OpenArena gl2min and Luanti; SuperTuxKart's was interrupted
by the reboot session. The lists are in `/var/tmp/ps3-bench/gprefetch` on the PS3. Step 1, the
measured A/B, has not run.)

### The A/B on the PS3

Same boot, desktop stopped. The harness's quiet drops the caches before every run, so each run
starts cold. The base runs and the prefetch runs interleave within each round. All runs have the
recorder (`STALLS=1`) for `stall_disk_s` and `load_disk_s`. The recorder changes the stalls it
records (above), but it does so for both sides alike. ps3-desktop with game-run must be installed,
and `make bench-push` done.

    # 0. one learn run per game, cold, not measured (the list from the game's own cold start)
    for g in "sauerbraten base" "openarena gl2min" "luanti base" "supertuxkart lean"; do
        ENVS="GAMEPREFETCH=learn" /var/tmp/bench/run.sh bench $g 1; done
    /usr/lib/ps3/desktop/ps3-game-prefetch game-list --dir /var/tmp/ps3-bench/gprefetch
    /usr/lib/ps3/desktop/ps3-game-prefetch game-show --dir /var/tmp/ps3-bench/gprefetch /usr/games/cube2
    # 1. base against prefetch, 3 rounds each (24 runs, about 2 h)
    STALLS=1 ENVS="- GAMEPREFETCH=fetch" /var/tmp/bench/run.sh bench sauerbraten base 3
    STALLS=1 ENVS="- GAMEPREFETCH=fetch" /var/tmp/bench/run.sh bench openarena gl2min 3
    STALLS=1 ENVS="- GAMEPREFETCH=fetch" /var/tmp/bench/run.sh bench luanti base 3
    STALLS=1 ENVS="- GAMEPREFETCH=fetch" /var/tmp/bench/run.sh bench supertuxkart lean 3
    # 2. if 1 helps: the budget and the I/O priority (Sauerbraten, the most disk-bound)
    STALLS=1 ENVS="GAMEPREFETCH=fetch GAMEPREFETCH=fetch+GAMEPREFETCH_MB=128 GAMEPREFETCH=fetch+GAMEPREFETCH_IO=idle" \
        /var/tmp/bench/run.sh bench sauerbraten base 3
    ps3-bench.sh desktop
    python3 /var/tmp/bench/report.py --stalls /var/tmp/ps3-bench/<the runs>

Read per game, base@rec10 against base@rec10+gpffetch:
- `load`, `load_disk_s`;
- `stalls_warm`, `stall_warm_s`, `warm_disk_s` (the first minute);
- `stall_play_s`, `stall_disk_s` (the window);
- `steady` and `low1` (no change expected);
- `avail`, the lowest MemAvailable: the prefetch must not cost the game memory, so watch `so_all`
  (swap-outs) as well.

Each prefetch run's game.log has the `game-fetch` line, and its MB read, time and stop reason say
how the budget went. Expected if the disk is the cost: lower `load_disk_s` and load time, fewer
`stalls_warm`, and the same fps. If `stopped: memory` shows up, the reserve is too high or the
budget too large for that game. The webcam check holds as for every run. On the desktop, after the
bench: start OpenArena twice from the menu with game mode on and read the user journal (`journalctl
--user -u ps3-game-mode`). The first start should log `nothing learnt yet`, then `game-learn` after
`left game mode`. The second should log `game-fetch ... read`.

### The fix on the PS3 (2026-10-18, kernel 6.12.111+ps3.419)

Same boot, probe without a game (`ps3-waker -m probe -i 1.37 -t 120`), runs
ps3-bench-20261018T120716Z-probe-fixon and ...-probe-fixoff: with `ps3_idle_prep=Y` both threads'
worst wakeup was 3.6 / 4.0 ms (none 10 ms or later); with the switch at 0 thread 1 was up to 1103 ms
late (11 wakeups 100 ms or more, 1 over 1 s), thread 0 16.7 ms. The fix removes the lost decrementer
and does better than the 10 ms kicker (19.4 ms worst).

In a game, same boot, no kicker, OpenArena base with the switch on/off/on/off (results row "kernel
6.12.111+ps3.419 idle fix on/off"): gameplay stalls 0.5 / 8.2 / 0.1 / 5.6 s, longest frame 285 / 2598 /
122 / 982 ms, steady 71.2 / 64.4 / 66.7 / 67.0 fps. An SSH wait loop polled the PS3 through all four
runs (listed in results/interfered.txt), so the absolute numbers aren't clean, but the on/off
difference held in both pairs. Kernel .433 (the fix plus the hwmon, fan and shutdown patches) booted
next with the fix active. Still to do: the clean repeat on .433, then the fixed kernel as the default
and the kicker retired.

## Interference (2026-10-18)

The user: "add an interference test in the test harness so if someone interferes with the PS3 the
test is invalidated". Their child was in the room that day, and on 2026-10-17 the user once hit
Ctrl+Alt+F2 by accident in the middle of a run. Later rules from the user: trying to play the game
instead of letting the demo play is interference too (no guessing at intent: any real input
counts); so is running commands on the PS3 during a run (disk activity, swapped pages brought
back); the mouse gets some tolerance (a phantom movement or a bump is fine, deliberate motion and
any button or wheel are not).

### The detector

`ps3-intfwatch` (ps3-gpu-tools/src/ps3-intfwatch.c, its header has every rule) runs as root next
to every `ps3-bench.sh x` and `run`, on by default (`BENCH_INTF=0` turns it off). It starts just
before the command, so it sees the run's X server take vt1, and stops with the stop file. What
counts, each a `!` line in the run's `interference.log`:

- **Input** on every `/dev/input/event*` device, including devices connected mid-run (inotify on
  `/dev/input`): every key press (repeats and releases don't count; the key's code isn't logged,
  so the log is no key logger), controller and mouse buttons, wheel steps, a stick or trigger
  leaving its rest position by a quarter of its range (once per excursion; the DualShock 3's
  sticks jitter by a few units of 255 and stay quiet), any d-pad hat value. Mouse motion counts
  only when deliberate: one burst of 2 s or more, or 1500 counts within 10 s. A bump is recorded
  as a `-` line and doesn't count. The DS3's motion sensors (an accelerometer device) aren't
  watched. A DS3 that is already on before the run is watched like any other device, and one
  switched on mid-run shows up as a new device.
- **Devices**: an input device appearing or going away, a kernel line of a USB device connected
  or disconnected (`/dev/kmsg`). A Bluetooth controller that drops its connection by itself
  counts too: the log can't tell that from someone switching it off.
- **VT switches** (`/sys/class/tty/tty0/active`, polled for POLLPRI): everything except the run's
  X server taking vt1 and giving the VT back after the game has exited.
- **Logins and commands**: a new logind session (inotify on `/run/systemd/sessions`; every SSH
  connection opens one, `ssh ps3 cmd` included), a new process in a login session that isn't the
  run's own, and a process of a login session that already existed and used 1 s of CPU or more
  during the run. The kernel has no proc connector (CONFIG_PROC_EVENTS is off), so `/proc` is
  scanned every 250 ms. Only the directory is read; a new pid's stat, cgroup, command line and,
  when needed, environment are read once. The run's own processes are ps3-bench.sh's descendants
  and process group, plus anything carrying `BENCH_DIR=<run dir>` in its environment (a daemon
  a game forks). The run's ancestors (run.sh, the SSH session that started it) belong to the
  harness, but what they start meanwhile doesn't.

Exempt, recorded as `-` lines: the bench's own evemu keyboards (uinput devices under
`/devices/virtual/input/` whose `/dev/uinput` is held by `evemu-device`; they carry the real
receiver's name "Logitech USB Receiver", so the name can't tell them apart), and
`tools/bench-cam.sh`'s watcher (its command line has `pgrep -x run.sh` and `/dev/shm/ps3-bench-`;
it and its session are exempt). XTEST (`xkeys.py`) makes no kernel input event at all, so nothing
is seen. System services' and the user manager's new processes are listed under `info`.

Not seen: a process outside a new session that lives less than one 250 ms scan. The PS3's power
and reset buttons aren't input devices: the System Manager sends init SIGINT (ctrl-alt-del), the
reboot ends the run, and its /dev/shm directory goes with it. Nothing before the detector's start
(the quiet phase) or after the stop is watched. The game window losing focus isn't checked.

Cost on the host: 3 ms of CPU a second with 570 processes, 2 MB RSS. On the PS3 (about 150
processes, a slower PPE) the `result` line `interference: none (...)` and the log's `# self cpu=`
give the number; nothing has measured it there yet.

### Results

bench.txt gets `interfered=0|1` and `interference=<summary>` (for example "3 key presses, VT switch
1->2 at 85 s, 1 login(s): session 12 (tibi, sshd from 10.0.0.45) at 40 s"). The result file and the
terminal get a loud `!! INTERFERENCE: ... THIS RUN IS INVALID` line. run.sh warns after such a run,
and `RERUN_INTERFERED=1` runs it once more right away. report.py has an `intf` column (1, 0, - for
not watched) and marks the run `!INTERFERED`. It leaves interfered runs out of the medians and lists
them at the end (`--include-interfered` keeps them in). matrix.py skips them and lists what it
skipped (`--include-interfered` too). Runs from before the detector are listed in
`games/bench/results/interfered.txt`, which report.py and matrix.py read the same way.

### The bench console

When the desktop stops, the TV used to keep the last frame: the user saw a frozen boot splash and
thought the PS3 had locked up. Now `quiet` writes a "BENCHMARK RUNNING" screen to tty8 (`BENCH_VT`,
0 turns it off) and switches to it. The screen says what runs (run.sh passes game and variant as
`BENCH_WHAT`), when it started, and that any input invalidates the run. tty8 has no getty (logind
starts autovt gettys on tty1-6 only). The run's X server (`xinit ... vt1`) switches to vt1 and gives
tty8 back when it exits, so the screen shows again between runs. After an interfered run it says
so. `desktop` switches to vt1 and frees tty8. The detector expects both of the X server's switches
(`-x 1 -H 8`) and counts every other one. The kernel has only the 8x16 font, so the banner is drawn
in `#` blocks.

### Retro-scan of 2026-10-17/18

`games/bench/interference-scan.py` looks at what saved run directories still show: Xorg.0.log (VT
switches, input devices added or removed mid-run, the evemu keyboards left out), dmesg.log (USB
connects, non-virtual input devices, Bluetooth HID lines), and game.log for Luanti, which logs
every player action ("singleplayer digs/activates ..."). The benchmark's flight never digs or
clicks, so each such line is a mouse button pressed in the game. Run over the 339 pulled
directories of 2026-10-17 and 18 (`--write` filled results/interfered.txt), it found six:

| Run | Evidence | Matrix row affected |
|---|---|---|
| 20261017T181626Z-supertuxkart-nohd | VT switch away 127 s after the X start, back 4 s later (the user's Ctrl+Alt+F2) | none (not in the matrices) |
| 20261017T224350Z-luanti-allbatchoff | 3 digs at 61 s, in the warm-up (stalls of 2.5 and 0.6 s at 59-62 s) | "Luanti ps3.364 batching ...": allbatchoff is the median of this run and one clean run |
| 20261018T081125Z-luanti-base | 1 activate (right click, empty hand) at 105 s, in the window | "batch 8 ...": luanti-base@wkkick10+perf |
| 20261018T093700Z-luanti-base | 4 activates at 142 s and 2 digs at 148 s, in the window; the run's longest frames are right after the digs (698 ms at 147.6 s, 1943 ms at 150.1 s, 546 ms at 150.7 s) | "Luanti ps3.405 memory variants ...": base (its 6.5 s of gameplay stalls and 1943 ms longest frame) |
| 20261018T094136Z-luanti-lean | 72 actions: 11 digs in the warm-up, 30 digs and 31 activates in the window | the same row: lean |
| 20261018T094600Z-luanti-tradeoff | 3 activates at 109 s, in the window | the same row: tradeoff |

The lean run's webcam photos confirm it: at 60 s the dig crack and the raised hand are on screen
and the hotbar holds a block, and at 150 s five blocks sit in the hotbar while the view points at
the sky. Clean runs have an empty hotbar and no hand. So "lean renders less terrain (missing
nearby terrain at 150 s)" was most likely someone digging and looking around, not the variant.
The same row's base, compared with cliunload ("gameplay stalls 6.5 -> 0.8 s, longest frame 1943 ->
259 ms", PLAN.md decision 2), had its worst frames right at the digs. Both comparisons need clean
runs of base, cliunload and lean before the decision (done 2026-10-18 evening, results row "Luanti
ps3.405 clean re-runs": base 1.5 and 2.1 s, cliunload worse at 3.8 and 5.2 s, lean a black void on the
webcam at 150 s in both clean runs: the interfered run's photo was the digging, but lean does
fail to draw the world). The same photos show a solitaire game in
progress on the PC's screen at those minutes (12:39-12:44 local). If the mouse used there is also
paired with the PS3's Logitech receiver, the user's own mouse reaches the PS3; worth checking.

What these files can't show: key presses, mouse motion and controller input that left no trace in
a game's log, the game window losing focus, and SSH logins or commands during a run. The journal
isn't saved per run. The PS3's kernel journal for 2026-10-18 has no input, USB or Bluetooth connect
or disconnect lines; its hci0 found/removed lines are mpris-proxy starting and stopping with each
desktop restart. A run without evidence isn't proven clean. Four runs with no trace in their files
are known to be contaminated and are listed by hand: the OpenArena `ps3_idle_prep` on/off batch of
15:13-15:31 local on 2026-10-18 (20261018T121340Z, 121821Z, 122303Z, 122726Z; matrix row "kernel
6.12.111+ps3.419 idle fix on/off", whose CAVEAT says so). An agent's wait loop polled the PS3 over
SSH every ~11 s through all four, and SSH scans of the journal and Xorg logs ran in the last one.
The on/off difference held across the alternation, but the absolute numbers need the clean repeat.

### Checking it on the PS3

(2026-10-18 evening: ps3-gpu-tools 0.453 is installed and the detector runs with every bench run since
batch 15; the six clean Luanti re-runs report `interfered=0`. The deliberate checks below, which need
someone at the PS3, have not been done, and neither the tty8 screen nor the detector's PPE cost has
been looked at.)

Needs ps3-gpu-tools with `ps3-intfwatch` (0.453 or later: `make ps3-gpu-tools-deb`, then a deploy) and
`make bench-push`. Nobody may be using the PS3; webMAN fan on SYSCON.

1. `run.sh smoke openarena`, then press one key on the PS3's keyboard about 30 s in. The run
   ends with `!! INTERFERENCE: 1 key presses ...`, bench.txt has `interfered=1`, and
   interference.log has a `! ... KEY dev="Logitech USB Receiver"` line with no key code.
   `report.py` on the directory shows `!INTERFERED` and `intf=1`.
2. The same run untouched: `interference: none (4 input devices watched; ...)` in the result, and
   only `-` lines in the log apart from the X server's two expected VT lines
   (`VT 8 -> 1 (the X server takes its VT)`, `VT 1 -> 8 (... after the game)`).
3. During a run, `ssh ps3 true` from the PC: counted as a login (session N, sshd from
   10.0.0.45). With `tools/bench-cam.sh` running for the batch, its watcher shows only as
   `exempt` in the info line.
4. `run.sh smoke armagetronad` (the evemu keyboard): `DEVADD ... (the bench's virtual device ...)`
   as a `-` line, no interference from its key presses.
5. The TV shows the BENCHMARK RUNNING screen while the desktop is stopped and between runs, the
   game during the run, and the desktop after `ps3-bench.sh desktop` (webcam).
6. Move the mouse briefly: a `- ... MOUSE ... bump` line, not counted. Move it for 3 s: counted.
