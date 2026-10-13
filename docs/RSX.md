# RSX as seen from Linux (measured 2026-10-14)

What the GPU context that ps3fb allocates can do, measured with `gpu/tools/rsx-probe.c` on the
CECH-20xx under our 6.12 kernel. Facts here are measured unless marked otherwise.

## Contexts

ps3fb calls `lv1_gpu_memory_allocate` and `lv1_gpu_context_allocate(mem, 0x820)` at boot: memory
handle 0x5a5a5a5a, context 0x55555555 (hardware channel 0).

- A second `lv1_gpu_memory_allocate` fails with -17 for every argument set tried (also ps3vram's),
  so ps3fb's handle owns all VRAM.
- A second context on ps3fb's memory handle works (`gpu/kmod/rsx_ctx_probe.c`): handle 0x55555554,
  hardware channel 1, its own ctrl/driver-info/reports areas. Its FIFO runs, all seven object
  handles below bind, M2MF copies correctly. So a DRM driver can have its own channel(s) next to
  ps3fb, sharing VRAM.
- IO window (`lv1_gpu_context_iomap`), per context: IO addresses below 0x10000000 (256 MB) map,
  anything at or above fails with -17. 64 KB maps fail: the IO page size is 1 MB (flags 0x820).
  A 16 MB map in one call works. Driver info: version 0x211, GPU 0x34, core 500 MHz, memory 650 MHz.

| mmap (`/dev/ps3gpu_*`) | size | what |
|---|---|---|
| vram (minor 0) | 0xe000000 (224 MB) | VRAM, CPU mapping is uncached + guarded |
| fifo (minor 1) | 0x80000 (512 KB) | start of the XDR window: reset program at 0, ring at 0x1000 (60 KB), rest is free scratch |
| ctrl (minor 2) | 0x1000 | FIFO control: put 0x40, get 0x44, ref 0x48 |
| driver (minor 3) | 0x20000 | driver info, display heads, IRQ and graph-exception dump |
| reports (minor 4) | 0x10000 | labels (semaphores) from 0, report records from 0x1400 |

The XDR window the GPU can reach (IOIF 0 upward) is ps3fb's buffer: 512 KB FIFO area plus 8.5 MB
("gart_size") of framebuffer, capped by ps3fb at 32 MB. Userspace can only map the first 512 KB
through minor 1; the rest is reachable through /dev/fb0.

## Objects bound by ps3fb's reset program

Handles are pre-created by the hypervisor (the same ones libgcm uses).

| subch | handle | object |
|---|---|---|
| 0 | 0x31337000 | 3D (NV40 class) |
| 1 | 0x31337303 | M2MF, DMA in XDR / out VRAM by default |
| 2 | 0x3137c0de | M2MF, in VRAM / out XDR |
| 3 | 0x313371c3 | context surfaces 2D |
| 4 | 0x31337a73 | swizzled surface |
| 5 | 0x31337808 | image from CPU |
| 6 | 0x3137af00 | scaled image |

DMA contexts: 0xfeed0000 VRAM, 0xfeed0001 XDR (IOIF), 0x66606660 semaphores (3D), 0x66616661
semaphores (FIFO), 0x66626660 reports, 0x66604200 notifier.

## Synchronisation

- **GET is not completion.** `get == put` only means the FIFO fetched the words; the engine may
  not have run them. Wait on the ref word or a semaphore instead.
- **SET_REFERENCE (0x50) works:** the value shows up in ctrl+0x48. This is all a single-channel
  nouveau fence needs (nv10-style).
- **Semaphore releases work** and land in the reports area at the given offset: FIFO release
  (0x64/0x6c), 3D texture-read release (0x1d6c/0x1d74), 3D back-end release (0x1d70, value is
  written with bytes 0 and 2 swapped, as on nv40).
- **FIFO semaphore acquire (0x68) is unreliable:** sometimes it blocks although the label already
  holds the value; a CPU write to the label (even the same value) wakes it. Not needed for a single
  channel; avoid it until we need cross-channel sync.
- **GET_REPORT (0x1800)** writes a 16-byte record `{u64 GPU timestamp, u32 value, u32 0}` at
  reports+0x1400+offset.

## Copies and CPU access

| path | rate |
|---|---|
| M2MF VRAM→VRAM | ~2.3 GB/s (copy rate) |
| M2MF XDR→VRAM (8 MB) | ~2.0 GB/s |
| M2MF VRAM→XDR (256 KB chunks) | ~600 MB/s |
| CPU write to VRAM, 8-byte stores | 124 MB/s |
| CPU write to VRAM, glibc memcpy | 19 MB/s |
| CPU read from VRAM | 10 MB/s |

CPU access to VRAM is unusable for real work: uploads and readbacks must go through XDR buffers
and the M2MF. (The mapping is uncached+guarded; a write-combining mapping might help writes, untested.)

## Errors

A graph exception (seen: cause 0x109, M2MF methods sent to the surface-2D object; cause 0x105,
M2MF transfer in a fresh context whose notifier DMA was never set) is reported through ps3fb's
interrupt (also for channel 1), which dumps the PFIFO cache and sets word 1023 of the FIFO area.

**It wedges the whole RSX, not one channel.** After an exception in channel 1, ps3fb's channel 0
stops too, and a freshly allocated context's FIFO doesn't even fetch. None of these recover it:
ps3fb's EXIT (FIFO_SETUP + reset program), context free + reallocate, FIFO_PAUSE + FIFO_RESUME
(both return 0). Only a reboot did. Untested: freeing every context and the memory handle
(unbinding ps3fb).

So the driver must never submit a faulting command stream, or find a real reset.
