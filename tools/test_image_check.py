#!/usr/bin/env python3
"""Host test of tools/image-check.py on fabricated rootfs tars: a clean one passes; a dev account, a dev
sudoers file, the LAN repository, a licensed content path, a licensed package in the status file, a
missing status file and an unreadable tar each fail. Run as python3 -I tools/test_image_check.py."""
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "image-check.py")
STATUS = "Package: bash\nStatus: install ok installed\nSection: shells\nVersion: 5\n\n"


def licensed_name_and_glob():
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "licensed.py"), "names"], stdout=subprocess.PIPE, text=True, check=True)
    g = subprocess.run([sys.executable, "-I", os.path.join(HERE, "licensed.py"), "globs"], stdout=subprocess.PIPE, text=True, check=True)
    return r.stdout.split()[0], g.stdout.split("\n")[0]


def make_tar(path, files, mode="w:xz"):
    with tarfile.open(path, mode) as t:
        for name, data in files.items():
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            t.addfile(ti, io.BytesIO(data))


def run(tar):
    p = subprocess.run([sys.executable, "-I", TOOL, tar], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


class ImageCheck(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.tar = os.path.join(self.tmp.name, "rootfs.tar.xz")
        self.base = {"./var/lib/dpkg/status": STATUS.encode(), "./etc/hostname": b"walnut\n"}

    def tearDown(self):
        self.tmp.cleanup()

    def check(self, extra=None, drop=(), want=0, text=None):
        files = dict(self.base)
        files.update(extra or {})
        for d in drop:
            files.pop(d)
        make_tar(self.tar, files)
        rc, out = run(self.tar)
        self.assertEqual(rc, want, out)
        if text:
            self.assertIn(text, out)

    def test_clean(self):
        self.check(want=0, text="image check clean")

    def test_dev_account(self):
        self.check({"./home/tibi/.bashrc": b"x"}, want=1, text="dev profile path")

    def test_dev_account_first_in_a_large_tar(self):
        # the old pipeline failed open when grep matched early and tar got SIGPIPE
        extra = {"./home/tibi/a": b"x"}
        extra.update({"./usr/share/f%d" % i: b"y" for i in range(2000)})
        self.check(extra, want=1, text="dev profile path")

    def test_dev_sudoers(self):
        self.check({"./etc/sudoers.d/tibi": b"x"}, want=1, text="dev profile path")

    def test_lan_repository(self):
        self.check({"./etc/apt/sources.list.d/ps3-lan.list": b"x"}, want=1, text="dev profile path")

    def test_other_home_is_fine(self):
        self.check({"./home/tibiu/x": b"x", "./etc/sudoers.d/tibi-not": b"x"}, want=0)

    def test_licensed_path(self):
        _, glob = licensed_name_and_glob()
        path = "./" + glob.replace("*", "x").replace("?", "x")
        self.check({path: b"x"}, want=1, text="licensed content path")

    def test_licensed_package(self):
        name, _ = licensed_name_and_glob()
        st = STATUS + "Package: %s\nStatus: install ok installed\nSection: local/games\nVersion: 1\n\n" % name
        self.check({"./var/lib/dpkg/status": st.encode()}, want=1, text="licensed package installed")

    def test_no_status_file(self):
        self.check(drop=["./var/lib/dpkg/status"], want=1, text="no var/lib/dpkg/status")

    def test_unreadable_tar(self):
        with open(self.tar, "wb") as f:
            f.write(b"not a tar at all" * 100)
        rc, out = run(self.tar)
        self.assertEqual(rc, 2, out)


if __name__ == "__main__":
    unittest.main()
