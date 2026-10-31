+++
component = "kernel"
patches = ["patches/local/0007-ps3-rsx-gart-cma.patch"]
aliases = ["local/0007", "kernel 0007"]
category = "feature"
switch = "ps3rsx_cma"
default = "0 in the code (no area); ps3rsx_cma=64M in our boot arguments"
forwarded = "no"
tests = []
batches = []
+++

# ps3rsx's GART from a CMA area

`ps3rsx_cma=<size>` declares a contiguous memory area at boot and exports three functions to ps3rsx:
its size, an allocation of whole 1 MiB chunks (1 MiB aligned, the GPU's IO page size) and their release.
ps3rsx's `gart.c` backs an IO page with a chunk when a buffer object first needs it and gives the chunk
back once it has been idle for 2 s and the GPU has passed every fence that could touch it. Between uses
the area holds ordinary movable pages, so the GART costs RAM only while the GPU uses it. Measured games
use 6.7 to 12.8 MB of it, against the 32 to 64 MB a fixed reservation held for good
([DRIVER.md, "The GART"](../../../docs/DRIVER.md#the-gart), [MEMORY.md](../../../docs/MEMORY.md)).

It reworks [kernel-gart-reserve](../kernel-gart-reserve/patch.md) (local/0004) without removing it: both
parameters stay, and the boot arguments pick the CMA area with `ps3rsx_gart=0 ps3rsx_cma=64M`. Added
2026-10-15 (commit 7fbda40, "Dynamic GART"). `rsxdrm-smoke gart` got 56 MB under memory pressure with
the data correct. [kernel-ps3fb-shadow-release](../kernel-ps3fb-shadow-release/patch.md) also takes
chunks from this area when it refills the console.
