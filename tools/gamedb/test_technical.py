#!/usr/bin/env python3
"""Tests for tools/gamedb/technical.py (technical.json, the site's technical pages): python3 -I
tools/gamedb/test_technical.py. A small gamedb in a scratch directory (a published game, a draft, an
engine, a survey, benchmark records), then the repository's own."""
import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import catalog     # noqa: E402
import check       # noqa: E402
import technical   # noqa: E402
from test_gamedb import load_tool, write_game   # noqa: E402

NO_IDENT = ((), ())

TECH_MD = """# Example: technical notes

Current state. The survey: [census](../../research/survey.md#part-b), the draft
[Cand](../cand/technical.md), its investigation [inv](../cand/investigations/2026-01-01-x.md),
our [investigation](investigations/2026-01-02-slow-load.md), the [doc](../../../docs/NOTES.md),
a [gone](../../../docs/GONE.md) one, [licensed](licensed/ex-data/), [web](https://example.org/a) and
[top](#current).

Refs: gamedb:batch/b2, (gamedb:batch/b9), gamedb:ex/slow-load, gamedb:eng, gamedb:cand,
gamedb:cand/x, gamedb:nosuch, `gamedb:batch/b1` and [gamedb:batch/b1](#top).

```
[not a link](../../research/survey.md)
```
"""

INV_MD = """+++
id = "slow-load"
games = ["ex", "cand"]
driver_patches = ["mesa 0050"]
+++
# Slow load

Back to [the notes](../technical.md) and [the survey](../../../research/survey.md); gamedb:batch/b1, gamedb:ex.
"""

RECIPE = """# Example 1.0: a demo, timed.
# Steady state: passes.
SUITE=1
COMM=example
# the old renderer
# (slower)
variant_old() { ENV+=(OLD=1); }
helper() { :; }
variant_fast() { ENV+=(FAST=1); }
"""


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        self.root = self.repo / "gamedb"
        self.content = self.root / "games"
        g = write_game(self.content, build='engine = "eng"\nrecipes = ["ex:fast"]\n'
                       '[[licensed]]\nname = "ex-data"\nbuilder = "script"\n')
        (g / "technical.md").write_text(TECH_MD)
        (g / "investigations").mkdir()
        (g / "investigations" / "2026-01-02-slow-load.md").write_text(INV_MD)
        (g / "rating").mkdir()
        (g / "rating" / "test.toml").write_text('process = "example"\nhome = [".example"]\n')
        (g / "benchmark" / "results").mkdir(parents=True)
        (g / "benchmark" / "recipe.sh").write_text(RECIPE)
        (g / "benchmark" / "results" / "b1.toml").write_text('batch = "b1"\n[variant.base]\nfps = 30.0\n')
        (g / "benchmark" / "results" / "b2.toml").write_text(
            'batch = "b2"\n[variant.base]\nfps = 31.0\nvram = 40\n[variant.fast]\nfps = 35.5\n')
        (g / "benchmark" / "results" / "sum.toml").write_text(
            'batch = "sum"\n[variant.base]\nlabel = "Example"\norder = 1\nnote = "right"\nfps_steady = 33.0\n')
        (g / "packages" / "example" / "patches").mkdir(parents=True)
        (g / "packages" / "example" / "package.conf").write_text("TYPE=patched\n")
        cand = self.content / "cand"
        (cand / "investigations").mkdir(parents=True)
        (cand / "index.md").write_text('+++\nid = "cand"\ntitle = "Candidate"\ndraft = true\nstatus = "candidate"\n+++\n')
        (cand / "technical.md").write_text("# Candidate: technical notes\n\nDRAFT-ONLY text.\n")
        (cand / "investigations" / "2026-01-01-x.md").write_text('+++\nid = "x"\ngames = ["cand"]\n+++\n# X\nDRAFT-INV\n')
        e = self.root / "engines" / "eng"
        (e / "packages" / "eng" / "patches").mkdir(parents=True)
        (e / "packages" / "eng" / "package.conf").write_text("TYPE=patched\n")
        (e / "engine.md").write_text('+++\nid = "eng"\ntitle = "Eng"\nlicense = "MIT"\ngames = ["ex", "cand"]\n+++\n'
                                     "Our [package](./packages/eng/).\n")
        (e / "build.toml").write_text('packages = ["eng"]\n')
        (self.root / "research").mkdir()
        (self.root / "research" / "survey.md").write_text(
            "# A survey\n\n## Part B\n\nSee [Example](../games/ex/index.md), [Cand](../games/cand/technical.md).\n")
        (self.root / "benchmarks").mkdir()
        (self.root / "benchmarks" / "batches.toml").write_text(
            '[[batch]]\nname = "sum"\nkind = "summary"\ntitle = "Summary"\nsource = "x"\n'
            '[[batch]]\nname = "b1"\nkind = "run"\norder = 1\ntitle = "First"\ndate = 2026-01-01\nruns = 2\nsource = "x"\n'
            '[[batch]]\nname = "b2"\nkind = "run"\norder = 2\ntitle = "Second"\ndate = 2026-01-02\nruns = 3\n'
            'stack = { mesa = "ps3.1" }\nobservations = "Fine."\nsource = "x"\n')
        (self.root / "benchmarks" / "metrics.toml").write_text(
            '[[metric]]\nname = "fps"\nunit = "frames a second"\nbetter = "higher"\n'
            '[[metric]]\nname = "vram"\nunit = "MiB"\nbetter = "lower"\n'
            '[[metric]]\nname = "fps_steady"\nunit = "frames a second"\nbetter = "higher"\n')
        (self.repo / "docs").mkdir()
        (self.repo / "docs" / "NOTES.md").write_text("notes\n")
        self.cat = {"games": [{"id": "ex", "title": "Example", "rating": {"medal": "Gold", "score": 85, "graded_by": "session"},
                               "tests": [{"kind": "session"}, {"kind": "demo"}],
                               "patches": [{"source": "example", "subject": "s", "file": "0001-a.patch", "url": "u"}]}]}

    def tearDown(self):
        self.tmp.cleanup()

    def build(self):
        dirs = {"example": "gamedb/games/ex/packages/example", "eng": "gamedb/engines/eng/packages/eng"}
        subjects = {"example": [("0001-a.patch", "fix a")]}
        return technical.build(self.content, self.cat, self.repo, patch_subjects=lambda n, r: subjects.get(n, []),
                               package_dir=lambda r, n: dirs.get(n))


