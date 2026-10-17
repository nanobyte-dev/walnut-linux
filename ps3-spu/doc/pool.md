# The deadline pool: one SPU queue for every latency-bound service

The user's decision of 2026-10-17 (docs/IDEAS.md, "Decisions" 3): one process-wide pool of 2
SPEs that every latency-bound service submits to (fluidsynth's synth, skinning, later an OpenAL
mixer), jobs taken by deadline, while DXT keeps its 4 bulk SPEs, so a process never runs more
SPU contexts than SPEs. Built 2026-10-17, host-tested, not run on the PS3 yet; "estimate" marks
numbers nobody measured.

Files: `lib/pool.c` (the pool), `lib/pool-job.h` (its program and op numbers), `spu/deadline.c`
(deadline.spu: the ops' table and their shared local-store arena), `spu/synth-op.c` and
`spu/skin-op.c` (the ops, now parts of deadline.spu), `spu/runtime.c` and `lib/queue.c` (the
deadline order), `tests/pool-test.c`, `tests/queue-test.c`.

## 1. What it replaces

Before, each service opened its own queue: DXT a bulk queue on 4 SPEs (`SPEQ_OPEN_BULK`), the
synth (synth.spu) and skinning (skin.spu) a deadline queue of up to 2 workers each
(`SPEQ_SYNTH_SPES`, `SPEQ_SKIN_SPES`). A game with music, skinning and compressed textures ran
4 + 2 + 2 = 8 contexts on 6 SPEs, and spufs time-slices past 6 in 80 ms slices: far past an
audio block (1.45 ms) or a frame. SPU-AUDIO.md 5(b) chose a queue per service then because one
shared queue needed a claim-order change in the runtime and the programs' buffers in one local
store; both are done here.

## 2. Design

- **One program, ops by number.** deadline.spu holds every latency-bound op (`POOL_OP_SYNTH`,
  `POOL_OP_SKIN`; an OpenAL mixer would be the third). A worker runs one job at a time, so the
  ops' buffers overlay in one arena (`pool_arena`, `POOL_ARENA_BYTES` = 120 KB): synth's 115 KB
  (entries, output, sample windows, ext) and skinning's 96 KB (palette, streams, output); each op
  keeps nothing in it between its jobs. What one keeps (synth's cubic and linear tables, 6 KB,
  fetched again only when their EA changes) lives outside. deadline.spu with spu-elf GCC 16.2:
  46.6 KB code, 133 KB data and bss, 176 KB of the 256 KB local store; the ops' stacks are a few
  KB (-fstack-usage: 1.6 KB at most), so ~80 KB stay free (a mixer of ~70 KB, estimate, fits the
  arena as it is). Separate programs with a shared queue are not possible: a queue's workers run
  one program, and a context switch between programs (a reload) costs ~3.2 ms.
- **Deadline order in the runtime.** A queue opened with `SPQ_OPEN_DEADLINE` (the pool only)
  sets `ctl.order`; its workers claim by job instead of at head (speq-abi.h): the submitter
  writes each job's deadline into a new 512-byte array after the ring, then, after `lwsync`, the
  job's flags word as its sequence number; a worker reads the first `SPEQ_SCAN` (16) jobs from
  head and the deadlines, takes the earliest deadline (the earliest queued on a tie) by setting
  `SPEQ_JOB_CLAIMED` in its line with getllar/putllc (the sequence number must still match: the
  slot is reused once head has passed it), then moves head past the claimed jobs at it (any
  worker does; the PPE's drain at close or after the last worker broke claims the same way).
  While the line is being filled it reads as claimed. A deadline is a scheduling hint: a stale
  one orders wrongly, never runs a wrong job. FIFO queues (DXT, bench) claim at head exactly as
  before (the same code path, measured on the PS3); the control block grows by 512 bytes and
  `SPEQ_ABI_VERSION` stays 2 (a program built before the order field claims at head in both
  orders: still correct, every worker of a queue runs one program).
- **Deadlines** are the time each service gives up on its jobs (`spq_submit_dl`, microseconds of
  CLOCK_MONOTONIC): the synth now + `SPEQ_SYNTH_TIMEOUT_US` + 5 us per voice block, a skinning
  mesh now + `SPEQ_SKIN_TIMEOUT_US` + 50 ns per vertex. A job already running isn't preempted, so
  each service keeps its jobs short: synth jobs of at most 128 voice blocks (estimate ~80 us),
  skinning jobs of at most 4096 vertices (new; estimate ~0.1 ms). A job then waits for at most
  one running job per worker and the jobs with earlier deadlines.
- **The pool** (`lib/pool.c`) opens with the first service that needs the SPEs:
  `SPEQ_POOL_SPES` workers (default `SPEQ_DEADLINE_SPES` = 2), never more than the SPEs the
  process's other queues leave (at least one: `SPQ_OPEN_FREE`), so DXT's bulk queue opened first
  (4) and the pool after it make 6; opened the other way round DXT takes 6 - 2 = 4 as before.
  Workers stay resident 200 ms past their last job and free their contexts after a minute (as
  each service had it). Once every worker has broken, the pool is gone for the process and every
  service declines (stays on the PPE).
- **Each service keeps its own policy**: modes, minimums, waits (the synth's no-spin futex
  sleep, skinning's 300 us spin now passed per wait, not per queue), timeouts, the PPE redo or
  decline, the back-off after three failures (per handle). A fault fails only the job that
  faulted; its worker gets a fresh context.

## 3. API and ABI

- Unchanged for callers: `speq_synth_*_v2` (SPEQ_SYNTH_ABI 2), `speq_skin_*_v1`
  (SPEQ_SKIN_ABI 1), `speq_s3tc_compress_v1`, the queue's `speq_*`; no export added or removed
  (debian/libspeq0.symbols as it was). Our fluidsynth and cube2 hooks need no rebuild.
- Environment: `SPEQ_POOL_SPES` (new): the pool's workers. `SPEQ_SYNTH_SPES` and
  `SPEQ_SKIN_SPES` keep a meaning: the most of the pool's workers one synth round or one mesh is
  split across (default 0: all of them); 1 leaves the other worker free for the other service
  during a big round. They no longer open workers of their own.
- Programs: synth.spu and skin.spu are gone, deadline.spu replaces both (ps3-spu-programs; libspeq0
  depends on the same version of it).
- Internals (speq-int.h): `spq_open_ex` (with `SPQ_OPEN_DEADLINE`, `SPQ_OPEN_FREE`),
  `spq_submit_dl`, `spq_deadline`, `spq_pool`, `spq_synth_stats`; the fake backend registers
  every program linked in by name (`SPEQ_PROGRAM`, speq-spu.h), so one test runs DXT's queue and
  the pool side by side as a game does.
- `SPEQ_DEBUG=1`: a line when the pool opens (`speq: deadline pool (deadline.spu): open, 2
  workers of 2 asked`), the services' counters as before.

## 4. Tests (host, 2026-10-17)

`make -C ps3-spu test` (fake backend, 24-core x86 host):

- queue-test, deadline order on bench.spu (a new op, `BENCH_TICKET`, records the order jobs
  start in): 16 jobs queued behind a running one start earliest deadline first, ties in arrival
  order; `speq_submit`'s jobs (no deadline) after those with one; a fault fails its job and the
  queue goes on; close fails the queued jobs (-ECANCELED); 8 submitters x 300 batches with random
  deadlines on 3 workers: every batch's count and error right, every add ran once (25214
  claims, 1223 lost claims, 3 spurious wakes); `SPQ_OPEN_FREE` next to 4 of 6 gives 2 of 3. The
  FIFO checks as before.
- pool-test, one process as a game: DXT's bulk queue opens first (4 workers), then the synth
  and skinning find one pool of 2 (6 contexts on 6 "SPEs").
  - sharing: synth rounds (24 voices, 4 blocks), skinning batches (3000 vertices) and DXT1 images
    (256x256) on three threads for 1.5 s: 3100 rounds, 0 declined, 0 timeouts, mean wait 75 us;
    1700 batches; 400 images; synth within 2.7e-7 of the PPE path's peak, skinning and DXT byte
    for byte;
  - deadlines: eight long jobs (400 us each, a fifth of the 2 ms timeout) queued in the pool at
    all times with later deadlines (+100 ms): 1231 rounds, 0 timeouts, mean wait 100-111 us, max
    421-436 us (one running long job); DXT meanwhile at its own speed (3.6-3.8 ms an image
    against 3.9 alone); the same flood with earlier deadlines: the rounds time out, are redone
    on the PPE (right) and the synth backs off after three, so the order is what protects them;
  - faults: a skinning mesh whose stream EA is unmapped: its batch returns -EIO, both its jobs
    fault (2 faults, the workers get fresh contexts, alive 2), the next 900 batches and 633
    synth rounds right, no synth error.
  Timeouts scale to the machine (20 idle rounds' wait, at least 2 ms) for the package build's
  qemu-user run.
- synth-test, speq-skin-fake --check (spu, auto, ppe), speq-dxt, dlopen-check: as before, the
  synth and skinning now through the pool. test-fluidsynth: see the end of this file.
- Big-endian: the same `make test` in the package build (ppc64 chroot, qemu-user, -O2
  -mcpu=cell): see the end of this file.

Not covered on the host: the runtime's deadline claims on hardware (MFC reservations, the
LLR_LOST wakes with claims that don't touch line 0), the pool's real latencies.

## 5. On the PS3 (to run)

- `speq-bench --quick`: the FIFO queue as before (P1-P5 numbers within noise of 2026-10-16's).
- `speq-skin --check` and `--bench` (skinning through the pool), synth-test isn't installed:
  dsda-doom `play` with `SPEQ_DEBUG=1` is the synth's check.
- Together, one boot, ps3-bench.sh, the user present: dsda-doom `playspu` (fluidsynth on the
  pool) and Sauerbraten `spuskin` each alone, then a run where both load the SPEs: Sauerbraten
  `spuskin` with its textures compressed (DXT on 4 SPEs at load) while dsda-doom plays music in
  another process (two processes: 4 + 2 + 2 contexts, spufs slices them: what the pool can't
  fix across processes; within one process the count is 6). Watch:
  - `speq: deadline pool ... 2 workers` in each log;
  - synth counters: timeouts and rounds redone near 0, mean wait against the first version's
    731 us; ALSA underruns against `playppe`;
  - skin counters: no timeouts or errors, mean wait;
  - `/spu`: `ls /spu` during the run shows 6 contexts per process at most.
- First run, libspeq 0.367 (2026-10-18): `speq-skin --check` through the pool (`open, 2
  workers of 2 asked`): the forced faults failed only their jobs (-EFAULT) and the workers came
  back with new contexts (~3.4 ms on the next job); its 8 failures were the check's float
  tolerance against the SPU's rounding toward zero, not the pool (doc/skin.md section 6).
- If something is off: `SPEQ_POOL_SPES=1` (one worker), `SPEQ_SYNTH=ppe` / `SPEQ_SKIN=ppe`
  (the services without SPEs), `SPEQ_SYNTH_SPES=1` (rounds on one worker).

## 6. Results of the slower host runs (2026-10-17)

- `make test-fluidsynth` (fluidsynth with the hook against unpatched, the generated GM piece):
  `SPEQ_SYNTH=off` identical; `ppe` and `spu` (now through the pool) 130.5-133.9 dB SNR against
  unpatched and each other, sinc identical (left to fluidsynth), as before the pool.
- Big-endian: `make test` in the ppc64 package chroot under qemu-user (`-O2 -mcpu=cell`), every
  program 0 failures. pool-test there calibrated to 3.9 ms a round (timeout 77 ms, flood jobs of
  15 ms): sharing 1732 rounds, 3 timeouts; the flood with later deadlines 1091 rounds, 0
  timeouts; with earlier deadlines 6 of 6 timed out; the fault as on x86.
