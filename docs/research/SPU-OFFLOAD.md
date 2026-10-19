# What in our stack could run on the SPEs

This is the survey we did on 2026-10-15 of what could move from the PPE to the six SPEs, with prior art, the runtime it would need and the hardware facts that decide feasibility. What became of it: the runtime is libspeq, and its first users are the DXT1/DXT5 encoder behind Mesa (shipped), fluidsynth's voice DSP and Sauerbraten's skinning; OpenAL Soft's mixer was measured at 3.7 to 4.7% of a game and not built. All of that is in [../SPU.md](../SPU.md) and [../SPU-AUDIO.md](../SPU-AUDIO.md). What is still open is in [../backlog/spu.md](../backlog/spu.md). "Unverified" marks claims nobody has measured here.

## Bottom line

The SPEs do not help the biggest PPE costs. In OpenArena's game thread Mesa is 30% and the kernel 23% (our driver a third of that): per-draw work that is branchy, takes microseconds and is needed immediately. An SPE round trip (dispatch, DMA, completion) costs more than it saves.

The SPEs suit work that is streaming, SIMD, latency-tolerant and at least about 100 us per job: audio mixing and synthesis, texture compression, video decoding, asynchronous game work such as meshing and asset decode. GameOS uses them that way, through SPURS.

A second pass the same day added the memory budget. Memory, not PPE time, is the binding limit: mid-game in OpenArena the kernel and hypervisor hold about 70 MB, the game 74 MB resident, X is squeezed to 6 MB and 128 MB sits in swap, and the multi-second frames are page faults and swap-ins on a USB disk that serves one command at a time. Anything that trades RAM (or VRAM, which now doubles as swap) for compute is worth more than the same PPE percentage saved. Profiles of the game thread then: OpenArena Mesa 30%, kernel 23%, renderer 15%, engine 9%, zlib 6%, audio about 8 to 10%; ClassiCube libgallium 52%, kernel 30% (`ps3rsx_validate` 9%, `ps3rsx_shadow_write` 6%), game 8%, OpenAL 3%.

## Hardware and runtime facts that decide feasibility

