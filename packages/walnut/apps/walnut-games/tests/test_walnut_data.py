#!/usr/bin/env python3
"""Tests for the game data import, on a PC without root, apt or game-data-packager:
python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_data.py (tools/run-tests.sh, suite walnut-data).

- walnut-games-helper import-data: tar streams built here (a good one, then a symlink, a hard link, a
  device, a FIFO, '../x', an absolute name, a setuid mode, an unknown file, a wrong checksum, a wrong
  size, too many bytes, a cut stream, a duplicate, a missing required file, too many generated
  files), checked against a manifest of the real one's shape; the package written (its control: no
  scripts, Conflicts/Replaces only the manifest's, X-Walnut-Edition, X-Walnut-Release from the
  alternative that matched), apt's simulation deciding. A fake dpkg-deb and apt-get (runner and stream
  functions); the staging directory, run directory and /usr are temporary directories.
- The lock (a second action refused, busy), status "importing", remove-data, install-repo-data.
- tools/gamedb/gdp-manifest.json: every entry the helper accepts.
- walnut-data: arguments, the --download choice, source kinds, mount points, a disc image through a fake
  udisksctl, the gdp command, and whole imports with a stubbed gdp, dpkg-deb, helper and systemd-run
  (the progress FIFO in the unit, Debian's gdp without --progress-fd, a gdp failure, a refusal, a deb
  that was not asked for, an image the desktop mounted first, loop-setup refused over ssh, one import
  at a time, the free-space check and its phases).
"""
import contextlib
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
import stat
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG = HERE.parent
REPO = PKG.parents[3]
sys.dont_write_bytecode = True


def load(name, path):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


helper = load("walnut_games_helper", PKG / "files/sbin/walnut-games-helper")
wd = load("walnut_data", PKG / "files/bin/walnut-data")

A, A2, B, MAN = b"a" * 3000, b"A" * 2000, b"b" * 1500, b"manual\n"
GD = "usr/share/games/alpha/"
MANIFEST = {"gdp_version": "89", "packages": {"alpha-data": {
    "game": "alpha", "version": "89", "edition": "full", "launchers": True, "symlinks": {},
    "conflicts": ["alpha-demo-data"], "replaces": ["alpha-demo-data"],
    "required": [GD + "a.dat", GD + "b.dat"],
    "files": {GD + "a.dat": [{"size": len(A), "sha256": hashlib.sha256(A).hexdigest(), "release": "cd"},
                             {"size": len(A2), "md5": hashlib.md5(A2).hexdigest(), "release": "gog"}],
              GD + "b.dat": [{"size": len(B), "sha1": hashlib.sha1(B).hexdigest()}],
              "usr/share/doc/alpha-data/manual.txt": [{"size": len(MAN), "sha256": hashlib.sha256(MAN).hexdigest()}]}}}}
GAMES = {"alpha": {"id": "alpha", "packages": ["alpha"], "data_setup": {
             "gdp": "alpha", "required": ["alpha-data"], "optional": ["alpha-music"], "downloads": [],
             "present_any": ["alpha-data", "alpha-lic-data"], "editions": {"alpha-data": "full", "alpha-music": "music"}}},
         "beta": {"id": "beta", "packages": ["beta"]}}


def tar_of(members):
    """a tar stream of (name, kind, data, mode, link) members"""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.GNU_FORMAT) as t:
        for name, kind, data, mode, link in members:
            ti = tarfile.TarInfo(name)
            ti.type, ti.mode, ti.linkname = kind, mode, link
            if kind == tarfile.REGTYPE:
                ti.size = len(data)
                t.addfile(ti, io.BytesIO(data))
            else:
                t.addfile(ti)
    return buf.getvalue()


def f(name, data, mode=0o644):
    return (name, tarfile.REGTYPE, data, mode, "")


def d(name):
    return (name, tarfile.DIRTYPE, b"", 0o755, "")


def good_members(a=A):
    return [d("./"), d("./usr/"), d("./usr/share/"), d("./usr/share/games/"), d("./" + GD),
            f("./" + GD + "a.dat", a), f("./" + GD + "b.dat", B),
            d("./usr/share/doc/"), d("./usr/share/doc/alpha-data/"),
            f("./usr/share/doc/alpha-data/manual.txt", MAN),
            f("./usr/share/doc/alpha-data/changelog.gz", b"gz"), f("./usr/share/doc/alpha-data/copyright", b"gdp's"),
            f("./usr/share/lintian/overrides/alpha-data", b"x"),
            f("./usr/share/applications/alpha-data.desktop", b"[Desktop Entry]"),
            f("./usr/share/pixmaps/alpha-data.png", b"png")]


def run_say(fn, *a, **kw):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        try:
            r = fn(*a, **kw)
        except Exception as e:   # the tests look at which one
            r = e
    return r, buf.getvalue().splitlines()


