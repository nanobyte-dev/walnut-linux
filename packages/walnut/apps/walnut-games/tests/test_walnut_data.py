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
- The manifest's symlinks (gdp's own: Quake III's): made exactly as listed, after the files; a missing,
  repeated, unlisted or retargeted link and a file in a link's place refused; unusable entries
  (absolute or escaping targets, a link over a file or another link) refused.
- The lock (a second action refused, busy), status "importing", remove-data, install-repo-data.
- Music packages (the manifest's tracks, no checksums): Ogg Vorbis tracks accepted at their exact paths;
  a WAV, an Opus stream, a page that isn't a stream's first, an empty file, an oversize track, a track
  number or name not listed, a missing track and a symlink refused; free space checked track by track;
  broken tracks entries refused.
- tools/gamedb/gdp-manifest.json: every entry the helper accepts (quake-music and quake2-music too).
- BIN/CUE: a synthetic disc (a MODE1/2352 data track holding an ISO volume, two audio tracks, a pregap):
  the cue parsed (CRLF, quotes, a file name in another case, INDEX 00), the ISO from MODE1/2352,
  MODE2/2352 and MODE1/2048 cut at the volume size, WAV output (MOTOROLA swapped), a multi-file cue,
  refusals (WAVE files, unknown modes, missing files, no INDEX 01, bad times, outside the file), the
  .bin given for its .cue, the tracks encoded through a fake oggenc; whole imports of a BIN/CUE (ISO
  loop-mounted and the music folder given to gdp, all removed after), music only (no ISO), no music
  package (no encoding), a broken cue refused before gdp, the space counted; the drive with a music
  package (the device given to gdp only when the disc has audio tracks).
- MDF/MDS: synthetic descriptors (one session, lead-in blocks, extra blocks, a footer): MODE1 in 2448-byte
  sectors with an audio track (ISO cut at the volume size, WAV without the subchannel), MODE2 form 1 in
  2352, cooked 2048-byte sectors (loop-mounted as they are at offset 0, extracted elsewhere), a lone .mdf
  sniffed (2448, 2352, a plain ISO; anything else refused), footer names (another case, UTF-16), a DVD
  and a CD without extra blocks, refusals (signature, version 2, mode 2 form 2, unknown mode, sector
  sizes, split images, outside the file, a missing image, a cut descriptor, no tracks); whole imports
  (ISO and music from an MDF, a plain .mdf loop-mounted, a broken .mds refused before gdp).
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
import struct
import sys
import tarfile
import tempfile
import unittest
import zipfile
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
            self.tmpdir = helper.ENV.get("TMPDIR")
            self.tree = {}
            for dirpath, dirs, files in os.walk(root):
                for n in files:
                    p = os.path.join(dirpath, n)
                    self.tree[os.path.relpath(p, root)] = (("link", os.readlink(p)) if os.path.islink(p) else
                                                           (stat.S_IMODE(os.lstat(p).st_mode), Path(p).read_bytes()))
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
        self.assertEqual(fake.tmpdir, os.path.dirname(deb[-1]))       # not /tmp: dpkg-deb's temporary member
        self.assertNotIn("TMPDIR", helper.ENV)
        self.assertEqual(len(fake.streams), 1)
        args = fake.streams[0]
        self.assertEqual(args[:4], ["install", "--no-install-recommends", "--reinstall", "--no-remove"])
        self.assertTrue(args[-1].endswith("/alpha-data.deb"))
        self.assertIn("progress 100 installed alpha-data (full, cd)", out)
        # the bar's lines: the plan, each step, the checked copy's bytes (each file at its largest size)
        self.assertEqual([l for l in out if l.split(" ", 1)[0] in ("plan", "step", "count")],
                         ["plan check deb apt", "step check checking the files of alpha-data",
                          "count %d 4507" % len(A), "count %d 4507" % (len(A) + len(B)),
                          "count %d 4507" % (len(A) + len(B) + len(MAN)),
                          "step deb making the package alpha-data", "step apt installing alpha-data"])
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

    def test_listed_symlinks(self):
        """gdp's own links (Quake III's pk3s under /usr/lib/quake3): exactly the manifest's, made after the files"""
        man = json.loads(json.dumps(MANIFEST))["packages"]
        links = {"usr/lib/alpha/base/a.dat": "../../../share/games/alpha/a.dat",
                 "usr/share/games/alpha-old/b.dat": "../alpha/b.dat"}
        man["alpha-data"]["symlinks"] = links
        ln = [(("./" + p), tarfile.SYMTYPE, b"", 0o777, t) for p, t in links.items()]
        # a link may come before the file it points to
        r, out, fake = self.imp(tar_of(ln[:1] + good_members() + ln[1:]), manifest=man)
        self.assertIsNone(r, out)
        self.assertEqual(fake.tree["usr/lib/alpha/base/a.dat"], ("link", "../../../share/games/alpha/a.dat"))
        self.assertEqual(fake.tree["usr/share/games/alpha-old/b.dat"], ("link", "../alpha/b.dat"))
        self.refused(good_members() + ln[:1], "files are missing", manifest=man)                  # all of them
        self.refused(good_members() + ln + [ln[0]], "not the link", manifest=man)                 # twice
        self.refused(good_members() + [ln[0][:4] + ("../../../../etc/shadow",), ln[1]], "not the link", manifest=man)
        self.refused(good_members() + ln + [("./usr/lib/alpha/x", tarfile.SYMTYPE, b"", 0o777, "a")],
                     "not a regular file", manifest=man)                                     # unlisted
        self.refused(good_members() + ln[:1] + [f("./usr/share/games/alpha-old/b.dat", B)], "is not a file of",
                     manifest=man)                                                           # a file in a link's place

    def test_symlink_entries_checked(self):
        files = MANIFEST["packages"]["alpha-data"]["files"]
        for links in ({"usr/x": "/etc/shadow"}, {"usr/x": "../../etc/shadow"}, {"usr/x": ""}, {"usr/x": "a\0b"},
                      {GD + "a.dat": "b.dat"}, {"usr/share/games": "alpha"}, {"usr/l": "x", "usr/l/m": "y"},
                      {"../usr/x": "y"}, {"usr/x": "y" * 256}, ["usr/x"], {"usr/x": 3}):
            e = dict(json.loads(json.dumps(MANIFEST))["packages"]["alpha-data"], symlinks=links)
            with self.assertRaises(helper.Refused, msg=links):
                helper.check_entry("alpha-data", e)
        e = dict(json.loads(json.dumps(MANIFEST))["packages"]["alpha-data"],
                 symlinks={"usr/lib/quake3/ta/baseq3/pak0.pk3": "../../../../share/games/quake3-data/baseq3/pak0.pk3",
                           "usr/share/games/q/ta": "../../../games/quake3"})
        self.assertTrue(helper.check_entry("alpha-data", e) and files)

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

    def test_cut_at_a_member_boundary_or_in_a_header(self):
        # every required file is in, only the optional manual and gdp's own files are cut off: tarfile reads
        # such a stream as a whole one
        data = tar_of([m for m in good_members() if not m[0].endswith("manual.txt")][:7])
        with tarfile.open(fileobj=io.BytesIO(data)) as t:
            last = t.getmembers()[-1]
        end = last.offset_data + (last.size + 511) // 512 * 512
        for cut in (end, end + 100, end + 512):
            with self.subTest(cut=cut):
                self.refused(data[:cut] if cut != end + 512 else data[:end] + b"x" * 512, "cut short")
        r, out, fake = self.imp(data)               # the same members with the end-of-archive block: fine
        self.assertIsNone(r, out)

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

    def test_space_in_two_steps(self):
        """before the stream, room for the staged files (walnut-data's deb still holds its space then); once
        it has ended, dpkg-deb's temporary data member and the deb, twice the staged bytes more"""
        old = helper.free_bytes, helper.SPACE_MARGIN
        cap = len(A) + len(B) + len(MAN)
        helper.SPACE_MARGIN = 0
        try:
            calls = []
            helper.free_bytes = lambda p: calls.append(p) or (cap + 10, 1)   # the files fit, their deb doesn't
            self.refused(tar_of(good_members()), "not enough space")
            self.assertEqual(len(calls), 4)                                  # refused by the second check
            helper.free_bytes = lambda p: (1 << 20, 1)
            r, out, fake = self.imp(tar_of(good_members()))
            self.assertIsNone(r, out)
        finally:
            helper.free_bytes, helper.SPACE_MARGIN = old

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
                             ["h", "import-data", "alpha", "alpha-data"], ["h", "adopt"]):
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
            for argv in (["h", "import-data", "alpha"], ["h", "remove-data"], ["h", "install-repo-data", "a", "b"],
                         ["h", "adopt", "alpha"], ["h", "adopt", "--all"]):
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
        fake = Fake(sim="Remv alpha-data [89]\n", inst={"alpha-data"}, running=True)
        r, out = run_say(helper.do_remove_data, "alpha", GAMES, fake.runner, fake.stream)
        self.assertIsInstance(r, helper.Refused)
        self.assertIn("a game is running", str(r))
        self.assertEqual(fake.streams, [])

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
            self.assertEqual(wd.main(["import"]), 2)                  # no GAME
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
        ds = dict(DS, fetch=["quake2-demo-data"])
        self.assertEqual(wd.download_choice(ds, ["quake2-demo-data"], sourceless=True), "--download")
        self.assertEqual(wd.download_choice(ds, ["quake2-demo-data"]), "--no-download")     # with a source: downloads only
        self.assertEqual(wd.download_choice(ds, ["quake2-full-data"], sourceless=True), "--no-download")

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
for f in os.environ.get("FAKE_GDP_FILES", "").split():        # NAME:BYTES, its file lines
    name, _, size = f.rpartition(":")
    prog("file %s %s" % (name, size))
