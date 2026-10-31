+++
component = "kernel-ps3"
patches = ["patches/ps3/0260-lv1call-add-debug-console-hvcalls.patch", "patches/ps3/0270-udbg-lv1-console.patch"]
aliases = ["ps3/0260", "ps3/0270"]
category = "diagnostics"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# Early printk through the lv1 debug console (built off)

0270 adds a udbg backend that writes early kernel output to the hypervisor's debug console, and an
early-debug option for it; 0260 declares the debug console hcalls (105 to 109) it uses. One change.
It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. `PS3_LV1_CONS_UDBG` is unset and `PPC_EARLY_DEBUG` is off in our config, so neither is
compiled; only the declarations remain. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it SAFE.
