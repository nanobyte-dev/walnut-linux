# SPU offload: what to move, with memory as the main constraint

2026-10-15. Follow-up to docs/SPU-OFFLOAD.md (same day), which ranked candidates by PPE time.
This pass adds what we learned since: the memory budget (docs/PLAN.md "Memory budget"), the
VRAM swap work (ps3vswap) and new profiles. "Unverified" marks numbers nobody has measured here.

## What changed the picture

- **Memory, not PPE time, is the binding limit.** Mid-game (OpenArena): ~70 MB kernel and
  hypervisor, game 74 MB resident, X squeezed to 6 MB, 128 MB in swap; the multi-second
  frames are page faults and swap-ins on a USB disk that serves one command at a time.
  Anything that trades RAM (or VRAM, which now doubles as swap) for compute is worth more
  than the same PPE % saved.
- **Profiles (game thread):**
  - OpenArena: Mesa 30%, kernel 23% (our driver a third of it), renderer 15%, engine 9%,
    zlib 6%; audio ~8–10% (OpenAL Soft float mixing + ALSA conversion).
  - ClassiCube: libgallium 52%, kernel 30% (`ps3rsx_validate` 9%, `ps3rsx_shadow_write` 6%),
    game 8%, OpenAL 3%, `Vorbis_OutputFrame` 0.8%.
- **Cell can't read VRAM fast.** Sony's developer figures: Cell writes RSX local memory at
  ~4 GB/s but reads it at ~16 MB/s; our CPU measured 10 MB/s (docs/RSX.md). SPU DMA goes
  through the same path, so SPUs can *fill* VRAM quickly but not read it: anything that
  reads VRAM back (ps3vswap swap-ins) must have the RSX push it (M2MF), not an SPU.
- **The kernel can't use SPEs** (no in-kernel spufs client upstream; the 2007 KSPU RFC never
  landed): kernel-side work (validator, shadow, zram/zswap compression, ps3vswap copies)
  stays on the PPE or the RSX.

## Hardware and runtime facts that decide feasibility

| Fact | Value | Consequence |
|---|---|---|
| SPEs available to Linux | 6, 256 KB local store each | jobs must stream; LS is not a memory pool (1.5 MB total) |
| spufs context switch | ~20–200 µs (quoted, unverified here) | resident workers only (libspeq), never one context per job |
| Job dispatch via reservation line | sub-µs to µs (unverified) | jobs ≥ ~50–100 µs to pay off |
| SPU DMA | 16 B aligned, 128 B best, ≤16 KB per command | buffers in pinned/touched user memory; page faults stall the MFC |
| Cell → VRAM write / read | ~4 GB/s / ~16 MB/s (Sony figures) | SPUs can produce data straight into mapped VRAM; never read it |
| SPU toolchain | GCC 16.2 port, newlib 4.6, `spurun` (docs/SPU-GCC.md) | C works today; no libspe2, runtime is ours to write |

## Candidates

PPE % from the profiles above; memory effect is the new column.

