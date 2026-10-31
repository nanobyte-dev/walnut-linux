+++
component = "kernel"
patches = ["patches/local/0043-ps3fb-release-console-shadow.patch"]
aliases = ["local/0043", "kernel 0043"]
category = "performance"
switch = "ps3fb.shadow_release"
default = "0 (off)"
forwarded = "no (test on the PS3 first; docs/KERNEL-MEMORY.md)"
tests = []
batches = []
+++

# ps3fb: release the console's XDR shadow while ps3rsx owns the GPU

While ps3rsx owns the GPU ps3fb neither blits nor flips, and with X in front nobody sees the console,
yet the frame part of ps3fb's video memory (8 of its 9 MiB of XDR) stays allocated. With
`ps3fb.shadow_release=1` (also writable at run time) ps3fb gives it back 3 s after a takeover when no
process has `/dev/fbN` open: fbcon is suspended, the frame's IO pages are pointed read-only at the first
MiB (a stray GPU fetch never faults), and the memory goes back to the page allocator. Handing the GPU
back is immediate; a work item then takes 1 MiB chunks again (the page allocator, else ps3rsx's CMA area
from [kernel-gart-cma](../kernel-gart-cma/patch.md)), maps and vmaps them and resumes fbcon, retrying with
backoff. The first MiB (reset program, FIFO) stays.

It is the alternative to T2's ps3fb-in-VRAM, which would cost 23 MB of scarce VRAM
([OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) decision 6). It builds on the takeover of
[kernel-ps3fb-sharing-api](../kernel-ps3fb-sharing-api/patch.md). It is in the kernels since .472 with
the switch off and has not been turned on yet; the design and the PS3 test plan are in
[KERNEL-MEMORY.md](../../../docs/KERNEL-MEMORY.md#6-ps3fbs-console-shadow-while-x-owns-the-display-local0043-switch-off-by-default).
