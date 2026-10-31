+++
component = "kernel-ps3"
patches = ["patches/ps3/0030-ps3fb.patch"]
aliases = ["ps3/0030"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# ps3fb's direct GPU interface (`/dev/ps3gpu_*`)

The patch extends ps3fb with mmap devices for the GPU's VRAM, FIFO, control, driver-info and reports
areas, plus ioctls to enter and leave direct GPU use, set GPU and context attributes and drive the
hardware cursor. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. ps3fb is built in (`FB_PS3=y`); Gentoo's 6.12 set has this patch off, ours
builds it. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it CAREFUL: it changes GPU state at run time only.

It is the base the RSX work stands on: the probes in [RSX.md](../../../docs/RSX.md) used these devices, and
ps3rsx shares the GPU with ps3fb through our patches on top. Its FIFO and ctrl mmap offsets were
wrong ([kernel-ps3fb-mmap-offsets](../kernel-ps3fb-mmap-offsets/patch.md), local/0002); the sharing
API ps3rsx uses is [kernel-ps3fb-sharing-api](../kernel-ps3fb-sharing-api/patch.md) (local/0005);
the exception dump is reworked by [kernel-ps3fb-exception-dump](../kernel-ps3fb-exception-dump/patch.md)
(local/0051). While ps3rsx owns the GPU, the direct interface returns -EBUSY
([DRIVER.md](../../../docs/DRIVER.md#takeover-ps3fb-and-ps3rsx-take-turns)).
