#!/usr/bin/env python3
"""ps3-desktop's X wrapper setting (tools/run-tests.sh suite xwrapper): its postinst block leaves
/etc/X11/Xwrapper.config with allowed_users=console (a VT login, the tty1 autologin's startx; an SSH login is
refused) and needs_root_rights=yes, from a stock file, an old "anybody" one, spaced keys, or none. The start path
(getty autologin on tty1, profile.d startx with the tty as stdin) is checked for what the wrapper needs.
  python3 -I packages/walnut/system/ps3-desktop/tests/test_xwrapper.py"""
import re, shutil, subprocess, tempfile, unittest
from pathlib import Path

PKG = Path(__file__).resolve().parent.parent


def block():
    text = (PKG / "debian/postinst").read_text()
    m = re.search(r"\t# Only a user logged in on a physical console.*?\n\tfi\n", text, re.S)
    assert m
    return m.group(0)


class Xwrapper(unittest.TestCase):
    def run_block(self, initial):
        t = Path(tempfile.mkdtemp(prefix="xw-"))
        self.addCleanup(shutil.rmtree, t, True)
        cfg = t / "Xwrapper.config"
        if initial is not None:
            cfg.write_text(initial)
        b = t / "bin"
        b.mkdir()
        (b / "debconf-set-selections").write_text("#!/bin/sh\ncat >> %s\n" % (t / "debconf"))
        (b / "debconf-set-selections").chmod(0o755)
        for tool in ("sed", "grep", "cat"):
            (b / tool).symlink_to(shutil.which(tool))
        script = block().replace("/etc/X11/Xwrapper.config", str(cfg))
        p = subprocess.run(["/bin/sh", "-ec", script], env={"PATH": str(b)}, capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        lines = [l.strip() for l in cfg.read_text().splitlines() if l.strip() and not l.startswith("#")]
        return lines, (t / "debconf").read_text()

    def test_stock_file(self):
        lines, deb = self.run_block("# Xwrapper.config\nallowed_users=console\nneeds_root_rights=auto\n")
        self.assertEqual(lines, ["allowed_users=console", "needs_root_rights=yes"])
        self.assertIn("allowed_users select console", deb)

    def test_old_anybody_file_is_switched(self):
        lines, _ = self.run_block("allowed_users=anybody\nneeds_root_rights=yes\n")
        self.assertEqual(lines, ["allowed_users=console", "needs_root_rights=yes"])

    def test_spaced_or_missing_keys(self):
        lines, _ = self.run_block("allowed_users = anybody\n")
        self.assertEqual(sorted(lines), ["allowed_users=console", "needs_root_rights=yes"])
        lines, _ = self.run_block("# only a comment\n")
        self.assertEqual(sorted(lines), ["allowed_users=console", "needs_root_rights=yes"])

    def test_no_file(self):
        lines, _ = self.run_block(None)
        self.assertEqual(lines, ["allowed_users=console", "needs_root_rights=yes"])

    def test_nothing_says_anybody(self):
        self.assertNotIn("anybody", (PKG / "debian/postinst").read_text())

    def test_the_start_path_keeps_a_console_as_stdin(self):
        """Xorg.wrap checks that the caller is on a physical console: X is started from the tty1 login shell
        (autologin), with only stdout and stderr redirected, so stdin stays /dev/tty1"""
        prof = (PKG / "files/profile.d/ps3-desktop.sh").read_text()
        self.assertIn('[ "$(tty 2>/dev/null)" = /dev/tty1 ]', prof)
        self.assertRegex(prof, r"startx /usr/bin/ps3-session -- :0 .*> \"\$HOME/\.xsession\.log\" 2>&1")
        self.assertNotRegex(prof, r"startx[^\n]*<")
        self.assertIn("getty@tty1.service.d", (PKG / "files/generators/ps3-desktop-autologin").read_text())


if __name__ == "__main__":
    unittest.main()
