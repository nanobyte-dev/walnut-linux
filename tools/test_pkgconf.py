"""Tests of tools/pkgconf.py (suite pkgconf): its reading of package.conf agrees with sh's, for every package in the
tree; the forms it accepts and the ones it refuses; get, words and json on a real package."""
import importlib.util
import os
import subprocess
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("pkgconf", HERE / "pkgconf.py")
pc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pc)


def sh_values(conf, keys, env):
    """what sourcing CONF in sh gives for KEYS (unset: empty), with ENV as the environment"""
    script = '. "$1"; shift; for k; do eval "printf %s\\\\\\\\0 \\"\\${$k:-}\\""; done'
    r = subprocess.run(["sh", "-c", script, "sh", str(conf)] + list(keys), capture_output=True, text=True, env=env, check=True)
    return r.stdout.split("\0")[:-1]


class Reader(unittest.TestCase):
    def test_forms(self):
        got = pc.parse('# c\n\nA=1\nB="two words"  # note\nC=\'$A\'\nD="$A-${B}"\nE=\nF=x$UNSET_FOR_SURE\n', env={})
        self.assertEqual(got, {"A": "1", "B": "two words", "C": "$A", "D": "1-two words", "E": "", "F": "x"})

    def test_hash_and_lenient(self):
        self.assertEqual(pc.parse("A=a#b\nB=1 # c\nC='$1 ~'\n", env={}), {"A": "a#b", "B": "1", "C": "$1 ~"})
        got = pc.parse("TYPE=native\nVERSION=$(x)\nbad line\n", env={}, strict=False)
        self.assertEqual((dict(got), "VERSION" in got.bad), ({"TYPE": "native"}, True))

    def test_refused(self):
        for bad in ("echo hi", "A=1 B=2", 'A="open', "A=b c", "A=$(date)", "a-b=1", 'A="x" "y"', 'A="x"#c', "A=$1", "A=$$", "A=~/x", "A=x:~/y", 'A="$1"', "A=x$"):
            with self.assertRaises(pc.Error, msg=bad):
                pc.parse(bad + "\n", env={})

    def test_every_package_reads_like_sh(self):
        env = {"PATH": os.environ["PATH"], "HOME": "/nonexistent"}
        dirs = pc.all_dirs()
        self.assertGreater(len(dirs), 50)
        for d in dirs:
            conf = pc.ROOT / d / "package.conf"
            ours = pc.parse(conf.read_text(), str(conf), env=env)
            self.assertEqual(sh_values(conf, list(ours), env), list(ours.values()), d)

    def test_a_real_package(self):
        self.assertEqual(pc.read(pc.find("ps3-boot")).get("TYPE"), "native")
        r = subprocess.run(["python3", "-I", str(HERE / "pkgconf.py"), "words", "ps3-boot", "VERSION_PATHS"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(r.stdout.split())
        self.assertEqual(subprocess.run(["python3", "-I", str(HERE / "pkgconf.py"), "get", "no-such-pkg", "TYPE"],
                                        capture_output=True).returncode, 1)


if __name__ == "__main__":
    unittest.main()