class Fake:
    """dpkg-deb, apt-get -s, systemctl and the apt stream, recording what they were asked"""

    def __init__(self, sim="Inst alpha-data (89 local-deb [all])\nConf alpha-data (89 local-deb [all])\n",
                 running=False, inst=(), policy=""):
        self.sim, self.running, self.inst, self.policy = sim, running, set(inst), policy
        self.calls, self.streams, self.tree = [], [], None

    def runner(self, cmd):
        self.calls.append(cmd)
        if cmd[0] == helper.SYSTEMCTL:
            return 0 if self.running else 3, "active\n" if self.running else "inactive\n"
        if cmd[0] == helper.DPKG_DEB:
            root, deb = cmd[-2], cmd[-1]
            self.tree = {}
            for dirpath, dirs, files in os.walk(root):
                for n in files:
                    p = os.path.join(dirpath, n)
                    self.tree[os.path.relpath(p, root)] = (stat.S_IMODE(os.lstat(p).st_mode), Path(p).read_bytes())
            Path(deb).write_bytes(b"deb")
            return 0, ""
        if cmd[0] == helper.APT_GET and "-s" in cmd:
            return 0, self.sim
        if cmd[0] == helper.DPKG_QUERY:
            return 0, "".join("%s\tii \n" % p for p in cmd[4:] if p in self.inst)
        if cmd[0] == helper.APT_CACHE and cmd[1] == "policy":
            return 0, self.policy
        raise AssertionError(cmd)

    def stream(self, args):
        self.streams.append(args)
        return 0, []


class Dirs(unittest.TestCase):
    """the helper's run, import and /usr directories in a temporary directory"""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.old = helper.RUN_DIR, helper.IMPORT_DIR, helper.USR, helper.SKIP_CAP
        helper.RUN_DIR, helper.IMPORT_DIR, helper.USR = str(t / "run"), str(t / "lib/import"), str(t)
        self.addCleanup(self.restore)

    def restore(self):
        helper.RUN_DIR, helper.IMPORT_DIR, helper.USR, helper.SKIP_CAP = self.old
        self.tmp.cleanup()

    def imp(self, data, fake=None, pkg="alpha-data", gid="alpha", manifest=None):
        fake = fake or Fake()
        man = manifest or json.loads(json.dumps(MANIFEST))["packages"]
        r, out = run_say(helper.do_import_data, gid, pkg, GAMES, io.BytesIO(data), man, "89", fake.runner,
                         fake.stream, check_owner=False)
        return r, out, fake


