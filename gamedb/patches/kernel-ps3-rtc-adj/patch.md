+++
component = "kernel-ps3"
patches = ["patches/ps3/0280-ps3_rtc_adj.patch"]
aliases = ["ps3/0280"]
category = "feature"
switch = "RTCAdj"
default = "0"
authors = ["psx-place patch set (author not recorded)", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# `RTCAdj=`: an offset on the PS3's clock

A boot argument, `RTCAdj=<seconds>`, added to every RTC read in the platform's time code and in rtc-ps3.
It is one of the fixes the psx-place set (Sombat Tragoongoson) added after glevand's OtherOS++ patches; who wrote it is not recorded. We took it from Damian Dudycz's ps3 repository. It is compiled in and defaults to 0, and Walnut doesn't set it. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it SAFE.

Our kernel keeps the clock offset without flash writes another way: [kernel-os-area-no-writes](../kernel-os-area-no-writes/patch.md)
(local/0044) stops rtc-ps3 from writing its offset to the OS area.
