#!/usr/bin/python3 -I
"""ds3-idle-check.py: on the PS3, the axes a DualShock 3's input device starts with
(linux/patches/local/0050). Plug the pad in by USB and don't press its PS button, then run this
(no root needed when the user may read /dev/input/event*; else sudo). It reads every evdev node
named like the DS3 with EVIOCGABS: with the patch the sticks (ABS_X, ABS_Y, ABS_RX, ABS_RY) are
128 and the triggers (ABS_Z, ABS_RZ) 0; without it everything is 0, the sticks fully up and left.
After the PS button the values are whatever the pad reports. Exit 0 when every pad found is
centered, 1 when one isn't, 2 when none is found."""
import fcntl
import glob
import os
import struct
import sys

# ioctl numbers: powerpc has 13 size bits and the direction at bit 29, x86 14 and bit 30
PPC = os.uname().machine.startswith("ppc")
DIRSHIFT, READ = (29, 2) if PPC else (30, 2)


def ior(nr, size):
    return (READ << DIRSHIFT) | (size << 16) | (ord("E") << 8) | nr


def eviocgname(n):
    return ior(0x06, n)


def eviocgabs(code):
    return ior(0x40 + code, 24)


AXES = {"ABS_X": 0x00, "ABS_Y": 0x01, "ABS_Z": 0x02, "ABS_RX": 0x03, "ABS_RY": 0x04, "ABS_RZ": 0x05}
WANT = {"ABS_X": 128, "ABS_Y": 128, "ABS_RX": 128, "ABS_RY": 128, "ABS_Z": 0, "ABS_RZ": 0}

print("kernel", os.uname().release)
found = bad = 0
for path in sorted(glob.glob("/dev/input/event*"), key=lambda p: int(p[16:])):
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as e:
        print(f"{path}: {e.strerror}")
        continue
    try:
        buf = bytearray(256)
        n = fcntl.ioctl(fd, eviocgname(len(buf)), buf)
        name = bytes(buf[:n]).split(b"\0")[0].decode(errors="replace")
        if "PLAYSTATION(R)3" not in name or "Motion" in name:
            continue
        found += 1
        print(f"{path}: {name}")
        for axis, code in AXES.items():
            info = bytearray(24)
            fcntl.ioctl(fd, eviocgabs(code), info)
            value, lo, hi, fuzz, flat, res = struct.unpack("=6i", info)
            ok = value == WANT[axis]
            bad += not ok
            print(f"  {axis:6} value={value:3} (range {lo}..{hi}) {'ok' if ok else 'NOT at rest, want ' + str(WANT[axis])}")
    finally:
        os.close(fd)
if not found:
    print("no DS3 input device found (plugged in? hid_sony loaded?)")
    sys.exit(2)
print("PASS: the pad starts at rest" if not bad else "FAIL: the pad starts with a stick pushed (or one is held)")
sys.exit(1 if bad else 0)