class ImportData(Dirs):
    def assert_clean(self):
        if os.path.isdir(helper.IMPORT_DIR):
            self.assertEqual(os.listdir(helper.IMPORT_DIR), [])     # staging removed in finally

    def test_good_stream(self):
        r, out, fake = self.imp(tar_of(good_members()))
        self.assertIsNone(r, out)
        tree = fake.tree
        self.assertEqual(sorted(tree), sorted(["DEBIAN/control", GD + "a.dat", GD + "b.dat",
                                               "usr/share/doc/alpha-data/manual.txt", "usr/share/doc/alpha-data/copyright"]))
        self.assertTrue(all(m == 0o644 for m, _ in tree.values()))
        self.assertEqual(tree[GD + "a.dat"][1], A)
        self.assertIn(b"not redistributable", tree["usr/share/doc/alpha-data/copyright"][1])
        control = tree["DEBIAN/control"][1].decode()
        fields = dict(l.split(": ", 1) for l in control.splitlines() if not l.startswith(" "))
        self.assertEqual(fields["Package"], "alpha-data")
        self.assertEqual(fields["Version"], "89")
        self.assertEqual(fields["Section"], "local/games")
        self.assertEqual(fields["Architecture"], "all")
        self.assertEqual(fields["Conflicts"], "alpha-demo-data")
        self.assertEqual(fields["Replaces"], "alpha-demo-data")
        self.assertEqual(fields["X-Walnut-Edition"], "full")
        self.assertEqual(fields["X-Walnut-Release"], "cd")
        self.assertNotIn("Depends", control)
        self.assertNotIn("Provides", control)
        deb = [c for c in fake.calls if c[0] == helper.DPKG_DEB][0]
        self.assertEqual(deb[1:4], ["--root-owner-group", "-Znone", "-b"])
        self.assertEqual(len(fake.streams), 1)
        args = fake.streams[0]
        self.assertEqual(args[:4], ["install", "--no-install-recommends", "--reinstall", "--no-remove"])
        self.assertTrue(args[-1].endswith("/alpha-data.deb"))
        self.assertIn("progress 100 installed alpha-data (full, cd)", out)
        self.assert_clean()

    def test_other_alternative_and_its_release(self):
        r, out, fake = self.imp(tar_of(good_members(A2)))
        self.assertIsNone(r, out)
        self.assertIn(b"X-Walnut-Release: gog\n", fake.tree["DEBIAN/control"][1])

    def test_no_release_names(self):
        man = json.loads(json.dumps(MANIFEST))["packages"]
        for alts in man["alpha-data"]["files"].values():
            for a in alts:
                a.pop("release", None)
        r, out, fake = self.imp(tar_of(good_members()), manifest=man)
        self.assertIsNone(r, out)
        self.assertNotIn(b"X-Walnut-Release", fake.tree["DEBIAN/control"][1])
        self.assertIn(b"X-Walnut-Edition: full\n", fake.tree["DEBIAN/control"][1])

    def test_no_files_but_the_required_ones(self):
        members = [m for m in good_members() if "manual" not in m[0] and "share/doc" not in m[0]]
        r, out, fake = self.imp(tar_of(members))
        self.assertIsNone(r, out)

    def test_setuid_mode_written_0644(self):
        members = [f(m[0], m[2], 0o4755) if m[0].endswith("b.dat") else m for m in good_members()]
        r, out, fake = self.imp(tar_of(members))
        self.assertIsNone(r, out)
        self.assertEqual(fake.tree[GD + "b.dat"][0], 0o644)

    def refused(self, members_or_data, want, **kw):
        data = members_or_data if isinstance(members_or_data, bytes) else tar_of(members_or_data)
        r, out, fake = self.imp(data, **kw)
        self.assertIsInstance(r, helper.Refused, out)
        self.assertIn(want, str(r))
        self.assertEqual(fake.streams, [])                         # nothing installed
        self.assertFalse([c for c in fake.calls if c[0] == helper.DPKG_DEB])
        self.assert_clean()
        return r

    def test_links_devices_fifos(self):
        base = good_members()
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.CHRTYPE, tarfile.BLKTYPE, tarfile.FIFOTYPE):
            with self.subTest(kind=kind):
                self.refused(base + [("./" + GD + "c.dat", kind, b"", 0o644, "/etc/shadow")], "not a regular file")
        # a link in place of a manifest file
        self.refused([m if not m[0].endswith("a.dat") else ("./" + GD + "a.dat", tarfile.SYMTYPE, b"", 0o777, "/etc/passwd")
                      for m in base], "not a regular file")

    def test_bad_names(self):
        for name in ("../x", "./usr/../../etc/x", "/usr/share/games/alpha/a.dat", "usr//share/x", "./usr/./x"):
            with self.subTest(name=name):
                self.refused(good_members() + [f(name, b"x")], "not a plain relative name")

    def test_unknown_file(self):
        self.refused(good_members() + [f("./" + GD + "extra.dat", b"x")], "is not a file of alpha-data")
        # a generated file's place of another package
        self.refused(good_members() + [f("./usr/share/doc/other/copyright", b"x")], "is not a file of")

    def test_launcher_files_only_for_the_doom_family(self):
        man = json.loads(json.dumps(MANIFEST))["packages"]
        man["alpha-data"]["launchers"] = False
        self.refused(good_members(), "alpha-data.desktop is not a file", manifest=man)

    def test_wrong_checksum_and_size(self):
        self.refused([f(m[0], b"c" * len(B)) if m[0].endswith("b.dat") else m for m in good_members()],
                     "checksum differs")
        self.refused([f(m[0], B + b"!") if m[0].endswith("b.dat") else m for m in good_members()], "wrong size")
        self.refused([f(m[0], A[:-1] + b"b") if m[0].endswith("a.dat") else m for m in good_members()], "checksum")

    def test_truncated(self):
        data = tar_of(good_members())
        for cut in (len(data) // 3, 512 * 7 + 100):
            with self.subTest(cut=cut):
                self.refused(data[:cut], "")

    def test_duplicate(self):
        self.refused(good_members() + [f("./" + GD + "b.dat", B)], "twice")
        self.refused(good_members() + [f("usr/share/doc/alpha-data/copyright", b"x")], "twice")

    def test_missing_required(self):
        self.refused([m for m in good_members() if not m[0].endswith("b.dat")], "missing")

    def test_generated_bytes_capped(self):
        helper.SKIP_CAP = 10
        self.refused(good_members(), "too much besides")

    def test_total_capped(self):
        man = MANIFEST["packages"]["alpha-data"]
        with tempfile.TemporaryDirectory() as root:
            r, out = run_say(helper.read_stream, io.BytesIO(tar_of(good_members())), "alpha-data", man, root, len(A))
        self.assertIsInstance(r, helper.Refused)
        self.assertIn("larger than", str(r))

    def test_request_refusals(self):
        self.refused(tar_of(good_members()), "is not a data package of alpha", pkg="alpha-other")
        self.assertIsInstance(self.imp(b"", pkg="beta")[0], helper.Refused)
        r, _, _ = self.imp(b"", gid="gamma")
        self.assertIn("unknown game", str(r))
        r, _, _ = self.imp(b"", gid="beta")
        self.assertIn("no data to set up", str(r))
        r, _, fake = self.imp(b"", pkg="alpha-music")                 # the catalog's, but not in the manifest
        self.assertIn("manifest has no alpha-music", str(r))
        r, _, fake = self.imp(tar_of(good_members()), fake=Fake(running=True))
        self.assertIn("a game is running", str(r))
        self.assertEqual(fake.streams, [])

    def test_simulation_decides(self):
        r, out, fake = self.imp(tar_of(good_members()), fake=Fake(
            sim="Remv alpha-demo-data [89]\nInst alpha-data (89 local-deb [all])\n"))
        self.assertIsNone(r, out)
        self.assertNotIn("--no-remove", fake.streams[0])               # the replaced demo goes
        for sim, want in (("Remv beta [1]\nInst alpha-data (89)\n", "beta"),
                          ("Inst alpha-data (89)\nInst libfoo (1)\n", "libfoo"),
                          ("", "apt would not install it")):
            with self.subTest(sim=sim):
                r, out, fake = self.imp(tar_of(good_members()), fake=Fake(sim=sim))
                self.assertIsInstance(r, helper.Refused)
                self.assertIn(want, str(r))
                self.assertEqual(fake.streams, [])
                self.assert_clean()

    def test_stale_staging_swept(self):
        os.makedirs(os.path.join(helper.IMPORT_DIR, "import-old/root"), 0o700)
        os.chmod(helper.IMPORT_DIR, 0o700)
        r, out, fake = self.imp(tar_of(good_members()))
        self.assertIsNone(r, out)
        self.assert_clean()

    def test_space(self):
        old = helper.SPACE_MARGIN
        helper.SPACE_MARGIN = 1 << 62
        try:
            self.refused(tar_of(good_members()), "not enough space")
        finally:
            helper.SPACE_MARGIN = old

    def test_manifest_entries_checked(self):
        for change in ({"version": "89\nDepends: evil"}, {"edition": "deluxe"}, {"symlinks": {"a": "b"}},
                       {"conflicts": ["Bad Name"]}, {"game": "a b"}, {"required": ["nope"]},
                       {"files": {"../x": [{"size": 1, "md5": "0" * 32}]}},
                       {"files": {"x": [{"size": 1, "md5": "nothex"}]}}, {"files": {"x": [{"size": 1}]}}):
            e = dict(json.loads(json.dumps(MANIFEST))["packages"]["alpha-data"], **change)
            with self.assertRaises(helper.Refused, msg=change):
                helper.check_entry("alpha-data", e)
        self.assertTrue(helper.check_entry("alpha-data", MANIFEST["packages"]["alpha-data"]))

    def test_member_path(self):
        self.assertEqual(helper.member_path("./usr/share/x"), "usr/share/x")
        self.assertEqual(helper.member_path("usr/share/"), "usr/share")
        self.assertEqual(helper.member_path("./"), "")
        for bad in ("/x", "../x", "a/../b", "a//b", "a/./b", "a\0b", "//"):
            self.assertIsNone(helper.member_path(bad), bad)

    def test_real_manifest(self):
        """tools/gamedb/gdp-manifest.json: every package the helper accepts, editions from the closed set"""
        data = json.loads((REPO / "tools/gamedb/gdp-manifest.json").read_text())
        self.assertTrue(data["packages"])
        for pkg, e in data["packages"].items():
            helper.check_entry(pkg, e)
        self.assertEqual(data["packages"]["quake-registered"]["replaces"], ["quake-shareware"])
        self.assertTrue(data["packages"]["doom2-wad"]["launchers"])
        self.assertEqual(data["packages"]["transport-tycoon-deluxe-music"]["edition"], "music")


class Lock(Dirs):
    def test_second_action_refused(self):
        fd = helper.take_lock("import-data", "alpha", check_owner=False)
        try:
            with self.assertRaises(helper.Busy):
                helper.take_lock("remove", "alpha", check_owner=False)
            self.assertEqual(helper.lock_state(False), ("import-data", "alpha"))
            with tempfile.TemporaryDirectory() as t:
                p = Path(t) / "catalog.json"
                p.write_text(json.dumps({"games": list(GAMES.values())}))
                err = io.StringIO()
                for argv in (["h", "remove-data", "alpha"], ["h", "install", "beta"],
                             ["h", "import-data", "alpha", "alpha-data"]):
                    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()) as out:
                        self.assertEqual(helper.main(argv, str(p), False, False, stdin=io.BytesIO(b"")), 2)
                    self.assertIn("busy", out.getvalue())
                with contextlib.redirect_stdout(io.StringIO()) as out:
                    self.assertEqual(helper.main(["h", "status", "alpha"], str(p), False, False), 0)
                self.assertEqual(out.getvalue(), "status importing\n")
                # a bad request is refused before the lock is even looked at
                with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()) as out:
                    self.assertEqual(helper.main(["h", "import-data", "alpha", "beta"], str(p), False, False), 2)
                self.assertNotIn("busy", out.getvalue())
        finally:
            helper.release_lock(fd)
        self.assertIsNone(helper.lock_state(False))
        helper.release_lock(helper.take_lock("remove", "alpha", check_owner=False))   # free again

    def test_stale_state_ignored(self):
        os.makedirs(helper.RUN_DIR)
        Path(helper.RUN_DIR, "state").write_text("%d import-data alpha\n" % os.getpid())
        self.assertIsNone(helper.lock_state(False))                # nobody holds the lock

    def test_usage(self):
        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            for argv in (["h", "import-data", "alpha"], ["h", "remove-data"], ["h", "install-repo-data", "a", "b"]):
                self.assertEqual(helper.main(argv, "/nonexistent", False, False), 2)


