+++
component = "kernel-ps3"
patches = ["patches/ps3/0050-ps3sysmgr-lpar-reboot.patch"]
aliases = ["ps3/0050"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# Reboot asks for an LPAR reboot

`ps3_sys_manager_restart()` sends the System Manager `PS3_SM_NEXT_OP_LPAR_REBOOT` instead of
`PS3_SM_NEXT_OP_SYS_REBOOT`: a restart of the Linux partition rather than of the whole console. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository.
The sys-manager driver is built in. [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) rates it CAREFUL (a System Manager request, run time only)
and keeps it.

Our System Manager patches on the same driver are [kernel-sm-temperatures](../kernel-sm-temperatures/patch.md),
[kernel-sm-shutdown-wait](../kernel-sm-shutdown-wait/patch.md) and [kernel-sm-fan-policy](../kernel-sm-fan-policy/patch.md).
