+++
component = "kernel"
patches = ["patches/local/0040-ps3-sys-manager-hwmon-temperatures.patch"]
aliases = ["local/0040", "kernel 0040"]
category = "feature"
switch = "ps3_sys_manager.hwmon"
default = "on (ps3_sys_manager.hwmon=0 turns the sensors off)"
forwarded = "not yet (test on the PS3 first)"
tests = []
batches = []
+++

# The System Manager's temperature sensors as hwmon

SYSCON measures the Cell's and the RSX's temperatures, and GameOS reads them through the System Manager
(service 13). Linux had no way to see them. With `CONFIG_PS3_SYS_MANAGER_HWMON` the sys-manager driver
registers a hwmon device `ps3_sm` with `temp*_input`, `temp*_label` and `temp*_alarm` for the zones in
`ps3_sys_manager.temp_zones` (default 0,1: Cell BE, RSX). A read sends one get request on the driver's
own vuart port and waits up to a second for the reply; values are cached for a second, one request at a
time. The request's bytes are those of ps3vuart-tools' `get_temperature` (glevand, sguerrini97), which
read the same values under OtherOS++ through glevand's raw System Manager device, which we dropped
(ps3/0060, [OTHEROS-PATCHES.md](../../../docs/OTHEROS-PATCHES.md) decision 2).

The design point is caution: nothing but that get request is ever sent, and a REQUEST_ERROR or three
failures in a row turn the sensors off for the boot. The patch also hardens the existing message paths
so that a second sender can't break the stream: header and payload go out in one write, `send_attr`
no longer sends 8 bytes of stack, an unexpected payload size is logged instead of hitting `BUG()`, and
unhandled payloads are read out of the receive list instead of being parsed as the next header.

Details and what was confirmed on the PS3 (kernel .433 read both zones) are in
[THERMAL.md](../../../docs/THERMAL.md#the-hwmon-driver-local0040).
[kernel-sm-shutdown-wait](../kernel-sm-shutdown-wait/patch.md) (local/0041) and
[kernel-sm-fan-policy](../kernel-sm-fan-policy/patch.md) (local/0042) build on it; 0041 changed this
patch's temperature reply handler to return 0.