class RemoveAndRepo(unittest.TestCase):
    def test_remove_data(self):
        fake = Fake(sim="Remv alpha-data [89]\nRemv alpha-lic-data [1]\n", inst={"alpha-data", "alpha-lic-data", "alpha"})
        r, out = run_say(helper.do_remove_data, "alpha", GAMES, fake.runner, fake.stream)
        self.assertIsNone(r, out)
        self.assertEqual(fake.streams, [["remove", "alpha-data", "alpha-lic-data"]])     # never the game itself
        fake = Fake(sim="Remv alpha-data [89]\nRemv ps3-games-licensed [1]\n", inst={"alpha-data"})
        r, out = run_say(helper.do_remove_data, "alpha", GAMES, fake.runner, fake.stream)
        self.assertIsInstance(r, helper.Refused)
        self.assertEqual(fake.streams, [])
        fake = Fake(inst=set())
        r, out = run_say(helper.do_remove_data, "alpha", GAMES, fake.runner, fake.stream)
        self.assertIsNone(r)
        self.assertEqual(fake.streams, [])
        self.assertIsInstance(run_say(helper.do_remove_data, "beta", GAMES, fake.runner, fake.stream)[0], helper.Refused)

    POLICY = "alpha-data:\n  Installed: (none)\n  Candidate: (none)\nalpha-lic-data:\n  Installed: (none)\n  Candidate: 1.0\n"

    def test_install_repo_data(self):
        fake = Fake(sim="Inst alpha-lic-data (1.0 licensed [all])\n", policy=self.POLICY)
        r, out = run_say(helper.do_install_repo_data, "alpha", GAMES, fake.stream, lambda: 60, fake.runner)
        self.assertIsNone(r, out)
        self.assertEqual(fake.streams, [["install", "--no-remove", "--no-install-recommends", "alpha-lic-data"]])
        self.assertFalse([c for c in fake.calls if c[:2] == [helper.APT_CACHE, "show"]])   # no Section lock
        for sim in ("Inst alpha-lic-data (1)\nInst other (1)\n", "Inst alpha-lic-data (1)\nRemv alpha [1]\n"):
            fake = Fake(sim=sim, policy=self.POLICY)
            r, out = run_say(helper.do_install_repo_data, "alpha", GAMES, fake.stream, lambda: 60, fake.runner)
            self.assertIsInstance(r, helper.Refused, sim)
            self.assertEqual(fake.streams, [])
        fake = Fake(policy=self.POLICY.replace("1.0", "(none)"))
        r, out = run_say(helper.do_install_repo_data, "alpha", GAMES, fake.stream, lambda: 60, fake.runner)
        self.assertIsInstance(r, helper.Refused)
        self.assertIn("none of alpha-data, alpha-lic-data", str(r))
        fake = Fake(inst={"alpha-data"}, policy=self.POLICY)
        r, out = run_say(helper.do_install_repo_data, "alpha", GAMES, fake.stream, lambda: 60, fake.runner)
        self.assertIsNone(r)
        self.assertEqual(fake.streams, [])                            # the data is there already
        fake = Fake(sim="Inst alpha-lic-data (1)\n", policy=self.POLICY)
        run_say(helper.do_install_repo_data, "alpha", GAMES, fake.stream, lambda: None, fake.runner)
        self.assertEqual(fake.streams[0], ["update"])                 # no lists yet: updated first