| # | Candidate | PPE time | Memory effect | Effort | Verdict |
|---|---|---|---|---|---|
| 1 | **libspeq runtime** (resident workers, job queue, completion) | — | — | 1–2 wk | prerequisite for everything below; measure latencies first |
| 2 | **DXT1/DXT5 texture compression at upload** (SPU encoder behind Mesa's S3TC pack) | load-time only | **VRAM and GART staging ÷4–8** for RGB(A)8 textures | 2–3 wk after #1 | **best memory win**; frees VRAM for ps3vswap and games |
| 3 | **Audio: mixing + streaming decode** (OpenAL Soft voices; Vorbis decoded per buffer instead of whole) | ~5–8% (OpenArena), ~4% (ClassiCube) | streamed music/voices instead of decoded PCM in RAM (MBs per track) | 3–5 wk after #1 | best CPU win; do the free config fixes first |
| 4 | ClassiCube chunk meshing (and map generation) | smoother frames, part of game 8% | none (meshes go to VRAM anyway) | 2–4 wk | already asynchronous on the PPE; good showcase, low priority |
| 5 | zlib inflate (pk3 loading) | ≤6%, while loading | none; stored (uncompressed) pk3s cost disk, not RAM | 1–2 wk | low; inflate is serial |
| 6 | Procedural textures/meshes | — | replaces disk reads and RAM staging, still occupies VRAM | per game | mostly an RSX job (fragment programs rendering into textures); SPUs only for CPU-side generation (noise, meshes) |
| 7 | Video decode (MPEG-2 first, H.264 later) | impossible → possible | — | 4–8 wk / months | parked (HEVC project); high value, high cost |
| 8 | Compressed swap on SPUs (a `ublk` block device served by SPU LZ4) | kswapd load | ~2–3× effective swap in RAM/VRAM | 3–4 wk + kernel `BLK_DEV_UBLK` | no for now: userspace swap servers deadlock under reclaim unless fully mlocked (ublk-qcow2 discussion); ps3vswap gives the space without that risk |
| 9 | Mesa per-draw paths (u_vbuf, translate, state validation) | part of 30–52% | — | 6+ wk, high risk | no: microsecond, branchy, needed now; fix by avoiding the work |
| 10 | ps3rsx validator / shadow | 15% (ClassiCube) | — | — | no: kernel can't drive SPEs, and validation must stay in the kernel to be trusted. Optimise on the PPE instead |
| 11 | zram/zswap compression in the kernel | kswapd | — | — | no: no kernel-side SPE use |
| 12 | ps3vswap copies via SPU DMA | — | — | — | no: kernel-side, and VRAM reads at 16 MB/s; use the RSX's M2MF for reads |
| 13 | Software rendering (Mesa "cell" driver revival) | — | — | months | no: the RSX exists; GL3-only games stay out of reach anyway |

## Notes on the top three

**#2 DXT compression.** NV47 samples S3TC natively and Mesa already exposes
`GL_EXT_texture_compression_s3tc`. Today a game asking for a compressed internal format
(ioquake3: `r_ext_compressed_textures 1`) makes Mesa compress on the PPE with its built-in
encoder: slow loads, which is why it's off. An SPU encoder (van Waveren's real-time DXT
method; Sony's PlayStation Home shipped an SPU DXT encoder) makes it cheap enough to turn on.
Effect per texture: RGBA8 → DXT5 ÷4, RGB8 → DXT1 ÷8, in VRAM, in GART upload staging (which
costs CMA memory now) and in upload bandwidth. Quality loss is the usual DXT loss.
- Free experiment first (no SPU): OpenArena with `r_ext_compressed_textures 1`; measure
  VRAM (`ps3rsx_mm`), GART peak and level load time against 0.
- Integration: an SPU job behind Mesa's `util_format_dxt*_pack_rgba_8unorm` (one 4×4 block row
  per DMA batch), so every GL app benefits, plus an opt-in driconf/env switch to compress
  plain RGB(A)8 uploads for chosen games.

**#3 Audio.** SPU-OFFLOAD.md's plan stands (step 0: `sample-type=int16`, linear resampler,
lower rate, or ioquake3's own mixer; re-profile). New: decode Vorbis per buffer on an SPU
and keep only the compressed stream resident (memory, not just CPU). Mixing jobs are ~1–5 ms
per update, comfortably above dispatch cost.

**#1 libspeq** is the shared base: the same queue serves DXT blocks, audio buffers, inflate
and meshing. Without it every consumer pays spufs context switches.

## Recommendation

1. **Free experiments (days):** OpenArena `r_ext_compressed_textures 1` (VRAM/GART/load
   time); OpenAL Soft int16 + linear resampler; re-profile.
2. **libspeq** (1–2 wk): resident SPE workers, reservation-line job queue, done-word
   completion, newlib stop 0x2100 for `printf`; benchmark dispatch latency and SPU DMA
   write rate into mapped VRAM (expect GB/s if Sony's 4 GB/s holds).
3. **First consumer: the SPU DXT encoder** behind Mesa's S3TC texstore (`tx_compress_dxt1`,
   reached from glTexImage; not `util_format_dxt*_pack`): prototype as a
   standalone `spurun` tool compressing a 1024×1024 RGBA image (target ≥50 Mpixel/s across
   6 SPEs, unverified), then wire into Mesa. Success = OpenArena level load no slower than
   uncompressed, VRAM for textures ÷4 or better.
