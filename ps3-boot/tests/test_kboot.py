#!/usr/bin/env python3
"""Host tests of ps3-update-kboot (tools/run-tests.sh suite ps3-boot): the kernels' entries, the default,
and the extra entries of /etc/kboot.d (docs/GAMEOS-HDD.md "Phase B"): kept verbatim across kernel
installs and removals, left out (as a comment) when their label is taken, when they aren't entries,
or when their image or initrd is missing. Runs the script on a fake root (PS3_BOOT_ROOT)."""
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SCRIPT = HERE.parent / "ps3-update-kboot"
SCRATCH = ROOT / "build" / "tmp" / "ps3-boot-test"

REL = "6.12.111+ps3.472"
DBG = "6.12.111+ps3.472-debug202610181726"
OLD = "6.12.111+ps3.457"
RARGS = "video=ps3fb:mode:133 consoleblank=0 console=tty1 quiet splash"
SSD = ("ssd-%s='/boot/vmlinux-%s initrd=/boot/initrd-ssd-%s.img root=/dev/mapper/ps3root rootfstype=ext4 ro "
       "ps3ssd.image=/linux/ssdtest.img ip=dhcp %s'" % (REL, REL, REL, RARGS))


class Kboot(unittest.TestCase):
    def setUp(self):
        self.r = SCRATCH / self._testMethodName
        shutil.rmtree(self.r, ignore_errors=True)
        for d in ("boot", "etc/default", "etc/kboot.d", "usr/lib/ps3/boot", "proc"):
            (self.r / d).mkdir(parents=True)
        (self.r / "etc/default/ps3-boot").write_text(
            'PS3_ROOT_ARGS="root=PARTUUID=50533301-02 rootfstype=ext4 ro rootwait"\n'
            'PS3_EXTRA_ARGS="ip=dhcp"\nPS3_DEFAULT_FLAVOUR=release\n')
        for k in (REL, DBG, OLD):
            self.kernel(k)
        (self.r / "usr/lib/ps3/boot" / (REL + ".args")).write_text(RARGS + "\n")
        (self.r / "usr/lib/ps3/boot" / (DBG + ".args")).write_text("video=ps3fb:mode:133 loglevel=4\n")

    def tearDown(self):
        shutil.rmtree(self.r, ignore_errors=True)

    def kernel(self, k):
        (self.r / "boot" / ("vmlinux-" + k)).write_bytes(b"\0")

    def run_it(self):
        e = dict(os.environ, PS3_BOOT_ROOT=str(self.r))
        p = subprocess.run(["sh", str(SCRIPT)], env=e, capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode, 0, p.stderr)
        return (self.r / "etc/kboot.conf").read_text().splitlines(), p.stdout + p.stderr

    def entries(self, lines):
        return [x.split("=", 1)[0] for x in lines if x and not x.startswith("#") and not x.startswith("default=")]

    def test_kernels_only(self):
        lines, out = self.run_it()
        self.assertEqual(self.entries(lines), [DBG, REL, OLD])   # Debian's order: newest first
        self.assertIn("%s='/boot/vmlinux-%s root=PARTUUID=50533301-02 rootfstype=ext4 ro rootwait ip=dhcp %s'"
                      % (REL, REL, RARGS), lines)
        self.assertEqual(lines[-1], "default=" + REL)
        self.assertIn("0 extra entries", out)

    def test_ssd_entry_kept_verbatim(self):
        (self.r / "boot" / ("initrd-ssd-%s.img" % REL)).write_bytes(b"\0")
        (self.r / "etc/kboot.d/ssd.conf").write_text("# the SSD root (docs/GAMEOS-HDD.md)\n\n" + SSD + "\n")
        lines, out = self.run_it()
        self.assertEqual(self.entries(lines), [DBG, REL, OLD, "ssd-" + REL])
        self.assertIn(SSD, lines)
        self.assertEqual(lines[-1], "default=" + REL)   # extra entries never become the default
        self.assertIn("1 extra entry", out)
        # a new kernel arrives, an old one goes: the entry stays
        self.kernel("6.12.111+ps3.480")
        (self.r / "boot" / ("vmlinux-" + OLD)).unlink()
        lines, out = self.run_it()
        self.assertEqual(self.entries(lines), ["6.12.111+ps3.480", DBG, REL, "ssd-" + REL])
        self.assertIn(SSD, lines)

    def test_missing_files_left_out_then_back(self):
        (self.r / "etc/kboot.d/ssd.conf").write_text(SSD + "\n")
        lines, out = self.run_it()
        self.assertNotIn(SSD, lines)
        self.assertIn("# /etc/kboot.d/ssd.conf: left out: ssd-%s: missing /boot/initrd-ssd-%s.img" % (REL, REL), lines)
        self.assertIn("left out", out)
        (self.r / "boot" / ("initrd-ssd-%s.img" % REL)).write_bytes(b"\0")
        lines, out = self.run_it()
        self.assertIn(SSD, lines)
        (self.r / "boot" / ("vmlinux-" + REL)).unlink()   # its kernel removed: the entry goes with it
        lines, out = self.run_it()
        self.assertNotIn(SSD, lines)
        self.assertTrue(any("missing /boot/vmlinux-%s" % REL in x for x in lines), lines)

    def test_refused_lines(self):
        (self.r / "boot" / "x.img").write_bytes(b"\0")
        (self.r / "etc/kboot.d/10-bad.conf").write_text(
            "%s='/boot/vmlinux-%s root=/dev/sda1'\n" % (REL, REL) +      # a kernel's label
            "default=ssd\n"                                               # a global
            "just some words\n"                                           # not an entry
            "bad label='/boot/vmlinux-%s'\n" % REL +                     # a space in the label
            "dup='/boot/vmlinux-%s initrd=/boot/x.img'\n" % REL +
            "dup='/boot/vmlinux-%s'\n" % REL +                            # taken by the line before
            "other='ps3dc:/linux/vmlinux initrd=ps3dc:/linux/initrd.img root=/dev/x'\n")   # another device: not checked
        (self.r / "etc/kboot.d/20-next.conf").write_text("zz='/boot/vmlinux-%s'\n" % OLD)
        (self.r / "etc/kboot.d/README").write_text("not a .conf: ignored\n")
        lines, out = self.run_it()
        self.assertEqual(self.entries(lines), [DBG, REL, OLD, "dup", "other", "zz"])
        self.assertEqual(sum(1 for x in lines if x.startswith("# /etc/kboot.d/10-bad.conf: left out")), 5, lines)
        self.assertIn("label %s is already taken" % REL, "\n".join(lines))
        self.assertIn("a global setting", "\n".join(lines))
        self.assertEqual(lines[-1], "default=" + REL)


if __name__ == "__main__":
    r = unittest.main(exit=False, verbosity=2).result
    shutil.rmtree(SCRATCH, ignore_errors=True)
    sys.exit(0 if r.wasSuccessful() else 1)