| Fact | Value | Consequence |
|---|---|---|
| SPEs available to Linux | 6, 256 KB local store each | jobs must stream; local store is not a memory pool (1.5 MB in total) |
| spufs context switch | about 20 to 200 us (quoted) | resident workers only, never one context per job. Measured since: a parked worker resumes in about 90 us, a freed one in about 3.2 ms |
| Job dispatch via a reservation line | sub-us to us (unverified then) | measured since: wake 0.8 to 1.3 us, submit p50 2.6 us; jobs of 50 to 100 us and up pay off ([../SPU.md](../SPU.md)) |
| SPU DMA | 16 B aligned, 128 B best, at most 16 KB per command | buffers in pinned or touched user memory; page faults stall the MFC |
| Cell to VRAM write and read | about 4 GB/s write, about 16 MB/s read (Sony's figures) | SPUs can produce data straight into mapped VRAM, never read it. Our PPE measured 10 MB/s ([../RSX.md](../RSX.md)) |
| SPU toolchain | GCC 16.2 port, newlib 4.6, `spurun` ([../SPU-GCC.md](../SPU-GCC.md)) | C works; no libspe2, the runtime is ours |
| Kernel | no in-kernel spufs client upstream (the 2007 KSPU RFC never landed) | kernel-side work (validator, shadow, zram, ps3vswap copies) stays on the PPE or the RSX |

SPU DMA goes through the same path as the PPE's, so anything that reads VRAM back (ps3vswap swap-ins) must have the RSX push it (M2MF), not an SPU. The feasibility of SPU DMA into VRAM is in [RSX-SPU-FEASIBILITY.md](RSX-SPU-FEASIBILITY.md).

## Candidates, ranked

First pass (PPE time) and second pass (memory) merged. Verdicts are as of the survey; the last column says what happened.

| # | Candidate | PPE time saved | Memory effect | Effort | Verdict | What became of it |
|---|---|---|---|---|---|---|
| 1 | libspeq runtime (resident workers, job queue, completion) | | | 1 to 2 weeks | prerequisite for the rest | built |
| 2 | DXT1/DXT5 compression at upload (SPU encoder behind Mesa's S3TC texstore) | load time only | VRAM and GART staging a quarter to an eighth for RGB(A)8 textures | 2 to 3 weeks after #1 | best memory win | shipped for OpenArena; every-game switch built (Mesa 0038) |
| 3 | Audio: OpenAL voice mixing, Vorbis decoded per buffer | about 5 to 8% (OpenArena), 4% (ClassiCube) | streamed music instead of decoded PCM | 3 to 5 weeks after #1 | best CPU win | split: fluidsynth built, OpenAL under the bar |
| 4 | ClassiCube chunk meshing, Quake 3 MD3 vertex interpolation | smoother frames | none | 2 to 4 weeks each | meshing is already asynchronous; MD3 needs the renderer restructured; a good showcase, low priority | not started |
| 5 | zlib inflate (pk3 loading) | 6% at most, while loading | none | 1 to 2 weeks | low: inflate is serial and branchy; a stored pk3 costs disk, not RAM | no; libdeflate and no crc32 on the PPE instead (ioquake3 0012, 0016) |
| 6 | Procedural textures and meshes | | replaces disk reads and RAM staging, still occupies VRAM | per game | mostly an RSX job (fragment programs into textures); SPUs only for CPU-side generation | not started |
| 7 | Video decode (MPEG-2 first, H.264 later) | impossible to possible | | 4 to 8 weeks / months | highest value, highest cost | parked (HEVC project) |
| 8 | Compressed swap on SPUs (a `ublk` block device served by SPU LZ4) | kswapd load | about 2 to 3 times the effective swap | 3 to 4 weeks plus kernel `BLK_DEV_UBLK` | no | rejected, see below |
| 9 | Mesa per-draw paths (u_vbuf, translate, state validation, index min/max) | part of 30 to 52% | | 6+ weeks, high risk | no | rejected, see below |
| 10 | ps3rsx validator and shadow | 15% (ClassiCube) | | | no | rejected |
| 11 | zram/zswap compression in the kernel | kswapd | | | no | rejected |
| 12 | ps3vswap copies via SPU DMA | | | | no | rejected |
| 13 | Software rendering (Mesa's "cell" driver revived) | | | months | no | rejected |
| 14 | memcpy / GART uploads | small | | 1 to 2 weeks | no: the RSX's DMA engines do it | rejected |

Notes on the top three:

**#2 DXT.** NV47 samples S3TC natively and Mesa exposes `GL_EXT_texture_compression_s3tc`. A game asking for a compressed internal format (ioquake3: `r_ext_compressed_textures 1`) made Mesa compress on the PPE with its built-in encoder: slow loads, which is why it was off. An SPU encoder (van Waveren's real-time DXT method; Sony's PlayStation Home shipped one) makes it cheap. Per texture: RGBA8 to DXT5 is a quarter, RGB8 to DXT1 an eighth, in VRAM, in GART upload staging (which costs CMA memory) and in upload bandwidth. Integration: a job behind Mesa's S3TC texstore (`tx_compress_dxt1`, reached from `glTexImage`, not `util_format_dxt*_pack`), so every GL app benefits, plus an opt-in switch to compress plain RGB(A)8 uploads for chosen games.

**#3 Audio.** Mixing jobs are about 1 to 5 ms per update, comfortably above dispatch cost. Step 0's free fixes were `sample-type=int16`, a linear resampler, a lower rate, or ioquake3's own integer mixer, then re-profile before writing any SPE code. If audio dropped below about 3%, start with meshing or video instead. Decoding Vorbis per buffer on an SPU and keeping only the compressed stream resident would save memory too; it was later dropped, since OpenArena's effects are WAV ([../SPU-AUDIO.md](../SPU-AUDIO.md)).

**#1 libspeq** is the shared base: the same queue serves DXT blocks, audio buffers, inflate and meshing. Without it every consumer pays spufs context switches.

## Prior art

- **Mesa's Gallium "cell" driver** (2007 to 2011, removed for lack of users). The SPUs did rasterisation and fragment work in 32x32 tiles with runtime-generated SPU code for fragment shaders and blending; the PPE batched triangles in memory and the SPUs pulled them. Only that "PPE writes batches, SPUs pull" pattern applies to us: the RSX rasterises, and NV47 runs vertex shaders in hardware.
- **spu-medialib** (2007): SPU colour conversion and scaling and an MPEG-2 IDCT. Its author claimed 720p MPEG-2 played with near-zero PPE load. Nobody did H.264 on PS3 Linux. CABAC is serial: about 100 ms per 720p I-frame on the PPE in a 2009 FFmpeg thread.
- **Kernel uses.** A 2007 RFC ("KSPU") ran AES for the kernel's crypto API on an SPU; it was probably never merged (unverified). ps3vram used VRAM as a block device or swap through GPU DMA, with no SPE involved.
- **Sony MARS / SPURS.** Resident SPU kernels that poll job queues and avoid spufs context switches. This is the model libspeq copies.

## The runtime we needed (replacing libspe2)

Resident workers: one spufs context per SPE, at most 6, never oversubscribed, each with a thread blocked in `spu_run`. Dispatch without syscalls: the SPU waits on a 128-byte job-queue line in process memory with an atomic reservation (`getllar`/`putllc`), and a PPE store to that line wakes it. Completion: the SPU DMA-writes a done word the PPE polls, or the PPE blocks on `ibox`. The SPU reads and writes the owning process's addresses directly, so buffers should be touched first.

The pieces: an ELF loader (`ps3-spu/src/spurun.c`) and a way to ship SPU programs, the job ABI, an SPU main loop with double-buffered DMA, newlib's PPE-assisted calls (stop 0x2100, so `printf` works on the SPU), and error reporting with clean teardown. We estimated one to two weeks including a latency benchmark. What was built and measured is in [../SPU.md](../SPU.md).

## What was rejected, and why

- **Kernel-side SPU use** (validator, zram/zswap compression, ps3vswap copies). The kernel cannot drive spufs, and the validator must stay in the kernel to be trusted: userspace validation cannot be. Optimise on the PPE instead. For swap, VRAM as swap (ps3vram-style, GPU DMA) is the better answer, and it is what ps3vswap is. VRAM reads by an SPU would also run at 16 MB/s, so swap-ins use the RSX's M2MF.
- **Mesa per-draw paths** (u_vbuf, translate, state validation). Microsecond-sized and branchy, and needed right away: an SPE round trip costs more than the PPE spends. Fix it by avoiding the work (VBO bounds caching, VRAM vertex buffers, fewer words per draw). This is what Mesa patches 0015 to 0025 did.
- **Compressed swap via a userspace block device** (a `ublk` device served by SPU LZ4). Userspace swap servers deadlock under reclaim unless fully mlocked (the ublk-qcow2 discussion), and ps3vswap gives the space without that risk. It also needs kernel `BLK_DEV_UBLK`.
- **Software rendering on SPEs / the Cell driver revival.** The RSX exists, and GL3-only games stay out of reach anyway.
- **memcpy and GART uploads.** Small, and the RSX's DMA engines do it.
- **Vorbis on an SPU.** ioquake3 already streams music in 4 KB buffers through libvorbisfile and the effects are WAV; see [../SPU-AUDIO.md](../SPU-AUDIO.md).

## The order we proposed, and the free experiments

1. Free experiments first (days): OpenArena `r_ext_compressed_textures 1` (VRAM, GART, level load time); OpenAL Soft int16 plus a linear resampler; re-profile.
2. libspeq (1 to 2 weeks): resident workers, reservation-line job queue, done-word completion, newlib stop 0x2100 for `printf`; benchmark dispatch latency and SPU DMA write rate into mapped VRAM (expect GB/s if Sony's 4 GB/s holds).
3. First consumer, the SPU DXT encoder: prototype as a standalone `spurun` tool compressing a 1024x1024 RGBA image (target at least 50 Mpixel/s across 6 SPEs), then wire it into Mesa. Success was an OpenArena level load no slower than uncompressed and texture VRAM a quarter or better.
4. Then audio mixing and decoding; meshing as a showcase.

This is the order that happened, except that audio became fluidsynth first. The target was beaten: 379 Mpixel/s on one SPE, and the level load is no slower ([../SPU.md](../SPU.md)).

## Sources

- Cell/RSX bandwidth figures (Cell read of GDDR3 about 16 MB/s, write about 4 GB/s): [Wikipedia: RSX Reality Synthesizer](https://en.wikipedia.org/wiki/RSX_Reality_Synthesizer), [bit-tech (2006)](https://bit-tech.net/news/gaming/PS3_hardware_could_be_hobbled/1/), [AnandTech forum thread](https://forums.anandtech.com/threads/some-more-ps3-infomation.1878655)
- Real-time DXT: [van Waveren, Real-Time DXT Compression](https://www.quaddicted.com/webarchive/mrelusive.com_20170131/publications/papers/Real-Time-Dxt-Compression.pdf), [NVIDIA, Real-Time YCoCg-DXT](https://developer.download.nvidia.com/whitepapers/2007/Real-Time-YCoCg-DXT-Compression/Real-Time%20YCoCg-DXT%20Compression.pdf), [Castaño, GPU texture compression everywhere (SPU encoder for PlayStation Home)](https://www.ludicon.com/castano/blog/?p=1468), [Intel, Fast CPU DXT compression](https://www.intel.com/content/www/us/en/developer/articles/technical/fast-cpu-dxt-compression.html)
- Userspace block devices and reclaim deadlocks: [ublk-qcow2 discussion (2022)](https://lore.kernel.org/all/Yz7vvNKSNRyBVObo@T590/), [Stefan Hajnoczi's reply](https://lore.kernel.org/io-uring/Yz7fTANAxAQ8KT4v@fedora/)
- Phoronix, Gallium3D Cell driver dropped (2011): https://www.phoronix.com/news/MTAyMTM
- Mesa 7.10.2 `docs/cell.html`: https://fuchsia.googlesource.com/third_party/mesa/+/refs/tags/mesa-7.10.2/docs/cell.html
- spu-medialib: https://lists.mplayerhq.hu/pipermail/ffmpeg-devel/2007-June/030977.html, https://www.ffmpeg.org/pipermail/ffmpeg-devel/2007-October/038131.html
- H.264 on PS3 Linux / CABAC: https://lists.ozlabs.org/pipermail/cbe-oss-dev/2009-May/006714.html, https://ffmpeg.org/pipermail/ffmpeg-devel/2009-May/074972.html
- KSPU RFC: https://lists.ozlabs.org/pipermail/cbe-oss-dev/2007-June/002275.html
- ps3vram: https://lkml.iu.edu/hypermail/linux/kernel/0903.0/02419.html
- SPE context-switch cost: https://lists.ozlabs.org/pipermail/cbe-oss-dev/2009-June/006756.html
- MARS: https://www.kernel.org/pub/linux/kernel/people/geoff/cell/mars/mars-docs-1.1.5/index.html
- spufs: https://kernel.org/doc/Documentation/filesystems/spufs.txt

Older versions and the dated history: [../archive/SPU-OFFLOAD.md](../archive/SPU-OFFLOAD.md), [../archive/SPU-OFFLOAD-SURVEY.md](../archive/SPU-OFFLOAD-SURVEY.md).
