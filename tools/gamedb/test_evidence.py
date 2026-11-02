#!/usr/bin/env python3
"""Tests of a played session's evidence (tools/gamedb/evidence.py, import-run.py, reimport.py): the scrub
and the public-safe scan, the evidence a run leaves in its session folder, and that the record derived
from the evidence alone is the one import-run wrote. Shot paths: only a plain picture under the run's shots/ is
opened (bench.txt may be a tester's), ImageMagick reads it with the coder its suffix names (a vector file refused),
the PC reference's directory is never a path out, a rebuilt run's pictures land only in its shots/.
python3 -I tools/gamedb/test_evidence.py (tools/run-tests.sh gamedb-evidence)."""
import contextlib
import importlib.util
import io
import lzma
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent.parent / "bench" / d) for d in ("analysis", "harness")] + [str(HERE)]
import check     # noqa: E402
import evidence  # noqa: E402

IDENT = (("alice",), ("box-one", "10.1.2.3"))


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


class Scrub(unittest.TestCase):
    def test_scrub(self):
        text = ('dir /home/alice/.openarena, GL_ARB_ES2_compatibility; player "alice"; IP: 10.0.0.5 '
                'and 192.168.1.20, mac 00:1f:a3:BB:cc:9d on box-one; version 1.10.0.0.1; 172.20.0.1; 8.8.8.8')
        got = evidence.scrub(text, IDENT)
        self.assertEqual(got, 'dir ~/.openarena, GL_ARB_ES2_compatibility; player "player"; IP: LAN-IP '
                              'and LAN-IP, mac xx:xx:xx:xx:xx:xx on host; version 1.10.0.0.1; LAN-IP; 8.8.8.8')
        self.assertEqual(evidence.leftovers(got, IDENT), [])
        found = {w for w, _ in evidence.leftovers(text + " alice@box", IDENT)}
        self.assertEqual(found, {"a home directory", "a MAC address", "a LAN address", "a private user name",
                                 "a private host name", "user@"})
        self.assertEqual(evidence.scrub("compatibility", (("pat",), ())), "compatibility")   # whole words only

    def test_pack_is_deterministic(self):
        data = b"frame\n" * 1000
        self.assertEqual(evidence.pack(data), evidence.pack(data))
        self.assertEqual(lzma.decompress(evidence.pack(data)), data)

    def test_shot_names(self):
        self.assertEqual(evidence.shot_name(60), "ps3-60s.jpg")
        self.assertEqual(evidence.shot_name(31.4), "ps3-mark-31_4s.jpg")


def picture(path, colour):
    subprocess.run(["magick", "-size", "192x108", "xc:" + colour, "-fill", "white", "-draw", "rectangle 20,20 90,60",
                    str(path)], check=True)


