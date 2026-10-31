+++
component = "kernel-ps3"
patches = ["patches/ps3/0080-ps3dispmgr.patch"]
aliases = ["ps3/0080"]
category = "feature"
switch = "none"
default = "always on"
authors = ["glevand", "graf_chokolo", "Sombat Tragoongoson", "Damian Dudycz"]
forwarded = "no"
tests = []
batches = []
+++

# The dispatcher manager driver (not built)

`/dev/ps3dispmngr`, a vuart driver for the dispatcher manager, which fronts the update manager, the
storage manager and the secure RTC; the device is set up only when the partition's LAID (read
from the repository) is 0x1070000002000001. It comes from glevand's OtherOS++ patch set as Sombat Tragoongoson refreshed it for 6.12 (the psx-place set), taken from Damian Dudycz's ps3 repository. graf_chokolo is a co-author.

[OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) classes it FORBIDDEN-capable (the update manager means firmware updates and EEPROM writes).
`CONFIG_PS3_DISP_MANAGER` is off in our config since the user's decision of 2026-10-18, so nothing
is built from it; the patch stays in the series. The module is also blacklisted while ps3rsx runs
([DRIVER.md](../../../docs/DRIVER.md#build-and-configuration)).