# ---- walnut-data ------------------------------------------------------------------------------

DS = {"gdp": "quake2", "required": ["quake2-demo-data", "quake2-full-data"], "optional": [],
      "downloads": ["quake2-full-data"], "present_any": ["quake2-demo-data", "quake2-full-data"]}


class DataArgs(unittest.TestCase):
    def test_parse(self):
        a = wd.parse(["import", "-p", "x", "-p", "y", "--no-download", "yquake2", "/a", "b.iso"])
        self.assertEqual((a.cmd, a.game, a.sources, a.package, a.no_download), ("import", "yquake2", ["/a", "b.iso"],
                                                                              ["x", "y"], True))
        self.assertEqual(wd.parse(["list"]).cmd, "list")
        self.assertEqual(wd.parse(["remove", "doom"]).game, "doom")
        self.assertEqual(wd.parse(["install-repo", "doom"]).cmd, "install-repo")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(wd.main(["import", "doom"]), 2)          # no SOURCE
            self.assertEqual(wd.main(["frobnicate"]), 2)

    def test_packages_and_download_choice(self):
        self.assertEqual(wd.wanted_packages(DS, []), ["quake2-demo-data", "quake2-full-data"])
        self.assertEqual(wd.wanted_packages(DS, ["quake2-full-data"]), ["quake2-full-data"])
        with self.assertRaises(wd.Refused):
            wd.wanted_packages(DS, ["quake-registered"])
        self.assertEqual(wd.download_choice(DS, ["quake2-demo-data", "quake2-full-data"]), "--no-download")
        self.assertEqual(wd.download_choice(DS, ["quake2-full-data"]), "--download")
        self.assertEqual(wd.download_choice(DS, ["quake2-full-data"], no_download=True), "--no-download")
        self.assertEqual(wd.download_choice(dict(DS, downloads=[]), ["quake2-full-data"]), "--no-download")

    def test_game_checks(self):
        games = {"yquake2": {"id": "yquake2", "data_setup": DS}, "beta": {"id": "beta"}}
        for gid, want in (("nope", "unknown game"), ("beta", "no data"), ("../x", "not a game id")):
            with self.assertRaises(wd.Refused) as c:
                wd.data_setup(gid, games)
            self.assertIn(want, str(c.exception))

    def test_source_kinds(self):
        with tempfile.TemporaryDirectory() as t:
            for n, kind in (("rct2.iso", "image"), ("RCT2.ISO", "image"), ("disk.img", "image"),
                            ("setup_rct2.exe", "path"), ("data.zip", "path")):
                Path(t, n).write_bytes(b"x")
                self.assertEqual(wd.source_kind(os.path.join(t, n)), kind, n)
            self.assertEqual(wd.source_kind(t), "path")
            os.mkdir(os.path.join(t, "folder.iso"))                   # a folder is a folder whatever its name
            self.assertEqual(wd.source_kind(os.path.join(t, "folder.iso")), "path")
            with self.assertRaises(wd.Refused):
                wd.source_kind(os.path.join(t, "missing"))

    def test_mount_point(self):
        with tempfile.TemporaryDirectory() as t:
            m = Path(t) / "mounts"
            m.write_text("/dev/sda1 / ext4 rw 0 0\n/dev/sr0 /media/u/RCT2\\040CD iso9660 ro 0 0\n")
            self.assertEqual(wd.mount_point("/dev/sr0", str(m)), "/media/u/RCT2 CD")
            self.assertIsNone(wd.mount_point("/dev/sr1", str(m)))

    def test_gdp_command(self):
        cmd = wd.gdp_command(DS, ["quake2-full-data"], ["/m/cd"], "/c/out", "--download", 3, True)
        self.assertEqual(cmd, [wd.GDP, "--no-search", "--no-compress", "--no-launchers", "--progress-fd", "3", "-n",
                               "-d", "/c/out", "--download", "-p", "quake2-full-data", "quake2", "/m/cd"])
        cmd = wd.gdp_command(DS, ["a", "b"], ["/m"], "/o", "--no-download", None, False)   # Debian's gdp
        self.assertNotIn("--progress-fd", cmd)
        self.assertNotIn("--no-launchers", cmd)
        self.assertEqual(cmd[-6:], ["-p", "a", "-p", "b", "quake2", "/m"])

    def test_unit_command(self):
        u = wd.unit_command(["gdp", "x"], "/c/tmp", "/c/fifo", True)
        self.assertEqual(u[:6], [wd.SYSTEMD_RUN, "--user", "--pipe", "--wait", "--quiet", "--collect"])
        for p in ("MemoryMax=" + wd.MEMORY_MAX, "RuntimeMaxSec=" + wd.RUNTIME_MAX, "NoNewPrivileges=yes",
                  "PrivateNetwork=yes"):
            self.assertEqual(u[u.index(p) - 1], "-p")
        self.assertIn("--setenv=TMPDIR=/c/tmp", u)
        self.assertEqual(u[u.index("/bin/sh"):], ["/bin/sh", "-c", 'exec 3>"$0" && exec "$@"', "/c/fifo", "gdp", "x"])
        self.assertNotIn("PrivateNetwork=yes", wd.unit_command(["gdp"], "/t", None, False))


