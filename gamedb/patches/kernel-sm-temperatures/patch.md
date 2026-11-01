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
[kernel-sm-fan-policy](../kernel-sm-fan-policy/patch.md) (local/0042) build on it. Its temperature reply handler returns 0, like the event handler: until
2026-10-31 it returned -EAGAIN to keep the old shutdown poll going and 0041 changed that, a return value
now folded into this patch ([MERGES.md](../MERGES.md)) so that 0041 touches only the upstream code.
The final source is unchanged, but this patch alone would let a late temperature reply end the old
poll, like an event; the shipped kernel always has 0041 on top. Validated on the PS3 in this form, below.

## Validation

Validated on 2026-11-01 (PS3 session K, the merged test kernel 6.12.111+ps3.1210~dev20261031171117 with ps3rsx 0.1209): hwmon `ps3_sm` read both zones, Cell 63.8 C and RSX 57.5 C at
the start of the session and 63.5 / 57.3 C at its end. The first reply logged its one info line; no
error, no stray message lines, and the sensors stayed on through the session.

```
ps3_sys_manager vuart_02: hwmon: 2 temperature zones, read on demand
ps3_sys_manager vuart_02: temperature reply: service id 14, payload 8, tag 0
/sys/class/hwmon/hwmon0 ps3_sm 63762 57500
```