class Pages(Fixture):
    def test_published_games_only(self):
        t = self.build()
        self.assertEqual([g["id"] for g in t["games"]], ["ex"])
        self.assertEqual([i["url"] for i in t["investigations"]], ["games/ex/investigations/slow-load/"])
        text = json.dumps(t)
        for draft in ("DRAFT-ONLY", "DRAFT-INV", "games/cand/", "Candidate"):
            self.assertNotIn(draft, text)
        self.assertEqual([x["id"] for x in t["engines"][0]["games"]], ["ex"])
        self.assertEqual([x["id"] for x in t["investigations"][0]["facts"]["games"]], ["ex"])

    def test_game_page(self):
        g = self.build()["games"][0]
        self.assertEqual(g["url"], "games/ex/technical/")
        self.assertTrue(g["notes"].startswith("Current state."))    # the title heading is the page's
        self.assertEqual(g["engine"], {"id": "eng", "title": "Eng", "url": "engines/eng/"})
        self.assertEqual(g["build"]["licensed"], ["ex-data"])
        self.assertEqual([p["name"] for p in g["our_packages"]], ["example", "eng"])
        self.assertEqual(g["our_packages"][0]["patches"][0]["url"],
                         technical.GITHUB % ("blob", "gamedb/games/ex/packages/example/patches/0001-a.patch"))
        self.assertEqual(g["patch_count"], 1)
        self.assertEqual((g["sessions"], g["tests"]), (1, 2))
        self.assertEqual(g["test"]["process"], "example")
        self.assertEqual(g["investigations"][0]["title"], "Slow load")

    def test_links(self):
        n = self.build()["games"][0]["notes"]
        self.assertIn("[census](../../../research/survey/#part-b)", n)
        self.assertIn("the draft\nCand,", n)                              # a draft's page: text only
        self.assertIn("its investigation inv,", n)
        self.assertIn("[investigation](../investigations/slow-load/)", n)
        self.assertIn("[doc](%s)" % (technical.GITHUB % ("blob", "docs/NOTES.md")), n)
        self.assertIn("a gone one", n)
        self.assertIn(" licensed, ", n)
        self.assertIn("[web](https://example.org/a)", n)
        self.assertIn("[top](#current)", n)
        self.assertIn("[not a link](../../research/survey.md)", n)        # a code block is left as it is
        t = self.build()
        inv = t["investigations"][0]["body"]
        self.assertIn("[the notes](../../technical/)", inv)
        self.assertIn("[the survey](../../../../research/survey/)", inv)
        r = t["research"][0]
        self.assertEqual((r["slug"], r["title"], r["url"]), ("survey", "A survey", "research/survey/"))
        self.assertIn("[Example](../../games/ex/)", r["body"])
        self.assertIn("Cand.", r["body"])
        e = t["engines"][0]
        self.assertIn("[package](%s)" % (technical.GITHUB % ("tree", "gamedb/engines/eng/packages/eng")), e["body"])

    def test_references(self):
        t = self.build()
        n = t["games"][0]["notes"]
        self.assertIn("Refs: [batch b2](../test-runs/#batch-b2), (batch b9), [Slow load](../investigations/slow-load/), "
                      "[Eng](../../../engines/eng/), cand,\nx, gamedb:nosuch, `gamedb:batch/b1` and "
                      "[gamedb:batch/b1](#top).", n)                 # drafts and unknown ids: text; code: as is
        inv = t["investigations"][0]["body"]
        self.assertIn("gamedb:batch/b1", INV_MD)
        self.assertIn("[batch b1](../../test-runs/#batch-b1), [Example](../../technical/).", inv)
        (self.root / "engines" / "eng" / "technical.md").write_text("# Eng\n\nOn the engine: gamedb:batch/b2.\n")
        e = self.build()["engines"][0]
        self.assertIn("[batch b2](../../games/ex/test-runs/#batch-b2)", e["notes"])

    def test_guides(self):
        self.assertEqual(self.build()["games"][0]["guides"], [])
        gd = self.content / "ex" / "guides"
        gd.mkdir()
        (gd / "game-data.md").write_text("# Setting up the game data\n\nSee [the notes](../technical.md) and gamedb:batch/b2.\n\n"
                                         "## Which editions\n\n```\n## not a heading\n```\n")
        (gd / "a-tip.md").write_text("# A tip\n\nBack to [the data](game-data.md).\n")
        g = self.build()["games"][0]["guides"]
        self.assertEqual([(x["slug"], x["title"]) for x in g], [("a-tip", "A tip"), ("game-data", "Setting up the game data")])
        self.assertIn("Back to [the data](./)", g[0]["body"])       # a guide links to the Guides tab
        body = g[1]["body"]
        self.assertIn("[the notes](../technical/) and [batch b2](../test-runs/#batch-b2)", body)
        self.assertIn("### Which editions", body)                     # one level down, under the guide's title
        self.assertIn("```\n## not a heading\n```", body)             # code is left as it is

    def test_data_guide(self):
        self.assertIsNone(self.build()["games"][0]["data_guide"])           # no [data_setup]: none
        self.cat["games"][0]["data_setup"] = {
            "gdp": "exg", "original": "Ex Original", "required": ["ex-full", "ex-demo"], "optional": ["ex-music", "ex-extra"],
            "downloads": ["ex-full"], "fetch": ["ex-demo"], "hint": "its CD",
            "editions": {"ex-full": "full", "ex-demo": "demo", "ex-music": "music", "ex-extra": "expansion"}}
        files = {"usr/share/games/ex/a%02d.dat" % i: [{"size": 10, "md5": "aa"}] for i in range(30)}
        files["usr/share/games/ex/big.pak"] = [{"size": 3 << 20, "sha256": "bb", "release": "1.1"}]
        manifest = {"gdp_version": "89", "packages": {
            "ex-full": {"edition": "full", "version": "89", "replaces": ["ex-demo"], "required": ["usr/share/games/ex/pak0.pak"],
                        "files": {"usr/share/games/ex/pak0.pak": [{"size": 18689235, "sha256": "ab", "release": "106"},
                                                                  {"size": 5, "md5": "cd"}],
                                  "usr/share/doc/ex-full/manual.txt": [{"size": 1, "md5": "ef"}]},
                        "symlinks": {"usr/lib/ex/pak0.pak": "../../share/games/ex/pak0.pak"}},
            "ex-demo": {"required": sorted(files), "files": files},
            "ex-music": {"required": [], "files": {}, "tracks": {"first": 2, "last": 3, "max_size": 64 << 20,
                                                                  "source": "m/t%02d.ogg", "paths": ["usr/share/games/ex/m/t02.ogg"]}}}}
        (self.repo / "tools" / "gamedb").mkdir(parents=True)
        (self.repo / "tools" / "gamedb" / "gdp-manifest.json").write_text(json.dumps(manifest))
        pd = self.repo / "packages" / "gdp" / "patches"
        pd.mkdir(parents=True)
        (pd / "0001-x.patch").write_text("Subject: Add ex-extra\n\ndiff --git a/data/exg.yaml b/data/exg.yaml\n"
                                         "--- a/data/exg.yaml\n+++ b/data/exg.yaml\n@@ -1 +1,2 @@\n+  ex-extra:\n")
        (pd / "0002-y.patch").write_text("Subject: general\n\n--- a/build.py\n+++ b/build.py\n")
        (pd / "0003-z.patch").write_text("Subject: lha for Ex Original's CD\n\n--- a/build.py\n+++ b/build.py\n")
        subjects = {"game-data-packager": [("0001-x.patch", "Add ex-extra"), ("0002-y.patch", "general"),
                                           ("0003-z.patch", "lha for Ex Original's CD")]}
        t = technical.build(self.content, self.cat, self.repo, patch_subjects=lambda n, r: subjects.get(n, []),
                            package_dir=lambda r, n: {"game-data-packager": "packages/gdp"}.get(n))
        g = t["games"][0]
        self.assertEqual(g["guides"][0]["slug"], "game-data")              # the guide exists without a game-data.md
        dg = g["data_guide"]
        self.assertEqual([p["file"] for p in dg["patches"]], ["0001-x.patch", "0003-z.patch"])   # the game's own
        self.assertEqual(dg["ours"], ["ex-extra"])
        full, demo, music, extra = dg["packages"]
        self.assertEqual(full["walnut"], "walnut-data import ex /path/to/your/copy")
        self.assertEqual(full["gdp"], "game-data-packager -d . -p ex-full exg /path/to/your/copy")   # may download its patches
        self.assertEqual(demo["walnut"], "walnut-data import -p ex-demo ex")                       # fetched whole: no source
        self.assertEqual(demo["gdp"], "game-data-packager -d . -p ex-demo exg")
        self.assertEqual(extra["gdp"], "game-data-packager -d . --no-download -p ex-extra exg /path/to/your/copy")
        self.assertEqual(dg["install_deb"], "ex-full_*.deb")
        self.assertEqual(full["dir"], "/usr/share/games/ex")
        self.assertEqual(full["doc_count"], 1)
        self.assertEqual([a["bytes"] for a in full["files"][0]["alts"]], ["18,689,235", "5"])
        self.assertEqual(full["files"][0]["alts"][1]["algo"], "md5")
        self.assertEqual(full["symlinks"], [{"path": "/usr/lib/ex/pak0.pak", "target": "../../share/games/ex/pak0.pak"}])
        self.assertEqual(demo["files"], [])                                   # too many: a summary per folder
        self.assertEqual(demo["folders"], [{"dir": "/usr/share/games/ex", "count": 31, "size_h": "3.0 MB"}])
        self.assertEqual([f["name"] for f in demo["key_files"]], ["big.pak"])
        self.assertEqual(music["tracks"]["paths"], ["/usr/share/games/ex/m/t02.ogg"])
        self.assertFalse(extra["known"])                                     # not in the manifest
        self.assertEqual(dg["addons"], ["ex-music", "ex-extra"])

    def test_benchmark(self):
        b = self.build()["games"][0]["benchmark"]
        self.assertEqual([x["batch"] for x in b["tables"]], ["sum", "b2", "b1"])   # the summary, then newest first
        b2 = b["tables"][1]
        self.assertEqual([c["name"] for c in b2["columns"]], ["fps", "vram"])
        self.assertEqual(b2["columns"][1]["unit"], "MiB")
        self.assertEqual([(r["variant"], r["values"]) for r in b2["rows"]], [("base", ["31.0", "40"]), ("fast", ["35.5", ""])])
        self.assertEqual((b2["date"], b2["runs"], b2["stack"]), ("2026-01-02", 3, [{"name": "mesa", "version": "ps3.1"}]))
        self.assertEqual((b["tables"][0]["rows"][0]["label"], b["tables"][0]["rows"][0]["note"]), ("Example", "right"))
        self.assertEqual(b["header"], "Example 1.0: a demo, timed. Steady state: passes.")
        self.assertEqual([(v["name"], v["note"], v["catalog"]) for v in b["variants"]],
                         [("base", "the recipe as it is, without a variant", False),
                          ("old", "the old renderer (slower)", False), ("fast", None, True)])

    def test_no_notes(self):
        (self.content / "ex" / "technical.md").unlink()
        self.assertIsNone(self.build()["games"][0]["notes"])

    def test_scan(self):
        t = self.build()
        self.assertEqual(technical.scan(t, self.repo, NO_IDENT), [])
        t["games"][0]["notes"] += "\nlog of /home/someone/x and 192.168.1.20\n"
        errs = technical.scan(t, self.repo, NO_IDENT)
        self.assertEqual(len(errs), 2, errs)
        self.assertTrue(all(e.startswith("games[0].notes: ") for e in errs))
        (self.repo / "tools").mkdir()
        (self.repo / "tools" / "public-replace.local.txt").write_text("regex:SECRET-[0-9]+==>X\n")
        t["research"][0]["body"] += "SECRET-42"
        self.assertIn("research[0].body: a text tools/public-replace.local.txt hides",
                      technical.scan(t, self.repo, NO_IDENT))

    def test_write_refuses_what_is_not_public_safe(self):
        (self.content / "ex" / "technical.md").write_text("# Example\n\nfrom /home/someone/game\n")
        out = Path(self.tmp.name) / "technical.json"
        orig = technical.build
        technical.build = lambda c, cat, repo: orig(c, cat, repo, patch_subjects=lambda n, r: [],
                                                    package_dir=lambda r, n: None)
        try:
            with self.assertRaises(technical.TechnicalError):
                technical.write(self.content, self.cat, out, self.repo)
        finally:
            technical.build = orig
        self.assertFalse(out.exists())