FAKE_GDP = r'''#!/usr/bin/python3
import json, os, sys
a = sys.argv[1:]
LOG = os.environ["FAKE_LOG"]
if a == ["--help"]:
    print("usage ... --search | --no-search" + os.environ.get("FAKE_GDP_HELP", " --progress-fd FD --no-launchers"))
    sys.exit(0)
with open(LOG, "a") as f:
    f.write(json.dumps({"gdp": a, "tmpdir": os.environ.get("TMPDIR")}) + "\n")
fd = int(a[a.index("--progress-fd") + 1]) if "--progress-fd" in a else None
def prog(line):
    if fd is not None:
        os.write(fd, (line + "\n").encode())
prog("step searching " + a[-1])
print("game-data-packager talking")
if os.environ.get("FAKE_GDP_FAIL"):
    prog("error not-found no files")
    sys.exit(1)
out = a[a.index("-d") + 1]
pkg = a[a.index("-p") + 1]
open(os.path.join(out, pkg + "_89_all.deb"), "wb").write(b"TARDATA:" + pkg.encode())
if os.environ.get("FAKE_GDP_EXTRA"):
    open(os.path.join(out, "heretic-shareware-wad_1.2_all.deb"), "wb").write(b"x")
prog("generated " + os.path.join(out, pkg + "_89_all.deb"))
'''
FAKE_DPKG_DEB = r'''#!/bin/sh
[ "$1" = --fsys-tarfile ] && exec cat "$2"
exit 2
'''
FAKE_HELPER = r'''#!/usr/bin/python3
import json, os, sys
data = sys.stdin.buffer.read() if sys.argv[1] == "import-data" else b""
with open(os.environ["FAKE_LOG"], "a") as f:
    f.write(json.dumps({"helper": sys.argv[1:], "stdin": data.decode()}) + "\n")
rc = int(os.environ.get("FAKE_HELPER_RC", "0"))
print("progress 100 installed" if rc == 0 else "error refused: no")
sys.exit(rc)
'''
# systemd-run: its options skipped, the command run (the unit's environment: --setenv)
FAKE_SYSTEMD_RUN = r'''#!/usr/bin/python3
import os, sys
a, i = sys.argv, 1
env = dict(os.environ)
while a[i].startswith("-"):
    if a[i].startswith("--setenv="):
        k, _, v = a[i][9:].partition("=")
        env[k] = v
    if a[i] == "-p":
        i += 1
    i += 1
os.execve(a[i], a[i:], env)
'''
FAKE_UDISKSCTL = r'''#!/bin/sh
echo "$*" >> "$FAKE_UDISKS_LOG"
case $1:${FAKE_UDISKS_MODE:-} in
loop-setup:noauth) echo "Error setting up loop device for $4: GDBus.Error:org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain: Not authorized to perform operation"; exit 1 ;;
loop-setup:*) echo "Mapped file $4 as /dev/loop7." ;;
mount:already) echo "Error mounting $3: GDBus.Error:org.freedesktop.UDisks2.Error.AlreadyMounted: Device $3 is already mounted at \`$FAKE_MNT'."; exit 1 ;;
mount:*) echo "Mounted $3 at $FAKE_MNT." ;;
esac
'''