@unittest.skipUnless(shutil.which("magick"), "needs ImageMagick (magick)")
class Evidence(unittest.TestCase):
    def setUp(self):
        self.ir = load("import_run", "import-run.py")
        self.re = load("reimport", "reimport.py")
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))
        tmp = self.enterContext(tempfile.TemporaryDirectory())
        self.content = Path(tmp) / "games"
        g = self.content / "ex"
        g.mkdir(parents=True)
        (g / "index.md").write_text('+++\nid = "ex"\ntitle = "Example"\n+++\n')
        self.ref = Path(tmp) / "ref"
        d = self.run = Path(tmp) / "ps3-bench-20261001T120000Z-session-example-base"
        (d / "hud").mkdir(parents=True)
        (d / "shots").mkdir()
        (d / "hud" / "frametime_(ms)").write_text("\n".join(["20"] * 4500) + "\n")
        (d / "hud" / "fps").write_text("50\n" * 90)
        bench = {"game": "example", "variant": "base", "seconds": "110", "load_end": "10", "hud_t1": "10",
                 "window": "20 110", "underruns": "0", "oal_xrun": "0", "oal_wait": "0", "snd_stall": "0",
                 "interfered": "0", "ended": "quit", "rc": "0", "kind": "session", "launcher": "Example",
                 "shots": "30 60", "shot": "30s:shots/shot-30s.xwd:100", "shot_t": "30s:30.0:1:2"}
        lines = ["%s=%s" % kv for kv in bench.items()] + ["shot=60s:shots/shot-60s.xwd:100", "shot_t=60s:60.0:1:2",
                                                          "shot_mark=45.5s:shots/mark-45.5s.xwd:120"]
        (d / "bench.txt").write_text("\n".join(lines) + "\n")
        picture(d / "shots" / "shot-30s.png", "navy")
        picture(d / "shots" / "shot-60s.png", "darkgreen")
        picture(d / "shots" / "mark-45.5s.png", "maroon")
        (d / "shots" / "geometry.tsv").write_text("at_s\tx\ty\tw\th\tfullscreen\tscreen_w\tscreen_h\n"
                                                  "30\t0\t0\t1920\t1080\t1\t1920\t1080\n")
        (d / "versions").write_text("example\t1.0\n6.12.0+ps3.1\n")
        (d / "game.log").write_text("loading /home/alice/.example/config from 10.0.0.5\n")
        (d / "dmesg.log").write_text("[1.0] eth0: 00:1f:a3:bb:cc:9d up\n")
        (d / "session.toml").write_text('form_version = 2\nglitches = "none"\nnotes = "fine"\n')
        (d / "REVIEW.md").write_text(
            "verdict: ok: as on the PC\nstretched: no\ntext_px: 20: menus\npointer: hidden\n"
            + "".join("%s: no\n" % c for c in self.ir.score.DEFECTS) + "broken: none\n")
        (d / "fresh-home" / ".example").mkdir(parents=True)
        (d / "fresh-home" / ".example" / "config.cfg").write_text('name "alice"\n')
        (d / "fresh-home" / ".example" / "big.dat").write_bytes(b"\0" * 100)
        (d / "pgid").write_text("1\n")

    def session(self):
        return self.content / "ex" / check.SESSIONS / "2026-10-01-session"

    def test_import_keeps_the_evidence(self):
        self.ir.import_run(self.run, "ex", self.content)
        t = self.session()
        self.assertEqual(sorted(p.name for p in (t / "shots").iterdir()),
                         ["ps3-30s.jpg", "ps3-60s.jpg", "ps3-mark-45_5s.jpg"])        # every shot, as JPEG
        r = tomllib.loads((t / "result.toml").read_text())
        self.assertEqual(r["shots"]["ps3"][0], "shots/ps3-mark-45_5s.jpg")             # the player's first
        self.assertTrue(all((t / f).is_file() for f in r["shots"]["ps3"]))
        self.assertEqual((t / "form.toml").read_text(), (self.run / "session.toml").read_text())   # as given
        self.assertEqual((t / "review.md").read_text(), (self.run / "REVIEW.md").read_text())
        log = lzma.decompress((t / "raw" / "game.log.xz").read_bytes()).decode()
        self.assertNotIn("/home/", log)
        self.assertNotIn("10.0.0.5", log)
        self.assertFalse((t / "raw" / "pgid.xz").exists())                             # not evidence
        imp = evidence.read_import(t)
        self.assertEqual(imp["run"], self.run.name)
        self.assertTrue(any("big.dat" in s for s in imp["skipped"]))                    # binary: left out
        self.assertTrue((t / "raw" / "fresh-home" / ".example" / "config.cfg.xz").is_file())
        self.assertEqual(evidence.folder_errors(t, set(r["shots"]["ps3"])), [])
        # the record comes from the evidence alone: the run directory gone, the same record
        shutil.rmtree(self.run)
        self.assertEqual(self.ir.derive_session(t, "ex", self.content)[0], (t / "result.toml").read_text())

    def test_reimport(self):
        self.ir.import_run(self.run, "ex", self.content, keep=[60])
        t = self.session()
        self.assertEqual(evidence.read_import(t)["shots"], [60])
        good = (t / "result.toml").read_text()
        self.assertIn('ps3 = ["shots/ps3-mark-45_5s.jpg", "shots/ps3-60s.jpg"]', good)
        shutil.rmtree(self.run)
        (t / "result.toml").write_text(good.replace("median_fps = 50.0", "median_fps = 1.0"))
        argv = sys.argv
        try:
            sys.argv = ["reimport.py", "--content", str(self.content), "--check"]
            self.assertEqual(self.re.main(), 1)                                         # differs: --check says so
            sys.argv = ["reimport.py", "--content", str(self.content)]
            self.assertEqual(self.re.main(), 0)
            self.assertEqual((t / "result.toml").read_text(), good)
            sys.argv = ["reimport.py", "--content", str(self.content), "--check"]
            self.assertEqual(self.re.main(), 0)
        finally:
            sys.argv = argv

    def test_keep_record(self):
        t = self.session()
        t.mkdir(parents=True)
        old = ('schema = 1\nkind = "session"\ngame = "ex"\nrun = "%s"\n\n[numbers]\nmedian_fps = 7.0\n\n'
               '[shots]\nps3 = ["ps3-60s.jpg"]\n' % self.run.name)
        (t / "result.toml").write_text(old)
        (t / "ps3-60s.jpg").write_bytes(b"old")
        self.ir.import_run(self.run, "ex", self.content, session=t.name, keep_record=True)
        self.assertEqual((t / "result.toml").read_text(), old.replace('["ps3-60s.jpg"]', '["shots/ps3-60s.jpg"]'))
        self.assertFalse((t / "ps3-60s.jpg").exists())
        self.assertTrue(evidence.read_import(t)["frozen"])
        argv = sys.argv
        try:
            sys.argv = ["reimport.py", "--content", str(self.content), "--check"]
            self.assertEqual(self.re.main(), 0)                                         # frozen: left as it is
        finally:
            sys.argv = argv

    def test_scan_refuses_leftovers(self):
        self.ir.import_run(self.run, "ex", self.content)
        t = self.session()
        listed = set(tomllib.loads((t / "result.toml").read_text())["shots"]["ps3"])
        (t / "raw" / "extra.log.xz").write_bytes(evidence.pack(b"ssh bob@10.0.0.5\n"))
        (t / "raw" / "id1").mkdir()
        (t / "raw" / "id1" / "pak0.pak.xz").write_bytes(evidence.pack(b"PACK"))
        (t / "review.md").write_text("seen at /home/bob/x\n")
        (t / "shots" / "stray.png").write_bytes(b"x")
        (t / "notes.txt").write_text("x")
        errs = " | ".join(evidence.folder_errors(t, listed))
        for want in ("raw/extra.log: a LAN address", "raw/id1/pak0.pak: a licensed file", "review.md: a home directory",
                     "unexpected file shots/stray.png", "unexpected file notes.txt"):
            self.assertIn(want, errs)
        (t / "raw" / "extra.log.xz").write_bytes(evidence.LFS_MAGIC + b"\noid sha256:00\nsize 1\n")
        self.assertNotIn("raw/extra.log:", " ".join(evidence.folder_errors(t, listed)))   # not fetched: not read


