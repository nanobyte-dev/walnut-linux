+++
component = "kernel-ps3"
patches = ["patches/ps3/0035-ps3flash.patch"]
aliases = ["ps3/0035"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "graf_chokolo", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# ps3flash for NAND and NOR consoles (built off)

Raw flash storage drivers: a new ps3flash ("NG") and a NOR flash driver, with the device types
registered on the system bus. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. graf_chokolo is a co-author. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) classes it FORBIDDEN: it can write
the console's flash. `PS3_FLASH`, `PS3_FLASH_NG` and `PS3_NOR_FLASH` are off in
`packages/patched/kernel/linux/config/ps3.config`, so none of the drivers is compiled; the patch stays
in the series only because later patches apply on top of it. The project's rule is that Linux never
writes the PS3's flash, which [kernel-os-area-no-writes](../kernel-os-area-no-writes/patch.md)
(local/0044) also enforces for the OS area.
