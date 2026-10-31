#!/usr/bin/env python3
"""test_schema.py: tools/gamedb/schema.py and gamedb/schemas/ (run-tests.sh suite gamedb-schema).

Every kind gets a good fixture that validates and bad ones that must be refused: an unknown key, a wrong
type, a missing required key, a value outside its enum, a missing required section (the Markdown kinds that
have one), and a file of no known kind. Then the real tree: every gamedb file validates and is of exactly
one kind, and the key sets the PS3 side keeps for itself (playdef.py, gamedata.py) equal the schemas'.
"""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import schema  # noqa: E402

FM = "+++\n%s\n+++\n%s"
GOOD = {
    "index": ("gamedb/games/g/index.md", FM % ('id = "g"\ntitle = "G"\nsummary = "s"\nlicense = "GPL"\n'
                                              'homepage = "https://x.org"\ntags = ["fps"]\ncontroller = "full"', "Notes.\n")),
    "engine": ("gamedb/engines/e/engine.md", FM % ('id = "e"\ntitle = "E"\nhomepage = "https://e.org"\nlicense = "GPL"\n'
                                                   'games = ["g"]', "A note.\n")),
    "build": ("gamedb/games/g/build.toml", 'packages = ["g-game"]\nsuite = "free"\nneeds_data = false\n'
                                           '[data_setup]\ngdp = "g"\nrequired = []\noptional = ["g-data"]\n'
                                           'editions = { "g-data" = "full" }\n'),
    "engine-build": ("gamedb/engines/e/build.toml", 'packages = ["e-engine"]\n'),
    "technical": ("gamedb/games/g/technical.md", "# G: technical notes\n\n## Status\n\nTried.\n\n## Research\n\n- x\n"),
    "investigation": ("gamedb/games/g/investigations/2026-10-01-x.md",
                      FM % ('id = "x"\ngames = ["g"]\nbatches = ["b1"]', "# A story\n\n## Symptom\n\nIt broke.\n")),
    "guide": ("gamedb/games/g/guides/game-data.md", "# Setting up the game data\n\nCopy the files.\n"),
    "research": ("gamedb/research/survey.md", "# A survey\n\nText.\n"),
    "upstream": ("gamedb/games/g/upstream.md", "# G: a fix\n\nStatus: draft.\n\n## GitHub issue (g/g)\n\nx\n\n"
                                               "## Pull request\n\nx\n\n## Submission steps\n\nx\n"),
    "review": ("gamedb/games/g/rating/sessions/s/review.md", "# REVIEW.md\nverdict: ok: fine\nstretched: no\n"
                                                             "text_px: 12: menus\npointer: hidden\nmissing: no\n"
                                                             "missing: ps3: the HUD\nbroken: none\n"),
    "patch": ("gamedb/patches/p/patch.md", FM % ('component = "mesa"\npatches = ["patches/0001-x.patch"]\n'
                                                 'category = "stability"\nswitch = "none"\ndefault = "always on"\n'
                                                 'gating = "is_ps3"\nforwarded = "no"\nbatches = ["b1"]',
                                                 "# A fix\n\nText.\n\n## Investigation\n\nx\n\n## Validation\n\nx\n")),
    "test": ("gamedb/games/g/rating/test.toml", 'process = "g"\nhome = [".g"]\nsound_at = 20\n'),
    "form": ("gamedb/games/g/rating/sessions/s/form.toml", 'form_version = 2\nsound = "yes"\nmusic = "none"\n'
                                                           'notes = "fine"\nform = "tk"\ngame = "g"\nvariant = "base"\n'),
    "result": ("gamedb/games/g/rating/sessions/s/result.toml",
               'schema = 1\nrules = "2.0"\nkind = "session"\ndate = "2026-10-01"\nrun = "r"\ngame = "g"\n'
               'variant = "base"\nlauncher = "G"\nplay_seconds = 10.0\nend = "quit"\ncrashed = false\n'
               'started = true\nreached_play = true\nrender = "1920x1080"\n[numbers]\nmedian_fps = 60.0\n'
               '[versions]\nkernel = "6.12"\n[shots]\nps3 = ["shots/ps3-60s.jpg"]\n'),
    "todo": ("gamedb/games/g/todo.toml", '[[item]]\nkind = "session"\ntext = "play"\nadded = 2026-10-01\norder = 1\n'),
    "analysis": ("gamedb/games/g/benchmark/analysis.toml", '[reload]\nfixed = 2.0\n[[log_ignore]]\nre = "x"\nwhy = "noise"\n'),
    "engine-analysis": ("gamedb/engines/e/analysis.toml", '[[log_strip]]\nre = "x"\nwhy = "colour codes"\n'),
    "results": ("gamedb/games/g/benchmark/results/b1.toml", 'batch = "b1"\n[variant.base]\nfps = 30.0\n'),
    "batches": ("gamedb/benchmarks/batches.toml", '[[batch]]\nname = "b1"\norder = 1\nkind = "run"\ntitle = "t"\n'
                                                  'runs = 2\nsource = "x"\ndate = 2026-10-01\n'),
    "metrics": ("gamedb/benchmarks/metrics.toml", '[[metric]]\nname = "fps"\nunit = "fps"\nbetter = "higher"\n'
                                                  'matrix = "always"\ntool = "fps"\n'),
    "caveats": ("gamedb/benchmarks/caveats.toml", '[[caveat]]\nbatch = "b1"\nkind = "invalid"\nwhy = "black screen"\n'),
    "tags": ("gamedb/tags.toml", '[[group]]\nid = "genre"\nlabel = "Genre"\n[[group.tag]]\nid = "fps"\nlabel = "FPS"\n'
                                 '[[group.tag]]\nid = "gamepad"\nlabel = "Pad"\nautomatic = true\n'),
    "launcher": ("gamedb/games/g/desktop/launchers/g.desktop",
                 "[Desktop Entry]\nType=Application\nName=G\nComment=c\nExec=ps3-game /usr/games/g\nIcon=g\n"
                 "Terminal=false\nCategories=Game;ActionGame;\nNoDisplay=true\n"),
    "package-conf": ("gamedb/games/g/packages/g/package.conf", "# ours\nTYPE=debsrc\nSOURCE=g\nVERSION=1-1\nBUILD=native\n"),
    "migrate-home": ("gamedb/games/g/desktop/migrate-home.txt", "# list\n.g/cfg /usr/share/g/cfg\n.g/other\n"),
    "scummvm-ini": ("gamedb/games/g/desktop/files/usr/share/ps3-games/scummvm/g.ini",
                    "# defaults\n[scummvm]\nfullscreen=true\noutput_rate=22050\n\n[g]\ngameid=g\nengineid=scumm\n"),
    "game-cfg": ("gamedb/games/g/desktop/files/usr/lib/g/base/autoexec.cfg", '// defaults\nseta r_mode "-2"\nbind X "+attack"\n'),
    "interfered": ("gamedb/benchmarks/interfered.txt", "# runs\nps3-bench-20261017T181626Z-g-base game.log: a key press\n"),
    "schema": ("gamedb/schemas/x.toml", 'kind = "x"\ntitle = "X"\npaths = ["gamedb/x"]\nformat = "toml"\n'
                                        '[keys.a]\ntype = "str"\nmeaning = "a"\n'),
}
# kind: [(old, new, expected problem text)]: unknown key, wrong type, missing required, bad enum, section
BAD = {
    "index": [('controller = "full"', 'controller = "full"\ncolour = "red"', "colour: unknown key"),
              ('title = "G"', 'title = 3', "title: should be a string"),
              ('summary = "s"\n', '', "summary: missing"),
              ('controller = "full"', 'controller = "wireless"', "'wireless' is not one of"),
              ('tags = ["fps"]', 'tags = ["fun"]', "'fun' is no name of gamedb/tags.toml"),
              ('tags = ["fps"]', 'tags = ["gamepad"]', "'gamepad' is no name")],
    "engine": [('games = ["g"]', 'games = ["g"]\nsize = 1', "size: unknown key"),
               ('title = "E"\n', '', "title: missing")],
    "build": [('suite = "free"', 'suite = "free"\nsize = 1', "size: unknown key"),
              ('needs_data = false', 'needs_data = "no"', "needs_data: should be true or false"),
              ('suite = "free"\n', '', "suite: missing"),
              ('suite = "free"', 'suite = "paid"', "'paid' is not one of"),
              ('"full"', '"gold"', "data_setup.editions.g-data: 'gold' is not one of"),
              ('gdp = "g"\n', '', "data_setup.gdp: missing")],
    "engine-build": [('packages', 'pkgs', "pkgs: unknown key")],
    "technical": [("# G: technical notes", "# G", "not the title line"),
                  ("## Status\n\nTried.\n\n## Research", "## Research\n\nx\n\n## Status", "comes after")],
    "investigation": [('batches = ["b1"]', 'batches = ["b1"]\nfix = "x"', "fix: unknown key"),
                      ('batches = ["b1"]', 'batches = "b1"', "batches: should be a list"),
                      ('id = "x"\n', '', "id: missing"),
                      ('batches = ["b1"]', 'batches = ["b9"]', "'b9' is no name of gamedb/benchmarks/batches.toml")],
    "guide": [("# Setting up the game data\n", "Setting up\n", "not the title line"),
              ("\nCopy the files.\n", "", "a title and nothing else")],
    "research": [("# A survey", "+++\nx = 1\n+++\n# A survey", "this kind has none")],
    "upstream": [("## Pull request\n\nx\n\n", "", "no section 'Pull request'"),
                 ("## Pull request\n\nx\n\n## Submission steps\n\nx\n", "## Submission steps\n\nx\n\n## Pull request\n\nx\n",
                  "comes after")],
    "review": [("broken: none", "broken: none\nshiny: yes", "unknown key 'shiny'"),
               ("verdict: ok: fine\n", "", "verdict: missing"),
               ("pointer: hidden", "pointer: maybe", "does not match"),
               ("pointer: hidden", "pointer: hidden\npointer: visible", "pointer twice")],
    "patch": [('forwarded = "no"', 'forwarded = "no"\nowner = "x"', "owner: unknown key"),
              ('category = "stability"', 'category = "crash"', "'crash' is not one of"),
              ('component = "mesa"\n', '', "component: missing"),
              ("## Validation\n\nx\n", "", "no section 'Validation'"),
              ('patches = ["patches/0001-x.patch"]', 'patches = "x"', "patches: should be a list"),
              ('gating = "is_ps3"\n', '', "gating: missing")],
    "test": [('sound_at = 20', 'sound_at = "20"', "sound_at: should be an integer"),
             ('process = "g"\n', '', "process: missing"),
             ('sound_at = 20', 'sound_at = 20\nlaunch = 1', "launch: unknown key")],
    "form": [('music = "none"', 'music = "loud"', "'loud' is not one of"),
             ('notes = "fine"\n', '', "notes: missing"),
             ('form = "tk"', 'form = "tk"\nmood = "ok"', "mood: unknown key")],
    "result": [('median_fps = 60.0', 'median_fps = 60.0\nfun = 1', "numbers.fun: unknown key"),
               ('crashed = false', 'crashed = "no"', "crashed: should be true or false"),
               ('kind = "session"', 'kind = "party"', "'party' is not one of"),
               ('run = "r"\n', '', "run: missing"),
               ('render = "1920x1080"', 'render = "1920x1080"\ntester = "me"', "tester: only where source")],
    "todo": [('order = 1', 'order = 0', "less than 1"),
             ('kind = "session"', 'kind = "chore"', "'chore' is not one of"),
             ('added = 2026-10-01\n', '', "item[0].added: missing"),
             ('kind = "session"', 'kind = "fix"', "item[0].order: only where kind = 'session'"),
             ('added = 2026-10-01', 'added = "2026-10-01"', "added: should be a date")],
    "analysis": [('fixed = 2.0', 'fixed = 2.0\nevery = 3', "reload.every: unknown key"),
                 ('why = "noise"\n', '', "log_ignore[0].why: missing")],
    "engine-analysis": [('[[log_strip]]', '[reload]\nfixed = 1\n[[log_strip]]', "reload: unknown key")],
    "results": [('fps = 30.0', 'fps = 30.0\nframes = 3', "variant.base.frames: unknown key (no name of"),
                ('fps = 30.0', 'fps = "30"', "variant.base.fps: should be a number"),
                ('batch = "b1"', 'batch = "b9"', "'b9' is no name")],
    "batches": [('runs = 2', 'runs = 2\nmood = "x"', "batch[0].mood: unknown key"),
                ('kind = "run"', 'kind = "rum"', "'rum' is not one of"),
                ('order = 1\n', '', "batch[0].order: missing"),
                ('date = 2026-10-01', 'date = "2026-10-01"', "date: should be a date")],
    "metrics": [('better = "higher"', 'better = "more"', "'more' is not one of"),
                ('tool = "fps"\n', '', "tool: missing")],
    "caveats": [('kind = "invalid"', 'kind = "bad"', "'bad' is not one of"),
                ('why = "black screen"', 'why = ""', "why: empty")],
    "tags": [('label = "FPS"', 'label = "FPS"\ncolour = "red"', "group[0].tag[0].colour: unknown key"),
             ('label = "Genre"\n', '', "group[0].label: missing")],
    "launcher": [("NoDisplay=true", "NoDisplay=yes", "is not true or false"),
                 ("Icon=g\n", "", "Icon: missing"),
                 ("NoDisplay=true", "NoDisplay=true\nColour=red", "unknown key 'Colour'"),
                 ("Type=Application", "Type=Link", "'Link' is not one of"),
                 ("[Desktop Entry]", "[Desktop Thing]", "unknown group")],
    "package-conf": [("BUILD=native", "BUILD=native\nMOOD=1", "MOOD: unknown key"),
                     ("TYPE=debsrc", "TYPE=svn", "'svn' is not one of"),
                     ("SOURCE=g\n", "", "SOURCE: missing")],
    "migrate-home": [(".g/other", "/etc/g", "does not match"), (".g/other", "../g", "does not match")],
    "scummvm-ini": [("output_rate=22050", "output_rate=fast", "is not an integer"),
                    ("engineid=scumm\n", "", "[g] engineid: missing"),
                    ("fullscreen=true", "fullscreen=true\nshader=crt", "unknown key 'shader'")],
    "game-cfg": [('bind X "+attack"', 'exec x.cfg', "does not match")],
    "interfered": [("ps3-bench-", "run-", "does not match")],
    "schema": [('format = "toml"', 'format = "yaml"', "format 'yaml'"),
               ('type = "str"', 'type = "text"', "type 'text'"),
               ('meaning = "a"\n', '', "no meaning")],
}
REFS = {
    "gamedb/tags.toml": GOOD["tags"][1],
    "gamedb/benchmarks/batches.toml": GOOD["batches"][1],
    "gamedb/benchmarks/metrics.toml": GOOD["metrics"][1],
    "gamedb/games/g/index.md": GOOD["index"][1],
}


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


