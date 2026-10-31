+++
component = "kernel"
patches = ["patches/local/0042-ps3-sys-manager-fan-policy.patch"]
aliases = ["local/0042", "kernel 0042"]
category = "feature"
switch = "ps3_sys_manager.fan_policy, ps3_sys_manager.fan_floor"
default = "auto (SYSCON automatic at boot and on every exit path)"
forwarded = "no (test on the PS3 first; docs/THERMAL.md)"
tests = []
batches = []
+++

# Fan policy through the System Manager (hwmon pwm1)

SYSCON keeps a fan policy a guest set in its RAM after the guest is gone: a manual duty webMAN set in
GameOS stays in force under Linux, where nothing runs a control loop. This patch uses the System
Manager's FAN_POLICY service (the one behind GameOS's `sys_sm_set_fan_policy` and
`sys_sm_get_fan_policy`). At boot it logs the policy GameOS left, sets SYSCON automatic, and then full
speed or a manual floor only if the command line asks (`ps3_sys_manager.fan_policy=auto|keep|full|off`,
`fan_floor=<duty>`). hwmon `ps3_sm` gets `pwm1_enable` (0 full speed, 1 manual, 2 automatic) and `pwm1`.

The design is the project's fan rule ([THERMAL.md](../../../docs/THERMAL.md#the-fan-driver-local0042)):
a manual duty is a fixed floor clamped to 0x99-0xf4 (60-95%) with no control loop, and SYSCON automatic
comes back on every exit path when anything else was set: `.shutdown`, the final power off and restart,
and a panic notifier, the lockless ones through a new `ps3_vuart_write_atomic()`. Failures fall back to
automatic, a refused set leaves only automatic for the boot, and a thermal alert hands a manual duty back
to SYSCON. Nothing else is sent, and SYSCON's EEPROM is never written.

Builds on [kernel-sm-temperatures](../kernel-sm-temperatures/patch.md). On kernel .433 the fan went to
automatic at boot, and full speed and back to automatic worked; the floor, the panic path and the get
reply's layout are not confirmed yet.