class DataImport(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.t = Path(self.tmp.name)
        self.old = {k: getattr(wd, k) for k in ("GDP", "DPKG_DEB", "HELPER", "SUDO", "SYSTEMD_RUN", "UDISKSCTL",
                                                "MANIFEST", "SYS_BLOCK", "AUTOMOUNT_WAIT")}
        self.env = dict(os.environ)
        for name, text in (("gdp", FAKE_GDP), ("dpkg-deb", FAKE_DPKG_DEB), ("helper", FAKE_HELPER),
                           ("systemd-run", FAKE_SYSTEMD_RUN), ("udisksctl", FAKE_UDISKSCTL)):
            (t / name).write_text(text)
            (t / name).chmod(0o755)
        wd.GDP, wd.DPKG_DEB, wd.HELPER, wd.SUDO = str(t / "gdp"), str(t / "dpkg-deb"), str(t / "helper"), []
        wd.SYSTEMD_RUN, wd.UDISKSCTL = str(t / "none"), str(t / "udisksctl")
        wd.MANIFEST, wd.SYS_BLOCK, wd.AUTOMOUNT_WAIT = str(t / "no-manifest"), str(t / "sys"), 0
        os.environ.update(FAKE_LOG=str(t / "log"), XDG_CACHE_HOME=str(t / "cache"), FAKE_UDISKS_LOG=str(t / "udisks"),
                          FAKE_MNT=str(t / "mnt"))
        (t / "src").mkdir()
        self.games = {"openrct2": {"id": "openrct2", "data_setup": {
            "gdp": "roller-coaster-tycoon2", "required": ["roller-coaster-tycoon2-data"], "optional": [],
            "downloads": [], "present_any": ["roller-coaster-tycoon2-data", "rct2-data"]}}}

    def tearDown(self):
        for k, v in self.old.items():
            setattr(wd, k, v)
        os.environ.clear()
        os.environ.update(self.env)
        self.tmp.cleanup()

    def log(self):
        p = self.t / "log"
        return [json.loads(l) for l in p.read_text().splitlines()] if p.exists() else []

    def imp(self, *sources, game="openrct2"):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = wd.main(["import", game] + [str(s) for s in sources], catalog=self.catalog())
        return rc, buf.getvalue().splitlines()

    def catalog(self):
        p = self.t / "catalog.json"
        p.write_text(json.dumps({"games": list(self.games.values())}))
        return str(p)

    def test_import_direct(self):
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        log = self.log()
        gdp, h = log[0], log[1]
        a = gdp["gdp"]
        self.assertEqual(a[:4], ["--no-search", "--no-compress", "--no-launchers", "--progress-fd"])
        self.assertEqual(a[a.index("-d") + 1], str(self.t / "cache/walnut-data/out"))
        self.assertIn("-n", a)
        self.assertIn("--no-download", a)
        self.assertEqual(a[-3:], ["roller-coaster-tycoon2-data", "roller-coaster-tycoon2", str(self.t / "src")])
        self.assertEqual(gdp["tmpdir"], str(self.t / "cache/walnut-data/tmp"))
        self.assertEqual(h, {"helper": ["import-data", "openrct2", "roller-coaster-tycoon2-data"],
                             "stdin": "TARDATA:roller-coaster-tycoon2-data"})
        self.assertIn("gdp step searching " + str(self.t / "src"), out)
        self.assertIn("log game-data-packager talking", out)
        self.assertIn("progress 100 installed", out)
        # out/ and tmp/ gone, the lock left (free); the deb gone as soon as dpkg-deb had it open
        self.assertEqual(sorted(os.listdir(self.t / "cache/walnut-data")), ["import.lock"])
        self.assertIsNone(wd.import_state(str(self.t / "cache/walnut-data")))

    def test_import_in_a_unit_with_the_fifo(self):
        wd.SYSTEMD_RUN = str(self.t / "systemd-run")
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        a = self.log()[0]["gdp"]
        self.assertEqual(a[a.index("--progress-fd") + 1], "3")
        self.assertIn("gdp step searching " + str(self.t / "src"), out)
        self.assertTrue([l for l in out if l.startswith("gdp generated ")])
        self.assertEqual(self.log()[0]["tmpdir"], str(self.t / "cache/walnut-data/tmp"))

    def test_debian_gdp_without_our_options(self):
        os.environ["FAKE_GDP_HELP"] = ""
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        a = self.log()[0]["gdp"]
        self.assertNotIn("--progress-fd", a)
        self.assertNotIn("--no-launchers", a)
        self.assertFalse([l for l in out if l.startswith("gdp ")])

    def test_gdp_fails(self):
        os.environ["FAKE_GDP_FAIL"] = "1"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 1)
        self.assertIn("error game-data-packager made no package: no files of this game were found there", out)
        self.assertEqual([e for e in self.log() if "helper" in e], [])

    def test_helper_refuses(self):
        os.environ["FAKE_HELPER_RC"] = "2"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 2)
        self.assertIn("error refused: no", out)

    def test_missing_source_and_unknown_game(self):
        self.assertEqual(self.imp(self.t / "nope")[0], 2)
        self.assertEqual(self.imp(self.t / "src", game="doom")[0], 2)
        self.assertEqual(self.log(), [])

    def test_disc_image(self):
        (self.t / "mnt").mkdir()
        iso = self.t / "RCT2.iso"
        iso.write_bytes(b"iso")
        rc, out = self.imp(iso)
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.log()[0]["gdp"][-1], str(self.t / "mnt"))   # gdp reads the mounted image
        calls = (self.t / "udisks").read_text().splitlines()
        # unmounting frees the loop device: no loop-delete
        self.assertEqual(calls, ["loop-setup -r -f %s" % iso, "mount -b /dev/loop7", "unmount -b /dev/loop7"])

    def test_disc_image_the_desktop_mounted_first(self):
        (self.t / "mnt").mkdir()
        (self.t / "sys/loop7/loop").mkdir(parents=True)
        (self.t / "sys/loop7/loop/backing_file").write_text("x")      # still attached after the unmount
        iso = self.t / "RCT2.iso"
        iso.write_bytes(b"iso")
        os.environ["FAKE_UDISKS_MODE"] = "already"
        rc, out = self.imp(iso)
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.log()[0]["gdp"][-1], str(self.t / "mnt"))   # the desktop's mount, from the message
        calls = (self.t / "udisks").read_text().splitlines()
        self.assertEqual(calls, ["loop-setup -r -f %s" % iso, "mount -b /dev/loop7", "unmount -b /dev/loop7",
                                 "loop-delete -b /dev/loop7"])
        self.assertEqual(wd.already_mounted_at("Error: Device /dev/loop0 is already mounted at `/run/media/u/RCT 2'.\n"),
                         "/run/media/u/RCT 2")

    def test_disc_image_over_ssh(self):
        iso = self.t / "RCT2.iso"
        iso.write_bytes(b"iso")
        os.environ["FAKE_UDISKS_MODE"] = "noauth"
        rc, out = self.imp(iso)
        self.assertEqual(rc, 2)
        self.assertTrue([l for l in out if l.startswith("error ") and "--slice=ps3-desktop.slice" in l], out)
        self.assertEqual(self.log(), [])

    def test_unasked_deb_refused(self):
        os.environ["FAKE_GDP_EXTRA"] = "1"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 1)
        self.assertIn("error game-data-packager also made heretic-shareware-wad_1.2_all.deb, which was not asked for: "
                      "nothing was imported", out)
        self.assertEqual([e for e in self.log() if "helper" in e], [])

    def test_one_import_at_a_time(self):
        base = self.t / "cache/walnut-data"
        base.mkdir(parents=True)
        fd = wd.take_import_lock(str(base), "doom")
        try:
            self.assertEqual(wd.import_state(str(base)), (os.getpid(), "doom"))
            rc, out = self.imp(self.t / "src")
            self.assertEqual(rc, 2)
            self.assertIn("error busy: another import is running (doom)", out)
        finally:
            os.close(fd)
        self.assertIsNone(wd.import_state(str(base)))
        self.assertIsNone(wd.import_state(str(self.t / "nowhere")))

    def test_space_checked_from_the_manifest(self):
        Path(wd.MANIFEST).write_text(json.dumps({"packages": {"roller-coaster-tycoon2-data": {"files": {
            "a": [{"size": 10}, {"size": 1 << 50}], "b": [{"size": 5}]}}}}))
        self.assertEqual(wd.data_bytes(["roller-coaster-tycoon2-data", "other"]), (1 << 50) + 5)
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 2)
        self.assertTrue([l for l in out if l.startswith("error not enough space: importing this data needs about ")], out)
        self.assertEqual(self.log(), [])
        self.assertIsNone(wd.data_bytes(["x"], str(self.t / "missing.json")))

    def test_space_needs(self):
        T = 100
        one = wd.space_needs(T, False, "/c", "/v", "/u", dev=lambda p: 1)
        self.assertEqual(one, {1: (2 * T, ["/c", "/v", "/u"])})
        self.assertEqual(wd.space_needs(T, True, "/c", "/v", "/u", dev=lambda p: 1)[1][0], 3 * T)   # an installer
        apart = wd.space_needs(T, False, "/c", "/v", "/u", dev=lambda p: p)
        self.assertEqual(apart, {"/c": (2 * T, ["/c"]), "/v": (2 * T, ["/v"]), "/u": (T, ["/u"])})
        with self.assertRaises(wd.Refused) as e:
            wd.check_space(1 << 30, False, "/c", "/v", "/u", free=lambda p: 1 << 30)
        self.assertIn("about 2.1 GB free", str(e.exception))
        self.assertIn("1.0 GB is free", str(e.exception))
        wd.check_space(1 << 20, False, str(self.t), str(self.t), str(self.t), free=lambda p: 1 << 40)

    def test_remove_and_install_repo(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            self.assertEqual(wd.main(["remove", "openrct2"], catalog=self.catalog()), 0)
            self.assertEqual(wd.main(["install-repo", "openrct2"], catalog=self.catalog()), 0)
        self.assertEqual([e["helper"] for e in self.log()], [["remove-data", "openrct2"], ["install-repo-data", "openrct2"]])

    def test_shebang_and_stdlib(self):
        text = (PKG / "files/bin/walnut-data").read_text()
        self.assertEqual(text.splitlines()[0], "#!/usr/bin/python3 -I")
        self.assertNotIn("tkinter", text)
        self.assertNotIn("shell=True", text)


if __name__ == "__main__":
    unittest.main()
