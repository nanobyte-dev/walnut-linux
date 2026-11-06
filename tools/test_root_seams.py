#!/usr/bin/env python3
"""The test seams of the root helpers (tools/run-tests.sh suite root-seams): ps3-imgmap's PS3_IMGMAP_*,
ps3-vswap's PS3_VSWAP_ROOT/PS3_VSWAP_BIN, ps3-update-kboot's PS3_BOOT_ROOT and ps3-hdd1-boot's
PS3_BOOT_ROOT/PS3_HDD1_DIR/PS3_HDD1_DEV are read only for a caller that is not root (or while a
root-owned /run/ps3-test-seams exists). A root process is faked with `unshare -Ur`; without it the
root half is skipped. python3 -I tools/test_root_seams.py"""
import os, shutil, subprocess, sys, tempfile, unittest
from pathlib import Path

# The root helpers ignore their test seams as root (they would act on this host's /etc and swap): never run as root.
if os.geteuid() == 0:
    sys.exit("%s: refuses to run as root (the scripts under test ignore their test seams as root and would touch this host)" % __file__)

ROOT = Path(__file__).resolve().parent.parent
IMGMAP = ROOT / "tools/ssdimg/ps3-imgmap"
VSWAP = ROOT / "packages/walnut/system/ps3-system/files/sbin/ps3-vswap"
KBOOT = ROOT / "packages/walnut/system/ps3-boot/ps3-update-kboot"
HDD1 = ROOT / "packages/walnut/system/ps3-boot/ps3-hdd1-boot"


def can_fake_root():
    try:
        return subprocess.run(["unshare", "-Ur", "id", "-u"], capture_output=True, text=True, timeout=10).stdout.strip() == "0"
    except (OSError, subprocess.TimeoutExpired):
        return False


FAKE_ROOT = can_fake_root()
AS_ROOT = ["unshare", "-Ur"]
PROBE = ("import importlib.machinery as m, importlib.util as u\n"
         "l = m.SourceFileLoader('imgmap', %r); s = u.spec_from_loader('imgmap', l); mod = u.module_from_spec(s); l.exec_module(mod)\n"
         "print(mod.DEVICE, mod.STATE, ' '.join(mod.DMSETUP))\n" % str(IMGMAP))


