+++
component = "kernel-ps3"
patches = ["patches/ps3/0410_mmzone.patch"]
aliases = ["ps3/0410"]
category = "build"
switch = "none"
default = "always on"
authors = ["psx-place patch set (author not recorded)", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# The MAX_PAGE_ORDER check in mmzone.h relaxed by one

`include/linux/mmzone.h` refuses to build when `MAX_PAGE_ORDER + PAGE_SHIFT` exceeds
`SECTION_SIZE_BITS`. The patch subtracts one from the left side so the PS3's configuration builds.
It is one of the fixes the psx-place set (Sombat Tragoongoson) added after glevand's OtherOS++ patches; who wrote it is not recorded. We took it from Damian Dudycz's ps3 repository. It only moves a compile-time check; [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it SAFE.
