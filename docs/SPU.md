# The SPEs: libspeq and what runs on them

The PS3's Cell chip has six SPEs (synergistic processing elements, the small vector cores, also called SPUs) that Linux can use. We run three things on them today, all through one userspace library, libspeq: the DXT1/DXT5 texture encoder behind Mesa, fluidsynth's voice DSP, and an optional skinning path for Sauerbraten. The texture encoder and the synth are in use and measured; skinning is selectable and gives no gain. This page covers the runtime, those users with their switches, the measurements, and how to run the tests. Sound is in [SPU-AUDIO.md](SPU-AUDIO.md), the survey of what else could move is in [research/SPU-OFFLOAD.md](research/SPU-OFFLOAD.md), unbuilt work is in [backlog/spu.md](backlog/spu.md).

Terms (SPE/SPU, PPE, XDR, VRAM, libspeq, DXT1/DXT5) are in the [glossary](README.md#glossary).

## What libspeq is

libspeq (`ps3-spu/lib`, packages `libspeq0` and `libspeq-dev`) is a client library plus a small resident runtime for the SPEs, used like a thread pool. A program opens a queue, libspeq starts one worker per SPE and keeps it there, and the program submits jobs and waits for their completion words. The kernel's spufs file system already schedules SPE contexts across processes, so nothing here is kernel code. The kernel itself cannot use SPEs (there is no in-kernel spufs client), which is why the validator, zram and the VRAM swap stay on the PPE and the RSX.

How the pieces work:

- **Resident workers.** One spufs context per SPE, created with `SPU_CREATE_EVENTS_ENABLED` (without it an SPU DMA fault sends SIGSEGV to the whole process, fatal inside Mesa). Each context has a thread blocked in `spu_run`. A faulted context cannot be restarted, so libspeq closes and recreates it (3.4 ms).
- **Dispatch without syscalls.** The SPU waits on a 128-byte job line in process memory with an atomic reservation (`getllar`/`putllc`). The PPE's submit stores to that line and the SPU wakes. The queue word packs `tail|parked`, so one `lwarx`/`stwcx.` takes the slot and clears `parked`. The SPU fetches a job line before its claiming `putllc`, otherwise the PPE could reuse the slot.
- **Completion.** The SPU DMA-writes a done word. `speq_wait` spins 80 us, then sleeps on a futex through newlib's stop code 0x2104 (the whitelisted syscalls futex, write, nanosleep and getpid run on the worker's thread inside `spu_run`). Stops 0x2100 and 0x2101 (C99 and POSIX stdio) need a PPE server that nothing provides yet.
- **Parking.** A worker with no work parks after `SPEQ_IDLE_US` (10 ms by default; the deadline pool uses 200 ms) and frees its context after `SPEQ_FREE_MS` (5 s; a minute in the pool). The decrementer ticks at the 79.8 MHz timebase, so this runs on time.
- **SPU programs are files** in `/usr/lib/ps3/spu` (package `ps3-spu-programs`), not embedded in the library. `ps3-spu` is built twice: the SPU half with our spu-elf toolchain on the PC, the PPE half in the ppc64 chroot. An ABI word (`.speq_abi` section, `SPEQ_ABI_VERSION`, now 2) keeps the halves in step. The toolchain is described in [SPU-GCC.md](SPU-GCC.md).
- **Mesa holds no policy.** Its hook (`mesa/patches/0011-s3tc-libspeq.patch`, extended by `0032-s3tc-dxt5-libspeq.patch`) is about 30 lines that `dlopen` `libspeq.so.0` and call `speq_s3tc_compress_v1`. Sizes, the PPE/SPU choice and the debug switches live in libspeq, so tuning never needs a Mesa rebuild (12 to 25 minutes). Mesa has no package dependency on libspeq. Fluidsynth and Sauerbraten load it the same way.

### Queues: four bulk SPEs and a shared deadline pool

There are two kinds of queue, and a process never holds more SPE contexts than there are SPEs.

