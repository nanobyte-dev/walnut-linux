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
| SPU toolchain | GCC 16.2 port, newlib 1.20, `spurun` (docs/SPU-GCC.md) | C works today; no libspe2, runtime is ours to write |

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
3. **First consumer: the SPU DXT encoder** behind Mesa's S3TC pack: prototype as a
   standalone `spurun` tool compressing a 1024×1024 RGBA image (target ≥50 Mpixel/s across
   6 SPEs, unverified), then wire into Mesa. Success = OpenArena level load no slower than
   uncompressed, VRAM for textures ÷4 or better.
4. Then audio mixing/decoding; meshing as a showcase.

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