class Fixtures(unittest.TestCase):
    """each kind's good fixture validates, each bad one is refused with its problem"""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for rel, text in REFS.items():
            self.write(rel, text)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, rel, text):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        return p

    def problems(self, kind, text):
        rel = GOOD[kind][0]
        p = self.write(rel, text)
        self.assertEqual(schema.kind_of(rel), kind, rel)
        return schema.validate(p, root=self.root)

    def test_every_kind_has_fixtures(self):
        self.assertEqual(sorted(GOOD), sorted(schema.load()))
        self.assertEqual(sorted(BAD), sorted(schema.load()))

    def test_good(self):
        for kind, (rel, text) in GOOD.items():
            with self.subTest(kind=kind):
                self.assertEqual(self.problems(kind, text), [])
                for r, t in REFS.items():
                    self.write(r, t)

    def test_bad(self):
        for kind, cases in BAD.items():
            for old, new, want in cases:
                with self.subTest(kind=kind, change=new[:30]):
                    text = GOOD[kind][1]
                    self.assertIn(old, text)
                    probs = self.problems(kind, text.replace(old, new, 1))
                    self.assertTrue(any(want in p for p in probs), "%s: want %r in %s" % (kind, want, probs))
                    for r, t in REFS.items():
                        self.write(r, t)

    def test_draft_state(self):
        """a draft needs only id, title and status, and its build.toml none of packages, suite, needs_data"""
        draft = FM % ('id = "g"\ntitle = "G"\ndraft = true\nstatus = "candidate"', "")
        self.assertEqual(self.problems("index", draft), [])
        self.assertIn("status: missing", self.problems("index", draft.replace('status = "candidate"\n', "")))
        self.write("gamedb/games/g/index.md", draft)
        self.assertEqual(self.problems("build", 'engine = "e"\n'), [])
        self.write("gamedb/games/g/index.md", GOOD["index"][1])
        self.assertIn("packages: missing", self.problems("build", 'engine = "e"\n'))

    def test_stability_only_sections(self):
        """Investigation and Validation are required of a stability entry only"""
        text = GOOD["patch"][1].replace("## Investigation\n\nx\n\n## Validation\n\nx\n", "")
        self.assertIn("body: no section 'Investigation'", self.problems("patch", text))
        self.assertEqual(self.problems("patch", text.replace('"stability"', '"performance"')), [])

    def test_no_kind(self):
        self.assertIsNone(schema.kind_of("gamedb/games/g/notes.txt"))
        self.assertIn("no schema matches", schema.coverage(["gamedb/games/g/notes.txt"])[0])
        self.assertEqual(schema.validate(self.write("gamedb/games/g/notes.txt", "x"), root=self.root),
                         ["no schema matches it (gamedb/schemas/)"])
        # a file on the no-schema list, and a schema's match inside a no-schema folder
        self.assertEqual(schema.coverage(["gamedb/games/g/packages/g/debian/control",
                                          "gamedb/games/g/packages/g/package.conf"]), [])
        self.assertEqual(schema.kind_of("gamedb/games/g/packages/g/package.conf"), "package-conf")

    def test_broken_schema_refused(self):
        d = self.root / "schemas"
        d.mkdir()
        (d / "x.toml").write_text('kind = "y"\ntitle = "X"\npaths = ["a"]\nformat = "toml"\n'
                                  '[keys.a]\ntype = "str"\nmeaning = "a"\nenum = "@nope"\n')
        with self.assertRaises(schema.SchemaError) as e:
            schema.load(d)
        self.assertIn("kind 'y' is not the file's name", str(e.exception))
        self.assertIn("names no [enums] list", str(e.exception))