class Seams(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp(prefix="seams-"))

    def tearDown(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def run_cmd(self, cmd, root, env=None):
        e = dict(os.environ, **(env or {}))
        return subprocess.run((AS_ROOT if root else []) + cmd, env=e, capture_output=True, text=True, timeout=60)

    # ps3-imgmap
    IM = {"PS3_IMGMAP_TEST_DEVICE": "/dev/seam", "PS3_IMGMAP_STATE": "/tmp/seam-state", "PS3_IMGMAP_DMSETUP": "fakedm --x"}

    def test_imgmap_honours_seams_for_a_normal_user(self):
        p = self.run_cmd([sys.executable, "-I", "-c", PROBE], False, self.IM)
        self.assertEqual(p.stdout.split(), ["/dev/seam", "/tmp/seam-state", "fakedm", "--x"], p.stderr)

    @unittest.skipUnless(FAKE_ROOT, "no unshare -r")
    def test_imgmap_ignores_seams_as_root(self):
        p = self.run_cmd([sys.executable, "-I", "-c", PROBE], True, self.IM)
        self.assertEqual(p.stdout.split(), ["/dev/ps3db", "/var/lib/ps3-imgmap", "dmsetup"], p.stderr)

    # ps3-update-kboot
    def kboot_root(self):
        r = self.t / "kroot"
        (r / "etc/default").mkdir(parents=True)
        (r / "boot").mkdir()
        (r / "boot/vmlinux-6.12.1").write_bytes(b"\0")
        (r / "etc/default/ps3-boot").write_text('PS3_ROOT_ARGS="root=/dev/x ro"\n')
        return r

    def test_kboot_prefix_for_a_normal_user(self):
        r = self.kboot_root()
        p = self.run_cmd(["sh", str(KBOOT)], False, {"PS3_BOOT_ROOT": str(r)})
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertTrue((r / "etc/kboot.conf").exists())

    @unittest.skipUnless(FAKE_ROOT, "no unshare -r")
    def test_kboot_prefix_ignored_as_root(self):
        r = self.kboot_root()
        self.run_cmd(["sh", str(KBOOT)], True, {"PS3_BOOT_ROOT": str(r)})
        self.assertFalse((r / "etc/kboot.conf").exists())

    # ps3-hdd1-boot: the stand-in directory must not replace the device check for root
    @unittest.skipUnless(FAKE_ROOT, "no unshare -r")
    def test_hdd1_dir_ignored_as_root(self):
        r = self.kboot_root()
        d = self.t / "hdd1"
        d.mkdir()
        env = {"PS3_BOOT_ROOT": str(r), "PS3_HDD1_DIR": str(d), "PS3_HDD1_DEV": str(self.t / "nodev")}
        (r / "etc/default/ps3-boot").write_text('PS3_ROOT_ARGS="root=/dev/x ro ps3ssd.image=/x"\n')
        (r / "boot/config-6.12.1").write_text("CONFIG_UFS_FS=y\nCONFIG_BLK_DEV_DM=y\n")
        p = self.run_cmd(["sh", str(HDD1), "6.12.1"], True, env)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(list(d.iterdir()), [])

    # ps3-vswap: a fake findfs on the PS3_VSWAP_BIN path must not run for root
    def vswap_fixture(self):
        b = self.t / "bin"
        b.mkdir()
        log = self.t / "ran"
        (b / "findfs").write_text("#!/bin/sh\necho findfs >> %s\necho /dev/null\n" % log)
        (b / "findfs").chmod(0o755)
        r = self.t / "vroot"
        (r / "etc").mkdir(parents=True)
        (r / "etc/fstab").write_text("LABEL=swap none swap sw 0 0\n")
        return b, r, log

    def test_vswap_bin_for_a_normal_user(self):
        b, r, log = self.vswap_fixture()
        self.run_cmd(["sh", str(VSWAP)], False, {"PS3_VSWAP_BIN": str(b), "PS3_VSWAP_ROOT": str(r)})
        self.assertTrue(log.exists())

    @unittest.skipUnless(FAKE_ROOT, "no unshare -r")
    def test_vswap_seams_ignored_as_root(self):
        b, r, log = self.vswap_fixture()
        self.run_cmd(["sh", str(VSWAP)], True, {"PS3_VSWAP_BIN": str(b), "PS3_VSWAP_ROOT": str(r)})
        self.assertFalse(log.exists())


class Lint(unittest.TestCase):
    def test_vswap_fixes_its_path_before_it_runs_anything(self):
        lines = VSWAP.read_text().splitlines()
        path = next(i for i, l in enumerate(lines) if l.startswith("PATH=/usr/sbin"))
        first = next(i for i, l in enumerate(lines) if "id -u" in l)
        self.assertLess(path, first)

    def test_marker_makers_remove_it_on_every_exit(self):
        """both scripts run on the PS3 as root: the marker is made only around the call and a killed shell removes it"""
        usb = (ROOT / "tools/usb-root-remote.sh").read_text()
        i = usb.index("touch /run/ps3-test-seams")
        before = usb[:i]
        self.assertIn("trap 'rm -f /run/ps3-test-seams' EXIT", before)
        self.assertIn("HUP INT TERM", before.split("trap 'rm -f /run/ps3-test-seams' EXIT")[1])
        ssd = (ROOT / "tools/ssdimg/ssd-install-image.sh").read_text()
        j = ssd.index("touch /run/ps3-test-seams")
        self.assertIn('if [ -n "$R" ]; then', ssd[j - 80:j])          # never made on the PS3 itself
        cleanup = ssd[ssd.index("cleanup() {"):ssd.index("trap cleanup EXIT")]
        self.assertIn("rm -f /run/ps3-test-seams", cleanup)

    def test_usb_root_remote_sets_the_target_to_root(self):
        usb = (ROOT / "tools/usb-root-remote.sh").read_text()
        self.assertIn("echo PS3_BOOT_TARGET=root >>", usb)
        self.assertLess(usb.index("echo PS3_BOOT_TARGET=root"), usb.index("touch /run/ps3-test-seams"))

    def test_root_refusal_in_the_suites(self):
        for f in (ROOT / "tools/test_root_seams.py", ROOT / "tools/ssdimg/test_ssdimg.py",
                  ROOT / "packages/walnut/system/ps3-boot/tests/test_kboot.py"):
            self.assertIn("os.geteuid() == 0", f.read_text(), str(f))


if __name__ == "__main__":
    unittest.main()
