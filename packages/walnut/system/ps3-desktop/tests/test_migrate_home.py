#!/usr/bin/env python3
"""ps3-migrate-home (tools/run-tests.sh suite migrate-home): as the user it moves the old desktop files into
~/.local/share/ps3-legacy; as root it starts over as the user through setpriv --reuid/--regid/--clear-groups
(a stub `id` says root, a stub `setpriv` records its arguments), and never for the root user itself.
  python3 -I packages/walnut/system/ps3-desktop/tests/test_migrate_home.py"""
import os, shutil, subprocess, tempfile, unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "files" / "sbin" / "ps3-migrate-home"


class Migrate(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp(prefix="mh-"))
        self.home = self.t / "home"
        (self.home).mkdir()
        (self.home / ".bash_profile").write_text("exec startx /usr/local/bin/ps3-session\n")
        self.bin = self.t / "bin"
        self.bin.mkdir()
        self.log = self.t / "setpriv.log"

    def tearDown(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def stub(self, name, body):
        (self.bin / name).write_text("#!/bin/sh\n" + body)
        (self.bin / name).chmod(0o755)

    def run_it(self, uid, euid):
        self.stub("getent", 'echo "bob:x:%s:%s::%s:/bin/sh"\n' % (uid, uid + 1, self.home))
        self.stub("id", 'echo %s\n' % euid)
        self.stub("setpriv", 'echo "$*" >> %s\n' % self.log)
        e = dict(os.environ, PATH="%s:%s" % (self.bin, os.environ["PATH"]))
        return subprocess.run(["sh", str(SCRIPT), "bob"], env=e, capture_output=True, text=True, timeout=30)

    def test_user_moves_the_files(self):
        p = self.run_it(1000, 1000)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertTrue((self.home / ".local/share/ps3-legacy/.bash_profile").exists())
        self.assertFalse(self.log.exists())

    def test_root_drops_privileges_for_the_copy(self):
        p = self.run_it(1000, 0)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(self.log.read_text().split()[:3], ["--reuid=1000", "--regid=1001", "--clear-groups"])
        self.assertTrue((self.home / ".bash_profile").exists(), "root itself must not touch the home")

    def test_root_user_is_not_reexecuted(self):
        p = self.run_it(0, 0)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertFalse(self.log.exists())
        self.assertTrue((self.home / ".local/share/ps3-legacy/.bash_profile").exists())


if __name__ == "__main__":
    unittest.main()
