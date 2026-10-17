# SPU offload plan: status (parked 2026-10-16)

The working plan behind SPU-OFFLOAD-SURVEY.md, as it stood when it was parked so the game suite
and its driver work (GAME-SUITE.md) could come first. Measured results are in
SPU-OFFLOAD-SURVEY.md ("Measured: step 0", "Measured: libspeq on the SPEs"); the audio design is
SPU-AUDIO.md. Rule from the user: game-level decisions are re-checked across the suite, not one
game.

## Done

| Milestone | What | Where |
|---|---|---|
| Harness | `ps3-bench.sh`: desktop stopped, caches dropped, swaps emptied and restored (VRAM swap with a discard), 0.5 s samples in /dev/shm, thermal-alert watchdog, bare-X runs, bench lock that `make deploy` respects | `ps3-gpu-tools/scripts/ps3-bench.sh`, `games/openarena/step0.sh`, `oa-profile.sh`, `step0-report.py` |
| A: step 0 | DXT1 samples right on the RSX; OpenArena compressed textures save 11-14 MB of VRAM; sound costs ~10% of the frame rate and no setting helps | SPU-OFFLOAD-SURVEY.md |
| B: libspeq + PPE encoder | `libspeq0`/`libspeq-dev`; van Waveren DXT1 encoder (better than Mesa's on OpenArena textures, 2.4x its speed on the PPE); Mesa hook `mesa/patches/0011-s3tc-libspeq.patch` (dlopen, all policy in libspeq: `SPEQ_S3TC`, `SPEQ_DEBUG`) | `ps3-spu/lib`, `ps3-spu/tools/speq-dxt.c`, `ps3-gpu-tools/mesa-tests/gl-dxt.c` |
| C: SPU runtime | resident workers (one per SPE, `SPU_CREATE_EVENTS_ENABLED`), reservation-line queue, park/free, futex completion via stop 0x2104, fault recovery; `speq-bench` P1-P5 measured on hardware | `ps3-spu/spu/runtime.c`, `ps3-spu/lib/queue.c`, `ps3-spu/tools/speq-bench.c` |
| D: SPU DXT1 | `dxt.spu` (SIMD, byte-identical, 379 Mpixel/s per SPE, 1173 on six); `SPEQ_S3TC=auto` default; OpenArena compressed textures load as fast as uncompressed (13.63 vs 13.84 s), 14 MB less VRAM | `ps3-spu/spu/dxt.c`, `dxt-op.c`, `ps3-spu/lib/s3tc.c` |
| Shipped | OpenArena `r_ext_compressed_textures 1` (ps3-desktop autoexec.cfg) | |

## Parked, in the order they would come back

1. **Re-check across the game suite** (GAME-SUITE.md). Step 0's game numbers and D's OpenArena
   load times are invalid (nothing reached the screen, SPU-OFFLOAD-SURVEY.md "Measured: step 0"):
   measure them again through games/bench first. Then the compressed-texture default and the
   SPU encoder in every game that compresses (OpenArena, Sauerbraten after its one-line patch,
   games asking for generic compressed formats); `SPEQ_DEBUG` shows which path each took.
2. **DXT5 in libspeq** (SPU and PPE): Mesa turns generic compressed RGBA into DXT5; today those
   fall back to Mesa's slow encoder (Teeworlds, TORCS per GAME-SUITE.md). Same structure as DXT1
   plus the alpha block (8-entry palette, 48-bit indices).
3. **Audio** (SPU-AUDIO.md): first the free measurements: re-profile with `libopenal1-dbgsym`
   (step 0's profile left the mixer thread unresolved), variants B16 (int16 without limiter and
   dither) and F44 (44.1 kHz device and dmix), across the suite's OpenAL games. Go only if voice
   mixing is >= 6% of a game's samples. Then `speq_mix_v1` (one job per mixer update) and the
   openal-soft debsrc patch. Decide first: one shared queue per process for DXT and audio, or
   audio 1 SPE and DXT 5. Vorbis on SPUs: not worth it (OpenArena's sounds are WAV).
3b. **MIDI synthesis (fluidsynth) on the SPUs** (the user, 2026-10-17). Measured: dsda-doom
   playing its demo at normal speed used 53 s of CPU in 60 s with fluidsynth music and 29 s
   without: fluidsynth alone ~40% of a PPE thread, and ALSA underruns went from 18 to 1280 (audible
   chopping). The work is per-voice interpolation, envelopes, filters and mixing into float
   blocks: SIMD-friendly, block-sized, the same shape as the OpenAL mixer job (one SPU job per
   synth block, voices split across SPEs). Users: dsda-doom, OpenTTD's music, SDL_mixer MIDI.
   Measure first with a perf profile of libfluidsynth3 (dbgsym) to see the split between voice
   rendering and the rest.
4. **Queue improvements** from speq-bench: per-worker doorbells (a submit to six idle workers
   wakes all of them: 8.6 spurious wakes per job, p99 65 us); a `speq_wait` timeout and a
   no-spin mode (audio needs both); the stop 0x2100/0x2101 printf server and spurun on libspeq.
5. **Probes E16/E8b** (RSX-SPU-FEASIBILITY.md): SPU puts straight into VRAM; only when a
   consumer would write VRAM (decompression into textures, meshing into VBOs).
6. **Meshing**: ClassiCube chunks on SPUs (showcase), after E16.

## Known facts to keep in mind

- SPU writes to a page the process hasn't touched cost 40-87 us per 4 KiB (the worker thread
  faults it in): pre-touch outputs (libspeq's s3tc does).
- Opening the queue costs 1.6 ms for one worker, ~15 ms for six (67-80 ms the first time in a
  game, cold); a parked worker resumes in ~90 us, a freed one in ~3.2 ms; 320 KB of kernel
  vmalloc per context.
- A futex sleep costs ~62 us over spinning; `speq_wait` spins 80 us.
- Linux gets 6 SPEs (`spu_base.enum_shared=0`); XDR bandwidth ~20-23 GB/s with all six.

## Context for picking it back up

### Decisions (with the user, 2026-10-16) and why

- **SPU work is userspace only.** spufs (kernel) already schedules SPU contexts across processes;
  libspeq is a client library plus a resident SPU runtime, like a thread pool. The kernel can't
  use SPEs (no in-kernel spufs client), so validator, zram and vswap stay on the PPE/RSX.
- **VRAM counts as scarce**: it doubles as swap (ps3vswap, up to 96 MB) and future games are
  heavier than OpenArena, so VRAM savings (DXT) are memory wins even when RAM numbers don't move.
- **DXT first, audio second**, after step 0. Audio stays a candidate (~10% of OpenArena's frame
  rate); Vorbis on SPUs was dropped (OpenArena's sounds are WAV).
- **Mesa holds no policy.** The hook is ~30 lines that dlopen `libspeq.so.0` and call
  `speq_s3tc_compress_v1`; sizes, PPE/SPU choice and debug switches live in libspeq, so tuning
  never needs a Mesa rebuild (12-25 min). Mesa has no package dependency on libspeq.
- **SPU programs are files** in `/usr/lib/ps3/spu` (package ps3-spu-programs), not embedded:
  ps3-spu builds `-A` on amd64 with our spu-elf toolchain and `-B` in the ppc64 chroot. An ABI
  word (`.speq_abi` section, `SPEQ_ABI_VERSION`) keeps the halves in step.
- **Every PS3 measurement through ps3-bench.sh** (desktop stopped, swaps emptied), never during
  a deploy; all-SPE load capped (60 s per run, 30 s gaps), the user present, webMAN fan on
  SYSCON preferred over a fixed 60%.

### Facts from source (reviews of the plan against the kernel, newlib, Mesa)

- spufs: create contexts with `SPU_CREATE_EVENTS_ENABLED`, or an SPU DMA fault sends
  SIGSEGV/SIGBUS to the whole process (fatal inside Mesa). `spu_run` returns the status word
  with the event in its third argument (not -EFAULT); a faulted context can't be restarted
  (close and recreate). `spu_run` takes no argument: registers go through the context's `regs`
  file (crt0: `main(r3, r4, r5)`, r6 saved as `__ea_local_store`). Stop 0x2104 runs whitelisted
  syscalls (futex, write, nanosleep, getpid) on the worker thread inside `spu_run`; 0x2100 (C99
  stdio) and 0x2101 (POSIX) need a PPE server, which nothing provides yet. With no more contexts
  than SPEs nothing is preempted; past that, 80 ms slices.
- Queue protocol: `tail|parked` packed in one word (the submit's lwarx/stwcx. takes and clears
  `parked`), the SPU fetches a job line *before* its claiming putllc (or the PPE can reuse the
  slot), `pending|waiters` in one word per done line, acquire `lwsync` after completion.
- Mesa: glTexImage on nv30 reaches `tx_compress_dxtN()` through `_mesa_texstore_*dxt*`
  (`src/mesa/main/texcompress_s3tc.c`), never `util_format_dxt*_pack`; the destination is a
  fresh, page-aligned GART BO, stride 64-aligned, a PFNMAP mapping the SPEs can write. Mesa writes
  DXT blocks byte by byte little-endian; alpha <= 127 is transparent (punch-through). OpenArena
  (renderergl1) compresses only opaque textures, as `RGBA_DXT1`, from RGBA, one glTexImage per
  CPU-made mip level; its sources are usually 8 bytes past a 16-byte boundary (hunk allocator).
  Generic compressed RGBA becomes DXT5 (not encoded by libspeq yet).
- Swap in the harness: `swapon -a` restores nothing here (fstab swap is `noauto`; ps3-swap and
  ps3-vswap services enable zram0 and ps3vswap0); restarting ps3-vswap fails once its backing is
  set. Restore by hand: `swapon -p 300 --discard /dev/ps3vswap0` (frees stale VRAM slots),
  `swapon -p 100 /dev/zram0`.
- xinit runs a client only when its path starts with `/`; OpenArena archives `+set` cvars to
  q3config.cfg on quit (the harness backs it up per session).

### Step 0 caveats found later (SPU-AUDIO.md)

- Variant B (`sample-type=int16`) also turned on OpenAL Soft's output limiter and dithering,
  which likely explains why it cost more; B22 probably still ran at 48 kHz (ALSA dmix is fixed
  at 48 kHz, OpenAL disables ALSA's resampler). HRTF was never on. ppc64 has no SIMD mixer
  (`Mix_C`, `Resample_Cubic_C`).
- perf: `-a -g` hung the PS3; use `-p <pid>`, `-F 999`, `timeout`. Run `perf report` as the
  owner of perf.data (not sudo), with `-g none` for flat sorts; the step 0 profiles left OpenAL's
  mixer thread as `[unknown]`.

### How to run things

- Build: `make ps3-spu-deb` (also `mesa-deb`, `ps3-gpu-tools-deb`); host tests:
  `make -C ps3-spu test` (fake backend `SPEQ_FAKE=1`, runtime and ops on host threads),
  `make -C ps3-spu test-mesa` (our encoder against Mesa's on an OpenArena corpus).
- Deploy: `make deploy PKGS="libspeq0"` (upgrades ps3-spu and programs too); kernels only with
  `KERNELS=1` or named. The user allowed `make deploy` on 2026-10-16 (the auto-mode classifier
  blocked it before that).
- On the PS3 (as tibi over SSH):
  - `ps3-bench.sh run LABEL -- cmd`, `ps3-bench.sh x LABEL -- client`, `ps3-bench.sh desktop`
    at the end; results in `/var/tmp/ps3-bench/`, summaries with `games/openarena/step0-report.py`.
  - `speq-dxt --check --path spu`, `speq-dxt --bench`, `speq-bench [--quick] [--fault]`,
    `SPEQ_DEBUG=1 gl-dxt`, `spurun <prog.spu> [symbol]`.
  - `games/openarena/step0.sh smoke|tc1-first|tex N|audio N|profile V` (copied to
    `/var/tmp/step0` on the PS3).
- libspeq environment: `SPEQ_S3TC=auto|spu|ppe|off` (default auto), `SPEQ_S3TC_MIN` (64x64),
  `SPEQ_DEBUG=1`, `SPEQ_SPIN_US` (80), `SPEQ_IDLE_US` (10000), `SPEQ_FREE_MS` (5000).