4. Then audio mixing/decoding; meshing as a showcase.

## Measured: step 0 (2026-10-16)

No SPU code yet: what OpenArena's own settings do, measured with `ps3-bench.sh` (desktop stopped,
caches dropped, swaps emptied; the game alone under a bare X) and `games/openarena/step0.sh`, on
kernel 6.12.111+ps3.163-ps3-release and Mesa 26.2.4-1+ps3.152. Timedemo `bench`, medians with
[min..max]; `step0-report.py` reads the runs. Raw runs: `/var/tmp/ps3-bench` on the PS3.

**DXT on the RSX** (`gl-dxt`, first DXT ever sampled here, VRAM swap off): DXT1 RGB and RGBA
sample exactly as decoded (max difference 1, no graph exception). Mesa's encoder on the PPE:
**2.6 Mpixel/s** (the dev PC: 27). libspeq's PPE encoder (van Waveren, behind Mesa's hook since
Mesa 26.2.4-1+ps3.186, kernel .178): **6.5 Mpixel/s** through glTexImage, 7.1 for the encoder alone
(148 ms per 1024x1024); the same RSX check passes. Its RMSE on gl-dxt's synthetic image is higher
(33.7 against 29.7: the noise quadrants, where a bounding box loses), but lower on OpenArena's opaque
textures (mean 4.23 against 4.29, better on 37 of 50; `make -C ps3-spu test-mesa`).

**Compressed textures** (`r_ext_compressed_textures` 0/1; OpenArena compresses only opaque
textures, to RGBA DXT1). With the VRAM swap on, 3 runs each, interleaved:

| | tc0 | tc1 |
|---|---|---|
| level load (`CL_InitCGame`) | 14.02 s [13.94..14.63] | 15.14 s [15.10..15.70] |
| timedemo fps | 48.8 [48.6..49.8] | 52.4 [44.2..54.0] |
| longest frame | 975 ms [902..1016] | 1258 ms [1059..4393] |
| game's peak VRAM | 51.6 MB | 37.5 MB |
| GART backed (peak) | 15.4 MB | 14.3 MB |
| pages swapped out (run) | 8638 | 7098 |

The first 5+5 runs (VRAM swap accidentally off) agree: load 13.98 → 15.45 s, VRAM 51.7 → 40.3 MB.
- **VRAM: -11 to -14 MB** for the game's textures (a quarter of the opaque ones), and ~6 MB less
  swapped out. No VRAM swap write-backs in either case: OpenArena doesn't press VRAM hard enough.
- **Cost on the PPE: +1.1 to +1.5 s of load (+8 to +10%)**, inside the plan's max(10%, 2 s), but
  the longest frame grows by 150-300 ms: models loaded during play (bots appearing) are
  compressed at 2.6 Mpixel/s in the middle of a frame. So tc1 is **not shipped on Mesa's
  encoder**; it ships with libspeq's encoder behind the Mesa hook (`mesa/patches/0011`), re-measured
  the same way, and the SPU encoder removes the hitch entirely.

**With libspeq's encoder** (kernel .178, Mesa +ps3.186, 5 runs each, interleaved): load 13.68 s
[13.66..13.87] → 14.16 s [14.08..14.68] (+0.5 s), longest frame 956 ms [782..1475] → 1008 ms
[849..1469] (no change), fps 49.9 → 53.0, game VRAM 51.6 → 37.5 MB. **Shipped**: OpenArena's
autoexec.cfg sets `r_ext_compressed_textures 1` (ps3-desktop).