- **The bulk queue** takes the SPEs nobody else holds but leaves two (`SPEQ_DEADLINE_SPES`). In a game with the pool open that is 4. It runs DXT jobs, which are long (a 1024x1024 image is about 0.46 ms per worker, a 2048x2048 about 1.8 ms) and are served first come, first served.
- **The deadline pool** is one program, `deadline.spu`, with 2 workers (`SPEQ_POOL_SPES`, default 2) shared by every latency-bound service in the process: fluidsynth's synth and skinning today, an OpenAL mixer if one is ever built. Each job carries a deadline, and a worker takes the earliest among the first 16 queued jobs. A running job is not preempted, so each service cuts its work short (synth jobs of at most 128 voice blocks, skinning jobs of at most 4096 vertices). The ops' buffers overlay in one 120 KB arena, since a worker runs one job at a time. `deadline.spu` is 46.6 KB of code and 133 KB of data and bss, 176 KB of the 256 KB local store.

The decision (the user, 2026-10-17): one shared deadline pool of 2 SPEs for every latency-bound service, DXT keeps its 4 bulk SPEs. The reason is spufs: past 6 contexts it time-slices in 80 ms slices, far beyond an audio block (1.45 ms) or a frame. The first design gave each service its own queue, and a game with music, skinning and compressed textures would have run 4 + 2 + 2 = 8 contexts on 6 SPEs. A separate program per service with a shared queue was not possible either: a queue's workers run one program, and switching programs costs about 3.2 ms. Other processes' SPE use still time-slices; the services' timeouts and PPE fallbacks cover that. The full design, tests and the PS3 checks are in [ps3-spu/doc/pool.md](../ps3-spu/doc/pool.md).

## Users and their switches

All switches are environment variables read by libspeq. `SPEQ_DEBUG=1` prints a line when a queue or the pool opens and counters every 10 s and at exit, per service: jobs, mean and max wait, refusals, timeouts, errors, back-offs.

| Switch | Values | What it does |
|---|---|---|
| `SPEQ_S3TC` | `auto` (default), `spu`, `ppe`, `off` | texture encoder behind Mesa |
| `SPEQ_S3TC_RGB` | `ppe`, `off` | applies to RGB (3-byte) sources only; `ppe` sends them to the PPE encoder |
| `SPEQ_S3TC_MIN` | 64 (64x64 pixels) | smallest image sent to the SPEs; smaller mip levels stay on the PPE |
| `SPEQ_SYNTH` | `auto` (default), `spu`, `ppe`, `off` | fluidsynth's voice DSP. `auto` takes rounds with at least 4 voices (`SPEQ_SYNTH_MIN`); `ppe` runs libspeq's DSP on the calling thread (to tell hook bugs from SPU bugs); `off` is stock fluidsynth |
| `SPEQ_SKIN` | `auto` (default), `spu`, `ppe`, `off` | Sauerbraten's skinning, if the engine's hook is used; `auto` declines batches under `SPEQ_SKIN_MIN` (256) vertices |
| `SPEQ_POOL_SPES` | 2 | workers in the deadline pool |
| `SPEQ_SYNTH_SPES`, `SPEQ_SKIN_SPES` | 0 = all | most pool workers one synth round or one mesh is split across; they open nothing |
| `SPEQ_SPIN_US`, `SPEQ_IDLE_US`, `SPEQ_FREE_MS` | 80, 10000, 5000 | queue tuning |
| `SPEQ_DEBUG` | `1` | counters, see above |

If something misbehaves, `SPEQ_POOL_SPES=1`, `SPEQ_SYNTH=ppe`, `SPEQ_SKIN=ppe` and `SPEQ_S3TC=ppe` each take one piece off the SPEs while keeping the hook's code path, and `off` removes it.

### Texture compression (DXT1 and DXT5)

NV47 samples S3TC natively and Mesa exposes `GL_EXT_texture_compression_s3tc`. A game that asks for a compressed format used to make Mesa compress on the PPE with its own encoder at 2.6 Mpixel/s, which is why compressed textures were off. libspeq has a DXT encoder (van Waveren's real-time method, as Sony's PlayStation Home used on the SPEs) that runs on the SPEs and, for small images, on the PPE.

What it does:

- **Entry point.** On nv30, `glTexImage` reaches `tx_compress_dxtN()` through `_mesa_texstore_*dxt*` in `src/mesa/main/texcompress_s3tc.c`, never `util_format_dxt*_pack`. The destination is a fresh, page-aligned GART buffer, a PFNMAP mapping the SPEs can write. Mesa writes DXT blocks little-endian byte by byte, and alpha at or below 127 is transparent (punch-through).
- **DXT1** (SPU `dxt.spu`): byte-identical to the reference on 37856 test blocks, 135 cycles per block, **379 Mpixel/s per SPE** (RGBA 335), 1173 Mpixel/s on six SPEs, against 6.5 Mpixel/s for libspeq's own PPE encoder and 2.6 for Mesa's. Through `glTexImage` a 1024x1024 RGBA image runs at 352 Mpixel/s: the encoder is not the limit, dispatch and the output page faults are. The PPE encoder is also better than Mesa's on OpenArena's textures (mean RMSE 4.23 against 4.29, better on 37 of 50).
- **DXT5** (Mesa 0032, libspeq 0.367): generic compressed RGBA requests (`GL_COMPRESSED_RGBA`, `RGBA_DXT5`) go to libspeq too. Details and the alpha block in [ps3-spu/doc/dxt5.md](../ps3-spu/doc/dxt5.md).
- **RGB sources** (libspeq 0.394): a 3-byte source used to fall to the PPE encoder at 165 ms for a 1024x1024 (43 ms for 512x512). The SPEs now take them: **1.0 ms per 1024x1024 on 4 SPEs against about 175 ms on the PPE**. Details in [ps3-spu/doc/dxt-rgb.md](../ps3-spu/doc/dxt-rgb.md). No game in the suite has exercised this path: Sauerbraten's demo uploads no RGB DXT1 (rgbspu and rgbppe came out identical at 39 fps, fps.csv row for "ioquake3 ps3.396 ... libspeq 0.394"), and Teeworlds' DXT5 has not shown a `SPEQ` line in a log yet.

What shipped: OpenArena runs with `r_ext_compressed_textures 1` (set in ps3-desktop's `autoexec.cfg`). It compresses only opaque textures, as RGBA DXT1. With the SPU encoder (libspeq 0.210, `SPEQ_S3TC=auto`, 5 runs each, interleaved): level load **13.84 s uncompressed against 13.63 s compressed** (the fewer bytes to upload pay for the encoding), game VRAM 51.7 to 37.5 MB (**14 MB less**), fps 49.1 to 52.7, longest frame unchanged within the noise. One level load made 1476 compression calls with `SPEQ_DEBUG=1`: 254 on the SPEs (every texture from 64x64 up, no failures), the rest small mip levels on the PPE (under 64x64, tens of us each) or, for 1x1 and 2x2, Mesa's own encoder. The queue opens once per game (67 ms). These OpenArena numbers come from step 0's harness, which had a display problem (see "Measured: step 0 and the harness" below); the VRAM figures do not depend on the display, and the load-time comparison has not been repeated through the suite.

Compression at upload for every game, not only those that ask for it, is a separate Mesa switch, `NV30_FORCE_DXT=off|opaque|all` (patch 0038, off by default, checked on softpipe). It uses this encoder; its PS3 A/B is in [MEMORY.md, "Texture compression at upload"](MEMORY.md) and is scheduled in [TESTS.md](TESTS.md).

### fluidsynth's voice DSP

The per-voice DSP of fluidsynth 2.6.1 (sample interpolation, the resonant filter, the mix into the output buffers) renders on the pool, one round trip per write request, in single precision with 4-wide vectors. fluidsynth's control code (envelopes, LFOs, allocation, SoundFont lookups, reverb and chorus) stays on the PPE. Our package `fluidsynth/` carries the patch (`fluidsynth/patches/0001-rvoice-voice-DSP-through-libspeq-on-the-PS3-s-SPEs.patch`), libspeq the `speq_synth_*_v2` calls. `SPEQ_SYNTH=auto` is the default, so installing `libspeq0` and our fluidsynth is enough. The user of it is dsda-doom (music), and anything else playing MIDI through fluidsynth (OpenTTD's music, SDL_mixer MIDI).

Result on the PS3 (dsda-doom `play`, 60 s): CPU 44.5 to 31.8 s a minute (no music at all: about 29 s), ALSA underruns about halved, and the user, listening, called it "night and day". The music still chops: 465 underruns a minute on the pool against 1095 on the PPE, against 18 with no music. Design, host tests, and the open question are in [SPU-AUDIO.md](SPU-AUDIO.md).

### Sauerbraten's skinning

