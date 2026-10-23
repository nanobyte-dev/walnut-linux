#!/usr/bin/env python3
"""Tests for the Walnut Games tools: python3 -m unittest (from tools/gamedb) or
python3 -I tools/gamedb/test_gamedb.py. The calibration test reads the table in docs/GAMEDB.md, so a
change to the scoring that moves a medal, or a doc edit that no longer matches the code, fails here."""
import contextlib
import importlib.util
import io
import json
import re
import shutil
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import catalog   # noqa: E402
import check     # noqa: E402
import score     # noqa: E402


def load_import_run():
    spec = importlib.util.spec_from_file_location("import_run", HERE / "import-run.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Curves(unittest.TestCase):
    def test_anchor_points(self):
        self.assertEqual(score.smooth_score(0.1, 250), 100)
        self.assertAlmostEqual(score.smooth_score(0.5, 0), 75)
        self.assertAlmostEqual(score.smooth_score(2, 0), 55)
        self.assertEqual(score.smooth_score(5, 0), 0)
        self.assertAlmostEqual(score.smooth_score(0, 1000), 75)
        self.assertAlmostEqual(score.smooth_score(0, 3000), 55)
        self.assertEqual(score.smooth_score(0, 5000), 0)
        for faults, want in ((0, 100), (1, 75), (6, 55), (20, 35), (60, 0), (500, 0)):
            self.assertAlmostEqual(score.sound_score(faults), want)
        for secs, want in ((10, 100), (15, 100), (30, 75), (60, 55), (120, 0), (300, 0)):
            self.assertAlmostEqual(score.load_score(secs), want)
        self.assertEqual(score.frame_score(20, 10), 0)
        self.assertEqual(score.frame_score(55, 45), 100)
        self.assertAlmostEqual(score.frame_score(55, 10), 50)

    def test_silent_or_wedged_sound_is_zero(self):
        self.assertEqual(score.sound_score(0, wedged=True), 0)
        self.assertEqual(score.sound_score(0, heard=False), 0)
        self.assertEqual(score.sound_score(0, heard=True), 100)
        self.assertIsNone(score.sound_score(None))

    def test_fps_cap(self):
        self.assertLess(score.frame_score(35, 30), 100)           # Doom at 35 Hz, no cap known
        self.assertEqual(score.frame_score(35, 30, fps_cap=35), 100)
        self.assertLess(score.frame_score(30, 30, fps_cap=35), 100)   # below 0.95 x cap
        self.assertLess(score.frame_score(35, 20, fps_cap=35), 100)   # 1% low below 0.8 x cap

    def test_stability(self):
        self.assertEqual(score.stability_score(0, 3), (100, False))
        s, prov = score.stability_score(0, 0.5)
        self.assertTrue(prov)
        self.assertAlmostEqual(s, 90 + 10 * 0.5 / 3)    # crash-free starts at Platinum level
        self.assertEqual(score.stability_score(0, 1 / 6)[0] >= 90, True)
        self.assertAlmostEqual(score.stability_score(1, 3)[0], 75)     # one crash in 3 h
        self.assertAlmostEqual(score.stability_score(1, 1)[0], 55)
        self.assertAlmostEqual(score.stability_score(4, 1)[0], 35)
        self.assertAlmostEqual(score.stability_score(1, 0.1)[0], 55)   # a short run counts as an hour
        self.assertEqual(score.stability_score(0, 0), (None, True))

    def test_unmeasured_aspects_drop_out(self):
        self.assertEqual(score.weighted_mean({"frame": 80, "smooth": None}), 80)
        self.assertAlmostEqual(score.weighted_mean({"frame": 100, "smooth": 0}), 100 * 25 / 45)
        self.assertIsNone(score.weighted_mean({"frame": None}))
        self.assertEqual(score.rate({})["medal"], "Untested")


class Medals(unittest.TestCase):
    def test_thresholds(self):
        for mean, medal in ((100, "Platinum"), (90, "Platinum"), (89.9, "Gold"), (75, "Gold"), (74.9, "Silver"),
                            (55, "Silver"), (35, "Bronze"), (34.9, "Tin"), (25, "Tin"), (24.9, "Rust"),
                            (15, "Rust"), (14.9, "Scrap"), (0, "Scrap")):
            self.assertEqual(score.medal_of(mean), medal)
            if mean >= 90:      # one aspect alone: its own level caps the medal one above
                self.assertEqual(score.rate({"frame": mean})["medal"], medal)

    def test_weakest_link_cap(self):
        # perfect but silent: Bronze
        r = score.rate({"frame": 100, "smooth": 100, "sound": 0, "picture": 100, "stability": 100})
        self.assertEqual((r["medal"], r["capped"], r["weakest"]), ("Bronze", True, "sound"))
        # one near miss costs nothing: weakest 72 is a Silver level, one above it is Gold
        r = score.rate({"frame": 72, "smooth": 100, "sound": 100})
        self.assertEqual(r["medal"], "Gold")
        # an aspect at Bronze level allows Silver, an aspect near 35 too
        r = score.rate({"frame": 40, "smooth": 100, "sound": 100, "picture": 100, "stability": 100, "display": 100})
        self.assertEqual(r["medal"], "Silver")
        # a game rated below Bronze by its mean keeps the lower medal
        r = score.rate({"frame": 30, "smooth": 30, "sound": 30})
        self.assertEqual(r["medal"], "Tin")

    def test_scrap_floors(self):
        self.assertEqual(score.scrap_floor(started=False), "won't start")
        self.assertEqual(score.scrap_floor(reached_play=False), "doesn't reach play")
        self.assertEqual(score.scrap_floor(crashed_sessions=2, sessions=3), "crashes")
        self.assertIsNone(score.scrap_floor(crashed_sessions=1, sessions=2))
        self.assertEqual(score.scrap_floor(median_fps=14.9), "too slow")
        self.assertEqual(score.scrap_floor(median_fps=30, stutter=10.5), "too slow")
        self.assertIsNone(score.scrap_floor(median_fps=15, stutter=10))
        r = score.rate({"frame": 100, "smooth": 100}, "crashes")
        self.assertEqual((r["medal"], r["reason"]), ("Scrap", "crashes"))


def doc_table():
    """the rows of docs/GAMEDB.md's calibration table: (frame, smooth, sound, mean, weakest, medal) ints/words"""
    text = (REPO / "docs" / "GAMEDB.md").read_text()
    body = text.split("## Calibration", 1)[1].split("\n\n", 2)[2].split("\n\n")[0]
    rows = []
    for line in body.splitlines()[2:]:
        c = [x.strip() for x in line.strip("|").split("|")]
        first = lambda s: int(re.search(r"\d+", s)[0])
        rows.append((c[0], first(c[1]), first(c[2]), first(re.sub(r"~", "", c[3])), first(c[4]), first(c[5]),
                     c[6].split()[0]))
    return rows


class Calibration(unittest.TestCase):
    """the 13 benchmarked games (15 rows) come out as docs/GAMEDB.md says, to a point of rounding"""

    def test_table_matches_doc(self):
        rows = doc_table()
        self.assertEqual(len(rows), len(score.CALIBRATION))
        for doc, row in zip(rows, score.CALIBRATION):
            scores, rating = score.calibrate(row)
            got = (scores["frame"], scores["smooth"], scores["sound"], rating["score"], scores[rating["weakest"]])
            for want, have, what in zip(doc[1:6], got, ("frame", "smooth", "sound", "mean", "weakest")):
                self.assertLessEqual(abs(want - have), 1, "%s %s: doc %s, computed %.1f" % (doc[0], what, want, have))
            self.assertEqual(doc[6], rating["medal"], doc[0])

    def test_scrap_reason(self):
        _, r = score.calibrate(score.CALIBRATION[-1])
        self.assertEqual((r["medal"], r["reason"]), ("Scrap", "too slow"))


class Combine(unittest.TestCase):
    def test(self):
        good = {"kind": "demo", "play_seconds": 600, "crashed": False,
                "numbers": {"median_fps": 60, "low1_fps": 50, "stutter_s_per_min": 0, "longest_ms": 40,
                            "sound_faults_per_min": 0, "shot_points": 2, "shot_differing": 0}}
        r = score.combine([good])
        self.assertEqual(r["graded_by"], "demo")
        self.assertTrue(r["provisional"])                 # under an hour
        self.assertAlmostEqual(r["aspects"]["stability"], 90 + 10 * (1 / 6) / 3, places=1)
        self.assertEqual(r["medal"], "Platinum")          # a short crash-free test never caps it
        self.assertEqual(score.combine([])["medal"], "Untested")
        cap = {"kind": "capture", "numbers": {"shot_points": 1, "shot_differing": 1}}
        self.assertEqual(score.combine([cap])["medal"], "Untested")      # a picture alone is no medal
        crashing = dict(good, crashed=True)
        r = score.combine([crashing, crashing, good])
        self.assertEqual((r["medal"], r["reason"]), ("Scrap", "crashes"))
        session = dict(good, kind="session", numbers=dict(good["numbers"], median_fps=30, low1_fps=25))
        self.assertEqual(score.combine([good, session])["graded_by"], "session")   # sessions replace demos


def write_game(content, gid="ex", **over):
    fm = {"id": gid, "title": "Example", "summary": "An example.", "categories": '["puzzle"]', "tags": '["a"]',
          "license": '"GPL-2.0-or-later"', "homepage": '"https://example.org/"', "packages": '["example"]',
          "desktop": '["example.desktop"]', "needs_data": "false", "controller": '"keyboard"'}
    fm = {k: (v if k in ("categories", "tags", "license", "homepage", "packages", "desktop", "needs_data",
                         "controller") else '"%s"' % v) for k, v in fm.items()}
    fm.update(over)
    g = Path(content) / gid
    g.mkdir(parents=True, exist_ok=True)
    (g / "index.md").write_text("+++\n" + "\n".join("%s = %s" % kv for kv in fm.items()) + "\n+++\nbody\n")
    (g / "thumb.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"0" * 20)
    return g


class Check(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.content = Path(self.tmp.name) / "games"
        self.repo = Path(self.tmp.name) / "repo"
        (self.repo / "tools").mkdir(parents=True)
        (self.repo / "tools" / "licensed-packages.txt").write_text("# comment\nquake-shareware\nrct2-data  # x\n")

    def tearDown(self):
        self.tmp.cleanup()

    def problems(self):
        return check.check(self.content, self.repo)

    def test_good_game(self):
        write_game(self.content)
        self.assertEqual(self.problems(), {})

    def test_bad_id(self):
        write_game(self.content, "Bad_Id", id='"Bad_Id"')
        self.assertIn("id", " ".join(self.problems()["Bad_Id"]))
        write_game(self.content, "x", id='"y"')       # id and folder differ
        self.assertIn("x", self.problems())

    def test_licensed_package_refused(self):
        write_game(self.content, packages='["example", "quake-shareware"]')
        self.assertIn("licensed", " ".join(self.problems()["ex"]))

    def test_package_name_must_end_alnum(self):
        write_game(self.content, packages='["example-"]')
        self.assertIn("must end in", " ".join(self.problems()["ex"]))

    def test_category_list_is_fixed(self):
        write_game(self.content, categories='["fun"]')
        self.assertIn("categories", " ".join(self.problems()["ex"]))

    def test_needs_data_needs_guide(self):
        write_game(self.content, needs_data="true")
        self.assertIn("data_guide", " ".join(self.problems()["ex"]))
        write_game(self.content, needs_data="true", data_guide='"https://example.org/guide"')
        self.assertEqual(self.problems(), {})

    def test_unknown_field_and_missing_thumb(self):
        g = write_game(self.content, colour='"red"')
        (g / "thumb.png").unlink()
        text = " ".join(self.problems()["ex"])
        self.assertIn("unknown field", text)
        self.assertIn("thumb.png", text)

    def test_test_folder_files(self):
        g = write_game(self.content)
        t = g / "tests" / "2026-01-01-demo"
        t.mkdir(parents=True)
        (t / "result.toml").write_text('schema = 1\nkind = "demo"\ngame = "ex"\n[shots]\nps3 = ["ps3-1s.jpg"]\n')
        self.assertIn("missing", " ".join(self.problems()["ex"]))
        (t / "ps3-1s.jpg").write_bytes(b"x")
        (t / "stray.dat").write_bytes(b"x")
        self.assertIn("unexpected file stray.dat", " ".join(self.problems()["ex"]))

    def test_repository_catalog_is_valid(self):
        self.assertEqual(check.check(REPO / "gamedb" / "content" / "games"), {})


class Importer(unittest.TestCase):
    def setUp(self):
        self.ir = load_import_run()
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))   # import_run prints one line
        self.tmp = tempfile.TemporaryDirectory()
        self.content = Path(self.tmp.name) / "games"
        write_game(self.content)
        self.run_dir = Path(self.tmp.name) / "ps3-bench-20261001T120000Z-example-base"
        (self.run_dir / "hud").mkdir(parents=True)
        # 10 s of load, then 100 s of 20 ms frames with five 400 ms stalls; 3 sound faults
        ft = ["20"] * 4500 + ["400"] * 5
        (self.run_dir / "hud" / "frametime_(ms)").write_text("\n".join(ft) + "\n")
        self.write_bench(ended="window", rc="137")
        (self.run_dir / "versions").write_text("openal\t1.0\nmesa-libgallium:ppc64\t26.2\n6.12.0+ps3.1\n")

    def tearDown(self):
        self.tmp.cleanup()

    def write_bench(self, **over):
        b = {"game": "example", "variant": "base", "seconds": "110", "load_end": "10", "hud_t1": "10",
             "window": "20 110", "warmup": "10", "measure": "90", "underruns": "2", "oal_xrun": "1", "oal_wait": "0",
             "snd_stall": "0", "interfered": "0", "interference": "none", "ended": "window", "rc": "137"}
        b.update(over)
        (self.run_dir / "bench.txt").write_text("".join("%s=%s\n" % kv for kv in b.items()))

    def do(self):
        self.ir.import_run(self.run_dir, "ex", self.content, None)
        out = list((self.content / "ex" / "tests").iterdir())
        self.assertEqual(len(out), 1)
        return tomllib.loads((out[0] / "result.toml").read_text()), out[0]

    def test_numbers(self):
        r, _ = self.do()
        n = r["numbers"]
        self.assertEqual(r["kind"], "demo")
        self.assertEqual(r["date"], "2026-10-01")
        self.assertAlmostEqual(n["median_fps"], 50.0)
        self.assertEqual(n["longest_ms"], 400.0)
        # five frames of 400 ms lose 0.38 s each: 1.9 s in 92 s of frames (4500 x 20 ms + 5 x 400 ms)
        self.assertAlmostEqual(n["stutter_s_per_min"], 1.9 / (92 / 60), places=1)
        self.assertEqual(n["sound_faults"], 3)
        self.assertAlmostEqual(n["sound_faults_per_min"], 3 / (100 / 60), places=2)
        self.assertEqual(r["play_seconds"], 100.0)
        self.assertFalse(r["crashed"])
        self.assertEqual(r["versions"]["kernel"], "6.12.0+ps3.1")
        self.assertNotIn(self.tmp.name, json.dumps(r))      # no absolute paths

    def test_crash_states(self):
        self.write_bench(ended="exit", rc="139")
        self.assertTrue(self.do()[0]["crashed"])
        self.write_bench(ended="exit", rc="0")
        (self.run_dir / "dmesg.log").write_text("[ 1.0] example[12]: segfault at 0 ip 0\n")
        self.assertEqual(self.do()[0]["end"], "crash")
        (self.run_dir / "dmesg.log").write_text("")
        self.assertFalse(self.do()[0]["crashed"])

    def test_refusals(self):
        self.write_bench(interfered="1", interference="ssh during the run")
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content, None)
        self.write_bench(temp_stop="1")
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content, None)
        self.write_bench()
        with self.assertRaises(self.ir.Refused):      # a game that is not in the catalog
            self.ir.import_run(self.run_dir, "nope", self.content, None)

    def test_capture_run_needs_a_review(self):
        self.write_bench(window="", shots="30", shot="30s:shots/shot-30s.png:100", shot_t="30s:30.0:1:2")
        (self.run_dir / "shots").mkdir()
        (self.run_dir / "shots" / "shot-30s.png").write_bytes(b"png")
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content, None)

    def test_launcher(self):
        self.assertNotIn("launcher", self.do()[0])           # none recorded, none given: no key
        self.write_bench(launcher="OpenArena (GL 2.0)", launcher_file="openarena-gl2.desktop")
        self.assertEqual(self.do()[0]["launcher"], "OpenArena (GL 2.0)")

    def test_launcher_option_for_old_runs(self):
        self.ir.import_run(self.run_dir, "ex", self.content, None, False, "OpenArena (GL 1.2)")
        out = next((self.content / "ex" / "tests").iterdir())
        self.assertEqual(tomllib.loads((out / "result.toml").read_text())["launcher"], "OpenArena (GL 1.2)")

    def test_launcher_option_must_match_the_recorded_one(self):
        self.write_bench(launcher="OpenArena (GL 2.0)")
        self.ir.import_run(self.run_dir, "ex", self.content, None, False, "OpenArena (GL 2.0)")   # same: fine
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content, None, False, "OpenArena (GL 1.2)")

    def test_toml_writer_roundtrip(self):
        text = self.ir.toml_dump({"a": 1, "b": "x\"y", "c": True, "n": None},
                                 {"t": {"f": 1.5, "l": ["p", "q"], "bad key": 2, "z": None}})
        self.assertEqual(tomllib.loads(text), {"a": 1, "b": "x\"y", "c": True, "t": {"f": 1.5, "l": ["p", "q"], "bad key": 2}})
        self.assertTrue(text.startswith("# generated"))


