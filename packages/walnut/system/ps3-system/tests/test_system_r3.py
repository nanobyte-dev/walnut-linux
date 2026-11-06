#!/usr/bin/env python3
"""ps3-system's small system fixes of round 3 (tools/run-tests.sh suite ps3-system-r3):
ds3-pair writes BlueZ's storage and asks for a bluetooth restart only when the pairing changed;
ps3-vswap.service is ordered after what it needs; DRIVER.md says who can hold the escape hatch.
  python3 -I packages/walnut/system/ps3-system/tests/test_system_r3.py"""
import importlib.machinery, importlib.util, re, shutil, subprocess, tempfile, unittest
from pathlib import Path

PKG = Path(__file__).resolve().parent.parent
ROOT = PKG.parents[3]


def load_ds3():
    l = importlib.machinery.SourceFileLoader("ds3pair", str(PKG / "files/sbin/ds3-pair"))
    m = importlib.util.module_from_spec(importlib.util.spec_from_loader("ds3pair", l))
    l.exec_module(m)
    return m


class Ds3Pair(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp(prefix="ds3-"))
        self.m = load_ds3()

    def tearDown(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def test_first_plug_writes_and_asks_for_a_restart(self):
        self.assertTrue(self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF"))
        info = (self.t / "AA:BB:CC:DD:EE:FF/info").read_text()
        self.assertIn("Trusted=true", info)

    def test_same_controller_again_needs_no_restart(self):
        self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF")
        self.assertFalse(self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF"))

    def test_bluez_additions_are_kept_and_need_no_restart(self):
        self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF")
        f = self.t / "AA:BB:CC:DD:EE:FF/info"
        f.write_text(f.read_text() + "\n[LinkKey]\nKey=00112233\n")
        self.assertFalse(self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF"))
        self.assertIn("[LinkKey]", f.read_text())

    def test_a_second_controller_changes(self):
        self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF")
        self.assertTrue(self.m.register(str(self.t), "11:22:33:44:55:66"))

    def test_an_untrusted_record_is_repaired(self):
        self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF")
        f = self.t / "AA:BB:CC:DD:EE:FF/info"
        f.write_text(f.read_text().replace("Trusted=true", "Trusted=false"))
        self.assertTrue(self.m.register(str(self.t), "AA:BB:CC:DD:EE:FF"))
        self.assertIn("Trusted=true", f.read_text())


class VswapUnit(unittest.TestCase):
    def unit(self):
        sec, d = None, {}
        for line in (PKG / "debian/ps3-system.ps3-vswap.service").read_text().splitlines():
            if line.startswith("["):
                sec = line
            elif "=" in line and not line.startswith("#"):
                k, v = line.split("=", 1)
                d.setdefault((sec, k), []).extend(v.split())
        return d

    def test_ordered_after_what_it_needs(self):
        after = self.unit()[("[Unit]", "After")]
        for need in ("systemd-modules-load.service", "systemd-udev-trigger.service", "local-fs.target"):
            self.assertIn(need, after)

    def test_no_wait_added_for_the_boot(self):
        d = self.unit()
        # nothing slower than the default basic.target ordering, and no ordering that makes others wait
        for slow in ("systemd-udev-settle.service", "network-online.target", "multi-user.target"):
            self.assertNotIn(slow, d[("[Unit]", "After")])
        self.assertNotIn(("[Unit]", "Before"), d)
        self.assertNotIn(("[Unit]", "DefaultDependencies"), d)


class Docs(unittest.TestCase):
    def test_escape_hatch_says_who_can_hold_it(self):
        text = (ROOT / "docs/DRIVER.md").read_text()
        line = [l for l in text.splitlines() if l.startswith("- **The escape hatch:**")][0]
        self.assertIn("`input` group", line)


class SudoersPostinst(unittest.TestCase):
    """The three postinsts that drop their own sudoers file when it does not parse."""
    PKGS = {"ps3-desktop": "packages/walnut/system/ps3-desktop", "walnut-rate": "packages/walnut/apps/walnut-rate",
            "walnut-games": "packages/walnut/apps/walnut-games"}

    def block(self, name):
        text = (ROOT / self.PKGS[name] / "debian/postinst").read_text()
        m = re.search(r"\t# The sudoers drop-in.*?\n\tfi\n", text, re.S)
        self.assertTrue(m, name)
        return m.group(0)

    def run_block(self, name, visudo_rc):
        t = Path(tempfile.mkdtemp(prefix="sudoers-"))
        self.addCleanup(shutil.rmtree, t, True)
        (t / "d").mkdir()
        (t / "d" / name).write_text("x\n")
        (t / "d" / "other").write_text("y\n")
        b = t / "bin"
        b.mkdir()
        (b / "rm").symlink_to(shutil.which("rm"))
        if visudo_rc is not None:
            (b / "visudo").write_text("#!/bin/sh\nexit %d\n" % visudo_rc)
            (b / "visudo").chmod(0o755)
        script = self.block(name).replace("/etc/sudoers.d", str(t / "d"))
        subprocess.run(["/bin/sh", "-c", script], env={"PATH": "%s:/usr/bin:/bin" % b if visudo_rc is not None else str(b)},
                       capture_output=True, text=True)
        return sorted(x.name for x in (t / "d").iterdir())

    def test_a_file_that_parses_stays(self):
        for n in self.PKGS:
            self.assertEqual(self.run_block(n, 0), sorted([n, "other"]), n)

    def test_a_file_that_does_not_parse_goes_alone(self):
        for n in self.PKGS:
            self.assertEqual(self.run_block(n, 1), ["other"], n)

    def test_no_visudo_leaves_the_file(self):
        for n in self.PKGS:
            self.assertEqual(self.run_block(n, None), sorted([n, "other"]), n)


if __name__ == "__main__":
    unittest.main()
