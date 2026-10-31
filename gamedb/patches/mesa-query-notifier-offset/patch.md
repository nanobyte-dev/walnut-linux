+++
component = "mesa"
patches = ["patches/0006-nv30-query-notifier.patch"]
aliases = ["Mesa 0006"]
category = "port"
switch = "none"
default = "always on"
gating = "is_ps3"
forwarded = "no"
tests = []
batches = []
+++

# nv30: query notifier offset on the PS3

On a PC each nouveau channel has a query notifier of its own, and nv30 writes report offsets from 0. On
the PS3 every channel shares the one reports area of the hypervisor's context, and ps3rsx hands each
channel a 4 KiB slice of it at 0x1400 + 0x1000 x channel
([DRIVER.md](../../../docs/DRIVER.md#ioctls-abi16c-nvifh)). `QUERY_GET` (0x1800) and `RENDER_ENABLE`
(0x1e98) now add `query->offset - 0x1400`, so each channel's reports land in its own slice and
occlusion and timer queries of two processes never overwrite each other. Timer queries stay on.

The 124 report slots of a slice are what [mesa-query-report-steal](../mesa-query-report-steal/patch.md)
(0046) recycles safely.