MVG = b"push graphic-context\nviewbox 0 0 64 36\nfill white\nrectangle 0,0 64,36\npop graphic-context\n"
SVG = b'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="36"><rect width="64" height="36" fill="white"/></svg>'


@unittest.skipUnless(shutil.which("magick"), "needs ImageMagick (magick)")
class ShotPaths(unittest.TestCase):
    """bench.txt names each shot's picture; a submitted run's bench.txt is the tester's: only a picture under the
    run's shots/ is opened, ImageMagick reads it with the coder its suffix names, and the PC reference's directory
    and a rebuilt run's pictures are never a path out"""

    def setUp(self):
        import shots
        self.shots = shots
        self.ir = load("import_run", "import-run.py")
        self.t = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.d = self.t / "a" / "b" / "run"
        (self.d / "shots").mkdir(parents=True)
        (self.d / "hud").mkdir()

    def test_validate_shot_paths(self):
        d = self.d
        picture(d / "shots" / "shot-30s.png", "navy")
        (d / "hud" / "x.msl").write_bytes(b"<image/>")
        picture(self.t / "a" / "x.jpg", "navy")
        picture(self.t / "x.jpg", "navy")
        self.assertEqual(self.shots.image(d, {"file": "shots/shot-30s.xwd"}), d / "shots" / "shot-30s.png")
        for f in ("hud/x.msl", "../x.jpg", "../../x.jpg", str(self.t / "x.jpg"), "shots/../../x.jpg", "shots/x.msl"):
            self.assertIsNone(self.shots.image(d, {"file": f}), f)
        (d / "shots" / "link.png").symlink_to(self.t / "x.jpg")
        self.assertIsNone(self.shots.image(d, {"file": "shots/link.png"}))
        # the PC reference: game and variant are names
        ref = self.t / "ref"
        (ref / "ex-base").mkdir(parents=True)
        (ref / "ex-base" / "bench.txt").write_text("game=ex\n")
        (self.t / "a" / "bench.txt").write_text("game=other\n")
        self.assertEqual(self.shots.ref_dir({"game": "ex", "variant": "base"}, ref), ref / "ex-base")
        for v in ("base/../../a", "base/..", "../a"):
            self.assertIsNone(self.shots.ref_dir({"game": "ex", "variant": v}, ref), v)
        self.assertIsNone(self.shots.ref_dir({"game": "../a", "variant": "base"}, ref))

    def test_picture_coder_from_suffix(self):
        d = self.d
        (d / "shots" / "shot-30s.png").write_bytes(MVG)        # drawing commands under a picture's name
        self.assertIsNone(self.ir.gray(d / "shots" / "shot-30s.png"))
        self.assertEqual(self.shots.flag(d / "shots" / "shot-30s.png"), "UNREADABLE")
        self.assertIsNotNone(evidence.jpeg(d / "shots" / "shot-30s.png", self.t / "out.jpg"))
        self.assertFalse((self.t / "out.jpg").exists())
        (self.t / "x.svg").write_bytes(SVG)                       # a vector format, by its suffix
        self.assertIsNone(self.ir.gray(self.t / "x.svg"))
        self.assertIn("not a screenshot", evidence.jpeg(self.t / "x.svg", self.t / "out.jpg"))
        self.assertFalse((self.t / "out.jpg").exists())
        with self.assertRaises(ValueError):
            self.shots.coded("hud/x.msl")
        picture(d / "shots" / "shot-60s.png", "navy")           # a real picture still reads
        self.assertIsNotNone(self.ir.gray(d / "shots" / "shot-60s.png"))
        self.assertIsNone(evidence.jpeg(d / "shots" / "shot-60s.png", self.t / "ok.jpg"))
        self.assertEqual(self.shots.flag(d / "shots" / "shot-60s.png"), "")

    def test_rebuild_writes_only_into_the_run(self):
        sdir = self.t / "session"
        (sdir / "raw").mkdir(parents=True)
        (sdir / "shots").mkdir()
        (sdir / "raw" / evidence.IMPORT).write_text('run = "ps3-bench-20261001T120000Z-session-ex-base"\n')
        bench = "game=ex\nvariant=base\nshot=30s:../../../escaped.xwd:1\nshot=60s:shots/shot-60s.xwd:1\n"
        (sdir / "raw" / "bench.txt.xz").write_bytes(evidence.pack(bench.encode()))
        for p in (30, 60):
            picture(sdir / "shots" / evidence.shot_name(p), "navy")
        dest = self.t / "dest" / "x"
        dest.mkdir(parents=True)
        rd = evidence.rebuild(sdir, dest)
        self.assertTrue((rd / "shots" / "shot-60s.jpg").is_file())
        self.assertEqual([f for f in self.t.rglob("escaped*")], [])


if __name__ == "__main__":
    unittest.main()
