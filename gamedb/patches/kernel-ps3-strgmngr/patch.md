+++
component = "kernel-ps3"
patches = ["patches/ps3/0130-lv1call-add-storage-region-hvcalls.patch", "patches/ps3/0150-ps3strgmngr.patch"]
aliases = ["ps3/0130", "ps3/0150"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "graf_chokolo", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# The storage manager driver (built off)

0150 adds `/dev/ps3strgmngr`, an ioctl interface to the hypervisor's storage regions (create and delete
regions, their access lists); 0130 declares the four storage-region hcalls (250 to 253) it calls. One
change in two files. Both are glevand's, 0150 with graf_chokolo as co-author, refreshed for 6.12 by
Sombat Tragoongoson and taken from Damian Dudycz's ps3 repository.

[OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) classes it FORBIDDEN: the region table is persistent. `PS3_STRGMNGR` is off, so nothing is built
from it. [research/HDD-REGION.md](../../../docs/research/HDD-REGION.md) notes that Petitboot's own
region tool uses a different driver with other ioctls.
