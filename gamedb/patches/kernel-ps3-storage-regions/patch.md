+++
component = "kernel-ps3"
patches = ["patches/ps3/0010-ps3stor-multiple-regions.patch"]
aliases = ["ps3/0010"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# ps3disk and ps3rom see every storage region

Stock Linux gives ps3disk only the region the hypervisor assigns to the OtherOS partition. This
patch makes the storage library and ps3disk handle every region the console's disk has, one gendisk
per region, named after the region's index: `ps3da` is region 0 (the whole disk, VFLASH included),
`ps3db` GameOS's `dev_hdd0`, `ps3dc` its `dev_hdd1`. ps3rom gets the region-aware read and write
calls too. That is what lets Walnut read GameOS's disk and keep its system image on `dev_hdd0`
([GAMEOS-HDD.md](../../../docs/GAMEOS-HDD.md)). It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. It is built in (`PS3_DISK=y`, `PS3_ROM=y`).

The file is refreshed for 6.12.112: one hunk was dropped because upstream already has the fix, and the
series applies it after the other ps3/ patches.

Exposing every region read-write is also its risk: `ps3da` and `ps3db` sit one letter away from a
region Linux may write. Our write guard on top of it, [kernel-ps3disk-write-guard](../kernel-ps3disk-write-guard/patch.md)
(local/0045), makes `ps3da` read-only and `ps3db` writable only inside sealed ranges. Two bugs in its
probe error path and remove order are fixed by [kernel-ps3disk-unwind](../kernel-ps3disk-unwind/patch.md)
(local/0048).
