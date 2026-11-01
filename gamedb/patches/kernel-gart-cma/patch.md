+++
component = "kernel"
patches = ["patches/local/0007-ps3-rsx-gart-cma.patch"]
aliases = ["local/0007", "kernel 0007", "local/0004", "kernel 0004", "kernel-gart-reserve", "0004-ps3-reserve-rsx-gart-memory.patch"]
category = "feature"
switch = "ps3rsx_cma, ps3rsx_gart"
default = "both 0 in the code (no GART); ps3rsx_cma=64M in our boot arguments"
forwarded = "no"
tests = []
batches = []
+++

# ps3rsx's GART from a CMA area

The RSX reaches main memory through its context's IO window in 1 MiB pages, and 1 MiB contiguous
allocations are unreliable on a 256 MB machine once it is up, so the GART's memory is set aside at boot.

`ps3rsx_cma=<size>` declares a contiguous memory area at boot and exports three functions to ps3rsx:
its size, an allocation of whole 1 MiB chunks (1 MiB aligned, the GPU's IO page size) and their release.
ps3rsx's `gart.c` backs an IO page with a chunk when a buffer object first needs it and gives the chunk
back once it has been idle for 2 s and the GPU has passed every fence that could touch it. Between uses
the area holds ordinary movable pages, so the GART costs RAM only while the GPU uses it. Measured games
use 6.7 to 12.8 MB of it, against the 32 to 64 MB a fixed reservation held for good
([DRIVER.md, "The GART"](../../../docs/DRIVER.md#the-gart), [MEMORY.md](../../../docs/MEMORY.md)).
`rsxdrm-smoke gart` got 56 MB under memory pressure with the data correct.
[kernel-ps3fb-shadow-release](../kernel-ps3fb-shadow-release/patch.md) also takes chunks from this area
when it refills the console.

`ps3rsx_gart=<size>` reserves a fixed area instead, from memblock like ps3fb's video memory, held for
good; ps3rsx uses it in place of the CMA area when it is set. It was the GART of Phase 1 (local/0004,
2026-10-14, 32 MB in the phase-1 check); the CMA area came a day later (commit 7fbda40, "Dynamic GART").
It stays as a fallback for experiments (a forced small GART, for one), with a default of 0: before the
fold its default was 64 MiB, so a command line without `ps3rsx_gart=0` reserved 64 MiB for good next to
the CMA area. The boot arguments are now `ps3rsx_cma=64M` alone. Without either parameter ps3rsx has no
GART and its probe fails with "no GART (ps3rsx_cma=<size> or ps3rsx_gart=<size>)".

local/0004 was folded into this patch on 2026-10-31 ([MERGES.md](../MERGES.md)). Dropping
`ps3rsx_gart=` altogether would also need a ps3rsx change (its `gart.c` reads the exported
`ps3rsx_gart`), so the fold keeps the symbol and the parameter and only changes the default.

## Validation

Validated on 2026-11-01 (PS3 session K, the merged test kernel 6.12.111+ps3.1210~dev20261031171117 with ps3rsx 0.1209), the game run in the next boot (below). The
command line has `ps3rsx_cma=64M` and no `ps3rsx_gart`; ps3rsx's probe line says `(on demand)` and
there is no fixed-reservation line. `MemTotal` is 222560 kB, the same as kernel .874 in the 2026-10-29
bench snapshots (a comparison across boots), so the 64 MiB reservation did not come back; `CmaTotal` is
65536 kB. `rsxdrm-smoke gart` filled the GART with 4 MiB buffers until the 13th failed, as it should,
and copied into each with the GPU. The backing rose from 12 to 60 MiB and was back at 12 MiB within
20 s (CmaFree 20608, then 112, then 30748 kB). No `no GART` or `no memory to back the GART` line.

Game run, 2026-11-01, kernel .1220.dev20261101045143 (U5d session 2 boot, with the merged series):
`rsxdrm-smoke gart` fine; OpenTTD drew; the GART backing rose from 13 to 20 MiB and was back at 12 MiB
within 5 s of the exit; CmaFree fell from 28 to 11 MB and came back to about 20 MB; no GART errors.

```
ps3rsx ps3rsx: [drm] VRAM 245 MiB (from 0x900000), GART 63 MiB (on demand), display 1920x1080 pitch 7680
  12 x 4 MiB GART buffers (48 MiB), last at 0x38a1000: copied, 0 words wrong
GART backed by memory: 61440 KiB (on demand)
```
