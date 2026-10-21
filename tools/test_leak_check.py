#!/usr/bin/env python3
"""Host tests of tools/leak-check.py (suite leak-check): synthetic .deb files, source packages and a
dpkg status file, no dpkg needed."""
import importlib.util
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "leak-check.py")
spec = importlib.util.spec_from_file_location("leakcheck", TOOL)
lc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lc)

SECRET = b"mojang terrain " * 100       # a licensed file's content (over MIN_SIZE)


def tar_bytes(files, comp="gz"):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:" + comp) as t:
        for name, data in files.items():
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            t.addfile(ti, io.BytesIO(data))
    return buf.getvalue()


def zip_bytes(files):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)
    return buf.getvalue()


def make_deb(path, package, section, files):
    control = "Package: %s\nVersion: 1\nArchitecture: all\nSection: %s\n" % (package, section)
    parts = [("debian-binary", b"2.0\n"),
             ("control.tar.gz", tar_bytes({"./control": control.encode()})),
             ("data.tar.xz", tar_bytes(files, "xz"))]
    with open(path, "wb") as f:
        f.write(b"!<arch>\n")
        for name, data in parts:
            f.write(("%-16s%-12s%-6s%-6s%-8s%-10s`\n" % (name, 0, 0, 0, 100644, len(data))).encode())
            f.write(data)
            if len(data) % 2:
                f.write(b"\n")


def make_dsc(d, source, pkgs, tar_files):
    tb = "%s_1.orig.tar.gz" % source
    with open(os.path.join(d, tb), "wb") as f:
        f.write(tar_bytes(tar_files))
    plist = "".join(" %s deb %s optional arch=all\n" % (n, s) for n, s in pkgs)
    dsc = os.path.join(d, "%s_1-1.dsc" % source)
    with open(dsc, "w") as f:
        f.write("Format: 3.0 (quilt)\nSource: %s\nBinary: %s\nVersion: 1-1\nPackage-List:\n%sFiles:\n"
                " 00000000000000000000000000000000 1 %s\n" % (source, ", ".join(n for n, _ in pkgs), plist, tb))
    return dsc


def run(*args):
    p = subprocess.run([sys.executable, TOOL] + list(args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


class LeakCheck(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name
        self.lic = os.path.join(self.d, "lic")
        os.mkdir(self.lic)
        # the licensed data package holds the secret inside a zip, as classicube-data does
        make_deb(os.path.join(self.lic, "game-data_1_all.deb"), "game-data", "local/games",
                 {"./usr/lib/game/texpack.zip": zip_bytes({"terrain.png": SECRET}),
                  "./usr/share/doc/game-data/copyright": b"x" * 400})
        make_deb(os.path.join(self.d, "engine_1_all.deb"), "engine", "games", {"./usr/games/engine": b"#!/bin/sh\n"})

    def tearDown(self):
        self.tmp.cleanup()

    def test_debs(self):
        rc, out = run("debs", os.path.join(self.d, "engine_1_all.deb"))
        self.assertEqual(rc, 0, out)
        rc, out = run("debs", os.path.join(self.lic, "game-data_1_all.deb"))
        self.assertEqual(rc, 1, out)
        self.assertIn("LEAK: licensed package game-data", out)

    def test_listed_name_without_section(self):
        # classicube-data is in tools/licensed-packages.txt: licensed whatever its Section says
        make_deb(os.path.join(self.d, "cd.deb"), "classicube-data", "games", {"./x": b"y"})
        self.assertEqual(run("debs", os.path.join(self.d, "cd.deb"))[0], 1)

    def test_installed(self):
        st = os.path.join(self.d, "status")
        with open(st, "w") as f:
            f.write("Package: engine\nStatus: install ok installed\nSection: games\nVersion: 1\n\n"
                    "Package: quake-shareware\nStatus: deinstall ok config-files\nSection: non-free/games\n\n")
        self.assertEqual(run("installed", st)[0], 0)
        with open(st, "a") as f:
            f.write("Package: tyrian-data\nStatus: install ok installed\nSection: games\nVersion: 2\n")
        rc, out = run("installed", st)
        self.assertEqual(rc, 1, out)
        self.assertIn("tyrian-data", out)

    def test_classify(self):
        free = make_dsc(self.d, "engine", [("engine", "games")], {"engine-1/main.c": b"int main;"})
        lic = make_dsc(self.d, "mixed", [("mixed", "games"), ("mixed-data", "non-free/games")], {"m/x": b"x"})
        named = make_dsc(self.d, "sauerbraten-nonfree-data", [("sauerbraten-nonfree-data", "games")], {"s/x": b"x"})
        rc, out = run("classify", free, lic, named)
        self.assertEqual(out.split("\n")[:3], ["free " + free, "licensed " + lic, "licensed " + named])

    def test_sources_clean(self):
        dsc = make_dsc(self.d, "engine", [("engine", "games")], {"engine-1/main.c": b"int main;" * 50})
        rc, out = run("sources", "--licensed", self.lic, dsc)
        self.assertEqual(rc, 0, out)

    def test_sources_copy_in_a_zip_under_another_name(self):
        # upstream's own copy of the licensed textures, zipped again under another name
        dsc = make_dsc(self.d, "engine", [("engine", "games")],
                       {"engine-1/misc/port/assets.zip": zip_bytes({"tex/terrain.png": SECRET})})
        rc, out = run("sources", "--licensed", self.lic, dsc)
        self.assertEqual(rc, 1, out)
        self.assertIn("assets.zip!tex/terrain.png is game-data_1_all.deb:usr/lib/game/texpack.zip!terrain.png", out)

    def test_sources_plain_copy_and_doc_ignored(self):
        dsc = make_dsc(self.d, "engine", [("engine", "games")],
                       {"engine-1/terrain.png": SECRET, "engine-1/COPYING": b"x" * 400})
        rc, out = run("sources", "--licensed", self.lic, dsc)
        self.assertEqual(rc, 1, out)
        self.assertEqual(out.count("LEAK"), 1, out)     # the licensed package's copyright file isn't content

    def test_sources_pattern(self):
        dsc = make_dsc(self.d, "engine", [("engine", "games")], {"ClassiCube-x/misc/cc_textures.zip": b"PK"})
        rc, out = run("sources", dsc)
        self.assertEqual(rc, 1, out)
        self.assertIn("matches misc/cc_textures.zip", out)

    def test_sources_licensed_source_refused(self):
        dsc = make_dsc(self.d, "game-data", [("game-data", "local/games")], {"g/x": b"x"})
        rc, out = run("sources", dsc)
        self.assertEqual(rc, 1, out)
        self.assertIn("licensed source package", out)

    def test_missing_file(self):
        rc, out = run("sources", os.path.join(self.d, "nope.dsc"))
        self.assertEqual(rc, 2, out)


if __name__ == "__main__":
    unittest.main()
