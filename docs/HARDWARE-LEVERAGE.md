# Getting more out of the PS3 hardware

Research notes (2026-10-15): which techniques could relieve the PS3's limits under our Linux
stack, what each one wins, and what to do first. Measured facts come from docs/RSX.md, PLAN.md
and today's sessions; figures marked *est.* are estimates to be measured.

## The situation in numbers

| Resource | State today | Source |
|---|---|---|
| XDR RAM | 256 MB; ~240 MB reach Linux; kernel, ps3fb and reserves take ~70 MB | PLAN.md memory budget |
| VRAM (GDDR3) | 256 MB; X ~19 MB, a game ~70 MB: **~150 MB idle** | ps3rsx_mm |
| PPE → VRAM | writes 124 MB/s (8-byte stores), **reads 10 MB/s** | RSX.md E8 |
| RSX copies (M2MF) | VRAM↔VRAM 2.3 GB/s, XDR→VRAM 2.0 GB/s, VRAM→XDR 600 MB/s | RSX.md |
| Disk | USB 2, Bulk-Only Transport: **one command at a time** | PLAN.md game mode |
| Where frames go | OpenArena at 960×540 reaches 65 fps, so most of a 1080p frame is **CPU (PPE)** work: Mesa ~30%, kernel ~23% | PLAN.md OpenArena |

Two consequences shape everything below:

- **RAM is the scarce resource and VRAM is the spare one.** PS3 games had the opposite
  problem, putting geometry in XDR because VRAM was full; we should push everything movable
  into VRAM.
- **The PPE must never read VRAM.** Sony's developer slides gave Cell reads from RSX local
  memory as 16 MB/s, and we measured 10. Data comes back from VRAM only through the RSX's copy
  engine.

## 1. Memory

| Technique | Win | Layer | Effort | Risks / notes |
|---|---|---|---|---|
| **VRAM swap tier** (in progress, vswap.c) | Up to ~96 MB of near-RAM-speed swap in front of the USB disk | ps3rsx | In progress | Today the PPE reads swapped pages back from VRAM at 10 MB/s, so a 4 KB swap-in takes 0.4 ms and write-back to disk runs at 7 MB/s. **Swap-in and write-back should use M2MF (600 MB/s)**: needs kernel-submitted copies on the shared ring, alongside the software context switch, and a GPU wedge would then lose pages that sit only in VRAM. |
| **Textures stay VRAM-only**: already true | – | Mesa | – | Checked `nv30_miptree.c`: uploads go through a temporary GART staging BO, which is freed after the fence. Mesa keeps no CPU copy of texture images, and ioquake3 frees its decoded images after upload. So texture size costs VRAM and GART bandwidth, not resident RAM. |
| **S3TC/DXT textures** | VRAM and staging 4–6× smaller, uploads faster; RAM only transiently | game config | Trivial | RSX samples DXT1/3/5 natively, and our Mesa exposes `GL_EXT_texture_compression_s3tc`. OpenArena: `r_ext_compressed_textures 1`; Mesa then compresses on the CPU at load, so loading is slower. |
| **Kernel trim** | ~5–10 MB *est.* | kernel config | Low | The `-ps3-debug` build has `SLUB_DEBUG` (per-object metadata), `KALLSYMS_ALL`, `DYNAMIC_DEBUG` tables and `IKCONFIG`. A release flavour without them sits in Petitboot next to the debug kernel. Built-ins that are modules elsewhere (Bluetooth, sound) cost RAM only while loaded. |
| **ps3fb console frame** | ~5 MB at 720p, ~9 MB with no XDR frame | kernel / boot args | Low–medium | `ps3fb=<size>` sets the XDR reservation (9 MB today, for 1080p). A 720p mode needs ~4 MB. Dropping it entirely means the console drawing through ps3rsx (DRM fbdev in VRAM) instead of ps3fb. |
| **Wallpaper in VRAM / solid colour** | 8 MB | desktop config | Trivial | PCManFM's 1080p wallpaper sits in an 8 MB SysV segment shared with X. |
| **Drop zram when VRAM swap is up** | Its RAM (~5:1 compressed pages) | rootfs | Trivial | zram only adds RAM use once a faster tier exists. |
| **Game memory knobs** | Several MB each *est.* | game config | Trivial | OpenArena: decoded sounds live in RAM, so `s_khz 11` halves them; check ioquake3's ADPCM path for world sounds. ClassiCube: view distance decides the chunk meshes it builds on the CPU. |
| **Page cache of game data** | – | – | – | Not movable to VRAM: the kernel's page cache must be CPU-addressable. Keep data compressed on disk and decompress on demand instead (section 3). |

