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