class Catalog(unittest.TestCase):
    def test_patch_subjects(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "pkg" / "patches"
            p.mkdir(parents=True)
            (p / "series").write_text("0001-a.patch\n0002-b.patch\n")
            (p / "0001-a.patch").write_text("From: x\nSubject: [PATCH 1/2] first thing\n and its tail\nDate: y\n\n---\n")
            (p / "0002-b.patch").write_text("Subject: second\n")
            self.assertEqual(catalog.patch_subjects("pkg", Path(t)), ["first thing and its tail", "second"])
            self.assertEqual(catalog.patch_subjects("none", Path(t)), [])

    def test_versions(self):
        def t(i, date, walnut, ver, kind="session", fps=60):
            v = {"xmoto": ver, "mesa-libgallium": "26"}
            if walnut:
                v["walnut"] = walnut
            return {"id": i, "date": date, "kind": kind, "versions": v, "play_seconds": 600,
                    "numbers": {"median_fps": fps, "low1_fps": fps - 10}}
        tests = [t("a", "2026-10-20", None, "0.6.3-1", kind="demo", fps=30),
                 t("b", "2026-10-24", "0.1", "0.6.3-1+ps3.7"), t("c", "2026-10-25", "0.1", "0.6.3-1+ps3.9"),   # a rebuild: the same version
                 t("d", "2026-10-26", "0.1", "0.6.3-1+ps3.7", kind="capture")]
        self.assertEqual(catalog.tested_version(tests[0], ["xmoto"]), ("pre-0.1", "xmoto", "0.6.3-1"))
        self.assertEqual(catalog.tested_version(tests[1], ["other"]), ("0.1", None, None))
        groups = catalog.version_groups(tests, ["xmoto"])
        self.assertEqual([k for k, _ in groups], [("0.1", "xmoto", "0.6.3-1"), ("pre-0.1", "xmoto", "0.6.3-1")])
        h = catalog.history(groups, None)
        self.assertEqual([(r["walnut"], r["tests"], r["first"], r["last"]) for r in h],
                         [("0.1", 3, "2026-10-24", "2026-10-26"), ("pre-0.1", 1, "2026-10-20", "2026-10-20")])
        self.assertGreater(h[0]["aspects"]["frame"], h[1]["aspects"]["frame"])

    def test_rating_is_the_newest_graded_version(self):
        # a capture run on a newer version doesn't move the rating off the version that was played
        with tempfile.TemporaryDirectory() as d:
            src = REPO / "gamedb" / "content" / "games" / "openarena"
            g = Path(d) / "openarena"
            shutil.copytree(src, g)
            old = next((g / "tests").iterdir())
            new = g / "tests" / "2026-11-01-capture"
            shutil.copytree(old, new)
            txt = (new / "result.toml").read_text().replace('kind = "demo"', 'kind = "capture"')
            txt = txt.replace('date = "2026-10-20"', 'date = "2026-11-01"').replace("[versions]", '[versions]\nwalnut = "0.2"')
            (new / "result.toml").write_text(txt)
            out = catalog.build_game(g, REPO, {})
            self.assertEqual(out["rating"]["tested"]["walnut"], "pre-0.1")
            hist = out["launchers"][0]["history"]
            self.assertEqual([r["walnut"] for r in hist], ["0.2", "pre-0.1"])
            self.assertEqual(hist[0]["graded_by"], "capture")   # the newer version: picture only
            self.assertEqual(out["tests"][0]["walnut"], "0.2")      # runs newest first

    def test_launcher_groups(self):
        def t(i, kind="session", launcher=None):
            d = {"id": i, "kind": kind, "date": "2026-10-24"}
            if launcher:
                d["launcher"] = launcher
            return d
        # one named launcher: unnamed tests are its
        g = catalog.launcher_groups([t("a", "demo"), t("b", launcher="X")])
        self.assertEqual([(n, [x["id"] for x in ts]) for n, ts in g], [("X", ["a", "b"])])
        # two: unnamed graded tests form the default (None); an unnamed capture counts for all
        g = catalog.launcher_groups([t("a", "demo"), t("b", launcher="GL1"), t("c", launcher="GL2"), t("p", "capture")])
        self.assertEqual([(n, [x["id"] for x in ts]) for n, ts in g],
                         [(None, ["a", "p"]), ("GL1", ["b", "p"]), ("GL2", ["c", "p"])])
        self.assertEqual(catalog.launcher_groups([]), [(None, [])])

    def test_best_launcher_rates_the_game(self):
        with tempfile.TemporaryDirectory() as d:
            g = Path(d) / "openarena"
            shutil.copytree(REPO / "gamedb" / "content" / "games" / "openarena", g)
            old = next((g / "tests").iterdir())
            txt = re.sub(r'(?m)^launcher = .*\n', "", (old / "result.toml").read_text())
            (old / "result.toml").write_text(txt.replace('kind = "demo"', 'kind = "demo"\nlauncher = "OpenArena (GL 2.0)"'))
            fast = g / "tests" / "2026-10-21-demo"
            fast.mkdir()
            txt = txt.replace('date = "2026-10-20"', 'date = "2026-10-21"').replace('kind = "demo"', 'kind = "demo"\nlauncher = "OpenArena (GL 1.2)"')
            txt = re.sub(r"median_fps = [0-9.]+", "median_fps = 70.0", txt)
            txt = re.sub(r"low1_fps = [0-9.]+", "low1_fps = 50.0", txt)
            txt = re.sub(r"stutter_s_per_min = [0-9.]+", "stutter_s_per_min = 0.05", txt)
            (fast / "result.toml").write_text(txt)
            out = catalog.build_game(g, REPO, {})
            self.assertEqual([l["name"] for l in out["launchers"]], ["OpenArena (GL 1.2)", "OpenArena (GL 2.0)"])
            self.assertEqual(out["rating"]["launcher"], "OpenArena (GL 1.2)")
            self.assertEqual(out["rating"]["score"], out["launchers"][0]["rating"]["score"])
            self.assertGreater(out["rating"]["score"], out["launchers"][1]["rating"]["score"])
            self.assertEqual({t["launcher"] for t in out["tests"]}, {"OpenArena (GL 1.2)", "OpenArena (GL 2.0)"})

    def test_repository_catalog(self):
        cat = catalog.build(REPO / "gamedb" / "content" / "games", REPO)
        self.assertEqual(cat["schema"], 1)
        ids = [g["id"] for g in cat["games"]]
        self.assertTrue({"openarena", "supertuxkart", "warzone2100"} <= set(ids))
        for g in cat["games"]:
            json.dumps(g)
            if g["id"] not in ("openarena", "supertuxkart", "warzone2100"):
                continue
            self.assertNotEqual(g["rating"]["medal"], "Untested")
            self.assertTrue(g["patched"])
            self.assertEqual(g["rating"]["tested"]["walnut"], "pre-0.1")   # runs from before 0.1.0
            self.assertEqual(len(g["launchers"]), 1)
            self.assertEqual(len(g["launchers"][0]["history"]), 1)
            self.assertEqual(g["debs"], [])        # no --deb-index: no links


if __name__ == "__main__":
    unittest.main()