**Sound** (timedemo, 3 runs each): A stock OpenAL 49.7 fps; B OpenAL int16 + linear resampler,
no HRTF 49.8; B22 the same at 22 kHz 50.0; C the game's own mixer (`s_useOpenAL 0`, `s_khz 22`)
50.0; **Z sound off 55.3**. Level load 13.8-14.1 s with sound, 11.9 s without (decoding the
sounds), and ~7 MB more MemAvailable without.
- **Sound costs ~10% of the frame rate, and no setting helps**: B moves work around (its mixer
  thread is 16.5% of the game's profile samples against 11% for A), C splits it between SDL's
  audio thread (8.6%) and the main thread (+~4%). Nothing to ship.
- So audio stays an SPU consumer (rule: >=5%), after DXT. Decoded sound also costs RAM (~7 MB
  here) and load time (1.9 s), which per-buffer decoding on an SPU would save as well.

## Measured: libspeq on the SPEs (2026-10-16)

`speq-bench` (ps3-spu) through `ps3-bench.sh`, desktop stopped, kernel .178, 6 SPEs.
- **Wake through the reservation (P1):** a PPE store to the line an idle SPU has reserved wakes it
  in 0.8-1.3 us (p99 1.9); a store to the next line wakes nothing. The decrementer counts at the
  79.8 MHz timebase (P4), so idle workers park and free their contexts on time.
- **Submit to idle workers:** 1 SPE p50 2.6 / p99 6 us; 6 SPEs p50 9.8 / p99 65 us. With 6, every
  submit wakes all of them (10.6 wakes and 3.3 lost claim races per job): the herd, not the wake,
  makes the tail. Fine for 100 us+ jobs; per-worker doorbells would cut it if a consumer needs it.
- **Completion (P2):** spinning 2 us, a futex sleep ~62 us more (the kernel's 0x2104 futex wake
  from the SPU), ibox ~35 us. `speq_wait` now spins 80 us before sleeping.
- **Costs (P3):** open 1.6 ms for one worker, ~15 ms for six; 320 KB of kernel vmalloc per
  context, all returned at close; a parked worker resumes in ~90 us, a freed one in ~3.2 ms; the
  SPU's getpid through 0x2104 takes 8 us.
- **Faults:** an SPU DMA to an invalid address fails that job with EFAULT in 1 ms and the worker
  is recreated (3.4 ms), nothing in the kernel log. A put into a page the process hasn't touched
  costs 40-87 us per 4 KiB (the worker thread faults it in): pre-touch output buffers.
- **Bandwidth (P5):** one SPE gets 12-14 GB/s and puts 18-22 GB/s to XDR; six together ~20-23 GB/s,
  the memory's limit.
- **The SPU DXT1 encoder** (`ps3-spu/spu/dxt.c`, `dxt-test.spu` with spurun): byte-identical to the
  reference on 37856 test blocks; **135 cycles per block = 379 Mpixel/s per SPE** (RGBA 335),
  against 6.5 Mpixel/s for libspeq's PPE encoder. One SPE is faster than the encoder's input from
  memory needs; the job queue's dispatch and the output page faults decide the rest.

## Sources

- Cell/RSX bandwidth figures (Cell read of GDDR3 ~16 MB/s, write ~4 GB/s):
  [Wikipedia: RSX Reality Synthesizer](https://en.wikipedia.org/wiki/RSX_Reality_Synthesizer),
  [bit-tech (2006)](https://bit-tech.net/news/gaming/PS3_hardware_could_be_hobbled/1/),
  [AnandTech forum thread](https://forums.anandtech.com/threads/some-more-ps3-infomation.1878655)
- Real-time DXT: [van Waveren, Real-Time DXT Compression](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/papers/Real-Time-Dxt-Compression.pdf),
  [NVIDIA, Real-Time YCoCg-DXT](https://developer.download.nvidia.com/whitepapers/2007/Real-Time-YCoCg-DXT-Compression/Real-Time%20YCoCg-DXT%20Compression.pdf),
  [Castaño, GPU texture compression everywhere (SPU encoder for PlayStation Home)](https://www.ludicon.com/castano/blog/?p=1468),
  [Intel, Fast CPU DXT compression](https://www.intel.com/content/www/us/en/developer/articles/technical/fast-cpu-dxt-compression.html)
- Userspace block devices and reclaim deadlocks: [ublk-qcow2 discussion (2022)](https://lore.kernel.org/all/Yz7vvNKSNRyBVObo@T590/),
  [Stefan Hajnoczi's reply](https://lore.kernel.org/io-uring/Yz7fTANAxAQ8KT4v@fedora/)
- Everything else: docs/SPU-OFFLOAD.md "Sources" (Mesa cell driver, spu-medialib, KSPU, MARS,
  spufs, context-switch cost).