class Tree(unittest.TestCase):
    """the repository's gamedb/ against its schemas"""

    def test_all_files_valid_and_covered(self):
        files = schema.gamedb_files()
        self.assertEqual(schema.coverage(files), [])
        probs = ["%s: %s" % (f, p) for f in files for p in schema.validate(f) if not p.startswith("no schema")]
        self.assertEqual(probs, [])

    def test_targets_and_shared_inputs(self):
        t = schema.targets_and_shared_inputs()
        self.assertEqual(sorted(t), sorted(schema.load()))
        self.assertIn("gamedb/games/*/index.md", t["index"]["targets"])
        for want in ("gamedb/schemas/index.toml", "tools/gamedb/schema.py", "gamedb/tags.toml"):
            self.assertIn(want, t["index"]["shared"])
        self.assertIn("gamedb/benchmarks/metrics.toml", t["results"]["shared"])
        self.assertNotIn("gamedb/tags.toml", t["result"]["shared"])   # a session's record depends on nothing

    def test_ps3_side_key_sets(self):
        """playdef.py and gamedata.py ship to the PS3 without gamedb/: their key sets equal the schemas'"""
        playdef = load("playdef", REPO / "bench" / "harness" / "playdef.py")
        self.assertEqual({k: (t, r) for k, (t, r) in playdef.KEYS.items()}, schema.fields("test"))
        gamedata = load("gamedata", REPO / "bench" / "harness" / "gamedata.py")
        self.assertEqual(gamedata.ANALYSIS_KEYS, {k: t for k, t in schema.types("analysis").items()})
        self.assertEqual(set(gamedata.ENGINE_KEYS), set(schema.types("engine-analysis")))
        self.assertEqual({k for k, req in gamedata.INTERFERENCE_KEYS.items() if req},
                         set(schema.required("analysis", "interference")))
        self.assertEqual(set(gamedata.INTERFERENCE_KEYS), set(schema.types("analysis", "interference")))

    def test_shared_enums(self):
        score = load("score", HERE / "score.py")
        self.assertEqual(tuple(score.DESIGNED_FOR), schema.enum("index", "designed_for"))
        self.assertEqual(schema.enum("build", "licensed", "edition"), schema.enum("engine-build", "licensed", "edition"))
        self.assertEqual(schema.enum("build", "licensed", "edition"), schema.enum("build", "data_setup", "editions", "*"))

    def test_check_and_benchrecords_read_the_schemas(self):
        check = load("check", HERE / "check.py")
        self.assertEqual(check.BUILD_KEYS, schema.fields("build"))
        self.assertEqual(check.DRAFT_REQUIRED, {"id", "title", "status"})
        self.assertEqual(check.TODO_SESSION_ONLY, ("order", "launcher", "pc"))
        br = load("benchrecords", REPO / "bench" / "analysis" / "benchrecords.py")
        self.assertEqual(list(br.BATCH_KEYS)[:3], ["name", "order", "kind"])
        self.assertEqual(br.HAND_KEYS, ("tested", "conclusion", "observations"))
        self.assertEqual(br.WARN_KEYS, ("tested", "conclusion"))
        self.assertEqual(br.RUN_REQUIRED, ("order", "title", "runs"))
        self.assertEqual(set(br.ENTRY_KEYS), {"label", "order", "note", "source"})

    def test_cli(self):
        self.assertEqual(schema.main(["gamedb/tags.toml"]), 0)


if __name__ == "__main__":
    unittest.main()
