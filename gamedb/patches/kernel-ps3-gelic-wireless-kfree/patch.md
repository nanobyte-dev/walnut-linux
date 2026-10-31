+++
component = "kernel-ps3"
patches = ["patches/ps3/0430_ps3_gelic_wirelss_miss_kfree.patch"]
aliases = ["ps3/0430"]
category = "correctness"
switch = "none"
default = "always on"
authors = ["psx-place patch set (author not recorded)", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# gelic wireless frees the command when a scan fails

When starting a scan fails (`-ENOMEM`), `gelic_wl_start_scan()` jumped past the `kfree()` of its command. The patch drops that
jump, so the command is freed on that path too: a small leak per failed scan. It is one of the fixes the psx-place set (Sombat Tragoongoson) added after glevand's OtherOS++ patches; who wrote it is not recorded. We took it from Damian Dudycz's ps3 repository. T2's 0175 is the
same fix. The wireless driver is built in; [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it SAFE.