## 2. GPU features the stack doesn't use yet

| Feature | Hardware | Our Mesa (nv30 fork) | Win | Effort |
|---|---|---|---|---|
| **Vertex texture fetch** | NV40: 4 vertex units, FP32 formats | `nv40_verttex.c` is wired into state validation, but the vertex sampler count is 0, so GL doesn't offer it | Meshes from textures: heightmap terrain, displacement, GPU-animated water and grass | Medium: expose 4 samplers, restrict formats, test on the RSX (exception risk) |
| **Render-to-texture "bake at load"** | Yes (FBOs) | Works: glamor relies on it | Textures generated once on the GPU, with no file read, no RAM, no staging | Low in the driver; needs engine code (ports/new games) |
| **Procedural fragment programs per frame** | FP: 4096 instructions, 32 temps, 224 constants | Exposed (GLSL 1.20) | Unlimited detail with no storage | At 1080p60 the budget is ~10² instructions per pixel (24 pipes × 500 MHz, *est.*): fine for skies, water, noise detail, not whole scenes |
| **Derivatives (dFdx/dFdy)** | NV40 has DDX/DDY | `fragment_shader_derivatives = false` | Anti-aliased procedural textures, normal maps | Low–medium (Mesa cap + compiler) |
| **Tiling and Z-cull** | lv1 attributes 0x300/0x301 (RPCS3 encodings) | Unused (DRIVER-PLAN.md "Later") | Fill rate and bandwidth, maybe +10–30% in fill-bound scenes *est.* | Medium; low value while we're CPU-bound |
| **Instancing** | NV40-class GPUs did instancing via stream frequency dividers | `vertex_element_instance_divisor = false` ("xxx = yes?") | Fewer draw calls, which cuts PPE work | Medium; check the RSX method |
| **Occlusion queries / conditional render** | Yes | Exposed | Engines can skip hidden objects | Engine work only |
| **Native 720p output** | ps3av mode | One mode for console and KMS | ~2.25× fewer pixels; ~5 MB less ps3fb XDR; smaller VRAM targets | Config; most PS3 games rendered at 720p or below |

Not available: geometry shaders, transform feedback, compute. On this GPU, "compute" means
render-to-texture passes, or the SPUs.

## 3. SPU + RSX together (as PS3 games did)

What the industry did: EDGE geometry ran per-draw vertex jobs on SPUs, with culling,
~3000 jobs per frame in God of War III. MLAA ran on SPUs (~10 ms of SPU time per 720p frame,
in place to save memory). Killzone 3 culled occluded geometry on SPUs before the RSX saw it.
zlib/Edge decompression ran on SPUs while streaming. Textures and render targets lived in VRAM,
geometry in XDR.

| Technique | Win for us | Layer | Effort | Notes |
|---|---|---|---|---|
| **SPU DMA writes into VRAM** | VRAM writes at GB/s rather than the PPE's 124 MB/s; uploads and swap-outs without the PPE or GART staging | ps3rsx (map a VRAM BO into an SPU context), libspeq | Medium: **verify first** | GameOS SPUs DMA to RSX local memory routinely. Under Linux the question is whether spufs MFC DMA to a process mapping of the VRAM BO works. A one-afternoon probe; reads must still go through M2MF. |
| **SPU decompression into VRAM** | Assets stay compressed in RAM and on disk (zlib/DXT/custom): RAM and USB I/O cut 2–4× *est.* | libspeq + game/loader patch | Medium–high | The user's "compute instead of memory", made practical: decompress straight into VRAM, never holding the decoded form in XDR |
| **SPU procedural generation** | Textures and meshes from seeds (noise, terrain, voxel chunks) at 6 × 25.6 GFLOPS | libspeq + game | High (per game) | ClassiCube's chunk meshing is CPU-side and a natural SPU job: offloads the PPE; the meshes land in VRAM VBOs |
| **SPU culling and vertex work** | Fewer vertices pushed through the PPE and kernel validator | Mesa or engine | High | Needs engine hooks; the SPU-offload survey covers PPE relief |
| **SPU post-processing (MLAA etc.)** | Quality at no PPE cost | Engine | High | The VRAM → XDR readback must go through M2MF |

