+++
component = "kernel-ps3"
patches = ["patches/ps3/0040-ps3-partition.patch"]
aliases = ["ps3/0040"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# The PS3 partition table parser (compiled out)

A block partition parser for the PS3's own partition table (`block/partitions/ps3.c`). It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository.
`PS3_PARTITION` is unset in our config, so it isn't compiled; our kernel addresses GameOS's disk by
region instead ([kernel-ps3-storage-regions](../kernel-ps3-storage-regions/patch.md)). [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it
SAFE.
