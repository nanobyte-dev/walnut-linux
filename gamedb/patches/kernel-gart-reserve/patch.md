+++
component = "kernel"
patches = ["patches/local/0004-ps3-reserve-rsx-gart-memory.patch"]
aliases = ["local/0004", "kernel 0004"]
category = "feature"
switch = "ps3rsx_gart"
default = "64M in the code; ps3rsx_gart=0 in our boot arguments (the CMA GART is used)"
forwarded = "no"
tests = []
batches = []
+++

# A boot-time GART reservation for ps3rsx

The RSX reaches main memory through its context's IO window in 1 MiB pages, and 1 MiB contiguous
allocations are unreliable on a 256 MB machine once it is up. So this patch reserves `ps3rsx_gart=<size>`
(1 MiB aligned, 64 MiB unless the command line says otherwise) at boot the way ps3fb's video memory is
reserved, and exports it for the driver. It comes from highmem, which is added to memblock before the
reservation code runs, so it fits next to ps3fb's 9 MB.

It was the GART of Phase 1 (2026-10-14, 32 MB in the phase-1 check). A day later
[kernel-gart-cma](../kernel-gart-cma/patch.md) (local/0007) added a CMA area that costs RAM only while
the GPU uses it, and our boot arguments now set `ps3rsx_gart=0 ps3rsx_cma=64M`, so this reservation is
the fallback path ([DRIVER.md, "The GART"](../../../docs/DRIVER.md#the-gart)). Note that the code's
default is still 64 MiB: a command line without `ps3rsx_gart=0` reserves it as well.