print("game-data-packager talking")
if os.environ.get("FAKE_GDP_FAIL"):
    prog("error " + os.environ.get("FAKE_GDP_ERROR", "not-found no files"))
    sys.exit(1)
out = a[a.index("-d") + 1]
pkgs = [a[i + 1] for i, x in enumerate(a) if x == "-p"]
if os.environ.get("FAKE_GDP_PARTIAL"):       # dpkg-deb ran out of space halfway through the deb
    open(os.path.join(out, pkgs[0] + "_89_all.deb"), "wb").write(b"TARDA")
    print("dpkg-deb: error: cannot append ar member file (data.tar) to 'x.deb': cannot write (No space left on device)")
    prog("error failed CalledProcessError: dpkg-deb returned non-zero exit status 2")
    sys.exit(1)
skip = os.environ.get("FAKE_GDP_SKIP", "").split()
for pkg in pkgs:
    if pkg not in skip:
        open(os.path.join(out, pkg + "_89_all.deb"), "wb").write(b"TARDATA:" + pkg.encode())
for p in pkgs:
    if p not in skip:
        prog("step building " + p)
pkg = pkgs[0]
if os.environ.get("FAKE_GDP_EXTRA"):
    open(os.path.join(out, "heretic-shareware-wad_1.2_all.deb"), "wb").write(b"x")
