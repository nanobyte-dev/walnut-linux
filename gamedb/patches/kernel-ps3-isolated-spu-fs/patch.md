+++
component = "kernel-ps3"
patches = ["patches/ps3/0190-lv1call-add-undocumented-spe-hvcalls.patch", "patches/ps3/0200-export-spe-irq-setup-destroy.patch", "patches/ps3/0210-export-event-receive-port-destroy.patch", "patches/ps3/0230-spuisofs.patch", "patches/ps3/0240-spuldrfs.patch"]
aliases = ["ps3/0190", "ps3/0200", "ps3/0210", "ps3/0230", "ps3/0240"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# Isolated SPU file systems (built off)

`spuisofs` and `spuldrfs` (0230, 0240) are file systems that load and run SPU programs in the SPE's
isolation mode. They need lv1's SPE calls, which 0190 declares (and gives `hvcall.S` the 8-in stub
some of them need), and the PS3 interrupt helpers that 0200 and 0210 export to modules. The five
files are one feature. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository.

[OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates them CAREFUL (isolation mode, key material). `SPUISO_FS` and `SPULDR_FS` are off, so the
file systems aren't compiled; what remains in our kernel is the hcall declarations and the two
symbol exports, which nothing uses.