class NotInTheApp(unittest.TestCase):
    def test_pack_catalog_has_no_technical_json(self):
        with tempfile.TemporaryDirectory() as d:
            content = Path(d) / "games"
            write_game(content)
            (content / "ex" / "technical.md").write_text("# Example\n\nNotes.\n")
            out = Path(d) / "catalog.tar"
            load_tool("pack-catalog").pack(content, out, Path(d))
            with tarfile.open(out) as t:
                self.assertEqual([n for n in t.getnames() if "technical" in n], [])
            self.assertNotIn("technical", catalog.build(content, Path(d)))


class Repository(unittest.TestCase):
    def test_the_repository_pages(self):
        cat = catalog.build(check.CONTENT, REPO)
        t = technical.build(check.CONTENT, cat, REPO)
        published = sorted(d.name for d in check.CONTENT.iterdir() if d.is_dir() and not check.is_draft(d))
        self.assertEqual(sorted(g["id"] for g in t["games"]), published)
        self.assertEqual(technical.scan(t, REPO), [])
        drafts = {d.name for d in check.CONTENT.iterdir() if d.is_dir() and check.is_draft(d)}
        for i in t["investigations"]:
            self.assertNotIn(i["owner"], drafts)
        self.assertTrue(t["research"])


class BenchRows(unittest.TestCase):
    """what a benchmark row says it tested (technical.py conditions, caveat_of) and the sessions' CSV"""

    def test_conditions(self):
        env = {"stream": ["XMOTO_GL_STREAM=1"]}
        self.assertEqual(technical.conditions("base", env, ["vblank_mode=0"]), ("base", []))
        self.assertEqual(technical.conditions("stream@wkkick10", env, []),
                         ("stream", ["XMOTO_GL_STREAM=1", "waker kick at 10 ms"]))
        self.assertEqual(technical.conditions("base@rec10+rsx_shadow_fast1", env, [])[1],
                         ["stall recorder at 10 ms", "switch rsx_shadow_fast1"])

    def test_caveat_most_specific(self):
        cavs = [{"batch": "b", "kind": "not-comparable", "why": "all"},
                {"batch": "b", "game": "g", "kind": "invalid", "why": "g only"}]
        self.assertEqual(technical.caveat_of(cavs, "b", "g", "base")["why"], "g only")
        self.assertEqual(technical.caveat_of(cavs, "b", "h", "base")["why"], "all")
        self.assertIsNone(technical.caveat_of(cavs, "c", "g", "base"))

    def test_sessions_csv(self):
        import sessions
        s = {"id": "2026-10-24-session", "date": "2026-10-24", "walnut": "0.1", "package": "xmoto",
             "version": "0.6.3", "launcher": "X-Moto", "rules": "1.0", "minutes": 26.2, "end": "quit",
             "crashed": False, "medal": "Platinum", "score": 96, "numbers": {"median_fps": 60.3},
             "aspects": [{"key": "frame", "name": "Frame rate", "score": 98},
                         {"key": "display", "name": "Display", "score": None}]}
        head, row = sessions.to_csv([s]).splitlines()
        self.assertIn("Frame rate,Display,median_fps", head)
        self.assertIn(",98,,60.3,", row)    # an aspect without a score is an empty cell


if __name__ == "__main__":
    unittest.main()