prog("generated " + os.path.join(out, pkg + "_89_all.deb"))
'''
FAKE_MENU = "#!/bin/sh\necho \"$*\" >> \"$FAKE_LOG.menu\"\n"
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
if os.environ.get("FAKE_HELPER_STEPS"):
    print("plan check deb apt")
    print("step check checking the files of " + sys.argv[3])
    print("count 10 20")
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
                                                "MANIFEST", "SYS_BLOCK", "AUTOMOUNT_WAIT", "MENU_TOOL")}
        self.env = dict(os.environ)
        for name, text in (("gdp", FAKE_GDP), ("dpkg-deb", FAKE_DPKG_DEB), ("helper", FAKE_HELPER),
                           ("systemd-run", FAKE_SYSTEMD_RUN), ("udisksctl", FAKE_UDISKSCTL)):
            (t / name).write_text(text)
            (t / name).chmod(0o755)
        wd.GDP, wd.DPKG_DEB, wd.HELPER, wd.SUDO = str(t / "gdp"), str(t / "dpkg-deb"), str(t / "helper"), []
        wd.SYSTEMD_RUN, wd.UDISKSCTL = str(t / "none"), str(t / "udisksctl")
        wd.MANIFEST, wd.SYS_BLOCK, wd.AUTOMOUNT_WAIT = str(t / "no-manifest"), str(t / "sys"), 0
        wd.MENU_TOOL = str(t / "no-menu")
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

    def imp(self, *sources, game="openrct2", opts=()):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = wd.main(["import"] + list(opts) + [game] + [str(s) for s in sources], catalog=self.catalog())
        return rc, buf.getvalue().splitlines()

    def catalog(self):
        p = self.t / "catalog.json"
        p.write_text(json.dumps({"games": list(self.games.values())}))
        return str(p)

    def test_import_without_source_only_for_fetched_packages(self):
        ds = self.games["openrct2"]["data_setup"]
        ds.update(required=["roller-coaster-tycoon2-data"], optional=["rct2-demo"], downloads=["rct2-demo"],
                  fetch=["rct2-demo"])
        for args in ((), ("-p", "roller-coaster-tycoon2-data"), ("-p", "rct2-demo", "-p", "roller-coaster-tycoon2-data"),
                     ("--no-download", "-p", "rct2-demo")):
            rc, out = self.imp(opts=args)
            self.assertEqual(rc, 2, args)
            self.assertTrue(any("no SOURCE given" in l for l in out), out)
        self.assertEqual(self.log(), [])                               # gdp never ran
        rc, out = self.imp(opts=("-p", "rct2-demo"))
        a = self.log()[0]["gdp"]
        self.assertIn("--download", a)
        self.assertIn("--no-search", a)
        self.assertEqual(a[a.index("-p") + 1], "rct2-demo")
        self.assertEqual(a[-2:], ["rct2-demo", "roller-coaster-tycoon2"])     # no source after the game

    def test_plain_import_asks_for_the_base_only(self):
        self.partial_games()
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        a = self.log()[0]["gdp"]
        self.assertEqual([a[i + 1] for i, x in enumerate(a) if x == "-p"], ["a-data", "b-data"])   # not c-music
        rc, out = self.imp(self.t / "src", opts=("-p", "c-music"))
        a = [e for e in self.log() if "gdp" in e][1]["gdp"]
        self.assertEqual([a[i + 1] for i, x in enumerate(a) if x == "-p"], ["c-music"])
        ds = self.games["openrct2"]["data_setup"]
        ds.update(required=[], optional=["x-data", "x-music"])                       # only optional ones: all
        self.assertEqual(wd.wanted_packages(ds, []), ["x-data", "x-music"])

    def test_shareware_not_imported_beside_the_full_game(self):
        man = {"quake-registered": {"replaces": ["quake-shareware"]}, "quake-shareware": {"replaces": []}}
        ds = {"gdp": "quake", "required": ["quake-shareware", "quake-registered"], "optional": []}
        old = wd.installed
        self.addCleanup(setattr, wd, "installed", old)
        wd.installed = lambda pkgs: set()
        self.assertEqual(wd.superseded(["quake-shareware", "quake-registered"], ds, man),
                         {"quake-shareware": "quake-registered"})
        self.assertEqual(wd.superseded(["quake-registered"], ds, man), {})
        self.assertEqual(wd.superseded(["quake-shareware"], ds, man), {})
        wd.installed = lambda pkgs: {"quake-registered"} & set(pkgs)
        self.assertEqual(wd.superseded(["quake-shareware"], ds, man), {"quake-shareware": "quake-registered (installed)"})
        # through the import: only the full game reaches the helper
        mp = self.t / "manifest.json"
        mp.write_text(json.dumps({"packages": man}))
        wd.MANIFEST = str(mp)
        wd.installed = lambda pkgs: set()
        self.games["quakespasm"] = {"id": "quakespasm", "data_setup": dict(ds, downloads=[], present_any=ds["required"])}
        rc, out = self.imp(self.t / "src", game="quakespasm")
        self.assertEqual(rc, 0, out)
        self.assertEqual([e["helper"][2] for e in self.log() if "helper" in e], ["quake-registered"])
        self.assertIn("log quake-shareware not imported: quake-registered takes its place", out)

    def test_space_of_an_installer_is_its_own_size(self):
        self.partial_games()
        Path(wd.MANIFEST).write_text(json.dumps({"packages": {
            "a-data": {"files": {"a": [{"size": 100}]}}, "b-data": {"files": {"b": [{"size": 100}]}},
            "c-music": {"files": {"c": [{"size": 1 << 40}]}}}}))
        (self.t / "setup.exe").write_bytes(b"x" * 1000)
        seen = {}
        real = wd.check_space
        wd.check_space = lambda total, archive, cache, **kw: seen.update(total=total, archive=archive, **kw)
        try:
            rc, out = self.imp(self.t / "setup.exe")
        finally:
            wd.check_space = real
        self.assertEqual(rc, 0, out)
        self.assertEqual((seen["total"], seen["archive"], seen["unpack"]), (200, True, 1000))   # not 1 TB

    def test_space_of_an_archive_is_what_it_unpacks_to(self):
        # a compressed archive unpacks to more than its own size (GOG's Quake Enhanced: 607 MB, 1137 MB unpacked)
        self.partial_games()
        Path(wd.MANIFEST).write_text(json.dumps({"packages": {
            "a-data": {"files": {"a": [{"size": 100}]}}, "b-data": {"files": {"b": [{"size": 100}]}}}}))
        with zipfile.ZipFile(self.t / "setup.zip", "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("game/big.dat", bytes(50000))
            z.writestr("game/small.dat", bytes(30000))
        self.assertLess((self.t / "setup.zip").stat().st_size, 2000)
        seen = {}
        real = wd.check_space
        wd.check_space = lambda total, archive, cache, **kw: seen.update(total=total, archive=archive, **kw)
        try:
            rc, out = self.imp(self.t / "setup.zip")
        finally:
            wd.check_space = real
        self.assertEqual(rc, 0, out)
        self.assertEqual((seen["total"], seen["archive"], seen["unpack"]), (200, True, 80000))

    def test_addons_found_in_a_folder(self):
        self.partial_games()
        man = {"c-music": {"files": {"usr/x/Music/TRACK.ogg": [{"size": 5}]}, "required": ["usr/x/Music/TRACK.ogg"]},
               "d-pack": {"files": {"usr/x/d/pak0.pak": [{"size": 7}, {"size": 9}], "usr/x/d/pak1.pak": [{"size": 3}]},
                          "required": ["usr/x/d/pak0.pak"]}}
        ds = self.games["openrct2"]["data_setup"]
        ds.update(optional=["c-music", "d-pack", "e-pack"])
        (self.t / "src/music").mkdir()
        (self.t / "src/music/track.OGG").write_bytes(b"12345")
        (self.t / "src/PAK0.PAK").write_bytes(b"123456789")
        self.assertEqual(wd.find_addons(ds, ["a-data"], [str(self.t / "src")], [], man), ["c-music", "d-pack"])
        (self.t / "src/PAK0.PAK").write_bytes(b"12")                     # the wrong size is another file
        self.assertEqual(wd.find_addons(ds, ["a-data"], [str(self.t / "src")], [], man), ["c-music"])
        self.assertEqual(wd.find_addons(ds, ["a-data", "c-music"], [str(self.t / "src")], [], man), [])
        self.assertEqual(wd.find_addons(ds, ["a-data"], [str(self.t / "src")], [], {}), [])
        self.assertEqual(wd.find_addons(ds, ["a-data"], [str(self.t / "none")], [], man), [])
        # reported by the whole import, after the base
        Path(wd.MANIFEST).write_text(json.dumps({"packages": man}))
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        self.assertEqual([l for l in out if l.startswith("addon ")], ["addon available c-music"])
        self.assertEqual(out.index("addon available c-music"), len(out) - 1)

    def test_addons_found_in_an_installer_listing(self):
        self.partial_games()
        inno = self.t / "innoextract"
        inno.write_text('#!/bin/sh\necho "$*" >> "$FAKE_LOG.inno"\n'
                        'echo \'Extracting "Game" - setup data version 5.5.0\'\n'
                        'echo \' - "app\\Hipnotic\\Pak0.pak" (7 B)\'\n'
                        'echo \' - "app/music/track.ogg" (5.0 KiB)\'\n'
                        'echo \' - "tmp/readme.txt"\'\n'
                        # the real format (GOG's Quake Enhanced installer): a language tag, and a size cut off
                        # rather than rounded (496862633 bytes is 473.8 MiB)
                        'echo \' - "mg1/pak0.pak" [en-US] (473 MiB)\'\n')
        inno.chmod(0o755)
        old, wd.INNOEXTRACT = wd.INNOEXTRACT, str(inno)
        try:
            (self.t / "setup.exe").write_bytes(b"MZ")
            self.assertEqual(wd.listed_files(str(self.t / "setup.exe")),
                             [("pak0.pak", (7, 0)), ("track.ogg", (5120, 1024 * 0.1)), ("readme.txt", None),
                              ("pak0.pak", (473 << 20, 1 << 20))])
            self.assertEqual((self.t / "log.inno").read_text().split(), ["--list", str(self.t / "setup.exe")])
            man = {"d-pack": {"files": {"usr/x/hipnotic/pak0.pak": [{"size": 7}]}, "required": ["usr/x/hipnotic/pak0.pak"]},
                   "c-music": {"files": {"m/track.ogg": [{"size": 5300}]}},
                   "f-pack": {"files": {"usr/x/mg1/pak0.pak": [{"size": 496862633}]}},
                   "e-pack": {"files": {"m/readme.txt": [{"size": 3}]}}}
            ds = {"optional": ["d-pack", "c-music", "e-pack", "f-pack"]}
            self.assertEqual(wd.find_addons(ds, [], [str(self.t / "setup.exe")], [], man), ["d-pack", "e-pack", "f-pack"])
        finally:
            wd.INNOEXTRACT = old
        wd.INNOEXTRACT = str(self.t / "missing")
        try:
            self.assertEqual(wd.listed_files(str(self.t / "setup.exe")), [])      # no innoextract: nothing offered
        finally:
            wd.INNOEXTRACT = old

    def test_addons_found_in_a_zip_and_music_on_a_cue(self):
        import zipfile
        with zipfile.ZipFile(self.t / "g.zip", "w") as z:
            z.writestr("Dir/PAK0.PAK", b"1234567")
        self.assertEqual(wd.listed_files(str(self.t / "g.zip")), [("pak0.pak", (7, 0))])
        track = type("T", (), {"audio": True})
        data = type("T", (), {"audio": False})
        man = {"m-music": {"files": {}, "tracks": {"source": "m/track%02d.ogg", "first": 2, "last": 3}}}
        ds = {"optional": ["m-music"]}
        self.assertEqual(wd.find_addons(ds, [], [], [("disc", ("x.cue", [data, track]), 0)], man), ["m-music"])
        self.assertEqual(wd.find_addons(ds, [], [], [("disc", ("x.cue", [data]), 0)], man), [])
        self.assertEqual(wd.find_addons(ds, [], [], [("path", "/x", 0)], man), [])

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

    def test_the_bar_s_lines(self):
        """the plan, the read step counted by the files gdp names (the manifest's, by name and size, each
        once), gdp's build step, the plan again with what gdp made, the helper's steps without its plan"""
        rct = "usr/share/games/roller-coaster-tycoon2/"
        man = {"gdp_version": "89", "packages": {"roller-coaster-tycoon2-data": {
            "files": {rct + "Data/g1.dat": [{"size": 3000, "sha256": "0" * 64}, {"size": 2900, "sha256": "1" * 64}],
                      rct + "Data/ch.dat": [{"size": 1000, "sha256": "2" * 64}]}}}}
        (self.t / "manifest.json").write_text(json.dumps(man))
        wd.MANIFEST = str(self.t / "manifest.json")
        os.environ.update(FAKE_GDP_FILES="setup.exe:600000 setup.exe//app/Data/G1.DAT:2900 Data/g1.dat:2900 "
                                         "Readme.txt:1000 Data/ch.dat:1000 Data/ch.dat:999", FAKE_HELPER_STEPS="1")
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        bar = [l for l in out if l.split(" ", 1)[0] in ("plan", "step", "count")]
        self.assertEqual(bar, ["plan read:4000 build:4000 check:4000 deb:4000 apt:4000",
                               "step read reading the files of src",
                               "count 2900 4000", "count 3900 4000",
                               "step build building the package roller-coaster-tycoon2-data",
                               "plan read:4000 build:4000 check:4000 deb:4000 apt:4000",
                               "step check checking the files of roller-coaster-tycoon2-data", "count 10 20"])

    def test_read_count_and_the_plan(self):
        man = {"quake-shareware": {"files": {"usr/share/games/quake/id1/pak0.pak": [{"size": 18689235}]}},
               "quake-registered": {"files": {"usr/share/games/quake/id1/pak0.pak": [{"size": 18689235}],
                                              "usr/share/games/quake/id1/pak1.pak": [{"size": 34257856}]}},
               "quake-music": {"files": {}, "tracks": {"source": "id1/music/track%02d.ogg", "first": 2, "last": 11}}}
        c = wd.ReadCount(["quake-registered", "quake-shareware"], man)
        self.assertEqual(c.total, 18689235 + 34257856)                  # the shared pak0.pak once
        self.assertTrue(c.file("/media/u/QUAKE/ID1/PAK0.PAK", 18689235))
        self.assertFalse(c.file("id1/pak0.pak", 18689235))              # once
        self.assertFalse(c.file("id1/pak1.pak", 5))                     # not its size
        self.assertFalse(c.file("quake.exe", 34257856))                 # not its name
        self.assertTrue(c.file("resource.1//id1/pak1.pak", 34257856))
        self.assertEqual(c.done, c.total)
        self.assertEqual(wd.ReadCount(["quake-music"], man).total, 0)  # tracks: no size to count
        self.assertEqual(wd.plan_line(10, ["quake-registered", "quake-music"], man),
                         "read:10 build:10 check:52947091 deb:52947091 apt:52947091 check:%d deb:%d apt:%d"
                         % ((10 * wd.MUSIC_TRACK_BYTES,) * 3))
        self.assertEqual(wd.expected_imports({}, ["a", "b"], []), ["a"])
        self.assertEqual(wd.expected_imports({}, ["a", "b"], ["a", "b"]), ["a", "b"])

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

    def test_gdp_unsuitable(self):
        os.environ["FAKE_GDP_FAIL"] = "1"
        os.environ["FAKE_GDP_ERROR"] = "unsuitable hexen.wad Hexen 1.0: Hexen 1.1 (registered) is required"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 1)
        self.assertIn("gdp error unsuitable hexen.wad Hexen 1.0: Hexen 1.1 (registered) is required", out)
        self.assertIn("error game-data-packager made no package: this copy is Hexen 1.0, which can't be used: "
                      "Hexen 1.1 (registered) is required", out)
        self.assertEqual([e for e in self.log() if "helper" in e], [])

    def test_helper_refuses(self):
        os.environ["FAKE_HELPER_RC"] = "2"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 2)
        self.assertIn("error refused: no", out)

    def partial_games(self, **extra):
        ds = self.games["openrct2"]["data_setup"]
        ds.update(required=["a-data", "b-data"], optional=["c-music"], downloads=["b-data"], **extra)

    def test_partial_download_package_missing_exit_0(self):
        self.partial_games()
        os.environ["FAKE_GDP_SKIP"] = "b-data c-music"
        rc, out = self.imp(self.t / "src", opts=("-p", "a-data", "-p", "b-data", "-p", "c-music"))
        self.assertEqual(rc, 0, out)
        self.assertTrue(any(l.startswith("gdp error needs-download b-data: ") for l in out), out)
        self.assertTrue(any(l.startswith("warning missing c-music: ") for l in out), out)
        helpers = [e["helper"][2] for e in self.log() if "helper" in e]
        self.assertEqual(helpers, ["a-data"])

    def test_plain_import_any_required_made_no_noise(self):
        self.partial_games()
        self.games["openrct2"]["data_setup"].update(required=["a-data", "b-data"], downloads=["b-data"])
        os.environ["FAKE_GDP_SKIP"] = "b-data"
        rc, out = self.imp(self.t / "src", opts=("--no-download",))
        self.assertEqual(rc, 0, out)
        self.assertFalse(any("b-data" in l for l in out if l.startswith(("warning", "gdp error"))), out)
        # -p given: still reported
        rc, out = self.imp(self.t / "src", opts=("--no-download", "-p", "a-data", "-p", "b-data"))
        self.assertEqual(rc, 0, out)
        self.assertTrue(any(l.startswith("gdp error needs-download b-data: ") for l in out), out)

    def test_alternatives_group_member_not_reported(self):
        self.partial_games(present_any=[])
        self.games["openrct2"]["data_setup"].update(required=[], optional=["x-data", "x-music", "x-music-fm"],
                                                    downloads=[], alternatives=[["x-music", "x-music-fm"]])
        os.environ["FAKE_GDP_SKIP"] = "x-music"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        self.assertFalse(any(l.startswith("warning missing x-music") for l in out), out)
        os.environ["FAKE_GDP_SKIP"] = "x-music x-music-fm"        # neither made: both reported
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        self.assertTrue(any(l.startswith("warning missing x-music:") for l in out), out)
        self.assertTrue(any(l.startswith("warning missing x-music-fm:") for l in out), out)

    def test_partial_with_downloads_on_is_a_warning(self):
        self.partial_games()
        self.games["openrct2"]["data_setup"].update(required=["b-data"], optional=[], downloads=["b-data"])
        os.environ["FAKE_GDP_SKIP"] = "b-data"
        rc, out = self.imp(self.t / "src", opts=("-p", "b-data"))
        self.assertEqual(rc, 1, out)         # downloads on, nothing made, the only required one
        self.assertFalse(any("needs-download" in l for l in out if l.startswith("gdp ")), out)
        self.assertIn("warning missing b-data: game-data-packager did not make it", out)

    def test_nothing_required_made_fails_with_needs_download(self):
        self.partial_games()
        os.environ["FAKE_GDP_SKIP"] = "a-data b-data"
        rc, out = self.imp(self.t / "src", opts=("--no-download",))
        self.assertEqual(rc, 1, out)
        self.assertIn("gdp error needs-download b-data: its official patch files have to be downloaded", out)
        self.assertEqual([e for e in self.log() if "helper" in e], [])

    def test_optional_only_game_missing_one(self):
        self.partial_games(present_any=[])
        self.games["openrct2"]["data_setup"].update(required=[], optional=["x-data", "x-music"], downloads=[])
        os.environ["FAKE_GDP_SKIP"] = "x-music"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        self.assertIn("warning missing x-music: game-data-packager did not make it", out)
        os.environ["FAKE_GDP_SKIP"] = "x-music x-data"
        self.assertEqual(self.imp(self.t / "src")[0], 1)

    def test_menu_reload_after_import_and_remove(self):
        menu = self.t / "menu"
        menu.write_text(FAKE_MENU)
        menu.chmod(0o755)
        wd.MENU_TOOL = str(menu)
        os.environ.update(DISPLAY=":0", XDG_RUNTIME_DIR=str(self.t / "run"))
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 0, out)
        self.assertEqual((self.t / "log.menu").read_text().splitlines(), ["--reload %s" % (self.t / "run/ps3-game-menu")])
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = wd.main(["remove", "openrct2"], catalog=self.catalog())
        self.assertEqual(rc, 0, buf.getvalue())
        self.assertEqual(len((self.t / "log.menu").read_text().splitlines()), 2)
        # no X session, a failed import, a missing tool: no reload
        del os.environ["DISPLAY"]
        self.imp(self.t / "src")
        os.environ["DISPLAY"] = ":0"
        os.environ["FAKE_GDP_FAIL"] = "1"
        self.imp(self.t / "src")
        wd.MENU_TOOL = str(self.t / "nonexistent")
        os.environ.pop("FAKE_GDP_FAIL")
        self.assertEqual(self.imp(self.t / "src")[0], 0)
        self.assertEqual(len((self.t / "log.menu").read_text().splitlines()), 2)

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

    def test_partial_deb_of_a_failed_gdp_dropped(self):
        os.environ["FAKE_GDP_PARTIAL"] = "1"
        rc, out = self.imp(self.t / "src")
        self.assertEqual(rc, 1, out)
        self.assertIn("error game-data-packager made no package: not enough free space", out)
        self.assertTrue([l for l in out if "roller-coaster-tycoon2-data_89_all.deb dropped" in l], out)
        self.assertEqual([e for e in self.log() if "helper" in e], [])          # never streamed to the helper

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
        self.assertEqual(one, {1: (3 * T, ["/c", "/v", "/u"])})
        self.assertEqual(wd.space_needs(T, True, "/c", "/v", "/u", dev=lambda p: 1)[1][0], 4 * T)   # an installer
        # unpacked whole: every package of the game it holds (GOG's Quake III Gold for Team Arena alone)
        self.assertEqual(wd.space_needs(T, True, "/c", "/v", "/u", dev=lambda p: 1, unpack=250)[1][0], 3 * T + 250)
        self.assertEqual(wd.space_needs(T, False, "/c", "/v", "/u", dev=lambda p: 1, unpack=250)[1][0], 3 * T)
        apart = wd.space_needs(T, False, "/c", "/v", "/u", dev=lambda p: p)
        self.assertEqual(apart, {"/c": (3 * T, ["/c"]), "/v": (3 * T, ["/v"]), "/u": (T, ["/u"])})
        with self.assertRaises(wd.Refused) as e:
            wd.check_space(1 << 30, False, "/c", "/v", "/u", free=lambda p: 1 << 30)
        self.assertIn("about 3.1 GB free", str(e.exception))
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


# ---- music: the helper's rule for tracks without checksums ------------------------------------

def ogg(n=200, body=b"vorbis audio"):
    """the start of an Ogg Vorbis file: a BOS page whose first packet is the identification header"""
    head = b"OggS" + b"\x00" + b"\x02" + bytes(20) + bytes([1]) + bytes([30]) + b"\x01vorbis" + bytes(23)
    return (head + body * n)[:max(len(head), n)]


MD = "usr/share/games/alpha/base/music/"
MUSIC = {"game": "alpha", "version": "89", "edition": "music", "launchers": False, "symlinks": {},
         "conflicts": [], "replaces": [], "required": [], "files": {},
         "tracks": {"source": "base/music/%02d.ogg", "first": 2, "last": 3, "max_size": 4096,
                    "paths": [MD + "02.ogg", MD + "03.ogg"]}}


def music_members(t2=None, t3=None, extra=()):
    return [d("./"), d("./usr/"), d("./usr/share/"), d("./usr/share/games/"), d("./usr/share/games/alpha/"),
            d("./" + MD), f("./" + MD + "02.ogg", ogg(300) if t2 is None else t2),
            f("./" + MD + "03.ogg", ogg(500) if t3 is None else t3),
            f("./usr/share/doc/alpha-music/copyright", b"gdp's"),
            f("./usr/share/lintian/overrides/alpha-music", b"x")] + list(extra)


class MusicImport(Dirs):
    assert_clean, refused = ImportData.assert_clean, ImportData.refused

    def imp(self, data, fake=None, pkg="alpha-music", gid="alpha", manifest=None):
        fake = fake or Fake(sim="Inst alpha-music (89 local-deb [all])\n")
        man = manifest or {"alpha-music": json.loads(json.dumps(MUSIC))}
        return super().imp(data, fake, pkg, gid, man)

    def test_good_tracks(self):
        r, out, fake = self.imp(tar_of(music_members()))
        self.assertIsNone(r, out)
        self.assertEqual(sorted(fake.tree), sorted(["DEBIAN/control", MD + "02.ogg", MD + "03.ogg",
                                                    "usr/share/doc/alpha-music/copyright"]))
        self.assertEqual(fake.tree[MD + "03.ogg"], (0o644, ogg(500)))
        control = fake.tree["DEBIAN/control"][1].decode()
        self.assertIn("X-Walnut-Edition: music\n", control)
        self.assertNotIn("X-Walnut-Release", control)
        self.assertIn("progress 100 installed alpha-music (music)", out)

    def test_not_ogg_vorbis_refused(self):
        self.refused(music_members(t3=b"RIFF....WAVEfmt " + bytes(300)), "03.ogg is not an Ogg Vorbis file")
        opus = ogg(300).replace(b"\x01vorbis", b"OpusHea")
        self.refused(music_members(t2=opus), "02.ogg is not an Ogg Vorbis file")
        not_bos = bytearray(ogg(300))
        not_bos[5] = 0
        self.refused(music_members(t2=bytes(not_bos)), "02.ogg is not an Ogg Vorbis file")
        self.refused(music_members(t2=b""), "02.ogg is empty")

    def test_size_bound_names_and_all_tracks(self):
        self.refused(music_members(t2=ogg(5000)), "02.ogg is larger than a music track may be")
        self.refused(music_members(extra=[f("./" + MD + "04.ogg", ogg(100))]), "04.ogg is not a file of alpha-music")
        self.refused(music_members(extra=[f("./usr/share/games/alpha/base/evil.ogg", ogg(100))]), "is not a file of")
        members = [m for m in music_members() if not m[0].endswith("03.ogg")]
        self.refused(members, "1 of alpha-music's files are missing, e.g. " + MD + "03.ogg")
        sym = [m for m in music_members() if not m[0].endswith("03.ogg")] + [
            ("./" + MD + "03.ogg", tarfile.SYMTYPE, b"", 0o777, "/etc/shadow")]
        self.refused(sym, "is not a regular file")

    def test_room_checked_track_by_track(self):
        old = helper.free_bytes
        calls = []

        def free(path):
            calls.append(path)
            # the precheck (expected sizes) passes, the first track's real size does not
            return (old(path)[0] if len(calls) <= 2 else helper.SPACE_MARGIN, old(path)[1])
        helper.free_bytes = free
        try:
            self.refused(tar_of(music_members()), "the music needs about")
        finally:
            helper.free_bytes = old

    def test_tracks_entry_checked(self):
        good = json.loads(json.dumps(MUSIC))
        self.assertTrue(helper.check_entry("alpha-music", good))
        for tracks in ({"paths": ["usr/share/games/alpha/base/02.ogg"]},          # not in a music/ directory
                       {"paths": ["usr/share/games/alpha/music/x.so"]},
                       {"paths": ["usr/bin/music/02.ogg"]},
                       {"paths": ["usr/share/games/alpha/../music/02.ogg"]},
                       {"paths": [MD + "02.ogg", MD + "02.ogg"]}, {"paths": []},
                       {"max_size": 0}, {"max_size": 1 << 40}, {"max_size": "1"}):
            e = json.loads(json.dumps(MUSIC))
            e["tracks"].update(tracks)
            with self.assertRaises(helper.Refused, msg=tracks):
                helper.check_entry("alpha-music", e)
        e = json.loads(json.dumps(MUSIC))
        e.pop("tracks")
        with self.assertRaises(helper.Refused):          # no files and no tracks
            helper.check_entry("alpha-music", e)

    def test_real_music_entries(self):
        data = json.loads((REPO / "tools/gamedb/gdp-manifest.json").read_text())["packages"]
        for pkg, first in (("quake-music", "usr/share/games/quake/id1/music/track02.ogg"),
                           ("quake2-music", "usr/share/games/quake2/baseq2/music/02.ogg")):
            e = data[pkg]
            self.assertEqual(e["edition"], "music")
            self.assertEqual(e["tracks"]["paths"][0], first)
            self.assertEqual(len(e["tracks"]["paths"]), e["tracks"]["last"] - e["tracks"]["first"] + 1)
            helper.check_entry(pkg, e)
        self.assertNotIn("tracks", data["quake-registered"])


# ---- walnut-data: BIN/CUE images --------------------------------------------------------------

def mode1_sector(lba, data):
    """a MODE1/2352 sector: sync, header (BCD address, mode 1), 2048 bytes of user data, EDC/ECC (zeros here)"""
    assert len(data) == 2048
    f = lba + 150
    bcd = lambda n: (n // 10) << 4 | n % 10
    return wd.SYNC + bytes([bcd(f // 4500), bcd(f // 75 % 60), bcd(f % 75), 1]) + data + bytes(288)


def iso_blocks(n_blocks, volume_blocks):
    """n_blocks of user data: block i filled with its number, block 16 a primary volume descriptor"""
    out = []
    for i in range(n_blocks):
        b = bytearray(bytes([i % 251]) * 2048)
        if i == 16:
            b[:6] = b"\x01CD001"
            b[80:84] = volume_blocks.to_bytes(4, "little")
            b[84:88] = volume_blocks.to_bytes(4, "big")
            b[128:130] = (2048).to_bytes(2, "little")
        out.append(bytes(b))
    return out


def pcm(frames, seed):
    """frames CD sectors of 16-bit little-endian stereo samples"""
    import struct
    return b"".join(struct.pack("<hh", (i * seed) % 30000 - 15000, -(i * seed) % 30000)
                    for i in range(frames * 588))


class BinCue(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.make_disc()

    def make_disc(self):
        # a data track of 20 sectors (an ISO volume of 18 blocks: the rest is padding), audio of 3 + 4 sectors
        self.blocks = iso_blocks(20, 18)
        self.a2, self.a3 = pcm(3, 7), pcm(4, 11)
        data = b"".join(mode1_sector(i, b) for i, b in enumerate(self.blocks))
        (self.t / "Game Disc.BIN").write_bytes(data + self.a2 + bytes(2 * 2352) + self.a3)
        (self.t / "game.cue").write_text(
            'REM made by a test\r\nFILE "game disc.bin" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n'
            '  TRACK 02 AUDIO\r\n    INDEX 01 00:00:20\r\n  TRACK 03 AUDIO\r\n    INDEX 00 00:00:23\r\n'
            '    INDEX 01 00:00:25\r\n')

    def test_parse_single_file(self):
        ts = wd.parse_cue(str(self.t / "game.cue"))
        self.assertEqual([(t.number, t.mode) for t in ts], [(1, "MODE1/2352"), (2, "AUDIO"), (3, "AUDIO")])
        self.assertEqual(ts[0].path, str(self.t / "Game Disc.BIN"))          # matched whatever its case
        self.assertEqual([(t.start, t.end) for t in ts],
                         [(0, 20 * 2352), (20 * 2352, 23 * 2352), (25 * 2352, 29 * 2352)])   # pregap left out
        self.assertEqual(ts[2].sectors, 4)
        self.assertAlmostEqual(wd.track_seconds(ts[2]), 4 / 75)

    def test_iso_from_mode1_and_mode2(self):
        ts = wd.parse_cue(str(self.t / "game.cue"))
        n = wd.write_iso(ts[0], str(self.t / "out.iso"))
        self.assertEqual(n, 18 * 2048)                                      # cut at the volume's size
        self.assertEqual((self.t / "out.iso").read_bytes(), b"".join(self.blocks[:18]))
        # MODE2/2352 (form 1): 8 bytes of subheader before the data
        m2 = b"".join(wd.SYNC + bytes([0, 2, 0, 2]) + bytes(8) + b + bytes(280) for b in self.blocks)
        (self.t / "m2.bin").write_bytes(m2)
        (self.t / "m2.cue").write_text("FILE m2.bin BINARY\nTRACK 1 MODE2/2352\nINDEX 1 0:00:00\n")
        t = wd.parse_cue(str(self.t / "m2.cue"))[0]
        self.assertEqual(wd.write_iso(t, str(self.t / "m2.iso")), 18 * 2048)
        self.assertEqual((self.t / "m2.iso").read_bytes(), b"".join(self.blocks[:18]))
        # MODE1/2048 (cooked) as it is; a track that is not what the cue says refuses
        (self.t / "c.bin").write_bytes(b"".join(self.blocks))
        (self.t / "c.cue").write_text("FILE c.bin BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n")
        self.assertEqual(wd.write_iso(wd.parse_cue(str(self.t / "c.cue"))[0], str(self.t / "c.iso")), 18 * 2048)
        (self.t / "bad.cue").write_text("FILE c.bin BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n")
        with self.assertRaises(wd.Refused):
            wd.write_iso(wd.parse_cue(str(self.t / "bad.cue"))[0], str(self.t / "bad.iso"))

    def test_wav(self):
        ts = wd.parse_cue(str(self.t / "game.cue"))
        buf = io.BytesIO()
        self.assertEqual(wd.write_wav(ts[1], buf), len(self.a2))
        w = buf.getvalue()
        import wave
        with wave.open(io.BytesIO(w)) as r:
            self.assertEqual((r.getnchannels(), r.getsampwidth(), r.getframerate(), r.getnframes()), (2, 2, 44100, 3 * 588))
            self.assertEqual(r.readframes(10 ** 6), self.a2)
        # a MOTOROLA file holds big-endian samples: swapped to the WAV's little-endian
        big = bytearray(self.a3)
        big[0::2], big[1::2] = self.a3[1::2], self.a3[0::2]
        (self.t / "a.bin").write_bytes(bytes(big))
        (self.t / "a.cue").write_text("FILE a.bin MOTOROLA\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n")
        buf = io.BytesIO()
        wd.write_wav(wd.parse_cue(str(self.t / "a.cue"))[0], buf)
        self.assertEqual(buf.getvalue()[44:], self.a3)

    def test_multi_file(self):
        (self.t / "t1.bin").write_bytes(b"".join(mode1_sector(i, b) for i, b in enumerate(self.blocks)))
        (self.t / "t2.bin").write_bytes(bytes(150 * 2352) + self.a2)        # a 2 s pregap at the file's start
        (self.t / "t3.bin").write_bytes(self.a3)
        (self.t / "m.cue").write_text('FILE "t1.bin" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n'
                                      'FILE "t2.bin" BINARY\n TRACK 02 AUDIO\n  INDEX 00 00:00:00\n  INDEX 01 00:02:00\n'
                                      'FILE "t3.bin" BINARY\n TRACK 03 AUDIO\n  INDEX 01 00:00:00\n')
        ts = wd.parse_cue(str(self.t / "m.cue"))
        self.assertEqual([(os.path.basename(t.path), t.start, t.end) for t in ts],
                         [("t1.bin", 0, 20 * 2352), ("t2.bin", 150 * 2352, 153 * 2352), ("t3.bin", 0, 4 * 2352)])

    def test_refusals(self):
        for text, want in (("FILE x.wav WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n", "FILE type WAVE"),
                           ('FILE "game disc.bin" BINARY\nTRACK 01 CDG\nINDEX 01 00:00:00\n', "CDG"),
                           ("FILE missing.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n", "missing.bin"),
                           ('FILE "game disc.bin" BINARY\nTRACK 01 AUDIO\nINDEX 00 00:00:00\n', "no INDEX 01"),
                           ('FILE "game disc.bin" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:61:00\n', "not a cue sheet line"),
                           ('FILE "game disc.bin" BINARY\nTRACK 01 AUDIO\nINDEX 01 99:00:00\n', "outside"),
                           ("REM nothing\n", "no tracks"), ("TRACK 01 AUDIO\n", "not a cue sheet line")):
            (self.t / "x.cue").write_text(text)
            with self.assertRaises(wd.Refused, msg=text) as e:
                wd.parse_cue(str(self.t / "x.cue"))
            self.assertIn(want, str(e.exception), text)
        (self.t / "nul.cue").write_bytes(b"FILE \0")
        with self.assertRaises(wd.Refused):
            wd.parse_cue(str(self.t / "nul.cue"))

    def test_cue_for(self):
        self.assertEqual(wd.cue_for(str(self.t / "game.cue")), str(self.t / "game.cue"))
        (self.t / "game.bin").write_bytes(b"")
        self.assertEqual(wd.cue_for(str(self.t / "game.bin")), str(self.t / "game.cue"))
        with self.assertRaises(wd.Refused) as e:
            wd.cue_for(str(self.t / "Game Disc.BIN"))
        self.assertIn("needs its .cue", str(e.exception))
        self.assertIsNone(wd.cue_for(str(self.t / "setup.exe")))

    def test_encode_tracks(self):
        enc = self.t / "oggenc"
        enc.write_text("#!/bin/sh\n# oggenc -Q -o OUT -\ncat > \"$3\"\n")
        enc.chmod(0o755)
        old = wd.OGGENC
        wd.OGGENC = str(enc)
        try:
            ts = wd.parse_cue(str(self.t / "game.cue"))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                made = wd.encode_tracks(ts, "id1/music/track%02d.ogg", str(self.t / "music"), 2, 3)
            self.assertEqual(made, [str(self.t / "music/id1/music/track02.ogg"), str(self.t / "music/id1/music/track03.ogg")])
            self.assertEqual(Path(made[1]).read_bytes(), wd.wav_header(len(self.a3)) + self.a3)
            self.assertIn("gdp step encoding the CD's music: track 3 of 3 (0:00)", buf.getvalue())
            enc.write_text("#!/bin/sh\ncat >/dev/null; exit 3\n")
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(wd.Failed):
                wd.encode_tracks(ts, "m/%02d.ogg", str(self.t / "m2"), 2, 3)
            wd.OGGENC = str(self.t / "none")
            with self.assertRaises(wd.Failed) as e:
                wd.encode_tracks(ts, "m/%02d.ogg", str(self.t / "m3"), 2, 3)
            self.assertIn("vorbis-tools", str(e.exception))
        finally:
            wd.OGGENC = old


class BinCueImport(unittest.TestCase):
    """whole imports from a BIN/CUE and from the disc in the drive, with the stubs of DataImport"""
    tearDown, log, imp, catalog = DataImport.tearDown, DataImport.log, DataImport.imp, DataImport.catalog

    def setUp(self):
        DataImport.setUp(self)
        BinCue.make_disc(self)             # the bin/cue in self.t
        enc = self.t / "oggenc"
        enc.write_text("#!/bin/sh\ncat > \"$3\"\n")
        enc.chmod(0o755)
        self.old["OGGENC"] = wd.OGGENC
        self.old["disc_audio_tracks"] = wd.disc_audio_tracks
        wd.OGGENC = str(enc)
        (self.t / "mnt").mkdir()
        self.games = {"quakespasm": {"id": "quakespasm", "data_setup": {
            "gdp": "quake", "required": ["quake-registered"], "optional": ["quake-music"], "downloads": []}}}
        Path(wd.MANIFEST).write_text(json.dumps({"packages": {
            "quake-registered": {"files": {"id1/pak0.pak": [{"size": 1000}]}},
            "quake-music": {"files": {}, "tracks": {"source": "id1/music/track%02d.ogg", "first": 2, "last": 3,
                                                    "max_size": 1 << 26, "paths": []}}}}))

    def test_bin_cue_data_and_music(self):
        seen = {}
        real = wd.Sources.add

        def add(src, path):        # the ISO as it is when it is opened
            seen["iso"] = Path(path).read_bytes()
            return real(src, path)
        wd.Sources.add = add
        try:
            rc, out = self.imp(self.t / "game.cue", game="quakespasm", opts=("-p", "quake-registered", "-p", "quake-music"))
        finally:
            wd.Sources.add = real
        self.assertEqual(rc, 0, out)
        self.assertEqual(seen["iso"], b"".join(self.blocks[:18]))
        a = self.log()[0]["gdp"]
        music = str(self.t / "cache/walnut-data/disc/music")
        self.assertEqual(a[-3:], ["quake", str(self.t / "mnt"), music])        # the mounted ISO and the music
        self.assertEqual(a[a.index("-p") + 1], "quake-registered")
        calls = (self.t / "udisks").read_text().splitlines()
        iso = str(self.t / "cache/walnut-data/disc/game.iso")
        self.assertEqual(calls, ["loop-setup -r -f %s" % iso, "mount -b /dev/loop7", "unmount -b /dev/loop7"])
        self.assertFalse((self.t / "cache/walnut-data/disc").exists())         # ISO and music removed
        self.assertIn("gdp step extracting the data track of game.cue", out)
        self.assertEqual([e["helper"] for e in self.log()[1:]], [["import-data", "quakespasm", "quake-registered"],
                                                                 ["import-data", "quakespasm", "quake-music"]])

    def test_bin_given_and_music_only(self):
        (self.t / "game.bin").symlink_to(self.t / "Game Disc.BIN")
        rc, out = self.imp(self.t / "game.bin", game="quakespasm", opts=("-p", "quake-music"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.log()[0]["gdp"][-2:], ["quake", str(self.t / "cache/walnut-data/disc/music")])
        self.assertFalse((self.t / "udisks").exists())                         # no ISO, no loop device
        self.assertNotIn("gdp step extracting the data track of game.cue", out)

    def test_bin_cue_without_music_package(self):
        rc, out = self.imp(self.t / "game.cue", game="quakespasm", opts=("-p", "quake-registered"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.log()[0]["gdp"][-2:], ["quake", str(self.t / "mnt")])
        self.assertFalse([l for l in out if "encoding" in l])

    def test_broken_cue_refused_before_anything(self):
        (self.t / "game.cue").write_text("FILE nothere.bin BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n")
        rc, out = self.imp(self.t / "game.cue", game="quakespasm")
        self.assertEqual(rc, 2)
        self.assertEqual(self.log(), [])

    def test_space_counts_the_iso_and_music(self):
        sources = wd.Sources(str(self.t), [{"source": "m/%02d.ogg", "first": 2, "last": 3}])
        kind, (cue, tracks), extra = sources.plan(str(self.t / "game.cue"))
        self.assertEqual(kind, "disc")
        self.assertEqual(extra, 20 * 2048 + int(3 / 75 * wd.OGG_ENC_BYTES) + int(4 / 75 * wd.OGG_ENC_BYTES))
        need = wd.space_needs(100, False, "/c", "/v", "/u", dev=lambda p: p, extra=5000)
        self.assertEqual(need["/c"][0], 300 + 5000)
        self.assertEqual(wd.data_bytes(["quake-music", "quake-registered"]), 2 * wd.MUSIC_TRACK_BYTES + 1000)

    def test_disc_in_the_drive_with_music(self):
        dev = self.t / "sr0"
        os.mkfifo(dev)          # stands in for a block device
        wd.disc_audio_tracks = lambda d: [2, 3]
        src = wd.Sources(str(self.t), [{"source": "id1/music/track%02d.ogg", "first": 2, "last": 3}])
        src.mount = lambda d, ours=False: str(self.t / "mnt")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(src.add_device(str(dev)), [str(self.t / "mnt"), str(dev)])    # data, and the rip
            wd.disc_audio_tracks = lambda d: []
            self.assertEqual(src.add_device(str(dev)), [str(self.t / "mnt")])
            src.only_music = True
            with self.assertRaises(wd.Refused):
                src.add_device(str(dev))                                           # no audio, no data wanted
            wd.disc_audio_tracks = lambda d: [2, 3]
            self.assertEqual(src.add_device(str(dev)), [str(dev)])                # a music CD: not mounted

            def nomount(d, ours=False):
                raise wd.Refused("cannot mount: not a file system")
            src.mount, src.only_music = nomount, False
            self.assertEqual(src.add_device(str(dev)), [str(dev)])                # an audio CD has no file system
        plain = wd.Sources(str(self.t))
        plain.mount = nomount
        with self.assertRaises(wd.Refused):
            plain.add_device(str(dev))
        wd.disc_audio_tracks = self.old["disc_audio_tracks"]
        self.assertIsNone(wd.disc_audio_tracks(str(self.t / "no-such-drive")))
        self.assertIsNone(wd.disc_audio_tracks(str(self.t / "game.cue")))         # not a CD drive: ioctl fails


SUB = bytes(range(96))                     # a sector's 96 bytes of P-W subchannel in a 2448-byte MDF


def make_mds(tracks, medium=0, name=b"*.mdf", wide=False, version=(1, 3), extra=True):
    """an Alcohol 120% descriptor: one session, the three lead-in blocks (A0-A2), then a block per track
    (point, mode byte, sector size, start offset in the .mdf, length in sectors, number of files), each
    with its extra block (pregap 150 for track 1, the length), a footer naming the image"""
    blocks = [(0xA0, 0, 0, 0, 0, 0), (0xA1, 0, 0, 0, 0, 0), (0xA2, 0, 0, 0, 0, 0)] + list(tracks)
    tb_at = 88 + 24
    extra_at = tb_at + 80 * len(blocks)
    footer_at = extra_at + 8 * len(tracks)
    name_at = footer_at + 16
    hdr = bytearray(88)
    hdr[:16] = wd.MDS_SIGNATURE
    hdr[16:18] = bytes(version)
    struct.pack_into("<HH", hdr, 18, medium, 1)
    struct.pack_into("<I", hdr, 80, 88)
    sess = bytearray(24)
    struct.pack_into("<iiHBBHHII", sess, 0, -150, 0, 1, len(blocks), 3, 1, len(tracks), 0, tb_at)
    tb, ex = b"", b""
    for i, (point, mode, size, start, length, files) in enumerate(blocks):
        b = bytearray(80)
        b[0], b[1], b[4] = mode, 8 if size == 2448 else 0, point
        if point < 0xA0:
            k = i - 3
            struct.pack_into("<IH", b, 12, extra_at + 8 * k if extra else 0, size)
            struct.pack_into("<QII", b, 40, start, files, footer_at)
            ex += struct.pack("<II", 150 if point == 1 else 0, length)
        tb += bytes(b)
    raw_name = name.decode().encode("utf-16-le") + b"\0\0" if wide else name + b"\0"
    footer = struct.pack("<IIII", name_at, 1 if wide else 0, 0, 0)
    return bytes(hdr) + bytes(sess) + tb + ex + footer + raw_name


class MdfMds(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.t = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.blocks = iso_blocks(20, 18)
        self.a2 = pcm(3, 7)
        self.make_disc()

    def make_disc(self):
        """game.mdf/game.mds: a MODE1 data track of 20 2448-byte sectors, then an audio track of 3"""
        data = b"".join(mode1_sector(i, b) + SUB for i, b in enumerate(self.blocks))
        audio = b"".join(self.a2[i * 2352:(i + 1) * 2352] + SUB for i in range(3))
        (self.t / "game.mdf").write_bytes(data + audio)
        (self.t / "game.mds").write_bytes(make_mds([(1, 0xAA, 2448, 0, 20, 1), (2, 0xA9, 2448, 20 * 2448, 3, 1)]))

    def test_mode1_2448_and_audio(self):
        ts = wd.parse_mds(str(self.t / "game.mds"))
        self.assertEqual([(t.number, t.mode, t.audio, t.start, t.end) for t in ts],
                         [(1, "MODE1/2448", False, 0, 20 * 2448), (2, "AUDIO/2448", True, 20 * 2448, 23 * 2448)])
        self.assertEqual(ts[0].path, str(self.t / "game.mdf"))
        self.assertEqual(wd.write_iso(ts[0], str(self.t / "out.iso")), 18 * 2048)
        self.assertEqual((self.t / "out.iso").read_bytes(), b"".join(self.blocks[:18]))
        buf = io.BytesIO()
        self.assertEqual(wd.write_wav(ts[1], buf), len(self.a2))           # the subchannel bytes left out
        self.assertEqual(buf.getvalue(), wd.wav_header(len(self.a2)) + self.a2)
        self.assertAlmostEqual(wd.track_seconds(ts[1]), 3 / 75)

    def test_mode2_and_cooked_sectors(self):
        m2 = b"".join(wd.SYNC + bytes([0, 2, 0, 2]) + bytes(8) + b + bytes(280) for b in self.blocks)
        (self.t / "m2.mdf").write_bytes(m2)
        (self.t / "m2.mds").write_bytes(make_mds([(1, 0xEC, 2352, 0, 20, 1)]))
        t = wd.parse_mds(str(self.t / "m2.mds"))[0]
        self.assertEqual((t.mode, t.layout), ("MODE2/2352", (2352, 24)))
        self.assertEqual(wd.write_iso(t, str(self.t / "m2.iso")), 18 * 2048)
        self.assertEqual((self.t / "m2.iso").read_bytes(), b"".join(self.blocks[:18]))
        # 2048-byte sectors at offset 0: the .mdf is loop-mounted as it is; elsewhere in the file: extracted
        (self.t / "c.mdf").write_bytes(b"".join(self.blocks))
        (self.t / "c.mds").write_bytes(make_mds([(1, 0xAA, 2048, 0, 20, 1)]))
        src = wd.Sources(str(self.t))
        self.assertEqual(src.plan(str(self.t / "c.mds")), ("image", str(self.t / "c.mdf"), 0))
        self.assertEqual(src.plan(str(self.t / "c.mdf")), ("image", str(self.t / "c.mdf"), 0))
        (self.t / "o.mdf").write_bytes(bytes(4096) + b"".join(self.blocks))
        (self.t / "o.mds").write_bytes(make_mds([(1, 0xAA, 2048, 4096, 20, 1)]))
        kind, (sheet, ts), extra = src.plan(str(self.t / "o.mdf"))
        self.assertEqual((kind, sheet, extra), ("disc", str(self.t / "o.mds"), 20 * 2048))
        self.assertEqual(wd.write_iso(ts[0], str(self.t / "o.iso")), 18 * 2048)

    def test_lone_mdf_sniffed(self):
        os.unlink(self.t / "game.mds")
        self.assertEqual(wd.mds_for(str(self.t / "game.mdf")), "")
        src = wd.Sources(str(self.t))
        kind, (sheet, ts), extra = src.plan(str(self.t / "game.mdf"))
        self.assertEqual((kind, sheet, len(ts), ts[0].mode, ts[0].end), ("disc", str(self.t / "game.mdf"), 1,
                                                                         "MODE1/2448", 23 * 2448))
        self.assertEqual(wd.write_iso(ts[0], str(self.t / "out.iso")), 18 * 2048)   # cut before the audio
        self.assertEqual((self.t / "out.iso").read_bytes(), b"".join(self.blocks[:18]))
        (self.t / "r.mdf").write_bytes(b"".join(mode1_sector(i, b) for i, b in enumerate(self.blocks)))
        self.assertEqual(wd.sniff_mdf(str(self.t / "r.mdf"))[0].layout, (2352, 16))
        (self.t / "p.mdf").write_bytes(b"".join(self.blocks))
        self.assertEqual(wd.sniff_mdf(str(self.t / "p.mdf")), "iso")
        (self.t / "x.mdf").write_bytes(bytes(40000))
        with self.assertRaises(wd.Refused) as e:
            wd.sniff_mdf(str(self.t / "x.mdf"))
        self.assertIn("give its .mds", str(e.exception))

    def test_footer_names_dvd_and_sessions(self):
        (self.t / "Other Name.MDF").write_bytes(b"".join(self.blocks))
        (self.t / "a.mds").write_bytes(make_mds([(1, 0xAA, 2048, 0, 20, 1)], name=b"other name.mdf"))
        self.assertEqual(wd.parse_mds(str(self.t / "a.mds"))[0].path, str(self.t / "Other Name.MDF"))
        (self.t / "w.mds").write_bytes(make_mds([(1, 0xAA, 2048, 0, 20, 1)], name=b"Other Name.MDF", wide=True))
        self.assertEqual(wd.parse_mds(str(self.t / "w.mds"))[0].path, str(self.t / "Other Name.MDF"))
        # a DVD's track runs to the file's end (no extra block is read)
        (self.t / "dvd.mdf").write_bytes(b"".join(self.blocks))
        (self.t / "dvd.mds").write_bytes(make_mds([(1, 0xAA, 2048, 0, 1, 1)], medium=0x10))
        self.assertEqual(wd.parse_mds(str(self.t / "dvd.mds"))[0].end, 20 * 2048)
        # a CD without extra blocks: each track to the next one's start
        (self.t / "game.mds").write_bytes(make_mds([(1, 0xAA, 2448, 0, 0, 1), (2, 0xA9, 2448, 20 * 2448, 0, 1)],
                                                   extra=False))
        self.assertEqual([t.end for t in wd.parse_mds(str(self.t / "game.mds"))], [20 * 2448, 23 * 2448])

    def test_refusals(self):
        good = [(1, 0xAA, 2448, 0, 20, 1)]
        for raw, want in ((b"MEDIA DESCRIPTOX" + bytes(80), "not an Alcohol"),
                          (make_mds(good, version=(2, 0)), "version 2.0"),
                          (make_mds([(1, 0xAD, 2352, 0, 20, 1)]), "mode 2 form 2"),
                          (make_mds([(1, 0x00, 2352, 0, 20, 1)]), "unknown"),
                          (make_mds([(1, 0xAA, 2340, 0, 20, 1)]), "2340-byte sectors"),
                          (make_mds([(1, 0xA9, 2048, 0, 20, 1)]), "AUDIO in 2048-byte"),
                          (make_mds([(1, 0xAA, 2448, 0, 20, 3)]), "split over 3 files"),
                          (make_mds([(1, 0xAA, 2448, 0, 99, 1)]), "outside"),
                          (make_mds(good, name=b"gone.mdf"), "names gone.mdf"),
                          (make_mds(good)[:120], "cut short"),
                          (make_mds([]), "no tracks")):
            (self.t / "game.mds").write_bytes(raw)
            with self.assertRaises(wd.Refused, msg=want) as e:
                wd.parse_mds(str(self.t / "game.mds"))
            self.assertIn(want, str(e.exception))

    def test_mds_for(self):
        self.assertEqual(wd.mds_for(str(self.t / "game.mds")), str(self.t / "game.mds"))
        os.rename(self.t / "game.mds", self.t / "GAME.MDS")
        self.assertEqual(wd.mds_for(str(self.t / "game.mdf")), str(self.t / "GAME.MDS"))
        self.assertIsNone(wd.mds_for(str(self.t / "game.iso")))
        self.assertEqual(wd.source_kind(str(self.t / "game.mdf")), "path")    # only plan() reads it


class MdfImport(unittest.TestCase):
    """whole imports from an MDF/MDS, with the stubs of DataImport"""
    tearDown, log, imp, catalog = DataImport.tearDown, DataImport.log, DataImport.imp, DataImport.catalog

    def setUp(self):
        DataImport.setUp(self)
        self.blocks = iso_blocks(20, 18)
        self.a2 = pcm(3, 7)
        MdfMds.make_disc(self)
        enc = self.t / "oggenc"
        enc.write_text("#!/bin/sh\ncat > \"$3\"\n")
        enc.chmod(0o755)
        self.old["OGGENC"] = wd.OGGENC
        wd.OGGENC = str(enc)
        (self.t / "mnt").mkdir()
        self.games = {"quakespasm": {"id": "quakespasm", "data_setup": {
            "gdp": "quake", "required": ["quake-registered"], "optional": ["quake-music"], "downloads": []}}}
        Path(wd.MANIFEST).write_text(json.dumps({"packages": {
            "quake-registered": {"files": {"id1/pak0.pak": [{"size": 1000}]}},
            "quake-music": {"files": {}, "tracks": {"source": "id1/music/track%02d.ogg", "first": 2, "last": 3,
                                                    "max_size": 1 << 26, "paths": []}}}}))

    def test_mdf_data_and_music(self):
        seen = {}
        real = wd.Sources.open_image

        def open_image(src, path):        # the ISO as it is when it is opened
            seen["iso"] = Path(path).read_bytes()
            return real(src, path)
        wd.Sources.open_image = open_image
        try:
            rc, out = self.imp(self.t / "game.mdf", game="quakespasm", opts=("-p", "quake-registered", "-p", "quake-music"))
        finally:
            wd.Sources.open_image = real
        self.assertEqual(rc, 0, out)
        self.assertEqual(seen["iso"], b"".join(self.blocks[:18]))
        music = str(self.t / "cache/walnut-data/disc/music")
        self.assertEqual(self.log()[0]["gdp"][-3:], ["quake", str(self.t / "mnt"), music])
        self.assertEqual(Path(self.t / "udisks").read_text().splitlines()[0],
                         "loop-setup -r -f %s" % (self.t / "cache/walnut-data/disc/game.iso"))
        self.assertIn("gdp step extracting the data track of game.mds", out)
        self.assertFalse((self.t / "cache/walnut-data/disc").exists())

    def test_plain_mdf_loop_mounted(self):
        (self.t / "p.mdf").write_bytes(b"".join(self.blocks))
        rc, out = self.imp(self.t / "p.mdf", game="quakespasm", opts=("-p", "quake-registered"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(self.log()[0]["gdp"][-1], str(self.t / "mnt"))
        self.assertEqual((self.t / "udisks").read_text().splitlines(),
                         ["loop-setup -r -f %s" % (self.t / "p.mdf"), "mount -b /dev/loop7", "unmount -b /dev/loop7"])

    def test_broken_mds_refused_before_anything(self):
        (self.t / "game.mds").write_bytes(make_mds([(1, 0xAD, 2352, 0, 20, 1)]))
        rc, out = self.imp(self.t / "game.mdf", game="quakespasm")
        self.assertEqual(rc, 2)
        self.assertEqual(self.log(), [])


if __name__ == "__main__":
    unittest.main()