## 4. How "shaders instead of memory" maps onto our games

- **OpenArena / ioquake3:** its resident RAM is engine data (BSP, collision, models, decoded
  sounds, hunk), not textures. Q3 shader scripts are already procedural (`tcMod`, `deformVertexes`).
  Generating its textures on the GPU would save disk I/O and load time, not resident memory.
  Wins here come from section 1: VRAM swap, kernel and desktop trims, sound settings.
- **ClassiCube:** terrain is a small texture atlas. RAM goes to the block map (1 byte per
  block) and CPU-built chunk meshes, which are uploaded to VBOs in VRAM. Meshing on SPUs, or
  meshing from the block map with vertex texture fetch, fits the idea well, but means patching
  the game.
- **New or ported content:** this is where the idea pays fully. Bake textures on the GPU at
  load (render-to-texture), displace meshes from small heightmaps (vertex texture fetch),
  generate geometry on SPUs, keep everything else compressed and decompress into VRAM.

## Ranked roadmap

Status 2026-10-17: 1 done (ps3rsx kcopy.c, swap-ins ~10x faster); 2 partly: the release kernel
(+8.7 MB) shipped, the wallpaper leaves in game mode, the other trims wait (CLAUDE.md "Memory
trims"); 3 not run; 4 not started; 5 done for OpenArena (S3TC through libspeq's SPU encoder). The
open items are ranked again with today's measurements in IDEAS.md. (2026-10-18: 3 and 4 still not
started; for 2, the console frame release (local/0043, up to 8 MB while X runs) is built into kernel
.457 and the 720p console is still the user's call; 5's S3TC for every game is IDEAS #4.)

1. **Finish VRAM swap with GPU copies (M2MF) for swap-in and write-back.** It's the biggest
   RAM lever (~96 MB of fast swap), and the 10 MB/s PPE read path is its bottleneck: swap-in
   0.4 ms per page today, write-back 7 MB/s.
2. **Cheap RAM trims, measured together:**
   - a release kernel flavour;
   - a smaller ps3fb frame (`ps3fb=`, or 720p);
   - the wallpaper in VRAM or a solid colour;
   - zram off once VRAM swap is up.

   About 20–30 MB *est.*
3. **Probe SPU DMA into VRAM.** If it runs at GB/s, it unlocks SPU decompression and
   generation straight into VRAM (the practical form of "compute instead of memory") and
   faster VRAM swap writes.
4. **Expose the NV40 features Mesa leaves off:**
   - vertex texture fetch (4 FP32 units);
   - fragment derivatives;
   - instancing, if the RSX has a stream divider.

   All three are prerequisites for meshes from textures and anti-aliased procedural shading.
5. **Per-game settings that trade CPU for memory:**
   - OpenArena: S3TC (`r_ext_compressed_textures 1`) and `s_khz 11`;
   - ClassiCube: view distance.

   Measure each with the timedemo and game mode.

Deferred: tiling and Z-cull, native 720p, SPU culling and MLAA. They raise GPU throughput or
image quality, while the frame time today is PPE-bound; revisit after the SPU-offload work.

Sources: Cell read/write rates to RSX local memory from Sony's 2006 developer slides as
reported by [Bit-Tech](https://bit-tech.net/news/gaming/PS3_hardware_could_be_hobbled/1/) and
[AnandTech forums](https://forums.anandtech.com/threads/theinq-ps3-hardware-slow-and-broken.1878398/post-20396756);
[RSX overview (Wikipedia)](https://en.wikipedia.org/wiki/RSX_Reality_Synthesizer);
SPU geometry jobs and EDGE in [Practical SPU Programming in God of War III](https://www.slideshare.net/slideshow/practical-spu-programming-in-god-of-war-iii/88316227);
SPU MLAA in [MLAA on PS3](https://www.iryoku.com/aacourse/downloads/06-MLAA-on-PS3.pdf) and
[Filtering Approaches for Real-Time Anti-Aliasing](https://www.iryoku.com/aacourse/);
SPU culling in [The Making of Killzone 3](https://primagames.com/news/the-making-of-killzone-3-1334705)
and [PhyreEngine](https://www.slideshare.net/slideshow/the-next-generation-of-phyreengine/82393270);
[EDGE at GDC](https://mcvuk.com/sony-sharpens-edge-for-new-ps3-dev-tech/).
