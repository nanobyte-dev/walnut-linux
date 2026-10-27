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
ROOT = HERE.parents[4]   # the repository (packages/walnut/system/ps3-boot/tests)
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

    def builtins(self, *rels):
        for k in rels:
            (self.r / "boot" / ("config-" + k)).write_text("CONFIG_UFS_FS=y\nCONFIG_BLK_DEV_DM=y\n")

    def hdd1(self, extra=""):
        """the internal install: PS3_BOOT_TARGET=hdd1, dev_hdd1 stood in by a directory"""
        self.builtins(REL, OLD)
        (self.r / "etc/default/ps3-boot").write_text(
            'PS3_ROOT_ARGS="root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=/linux/walnut.img"\n'
            'PS3_EXTRA_ARGS=""\nPS3_DEFAULT_FLAVOUR=release\nPS3_BOOT_TARGET=hdd1\n' + extra)
        h = self.r / "hdd1"
        h.mkdir(exist_ok=True)
        e = dict(os.environ, PS3_BOOT_ROOT=str(self.r), PS3_HDD1_DIR=str(h),
                 PS3_HDD1_BOOT="sh " + str(HERE.parent / "ps3-hdd1-boot"))
        p = subprocess.run(["sh", str(SCRIPT)], env=e, capture_output=True, text=True, timeout=30)
        return p, h

    def test_hdd1_walnut_entries(self):
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"initrd-1")
        h = self.r / "hdd1"
        h.mkdir()
        (h / "kboot.conf").write_text("other='/x/vmlinux root=/dev/sda2'\nwalnut='stale'\n")
        (h / "linux/boot").mkdir(parents=True)
        (h / "linux/boot/vmlinux-6.12.111+ps3.400").write_bytes(b"old")
        p, h = self.hdd1()
        self.assertEqual(p.returncode, 0, p.stderr)
        lines = (h / "kboot.conf").read_text().splitlines()
        walnut = [x for x in lines if x.startswith("walnut=")]
        prev = [x for x in lines if x.startswith("walnut-prev=")]
        self.assertEqual(len(walnut), 1, lines)
        self.assertIn("/linux/boot/vmlinux-%s initrd=/linux/boot/initrd-walnut.img root=/dev/mapper/ps3root" % REL, walnut[0])
        self.assertIn("ps3ssd.image=/linux/walnut.img", walnut[0])
        self.assertIn(RARGS, walnut[0])
        self.assertEqual(len(prev), 1, lines)
        # PS3_EXTRA_ARGS is empty here: no double space, which Petitboot passed on as "(null)"
        for x in walnut + prev + [y for y in (self.r / "etc/kboot.conf").read_text().splitlines() if "='" in y]:
            self.assertNotIn("  ", x)
            self.assertFalse(x.endswith(" '"), x)
        self.assertIn("other='/x/vmlinux root=/dev/sda2'", lines)          # another install's entry kept
        self.assertTrue((h / "linux/boot" / ("vmlinux-" + REL)).exists())
        self.assertFalse((h / "linux/boot/vmlinux-6.12.111+ps3.400").exists())   # ours, not current: removed
        self.assertEqual((h / "linux/boot/initrd-walnut.img").read_bytes(), b"initrd-1")

    def test_hdd1_new_initrd_keeps_prev(self):
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"initrd-1")
        p, h = self.hdd1()
        self.assertEqual(p.returncode, 0, p.stderr)
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"initrd-2")
        p, h = self.hdd1()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual((h / "linux/boot/initrd-walnut.img").read_bytes(), b"initrd-2")
        self.assertEqual((h / "linux/boot/initrd-walnut-prev.img").read_bytes(), b"initrd-1")
        prev = [x for x in (h / "kboot.conf").read_text().splitlines() if x.startswith("walnut-prev=")]
        self.assertIn("initrd=/linux/boot/initrd-walnut-prev.img", prev[0])

    def test_hdd1_detected_from_the_generic_initramfs(self):
        """no PS3_BOOT_TARGET: the generic initramfs's log says this system boots from dev_hdd1"""
        self.builtins(REL, OLD)
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"i")
        (self.r / "run/ps3ssd").mkdir(parents=True)
        (self.r / "run/ps3ssd/init.log").write_text("ps3ssd: initramfs kind=generic release= built=x\n")
        (self.r / "etc/default/ps3-boot").write_text(
            'PS3_ROOT_ARGS="root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=/linux/walnut.img"\n'
            'PS3_DEFAULT_FLAVOUR=release\n')
        h = self.r / "hdd1"
        h.mkdir()
        e = dict(os.environ, PS3_BOOT_ROOT=str(self.r), PS3_HDD1_DIR=str(h),
                 PS3_HDD1_BOOT="sh " + str(HERE.parent / "ps3-hdd1-boot"))
        p = subprocess.run(["sh", str(SCRIPT)], env=e, capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("boots from dev_hdd1", p.stdout)
        self.assertTrue(any(x.startswith("walnut=") for x in (h / "kboot.conf").read_text().splitlines()))

    def test_maps_initramfs_or_usb_stays_on_the_root(self):
        """the SSD root of the maps kind (kernel from the USB disk) and a USB root: no dev_hdd1 writes"""
        (self.r / "run/ps3ssd").mkdir(parents=True)
        (self.r / "run/ps3ssd/init.log").write_text("ps3ssd: initramfs kind=maps release=6.12.111+ps3.648\n")
        h = self.r / "hdd1"
        h.mkdir()
        e = dict(os.environ, PS3_BOOT_ROOT=str(self.r), PS3_HDD1_DIR=str(h),
                 PS3_HDD1_BOOT="sh " + str(HERE.parent / "ps3-hdd1-boot"))
        p = subprocess.run(["sh", str(SCRIPT)], env=e, capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertNotIn("dev_hdd1", p.stdout)
        self.assertEqual(list(h.iterdir()), [])
        self.assertTrue((self.r / "etc/kboot.conf").exists())

    def test_hdd1_only_kernels_with_builtin_ufs_dm(self):
        """walnut-prev never points at a kernel the generic initramfs can't boot (ufs/dm as modules)"""
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"i")
        p, h = self.hdd1()
        (self.r / "boot" / ("config-" + OLD)).write_text("CONFIG_UFS_FS=m\nCONFIG_BLK_DEV_DM=m\n")
        shutil.rmtree(h)
        p, h = self.hdd1_again()
        self.assertEqual(p.returncode, 0, p.stderr)
        lines = (h / "kboot.conf").read_text().splitlines()
        self.assertTrue(any(x.startswith("walnut=") and REL in x for x in lines), lines)
        self.assertFalse(any(x.startswith("walnut-prev=") for x in lines), lines)

    def hdd1_again(self):
        h = self.r / "hdd1"
        h.mkdir(exist_ok=True)
        e = dict(os.environ, PS3_BOOT_ROOT=str(self.r), PS3_HDD1_DIR=str(h),
                 PS3_HDD1_BOOT="sh " + str(HERE.parent / "ps3-hdd1-boot"))
        return subprocess.run(["sh", str(SCRIPT)], env=e, capture_output=True, text=True, timeout=30), h

    def test_dpkg_temporary_files_are_not_kernels(self):
        """mid-upgrade dpkg leaves vmlinux-<release>.dpkg-tmp: never an entry, on the root or on dev_hdd1"""
        (self.r / "boot" / ("vmlinux-" + REL + ".dpkg-tmp")).write_bytes(b"\0")
        (self.r / "boot" / ("vmlinux-" + REL + ".dpkg-new")).write_bytes(b"\0")
        lines, out = self.run_it()
        self.assertNotIn("dpkg-", "\n".join(lines))
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"i")
        p, h = self.hdd1()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertNotIn("dpkg-", (h / "kboot.conf").read_text())
        self.assertEqual([f.name for f in (h / "linux/boot").iterdir() if "dpkg" in f.name], [])

    def test_hdd1_prev_is_the_previous_release_not_the_debug_twin(self):
        """a new release with its debug twin: walnut the release, walnut-prev the older release"""
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"i")
        new, newdbg = "6.12.111+ps3.700", "6.12.111+ps3.700-debug202610300000"
        for k in (new, newdbg):
            self.kernel(k)
        self.builtins(new, newdbg)
        p, h = self.hdd1()
        self.assertEqual(p.returncode, 0, p.stderr)
        lines = (h / "kboot.conf").read_text().splitlines()
        walnut = [x for x in lines if x.startswith("walnut=")][0]
        prev = [x for x in lines if x.startswith("walnut-prev=")][0]
        self.assertIn("vmlinux-%s " % new, walnut)
        self.assertIn("vmlinux-%s " % REL, prev)

    def test_hdd1_refuses_without_image_root(self):
        (self.r / "usr/lib/ps3/boot/initrd-walnut.img").write_bytes(b"i")
        (self.r / "etc/default/ps3-boot").write_text('PS3_ROOT_ARGS="root=/dev/sda2"\nPS3_BOOT_TARGET=hdd1\n')
        h = self.r / "hdd1"
        h.mkdir()
        e = dict(os.environ, PS3_BOOT_ROOT=str(self.r), PS3_HDD1_DIR=str(h),
                 PS3_HDD1_BOOT="sh " + str(HERE.parent / "ps3-hdd1-boot"))
        p = subprocess.run(["sh", str(SCRIPT)], env=e, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("not an internal install", p.stderr)
        self.assertFalse((h / "kboot.conf").exists())

    def test_kernels_only(self):
        lines, out = self.run_it()
        self.assertEqual(self.entries(lines), [DBG, REL, OLD])   # Debian's order: newest first
        self.assertIn("%s='/boot/vmlinux-%s root=PARTUUID=50533301-02 rootfstype=ext4 ro rootwait ip=dhcp %s'"
                      % (REL, REL, RARGS), lines)
        self.assertEqual(lines[-1], "default=" + REL)
        self.assertIn("0 extra entries", out)

    def test_kernel_without_gpu_driver_listed_last(self):
        # the newest release kernel's ps3rsx module isn't published yet: the older one, which has it,
        # boots (the first entry) and is the release default; the new one is still listed
        for k in (DBG, OLD):
            m = self.r / "lib/modules" / k / "updates"
            m.mkdir(parents=True)
            (m / "ps3rsx.ko").write_bytes(b"\0")
        lines, out = self.run_it()
        self.assertEqual(self.entries(lines), [DBG, OLD, REL])
        self.assertEqual(lines[-1], "default=" + OLD)
        self.assertIn("no GPU driver (ps3rsx) for " + REL, out)

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
