# RSX as seen from Linux

This is what we measured about the RSX (the PS3's NV47-class GPU) from Linux on the CECH-20xx under our 6.12 kernel: what the GPU context that ps3fb allocates can do, how to fence and copy, and which commands wedge the chip. The probes ran between 2026-10-14 and 2026-10-17 (the Mesa sections at the end are from 10-17); everything here was measured unless it says otherwise. The raw experiments used `ps3-gpu-tools/src/rsx-probe.c` and the probe modules in `ps3rsx/probes/`.

The facts are the reasons behind the driver's design. How the driver is built from them is in [DRIVER.md](DRIVER.md); the method-by-method audit of what Mesa sends is in [MESA-METHODS.md](MESA-METHODS.md); the FIFO words of the first triangle and textured quad are in [RSX-PROGRAMS.md](RSX-PROGRAMS.md). Terms such as lv1, FIFO, GART, M2MF and XDR are in the [glossary](README.md#glossary).

## Experiments at a glance

The probe numbers (E1, E2, ...) are cited all over the docs and the commit log.

| Probe | What it settled |
|---|---|
| E1 | Allocating contexts up to the limit and freeing them stalls every FIFO (see Contexts) |
| E2 | The IO window takes scattered 1 MB maps, remaps, and one chunk at two IO addresses |
| E3 | FIFO CALL into a buffer at an odd offset, then RETURN, works |
| E4 | The 3D back-end semaphore release stores the value with bytes 0 and 2 swapped |
| E5 | 3D state is shared between hardware channels |
| E6 | A display flip from a second context works |
| E7 | Our own context's IRQ outlet delivers vsync at 60/s |
| E8 | A write-combining CPU mapping of VRAM is no faster than the guarded one |
| E8b | Not run: what lv1 keeps of `ioremap_wc`, with pre-touched pages (see [backlog/graphics.md](backlog/graphics.md)) |
| E9 | Chunks mapped after context creation are reachable |
| E10 | VRAM is reachable to at least 253 MB |
| E11 | `SET_REFERENCE` is a real completion fence |
| E12, E13 | A triangle and a textured quad from raw FIFO words |
| E14 | The notifier object writes at reports+0, the same place as the labels |
| E15 | Mesa's whole init stream is accepted |
| E16, E17 | Not run: SPU DMA into a VRAM buffer, and M2MF small-copy latency for the VRAM swap; listed in [backlog/graphics.md](backlog/graphics.md) |

## Contexts

At boot ps3fb calls `lv1_gpu_memory_allocate` and `lv1_gpu_context_allocate(mem, 0x820)`. That gives memory handle 0x5a5a5a5a and context 0x55555555, which is hardware channel 0.

- A second `lv1_gpu_memory_allocate` fails with -17 for every argument set we tried (ps3vram's too), so ps3fb's handle owns all of VRAM.
- A second context on ps3fb's memory handle works (`ps3rsx/probes/rsx_ctx_probe.c`). It gets handle 0x55555554, hardware channel 1, and its own ctrl, driver-info and reports areas. Its FIFO runs, all seven object handles below bind, and M2MF copies are exact. A DRM driver can therefore own a channel next to ps3fb and share VRAM.
- **The context limit is ps3fb's plus two more** (hardware channels 1 and 2); the next allocation fails with -2. Allocating up to the limit and then freeing the contexts stalled every FIFO, ps3fb's included, with no graph exception (E1). Which step breaks it (the third context, the failed fourth, or the frees) is unknown. One extra context allocated and freed is safe, and that is all the driver ever uses.
- **3D state is shared between channels (E5).** A clear in context A, after context B had set its surface, went to B's surface. lv1 does no PGRAPH context switching. So the driver uses one hardware context, and every submission must re-emit the state it depends on.
- ps3fb's reset program (copied, then CALLed) runs fine in a fresh context and gives it the usual object and DMA setup.
- A display flip (`lv1_gpu_display_flip`) from the second context works (E6, checked with the webcam).
- Our context's IRQ outlet delivers vsync at 60/s once its driver-info irq mask is set (E7), so a driver needs no hook into ps3fb for interrupts.
- The IO window (`lv1_gpu_context_iomap`) is per context. IO addresses below 0x10000000 (256 MB) map; anything at or above fails with -17. 64 KB maps fail because the IO page size is 1 MB (flags 0x820). A 16 MB map in one call works. Scattered 1 MB maps anywhere in the window, unmapping and remapping elsewhere, and mapping one chunk at two IO addresses all work (E2). Chunks mapped after the context was created, at IO 64 MB and 255 MB, are reachable through 0xfeed0001 (E9).
- VRAM through 0xfeed0000 reaches at least 253 MB (round trips at 0xdf00000 and 0xfd00000, E10), which is 30 MB more than the 224 MB ps3fb assumes. Driver info reports version 0x211, GPU 0x34, core clock 500 MHz and memory clock 650 MHz.

ps3fb's `/dev/ps3gpu_*` devices expose these areas:

| mmap (`/dev/ps3gpu_*`) | size | what |
|---|---|---|
| vram (minor 0) | 0xe000000 (224 MB) | VRAM, CPU mapping uncached and guarded |
| fifo (minor 1) | 0x80000 (512 KB) | start of the XDR window: reset program at 0, ring at 0x1000 (60 KB), the rest free scratch |
| ctrl (minor 2) | 0x1000 | FIFO control: put 0x40, get 0x44, ref 0x48 |
| driver (minor 3) | 0x20000 | driver info, display heads, IRQ and graph-exception dump |
| reports (minor 4) | 0x10000 | labels (semaphores) from 0, report records from 0x1400 |

The XDR the GPU can reach (IOIF 0 upward) is ps3fb's buffer: the 512 KB FIFO area plus 8.5 MB (`gart_size`) of framebuffer, capped by ps3fb at 32 MB. Userspace can map only the first 512 KB through minor 1; the rest is reachable through `/dev/fb0`.

## Objects bound by ps3fb's reset program

The hypervisor pre-creates these handles (libgcm uses the same ones).

| subch | handle | object |
|---|---|---|
| 0 | 0x31337000 | 3D (NV40 class) |
| 1 | 0x31337303 | M2MF, DMA in XDR and out VRAM by default |
| 2 | 0x3137c0de | M2MF, in VRAM and out XDR |
| 3 | 0x313371c3 | context surfaces 2D |
| 4 | 0x31337a73 | swizzled surface |
| 5 | 0x31337808 | image from CPU |
| 6 | 0x3137af00 | scaled image |

DMA contexts: 0xfeed0000 VRAM, 0xfeed0001 XDR (IOIF), 0x66606660 semaphores (3D), 0x66616661 semaphores (FIFO), 0x66626660 reports, 0x66604200 notifier.

## Synchronisation

- **GET is not completion.** `get == put` only says the FIFO fetched the words, not that the engine ran them. Wait on the ref word or a semaphore.
- **`SET_REFERENCE` (0x50) is a real completion fence** (E11). The value shows up in ctrl+0x48 only after the preceding rendering has landed in memory: in the test, 8 of 8 32 MB clears were done when the ref arrived. That is all a single-channel nouveau fence needs (nv10 style).
- FIFO CALL into another chunk at an odd offset, then RETURN, works (E3). That is the push buffer model.
- **Semaphore releases work** and land in the reports area at the given offset. The FIFO release is 0x64/0x6c. The 3D texture-read release is 0x1d6c/0x1d74. The 3D back-end release is 0x1d70, and it writes its value with bytes 0 and 2 swapped, as on nv40; writing a pre-swizzled value stores the intended one (E4).
- The 0x66604200 notifier writes at reports+0, the same place as the labels (E14), so a fence must not use label offset 0.
- **FIFO semaphore acquire (0x68) is unreliable.** Sometimes it blocks although the label already holds the value, and a CPU write to the label (even of the same value) wakes it. A single channel does not need it. Avoid it until cross-channel sync is needed.
- **`GET_REPORT` (0x1800)** writes a 16-byte record `{u64 GPU timestamp, u32 value, u32 0}` at reports+0x1400+offset.

## Copies and CPU access

| path | rate |
|---|---|
| M2MF VRAM to VRAM | about 2.3 GB/s (copy rate) |
| M2MF XDR to VRAM (8 MB) | about 2.0 GB/s |
| M2MF VRAM to XDR (256 KB chunks) | about 600 MB/s |
| CPU write to VRAM, 8-byte stores | 124 MB/s |
| CPU write to VRAM, glibc `memcpy` | 19 MB/s |
| CPU read from VRAM | 10 MB/s |

CPU access to VRAM is unusable for real work. Uploads and readbacks go through XDR buffers and the M2MF. A write-combining kernel mapping is no faster than the guarded one (69 against 70 MB/s, E8), though that loop also timed each page's first touch, so hash faults may hide a difference (E8b would rule that out; not run).

The 600 MB/s VRAM-to-XDR copy is what makes the VRAM swap's swap-ins about ten times faster than reading VRAM with the CPU ([DRIVER.md](DRIVER.md#vram-swap-ps3vswap0)).

## Errors and what wedges the GPU

A graph exception is reported through ps3fb's interrupt (for channel 1 as well). The handler dumps the PFIFO cache and sets word 1023 of the FIFO area. The causes we have seen:

| Cause | Trigger |
|---|---|
| 0x105 | an M2MF transfer in a fresh context whose notifier DMA was never set; also a 3D texture sampled at an offset that is not 128-byte aligned (+0xaa0, from Mesa's blit reading back a 2x2 mip level for Neverball, 2026-10-17) |
| 0x106 | rendering to a swizzled surface 1 pixel wide (RT_FORMAT type swizzled, log2 width 0); also a draw from a vertex format of type 7 (U8_USCALED, libgcm's UB256) with 3 components (0x1740+ with `data & 0xff == 0x37`), Mesa 0043 on Sauerbraten, 2026-10-19 |
| 0x109 | M2MF methods sent to the surface-2D object |
| (graph exception, no cause noted) | vertex format type 0 (0x1740+ with `data & 0xf == 0`) |

The 0x106 case was found on 2026-10-14 from xeglgears and LXQt crashes: glamor draws into 1-pixel-wide pixmaps (dumps are in `docs/dumps/`). We isolated it with egl-thin: a 1x64 target raises it, while 2x64, 64x1 and 1427x1 (linear) render correctly. Linear surfaces 1 pixel wide are fine. Mesa keeps such render targets linear on the PS3, and the validator rejects the RT_FORMAT. The 0x105 texture offset case is fixed in Mesa (0014) and refused by ps3rsx since version 0.249.

The UB256 case: Mesa 0043 (`NV30_VTX_SNORM8=1`) fetched Sauerbraten's R8G8B8_SNORM normals as U8_USCALED with 3 components, and the RSX raised 0x106 2.2 s into the demo; the run before it in the same boot, without the switch, finished. The dump is `docs/dumps/exception-0x106-ub256-snorm8.txt`. GET (entry 83) is a BEGIN_END(TRIANGLES) still pending, so the exception came at the start of a draw, not at a method write; among the entries PGRAPH took just before it are the world vertex's texcoord and lightmap formats (VTXFMT(1) = 0x2022, float x2, stride 32; VTXFMT(2) = 0x2025, short x2), then the index buffer. The slots after VTXFMT(2), where a normal and a tangent would be, are not among the printed entries. Why we blame UB256 x3 although the dump doesn't show that word: RPCS3, which emulates what commercial games send, asserts UB256 has 4 components (`ensure(size == 4)` in `get_vertex_type_size_on_host`, while it accepts 1 to 4 for UB, S1, S32K and SF), and nouveau's NV30/NV40 table has U8_USCALED only as R8G8B8A8_USCALED. UB256 with 1 or 2 components was never sent; we treat it as forbidden too. **Only UB256 x4 is safe.** Mesa 0043 now always fetches four bytes ([HOTPATHS.md](HOTPATHS.md#snorm8-attributes-in-the-vertex-program-mesa-0043)). Since ps3rsx 0.545~dev the validator refuses type 7 with a size other than 4 or 0 (disabled), so a stream like that fails its push and leaves the GPU running. The controlled PS3 re-test is in [TESTS.md](TESTS.md) Next.

Reading ps3fb's dump takes care. Each pair is `fifo_cache:graph_fifo`, with subchannel `(a>>13)&7` and method `a & 0x1ffc` (bit 0 of `a`, as in `0xf825` or `0xf819`, is set on some entries; the method is still `a & 0x1ffc`). But ps3fb prints entries 4i..4i+3 for i = 0, 4, ..., 28 (entries 0-3, 16-19, ..., 112-115 of the 512), not the ones around GET, and its G/P markers compare GET and PUT with i. It should print the 32 entries ending at GET (task #29, not done). GET and PUT are indexes into that 512-entry cache. Once PGRAPH stops, PFIFO keeps filling the cache, so PUT ends up just behind GET: in every dump so far, 225 to 495 entries were pending. The entries from PUT to GET-1 are the last ones PGRAPH took, in order, written in the lap before; GET is the next one it would have taken.

**An exception wedges the whole RSX, not one channel.** After one in channel 1, ps3fb's channel 0 stops too, and a freshly allocated context's FIFO does not even fetch. None of these recovered it:

- ps3fb's EXIT (FIFO_SETUP plus the reset program);
- freeing the context and allocating a new one;
- FIFO_PAUSE then FIFO_RESUME (both return 0);
- freeing every context and the memory handle (which unbinds ps3fb without crashing) and rebinding: the fresh handle and context allocate, but the first FIFO run still times out.

Only a console reboot recovered it. Whether an LPAR-only reboot through Petitboot is enough is untested. The consequence is a hard rule: the driver must never submit a faulting command stream. That is why ps3rsx validates every push ([DRIVER.md](DRIVER.md#the-validator)). Ideas for a real reset are in [backlog/graphics.md](backlog/graphics.md).

## 3D pipeline from raw commands (E12, E13, E15)

`ps3-gpu-tools/rsx-tri` (its words are in [RSX-PROGRAMS.md](RSX-PROGRAMS.md)) draws on the visible frame, photographed with the webcam. It covers state and viewport writes, vertex program upload, a fragment program in VRAM (stored halfword-swapped), immediate-mode and vertex-array draws, colour interpolation, and a 4x4 A8R8G8B8 linear texture (big-endian `0xAARRGGBB` stores, correct orientation and channel order). No exceptions.

`ps3-gpu-tools/rsx-mesainit` replays Mesa's `nv30_screen_create` init stream with PS3 handles on Mesa's subchannel layout (3D 7, M2MF 2, SF2D 3, SSWZ 4, SIFM 5). The GPU accepted all of it, including the four values that differ from libgcm (0x1450 = 4, 0x1eac = 0xff800006, 0x1ef8, 0x1d64), and a 3D clear works afterwards.

ps3fb's vsync task blits the console into frame 0. ENTER stops it, but a blit already in flight can land after a test's clear, so tests wait 200 ms after ENTER.

## Through the ps3rsx driver

`ps3-gpu-tools/rsxdrm-smoke` through the nouveau ioctls gives exact M2MF GART-to-VRAM and VRAM-to-GART copies, an exact 3D clear, and an exact raw-command triangle. It also ran 10,000 fenced submits with the back-end semaphore label always landed when the fence signalled, and 20 load/unload cycles.

Two things Mesa does that libgcm does not: it sends the M2MF NOP (0x100 on the M2MF subchannel) and RT_ENABLE 0x12, and the GPU accepts both. **Vertex format type 0 (0x1740+ with `data & 0xf == 0`) raises a graph exception, and so does type 7 (UB256) with 3 components (cause 0x106, [above](#errors-and-what-wedges-the-gpu)).** The validator rejects both, UB256 with any size but 4 or 0, and Mesa must never emit them on the PS3.

## Mesa, KMS and X on the driver

Getting stock Mesa nv30 to run turned up these facts. The fixes are patches in our Mesa fork (`mesa/patches/`, numbered as in `mesa/patches/series`).

- **Z16 depth renders wrong.** The PS3 gets only 24-bit depth (0004).
- The 3D blit into a GART destination needs its own path, and its fragment program is stored halfword-swapped like every RSX fragment program (0007; big-endian only).
- **Inline vertex data with a single attribute draws nothing**, with no exception and no fragments. That hits position-only draws such as glamor's fills and any constant-colour shader. Two attributes work, so the PS3 pushes the lone attribute twice (0005).
- Mesa splits long inline draws across pushes, so a primitive can stay open between submits. The driver must not switch channels (and restore state) inside BEGIN/END: that raised a graph exception. ps3rsx waits for the primitive to close.
- The scanout format: the RSX renders and scans out A8R8G8B8 words, bytes A, R, G, B in memory. A big-endian X server asks gbm for ARGB8888 meaning exactly that, so on big-endian the DRM 8888 fourccs are taken as host-order words (0009).
- nv30 exports the fragment colour from the fp16 register H0. FP_CONTROL bit 6 (libgcm's "32-bit exports") takes it from R0 instead. That works on the RSX and rounds like an fp32 pipeline (0008). What remains against softpipe is attribute interpolation: the cutover between rounding down and up wanders over about 0.50 to 0.57 of an 8-bit step.
- The RSX fetches big-endian vertex and index buffers itself (0005), so vertex data is not pushed inline vertex by vertex. That change took ClassiCube from 6.5 fps to 60 fps (vsync-capped) at 1080p; the patch notes give 79 fps uncapped.

KMS: `lv1_gpu_display_flip` from our context shows any VRAM offset, including above 224 MB (tested at 0xe100000). The CPU's writes through a VRAM mmap are seen by the GPU as written. The flip latches at the display's vsync, and the driver sends flip events from the vsync interrupt (E7); `vblank_irq=0` brings back an earlier frame-rate timer that drifted against the real vsync. kmscube runs at 60 fps at 1920x1080.

X: modesetting with glamor (GL 2.1 context, GLSL 1.20) works on NV47. An `xwd` of a fill test is identical to the same run on softpipe, and glxgears runs at 60 fps with direct rendering.

Sharing with ps3fb's consoles: the display shows whichever offset was flipped last. After a handover ps3rsx flips both heads to ps3fb's frame itself, with libgcm's flip status reset as a precaution. PGRAPH state after ps3fb has had the GPU is unknown, so ps3rsx clears its hardware shadow and the next submit restores its channel's whole state.

## What Mesa exposes (2026-10-17, Mesa 26.2.4-1+ps3.186)

`glxinfo -l` on the desktop is kept in `glxinfo-nv47.txt` in this directory (extensions and limits). We get OpenGL 2.1 and GLSL 1.20.

Absent, with what that costs games (per game in [games/README.md](games/README.md)): ARB_texture_rg (dsda-doom's GL mode, OpenTTD's GL mode), ARB_shader_texture_lod, EXT/ARB_vertex_array_bgra, ARB_framebuffer_object (renderergl2's FBO features) and ARB_texture_float. Present: EXT_framebuffer_object, ARB_point_parameters and point sprites, NPOT textures, SGIS_generate_mipmap, EXT_texture3D, ARB_vertex_array_object and S3TC.

Measured with `ps3-gpu-tools/mesa-tests` on 2026-10-17:

- `gl-texfmt`: L8, A8, I8, 4444 and 1555 sample right. L8A8 swapped L and A on every texel: Gallium's L8A8 is a byte array, while the RSX reads the 16-bit texel as a big-endian halfword.
- `gl-genmip`: GPU mipmap generation is right for square power-of-two textures. For non-square ones, the levels 1 pixel wide or high are swizzled render targets that the validator refuses, and the whole push is lost with them, level 0's upload included.
- Both are fixed in Mesa 0012 and 0013 (ps3.223); gl-texfmt and gl-genmip pass. The 0x105 texture-offset exception above is Mesa 0014 (ps3.225); `gl-mipread` passes all 70 cases.

## Submission and fetch (2026-10-17, Mesa ps3.262 to ps3.299)

- Without relocations (Mesa 0019) nothing forced a submission mid-frame, so a frame reached the GPU in one piece at SwapBuffers and the GPU idled while the CPU built it. Armagetron dropped from 60 to 50 fps at the same CPU time. Submitting every 100 draws, or (0027) at a draw start once the GPU has passed the last fence and enough is queued, gave the overlap back (61.5 fps).
- The RSX fetches fragment programs from GART (Mesa 0031: constant changes run from a GART copy). OpenArena opengl2 draws the right picture with no exception.

Older versions and the dated history: [archive/RSX.md](archive/RSX.md).
