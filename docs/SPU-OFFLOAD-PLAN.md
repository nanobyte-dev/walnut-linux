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

1. **Re-check across the game suite** (GAME-SUITE.md): the compressed-texture default and the
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
