# What in our stack could run on the SPEs

2026-10-15. (Status 2026-10-17: the runtime is libspeq; its first consumers are the DXT1 encoder
behind Mesa (shipped), fluidsynth's voice DSP and Sauerbraten's skinning; OpenAL Soft's mixer
measured at 3.7-4.7% of a game, under the bar. SPU-OFFLOAD-PLAN.md has the current state.) Survey of prior art and our own profiles (OpenArena: docs/PLAN.md item 2). "Unverified"
marks claims nobody has measured here yet.

## Bottom line

The SPEs don't help the biggest PPE costs. In the OpenArena game thread, Mesa is 30% and the
kernel 23%: per-draw work that is branchy, takes microseconds, and is needed immediately. An
SPE round trip (dispatch, DMA, completion) costs more than it saves.

The SPEs suit work that is streaming, SIMD, latency-tolerant, and at least ~100 µs per job:
- audio mixing;
- video decoding;
- asynchronous game work (meshing, asset decode).

GameOS uses them the same way, through SPURS.

## Ranked candidates

| # | Candidate | PPE time saved | Effort | Verdict |
|---|---|---|---|---|
| 1 | Audio mixing (OpenAL Soft voices) | ~5–8% in OpenArena, after the free fixes below | runtime 1–2 wk + mixer 2–4 wk | best first prototype |
| 2 | Video decoding: MPEG-2/MPEG-4 ASP first (IDCT, motion comp.), then H.264 | from impossible to working | MPEG-2 4–8 wk; H.264 1080p months | highest value, highest cost |
| 3 | ClassiCube chunk meshing; Quake 3 MD3 vertex interpolation | smoother play; part of the renderer's 15% | 2–4 wk each | meshing is already async; MD3 needs the renderer restructured |
| 4 | zlib inflate (pk3 loading) | ≤6%, only while loading | 1–2 wk | low; inflate is serial and branchy. Free alternative: stored (uncompressed) pk3s |
| 5 | Mesa (translate, u_vbuf, index min/max) | part of 30% | 6+ wk, high risk | no: fix by avoiding the work (VBO bounds caching, VRAM vertex buffers) |
| 6 | ps3rsx command validator | ~9% | very high | no: the kernel can't drive spufs, and userspace validation can't be trusted |
| 7 | zram compression | kswapd load | infeasible | no: no kernel-side SPE use upstream. VRAM as swap (ps3vram-style, GPU DMA) is the better answer |
| 8 | Software rendering on SPEs | — | months | no: the RSX exists |
| 9 | memcpy / GART uploads | small | 1–2 wk | no: the RSX's DMA engines do it |

## Free fixes to try first (step 0)

Audio cost in OpenArena is ~8–10%:
- OpenAL Soft mixes in float;
- ALSA converts the float output to integer.

Cheaper options:
- OpenAL Soft (`alsoft.conf`): `sample-type=int16`, a linear resampler, a lower mixing rate;
- or ioquake3's own integer mixer, `s_useOpenAL 0`.

Re-profile before writing any SPE code. If audio drops below ~3%, start the SPE work with
meshing or video instead.

## Prior art

- **Mesa's Gallium "cell" driver** (2007–2011, removed for lack of users):
  - SPUs did rasterisation and fragment work in 32×32 tiles, with runtime-generated SPU code
    for fragment shaders and blending. The PPE batched triangles in memory and the SPUs pulled
    them.
  - Only that "PPE writes batches, SPUs pull" pattern applies to us. The RSX rasterises, and
    NV47 runs vertex shaders in hardware.
- **spu-medialib** (2007): SPU colour conversion and scaling, and an MPEG-2 IDCT. Its author
  claimed 720p MPEG-2 played with near-zero PPE load. Nobody did H.264 on PS3 Linux. CABAC is
  serial, about 100 ms per 720p I-frame on the PPE in a 2009 FFmpeg thread.
- **Kernel uses:** a 2007 RFC ("KSPU") ran AES for the kernel's crypto API on an SPU. It was
  probably never merged (unverified). ps3vram used VRAM as a block device/swap through GPU DMA,
  with no SPE involved.
- **Sony MARS / SPURS:** resident SPU kernels that poll job queues. They avoid spufs context
  switches, which cost ~20–200 µs according to the measurements quoted. This is the model to
  copy.

## Runtime we'd need (replacing libspe2)

- **Resident workers:** one spufs context per SPE, at most 6, never oversubscribed. Each has a
  pthread blocked in `spu_run`.
- **Dispatch without syscalls:** the SPU waits on a 128-byte job-queue line in process memory,
  with an atomic reservation (`getllar`/`putllc`). A PPE store to that line wakes it.
- **Completion:** the SPU DMA-writes a done word, which the PPE polls; or the PPE blocks on
  `ibox`.
- **Latencies:** probably sub-µs to wake through the reservation and µs to tens of µs through
  ibox (unverified). Measure them first.
- **DMA:** the SPU reads and writes the owning process's addresses directly. Page faults are
  expensive, so buffers should be touched or locked first.
- **Pieces:**
  - an ELF loader (`ps3-spu/src/spurun.c`) and embedding SPU programs in PPE binaries;
  - the job ABI;
  - an SPU main loop with double-buffered DMA;
  - serving newlib's PPE-assisted calls (stop 0x2100, so `printf` works on the SPU);
  - error reporting and clean teardown.
- **Effort:** about 1–2 weeks, including a latency benchmark.

## Plan

1. Step 0: the audio config fixes, then re-profile OpenArena.
2. `libspeq`: the runtime above plus measured latencies. Test it with a SIMD sum over a user
   buffer, and with zlib inflate as a correctness check of plain C on the SPU.
3. First real consumer, chosen from step 0's numbers:
   - **Audio mixing:** one SPE job per update for all voices (resample, filter and mix per
     voice), with HRTF and effects kept on the PPE. Target: ≥5% of the PPE freed without
     glitches.
   - **Otherwise:** meshing or MPEG-2.

## Sources

- Phoronix, Gallium3D Cell driver dropped (2011): https://www.phoronix.com/news/MTAyMTM
- Mesa 7.10.2 `docs/cell.html`: https://fuchsia.googlesource.com/third_party/mesa/+/refs/tags/mesa-7.10.2/docs/cell.html
- spu-medialib: https://lists.mplayerhq.hu/pipermail/ffmpeg-devel/2007-June/030977.html,
  https://www.ffmpeg.org/pipermail/ffmpeg-devel/2007-October/038131.html
- H.264 on PS3 Linux / CABAC: https://lists.ozlabs.org/pipermail/cbe-oss-dev/2009-May/006714.html,
  https://ffmpeg.org/pipermail/ffmpeg-devel/2009-May/074972.html
- KSPU RFC: https://lists.ozlabs.org/pipermail/cbe-oss-dev/2007-June/002275.html
- ps3vram: https://lkml.iu.edu/hypermail/linux/kernel/0903.0/02419.html
- SPE context-switch cost: https://lists.ozlabs.org/pipermail/cbe-oss-dev/2009-June/006756.html
- MARS: https://www.kernel.org/pub/linux/kernel/people/geoff/cell/mars/mars-docs-1.1.5/index.html
- spufs: https://kernel.org/doc/Documentation/filesystems/spufs.txt
