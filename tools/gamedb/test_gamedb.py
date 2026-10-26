#!/usr/bin/env python3
"""Tests for the Walnut Games tools: python3 -m unittest (from tools/gamedb) or
python3 -I tools/gamedb/test_gamedb.py. The calibration test scores the summary batch suite-summary-2026-10
(gamedb's benchmark records) and compares the medals and docs/GAMEDB.md's generated table, so a change to the
scoring that moves a medal, or a stale table, fails here."""
import contextlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
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


def load_tool(name, path=None):
    """a tool module by its file (tools/gamedb/<name>.py, or path)"""
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), path or HERE / ("%s.py" % name))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


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
        for faults, want in ((0, 100), (10, 90), (30, 82), (60, 72), (120, 60), (240, 50), (500, 50)):
            self.assertAlmostEqual(score.sound_score(faults), want)
        for secs, want in ((10, 100), (15, 100), (30, 75), (60, 55), (120, 0), (300, 0)):
            self.assertAlmostEqual(score.load_score(secs), want)
        self.assertEqual(score.frame_score(20, 10), 0)
        self.assertEqual(score.frame_score(55, 45), 100)
        self.assertAlmostEqual(score.frame_score(55, 10), 50)

    def test_silent_or_wedged_sound_is_zero(self):
        self.assertEqual(score.sound_score(0, wedged=True), 0)
        self.assertEqual(score.sound_score(0, effects="no", music="no"), 0)
        self.assertEqual(score.sound_score(0, effects="no", music="none"), 0)
        self.assertIsNone(score.sound_score(None))

    def test_sound_half_presence_half_clean(self):
        self.assertEqual(score.sound_score(0, effects="yes", music="yes"), 100)
        self.assertEqual(score.sound_score(0, effects="yes", music="none"), 100)   # no music by design
        self.assertEqual(score.sound_score(0, effects="yes", music="no"), 75)
        self.assertEqual(score.sound_score(0, effects="no", music="yes"), 75)
        self.assertEqual(score.sound_score(0, effects="partly", music="yes"), 75)
        self.assertAlmostEqual(score.sound_score(30, effects="yes", music="yes"), 82)
        self.assertEqual(score.sound_score(None, effects="yes", music="yes"), 100)  # not counted: clean

    def test_sound_audible_dropouts(self):
        # measured dropouts (ps3-xrunlog) replace the fault counts' curve, and fall faster
        for per_min, want in ((0, 100), (1, 95), (4, 85), (10, 72), (20, 60), (40, 50), (100, 50)):
            self.assertAlmostEqual(score.sound_score(None, dropouts_per_min=per_min), want)
        self.assertEqual(score.sound_score(90, dropouts_per_min=0, effects="yes", music="yes"), 100)  # counted, unheard
        self.assertAlmostEqual(score.sound_score(0, dropouts_per_min=10, effects="partly", music="yes"), 47)
        self.assertEqual(score.sound_score(0, wedged=True, dropouts_per_min=0), 0)
        self.assertAlmostEqual(score.test_scores({"sound_faults_per_min": 60, "sound_dropouts_per_min": 0.5})["sound"],
                               97.5)

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


class Display(unittest.TestCase):
    def test_samples(self):
        d = score.display_sample
        self.assertEqual(d(0, 0, 1920, 1080, 1, 1920, 1080), (100.0, "fullscreen 1920x1080"))
        self.assertEqual(d(0, 0, 1280, 720, 1, 1280, 720)[0], 100.0)                   # a smaller screen, filled
        self.assertEqual(d(0, 24, 1920, 1056, 0, 1920, 1080)[0], 100.0)             # under a title bar: covers it
        self.assertEqual(d(0, 0, 1920, 1080, 0, 1920, 1080)[0], 100.0)               # undecorated, no state
        self.assertEqual(d(0, 0, 1024, 768, 0, 1920, 1080)[0], 10.0)                  # 38 % of the screen: under half
        self.assertEqual(d(0, 0, 1440, 900, 0, 1920, 1080), (30.0, "a 1440x900 window on 1920x1080"))
        self.assertEqual(d(0, 0, 640, 480, 0, 1920, 1080)[0], 10.0)
        self.assertEqual(d(900, 500, 1200, 700, 0, 1920, 1080)[0], 0.0)              # ends at 2100 x 1200
        self.assertEqual(d(0, 0, 2560, 1440, 0, 1920, 1080)[0], 0.0)                 # cropped by the screen
        self.assertEqual(d(-1, 0, 1920, 1080, 1, 1920, 1080)[0], 100.0)              # a pixel off: not cropped

    def test_median(self):
        full, small = (0, 0, 1920, 1080, 1, 1920, 1080), (0, 0, 640, 480, 0, 1920, 1080)
        self.assertEqual(score.display_score([]), (None, None))
        self.assertEqual(score.display_score([None, None]), (None, None))
        self.assertEqual(score.display_score([full, full, small]), (100.0, "fullscreen 1920x1080"))
        self.assertEqual(score.display_score([full, small, small, None])[0], 10.0)
        self.assertEqual(score.display_score([full, small])[0], 10.0)     # the lower middle when even

    def test_in_the_rating(self):
        t = {"kind": "session", "numbers": {"display_score": 10.0, "display_detail": "a 640x480 window on 1920x1080"}}
        self.assertEqual(score.combine([t])["aspects"]["display"], 10.0)
        self.assertEqual(score.combine([t])["numbers"]["display_detail"], "a 640x480 window on 1920x1080")


class Geom(unittest.TestCase):
    """bench/geom.py's window choice, with xwininfo/xprop faked"""
    TREE = """
xwininfo: Window id: 0x1a3 (the root window) (has no name)

  Root window id: 0x1a3 (the root window) (has no name)
  Parent window id: 0x0 (none)
     5 children:
     0x400001 "panel": ("panel" "Panel")  1920x32+0+0  +0+0
     0x600001 "Game": ("game" "Game")  1920x1080+0+0  +0+0
        1 child:
        0x600002 (has no name): ()  1920x1080+0+0  +0+0
     0x700001 "other": ("o" "O")  800x600+10+10  +10+10
     0x800001 "tiny": ("t" "T")  10x10+0+0  +0+0
"""
    PROPS = {"0x400001": "_NET_WM_PID(CARDINAL) = 50\n_NET_WM_WINDOW_TYPE(ATOM) = _NET_WM_WINDOW_TYPE_DOCK",
             "0x600001": "_NET_WM_PID(CARDINAL) = 301\n_NET_WM_STATE(ATOM) = _NET_WM_STATE_FULLSCREEN",
             "0x600002": "", "0x700001": "_NET_WM_PID(CARDINAL) = 99"}

    def pick(self, props, unmapped=()):
        geom = load_geom()
        def fake(*cmd):
            if cmd[0] == "xprop":
                return props.get(cmd[2], "")
            return "IsUnMapped" if cmd[2] in unmapped else "Map State: IsViewable"
        geom.run = fake
        return geom.pick(self.TREE, {300, 301}, (1920, 1080))

    def test_by_pid(self):
        self.assertEqual(self.pick(self.PROPS), (0, 0, 1920, 1080, 1, "pid"))     # the pid's child; panel, other pid skipped

    def test_no_pid_falls_back_to_the_largest(self):
        props = {"0x600001": "", "0x700001": "_NET_WM_PID(CARDINAL) = 99", "0x400001": "_NET_WM_PID(CARDINAL) = 50"}
        self.assertEqual(self.pick(props), (0, 0, 1920, 1080, 1, "size"))          # no pid, covers the screen
        self.assertIsNone(self.pick(props, unmapped=("0x600001", "0x600002")))

    def test_family(self):
        geom = load_geom()
        self.assertIn(os.getpid(), geom.family(os.getppid()))


