# spetest: SPE health tests

`spetest` runs five small test programs on every SPE and prints one pass or fail line per physical SPE. Use it as a baseline for the six SPEs we use, to check the seventh when it is enabled (boot parameter `spu_base.enum_shared=1`), and the eighth if we ever get it. Nothing here touches the PS3 beyond running SPU programs: no flash, no syscon, no fan.

The runner is plain C for the PPE (no libspe2), the tests are SPU C plus one assembly routine, built with our spu-elf toolchain (`make spu-toolchain`). Design and results: [../../docs/SPU.md](../../docs/SPU.md), "The SPE health test".

## Running it on the PS3

The package `ps3-spu` has the runner (`/usr/bin/spetest`), `ps3-spu-programs` has the five programs (`/usr/lib/ps3/spetest/spetest-*.spu`). spufs is mounted at `/spu` with mode 1777 by the `spu.mount` unit, so a normal user can run it; root is not needed. Use it on an idle PS3: it takes every SPE, so no game, no benchmark and no libspeq user (Mesa's texture encoder, fluidsynth) should be running. If another process holds an SPE, that SPE is reported as not covered.

```
make spu-deploy                  # PC: build and install ps3-spu and ps3-spu-programs (ask the user first)
spetest                          # baseline: each test on each SPE once, typically under half a minute
spetest -v                       # the same with decrementer rates and the local store range tested
spetest --rounds 3               # every test three times on every SPE
spetest --passes 5               # each run repeats its test 5 times inside the SPU (default 2)
spetest --tests ls,reg           # only some tests
spetest --stress 10              # all tests on all SPEs for 10 minutes (only when asked, someone watching)
```

From a build tree instead of the packages: `make -C ps3-spu ppe spu OUT=b` (in the ppc64 chroot or `make spu-tests` from the top), then `b/spetest --dir b/spetest.d`.

Safe defaults: without `--stress` the run is a few seconds of light work, each SPE a fraction of a second per test. Stress runs only when asked. Temperatures (the `ps3_sm` hwmon device from kernel patch 0040, Cell BE and RSX) are printed at the start and the end, and a stress run refuses to start, or stops, when a sensor reaches 80 C (`--temp-limit`). Stress without a sensor needs `--no-temp`. The PS3 safety rules still apply: fan in SYSCON automatic mode (Linux sets it at boot), session supervised, stop on any thermal alert.

For the 7th SPE: boot with `spu_base.enum_shared=1`, check that `/sys/devices/system/spu/` lists `spu6`, then run `spetest` as above. SPE 6 gets its own row. The runner takes the SPE numbers from sysfs, so an eighth would appear the same way.

## Reading the report

```
spetest: spufs backend (/spu), 6 SPEs, tests ls reg exec chan mfc, seed 0x5eed, timebase 79.800 MHz
temperatures at start: Cell BE 52.0 C, RSX 47.5 C
round 1: ls@0 reg@1 exec@2 chan@3 mfc@4 ls@5  (6/6 ok)  max 52.0 C
...
results by physical SPE (runs: passed/total)
SPE  ls        reg       exec      chan      mfc
  0  ok 1/1    ok 1/1    ok 1/1    ok 1/1    ok 1/1
  4  ok 1/1    ok 1/1    ok 1/1    FAIL 0/1  ok 1/1
chan on SPE 4: FAIL (0.6 s)
  chan decrementer: ...
SPE 0: PASS
SPE 4: FAIL chan(1/1 failed)
RESULT: FAIL
```

- A round creates one context per SPE and runs one test in each at once. The kernel decides which physical SPE a context lands on, so the runner reads each context's `phys-id` file after it starts (the SPU waits for a go word first, so it is on an SPE by then) and attributes the result to that SPE. It repeats rounds, giving each context the test that most SPEs still lack, until every SPE has run every test `--rounds` times. If no new SPE and test pair shows up for four rounds it gives up and says which are missing.
- `ok n/m` is n passes in m runs. `FAIL`, `CRASH` (the SPU stopped some other way than its exit: halt, invalid instruction, DMA fault) and `HUNG` (it did not finish in `--timeout` seconds, default 30, and was stopped with a signal and destroyed) mark a cell. `-` means that pair never ran.
- A context that moved to another SPE during its run is noted in the details.
- Per SPE, the last block has one line: `PASS`, `FAIL` with the tests, or `INCOMPLETE` for SPEs the run could not cover.
- Exit code: 0 all passed, 1 a failure, 2 setup or usage (no spufs, programs missing), 3 incomplete coverage, 4 stopped by the temperature limit.
- If every SPE fails the same operation the same way, suspect the reference rather than the hardware. The exec test compares each op's results across SPEs as well (a majority vote with three or more SPEs), so an op that differs on one SPE only is flagged even when the PPE reference agrees.

## What each test covers

| Test | What it does | What it catches |
|---|---|---|
| `ls` | march C- over the free local store (about 245 KB: everything above the image and below a 2 KB stack reserve) with six background patterns, an address-in-data march and two random fills; failing addresses reported | stuck, shorted or coupled cells, address line faults |
| `reg` | all 128 registers x 128 bits loaded from a table and stored back (assembly, `regs.S`; every register is saved first and restored) with zeros, ones, 0x55, 0xAA, the register number, unique words, walking one, walking zero and three random tables | stuck or shorted register bits |
| `exec` | 93 operations on 256 pseudo-random quadwords each, results sent to the PPE and compared with its own: integer and logic, halfword and byte ops, shifts, rotates, quadword shifts and rotates, `shufb`, SPU single precision (truncating, no denormals, extended range, in C on the PPE: `spt_sp_fma` in `ref.c`), compares, `cflts`, double precision (`dfa`..`dfnma`, compares, `fesd`/`frds`) including denormal inputs and results; estimates (`frest`, `frsqest`, a divide) by digest only. Also a branch-heavy kernel (jump table, indirect calls, recursion) and an unaligned-access kernel (every size 1 to 16 at every offset), both computed by the PPE from the same C | broken ALU, shifter, FPU or shuffle paths; branch and hint faults; quadword load/store alignment faults |
| `chan` | inbound mailbox (1 to 3 words in flight), outbound and interrupt mailboxes (round trips with the PPE), both signal notification registers (one overwrite, one OR mode, set from the PPE through spufs), channel counts, the decrementer (counts down, wraps, raises its event, rate against the PPE's clock over half a second, 3 % tolerance) | mailbox and signal faults, decrementer clock faults |
| `mfc` | DMA get and put of every size 16 to 16384 in steps of 16 at moving offsets (with guards and a checksum table the PPE verifies), sizes 1, 2, 4, 8 at every natural alignment, 16-byte multiples at every 16-byte offset on both sides, DMA lists (get and scatter), fences and barriers (`putb`, `putf`, `getb`, `getf` ordering), tag status (all, any, immediate, masked), atomics: `getllar`/`putllc` incrementing a counter while a PPE thread increments another word of the same 128-byte line (nothing may be lost), a reservation the PPE takes away (`putllc` must fail), `putlluc` and `putqlluc` | DMA data corruption, size and alignment handling, command ordering, reservation faults |

Not covered: the 2 KB stack reserve and the image itself in the local store (about 11 KB); double-precision rounding modes other than nearest (the FPSCR layout was not checked against hardware); SPE-to-SPE signalling and DMA between local stores; `sndsig`; the isolation and privileged parts; timing.

## Layout

- `runner.c`, `main.c`: option parsing, the rounds, the per-context threads (create, find the physical SPE, go word, answer the SPU's messages, watchdog, kill, verify), the report, temperatures.
- `be_spufs.c`: the spufs backend (`spu_create`, `spu_run`, the files `mem`, `regs`, `mbox`, `ibox`, `wbox`, `signal1/2`, `signal1_type/2_type`, `phys-id`). A hung `spu_run` is stopped with SIGUSR1 and the context destroyed by closing its directory.
- `spt.h`, `proto.h`: the backend interface; the control and result blocks (32-bit words, big-endian) and the message words shared with the SPU.
- `gen.h`, `ref.c`, `ref.h`, `oplist.h`: input generator and kernels shared by both sides, the PPE's reference, and the X-macro list of the `exec` operations.
- `spu/`: `common.c` (start-up, failure list, result), `t_ls.c`, `t_reg.c`, `regs.S`, `t_exec.c`, `t_chan.c`, `t_mfc.c`.
- `sim/`: an interpreter of the SPU instruction set (`spusim.c`, opcode table `spu-opc.inc` generated from binutils' `spu-insns.h` by `gen-opc.sh`), a simulated spufs backend (`be_sim.c`) with fault injection, and `main_sim.c` (the runner on it).
- `tests/run-tests.sh`: the host tests.

## Tests on the PC

`make -C ps3-spu test-spetest SPU_CC=<spu-elf-gcc>`, or `make test ONLY=spetest` from the top (skipped without the SPU compiler; about 30 s). It builds the five SPU programs with our compiler and runs the real runner, with the simulated backend instead of spufs, over those real SPU binaries:

- a clean run on 6 SPEs (every test on every SPE, summary lines, exit 0);
- one injected fault per test, each on its own SPE: a stuck local store bit (reported with its address), a stuck register bit, a wrong `fa` result, flipped DMA data, a slow decrementer; only the faulty SPE may fail;
- a hung SPE (killed after `--timeout`, the rest pass), SPEs held by another process (fewer contexts, exit 3), `--rounds`;
- the temperature logic against a fake hwmon directory: stress refused at 91 C, a short stress run, a stress run aborted when the sensor climbs past 80 C, no sensor;
- setup errors (unknown test, no SPEs, missing programs).

It also passes with the simulator and runner built for big-endian ppc64 and run under qemu-ppc64-static (the runner's byte-order handling is native there).

What this does not show: the simulator moves DMA data at once and has no timing, so ordering faults, DMA timing and real reservation behaviour (`putllc` loss on a PPE store, here emulated by comparing the line) are exercised only on the PS3. Single precision follows the same model as the PPE reference (`spt_sp_fma`), so the sim run proves the plumbing and the SPU code paths, not that the model matches the hardware. Several semantics were written from the ISA documents and not checked on hardware: `rotqmby`/`rotqmbi` (negated counts), `gbh`/`gbb`, `mpyh`, `cflts` scaling, single precision truncation and the `fma` rounding in rare cancellation cases, double precision denormals. A first PS3 run may therefore fail an operation on all six SPEs alike; that points at the model in `ref.c`. The cross-SPE vote and the repeat-run digest are independent of it.
