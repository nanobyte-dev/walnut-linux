+++
component = "kernel-ps3"
patches = ["patches/ps3/0310-rtc-ps3.patch"]
aliases = ["ps3/0310"]
category = "correctness"
switch = "none"
default = "always on"
authors = ["psx-place patch set (author not recorded)", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# rtc-ps3's range ends at S64_MAX

rtc-ps3 declared `range_max = U64_MAX`. The RTC core works in signed 64-bit time, so the patch sets
`S64_MAX`, the largest value it can represent. It is one of the fixes the psx-place set (Sombat Tragoongoson) added after glevand's OtherOS++ patches; who wrote it is not recorded. We took it from Damian Dudycz's ps3 repository. Compiled in; [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it SAFE.