def load_geom():
    spec = importlib.util.spec_from_file_location("geom", REPO / "bench" / "geom.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


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


# the medals of the calibration (docs/GAMEDB.md, rules 1): a change to the scoring or to the summary batch's
# numbers that moves one fails here
CALIBRATION_MEDALS = ["Platinum", "Platinum", "Platinum", "Gold", "Gold", "Gold", "Gold", "Gold", "Silver", "Silver",
                      "Silver", "Bronze", "Bronze", "Bronze", "Scrap"]


class Calibration(unittest.TestCase):
    """the 13 benchmarked games (15 rows, the summary batch suite-summary-2026-10) come out with their medals,
    and docs/GAMEDB.md's generated table is what blocks.py makes of them"""

    def test_medals(self):
        rows = score.calibration()
        self.assertEqual(len(rows), 15)
        self.assertEqual([score.calibrate(r)[1]["medal"] for r in rows], CALIBRATION_MEDALS)
        self.assertEqual([r[0] for r in rows][:3], ["X-Moto", "OpenArena, opengl1", "Yamagi Quake II"])

    def test_table_matches_doc(self):
        blocks = load_tool("blocks")
        text = (REPO / "docs" / "GAMEDB.md").read_text()
        body = text.split("<!-- gamedb:calibration -->\n", 1)[1].split("<!-- /gamedb:calibration -->")[0]
        self.assertEqual(body, blocks.gen_calibration(REPO, []))

    def test_scrap_reason(self):
        _, r = score.calibrate(score.calibration()[-1])
        self.assertEqual((r["medal"], r["reason"]), ("Scrap", "too slow"))


class Records(unittest.TestCase):
    """the benchmark records (gamedb/benchmarks/README.md): check.py's rules on them, the CSV matrices they
    make, bench-import.py's column split, the README's generated suite table"""

    def setUp(self):
        self.br = load_tool("benchrecords", REPO / "bench" / "benchrecords.py")
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        (self.tmp / "benchmarks").mkdir()
        shutil.copy(REPO / "gamedb" / "benchmarks" / "metrics.toml", self.tmp / "benchmarks")
        self.br.save_batches([
            {"name": "b1", "order": 1, "kind": "run", "title": "Mesa ps3.1", "date": __import__("datetime").date(2026, 10, 1),
             "runs": 2, "observations": "ran, \"quoted\"", "source": "test"},
            {"name": "b2", "order": 2, "kind": "run", "title": "Mesa ps3.2", "runs": 1, "source": "test"},
            {"name": "s", "kind": "summary", "source": "test"}], self.tmp)
        self.br.save_result("g", {"batch": "b1", "column": "old", "variant": {"base@rec10": {"fps": 50.0, "stalls": 2}}}, self.tmp)
        self.br.save_result("g", {"batch": "b2", "variant": {"base": {"fps": 51.5, "stall_gpu_s": 0.2}}}, self.tmp)
        self.br.save_result("g", {"batch": "s", "variant": {"base": {"label": "G", "order": 1, "fps_steady": 50,
                                                                      "source": {"fps_steady": "gamedb:batch/b1"}}}}, self.tmp)

    def test_valid(self):
        self.assertEqual(self.br.check_records(self.tmp, {"g"}), [])

    def test_matrices(self):
        m = self.br.matrices(self.tmp)
        self.assertEqual(m["fps"][0], ["g-base", "old-base@rec10"])
        self.assertEqual([r["test"] for r in m["fps"][1]], ["Mesa ps3.1", "Mesa ps3.2"])
        self.assertEqual(m["fps"][1][0]["date"], "20261001")
        self.assertIn("stall_gpu_s", m)            # when-set: made, since a batch has it
        self.assertNotIn("stall_cpu_s", m)         # when-set and nobody has it
        self.assertIn("cpu", m)                    # always, even empty
        self.assertNotIn("sound_faults_per_min", m)
        self.br.to_csv(self.tmp / "csv", self.tmp)
        self.assertEqual((self.tmp / "csv" / "fps.csv").read_bytes().split(b"\r\n")[1],
                         b'Mesa ps3.1,20261001,2,"ran, ""quoted""",,50.0')

    def test_problems(self):
        f = self.br.result_path("g", "b2", self.tmp)
        f.write_text(f.read_text().replace("fps = 51.5", "fsp = 51.5\nnote = \"x\""))
        self.br.save_result("g", {"batch": "nope", "variant": {"base": {"fps": 1}}}, self.tmp)
        (self.tmp / "games" / "g" / "benchmark" / "results" / "x.csv").write_text("")
        b = self.br.load_batches(self.tmp)
        b[1]["order"] = 1
        b[1]["colour"] = "red"
        self.br.save_batches(b, self.tmp)
        (self.tmp / "benchmarks" / "batches.toml").write_text((self.tmp / "benchmarks" / "batches.toml").read_text()
                                                            + 'colour = "red"\n')
        errs = "\n".join(self.br.check_records(self.tmp, {"h"}))
        for want in ("unknown metric 'fsp'", "note belongs to a summary", "no batch 'nope'", "results/ holds <batch>.toml",
                     "order 1 after 1", "unknown key 'colour'", "no game g"):
            self.assertIn(want, errs)

    def test_check_py_reads_them(self):
        self.assertEqual(check.records_errors(REPO / "gamedb"), [])

    def test_matrix_metrics(self):
        matrix = load_bench("matrix")
        want = {m["name"] for m in self.br.load_metrics() if m["matrix"] != "none"}
        self.assertEqual(set(matrix.METRICS), want)

    def test_import_split(self):
        imp = load_tool("bench-import")
        names = imp.names_map(REPO / "gamedb")
        self.assertEqual(imp.split("sauerbraten-base@rec10", names), ("bananabread", "sauerbraten", "base@rec10"))
        self.assertEqual(imp.split("sauerbraten-edition-base", names), ("sauerbraten", "sauerbraten-edition", "base"))
        self.assertEqual(imp.split("supertuxkart-lean--@rec10", names), ("supertuxkart", "supertuxkart", "lean--@rec10"))
        self.assertEqual(imp.split("dsda-doom-gl@wkkick10", names), ("dsda-doom", "dsda-doom", "gl@wkkick10"))
        self.assertEqual(imp.stack("batch ssdroot1+2 A: baseline on the SSD root (kernel .472 Mesa .476 ps3rsx 0.443)"),
                         {"kernel": "ps3.472", "mesa": "ps3.476", "ps3rsx": "0.443"})

    def test_suite_table_matches_readme(self):
        blocks = load_tool("blocks")
        text = (REPO / "README.md").read_text()
        body = text.split("<!-- gamedb:suite-table -->\n", 1)[1].split("<!-- /gamedb:suite-table -->")[0]
        self.assertEqual(body, blocks.gen_suite_table(REPO, []))


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


class RulesTwoSessionsOnly(unittest.TestCase):
    """rules 2: a rating comes only from played sessions; scripted, demo and capture runs count for nothing"""
    def test(self):
        nums = {"median_fps": 60, "low1_fps": 50, "stutter_s_per_min": 0, "longest_ms": 40, "sound_faults_per_min": 0}
        demo = {"kind": "demo", "play_seconds": 7200, "crashed": False, "numbers": nums}
        script = {"kind": "script", "play_seconds": 7200, "crashed": False, "numbers": nums,
                  "checks": {"pad_play": "yes", "pad_hotplug": "yes"}}
        self.assertEqual(score.combine([demo, script], rules=2)["medal"], "Untested")   # no session, no rating
        sess = {"kind": "session", "play_seconds": 600, "crashed": False, "numbers": nums, "date": "2026-10-26",
                "picture": {"verdict": "ok"}, "review": {}}
        alone = score.combine([sess], rules=2)
        with_extra = score.combine([sess, demo, script], rules=2)
        self.assertEqual(with_extra["hours"], alone["hours"])                           # no crash-free hours
        self.assertEqual(with_extra["checks"], alone["checks"])                         # no scripted answers
        self.assertNotIn("pad_hotplug", with_extra["checks"])
        self.assertEqual(with_extra["aspects"]["stability"], alone["aspects"]["stability"])
        self.assertEqual(score.checks_of([script]), {})
        crashed = dict(demo, crashed=True, end="crash")
        self.assertEqual(score.combine([sess, crashed], rules=2)["crashes"], 0)


BUILD_KEYS = ("packages", "engine", "suite", "needs_data", "data", "desktop", "divert", "image_base", "recipes", "meta")


def write_game(content, gid="ex", **over):
    """a game folder: index.md, and build.toml with the keys of BUILD_KEYS (over: TOML values; build=None
    leaves build.toml out, build="text" appends text to it)"""
    fm = {"id": gid, "title": "Example", "summary": "An example.", "categories": '["puzzle"]', "tags": '["a"]',
          "license": '"GPL-2.0-or-later"', "homepage": '"https://example.org/"', "packages": '["example"]',
          "desktop": '["example.desktop"]', "needs_data": "false", "controller": '"keyboard"', "suite": '"none"'}
    fm = {k: (v if k in ("categories", "tags", "license", "homepage", "controller") + BUILD_KEYS else '"%s"' % v)
          for k, v in fm.items()}
    extra = over.pop("build", "")
    fm.update(over)
    bt = {k: v for k, v in fm.items() if k in BUILD_KEYS}
    fm = {k: v for k, v in fm.items() if k not in BUILD_KEYS}
    g = Path(content) / gid
    g.mkdir(parents=True, exist_ok=True)
    (g / "index.md").write_text("+++\n" + "\n".join("%s = %s" % kv for kv in fm.items()) + "\n+++\nbody\n")
    if extra is not None:
        (g / "build.toml").write_text("".join("%s = %s\n" % kv for kv in bt.items()) + extra)
    elif (g / "build.toml").exists():
        (g / "build.toml").unlink()
    (g / "thumb.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"0" * 20)
    return g


class Check(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.content = Path(self.tmp.name) / "games"
        self.repo = Path(self.tmp.name) / "repo"
        (self.repo / "tools").mkdir(parents=True)
        (self.repo / "tools" / "licensed-files.txt").write_text("# comment\n*.prx\n")
        (self.repo / "gamedb" / "games" / "q").mkdir(parents=True)
        (self.repo / "gamedb" / "games" / "q" / "build.toml").write_text(
            '[[licensed]]\nname = "quake-shareware"\nbuilder = "game-data-packager:quake"\n'
            '[[licensed]]\nname = "rct2-data"  # x\nbuilder = "script"\n')

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
        self.assertIn("data_steps", " ".join(self.problems()["ex"]))
        write_game(self.content, needs_data="true", data_guide='"https://example.org/guide"',
                   data_steps='["Download the data.", "Run `sudo apt install ./data.deb`."]')
        self.assertEqual(self.problems(), {})

    def test_data_steps_are_checked(self):
        guide = dict(needs_data="true", data_guide='"https://example.org/guide"')
        write_game(self.content, data_steps='["Fine."]', **guide)
        self.assertEqual(self.problems(), {})
        write_game(self.content, data_steps='["Run `ls."]', **guide)
        self.assertIn("backticks", " ".join(self.problems()["ex"]))
        write_game(self.content, data_steps='[""]', **guide)
        self.assertIn("non-empty", " ".join(self.problems()["ex"]))
        write_game(self.content, data_steps='["Not needed."]')
        self.assertIn("without needs_data", " ".join(self.problems()["ex"]))

    def test_unknown_field_and_missing_thumb(self):
        g = write_game(self.content, colour='"red"')
        (g / "thumb.png").unlink()
        text = " ".join(self.problems()["ex"])
        self.assertIn("unknown field", text)
        self.assertIn("thumb.png", text)

    def test_build_toml_keys(self):
        write_game(self.content, colour='"red"', build='size = 3\n')
        text = " ".join(self.problems()["ex"])
        self.assertIn("build.toml: unknown key 'size'", text)
        self.assertIn("unknown field 'colour'", text)
        write_game(self.content, build=None)                    # a published game needs one
        self.assertIn("no build.toml", " ".join(self.problems()["ex"]))
        write_game(self.content, draft="true", status='"candidate"', build=None)   # a draft doesn't
        self.assertEqual(self.problems(), {})
        write_game(self.content, suite='"some"')
        self.assertIn("suite 'some'", " ".join(self.problems()["ex"]))
        write_game(self.content, needs_data='"no"')
        self.assertIn("'needs_data' should be bool", " ".join(self.problems()["ex"]))
        write_game(self.content, suite='"licensed"')            # licensed: its data is
        self.assertIn("no data package is licensed", " ".join(self.problems()["ex"]))
        write_game(self.content, suite='"licensed"', data='["quake-shareware"]')
        self.assertEqual(self.problems(), {})
        write_game(self.content, divert='["usr/share/x.desktop"]', image_base="true")
        self.assertIn("divert", " ".join(self.problems()["ex"]))
        write_game(self.content, meta='["example"]')
        self.assertEqual(self.problems(), {})
        write_game(self.content, meta='["other"]')               # only its own packages
        self.assertIn("meta ['other']", " ".join(self.problems()["ex"]))

    def test_desktop(self):
        g = write_game(self.content)
        (g / "desktop" / "launchers").mkdir(parents=True)
        (g / "desktop" / "launchers" / "ex.desktop").write_text("[Desktop Entry]\nName=Ex\nExec=ps3-game ex -f\n")
        (g / "desktop" / "files" / "etc").mkdir(parents=True)
        (g / "desktop" / "files" / "etc" / "ex.cfg").write_text("x\n")
        (g / "desktop" / "migrate-home.txt").write_text("# c\n.ex/ex.cfg /etc/ex.cfg\n")
        self.assertEqual(self.problems(), {})
        (g / "desktop" / "launchers" / "ex.desktop").write_text("[Desktop Entry]\nName=Ex\nExec=ex -f\n")
        self.assertIn("through ps3-game", " ".join(self.problems()["ex"]))
        (g / "desktop" / "launchers" / "ex.desktop").write_text("[Desktop Entry]\nName=Ex\nExec=ps3-game ex\n")
        (g / "desktop" / "migrate-home.txt").write_text("/abs/path\n")
        (g / "desktop" / "notes.txt").write_text("")
        text = " ".join(self.problems()["ex"])
        self.assertIn("migrate-home.txt line", text)
        self.assertIn("unexpected file desktop/notes.txt", text)

    def test_build_toml_licensed(self):
        lic = '[[licensed]]\nname = "ex-data"\nbuilder = "source"\n'
        write_game(self.content, build=lic + 'meta = "yes"\n')
        self.assertIn("meta is true or false", " ".join(self.problems()["ex"]))
        write_game(self.content, build=lic)
        self.assertEqual(self.problems(), {})       # no licensed directory anywhere: the public copy
        g = self.content / "ex" / "licensed" / "ex-data"
        g.mkdir(parents=True)
        (g / "package.conf").write_text("")
        other = self.content / "ex" / "licensed" / "ex-music"
        other.mkdir()
        text = " ".join(self.problems()["ex"])
        self.assertIn("licensed/ex-music is not declared", text)
        other.rmdir()
        self.assertEqual(self.problems(), {})
        (g / "package.conf").unlink()                # a source builder is a package
        self.assertIn("needs licensed/ex-data/package.conf", " ".join(self.problems()["ex"]))
        write_game(self.content, build=lic.replace('"source"', '"script"'))
        self.assertIn("needs licensed/ex-data/ex-data.sh", " ".join(self.problems()["ex"]))
        (g / "ex-data.sh").write_text("")
        self.assertEqual(self.problems(), {})
        write_game(self.content, build=lic.replace('"source"', '"cd"') + 'size = 1\n')
        text = " ".join(self.problems()["ex"])
        self.assertIn("builder 'cd'", text)
        self.assertIn("unknown key 'size'", text)   # inside [[licensed]]
        write_game(self.content, build=lic.replace('"source"', '"game-data-packager:ex"'))
        self.assertIn("game-data-packager makes it", " ".join(self.problems()["ex"]))
        write_game(self.content, build=lic.replace('"source"', '"script"') + 'leak_files = ["/abs"]\n')
        self.assertIn("leak_files", " ".join(self.problems()["ex"]))

    def test_build_toml_engine_and_packages(self):
        e = self.content.parent / "engines" / "eng"
        e.mkdir(parents=True)
        self.content.mkdir()
        (e / "engine.md").write_text('+++\nid = "eng"\ntitle = "Eng"\ngames = ["ex"]\n+++\n')
        self.assertIn("doesn't say engine", " ".join(self.problems()["engines/eng"]))
        write_game(self.content, engine='"eng"')
        self.assertEqual(self.problems(), {})
        write_game(self.content, engine='"other"')
        self.assertIn("no gamedb/engines/other", " ".join(self.problems()["ex"]))
        (e / "build.toml").write_text('packages = ["eng"]\nsuite = "free"\n')
        write_game(self.content, engine='"eng"')
        self.assertIn("an engine's", " ".join(self.problems()["engines/eng"]))
        (e / "build.toml").write_text('packages = ["eng"]\n')
        self.assertEqual(self.problems(), {})
        # with an apt index, every package is in it or built by a debian/control of ours
        self.assertEqual(check.check(self.content, self.repo, apt={"example", "eng"}), {})
        self.assertIn("neither in the apt index", " ".join(check.check(self.content, self.repo, apt={"eng"})["ex"]))

    def test_game_facts_and_patched_by(self):
        e = self.content.parent / "engines" / "eng"
        (e / "packages" / "eng").mkdir(parents=True)
        (e / "packages" / "eng" / "package.conf").write_text("")
        (e / "engine.md").write_text('+++\nid = "eng"\ntitle = "Eng"\ngames = ["ex"]\n+++\n')
        g = write_game(self.content, engine='"eng"', recipes='["ex:base"]')
        (g / "packages" / "ex").mkdir(parents=True)
        (g / "packages" / "ex" / "package.conf").write_text("")
        (g / "packages" / "notapkg").mkdir()
        fm = check.game_facts(g)
        self.assertEqual(fm["patched_by"], ["ex", "eng"])
        self.assertEqual((fm["packages"], fm["needs_data"], fm["desktop"], fm["recipes"]),
                         (["example"], False, ["example.desktop"], ["ex:base"]))
        self.assertNotIn("suite", fm)

    def test_test_folder_files(self):
        g = write_game(self.content)
        t = g / check.SESSIONS / "2026-01-01-demo"
        t.mkdir(parents=True)
        (t / "result.toml").write_text('schema = 1\nkind = "demo"\ngame = "ex"\n[shots]\nps3 = ["shots/ps3-1s.jpg"]\n')
        self.assertIn("missing", " ".join(self.problems()["ex"]))
        (t / "shots").mkdir()
        (t / "shots" / "ps3-1s.jpg").write_bytes(b"x")
        (t / "stray.dat").write_bytes(b"x")
        self.assertIn("unexpected file stray.dat", " ".join(self.problems()["ex"]))
        (t / "stray.dat").unlink()
        self.assertNotIn("ex", self.problems())
        # the sessions live in rating/sessions/ only: the old tests/ folder, or anything else in rating/, is a stray
        (g / "tests").mkdir()
        (g / "rating" / "notes.txt").write_text("x")
        errs = " ".join(self.problems()["ex"])
        self.assertIn("unexpected file tests", errs)
        self.assertIn("unexpected file rating/notes.txt", errs)

    def test_draft_needs_only_id_title_status(self):
        g = Path(self.content) / "cand"
        g.mkdir(parents=True)
        (g / "index.md").write_text('+++\nid = "cand"\ntitle = "Candidate"\ndraft = true\nstatus = "candidate"\n+++\n')
        self.assertEqual(self.problems(), {})
        (g / "index.md").write_text('+++\nid = "cand"\ntitle = "Candidate"\ndraft = true\n+++\n')
        self.assertIn("missing field 'status'", " ".join(self.problems()["cand"]))
        (g / "index.md").write_text('+++\nid = "cand"\ntitle = "C"\ndraft = true\nstatus = "maybe"\n+++\n')
        self.assertIn("status 'maybe'", " ".join(self.problems()["cand"]))
        # what a draft has is still checked
        (g / "index.md").write_text('+++\nid = "cand"\ntitle = "C"\ndraft = true\nstatus = "tried"\n'
                                    'status_note = "crashes at start"\ncategories = ["fun"]\nhomepage = "x"\n+++\n')
        text = " ".join(self.problems()["cand"])
        self.assertIn("categories", text)
        self.assertIn("homepage", text)
        (g / "index.md").write_text('+++\nid = "cand"\ntitle = "C"\ndraft = "yes"\nstatus = "tried"\n+++\n')
        self.assertIn("'draft' should be bool", " ".join(self.problems()["cand"]))

    def test_published_game_keeps_the_full_rules(self):
        g = write_game(self.content, draft="false", status='"tested"')
        self.assertEqual(self.problems(), {})
        (g / "thumb.png").unlink()
        self.assertIn("thumb.png", " ".join(self.problems()["ex"]))
        g2 = Path(self.content) / "nofields"
        g2.mkdir()
        (g2 / "index.md").write_text('+++\nid = "nofields"\ntitle = "N"\nstatus = "candidate"\n+++\n')
        self.assertIn("missing field 'summary'", " ".join(self.problems()["nofields"]))

    def test_drafts_left_out_of_the_catalog_and_pack(self):
        write_game(self.content)
        g = Path(self.content) / "cand"
        g.mkdir()
        (g / "index.md").write_text('+++\nid = "cand"\ntitle = "Candidate"\ndraft = true\nstatus = "candidate"\n+++\n')
        cat = catalog.build(self.content, self.repo)
        self.assertEqual([x["id"] for x in cat["games"]], ["ex"])
        out = Path(self.tmp.name) / "catalog.tar"
        spec = importlib.util.spec_from_file_location("pack_catalog", HERE / "pack-catalog.py")
        pack = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pack)
        pack.pack(self.content, out, self.repo)
        with tarfile.open(out) as t:
            self.assertFalse([n for n in t.getnames() if "cand" in n])

    def test_repository_catalog_is_valid(self):
        self.assertEqual(check.check(check.CONTENT), {})


class BuildInfo(unittest.TestCase):
    """tools/gamedb/build-info.py over a small gamedb tree"""
    def run_info(self, root, *args):
        p = subprocess.run([sys.executable, "-I", str(HERE / "build-info.py"), "--root", str(root)] + list(args),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return p.returncode, p.stdout.split()

    def test_answers(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t) / "gamedb"
            for gid, text in (("q", 'packages = ["quakespasm"]\nsuite = "licensed"\ndata = ["quake-shareware"]\n'
                                    '[[licensed]]\nname = "quake-shareware"\nbuilder = "game-data-packager:quake"\n'),
                              ("r", 'data = ["rct2-data"]\n[[licensed]]\nname = "rct2-data"\nbuilder = "script"\n'),
                              ("f", 'packages = ["free", "free-data"]\nsuite = "free"\n')):
                (root / "games" / gid).mkdir(parents=True)
                (root / "games" / gid / "build.toml").write_text(text)
            self.assertEqual(self.run_info(root, "game-data"), (0, ["quake:quake-shareware"]))
            self.assertEqual(self.run_info(root, "scripts"), (0, ["rct2-data"]))
            self.assertEqual(self.run_info(root, "needs-licensed"), (0, ["quakespasm"]))
            self.assertEqual(self.run_info(root, "data", "q"), (0, ["quake-shareware"]))
            self.assertEqual(self.run_info(root, "packages", "f"), (0, ["free", "free-data"]))
            self.assertEqual(self.run_info(root, "packages", "none")[0], 2)
            self.assertEqual(self.run_info(root, "script", "rct2-data")[0], 2)    # no licensed/ here
            (root / "games" / "r" / "licensed" / "rct2-data").mkdir(parents=True)
            (root / "games" / "r" / "licensed" / "rct2-data" / "rct2-data.sh").write_text("")
            self.assertEqual(self.run_info(root, "script", "rct2-data"),
                             (0, ["gamedb/games/r/licensed/rct2-data/rct2-data.sh"]))
            self.assertEqual(self.run_info(root, "script", "quake-shareware")[0], 2)
            self.assertEqual(self.run_info(root, "image-base"), (0, []))
            (root / "games" / "f" / "build.toml").write_text('packages = ["free", "free-data"]\nimage_base = true\n')
            self.assertEqual(self.run_info(root, "image-base"), (0, ["free", "free-data"]))
            self.assertEqual(self.run_info(root, "own-packages", "r"), (0, []))  # rct2-data.sh: no package.conf
            for d in ("packages/r-engine", "licensed/rct2-data"):
                (root / "games" / "r" / d).mkdir(parents=True, exist_ok=True)
                (root / "games" / "r" / d / "package.conf").write_text("")
            self.assertEqual(self.run_info(root, "own-packages", "r"), (0, ["r-engine", "rct2-data"]))
            self.assertEqual(self.run_info(root, "own-packages", "none")[0], 2)
            (root / "games" / "f" / "build.toml").write_text("packages = [\n")
            self.assertEqual(self.run_info(root, "game-data")[0], 2)            # fails closed


class PackIntegration(unittest.TestCase):
    """tools/gamedb/pack-integration.py over a small gamedb tree"""
    def setUp(self):
        spec = importlib.util.spec_from_file_location("pack_integration", HERE / "pack-integration.py")
        self.pi = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.pi)
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "games"
        self.out = Path(self.tmp.name) / "out"

    def tearDown(self):
        self.tmp.cleanup()

    def game(self, gid, build, draft=False, launchers=(), files=(), migrate=None):
        g = self.root / gid
        g.mkdir(parents=True)
        (g / "index.md").write_text('+++\nid = "%s"\n%s+++\n' % (gid, "draft = true\n" if draft else ""))
        (g / "build.toml").write_text(build)
        for name, text in launchers:
            (g / "desktop" / "launchers").mkdir(parents=True, exist_ok=True)
            (g / "desktop" / "launchers" / name).write_text(text)
        for path, text in files:
            f = g / "desktop" / "files" / path
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text(text)
        if migrate is not None:
            (g / "desktop" / "migrate-home.txt").write_text(migrate)

    def pack(self):
        os.environ["SOURCE_DATE_EPOCH"] = "0"
        try:
            return self.pi.pack(self.root, self.out / "i.tar", self.out / "lic.substvars", self.out / "tree")
        finally:
            del os.environ["SOURCE_DATE_EPOCH"]

    def read(self, name):
        return (self.out / "tree" / name).read_text()

    def test_depends_and_tree(self):
        self.game("a", 'packages = ["a", "a-data"]\nmeta = ["a"]\nsuite = "free"\n'
                       'divert = ["/usr/share/applications/a.desktop"]\n',
                  launchers=[("a-gl.desktop", "[Desktop Entry]\nName=A\nExec=ps3-game env X=1 a -gl\nTryExec=old\n")],
                  files=[("usr/lib/a/a.cfg", "set x 1\n")], migrate="# home\n.a/a.cfg /usr/lib/a/a.cfg\n")
        self.game("b", 'packages = ["b"]\nsuite = "licensed"\ndata = ["b-data"]\n'
                       '[[licensed]]\nname = "b-data"\nbuilder = "source"\n', draft=True)   # a draft counts
        self.game("c", 'packages = ["c"]\nsuite = "none"\n[[licensed]]\nname = "c-data"\nbuilder = "source"\n'
                       'meta = true\n[[licensed]]\nname = "c-extra"\nbuilder = "script"\n')
        n, f, deps = self.pack()
        self.assertEqual(deps["ps3-games"], {"Depends": ["a", "b"], "Suggests": []})
        self.assertEqual(deps["ps3-games-free"], {"Depends": ["a"], "Suggests": ["b"]})
        self.assertEqual(deps["ps3-games-licensed"]["Depends"], ["b-data", "c-data"])
        self.assertEqual(self.read("control/ps3-games-free.substvars"), "games:Depends=a\ngames:Suggests=b\n")
        self.assertEqual((self.out / "lic.substvars").read_text(), "games:Depends=b-data, c-data\n")
        self.assertEqual(self.read("tree/usr/share/applications/a-gl.desktop"),
                         "[Desktop Entry]\nName=A\nExec=ps3-game env X=1 a -gl\nTryExec=a\n")
        self.assertEqual(self.read("tree/usr/lib/a/a.cfg"), "set x 1\n")
        self.assertEqual(self.read("tree/usr/share/ps3-games/divert.list"), "/usr/share/applications/a.desktop\n")
        self.assertEqual(self.read("tree/usr/share/ps3-games/migrate-home.list"), ".a/a.cfg /usr/lib/a/a.cfg\n")
        with tarfile.open(self.out / "i.tar") as t:
            names = t.getnames()
            self.assertEqual(names, sorted(names))
            self.assertTrue(all(m.uid == 0 and m.mtime == 0 for m in t.getmembers()))
        first = (self.out / "i.tar").read_bytes()
        self.pack()
        self.assertEqual((self.out / "i.tar").read_bytes(), first)          # reproducible

    def test_fails_closed(self):
        self.game("a", 'packages = ["a"]\nmeta = ["z"]\nsuite = "free"\n')
        with self.assertRaisesRegex(self.pi.Error, "meta z not in its packages"):
            self.pack()
        shutil.rmtree(self.root / "a")
        self.game("a", 'packages = ["a"]\nsuite = "free"\n', launchers=[("a.desktop", "Exec=a\n")])
        with self.assertRaisesRegex(self.pi.Error, "through ps3-game"):
            self.pack()
        shutil.rmtree(self.root / "a")
        self.game("a", 'packages = ["a"]\nsuite = "free"\n', files=[("etc/x.cfg", "1")])
        self.game("b", 'packages = ["b"]\nsuite = "free"\n', files=[("etc/x.cfg", "2")])
        with self.assertRaisesRegex(self.pi.Error, "a and b both install /etc/x.cfg"):
            self.pack()
        (self.root / "b" / "build.toml").write_text("packages = [\n")
        self.assertEqual(self.pi.main(["--root", str(self.root), "--out", str(self.out / "x.tar")]), 2)

    def test_this_gamedb(self):
        """the real gamedb packs, every launcher gets a TryExec, nothing licensed goes into the public tarball"""
        with tempfile.TemporaryDirectory() as t:
            os.environ["SOURCE_DATE_EPOCH"] = "0"
            try:
                _, _, deps = self.pi.pack(self.pi.GAMES, Path(t) / "i.tar", Path(t) / "l.substvars", Path(t) / "tree")
            finally:
                del os.environ["SOURCE_DATE_EPOCH"]
            apps = list((Path(t) / "tree" / "tree" / "usr/share/applications").glob("*.desktop"))
            self.assertTrue(apps)
            for a in apps:
                self.assertIn("\nTryExec=", a.read_text())
            public = " ".join((Path(t) / "tree" / "control" / f).read_text()
                              for f in ("ps3-games.substvars", "ps3-games-free.substvars"))
            for name in deps["ps3-games-licensed"]["Depends"]:
                self.assertNotIn(name, public)


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
        self.ir.import_run(self.run_dir, "ex", self.content)
        out = list((self.content / "ex" / check.SESSIONS).iterdir())
        self.assertEqual(len(out), 1)
        return tomllib.loads((out[0] / "result.toml").read_text()), out[0]

    def test_crash_freeze_is_the_crash(self):
        # frames, then a 60 s freeze into a crash: smoothness and the input checks stop where the freeze began
        ft = ["20"] * 4500 + ["400"] * 5 + ["60000"]
        (self.run_dir / "hud" / "frametime_(ms)").write_text("\n".join(ft) + "\n")
        self.write_bench(ended="crash", rc="139", seconds="170")
        b = self.ir.report.kv(self.run_dir / "bench.txt")
        self.assertAlmostEqual(self.ir.crash_freeze(self.run_dir, b), 102.0, delta=1.0)
        keep, _ = self.ir.frames(self.run_dir, b)
        self.assertLess(max(keep), 1000)
        (self.run_dir / "input.tsv").write_text("# inputlog readable=keyboard,pad unreadable=-\nt_s\twhat\tdev\tclass\tn\n"
                                                "50\tpress\t-\tkeyboard\t2\n130\tpress\t-\tkeyboard\t9\n")
        self.assertEqual(self.ir.input_numbers(self.run_dir, 10, self.ir.crash_freeze(self.run_dir, b))["kb_presses"], 2)
        self.write_bench(ended="quit", rc="0", seconds="170")
        self.assertIsNone(self.ir.crash_freeze(self.run_dir, self.ir.report.kv(self.run_dir / "bench.txt")))

    def test_local_stutter(self):
        # a steady slow stretch (100 ms frames) is no stutter against its neighbours; a 400 ms spike in it is
        ft = [20.0] * 300 + [100.0] * 300 + [20.0] * 300
        self.assertEqual(self.ir.local_stutter(ft), 0)
        ft[450] = 400.0
        self.assertAlmostEqual(self.ir.local_stutter(ft), 300.0)

    def test_later_level_loads_are_left_out(self):
        # play marks at 10 s and 60 s; a 5 s load frame before the second: out of the play, counted as a load
        ft = ["20"] * 2500 + ["5000"] + ["20"] * 2500
        (self.run_dir / "hud" / "frametime_(ms)").write_text("\n".join(ft) + "\n")
        self.write_bench(play_from="mark", play_start="10", play_marks="10.00 60.00", seconds="110", ended="quit", rc="0")
        b = self.ir.report.kv(self.run_dir / "bench.txt")
        self.assertEqual(self.ir.play_marks(self.run_dir, b), [10.0, 60.0])
        loads = self.ir.level_loads(self.run_dir, b)
        self.assertEqual(len(loads), 2)
        self.assertLess(loads[1][0], 60.0)
        keep, _ = self.ir.frames(self.run_dir, b)
        self.assertLess(max(keep), 1000)

    def test_numbers(self):
        r, _ = self.do()
        n = r["numbers"]
        self.assertEqual(r["kind"], "demo")
        self.assertEqual(r["date"], "2026-10-01")
        self.assertAlmostEqual(n["median_fps"], 50.0)
        self.assertEqual(n["longest_ms"], 400.0)
        # five frames of 400 ms lose 0.38 s each: 1.9 s in the frames after the warm-up (the frames run from
        # the load's end at 10 s to 102 s; WARMUP_S after the load is 40 s: 62 s measured)
        self.assertAlmostEqual(n["stutter_s_per_min"], 1.9 / (62 / 60), places=1)
        self.assertEqual(n["sound_faults"], 3)
        self.assertAlmostEqual(n["sound_faults_per_min"], 3 / (100 / 60), places=2)
        self.assertEqual(r["play_seconds"], 100.0)
        self.assertFalse(r["crashed"])
        self.assertEqual(r["versions"]["kernel"], "6.12.0+ps3.1")
        self.assertNotIn(self.tmp.name, json.dumps(r))      # no absolute paths
        self.assertNotIn("sound_dropouts", n)               # no ps3-xrunlog in this run: the counts score

    def test_session_play_mark_starts_the_play(self):
        # a session that logged its play mark at 40 s: frames from 40 + 30 s; the load is the start-up (10 s)
        self.write_bench(play_from="mark", play_start="40", load_end="40", hud_t1="12")
        n = self.do()[0]["numbers"]
        self.assertEqual(n["load_s"], 12.0)       # the first frame, not the mark after the menus
        self.assertEqual(self.ir.play_start(self.run_dir, self.ir.report.kv(self.run_dir / "bench.txt")), 40.0)

    def test_sound_dropouts(self):
        # 92 underruns counted, one of them audible (xruns.py), and one OpenAL wait: 2 dropouts
        self.write_bench(xrunlog="on", xruns_logged="92", xruns_audible="1", xrun_gap_ms_total="310.25", oal_wait="1")
        r, _ = self.do()
        n = r["numbers"]
        self.assertEqual((n["sound_dropouts"], n["xruns"], n["xrun_gap_ms"]), (2, 92, 310.2))
        self.assertAlmostEqual(n["sound_dropouts_per_min"], 1.71)   # 2 in the 70 s of play after the warm-up
        self.assertEqual(n["sound_faults"], 4)                 # still recorded, no longer scored
        self.assertAlmostEqual(r["scores"]["sound"], 50 + 45 - 10 / 3 * 0.71, places=1)   # 1.71 a minute: past (1, 45)
        self.write_bench(xrunlog="unused", xruns_audible="")   # the plugin never saw the game's sound
        self.assertNotIn("sound_dropouts", self.do()[0]["numbers"])

    def write_geometry(self, *rows):
        (self.run_dir / "shots").mkdir(exist_ok=True)
        (self.run_dir / "shots" / "geometry.tsv").write_text(
            "at_s\tx\ty\tw\th\tfullscreen\tscreen_w\tscreen_h\n" + "".join("\t".join(map(str, r)) + "\n" for r in rows))

    def test_display_from_geometry(self):
        n = self.do()[0]["numbers"]
        self.assertNotIn("display_score", n)       # an old run: no geometry, not measured
        self.write_geometry((60, 0, 0, 1920, 1080, 1, 1920, 1080), (120, 0, 0, 1920, 1080, 1, 1920, 1080),
                            (180, "-", "-", "-", "-", "-", 1920, 1080), (240, 0, 0, 640, 480, 0, 1920, 1080))
        self.write_bench(render_res="320x240")
        r, _ = self.do()
        n = r["numbers"]
        self.assertEqual((n["display_score"], n["display_detail"], n["display_samples"]), (100.0, "fullscreen 1920x1080", 3))
        self.assertEqual(r["scores"]["display"], 100.0)
        self.assertEqual(n["render_res"], "320x240")    # recorded, never scored
        self.write_geometry((60, 0, 0, 640, 480, 0, 1920, 1080))
        n = self.do()[0]["numbers"]
        self.assertEqual((n["display_score"], n["display_detail"]), (10.0, "a 640x480 window on 1920x1080"))
        self.write_geometry((60, 0, 0, 0, 0, "x", 1920, 1080))     # garbage: no sample
        self.assertNotIn("display_score", self.do()[0]["numbers"])

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
            self.ir.import_run(self.run_dir, "ex", self.content)
        self.write_bench(temp_stop="1")
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content)
        self.write_bench()
        with self.assertRaises(self.ir.Refused):      # a game that is not in the catalog
            self.ir.import_run(self.run_dir, "nope", self.content)

    def test_capture_run_needs_a_review(self):
        self.write_bench(window="", shots="30", shot="30s:shots/shot-30s.png:100", shot_t="30s:30.0:1:2")
        (self.run_dir / "shots").mkdir()
        (self.run_dir / "shots" / "shot-30s.png").write_bytes(b"png")
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content)

    def test_launcher(self):
        self.assertNotIn("launcher", self.do()[0])           # none recorded, none given: no key
        self.write_bench(launcher="OpenArena (GL 2.0)", launcher_file="openarena-gl2.desktop")
        self.assertEqual(self.do()[0]["launcher"], "OpenArena (GL 2.0)")

    def test_launcher_option_for_old_runs(self):
        self.ir.import_run(self.run_dir, "ex", self.content, False, "OpenArena (GL 1.2)")
        out = next((self.content / "ex" / check.SESSIONS).iterdir())
        self.assertEqual(tomllib.loads((out / "result.toml").read_text())["launcher"], "OpenArena (GL 1.2)")

    def test_launcher_option_must_match_the_recorded_one(self):
        self.write_bench(launcher="OpenArena (GL 2.0)")
        self.ir.import_run(self.run_dir, "ex", self.content, False, "OpenArena (GL 2.0)")   # same: fine
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content, False, "OpenArena (GL 1.2)")

    def test_pick_shots(self):
        flat = lambda v: bytes([v]) * (32 * 18)
        half = lambda a, b: bytes([a]) * (32 * 9) + bytes([b]) * (32 * 9)
        pics = {60: half(200, 40), 120: flat(5), 180: flat(120), 240: flat(121), 300: flat(122),
                360: half(40, 200), 420: flat(240)}
        items = sorted((p, "shot-%ds.png" % p) for p in pics)
        look = lambda f: pics[int(f[5:-5])]
        got = self.ir.pick_shots(items, look=look)
        # the black frame and the near-duplicates (180/240/300 are one screen) go; four left, in time order
        self.assertEqual([p for p, _ in got], [60, 180, 360, 420])
        self.assertEqual([p for p, _ in self.ir.pick_shots(items, n=2, look=look)], [60, 360])
        self.assertEqual([p for p, _ in self.ir.pick_shots(items, keep=[240, 420], look=look)], [240, 420])

    def test_dropouts_at_screenshots_are_the_recorders(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            (d / "bench.txt").write_text("shot_t=60s:60.30:1:2\nshot_t=120s:120.05:3:4\n")
            (d / "xruns.tsv").write_text("at_s\tgap_ms\tbefore_db\tafter_db\taudible\n"
                                         "28.4\t82.6\t-22\t-23\t1\n"      # the game's (a level load)
                                         "60.1\t40.0\t-20\t-20\t1\n"      # 0.2 s before a capture
                                         "61.9\t30.0\t-20\t-20\t1\n"      # 1.6 s after it
                                         "62.8\t30.0\t-20\t-20\t1\n"      # 2.5 s after: the game's
                                         "120.5\t5.0\t-20\t-20\t0\n")     # not audible: not counted
            self.assertEqual(self.ir.recorder_dropouts(d, 4), (2, 2))
            (d / "bench.txt").write_text("")
            self.assertEqual(self.ir.recorder_dropouts(d, 4), (4, 0))     # no screenshots: as counted

    def test_toml_writer_roundtrip(self):
        text = self.ir.toml_dump({"a": 1, "b": "x\"y", "c": True, "n": None},
                                 {"t": {"f": 1.5, "l": ["p", "q"], "bad key": 2, "z": None}})
        self.assertEqual(tomllib.loads(text), {"a": 1, "b": "x\"y", "c": True, "t": {"f": 1.5, "l": ["p", "q"], "bad key": 2}})
        self.assertTrue(text.startswith("# generated"))


def fixture_game(d):
    """OpenArena's catalog entry in d with one benchmark test (testdata/demo-result.toml, a 2026-10-20
    run): the test folder, for the tests that need a rated game"""
    g = Path(d) / "openarena"
    shutil.copytree(check.CONTENT / "openarena", g, ignore=shutil.ignore_patterns("rating"))
    t = g / check.SESSIONS / "2026-10-20-demo"
    t.mkdir(parents=True)
    shutil.copy(Path(__file__).parent / "testdata" / "demo-result.toml", t / "result.toml")
    return g, t


class Catalog(unittest.TestCase):
    def test_patch_subjects(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "packages" / "patched" / "games" / "pkg" / "patches"
            p.mkdir(parents=True)
            (p.parent / "package.conf").write_text("TYPE=debsrc\n")
            (p / "series").write_text("0001-a.patch\n0002-b.patch\n")
            (p / "0001-a.patch").write_text("From: x\nSubject: [PATCH 1/2] first thing\n and its tail\nDate: y\n\n---\n")
            (p / "0002-b.patch").write_text("Subject: second\n")
            self.assertEqual([s for _, s in catalog.patch_subjects("pkg", Path(t))], ["first thing and its tail", "second"])
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
            g, old = fixture_game(d)
            new = g / check.SESSIONS / "2026-11-01-capture"
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

    def test_sessions_merged_only_when_measured_alike(self):
        logged = {"sound_dropouts": 3}
        ts = [{"id": "a", "date": "2026-10-23", "kind": "session"}, {"id": "c", "date": "2026-10-24", "kind": "session", "numbers": logged},
              {"id": "b", "date": "2026-10-23", "kind": "session"}, {"id": "d", "date": "2026-10-25", "kind": "session", "numbers": logged},
              {"id": "p", "date": "2026-10-20", "kind": "capture"}]
        # the newest (d) was measured with the dropout logger: merged with c, not with a and b
        self.assertEqual([t["id"] for t in catalog.rated_tests(ts)], ["c", "d", "p"])
        self.assertEqual([t["id"] for t in catalog.rated_tests(ts[:3])], ["c"])
        self.assertEqual(catalog.rated_tests([]), [])

    def test_no_medal_without_frame_rate_and_smoothness(self):
        sess = {"kind": "session", "play_seconds": 600, "date": "2026-10-25", "id": "s",
                "numbers": {"sound_dropouts_per_min": 0.5, "sound_effects": "yes", "music": "yes"}}
        r = score.combine([sess])
        self.assertEqual(r["medal"], "Untested")
        self.assertEqual(r["reason"], "frame rate and smoothness not measured")
        crashed = dict(sess, crashed=True)
        self.assertEqual(score.combine([crashed])["medal"], "Scrap")     # a crash still floors it

    def test_measured_frame_rate_ranks_first(self):
        def r(medal, sc, frame):
            return {"graded_by": "session", "medal": medal, "score": sc, "aspects": {"frame": {"score": frame}}}
        # a software renderer's Gold 90 without a frame rate doesn't beat a measured Gold 83
        self.assertGreater(catalog.rank(r("Gold", 83, 74)), catalog.rank(r("Gold", 90, None)))
        self.assertGreater(catalog.rank(r("Gold", 90, 80)), catalog.rank(r("Gold", 83, 74)))

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
            g, old = fixture_game(d)
            txt = re.sub(r'(?m)^launcher = .*\n', "", (old / "result.toml").read_text())
            (old / "result.toml").write_text(txt.replace('kind = "demo"', 'kind = "demo"\nlauncher = "OpenArena (GL 2.0)"'))
            fast = g / check.SESSIONS / "2026-10-21-demo"
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
        cat = catalog.build(check.CONTENT, REPO)
        self.assertEqual(cat["schema"], 1)
        self.assertEqual(cat["content_date"], max(t["date"] for g in cat["games"] for t in g["tests"]))
        self.assertEqual(len(cat["revision"]), 16)
        again = catalog.build(check.CONTENT, REPO)
        self.assertEqual(again["revision"], cat["revision"])     # content only, not the clock
        self.assertEqual(catalog.diff(cat, again), [])          # "generated" is left out
        moved = json.loads(json.dumps(again))
        moved["games"][0]["title"] = "Other"
        moved["games"].append({"id": "zz-new"})
        self.assertEqual(catalog.diff(cat, moved), ['games[%s].title: %s -> "Other"' % (
            cat["games"][0]["id"], json.dumps(cat["games"][0]["title"])), 'games[zz-new]: (none) -> {"id": "zz-new"}'])
        for g in cat["games"]:                                   # shots: paths inside the game's folder
            for t in g["tests"]:
                for rel in t["ps3"]:
                    self.assertTrue(rel.startswith(check.SESSIONS + "/" + t["id"] + "/"), rel)
                    self.assertTrue((check.CONTENT / g["id"] / rel).is_file(), rel)
        ids = [g["id"] for g in cat["games"]]
        self.assertTrue({"openarena", "supertuxkart", "warzone2100", "xmoto"} <= set(ids))
        for g in cat["games"]:
            json.dumps(g)
            if g["id"] not in ("openarena", "supertuxkart", "warzone2100"):
                continue
            self.assertTrue(g["patched"])
            self.assertEqual(g["debs"], [])        # no --deb-index: no links


# ---- rules versions and data version 2 ---------------------------------------------------------------

def strip_to(x, ref):
    """x with only the keys ref has (recursively): new fields added next to the old ones don't count"""
    if isinstance(ref, dict) and isinstance(x, dict):
        return {k: strip_to(x.get(k), v) for k, v in ref.items()}
    if isinstance(ref, list) and isinstance(x, list) and len(x) == len(ref):
        return [strip_to(i, r) for i, r in zip(x, ref)]
    return x


class RulesV1Unchanged(unittest.TestCase):
    """the snapshot of every rating made under rules 1.0 (testdata/v1-ratings.json, catalog.json of 2026-10-26
    before rules 2): a game whose tests are still those comes out exactly the same, launchers, history and
    badges included"""

    def test_snapshot(self):
        snap = json.loads((HERE / "testdata" / "v1-ratings.json").read_text())
        cat = {g["id"]: g for g in catalog.build(check.CONTENT, REPO)["games"]}
        same = 0
        for gid, want in snap.items():
            g = cat.get(gid)
            if g is None or sorted(t["id"] for t in g["tests"]) != want["tests"]:
                continue    # a game tested again since: its rating moved on purpose
            same += 1
            self.assertEqual(strip_to(g["launchers"], want["launchers"]), want["launchers"], gid)
            # the "made for keyboard and mouse" badge comes from the front matter (designed_for), not a rating
            self.assertEqual([b for b in g["badges"] if b != "keyboard-mouse"], want["badges"], gid)
            for l in g["launchers"]:
                self.assertEqual((l["rating"]["rules"], l["rating"]["rules_data"]), ("1.0", 1), gid)
        self.assertGreater(same, 5)

    def test_absent_rules_is_1(self):
        self.assertEqual(score.data_version({}), 1)
        self.assertEqual(score.data_version({"rules": "2.0"}), 2)
        self.assertEqual(score.data_version({"rules": "2.3"}), 2)
        self.assertEqual(score.rules_label(1), "rules 1.0 (2026-10-24)")


def v2_session(i="s", date="2026-10-27", **over):
    """a data-version-2 session as import-run.py writes it (raw numbers and answers)"""
    t = {"id": i, "date": date, "kind": "session", "rules": "2.0", "play_seconds": 540.0, "end": "quit",
         "crashed": False,
         "numbers": {"median_fps": 60.0, "low1_fps": 50.0, "stutter_s_per_min": 0.05, "longest_ms": 80.0,
                     "sound_dropouts_per_min": 0.0, "sound_effects": "yes", "music": "yes", "load_s": 15.0,
                     "geometry": ["0 0 1920 1080 1 1920 1080"] * 3, "pointer_samples": 3, "pointer_visible": 0,
                     "desktop_back_s": 2.0, "input_log": True, "kb_readable": True, "pads_present": 1,
                     "pad_opened": True, "pad_events_play": 900, "kb_presses": 0},
         "form": {"glitches": "none", "pad_menus": "worked", "pad_play": "seamless", "keyboard_mouse": "na",
                  "menus_modes": "yes", "saves_load": "na"},
         "review": {}, "picture": {"verdict": "ok"}}
    for k, v in over.items():
        if k in ("numbers", "form", "review"):
            t[k] = dict(t[k], **v)
        else:
            t[k] = v
    return t


class RulesV2(unittest.TestCase):
    def test_weights(self):
        self.assertEqual(sum(score.WEIGHTS_V2.values()), 100)
        self.assertEqual(sum(score.WEIGHTS.values()), 100)
        self.assertEqual(score.ASPECTS[2][:5], ["frame", "smooth", "sound", "picture", "stability"])

    def test_picture_deductions(self):
        p = score.picture_v2
        self.assertEqual(p(None, None), (None, []))
        self.assertEqual(p([], None)[0], 100)                     # reviewed, nothing found
        self.assertEqual(p([("missing", "ps3", "the HUD")], None)[0], 75)
        # each category once per launcher; two categories add up; floor 0
        self.assertEqual(p([("missing", "ps3", "HUD"), ("missing", "pc", "shadows"), ("flicker", "ps3", "")], None)[0], 60)
        self.assertEqual(p([("garbage", "ps3", ""), ("missing", "ps3", ""), ("colours", "pc", "")], None)[0], 0)
        self.assertEqual(p([("softer", "ps3", "DXT")], None)[0], 95)
        self.assertEqual(p([("lighting", "ps3", "")], None)[0], 90)
        # the player's answer: broken -25 unless a defect already takes 25 or more; minor -10 unless covered
        self.assertEqual(p([], "broken")[0], 75)
        self.assertEqual(p([("missing", "ps3", "")], "broken")[0], 75)
        self.assertEqual(p([("lighting", "ps3", "")], "broken")[0], 65)
        self.assertEqual(p([("lighting", "ps3", "")], "minor")[0], 90)
        self.assertEqual(p([("softer", "ps3", "")], "minor")[0], 85)
        self.assertEqual(p([], "none")[0], 100)
        _, found = p([("missing", "pc", "the HUD"), ("missing", "ps3", "a shadow")], None)
        self.assertEqual(found[0]["origin"], "also on the PC and PS3/Walnut only")
        self.assertEqual(found[0]["what"], "the HUD; a shadow")
        self.assertEqual(score.parse_defects(["colors pc: the sky", "nope ps3: x"]), [("colours", "pc", "the sky")])

    def test_display_deductions(self):
        t = v2_session()
        self.assertEqual(score.display_v2([t])[:2], (100.0, "fullscreen 1920x1080"))
        t = v2_session(numbers={"pointer_visible": 2})                   # 2 of 3 shots: visible
        self.assertEqual(score.display_v2([t])[0], 90)
        t = v2_session(numbers={"pointer_visible": 3}, review={"pointer": "hidden"})   # the reviewer overrides it
        self.assertEqual(score.display_v2([t])[0], 100)
        self.assertEqual(score.display_v2([v2_session(review={"text_px": 14})])[0], 85)
        self.assertEqual(score.display_v2([v2_session(review={"text_px": 18})])[0], 100)
        self.assertEqual(score.display_v2([v2_session(review={"stretched": "yes"})])[0], 70)
        # measured hints never score: bars, a render size
        t = v2_session(numbers={"render_res": "640x480", "bars": "pillarbox", "bars_content": "1440x1080"})
        self.assertEqual(score.display_v2([t])[0], 100)
        t = v2_session(numbers={"geometry": ["0 0 1280 720 0 1920 1080", "0 0 1920 1080 1 1920 1080"]})
        self.assertEqual(score.display_v2([t])[0], 10)          # the lower middle: a small window (as shipped)
        self.assertEqual(score.display_v2([v2_session(numbers={"geometry": []})])[0], None)

    def test_checks_from_the_form_and_the_log(self):
        c = score.measured_checks(v2_session())
        self.assertEqual(c, {"pad_setup": "yes", "pad_play": "yes", "pad_menus": "yes", "no_keyboard": "yes",
                             "menus_modes": "yes"})
        c = score.measured_checks(v2_session(form={"pad_play": "setup", "pad_menus": "partly"}))
        self.assertEqual((c["pad_play"], c["pad_setup"], c["pad_menus"]), ("yes", "no", "partly"))
        c = score.measured_checks(v2_session(form={"pad_play": "didnt"}))
        self.assertEqual((c["pad_play"], c["pad_setup"]), ("no", "no"))
        self.assertNotIn("no_keyboard", c)                      # not playing with the pad: not a question
        # the form untried: the log tells whether the game opened the pad the player used
        c = score.measured_checks(v2_session(form={"pad_play": "na"}, numbers={"pad_opened": False}))
        self.assertEqual(c["pad_setup"], "no")
        # many keys pressed while playing with the pad: something needs the keyboard; a stray key or two doesn't
        self.assertEqual(score.measured_checks(v2_session(numbers={"kb_presses": 9}))["no_keyboard"], "no")
        self.assertEqual(score.measured_checks(v2_session(numbers={"kb_presses": 2}))["no_keyboard"], "yes")
        # the keyboard unreadable (no input group yet): not tested, never "no"
        self.assertNotIn("no_keyboard", score.measured_checks(v2_session(numbers={"kb_readable": False, "kb_presses": 50})))
        # nothing tried: nothing tested
        c = score.measured_checks(v2_session(numbers={"pads_present": 0, "pad_events_play": 0},
                                             form={"pad_play": "na", "pad_menus": "na"}))
        self.assertEqual(c, {"menus_modes": "yes"})
        self.assertEqual(score.measured_checks(v2_session(form={"keyboard_mouse": "some"}))["keyboard_mouse"], "no")
        self.assertEqual(score.measured_checks(v2_session(numbers={"pads_hotplugged": 1, "pads_hotplug_opened": 1}))
                         ["pad_hotplug"], "yes")

    def test_checklists_untested_never_count(self):
        checks = score.checks_of([v2_session()])
        s, failed, passed, n = score.checklist(checks, score.CONTROLS, "pad_setup", score.NO_PAD_CAP)
        self.assertEqual((s, failed, n), (100.0, [], 4))         # hotplug, extra pads, keyboard: not tested
        self.assertEqual(score.checklist({}, score.FEATURES)[0], None)
        setup = score.checks_of([v2_session(form={"pad_play": "setup"})])
        s, failed, _, _ = score.checklist(setup, score.CONTROLS, "pad_setup", score.NO_PAD_CAP)
        self.assertEqual(s, 30)                                     # 3 of 4 would be 75: capped
        self.assertEqual(failed[0]["item"], "pad_setup")
        partly = score.checks_of([v2_session(form={"pad_menus": "partly"})])
        self.assertEqual(score.checklist(partly, score.CONTROLS)[0], 87.5)    # partly counts half
        # the reviewer's lines win and carry where the problem comes from; newest answer wins
        t = v2_session(form={"saves_load": "yes"}, review={"broken": ["saves_load pc: the save menu crashes"],
                                                            "checks": ["online no"]})
        s, failed, passed, n = score.checklist(score.checks_of([t]), score.FEATURES)
        self.assertEqual((passed, n), (1, 3))
        self.assertEqual(failed[0], {"item": "saves_load", "name": "saves load", "answer": "no",
                                     "origin": "also on the PC", "what": "the save menu crashes"})
        old, new = v2_session("a", "2026-10-27", form={"saves_load": "no"}), v2_session("b", "2026-10-28", form={"saves_load": "yes"})
        self.assertEqual(score.checks_of([new, old])["saves_load"][0], "yes")
        self.assertEqual(score.controller_badge(score.checks_of([v2_session()])), "full")
        self.assertEqual(score.controller_badge(setup), "partial")
        self.assertEqual(score.controller_badge(score.checks_of([v2_session(form={"pad_play": "didnt"})])), "keyboard")
        self.assertIsNone(score.controller_badge({}))

    def test_designed_for_keyboard_and_mouse(self):
        # an RTS whose pad doesn't work: Controls scores the keyboard and mouse, the pad makes the badge
        t = v2_session(form={"pad_play": "didnt", "pad_menus": "didnt", "keyboard_mouse": "worked"})
        r = score.combine([t], rules=2, designed_for="keyboard-mouse")
        self.assertEqual((r["aspects"]["controls"], r["controller"]), (100, "keyboard"))
        self.assertLess(score.combine([t], rules=2)["aspects"]["controls"], 30)    # the same as a pad game
        t = v2_session(form={"pad_play": "didnt", "keyboard_mouse": "na"})
        self.assertIsNone(score.combine([t], rules=2, designed_for="keyboard-mouse")["aspects"]["controls"])

    def test_stability_counts_unclean_quits(self):
        self.assertIsNone(score.end_failure(v2_session()))
        self.assertEqual(score.end_failure(v2_session(end="crash")), "crash")
        self.assertEqual(score.end_failure(v2_session(end="closed")), "hang")    # the player had to kill it
        self.assertEqual(score.end_failure(v2_session(numbers={"desktop_back_s": 14.0})), "hang")
        self.assertEqual(score.end_failure(v2_session(numbers={"desktop_stuck": True})), "hang")
        self.assertIsNone(score.end_failure(v2_session(end="killed")))           # the harness's stop
        r = score.combine([v2_session(end="closed")], rules=2)
        self.assertEqual((r["crashes"], r["hangs"]), (0, 1))
        self.assertAlmostEqual(r["aspects"]["stability"], 55)     # one in (at least) an hour
        self.assertNotEqual(r["medal"], "Scrap")                  # a hang isn't broken

    def test_load_adds_the_level_load(self):
        r = score.combine([v2_session(numbers={"load_s": 15.0, "load_level_s": 15.0})], rules=2)
        self.assertEqual(r["aspects"]["load"], 75)                # 30 s in all
        self.assertIn("+ 15 s from the last input", r["details"]["load"])
        self.assertIn("not measured", score.combine([v2_session()], rules=2)["details"]["load"])

    def test_no_weakest_link_only_broken(self):
        r = score.combine([v2_session()], rules=2)
        self.assertEqual((r["rules"], r["medal"], r["broken"]), (2, "Platinum", []))  # 9 min is enough: the label says so
        # one weak but not broken aspect keeps the mean's medal (rules 1 would cap it)
        t = v2_session(review={"defects": ["missing ps3: the HUD", "colours ps3: the sky"]}, form={"pad_play": "setup"})
        r = score.combine([t], rules=2)
        self.assertEqual(r["aspects"]["picture"], 50)
        self.assertEqual((r["medal"], r["capped"]), (score.medal_of(r["score"]), False))
        want = sum(score.WEIGHTS_V2[k] * v for k, v in r["aspects"].items() if v is not None) / \
            sum(score.WEIGHTS_V2[k] for k, v in r["aspects"].items() if v is not None)
        self.assertAlmostEqual(r["score"], want)
        # a 400 fps game with garbage textures is Scrap, with the mean beside it and the reason
        fast = v2_session(numbers={"median_fps": 400.0, "low1_fps": 300.0},
                          review={"defects": ["garbage ps3: the terrain"]})
        r = score.combine([fast], rules=2)
        self.assertEqual(r["medal"], "Scrap")
        self.assertGreater(r["score"], 80)
        self.assertEqual(r["reason"], "picture broken (garbage textures or geometry)")
        for over, word in ((dict(form={"glitches": "broken"}), "picture"), (dict(numbers={"sound_effects": "no"}), "sound"),
                           (dict(numbers={"snd_stall": 1}), "sound"), (dict(numbers={"median_fps": 18.0, "low1_fps": 12.0}), "frame rate"),
                           (dict(numbers={"longest_ms": 6000.0}), "smoothness"),
                           (dict(numbers={"stutter_s_per_min": 5.0}), "smoothness")):
            r = score.combine([v2_session(**over)], rules=2)
            self.assertEqual(r["medal"], "Scrap", over)
            self.assertTrue(r["reason"].startswith(word + " broken"), (over, r["reason"]))
        # one crash in two short sessions: under an hour between crashes (one in one session is the old floor)
        r = score.combine([v2_session("a", end="crash"), v2_session("b")], rules=2)
        self.assertEqual((r["medal"], r["reason"]), ("Scrap", "stability broken (1 crash in 0.3 h)"))
        self.assertEqual(score.combine([v2_session(end="crash")], rules=2)["reason"], "crashes")
        # not broken: no music alone, a game capped at 18 fps holding its cap
        self.assertNotEqual(score.combine([v2_session(numbers={"music": "no"})], rules=2)["medal"], "Scrap")
        capped = v2_session(numbers={"median_fps": 18.0, "low1_fps": 17.0})
        self.assertEqual(score.combine([capped], fps_cap=18, rules=2)["broken"], [])
        # no frame times: no medal, as in rules 1
        t = v2_session(numbers={"median_fps": None, "low1_fps": None})
        self.assertEqual(score.combine([t], rules=2)["medal"], "Untested")
        # rules 1 keeps its weakest link
        self.assertEqual(score.rate({"frame": 100, "smooth": 100, "sound": 0})["medal"], "Bronze")


def v1_session(i="v1", date="2026-10-25", fps=40.0):
    return {"id": i, "date": date, "kind": "session", "play_seconds": 600.0, "end": "quit",
            "numbers": {"median_fps": fps, "low1_fps": fps - 10, "stutter_s_per_min": 0.1, "longest_ms": 100.0},
            "versions": {"xmoto": "1.0", "walnut": "0.1"}}


class RulesSelection(unittest.TestCase):
    def test_newest_data_version_rates_the_launcher(self):
        fm = {"packages": ["xmoto"]}
        v1 = v1_session()
        rating, hist = catalog.rate_launcher([v1], fm)
        self.assertEqual((rating["rules"], rating["order"]), ("1.0", score.ASPECTS[1]))
        self.assertEqual(rating["aspects"]["frame"]["score"], round(score.frame_score(40, 30)))
        v2 = dict(v2_session(), versions={"xmoto": "1.0", "walnut": "0.1"})
        rating, hist = catalog.rate_launcher([v1, v2], fm)
        self.assertEqual((rating["rules"], rating["rules_data"]), ("2.0", 2))
        self.assertEqual(rating["aspects"]["frame"]["score"], 100)     # the v1 session isn't merged in
        self.assertEqual(rating["basis"], {"tests": 1, "kind": "session", "minutes": 9})
        self.assertEqual(rating["hours"], 0.15)                       # its play alone
        self.assertEqual(list(rating["aspects"]), score.ASPECTS[2])
        # one version tested under both rules: a row for each, the newest rules first
        self.assertEqual([(h["rules"], h["tests"]) for h in hist], [("2.0", 1), ("1.0", 1)])
        # a newer Walnut release under rules 2, the old one under rules 1: History shows both
        v2b = dict(v2, versions={"xmoto": "1.0", "walnut": "0.2"}, date="2026-11-01")
        _, hist = catalog.rate_launcher([v1, v2b], fm)
        self.assertEqual([(h["walnut"], h["rules"]) for h in hist], [("0.2", "2.0"), ("0.1", "1.0")])
        # a version-2 capture doesn't move a version-1 launcher off rules 1 (graded tests decide)
        cap = {"id": "c", "date": "2026-10-28", "kind": "capture", "rules": "2.0", "picture": {"verdict": "ok"},
               "review": {}, "versions": {"walnut": "0.1"}}
        self.assertEqual(catalog.rate_launcher([v1, cap], fm)[0]["rules"], "1.0")

    def test_v2_badge_from_checks(self):
        with tempfile.TemporaryDirectory() as d:
            g, old = fixture_game(d)
            new = g / check.SESSIONS / "2026-10-27-session"
            new.mkdir()
            t = v2_session()
            (new / "result.toml").write_text(load_import_run().toml_dump(
                {"schema": 1, "rules": "2.0", "kind": "session", "date": t["date"], "game": "openarena",
                 "launcher": "OpenArena (GL 1.2)", "play_seconds": 540.0, "end": "quit", "crashed": False},
                {"numbers": dict(t["numbers"], sound_faults_per_min=0.0), "form": t["form"], "review": {},
                 "picture": {"verdict": "ok"}, "versions": {"openarena": "0.8.8-1", "walnut": "0.1"}}))
            self.assertEqual([e for e in check.check_game(g, set(), REPO) if "2026-10-27" in e], [])
            out = catalog.build_game(g, REPO, {})
            gl12 = next(l for l in out["launchers"] if l["name"] == "OpenArena (GL 1.2)")
            self.assertEqual(gl12["rating"]["rules"], "2.0")
            self.assertEqual(next(x for x in out["tests"] if x["id"] == "2026-10-27-session")["rules"], "2.0")
            if out["rating"]["launcher"] == "OpenArena (GL 1.2)":
                self.assertIn("controller-full", out["badges"])


REVIEW_OK = ("verdict: ok\nstretched: no\ntext_px: 24: the HUD\npointer: hidden\ngarbage: no\nmissing: no\n"
             "colours: no\nflicker: no\nlighting: no\nsofter: no\nbroken: none\n")


class ImporterV2(Importer):
    """a session recorded with the second form: data version 2, raw numbers, the form and the review mapped"""

    def form(self, **over):
        f = {"form_version": 2, "sound": "yes", "music": "none", "glitches": "minor", "pad_menus": "worked",
             "pad_play": "seamless", "keyboard_mouse": "na", "menus_modes": "no", "saves_load": "na",
             "controls": "full", "notes": "x"}
        f.update(over)
        (self.run_dir / "session.toml").write_text("".join("%s = %s\n" % (k, json.dumps(v)) for k, v in f.items()))

    def test_old_form_stays_rules_1(self):
        (self.run_dir / "session.toml").write_text('sound = "yes"\nmusic = "yes"\ncontrols = "full"\nnotes = ""\n')
        r, _ = self.do()
        self.assertEqual(r["rules"], "1.0")
        self.assertIn("scores", r)
        self.assertNotIn("form", r)

    def test_form_to_numbers(self):
        self.form()
        self.write_bench(desktop_back_s="3.5", fresh_home="2", fresh_home_new="1", recovered="0",
                         play_from="mark", play_start="40")
        self.write_geometry((60, 0, 0, 1920, 1080, 1, 1920, 1080), (120, 0, 0, 1920, 1080, 1, 1920, 1080))
        geo = self.run_dir / "shots" / "geometry.tsv"
        rows = geo.read_text().splitlines()
        geo.write_text("\n".join([rows[0] + "\tby\tcursor\tcursor_name", rows[1] + "\tpid\t1\tleft_ptr",
                                  rows[2] + "\tpid\t0\tleft_ptr"]) + "\n")
        (self.run_dir / "input.tsv").write_text(
            "# inputlog readable=pad,keyboard,mouse unreadable=-\nt_s\twhat\tdev\tclass\tn\n"
            "0.3\tpresent\tevent3\tpad\tSony PLAYSTATION(R)3 Controller\n0.3\tpresent\tevent1\tkeyboard\tkbd\n"
            "4.0\topened\tevent3\tpad\t-\n10\tpress\t-\tkeyboard\t4\n"
            "28.250\tkey\tevent3\tpad\t-\n31.500\tkey\tevent3\tpad\t-\n41.000\tkey\tevent3\tpad\t-\n"
            "30\tpress\t-\tpad\t25\n40\tmove\t-\tpad\t300\n"
            "65.2\tadded\tevent9\tpad\tSony PLAYSTATION(R)3 Controller\n")
        (self.run_dir / "REVIEW.md").write_text(REVIEW_OK.replace("verdict: ok", "verdict: wrong").replace(
            "missing: no", "missing: ps3: the HUD's ammo counter").replace("text_px: 24: the HUD", "text_px: 15: the chat")
            .replace("broken: none", "broken: menus_modes pc: the editor doesn't open"))
        r, _ = self.do()
        n, f, rv = r["numbers"], r["form"], r["review"]
        self.assertEqual(r["rules"], "2.0")
        self.assertNotIn("scores", r)                        # scored at catalog time
        self.assertEqual(n["sound_effects"], "yes")
        self.assertEqual(f, {"glitches": "minor", "pad_menus": "worked", "pad_play": "seamless", "keyboard_mouse": "na",
                             "menus_modes": "no", "saves_load": "na"})
        self.assertEqual(rv["defects"], ["missing ps3: the HUD's ammo counter"])
        self.assertEqual((rv["text_px"], rv["text_where"], rv["broken"]), (15, "the chat", ["menus_modes pc: the editor doesn't open"]))
        self.assertEqual((rv["stretched"], rv["pointer"]), ("no", "hidden"))
        self.assertEqual(n["geometry"], ["0 0 1920 1080 1 1920 1080"] * 2)
        self.assertNotIn("display_score", n)
        self.assertEqual((n["pointer_samples"], n["pointer_visible"]), (2, 1))
        self.assertEqual((n["desktop_back_s"], n["fresh_home"], n["fresh_home_new"]), (3.5, 2, 1))
        self.assertEqual((n["pads_present"], n["pad_opened"], n["pad_events_play"], n["kb_presses"], n["kb_readable"]),
                         (2, True, 300, 4, True))           # pad buckets from the mark on
        self.assertEqual((n["pads_hotplugged"], n["pads_hotplug_opened"]), (1, 0))
        self.assertEqual(n["load_level_s"], 8.5)                # the last press before the mark (31.5) to it (40)
        # and the numbers become the checks and the scores
        rr = score.combine([dict(r, id="t", kind="session")], rules=2)
        self.assertEqual((rr["checks"]["pad_setup"], rr["checks"]["pad_menus"]), ("yes", "yes"))
        self.assertEqual(rr["checks"]["no_keyboard"], "no")          # 4 presses while playing with the pad
        self.assertEqual(rr["checks"]["pad_hotplug"], "no")
        self.assertEqual(rr["checks"]["menus_modes"], "no")
        self.assertEqual(rr["aspects"]["picture"], 75)            # missing -25; "minor" is covered
        self.assertEqual(rr["aspects"]["display"], 100 - 15)      # text 15 px; the pointer: the reviewer says hidden
        self.assertEqual(rr["aspects"]["features"], 0)            # menus_modes no; nothing else tested
        self.assertEqual(rr["features_failed"][0]["origin"], "also on the PC")

    def test_review_questionnaire(self):
        self.form()
        (self.run_dir / "REVIEW.md").write_text(REVIEW_OK)
        self.assertEqual(self.do()[0]["review"], {"text_px": 24, "text_where": "the HUD", "pointer": "hidden",
                                                  "stretched": "no"})
        for review in (REVIEW_OK.replace("stretched: no", "stretched: ?"),            # unanswered
                       REVIEW_OK.replace("flicker: no\n", ""),                         # missing
                       REVIEW_OK.replace("missing: no", "missing: yes: the HUD"),       # no origin
                       REVIEW_OK.replace("verdict: ok", "verdict: wrong"),              # wrong, no defect
                       REVIEW_OK.replace("text_px: 24: the HUD", "text_px: big"),
                       REVIEW_OK.replace("broken: none", "broken: flying ps3: x"),
                       REVIEW_OK + "defect: weird ps3: x\n", REVIEW_OK + "check: flying yes\n"):
            (self.run_dir / "REVIEW.md").write_text(review)
            with self.assertRaises(self.ir.Refused, msg=review):
                self.ir.import_run(self.run_dir, "ex", self.content)
        (self.run_dir / "REVIEW.md").write_text(REVIEW_OK.replace("text_px: 24: the HUD", "text_px: none") +
                                                "defect: colours pc: the sky\ncheck: online no\n")
        rv = self.do()[0]["review"]
        self.assertEqual((rv["text_px_none"], rv["defects"], rv["checks"]), (True, ["colours pc: the sky"], ["online no"]))

    def test_rules_option(self):
        (self.run_dir / "REVIEW.md").write_text(REVIEW_OK)
        self.ir.import_run(self.run_dir, "ex", self.content, rules="2.0")
        out = next((self.content / "ex" / check.SESSIONS).iterdir())
        self.assertEqual(tomllib.loads((out / "result.toml").read_text())["rules"], "2.0")
        with self.assertRaises(self.ir.Refused):
            self.ir.import_run(self.run_dir, "ex", self.content, rules="9.0")

    def test_bars(self):
        w, h = 192, 108
        def pic(left, top=0):
            return bytes(0 if x < left or x >= w - left or y < top or y >= h - top else 150
                         for y in range(h) for x in range(w))
        self.assertEqual(self.ir.bars_of(pic(24))[0], "pillarbox")      # 4:3 in 16:9: 24 of 192 each side
        self.assertEqual(self.ir.bars_of(pic(0, 10))[0], "letterbox")
        self.assertEqual(self.ir.bars_of(pic(0))[0], None)
        pics = {"a": pic(24), "b": pic(24), "c": pic(0)}
        out = self.ir.bars(["a", "b", "c"], (1920, 1080), look=lambda p: pics[p])
        self.assertEqual(out, {"lit_shots": 3, "bars_shots": 2, "bars": "pillarbox", "bars_content": "1440x1080"})

    def test_review_template(self):
        spec = importlib.util.spec_from_file_location("review", HERE / "review.py")
        rv = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rv)
        text = rv.template(self.run_dir, {"bars": "no black bars", "pointer_words": "visible in 0 of 3 shots",
                                          "pointer": "hidden"})
        # every question the importer needs is in it, unanswered but the measured pointer
        (self.run_dir / "REVIEW.md").write_text(text)
        self.form()
        with self.assertRaises(self.ir.Refused) as e:
            self.ir.import_run(self.run_dir, "ex", self.content)
        self.assertIn("verdict, stretched, text_px, garbage, missing, colours, flicker, lighting, softer, broken",
                      str(e.exception))
        answered = re.sub(r"(?m)^(garbage|missing|colours|flicker|lighting|softer): \?$", r"\1: no", text)
        answered = answered.replace("verdict: ?", "verdict: ok").replace("stretched: ?", "stretched: no") \
            .replace("text_px: ?", "text_px: 20: menu").replace("broken: ?", "broken: none")
        (self.run_dir / "REVIEW.md").write_text(answered)
        self.assertEqual(self.do()[0]["review"]["text_px"], 20)


def load_bench(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), REPO / "bench" / (name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Recorders(unittest.TestCase):
    ir = load_import_run()
    """the session recorders of data version 2: inputlog.py, confwatch.py, geom.py's pointer, the form"""

    def test_inputlog_classes_and_counts(self):
        il = load_bench("inputlog")
        with tempfile.TemporaryDirectory() as t:
            def dev(name, key=(), rel=(), props=()):
                d = Path(t) / name / "device" / "capabilities"
                d.mkdir(parents=True)
                for f, bitsset in (("key", key), ("rel", rel), ("abs", ())):
                    n = sum(1 << b for b in bitsset)
                    words = []
                    while True:
                        words.append("%x" % (n & ((1 << il.LONG_BITS) - 1)))
                        n >>= il.LONG_BITS
                        if not n:
                            break
                    (d / f).write_text(" ".join(reversed(words)) + "\n")
                (d.parent / "properties").write_text("%x\n" % sum(1 << b for b in props))
                return str(d.parent)
            self.assertEqual(il.classify(dev("event0", key=(0x130, 0x131))), "pad")
            self.assertEqual(il.classify(dev("event1", key=(16, 30, 44, 28))), "keyboard")
            self.assertEqual(il.classify(dev("event2", key=(0x110,), rel=(0, 1))), "mouse")
            self.assertEqual(il.classify(dev("event3", props=(6,))), "motion")
            self.assertEqual(il.classify(dev("event4", key=(116,))), "other")      # the power button
        c, times = {"press": 0, "move": 0}, []
        data = b"".join(il.EV.pack(100, usec, typ, code, val) for usec, typ, code, val in
                        ((250000, 1, 0x130, 1), (300000, 1, 0x130, 0), (0, 0, 0, 0), (0, 3, 0, 128), (0, 3, 1, 120),
                         (0, 1, 30, 2)))
        il.count(data + b"xx", c, times)     # a partial event at the end is left for the next read
        self.assertEqual(c, {"press": 1, "move": 2})
        self.assertEqual(times, [100.25])    # the press at the kernel's own time
        self.assertEqual(il.event_of("/dev/input/event5"), "event5")
        self.assertIsNone(il.event_of("/dev/input/mice"))

    def test_inputlog_screenshot_chords(self):
        il = load_bench("inputlog")
        ev = lambda *rows: b"".join(il.EV.pack(int(t), round(t % 1 * 1e6), 1, code, val) for t, code, val in rows)
        # Alt+F12 on a keyboard (either Alt), F12 alone is nothing, Alt released first is nothing
        c = il.Chords()
        self.assertEqual(c.feed(ev((10.0, 88, 1), (10.1, 88, 0))), [])
        self.assertEqual(c.feed(ev((11.0, 56, 1), (11.2, 88, 1), (11.3, 88, 0), (11.4, 56, 0))), [11.2])
        self.assertEqual(c.feed(ev((12.0, 100, 1), (12.1, 88, 1))), [12.1])
        c = il.Chords()
        self.assertEqual(c.feed(ev((13.0, 56, 1), (13.1, 56, 0), (13.2, 88, 1))), [])
        # Select+Start on a pad, within 0.3 s, in either order; slower is just two presses
        c = il.Chords()
        sel, st = il.BTN_SELECT, il.BTN_START
        self.assertEqual(c.feed(ev((20.0, sel, 1), (20.2, st, 1))), [20.2])
        self.assertEqual(c.feed(ev((20.5, sel, 0), (20.6, st, 0), (21.0, st, 1), (21.1, sel, 1))), [21.1])
        c = il.Chords()
        self.assertEqual(c.feed(ev((30.0, sel, 1), (30.5, st, 1))), [])
        self.assertEqual(c.feed(ev((31.0, 0x130, 1), (31.1, 30, 1))), [])      # other keys: nothing, and not remembered
        self.assertEqual(c.down.keys() <= {sel, st}, True)
        # the log: a mark row, no key code, a request line; the second chord within 2 s is dropped
        with tempfile.TemporaryDirectory() as t:
            req = Path(t) / "req"
            out = io.StringIO()
            log = il.Log(100.0, out, request=str(req))
            self.assertTrue(log.mark(105.0, "event3", "pad"))
            self.assertFalse(log.mark(106.5, "event3", "pad"))
            self.assertTrue(log.mark(107.5, "event4", "keyboard"))
            self.assertEqual(out.getvalue().splitlines(), ["5.000\tmark\tevent3\tpad\t-", "7.500\tmark\tevent4\tkeyboard\t-"])
            self.assertEqual(req.read_text().split(), ["105.000000", "107.500000"])

    def test_player_marks_in_the_gallery_and_review(self):
        shots = load_bench("shots")
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            (d / "shots").mkdir()
            lines = ["game=ex", "variant=base", "shots=60 120", "shot=60s:shots/shot-60s.png:300", "shot_t=60s:60.5:1:2",
                     "shot=120s:shots/shot-120s.png:300", "shot_t=120s:120.5:1:2"]
            for k in (60, 120):
                (d / "shots" / ("shot-%ds.png" % k)).write_bytes(b"x")
            for tt in ("31.4", "75.2", "200.0"):
                (d / "shots" / ("mark-%ss.png" % tt)).write_bytes(b"x")
                lines.append("shot_mark=%ss:shots/mark-%ss.png:410" % (tt, tt))
            lines.append("shot_mark=300.0s:-:410")      # a capture that failed: no picture
            (d / "bench.txt").write_text("\n".join(lines) + "\n")
            sh = shots.parse(d)
            look = lambda f: bytes([sum(map(ord, Path(f).name)) % 200 + 20]) * (32 * 9) + bytes([sum(map(ord, Path(f).name)) * 7 % 200 + 20]) * (32 * 9)
            self.assertEqual(sorted(sh["marks"]), [31.4, 75.2, 200.0, 300.0])
            self.assertEqual(sh["marks"][31.4]["file"], "shots/mark-31.4s.png")
            self.assertIsNone(sh["marks"][300.0]["file"])
            self.assertEqual(sorted(sh["shots"]), [60, 120])         # the timed ones apart
            # the importer: all marks first, then timed ones up to four in all
            names = [self.ir.shot_name(p) for p, _ in self.ir.pick_all(d, sh, look=look)]
            self.assertEqual(names[:3], ["ps3-mark-31_4s.jpg", "ps3-mark-75_2s.jpg", "ps3-mark-200_0s.jpg"])
            self.assertEqual(len(names), 4)
            self.assertEqual(self.ir.shot_name(60), "ps3-60s.jpg")
            for tt in range(4):      # more marks than the gallery holds: all of them, up to six, and no timed ones
                (d / "shots" / ("mark-9%d.0s.png" % tt)).write_bytes(b"x")
                lines.append("shot_mark=9%d.0s:shots/mark-9%d.0s.png:410" % (tt, tt))
            (d / "bench.txt").write_text("\n".join(lines) + "\n")
            got = self.ir.pick_all(d, shots.parse(d), look=look)
            self.assertEqual(len(got), 6)
            self.assertTrue(all(isinstance(p, float) for p, _ in got))
            # the dropouts around a marked shot are the recorder's, as around a timed one
            (d / "xruns.tsv").write_text("at_s\tgap_ms\tbefore_db\tafter_db\taudible\n"
                                         "31.9\t40.0\t-20\t-20\t1\n"      # 0.5 s after the mark at 31.4
                                         "40.0\t40.0\t-20\t-20\t1\n")     # the game's
            (d / "bench.txt").write_text("shot_mark=31.4s:shots/mark-31.4s.png:410\n")
            self.assertEqual(self.ir.recorder_dropouts(d, 2), (1, 1))

    def test_freshhome_lossless(self):
        fh = load_bench("freshhome")
        with tempfile.TemporaryDirectory() as t:
            home, run, state = Path(t) / "home", Path(t) / "run", Path(t) / "state"
            run.mkdir()
            (home / ".openarena" / "baseoa").mkdir(parents=True)
            (home / ".openarena" / "baseoa" / "q3config.cfg").write_text('seta name "me"\n')
            world = home / ".local" / "share" / "warzone2100-4"
            world.mkdir(parents=True)
            (world / "big.wz").write_bytes(b"x" * (6 * 1024 * 1024))
            ino = (world / "big.wz").stat().st_ino
            out = fh.aside(str(state), "g", [".openarena", ".local/share/warzone2100*", ".config/new", "../outside"],
                           home=str(home))
            plan, lines = out[0], out[1:]
            self.assertIn("fresh_home=2", lines)
            self.assertTrue(any("not under the home" in x for x in lines))
            self.assertFalse((home / ".openarena").exists())
            self.assertTrue(any(x.name.startswith(".openarena.walnut-aside-") for x in home.iterdir()))
            # the game runs on fresh defaults: it makes its own, a big one too
            (home / ".openarena" / "baseoa").mkdir(parents=True)
            (home / ".openarena" / "baseoa" / "q3config.cfg").write_text('seta name "UnnamedPlayer"\n')
            (home / ".config" / "new").mkdir(parents=True)
            (home / ".config" / "new" / "c").write_text("fresh")
            world.mkdir()
            (world / "fresh.bin").write_bytes(b"y" * (6 * 1024 * 1024))     # over the evidence cap
            self.assertEqual(fh.restore(plan, str(run)), ["fresh_home_new=3"])
            # the player's own back, the very same files (a rename: the same inode, nothing copied)
            self.assertEqual((home / ".openarena" / "baseoa" / "q3config.cfg").read_text(), 'seta name "me"\n')
            self.assertEqual((world / "big.wz").stat().st_ino, ino)
            self.assertFalse((home / ".config" / "new").exists())
            # the session's own: small ones in the run directory, the big one kept beside the original
            self.assertEqual(sorted(x.name for x in (run / "fresh-home").iterdir()), [".config%new", ".openarena"])
            fresh = [x for x in world.parent.iterdir() if ".walnut-fresh-" in x.name]
            self.assertEqual(len(fresh), 1)
            self.assertTrue((fresh[0] / "fresh.bin").exists())
            self.assertTrue(Path(plan + ".done").exists())

    def test_freshhome_recovers_a_killed_session(self):
        fh = load_bench("freshhome")
        with tempfile.TemporaryDirectory() as t:
            home, state = Path(t) / "home", Path(t) / "state"
            (home / ".neverball").mkdir(parents=True)
            (home / ".neverball" / "neverballrc").write_text("mine")
            fh.aside(str(state), "neverball", [".neverball"], home=str(home))
            (home / ".neverball").mkdir()                   # the game's fresh one; then the session is killed
            (home / ".neverball" / "neverballrc").write_text("fresh")
            with contextlib.redirect_stderr(io.StringIO()) as err:
                out = fh.aside(str(state), "other", [".other"], home=str(home))     # the next session starts
            self.assertIn("recovered=1", out)
            self.assertIn("didn't restore", err.getvalue())
            self.assertEqual((home / ".neverball" / "neverballrc").read_text(), "mine")
            kept = [x for x in home.iterdir() if ".walnut-fresh-" in x.name]
            self.assertEqual(len(kept), 1)                  # the fresh one kept, never deleted
            # an aside name already taken: that directory is left alone, nothing overwritten
            st = fh.stamp()
            (home / (".neverball.walnut-aside-" + st)).mkdir()
            out = fh.aside(str(state), "neverball", [".neverball"], home=str(home))
            if fh.stamp() == st:
                self.assertIn("fresh_home=0", out)
                self.assertTrue((home / ".neverball").exists())

    def test_pointer_columns(self):
        geom = load_geom()
        self.assertEqual(geom.cursor_cols((0, 0, 1920, 1080, 1, "pid"), (960, 540, True, "left_ptr")), ("1", "left_ptr"))
        self.assertEqual(geom.cursor_cols((0, 0, 800, 600, 0, "pid"), (960, 540, True, "left_ptr")), ("0", "left_ptr"))
        self.assertEqual(geom.cursor_cols((0, 0, 1920, 1080, 1, "pid"), (960, 540, False, "-")), ("0", "-"))
        self.assertEqual(geom.cursor_cols(None, (1, 1, True, "-")), ("-", "-"))
        self.assertEqual(geom.cursor_cols((0, 0, 9, 9, 0, "pid"), None), ("-", "-"))

    def test_form_writes_the_second_form(self):
        with tempfile.TemporaryDirectory() as t:
            env = dict(os.environ, WALNUT_FORM_AUTO="sound=partly,music=none,glitches=broken,pad_play=setup,"
                                                    "saves_load=no,notes=a\\nb")
            env.pop("DISPLAY", None)
            import subprocess
            p = subprocess.run([sys.executable, "-I", str(REPO / "bench" / "session-form.py"), t, "g", "base"],
                               env=env, capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stderr)
            f = tomllib.loads((Path(t) / "session.toml").read_text())
            self.assertEqual((f["form_version"], f["sound"], f["glitches"], f["pad_play"], f["pad_menus"],
                              f["keyboard_mouse"], f["saves_load"], f["controls"], f["notes"]),
                             (2, "partly", "broken", "setup", "na", "na", "no", "partial", "a\nb"))


if __name__ == "__main__":
    unittest.main()
