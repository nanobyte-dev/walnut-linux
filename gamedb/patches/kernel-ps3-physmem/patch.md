+++
component = "kernel-ps3"
patches = ["patches/ps3/0120-lv1call-add-hvcalls-114-115.patch", "patches/ps3/0140-ps3physmem.patch"]
aliases = ["ps3/0120", "ps3/0140"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "graf_chokolo", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# ps3physmem: map the partition's memory from userspace (manual load)

0140 adds `ps3physmem`, a misc device whose mmap maps LPAR memory, set up with lv1 hcall 114; 0120
declares hcalls 114 and 115, which only ps3physmem uses. They are one change. Both are glevand's,
0140 with graf_chokolo as co-author, refreshed for 6.12 by Sombat Tragoongoson and taken from Damian
Dudycz's ps3 repository.

`PS3_PHYSMEM=m`: built as a module that nothing loads automatically. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it CAREFUL, and
FORBIDDEN-capable in principle (arbitrary memory writes), and keeps it for manual use only.
