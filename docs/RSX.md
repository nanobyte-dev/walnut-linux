# RSX as seen from Linux

This is what we measured about the RSX (the PS3's NV47-class GPU) from Linux on the CECH-20xx under our 6.12 kernel: what the GPU context that ps3fb allocates can do, how to fence and copy, and which commands wedge the chip. The probes ran between 2026-10-14 and 2026-10-17 (the Mesa sections at the end are from 10-17); everything here was measured unless it says otherwise. The raw experiments used `packages/walnut/tools/ps3-gpu-tools/src/rsx-probe.c` and the probe modules in `packages/walnut/drivers/ps3rsx/probes/`.

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
- A second context on ps3fb's memory handle works (`packages/walnut/drivers/ps3rsx/probes/rsx_ctx_probe.c`). It gets handle 0x55555554, hardware channel 1, and its own ctrl, driver-info and reports areas. Its FIFO runs, all seven object handles below bind, and M2MF copies are exact. A DRM driver can therefore own a channel next to ps3fb and share VRAM.
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
| 0x105 | an M2MF transfer in a fresh context whose notifier DMA was never set; also a 3D texture sampled at an offset that is not 128-byte aligned (+0xaa0, from Mesa's blit reading back a 2x2 mip level for [Neverball](../gamedb/games/neverball/), 2026-10-17) |
| 0x105 | a draw sampling a texture in format 0x14 (nouveau's A16, libgcm's X16): mpv `--vo=gpu`'s 64x64 dither texture (`GL_LUMINANCE16`, swizzled, NEAREST, REPEAT), at its first frame (2026-10-28, kernel .874, Mesa ps3.924, ps3rsx 0.924). The PFIFO cache lines up with the submit captured on the PC: PGRAPH had taken the draw and the fence's release, so the draw raised it. The draw's other new feature, `gl_FragCoord.xy`, is the second suspect until the PS3 check in [TESTS.md](TESTS.md) Next ([analysis](dumps/exception-0x105-mpv-vo-gpu.md), [dump](dumps/exception-0x105-mpv-vo-gpu.txt)) |
| 0x106 | rendering to a swizzled surface 1 pixel wide (RT_FORMAT type swizzled, log2 width 0); also a draw from a vertex format of type 7 (U8_USCALED, libgcm's UB256) with 3 components (0x1740+ with `data & 0xff == 0x37`), Mesa 0043 on [BananaBread](../gamedb/games/bananabread/), 2026-10-19 |
| 0x109 | M2MF methods sent to the surface-2D object |
| 0x106 | RT_ENABLE (0x220) = 0x12 on the 3D object: colour target 1 plus MRT without target 0, with target 1's offset and pitch never set (rsxdrm-smoke quirks' second push on ps3rsx 0.924, session M, 2026-11-01, channel 1; the exception block's res1[6] was 0x00100000 and res1[7] 0x2, res2 0x54). Mesa never sends it: it writes 0, 1, 3, 7, 0xf, 0x13, 0x17 or 0x1f. The quirks' third push, `VTXFMT(0) = 0`, never reached the GPU (ps3rsx refused it as wedged) |
| (graph exception, cause not proven) | vertex format type 0 (0x1740+ with `data & 0xf == 0`): the 2026-10-14 runs that sent it had RT_ENABLE 0x12 one push before, so the exception may have been that. The validator refuses type 0 anyway |

The 0x106 case was found on 2026-10-14 from xeglgears and LXQt crashes: glamor draws into 1-pixel-wide pixmaps (dumps are in `docs/dumps/`). We isolated it with egl-thin: a 1x64 target raises it, while 2x64, 64x1 and 1427x1 (linear) render correctly. Linear surfaces 1 pixel wide are fine. Mesa keeps such render targets linear on the PS3, and the validator rejects the RT_FORMAT. The 0x105 texture offset case is fixed in Mesa (0014) and refused by ps3rsx since version 0.249; the X16 format is not offered by Mesa on the PS3 (0052) and refused by ps3rsx since 0.1036.

The UB256 case: Mesa 0043 (`NV30_VTX_SNORM8=1`) fetched BananaBread's R8G8B8_SNORM normals as U8_USCALED with 3 components, and the RSX raised 0x106 2.2 s into the demo; the run before it in the same boot, without the switch, finished. The dump is `docs/dumps/exception-0x106-ub256-snorm8.txt`. GET (entry 83) is a BEGIN_END(TRIANGLES) still pending, so the exception came at the start of a draw, not at a method write; among the entries PGRAPH took just before it are the world vertex's texcoord and lightmap formats (VTXFMT(1) = 0x2022, float x2, stride 32; VTXFMT(2) = 0x2025, short x2), then the index buffer. The slots after VTXFMT(2), where a normal and a tangent would be, are not among the printed entries. Why we blame UB256 x3 although the dump doesn't show that word: RPCS3, which emulates what commercial games send, asserts UB256 has 4 components (`ensure(size == 4)` in `get_vertex_type_size_on_host`, while it accepts 1 to 4 for UB, S1, S32K and SF), and nouveau's NV30/NV40 table has U8_USCALED only as R8G8B8A8_USCALED. UB256 with 1 or 2 components was never sent; we treat it as forbidden too. **Only UB256 x4 is safe.** Mesa 0043 now always fetches four bytes ([HOTPATHS.md](HOTPATHS.md#snorm8-attributes-in-the-vertex-program-mesa-0043)). Since ps3rsx 0.545~dev the validator refuses type 7 with a size other than 4 or 0 (disabled), so a stream like that fails its push and leaves the GPU running. The controlled PS3 re-test is in [TESTS.md](TESTS.md) Next.

Reading ps3fb's dump takes care (a worked example: [the mpv analysis](dumps/exception-0x105-mpv-vo-gpu.md)). Each pair is `fifo_cache:graph_fifo`, with subchannel `(a>>13)&7` and method `a & 0x1ffc` (bit 0 of `a`, as in `0xf825` or `0xf819`, is set on some entries; the method is still `a & 0x1ffc`). GET and PUT are indexes into the 512-entry cache: GET is the next entry PGRAPH would take, PUT the next one PFIFO fills. Since kernel patch local/0051 (the first kernel built after 6.12.111+ps3.874) the dump prints the 32 entries ending at GET, wrapping at the end of the cache, so the faulting command is among them: the `get ... put ...` line has the raw words, the `cache 1d8-1f7 (G 1f7, P 0d4)` line the range and both indexes, and each of the 8 lines starts with its first entry's index. A letter before a pair marks GET (`G`), PUT (`P`) or both (`B`, an empty cache). The dump is rate-limited to two per 10 s. Older dumps, all of those in `docs/dumps/` so far, show entries 4i..4i+3 for i = 0, 4, ..., 28 (0-3, 16-19, ..., 112-115), whatever GET was, labelled `[i]`, and their G/P markers compare GET and PUT with i; there the faulting command is only present by luck. Once PGRAPH stops, PFIFO keeps filling the cache, so PUT ends up just behind GET: in the 0x106 dumps 225 to 495 entries were pending. When PUT falls inside the new dump's window, the pairs before the P mark are pending ones fetched after GET, and only those from P to GET-1 are what PGRAPH took last. When the FIFO runs dry first (DMA get equal to put), PUT stays just ahead of GET: in mpv's 0x105 dump one entry was pending, the kernel's SET_REFERENCE after the faulting submit. The entries from PUT to GET-1 are the last ones PGRAPH took, in order, written in the lap before; GET is the next one it would have taken.

**An exception wedges the whole RSX, not one channel.** After one in channel 1, ps3fb's channel 0 stops too, and a freshly allocated context's FIFO does not even fetch. None of these recovered it:

- ps3fb's EXIT (FIFO_SETUP plus the reset program);
- freeing the context and allocating a new one;
- FIFO_PAUSE then FIFO_RESUME (both return 0);
- freeing every context and the memory handle (which unbinds ps3fb without crashing) and rebinding: the fresh handle and context allocate, but the first FIFO run still times out.

Only a console reboot recovered it, until the recipe below (2026-11-01), which ps3rsx now runs in place ([DRIVER.md](DRIVER.md#recovery-after-a-graph-exception-unwedgec)). Whether an LPAR-only reboot through Petitboot is enough is untested. A recovery still costs every client its GPU state, and a second exception in the same boot isn't recovered, so the rule stands: the driver must never submit a faulting command stream. That is why ps3rsx validates every push and keeps a submit with a rejected push off the GPU whole: what reaches the GPU, laid end to end, passes the validator as one stream, whether the client then recovers or its channel is refused ([DRIVER.md](DRIVER.md#a-rejected-push-the-submit-is-cancelled-the-client-recovers)). Ideas for a real reset are in [backlog/graphics.md](backlog/graphics.md).

### lv1 GPU calls and their effects on a wedged RSX

Every call made on a wedged RSX gets a row here, harmless or not, so the next attempt knows what is safe. The plan and its step log are private (`~/Documents/walnut/process-2026-10-31/unwedge/`). Since 2026-11-01 only documented registers and documented lv1 calls are allowed: no reads of unknown `lv1_gpu_device_map` pages.

| Date | Call (on a wedged RSX) | Returned | Effect |
|---|---|---|---|
| 2026-10-14 | `FIFO_PAUSE` (0x2), then `FIFO_RESUME` (0x3) | 0, 0 | none: the FIFO still doesn't run |
| 2026-10-14 | ps3fb's EXIT: `FIFO_SETUP` (0x1) on ps3fb's context, then its reset program | 0 | none: the reset program doesn't run |
| 2026-10-14 | `lv1_gpu_context_free` of the faulting context, `lv1_gpu_context_allocate` of a new one | 0, 0 | none: the new context's FIFO doesn't fetch |
| 2026-10-14 | every context and the memory handle freed (ps3fb unbound), then allocated again (ps3fb rebound) | 0 | none: the first FIFO run times out. Whether `lv1_gpu_close` ran is unknown (snd_ps3 holds the GPU open too, and was most likely loaded) |
| 2026-11-01 | read of channel 1's control page and driver info (put, get, ref, irq fields, exception block) | - | harmless. put = get = 0x452a0, irq status, mask and graph cause 0, exception channel 1 cause 0x106, cache get 0xce put 0xcf |
| 2026-11-01 | `lv1_gpu_device_map(N)` for N = 0 to 15, each unmapped at once, nothing read | 0: -17; 1: 0 (4 KB); 2: -7; 3: 0 (4 KB); 4: 0 (2 MB); 5, 6: 0 (4 KB); 7: 0 (64 KB); 8: 0 (4 KB); 9 to 15: -4 | harmless; the LPAR address is new on every map |
| 2026-11-01 | device 8 mapped, its first 1 KB read | 0 | harmless; sparse single bits, nothing recognisable |
| 2026-11-01 | devices 1, 3, 5, 6 and 7 mapped, the first 256 bytes of each read | 0 | harmless. 1: 0x211 (the driver-info version) at +0x10, then mostly the same words as device 8; 3: all ones; 5, 6, 7: zeros |
| 2026-11-01 | device 4 mapped, its first 256 bytes read | 0 | harmless; 0x0000400a 0x500e8000, then ones and zeros |
| 2026-11-01 | device 4 mapped, then every word of its 2 MB read in order | - | **SYSCON fatal shutdown**: 3 beeps, red power light flashing, the console off. No line reached the log |
| 2026-11-01 | (the wedge for the rows below: rsxdrm-smoke quirks 0.1161 on ps3rsx 0.1209, kernel .1210~dev) | - | graph exception 0x106 on channel 1, res1[6..7] 0x00100000 / 0x2, res2 0x54; the new dump shows PGRAPH took bind 3D, RT_ENABLE 0x12 and RT_ENABLE 1, with the SET_REFERENCE behind them stuck at GET (get 0x55, put 0x56) |
| 2026-11-01 | `lv1_gpu_context_intr` on the driver context (channel 1), then on ps3fb's | 0, 0; no pending bits either | none: a ring of one SET_REFERENCE on channel 1 doesn't run (FIFO_SETUP returns 0, GET stays at the new ring's start, ref reads 0) |
| 2026-11-01 | channel 1's driver-info `irq.mask` := 0x4 (graph exception), then `lv1_gpu_context_intr` on it | -, 0; no pending bits, status 0 | none: the SET_REFERENCE ring still doesn't run |
| 2026-11-01 | `FIFO_PAUSE`, `FIFO_SETUP` (0x1000) to a SET_REFERENCE ring, `FIFO_RESUME`, kick, on channel 1 | 0, 0, 0 | none: the ring doesn't run; the exception block unchanged |
| 2026-11-01 | hcall 114 (`lv1_map_physical_address_region`) of one 4 KB page at 0x28000400000 (RSX MMIO + 0x400000: PGRAPH), one 32-bit read of NV03_PGRAPH_INTR (+0x100) per map, hcall 115 to unmap | 0 (a new lpar each time), 0 | harmless. INTR 0: lv1 has acked the interrupt |
| 2026-11-01 | the same page, one read each of TRAPPED_ADDR (+0x704), TRAPPED_DATA (+0x708), NSTATUS (+0x104), NSOURCE (+0x108), FIFO (+0x720) | 0 for every map and unmap | harmless. The registers read big-endian: TRAPPED_ADDR 0x220 (subchannel 0, RT_ENABLE), TRAPPED_DATA 0x12 (the trap, so the page is PGRAPH), NSTATUS 0x02000000 (bit 25, nouveau's NV10 BAD_ARGUMENT), NSOURCE 0, NV04_PGRAPH_FIFO 0 (PGRAPH's FIFO access off, as section 1 of the plan predicted) |
| 2026-11-01 | NV04_PGRAPH_FIFO := 1 (big-endian), nouveau `nv40_gr_intr()`'s last write; no INTR ack (INTR was 0) | 0, 0 (map, unmap) | harmless; FIFO reads 1 after it, INTR 0. **The GPU stays wedged:** a SET_REFERENCE ring on channel 1 still doesn't run (GET stays at its start). Something on PFIFO's side (the stuck CACHE1 entry, the puller or the pusher) holds too |
| 2026-11-01 | hcall 114 of PFIFO's pages (RSX MMIO + 0x2000, + 0x3000), one read each of the registers nouveau's `nv04_fifo_intr()` and cache-error recovery use, hcall 115 after each | 0 for every map and unmap | harmless. CACHES 1, INTR_0 0, INTR_EN_0 0, CACHE1_PUSH0 0, PUSH1 0x00010001 (DMA mode, channel 1), DMA_PUSH 0, DMA_PUT 0x1008, DMA_GET 0x1000, PULL0 0, GET 0, PUT 0: CACHE1 is empty (after the FIFO_SETUPs of U1 to U4), and channel 1's push access, DMA pusher and puller are all off. No write |
| 2026-11-01 | the restart half of nouveau's sequence: CACHE1_PUSH0 \|= 1, DMA_PUSH \|= 1, PULL0 := 1 (CACHES was already 1), each read back | 0 for every map and unmap | harmless. PUSH0 and PULL0 read 1 after, **DMA_PUSH stays 0** (the write doesn't stick); channel 1's pending ring (put 0x1008, get 0x1000) isn't fetched, and a new SET_REFERENCE ring doesn't run. lv1's FIFO_SETUP for that ring turned PUSH0 and PULL0 off again |
| 2026-11-01 | DMA_STATE (0x3228) and DMA_PUSH read; one `FIFO_SETUP` (0x1000) of channel 1; then PUSH1, GET, PUT, INTR_0, DMA_STATE, DMA_PUSH, DMA_GET, DMA_PUT read | 0 (FIFO_SETUP and every map and unmap) | harmless. Before: DMA_STATE 0, DMA_PUSH 0. **After the FIFO_SETUP, INTR_0 = 0x1000 (DMA_PUSHER) stays pending** (INTR_EN_0 is 0, so no handler sees it), DMA_STATE 0 (error NONE), DMA_PUSH 0x100 (access off, buffer bit set), DMA_GET = DMA_PUT = 0x1000. No write (the step required INTR_0 0) |
| 2026-11-01 | nouveau's DMA_PUSHER recovery with DMA_GET = DMA_PUT: DMA_STATE := 0, DMA_PUSH := 1, INTR_0 := 0x1000, CACHE1_PUSH0 \|= 1, PULL0 := 1 (each read back), then a SET_REFERENCE ring kicked through the control page, **no FIFO_SETUP** | 0 for every map and unmap | DMA_PUSH reads 0x101 and stays, INTR_0 0: **the SET_REFERENCE ran** (ref arrived, get = put). PFIFO's pusher and puller work again on channel 1 |
| 2026-11-01 | then ps3fb's reset program (CALL 0) + SET_REFERENCE kicked the same way | - | the pusher fetched it all (get = put), the ref didn't arrive: CACHE1 GET 0x4, PUT 0x644 (400 entries pending), INTR_0 0x1 (CACHE_ERROR), PULL0 0x10 (HASH_FAILED): the puller stopped at entry 1 on an object handle not found in RAMHT. PGRAPH FIFO 1, INTR 0, TRAPPED_DATA now 1 (RT_ENABLE's second write went through). Stopped, no write |
| 2026-11-01 | channel 1: `FIFO_SETUP`, then the acks and restart (INTR_0, DMA_STATE := 0, DMA_PUSH := 1, PUSH0 \|= 1, HASH := 0, PULL0 := 1), then a ring of bind 3D (0x31337000) + SET_REFERENCE, kicked without FIFO_SETUP | 0 | the pusher fetched it, the puller stopped at once: CACHE_ERROR, HASH_FAILED on the bind. **No object resolves on channel 1 any more** |
| 2026-11-01 | a third context (`lv1_gpu_context_allocate`, hardware channel 2), `FIFO_SETUP`, a ring binding M2MF and the notifier, kicked; `lv1_gpu_context_free` | 0, 0, 0 | none: PFIFO never switches to channel 2 (CACHE1_PUSH1 stays on channel 1), the ring isn't fetched; the FIFO_SETUP turned channel 1's puller off again |
| 2026-11-01 | (after a fresh wedge, before any FIFO_SETUP) every PFIFO/PGRAPH register of the healthy baseline read once | 0 for every map and unmap | harmless. Against healthy: INTR_EN_0 0 (healthy 0x017d1111), PUSH0 0, DMA_PUSH 0x100, PULL0 0, one CACHE1 entry stuck, CACHE1_ENGINE 0x01111111 (healthy 0x11111111), PGRAPH FIFO 0 and NSTATUS 0x02000000; MODE, RAMHT, RAMFC and DMA_INSTANCE unchanged |
| 2026-11-01 | the recovery (PGRAPH FIFO := 1, `FIFO_SETUP`, the acks), then PFIFO INTR_EN_0 := 0x017d1111 (the healthy value) | 0 | harmless, no interrupt storm. A `FIFO_SETUP` with interrupts on still leaves DMA_PUSH and PULL0 off (no DMA_PUSHER pending: lv1 doesn't restart them); after the ack, a bind of 0x31337000 still fails with HASH_FAILED (lv1 now takes the CACHE_ERROR, nothing restarts) |
| 2026-11-01 | (the U4e wedge, not rebooted) a bind ring per object handle on channel 1, a `FIFO_SETUP` and the acks (no HASH write) between | 0 | none: all seven objects (3D, both M2MF, surface 2D, swizzled surface, image from CPU, scaled image) HASH_FAILED on channel 1 |
| 2026-11-01 | a third context (channel 2): with CACHE1 still on channel 1, PFIFO never switches to it, even after nouveau's fifo start (PUSH0, PULL0, CACHES) | 0 | none |
| 2026-11-01 | nouveau `nv04_chan_stop()`'s null context for channel 1 without the RAMFC save (CACHES := 0, DMA_PUSH and PUSH0 and PULL0 off, CACHE1 GET = PUT = 0, PUSH1 := 0x1f, PUSH0 := 1, PULL0 := 1, CACHES := 1), then a third context's `FIFO_SETUP` and fifo start, then its ring | 0 | **PFIFO loads channel 2 (PUSH1 0x10002), its bind of 0x31337000 resolves and the SET_REFERENCE runs**: the first working bind since the trap |
| 2026-11-01 | the same, with ps3fb's reset program on channel 2 (after CACHE1_ENGINE := 0x11111111; a fresh channel loads ENGINE 0) | 0 | 164 entries go through, then the puller stalls on subchannel 0 method 0x308 (data 0x207) with PULL0 0x100 (read from NV40_PFIFO_CACHE1_METHOD/DATA, page 0x28000090000, hcall 114 accepted it). PGRAPH INTR_EN (0x40013c) is 0: lv1 masks PGRAPH interrupts after the trap too |
| 2026-11-01 | a fresh wedge: PGRAPH FIFO := 1, PFIFO INTR_EN_0 := 0x017d1111, NV40_PGRAPH_INTR_EN := 0x03111fd1 (healthy values); channel 1 still HASH_FAILED (no HASH write ever); the null swap, a third context, CACHE1_HASH := 0 (nouveau's pause), ENGINE := 0x11111111, ps3fb's reset program, and PULL0 := 1 three times where lv1 had taken a software-object CACHE_ERROR (0xcafebabe on subchannel 7) and left the puller off | 0 for every call | **the reset program ran to its SET_REFERENCE on channel 2.** Then `lv1_gpu_context_free` of that context (still loaded in CACHE1, its PGRAPH work done) never returned: **SYSCON fatal shutdown, 3 beeps** |
| 2026-11-01 | U5c on kernel .1210.dev20261101022051 (0052 v3): ps3rsx's probe runs the recovery (PGRAPH FIFO := 1, both interrupt enables, ps3fb's no-teardown and leak of the faulted context, a new context, null swap, `FIFO_SETUP`, HASH := 0, fifo start, ps3fb's reset program with the PULL0 watchdog) | 0 for every call | the 3D bind resolves, then entry 1 (the notifier 0x66604200 at method 0x180) HASH_FAILED, ten PULL0 restarts retry the same lookup, the probe fails (-EIO). The same as the first fresh context of U4g; the second one there passed it |
| 2026-11-01 | that context freed once (only its 3D bind run, CACHE1 parked on the null channel first) and allocated again (lv1 hands back the same handle and addresses), then the U5c ps3rsx loaded again | 0, 0 | **the probe succeeds: ps3fb's reset program runs on the new channel 2 (three PULL0 restarts), DRM comes up.** The first submit doesn't run: after the init lv1 has left DMA_PUSH and PUSH0 off (the first half of nouveau's cache-error path, from the reset program's software-object stalls), with the submit unfetched; the watchdog only restarts PULL0 |
| 2026-11-01 | the wedged U5c ps3rsx unloaded (its unload frees no context), the U5c ps3rsx with the watchdog doing the second half of nouveau's cache-error path (PUSH0 \|= 1, DMA_PUSH \|= 1, PULL0 := 1) loaded | 0 | **recovered without a reboot**: the probe's three FIFO restarts, then `rsxdrm-smoke nop` and `clear` (exact), egl-tri (the healthy checksum), X back, glxgears 60 s at 60 fps; no exception, no further restart. ps3fb's text console stays off the GPU (channel 0 not recovered) |
| 2026-11-01 | the whole sequence again after a reboot and a fresh wedge | 0 for every call | **reproduced**: the first fresh context hash-failed on the notifier again, the one free and reallocation fixed it, the probe needed the same three FIFO restarts, then nop, clear, egl-tri, X and glxgears at 60 fps for 60 s, no further restart |
| 2026-11-01 | the recipe inside ps3rsx itself, from the exception hook's work (sessions 3 and 4, kernel .1220~dev with ps3fb's recovery calls, Mesa ps3.1219), X, glxgears, mpv and [Neverball](../gamedb/games/neverball/) running | 0 for every call | **recovered in place**, the same steps (one notifier hash-fail, one free and reallocation, three FIFO restarts). All four clients re-sent their state and kept their frame rate for 5 minutes once the reports area followed the new context (below). A second exception in the same boot: display-only, nothing written |

### Recovering from a graph exception

What brings a wedged RSX back without a reboot, found step by step in the table above and run by ps3rsx since 2026-11-01. Registers are nouveau's documented NV04/NV40 ones (`nvkm/engine/fifo/nv04.c`, `regsnv04.h`, `gr/nv40.c`), reached one access at a time through `lv1_map_physical_address_region` (hcall 114) of three 4 KB pages: 0x28000002000 and 0x28000003000 (PFIFO) and 0x28000400000 (PGRAPH), plus 0x28000090000 (CACHE1's method/data) once for a check. Never channel 0's state.

1. **What lv1 leaves after a trap:** PGRAPH's FIFO access (0x400720) 0, PFIFO's INTR_EN_0 (0x2140) 0 (healthy 0x017d1111) and NV40 PGRAPH INTR_EN (0x40013c) 0 (healthy 0x03111fd1), one entry stuck in CACHE1, the pusher, push access and the puller off. The faulted channel's objects no longer bind (HASH_FAILED) and its context can't be reused.
2. **The steps:** PGRAPH FIFO := 1 and both interrupt enables back to their healthy values (only when nothing is pending); the faulted context left alone, never freed (freeing a context that has run graph work after a trap shut the console down: SYSCON, 3 beeps); a new context; before its `FIFO_SETUP`, nouveau's `nv04_chan_stop()` null context for the channel CACHE1 holds (PUSH1 := 0x1f, its stuck entries dropped); after it, CACHE1_HASH := 0 (nouveau's pause), PUSH0 \|= 1, PULL0 \|= 1, CACHES := 1; ps3fb's reset program.
3. **The first new context** hash-fails its notifier (method 0x180, 0x66604200) every time so far. Parked off CACHE1 (the null channel), freed once (it ran nothing but that bind) and allocated again, it binds.
4. **lv1 handles the software-object methods** after a trap (CACHE_ERROR) but does only the first half of nouveau's `nv04_fifo_intr_cache_error()`: it leaves the pusher and puller off. Doing the second half (PUSH0 \|= 1, DMA_PUSH \|= 1, PULL0 := 1) when PULL0 is off, INTR_0 is 0 and CACHE1's GET is the same on two polls lets the reset program run to its SET_REFERENCE; it was needed three times per recovery.
5. **The new context has its own reports area** (fence labels and query reports). Clients that keep reading the old one never see a fence complete; ps3rsx seeds the new area from the old one and moves every mapping of it.

**Scanout keeps running on a wedged RSX** (2026-11-01, webcam): CPU writes into the front buffer in VRAM show at once, and with X stopped, dumb buffers, SETCRTC and PAGE_FLIP (`lv1_gpu_display_flip`, which returned 0 every time) change the picture. That is what display-only mode stands on: KMS with CPU rendering after a failed recovery.

## 3D pipeline from raw commands (E12, E13, E15)

`ps3-gpu-tools/rsx-tri` (its words are in [RSX-PROGRAMS.md](RSX-PROGRAMS.md)) draws on the visible frame, photographed with the webcam. It covers state and viewport writes, vertex program upload, a fragment program in VRAM (stored halfword-swapped), immediate-mode and vertex-array draws, colour interpolation, and a 4x4 A8R8G8B8 linear texture (big-endian `0xAARRGGBB` stores, correct orientation and channel order). No exceptions.

`ps3-gpu-tools/rsx-mesainit` replays Mesa's `nv30_screen_create` init stream with PS3 handles on Mesa's subchannel layout (3D 7, M2MF 2, SF2D 3, SSWZ 4, SIFM 5). The GPU accepted all of it, including the four values that differ from libgcm (0x1450 = 4, 0x1eac = 0xff800006, 0x1ef8, 0x1d64), and a 3D clear works afterwards.

ps3fb's vsync task blits the console into frame 0. ENTER stops it, but a blit already in flight can land after a test's clear, so tests wait 200 ms after ENTER.

## Through the ps3rsx driver

`ps3-gpu-tools/rsxdrm-smoke` through the nouveau ioctls gives exact M2MF GART-to-VRAM and VRAM-to-GART copies, an exact 3D clear, and an exact raw-command triangle. It also ran 10,000 fenced submits with the back-end semaphore label always landed when the fence signalled, and 20 load/unload cycles.

One thing Mesa does that libgcm does not: it sends the M2MF NOP (0x100 on the M2MF subchannel), and the GPU accepts it. RT_ENABLE 0x12 (target 1 and MRT without target 0) is not something Mesa sends, and it raised graph exception 0x106 in session M. **Vertex type 7 (UB256) with 3 components raises 0x106; vertex format type 0 (0x1740+ with `data & 0xf == 0`) may too (not proven: [above](#errors-and-what-wedges-the-gpu)).** The validator rejects both, UB256 with any size but 4 or 0, and Mesa must never emit them on the PS3.

## Mesa, KMS and X on the driver

Getting stock Mesa nv30 to run turned up these facts. The fixes are patches in our Mesa fork (`packages/patched/graphics/mesa/patches/`, numbered as in `packages/patched/graphics/mesa/patches/series`).

- **Z16 depth renders wrong.** The PS3 gets only 24-bit depth (0004).
- The 3D blit into a GART destination needs its own path, and its fragment program is stored halfword-swapped like every RSX fragment program (0007; big-endian only).
- **Inline vertex data with a single attribute draws nothing**, with no exception and no fragments. That hits position-only draws such as glamor's fills and any constant-colour shader. Two attributes work, so the PS3 pushes the lone attribute twice (0005).
- Mesa splits long inline draws across pushes, so a primitive can stay open between submits. The driver must not switch channels (and restore state) inside BEGIN/END: that raised a graph exception. ps3rsx waits for the primitive to close.
- The scanout format: the RSX renders and scans out A8R8G8B8 words, bytes A, R, G, B in memory. A big-endian X server asks gbm for ARGB8888 meaning exactly that, so on big-endian the DRM 8888 fourccs are taken as host-order words (0009).
- nv30 exports the fragment colour from the fp16 register H0. FP_CONTROL bit 6 (libgcm's "32-bit exports") takes it from R0 instead. That works on the RSX and rounds like an fp32 pipeline (0008). What remains against softpipe is attribute interpolation: the cutover between rounding down and up wanders over about 0.50 to 0.57 of an 8-bit step.
- **The fragment program's TXB (opcode 0x31, the bias in .w) samples wrong whatever the bias.** Warzone 2100's 3D shaders sample through texture2D(s, uv, bias) ([dark-3d-view](../gamedb/games/warzone2100/investigations/2026-10-20-dark-3d-view.md)). Drawn in stripes on 2026-10-20, the same lookups were right as TEX and dark and flat as TXB, with bias 0 and with bias -20 alike. Mesa now emits TXB as TEX (0045; `NV30_TXB_HW=1` keeps the old code for tests). An exact bias would need another route (TXD with scaled derivatives, untried); the switch is there for that.
- **A point's size comes from the vertex program's PSZ output only with `POINT_PARAMS_ENABLE` (0x1ee4) set;** with it 0 (ps3fb's init) every point is `POINT_SIZE`, whatever the program writes. The RSX drops the fraction of a per-vertex size: 19.98 draws 19 pixels, 3.9998 draws 3 (gl-pointsmooth, 2026-10-27; llvmpipe rounds). No smoothing switch is known: smooth points are a fragment program variant (0050).
- The RSX fetches big-endian vertex and index buffers itself (0005), so vertex data is not pushed inline vertex by vertex. That change took [ClassiCube](../gamedb/games/classicube/) from 6.5 fps to 60 fps (vsync-capped) at 1080p; the patch notes give 79 fps uncapped.

KMS: `lv1_gpu_display_flip` from our context shows any VRAM offset, including above 224 MB (tested at 0xe100000). The CPU's writes through a VRAM mmap are seen by the GPU as written. The flip latches at the display's vsync, and the driver sends flip events from the vsync interrupt (E7); `vblank_irq=0` brings back an earlier frame-rate timer that drifted against the real vsync. kmscube runs at 60 fps at 1920x1080.

X: modesetting with glamor (GL 2.1 context, GLSL 1.20) works on NV47. An `xwd` of a fill test is identical to the same run on softpipe, and glxgears runs at 60 fps with direct rendering.

Sharing with ps3fb's consoles: the display shows whichever offset was flipped last. After a handover ps3rsx flips both heads to ps3fb's frame itself, with libgcm's flip status reset as a precaution. PGRAPH state after ps3fb has had the GPU is unknown, so ps3rsx clears its hardware shadow and the next submit restores its channel's whole state.

## What Mesa exposes (2026-10-17, Mesa 26.2.4-1+ps3.186)

`glxinfo -l` on the desktop is kept in `glxinfo-nv47.txt` in this directory (extensions and limits). We get OpenGL 2.1 and GLSL 1.20.

Absent, with what that costs games (per game in [games/README.md](games/README.md)): ARB_texture_rg ([dsda-doom](../gamedb/games/dsda-doom/)'s GL mode, [OpenTTD](../gamedb/games/openttd/)'s GL mode), ARB_shader_texture_lod, EXT/ARB_vertex_array_bgra, ARB_framebuffer_object (renderergl2's FBO features) and ARB_texture_float. Present: EXT_framebuffer_object, ARB_point_parameters and point sprites, NPOT textures, SGIS_generate_mipmap, EXT_texture3D, ARB_vertex_array_object and S3TC.

Measured with `packages/walnut/tools/ps3-gpu-tools/mesa-tests` on 2026-10-17:

- `gl-texfmt`: L8, A8, I8, 4444 and 1555 sample right. L8A8 swapped L and A on every texel: Gallium's L8A8 is a byte array, while the RSX reads the 16-bit texel as a big-endian halfword.
- `gl-genmip`: GPU mipmap generation is right for square power-of-two textures. For non-square ones, the levels 1 pixel wide or high are swizzled render targets that the validator refuses, and the whole push is lost with them, level 0's upload included.
- Both are fixed in Mesa 0012 and 0013 (ps3.223); gl-texfmt and gl-genmip pass. The 0x105 texture-offset exception above is Mesa 0014 (ps3.225); `gl-mipread` passes all 70 cases.

## Submission and fetch (2026-10-17, Mesa ps3.262 to ps3.299)

- Without relocations (Mesa 0019) nothing forced a submission mid-frame, so a frame reached the GPU in one piece at SwapBuffers and the GPU idled while the CPU built it. Armagetron dropped from 60 to 50 fps at the same CPU time. Submitting every 100 draws, or (0027) at a draw start once the GPU has passed the last fence and enough is queued, gave the overlap back (61.5 fps).
- The RSX fetches fragment programs from GART (Mesa 0031: constant changes run from a GART copy). [OpenArena](../gamedb/games/openarena/) opengl2 draws the right picture with no exception.

Older versions and the dated history: [archive/RSX.md](archive/RSX.md).
