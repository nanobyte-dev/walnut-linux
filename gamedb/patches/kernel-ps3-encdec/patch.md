+++
component = "kernel-ps3"
patches = ["patches/ps3/0220-ps3encdec.patch"]
aliases = ["ps3/0220"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "graf_chokolo", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# The ENCDEC device (built off)

`ps3encdec`, a misc device for the console's encryption and decryption storage device, registered on
the system bus with ps3stor_lib changes to go with it. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. graf_chokolo is a co-author. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates
it CAREFUL (keys); `PS3_ENCDEC` is off, so the driver isn't compiled.
