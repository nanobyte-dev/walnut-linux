+++
component = "kernel"
patches = ["patches/local/0050-hid-sony-sixaxis-center-sticks.patch"]
aliases = ["local/0050", "kernel 0050"]
category = "correctness"
switch = "none"
default = "always on"
forwarded = "no"
tests = ["ps3:ds3-idle-check.py"]
batches = []
+++

# HID: sony: start the Sixaxis sticks centered

A DualShock 3 plugged in by USB sends no input report until its PS button is pressed, and until then
its input device keeps the values the input core started it with: 0 on every axis. The sticks are
logical 0..255 with the rest at 128, so 0 is both sticks pushed fully up and left (EVIOCGABS 0, joydev
-32767, SDL -32768), and games take that as a held stick. The patch sets ABS_X, ABS_Y, ABS_RX and ABS_RY
to the middle of their range in `input_configured`, before the device is registered, for the Sixaxis
and the Navigation controller; L2/R2 rest at 0. The pad's first real report replaces these.

Found on 2026-10-26 with QuakeSpasm on SDL3: with an inactive DS3 on the cable, the menu cursor ran up
by itself. Host check: `packages/patched/kernel/linux/tests/sixaxis-idle-axes.sh`, a uhid DS3 in a QEMU
guest (0 before the patch, 128 with it; not part of `tools/run-tests.sh`). On the PS3,
`packages/patched/kernel/linux/tests/ds3-idle-check.py` reads the axes of an inactive pad (TESTS.md
"kernel-0050"). An upstream candidate.