Sauerbraten skins its skeletal models on the PPE (GPU skinning on nv30 turns the bone lookups into if/else trees: 11 fps against 28). libspeq has a skinning op (`speq_skin_*_v1`, `SPEQ_SKIN_ABI` 1, dual-quaternion palettes, one SPU pass per mesh, run on the pool) and cube2 patch `0006-CPU-skinning-through-libspeq-on-the-PS3-s-SPEs.patch` hooks it in. The modes are `SPEQ_SKIN=auto|spu|ppe|off`; the engine's own loop remains the default in the package.

Measured (Sauerbraten spawn and gun, one run each, same boot 2026-10-18; fps.csv row "Luanti ps3.364 batching, Neverball ps3.370 batching, Sauerbraten libspeq 0.381 skinning (spawn)"):

| Mode | steady fps | stalls |
|---|---|---|
| `ppe` (libspeq's vector code on the calling thread) | 28.1 | none |
| `spu` | 23.6 | 21 (4.3 s) |
| engine's own loop (`off`) | 23.0 | none |

So libspeq's PPE path is the fastest, and the SPU path adds hitches. The expected gain was small anyway (about 1% on map cave, where only the hud gun is skinned, around 3200 vertices). Skinning stays selectable and off the default. Design and tests: [ps3-spu/doc/skin.md](../ps3-spu/doc/skin.md).

## Measured: step 0 and the harness

All PS3 measurements go through `ps3-bench.sh` (`ps3-gpu-tools/scripts/ps3-bench.sh`): desktop stopped, caches dropped, swaps emptied and restored (the VRAM swap with a discard), 0.5 s samples in `/dev/shm`, a thermal-alert watchdog, bare-X runs, and a bench lock that `make deploy` respects. The OpenArena step 0 scripts are `games/openarena/step0.sh`, `oa-profile.sh` and `step0-report.py`. Since the game suite exists ([games/SUITE.md](games/SUITE.md)), game-level numbers come from `games/bench/` with the protocol of [TESTS.md](TESTS.md).

**Which step 0 numbers are void.** Step 0 (2026-10-16) ran each game through `ps3-bench.sh x` with no window manager. SDL's fullscreen request went unanswered, and ps3rsx left the video muted for every X started after another had quit (fixed in a1f1b91). The game rendered, but its frames never reached the screen. So every game number from step 0 is invalid: its fps, load times, the compressed-texture comparison on Mesa's encoder, and the sound variants. They are to be measured again through `games/bench`. What does not depend on the display stands: everything from `speq-dxt`, `speq-bench` and `gl-dxt`, the VRAM sizes (51.6 to 37.5 MB), and DXT correctness on the RSX.

What step 0 established that still holds:

- DXT1 RGB and RGBA sample on the RSX exactly as decoded (max difference 1, no graph exception) (`gl-dxt`, first DXT sampled on this hardware).
- Mesa's encoder runs 2.6 Mpixel/s on the PPE, which is why compression was off. libspeq's PPE encoder, behind the Mesa hook since Mesa 26.2.4-1+ps3.186, does 6.5 Mpixel/s through `glTexImage` (7.1 alone, 148 ms per 1024x1024).
- On Mesa's encoder, compressed textures cost 1.1 to 1.5 s of load and added 150 to 300 ms to the longest frame (models loaded during play were compressed mid-frame), so they were not shipped on it. The SPU encoder removes both.
- Sound in OpenArena cost about 10% of the frame rate and no OpenAL or game setting changed that (a stock OpenAL 49.7 fps, sound off 55.3). Those fps numbers are void, but the profile that followed (OpenAL Soft's share of a game's samples, measured through the suite) is valid: [SPU-AUDIO.md](SPU-AUDIO.md).

### Measured: libspeq on the SPEs (`speq-bench`, 2026-10-16)

`speq-bench` through `ps3-bench.sh`, desktop stopped, kernel .178, 6 SPEs. These do not depend on the display.

| Measure | Result |
|---|---|
| Wake through the reservation (P1) | a PPE store to the reserved line wakes an idle SPU in 0.8 to 1.3 us (p99 1.9); a store to the next line wakes nothing |
| Submit to idle workers | 1 SPE p50 2.6 us, p99 6; 6 SPEs p50 9.8, p99 65. With 6 every submit wakes all of them (10.6 wakes and 3.3 lost claim races per job): the herd makes the tail, not the wake. Fine for jobs of 100 us and up |
| Completion (P2) | spinning 2 us; a futex sleep about 62 us more; ibox about 35 us |
| Costs (P3) | open 1.6 ms for one worker, about 15 ms for six (67 to 80 ms the first time in a game, cold); 320 KB of kernel vmalloc per context, returned at close; a parked worker resumes in about 90 us, a freed one in about 3.2 ms; the SPU's getpid through 0x2104 takes 8 us |
| Faults | an SPU DMA to an invalid address fails that job with EFAULT in 1 ms, the worker is recreated in 3.4 ms, nothing in the kernel log |
| Bandwidth (P5) | one SPE gets 12 to 14 GB/s and puts 18 to 22 GB/s to XDR; six together about 20 to 23 GB/s, the memory's limit |

## The SPE health test

`spetest` (package `ps3-spu`, sources in [ps3-spu/spetest](../ps3-spu/spetest/README.md)) checks every SPE with five small programs and prints a pass or fail line per physical SPE: the local store (march C- and address-in-data patterns over about 245 KB), the 128 registers, about 90 operations of the execution units checked against the PPE's own results (integer, shifts, shuffles, single and double precision, a branch kernel, unaligned accesses), the channels (mailboxes, both signal registers, the decrementer's rate against the PPE's clock) and the MFC (every DMA size, alignments, lists, fences, tag status, `getllar`/`putllc` against PPE stores). It gives a baseline for the six SPEs we use, a check of the seventh with `spu_base.enum_shared=1`, and the eighth if we ever get it; `--stress MIN` runs all tests on all SPEs at once for MIN minutes, refuses to start and stops at 80 C by the `ps3_sm` hwmon temperatures, and never runs unless asked.

How it works: one spufs context per test, as many at once as there are SPEs; each SPU program waits for a go word, so the runner can read the context's `phys-id` first and attribute the result to the physical SPE; hung contexts are stopped with a signal after a timeout and destroyed. Rounds repeat until every SPE has run every test. The `exec` results are also voted across SPEs, so one SPE that disagrees with the rest fails even when the PPE's reference agrees.

State: built and tested on the PC only. The runner and the real SPU programs run against an SPU instruction set interpreter (`ps3-spu/spetest/sim`, written for this) with injected faults, also as big-endian ppc64 under qemu; `make test ONLY=spetest`. Not run on a PS3 yet: the first run is in [TESTS.md](TESTS.md), "SPE health baseline". The interpreter has no timing and moves DMA data at once, and its single precision follows the same model as the reference, so ordering faults, reservation behaviour and the model itself are checked only by hardware. If the first run fails one operation on all six SPEs alike, suspect the reference first.

## Known facts to keep in mind

- An SPU write to a page the process has not touched costs 40 to 87 us per 4 KiB, because the worker thread faults it in. Pre-touch outputs; libspeq's s3tc does.
- A DMA to a page that is swapped out is a USB-disk swap-in on the worker's thread. Services touch their input windows on the PPE first (the fluidsynth hook touches every sample window).
- Opening the queue is not free (see the table): open it once per process, not per job.
- A futex sleep costs about 62 us over spinning, so short jobs spin (80 us) and audio waits use `SPEQ_WAIT_NOSPIN` (the mixer thread shares the PPE core with the game through SMT, so a spin takes cycles from the game).
- Linux gets 6 SPEs (`spu_base.enum_shared=0`). A 7th by boot parameter is a hardware-side item: [backlog/spu.md](backlog/spu.md).
- The SPU rounds single-precision results toward zero and flushes denormals; host tests round to nearest, so comparisons use SNR where bit equality is impossible.
- `perf` on the PS3 needs `--proc-map-timeout=20000`; with the default 500 ms it attaches before it has read a big process's maps and files most samples under `[unknown]` (77% of OpenArena's, 56% of dsda-doom's in the first profiles). `perf record -a -g` hung the PS3: use `-p <pid>`, `-F 999` and `timeout`, and run `perf report` as the owner of `perf.data`, with `-g none` for flat sorts. See [PROFILING.md](PROFILING.md).
- Opening spufs contexts: `spu_run` takes no argument, registers go through the context's `regs` file; a faulted context returns the status word with the event in its third argument, not `-EFAULT`. With no more contexts than SPEs nothing is preempted.
- Harness traps: in the bench harness `swapon -a` restores nothing here (fstab swap is `noauto`; the `ps3-swap` and `ps3-vswap` services enable zram0 and ps3vswap0). Restore by hand with `swapon -p 300 --discard /dev/ps3vswap0` (frees stale VRAM slots) and `swapon -p 100 /dev/zram0`. xinit runs a client only when its path starts with `/`. OpenArena archives `+set` cvars to `q3config.cfg` on quit.

## How to run things

Build: `make ps3-spu-deb` (also `mesa-deb`, `ps3-gpu-tools-deb`). The SPU toolchain is `make spu-toolchain` ([SPU-GCC.md](SPU-GCC.md)).

Host tests, no PS3 needed:

- `make -C ps3-spu test`: the fake backend (`SPEQ_FAKE=1`, runtime and ops on host threads) with queue-test, pool-test, synth-test, `speq-skin --check`, `speq-dxt`, dlopen-check; the same inside the package build under qemu on ppc64, big-endian. Also `make test ONLY=ps3-spu` from the top.
- `make -C ps3-spu test-spetest` (also `make test ONLY=spetest`): spetest's runner against an SPU simulator, running its real SPU programs with injected faults ([spetest/README.md](../ps3-spu/spetest/README.md)); needs the SPU compiler.
- `make -C ps3-spu test-mesa`: our encoder against Mesa's on an OpenArena corpus.
- `make -C ps3-spu test-fluidsynth`: fluidsynth with the hook against the unpatched one, on a generated General MIDI piece ([SPU-AUDIO.md](SPU-AUDIO.md)).

Deploy: `make deploy PKGS="libspeq0"` (upgrades `ps3-spu` and the programs too). Kernels only with `KERNELS=1` or named.

On the PS3, as `tibi` over SSH, with the user not at the console:

- `ps3-bench.sh run LABEL -- cmd`, `ps3-bench.sh x LABEL -- client`, and `ps3-bench.sh desktop` at the end; results in `/var/tmp/ps3-bench/`.
- `spetest [-v] [--rounds N]` for the health of every SPE (idle PS3, no game or benchmark running), `speq-dxt --check --path spu`, `speq-dxt --bench`, `speq-bench [--quick] [--fault]`, `speq-skin --check`, `SPEQ_DEBUG=1 gl-dxt`, `spurun <prog.spu> [symbol]`.
- Game-level runs: `/var/tmp/bench/run.sh ...` after `make bench-push`, see [games/SUITE.md](games/SUITE.md). dsda-doom's recipe has the `playspu`, `playppe`, `playoff` and `playnomusic` variants for the synth; Sauerbraten's has `spuskin`, `ppeskin` and `offskin`.

Rules that come with SPU measurements: game-level decisions need the suite, not one game; every PS3 measurement is a same-boot comparison against the off-switch; all-SPE load is capped (60 s per run, 30 s gaps) with the user present and webMAN's fan in SYSCON mode.

## Decisions and why

- **Userspace only** (the user, 2026-10-16). spufs already schedules contexts across processes, and the kernel cannot use SPEs.
- **VRAM counts as scarce.** It doubles as swap (ps3vswap, up to 96 MB) and later games are heavier than OpenArena, so VRAM savings from DXT are memory wins even when RAM numbers do not move.
- **DXT first, audio second** (after step 0). Vorbis on SPUs was dropped (OpenArena's sounds are WAV).
- **No SPU mixer for OpenAL Soft** (2026-10-17): measured at 3.7 to 4.7% of a game's samples, under the 6% bar. [SPU-AUDIO.md](SPU-AUDIO.md) and [research/OPENAL-SPU-MIXER.md](research/OPENAL-SPU-MIXER.md).
- **Order by what the CPU-bound games spend** (2026-10-17). Most of a CPU-bound game's PPE time is the graphics stack (Mesa's per-draw work, ps3rsx validation): branchy and microsecond-sized, it stays on the PPE and is cut in Mesa instead. The SPE wins are the data-parallel work next to it; the user chose fluidsynth and OpenAL first. Both libraries became our own debsrc packages (`fluidsynth/`, `openal-soft/`) so their debug symbols match what runs.
- **One shared deadline pool of 2 SPEs, 4 bulk for DXT** (the user, 2026-10-17): described above.

Older versions and the dated history: [archive/SPU-OFFLOAD-PLAN.md](archive/SPU-OFFLOAD-PLAN.md), [archive/SPU-OFFLOAD-SURVEY.md](archive/SPU-OFFLOAD-SURVEY.md).
