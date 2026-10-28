#!/usr/bin/env python3
"""Tests for the walnut-games package, on a PC without root, apt or (unless an X server program is
installed) a display: python3 -I packages/walnut/apps/walnut-games/tests/test_walnut_games.py (tools/run-tests.sh, suite
walnut-games).

- The root helper (files/sbin/walnut-games-helper): ids and package names validated, packages only
  from the catalog file, the catalog's owner and mode, install's arguments (--no-remove, the lists'
  age), remove's simulation and its refusal, the metapackage's dependencies marked manual, adopt
  (the games' automatic packages marked manual, from extended_states; nothing when none is), apt's
  status descriptor relayed (a fake apt-get writes to it), signals ignored, `python3 -I`, sudoers.
- The app's pure functions (files/lib/walnut_games.py): Exec parsing, the play command, filters, dpkg
  state, the helper's output lines.
- Game data in the app: which data buttons show in which state (missing, present, a licensed build,
  the repository has it, importing, optional data only), apt-cache policy, editions and the remove
  question, source detection on a fake home and media root, lsblk's disc, walnut-data's progress
  lines, the error messages, the import lock; and a window test: the chooser lists the fake home's
  sources and an import through a fake walnut-data ends in "needs its official patch files" with Try
  again with download, which runs it again with -p (WALNUT_SHOT=file.png: a picture of the chooser).
- The pane (PLAN 5.11): the main button in every state (Install, Add game files..., Importing, Play with
  a menu of launchers, off while the helper runs or for a web-only game), the smaller buttons, Remove
  data's question, the glance's numbers and apt-cache's sizes; walnut:// links (install, add-data, play
  with a launcher, anything else ignored), walnut://game/<id> the only link taken from outside, the
  single instance's lock and hand-off, the .desktop file's scheme handler; Details' tabs and where a
  clicked link goes; the d-pad's focus moves (nav_pick), the text scale, the pad's buttons and the reader
  (old presses dropped).
- A smoke test that builds the window on a private X server (Xvfb or Xvnc, started here on its own
  display); skipped when neither is installed. It never uses the DISPLAY of the person running it. It
  goes through the pane's states, the walnut:// dispatch, Details without tkinterweb (the website opens)
  and with a fake tkinterweb (tabs by L1/R1 and the arrows, links, Back, five opens leaving nothing
  behind, Install from a page through a fake helper), the arrows between the cards and the pane, and a
  hand-off from a second start (WALNUT_SHOT=file.png: a picture of the pane).
"""
import contextlib
import importlib.machinery
import importlib.util
import io
import json
import os
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG = HERE.parent
REPO = PKG.parents[3]   # the repository (PKG: packages/walnut/apps/walnut-games)
sys.dont_write_bytecode = True
sys.path.insert(0, str(PKG / "files" / "lib"))
import walnut_games as app   # noqa: E402


def load_helper():
    loader = importlib.machinery.SourceFileLoader("walnut_games_helper", str(PKG / "files/sbin/walnut-games-helper"))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


helper = load_helper()

GAMES = {
    "alpha": {"id": "alpha", "title": "Alpha", "packages": ["alpha", "alpha-data"], "desktop": ["alpha.desktop"]},
    "beta": {"id": "beta", "title": "Beta", "packages": ["beta"], "desktop": ["beta.desktop"]},
}


def run_say(fn, *a, **kw):
    """(result or exception, the helper's output lines)"""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        try:
            r = fn(*a, **kw)
        except Exception as e:   # the tests look at which one
            r = e
    return r, buf.getvalue().splitlines()


class Names(unittest.TestCase):
    def test_bad_ids(self):
        for bad in ("", "Alpha", "../alpha", "alpha beta", "-alpha", "a" * 65, "alpha;ls", "alpha\n", "α"):
            with self.assertRaises(helper.Refused, msg=repr(bad)):
                helper.game_packages(bad, GAMES)

    def test_unknown_id(self):
        with self.assertRaises(helper.Refused):
            helper.game_packages("gamma", GAMES)
        with self.assertRaises(helper.Refused):    # a package name is not an id
            helper.game_packages("alpha-data", GAMES)

    def test_catalog_maps_id_to_packages(self):
        self.assertEqual(helper.game_packages("alpha", GAMES), ["alpha", "alpha-data"])
        self.assertEqual(helper.game_packages("a" * 64, {"a" * 64: {"packages": ["x1"]}}), ["x1"])

    def test_bad_package_names_in_the_catalog(self):
        for bad in ("Foo", "a;b", "-rf", "x", "a b", "foo-", "$(id)", "", 5, None):
            with self.assertRaises(helper.Refused, msg=repr(bad)):
                helper.game_packages("alpha", {"alpha": {"packages": ["alpha", bad]}})
        with self.assertRaises(helper.Refused):
            helper.game_packages("alpha", {"alpha": {"packages": []}})
        with self.assertRaises(helper.Refused):
            helper.game_packages("alpha", {"alpha": {}})

    def test_licensed_data_is_refused(self):
        for bad in ("sauerbraten-nonfree-data", "ps3-games-licensed"):
            with self.assertRaises(helper.Refused, msg=bad):
                helper.game_packages("alpha", {"alpha": {"packages": ["alpha", bad]}})
        # game names are not in the generic code: the apt Section check is the lock for these
        for ok in ("quake-shareware", "quake2-demo-data", "quakespasm", "sauerbraten-data"):
            self.assertEqual(helper.game_packages("alpha", {"alpha": {"packages": [ok]}}), [ok])

    def test_catalog_file_owner_and_mode(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "catalog.json"
            p.write_text(json.dumps({"games": list(GAMES.values())}))
            self.assertEqual(sorted(helper.load_catalog(str(p), check_owner=False)), ["alpha", "beta"])
            if os.geteuid() != 0:
                with self.assertRaises(helper.Refused):     # not root's
                    helper.load_catalog(str(p))
            os.chmod(p, 0o666)
            with self.assertRaises(helper.Refused):          # writable by others
                helper.load_catalog(str(p))

    def test_main_refusals(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "catalog.json"
            p.write_text(json.dumps({"games": list(GAMES.values())}))
            old = signal.getsignal(signal.SIGHUP), signal.getsignal(signal.SIGPIPE)
            try:
                err = io.StringIO()
                with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(helper.main(["h"], str(p), False, False), 2)
                    self.assertEqual(helper.main(["h", "purge", "alpha"], str(p), False, False), 2)
                    self.assertEqual(helper.main(["h", "install", "alpha", "x"], str(p), False, False), 2)
                    self.assertEqual(helper.main(["h", "install", "nope"], str(p), False, False), 2)
                    self.assertEqual(helper.main(["h", "remove", "../x"], str(p), False, False), 2)
                    if os.geteuid() != 0:
                        self.assertEqual(helper.main(["h", "install", "alpha"], str(p), False, True), 2)
                        self.assertEqual(helper.main(["h", "adopt"], str(p), False, True), 2)   # root only
                self.assertEqual(signal.getsignal(signal.SIGHUP), signal.SIG_IGN)
                self.assertEqual(signal.getsignal(signal.SIGPIPE), signal.SIG_IGN)
            finally:
                signal.signal(signal.SIGHUP, old[0])
                signal.signal(signal.SIGPIPE, old[1])

    def test_isolated_python_and_fixed_commands(self):
        text = (PKG / "files/sbin/walnut-games-helper").read_text()
        self.assertEqual(text.splitlines()[0], "#!/usr/bin/python3 -I")
        self.assertTrue(helper.APT_GET.startswith("/usr/bin/") and helper.APT_MARK.startswith("/usr/bin/"))
        self.assertEqual(helper.ENV["DEBIAN_FRONTEND"], "noninteractive")
        self.assertNotIn("shell=True", text)

    def test_sudoers(self):
        text = (PKG / "debian/sudoers").read_text()
        rules = [l for l in text.splitlines() if l.strip() and not l.startswith("#")]
        self.assertEqual(rules, ["%sudo ALL=(root) NOPASSWD: /usr/lib/ps3/sbin/walnut-games-helper"])
        if shutil.which("visudo") and os.geteuid() == 0:
            self.assertEqual(subprocess.run(["visudo", "-cf", str(PKG / "debian/sudoers")]).returncode, 0)


def fake_apt_cache(sections, rc=0):
    """a runner answering apt-cache show: sections is {package: Section or None (no Section line)};
    a package missing from it gets no stanza"""
    def runner(cmd):
        assert cmd[0] == helper.APT_CACHE and cmd[1:3] == ["show", "--no-all-versions"], cmd
        out = ""
        for p in cmd[3:]:
            if p in sections:
                out += "Package: %s\nVersion: 1.0\n%s\nDescription: x\n\n" % (
                    p, "Section: " + sections[p] if sections[p] else "Priority: optional")
        return rc, out
    return runner


FREE = fake_apt_cache({"alpha": "games", "alpha-data": "games", "beta": "games"})


class Install(unittest.TestCase):
    def test_install_arguments_and_stale_lists(self):
        calls = []
        stream = lambda args: (calls.append(args), (0, []))[1]   # noqa: E731
        _, out = run_say(helper.do_install, "alpha", GAMES, stream, lambda: 3 * 86400, FREE)
        self.assertEqual(calls, [["update"], ["install", "--no-remove", "alpha", "alpha-data"]])
        self.assertEqual(out[-1], "progress 100 installed")
        calls.clear()
        run_say(helper.do_install, "alpha", GAMES, stream, lambda: 600, FREE)
        self.assertEqual(calls, [["install", "--no-remove", "alpha", "alpha-data"]])
        calls.clear()
        run_say(helper.do_install, "alpha", GAMES, stream, lambda: None, FREE)   # no lists at all: update
        self.assertEqual(calls[0], ["update"])

    def test_failed_install(self):
        r, out = run_say(helper.do_install, "beta", GAMES, lambda a: (100, ["E: broken"]), lambda: 1, FREE)
        self.assertIsInstance(r, helper.AptFailed)
        self.assertTrue(any(l.startswith("error ") for l in out))

    def _install_with(self, runner):
        calls = []
        stream = lambda args: (calls.append(args), (0, []))[1]   # noqa: E731
        r, _ = run_say(helper.do_install, "alpha", GAMES, stream, lambda: 600, runner)
        return r, calls

    def test_section_games_installs(self):
        r, calls = self._install_with(fake_apt_cache({"alpha": "games", "alpha-data": "games/data"}))
        self.assertNotIsInstance(r, Exception)
        self.assertEqual(calls, [["install", "--no-remove", "alpha", "alpha-data"]])

    def test_section_local_or_nonfree_is_refused(self):
        for sec in ("local/games", "non-free/games"):
            r, calls = self._install_with(fake_apt_cache({"alpha": "games", "alpha-data": sec}))
            self.assertIsInstance(r, helper.Refused, msg=sec)
            self.assertEqual(calls, [], msg=sec)

    def test_section_one_licensed_of_several(self):
        r, calls = self._install_with(fake_apt_cache({"alpha": "local/games", "alpha-data": "games"}))
        self.assertIsInstance(r, helper.Refused)
        self.assertEqual(calls, [])

    def test_section_unreadable_fails_closed(self):
        for runner in (fake_apt_cache({"alpha": "games", "alpha-data": "games"}, rc=100),   # apt-cache failed
                       fake_apt_cache({"alpha": "games"}),                                    # no stanza
                       fake_apt_cache({"alpha": "games", "alpha-data": None})):               # no Section
            r, calls = self._install_with(runner)
            self.assertIsInstance(r, helper.Refused)
            self.assertEqual(calls, [])

    def test_former_package_replaced(self):
        games = dict(GAMES, beta=dict(GAMES["beta"], former_packages=["beta-old", "beta"]))
        sections = fake_apt_cache({"beta": "games"})

        def runner_for(inst, sim):
            seen = []

            def runner(cmd):
                seen.append(cmd)
                if cmd[0] == helper.DPKG_QUERY:
                    return 0, "".join("%s\tii \n" % p for p in cmd[4:] if p in inst)
                if cmd[0] == helper.APT_GET:
                    return 0, sim
                return sections(cmd)
            return runner, seen

        def install(runner):
            calls = []
            r, out = run_say(helper.do_install, "beta", games, lambda a: (calls.append(a), (0, []))[1], lambda: 600,
                             runner)
            return r, out, calls

        # the old name installed: the simulation removes it, the install runs without --no-remove
        runner, seen = runner_for({"beta-old"}, "Remv beta-old [0.874]\nInst beta (1.0 walnut)\n")
        r, out, calls = install(runner)
        self.assertNotIsInstance(r, Exception, out)
        self.assertIn([helper.APT_GET, "-s", "-o", "Debug::NoLocking=1", "install", "beta"], seen)
        self.assertEqual(calls, [["install", "beta"]])
        self.assertIn("log beta takes the place of beta-old", out)
        # a removal beyond the former names: refused, nothing installed
        runner, _ = runner_for({"beta-old"}, "Remv beta-old [0.874]\nRemv alpha [1]\nInst beta (1.0 walnut)\n")
        r, out, calls = install(runner)
        self.assertIsInstance(r, helper.Refused)
        self.assertIn("would also remove alpha", str(r))
        self.assertEqual(calls, [])
        # none of the former names installed: no simulation, --no-remove as for any game
        runner, seen = runner_for(set(), "")
        r, out, calls = install(runner)
        self.assertEqual(calls, [["install", "--no-remove", "beta"]])
        self.assertFalse([c for c in seen if c[0] == helper.APT_GET])
        # a bad former name refuses before anything
        bad = dict(games, beta=dict(games["beta"], former_packages=["Bad name"]))
        r, _ = run_say(helper.do_install, "beta", bad, lambda a: (0, []), lambda: 600, sections)
        self.assertIsInstance(r, helper.Refused)

    def test_install_options(self):
        o = helper.APT_OPTS
        self.assertIn("Dpkg::Options::=--force-confold", o)
        self.assertIn("DPkg::Lock::Timeout=60", o)

    def test_status_lines(self):
        self.assertEqual(helper.status_line("dlstatus:1:50.0:Downloading x"), ("progress", "20 Downloading x"))
        self.assertEqual(helper.status_line("pmstatus:alpha:50:Unpacking alpha"), ("progress", "70 Unpacking alpha"))
        self.assertEqual(helper.status_line("pmstatus:alpha:100:Done"), ("progress", "100 Done"))
        self.assertIsNone(helper.status_line("garbage"))
        self.assertIsNone(helper.status_line("pmstatus:a:x:y"))
        self.assertEqual(helper.status_line("pmerror:a:0:oops")[0], "log")

    def test_apt_stream_relays_status_descriptor(self):
        with tempfile.TemporaryDirectory() as d:
            fake = Path(d) / "apt-get"
            fake.write_text("""#!/bin/sh
for a in "$@"; do case "$a" in APT::Status-Fd=*) fd=${a#APT::Status-Fd=};; esac; done
[ -n "$fd" ] || exit 9
echo "args: $*"
echo "dlstatus:1:25.0:Downloading alpha" >&$fd
echo "pmstatus:alpha:100:Configuring alpha" >&$fd
ls /proc/$$/fd | wc -l | sed 's/^/fds: /'
echo "E: something" >&2
exit 7
""")
            fake.chmod(0o755)
            old = helper.APT_GET
            helper.APT_GET = str(fake)
            try:
                (rc, tail), out = run_say(helper.apt_stream, ["install", "--no-remove", "alpha"])
            finally:
                helper.APT_GET = old
        self.assertEqual(rc, 7)
        self.assertIn("progress 10 Downloading alpha", out)
        self.assertIn("progress 100 Configuring alpha", out)
        self.assertTrue(any(l.startswith("log args: -y -o Dpkg::Options::=--force-confold") for l in out))
        self.assertIn("E: something", tail)
        fds = int([l for l in out if l.startswith("log fds:")][0].split()[-1])
        self.assertLessEqual(fds, 5)   # 0 1 2, the status pipe, and ls' own directory handle


def fake_runner(installed, sim, depends=None, log=None):
    """a command runner: dpkg-query status of INSTALLED, apt-get -s answering SIM, Depends text"""
    depends = depends or {}

    def runner(cmd):
        if log is not None:
            log.append(cmd)
        if cmd[0] == helper.DPKG_QUERY and "${Depends}" in cmd:
            return 0, depends.get(cmd[-1], "")
        if cmd[0] == helper.DPKG_QUERY:
            return 0, "".join("%s\t%s\n" % (p, "ii " if p in installed else "un ") for p in cmd[3:])
        if cmd[0] == helper.APT_GET and "-s" in cmd:
            return 0, sim
        return 0, ""
    return runner


class Remove(unittest.TestCase):
    def test_removal_set(self):
        sim = "Reading package lists...\nRemv alpha [1.0]\nRemv ps3-games [0.1] [ps3-desktop:all ]\nInst x\nConf y\n"
        self.assertEqual(helper.removal_set(sim), ["alpha", "ps3-games"])

    def test_cascade_is_refused_and_nothing_changes(self):
        log, calls = [], []
        sim = "Remv beta [1]\nRemv some-library [2]\nRemv another-game [3]\n"
        r, out = run_say(helper.do_remove, "beta", GAMES, fake_runner({"beta"}, sim, log=log),
                         lambda a: (calls.append(a), (0, []))[1])
        self.assertIsInstance(r, helper.Refused)
        self.assertIn("some-library", str(r))
        self.assertEqual(calls, [])
        self.assertFalse([c for c in log if c[0] == helper.APT_MARK])

    def test_own_packages_only(self):
        log, calls = [], []
        sim = "Remv alpha [1]\nRemv alpha-data [1]\n"
        r, out = run_say(helper.do_remove, "alpha", GAMES, fake_runner({"alpha", "alpha-data"}, sim, log=log),
                         lambda a: (calls.append(a), (0, []))[1])
        self.assertIsNone(r)
        self.assertEqual(calls, [["remove", "alpha", "alpha-data"]])
        self.assertFalse([c for c in log if c[0] == helper.APT_MARK])   # no metapackage involved
        self.assertTrue(all("autoremove" not in " ".join(c) for c in log))

    def test_metapackage_dependencies_marked_manual_first(self):
        log, calls = [], []
        sim = "Remv beta [1]\nRemv ps3-games [0.1]\n"
        deps = {"ps3-games": "beta, alpha (>= 1), openarena | openarena-classic, ${misc:Depends}, not-installed"}
        inst = {"beta", "alpha", "openarena", "ps3-games"}
        r, out = run_say(helper.do_remove, "beta", GAMES, fake_runner(inst, sim, deps, log),
                         lambda a: (calls.append(a), log.append(["STREAM"] + a), (0, []))[2])
        self.assertIsNone(r)
        marks = [c for c in log if c[0] == helper.APT_MARK]
        self.assertEqual(marks, [[helper.APT_MARK, "manual", "alpha", "openarena"]])
        self.assertLess(log.index(marks[0]), log.index(["STREAM", "remove", "beta"]))

    def test_not_installed_is_a_noop(self):
        calls = []
        r, out = run_say(helper.do_remove, "beta", GAMES, fake_runner(set(), ""), lambda a: calls.append(a))
        self.assertIsNone(r)
        self.assertEqual(calls, [])
        self.assertEqual(out, ["progress 100 not installed"])

    def test_failed_simulation(self):
        runner = lambda cmd: (0, "beta\tii \n") if cmd[0] == helper.DPKG_QUERY else (100, "E: nope\n")   # noqa: E731
        r, _ = run_say(helper.do_remove, "beta", GAMES, runner, lambda a: (0, []))
        self.assertIsInstance(r, helper.AptFailed)

    SHARED = {
        "dsda-doom": {"id": "dsda-doom", "packages": ["dsda-doom", "freedoom"]},
        "doom": {"id": "doom", "packages": ["dsda-doom"],
                 "data_setup": {"required": ["doom-wad"], "optional": [], "present_any": ["doom-wad"]}},
        "heretic": {"id": "heretic", "packages": ["dsda-doom"],
                    "data_setup": {"required": ["heretic-wad"], "present_any": ["heretic-wad", "heretic-lic"]}},
    }

    def test_shared_engine_stays(self):
        # removing Doom while Freedoom's game is installed: only Doom's data goes, the engine stays
        log, calls = [], []
        inst = {"dsda-doom", "freedoom", "doom-wad"}
        r, out = run_say(helper.do_remove, "doom", self.SHARED, fake_runner(inst, "Remv doom-wad [89]\n", log=log),
                         lambda a: (calls.append(a), (0, []))[1])
        self.assertIsNone(r)
        self.assertEqual(calls, [["remove", "doom-wad"]])
        self.assertIn("log kept for dsda-doom: dsda-doom", out)
        # the data would take more with it (or a metapackage): refused, nothing touched
        calls.clear()
        r, out = run_say(helper.do_remove, "doom", self.SHARED,
                         fake_runner(inst, "Remv doom-wad [89]\nRemv ps3-games [1]\n"), lambda a: calls.append(a))
        self.assertIsInstance(r, helper.Refused)
        self.assertEqual(calls, [])
        # no data installed: nothing of its own to remove, the engine untouched
        r, out = run_say(helper.do_remove, "doom", self.SHARED, fake_runner({"dsda-doom", "freedoom"}, ""),
                         lambda a: calls.append(a))
        self.assertIsNone(r)
        self.assertEqual(calls, [])
        self.assertEqual(out[-1], "progress 100 nothing of this game's own to remove")

    def test_shared_engine_in_use_by_a_data_game(self):
        # Freedoom's game goes while Heretic (its licensed-suite data installed) still uses the engine:
        # freedoom goes, dsda-doom stays
        calls = []
        r, out = run_say(helper.do_remove, "dsda-doom", self.SHARED,
                         fake_runner({"dsda-doom", "freedoom", "heretic-lic"}, "Remv freedoom [1]\n"),
                         lambda a: (calls.append(a), (0, []))[1])
        self.assertIsNone(r)
        self.assertEqual(calls, [["remove", "freedoom"]])
        self.assertIn("log kept for heretic: dsda-doom", out)

    def test_engine_goes_when_no_other_game_uses_it(self):
        # Doom and Heretic without their data don't hold the engine: Freedoom's game removes as before
        calls = []
        r, _ = run_say(helper.do_remove, "dsda-doom", self.SHARED,
                       fake_runner({"dsda-doom", "freedoom"}, "Remv dsda-doom [1]\nRemv freedoom [1]\n"),
                       lambda a: (calls.append(a), (0, []))[1])
        self.assertIsNone(r)
        self.assertEqual(calls, [["remove", "dsda-doom", "freedoom"]])
        bad = dict(self.SHARED, doom=dict(self.SHARED["doom"], data_setup={"required": ["Bad Name"]}))
        self.assertIsInstance(run_say(helper.do_remove, "doom", bad, fake_runner({"dsda-doom", "freedoom"}, ""),
                                      lambda a: (0, []))[0], helper.Refused)

    def test_status(self):
        _, out = run_say(helper.do_status, "alpha", GAMES, fake_runner({"alpha"}, ""))
        self.assertEqual(out, ["status partial"])
        _, out = run_say(helper.do_status, "beta", GAMES, fake_runner({"beta"}, ""))
        self.assertEqual(out, ["status installed"])
        _, out = run_say(helper.do_status, "beta", GAMES, fake_runner(set(), ""))
        self.assertEqual(out, ["status not-installed"])


class Adopt(unittest.TestCase):
    """adopt: the catalog games' packages (and the old metapackages' dependencies) apt marked automatic
    become manual; only installed ones, only those names"""
    GAMES = dict(GAMES, doom={"id": "doom", "packages": ["dsda-doom"],
                              "data_setup": {"required": ["doom-wad"], "optional": ["doom-music"],
                                             "present_any": ["doom-wad", "doom-lic"]}})
    STATES = ("Package: alpha\nArchitecture: ppc64\nAuto-Installed: 1\n\n"
              "Package: beta\nArchitecture: ppc64\nAuto-Installed: 0\n\n"
              "Package: dsda-doom\nArchitecture: ppc64\nAuto-Installed: 1\n\n"
              "Package: doom-lic\nArchitecture: all\nAuto-Installed: 1\n\n"
              "Package: classicube\nArchitecture: ppc64\nAuto-Installed: 1\n\n"
              "Package: libfoo1\nArchitecture: ppc64\nAuto-Installed: 1\n")

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.states = Path(self.tmp.name) / "extended_states"
        self.states.write_text(self.STATES)

    def adopt(self, inst, deps=None, rc=0):
        log = []
        base = fake_runner(inst, "", deps, log)

        def runner(cmd):
            if cmd[0] == helper.APT_MARK:
                log.append(cmd)
                return rc, "E: locked\n" if rc else ""
            return base(cmd)
        r, out = run_say(helper.do_adopt, self.GAMES, runner, str(self.states))
        return r, out, [c for c in log if c[0] == helper.APT_MARK]

    def test_auto_marked(self):
        self.assertEqual(helper.auto_marked(str(self.states)), {"alpha", "dsda-doom", "doom-lic", "classicube", "libfoo1"})
        self.assertEqual(helper.auto_marked(str(Path(self.tmp.name) / "none")), set())

    def test_marks_installed_automatic_game_packages(self):
        r, out, marks = self.adopt({"alpha", "beta", "dsda-doom", "doom-lic", "libfoo1"})
        self.assertIsNone(r, out)
        # beta is manual already, alpha-data and classicube not installed, libfoo1 is no game's
        self.assertEqual(marks, [[helper.APT_MARK, "manual", "alpha", "dsda-doom", "doom-lic"]])

    def test_nothing_to_adopt_changes_nothing(self):
        r, out, marks = self.adopt({"beta"})
        self.assertIsNone(r)
        self.assertEqual(marks, [])
        self.assertEqual(out, ["progress 100 nothing to adopt"])

    def test_old_metapackage_dependencies(self):
        # a draft the catalog doesn't list (classicube), pulled in by the old ps3-games
        deps = {"ps3-games": "ps3-games-integration (= 0.9), classicube, alpha, ${misc:Depends}"}
        r, out, marks = self.adopt({"ps3-games", "classicube", "alpha"}, deps)
        self.assertIsNone(r, out)
        self.assertEqual(marks, [[helper.APT_MARK, "manual", "alpha", "classicube"]])

    def test_apt_mark_failure(self):
        r, out, marks = self.adopt({"alpha"}, rc=100)
        self.assertIsInstance(r, helper.AptFailed)
        self.assertIn("error apt-mark manual failed: E: locked", out)

    def test_app_runs_it_and_ignores_failures(self):
        calls = []

        class P:
            returncode = 1
        self.assertFalse(app.adopt_games("/nonexistent/helper", lambda *a, **k: calls.append(a)))
        self.assertEqual(calls, [])                                  # no helper: no sudo
        h = str(self.states)                                         # any existing file stands for it
        self.assertFalse(app.adopt_games(h, lambda cmd, **k: (calls.append(cmd), P())[1]))
        self.assertEqual(calls, [["sudo", "-n", h, "adopt"]])

        def boom(cmd, **k):
            raise subprocess.TimeoutExpired(cmd, 1)
        self.assertFalse(app.adopt_games(h, boom))


class AppFunctions(unittest.TestCase):
    def test_parse_exec(self):
        self.assertEqual(app.parse_exec("openarena %U"), ["openarena"])
        self.assertEqual(app.parse_exec("xmoto -fs %f"), ["xmoto", "-fs"])
        self.assertEqual(app.parse_exec('sh -c "echo hi there" %F'), ["sh", "-c", "echo hi there"])
        self.assertEqual(app.parse_exec("game --file=%f --x 100%%"), ["game", "--file=", "--x", "100%"])
        self.assertEqual(app.parse_exec("ps3-game /usr/games/ioquake3 +set fs_game baseoa"),
                         ["ps3-game", "/usr/games/ioquake3", "+set", "fs_game", "baseoa"])
        self.assertEqual(app.parse_exec("a %i %c %k %d %D %n %N %u %v %m"), ["a"])

    def entry(self, d, name, exec_, extra=""):
        p = Path(d) / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("[Desktop Action x]\nExec=wrong\n\n[Desktop Entry]\nName=X\nExec=%s\n%s\n" % (exec_, extra))

    def test_play_prefers_the_runtime_copy(self):
        with tempfile.TemporaryDirectory() as d:
            rt, apps = Path(d) / "run", Path(d) / "apps"
            self.entry(rt / "applications", "alpha.desktop", "ps3-game alpha --fs %U", "Path=/tmp")
            self.entry(apps, "alpha.desktop", "alpha %U")
            self.assertEqual(app.play_commands(GAMES["alpha"], str(rt), str(apps)),
                             [("X", ["ps3-game", "alpha", "--fs"], "/tmp")])

    def test_play_falls_back_to_ps3_game(self):
        with tempfile.TemporaryDirectory() as d:
            apps = Path(d) / "apps"
            self.entry(apps, "alpha.desktop", "/usr/games/alpha -f %f")
            self.entry(apps, "beta.desktop", "ps3-game beta")
            self.assertEqual(app.play_commands(GAMES["alpha"], str(Path(d) / "none"), str(apps)),
                             [("X", ["ps3-game", "/usr/games/alpha", "-f"], None)])
            self.assertEqual(app.play_commands(GAMES["beta"], None, str(apps)), [("X", ["ps3-game", "beta"], None)])
            self.assertEqual(app.play_commands({"desktop": ["missing.desktop"]}, None, str(apps)), [])

    def test_play_every_launcher_in_order(self):
        # two renderers: both offered, in the catalog's order, labelled with their Names
        with tempfile.TemporaryDirectory() as d:
            apps = Path(d) / "apps"
            (apps).mkdir()
            (apps / "g-gl2.desktop").write_text("[Desktop Entry]\nName=G (GL 2)\nExec=ps3-game g +r 2\n")
            (apps / "g-gl1.desktop").write_text("[Desktop Entry]\nName=G (GL 1)\nExec=ps3-game g +r 1\n")
            got = app.play_commands({"desktop": ["g-gl1.desktop", "g-gl2.desktop"]}, None, str(apps))
            self.assertEqual([c[0] for c in got], ["G (GL 1)", "G (GL 2)"])

    def test_play_from_the_manifest(self):
        # no catalog entry: the package's own entry (the diverted one is missing), and the shown
        # entries that run its program (OpenArena's two renderers in ps3-desktop)
        with tempfile.TemporaryDirectory() as d:
            rt, apps = Path(d) / "run", Path(d) / "apps"
            apps.mkdir()
            (apps / "oa-b.desktop").write_text("[Desktop Entry]\nName=B\nExec=ps3-game openarena +b\n")
            (apps / "oa-a.desktop").write_text("[Desktop Entry]\nName=A\nExec=env X=1 openarena +a\n")
            (apps / "oa-hidden.desktop").write_text("[Desktop Entry]\nName=H\nExec=openarena\nNoDisplay=true\n")
            (apps / "other.desktop").write_text("[Desktop Entry]\nName=O\nExec=ps3-game other\n")
            (apps / "zz-tool.desktop").write_text("[Desktop Entry]\nName=T\nExec=oa-tool\n")
            self.entry(rt / "applications", "oa-a.desktop", "ps3-game env X=1 openarena +a")
            shipped = ["/usr/games/openarena", "/usr/share/applications/openarena.desktop",
                       "/usr/share/applications/zz-tool.desktop", "/usr/share/doc/openarena"]
            g = {"packages": ["openarena"]}
            got = app.play_commands(g, str(rt), str(apps), files=lambda: [
                f.replace("/usr/share/applications", str(apps)) for f in shipped])
            self.assertEqual([c[0] for c in got], ["T", "X", "B"])
            self.assertEqual(got[1][1], ["ps3-game", "env", "X=1", "openarena", "+a"])
            self.assertEqual(app.play_commands(g, str(rt), str(apps)), [])

    def test_play_keeps_the_games_own_stamped_entries(self):
        # games sharing an engine (dsda-doom: Freedoom and Doom): an entry stamped X-Walnut-Game is only
        # its game's; unstamped entries count for any game, as before; a game without an id sees all
        with tempfile.TemporaryDirectory() as d:
            apps = Path(d) / "apps"
            apps.mkdir()
            (apps / "dsda-doom-gl.desktop").write_text("[Desktop Entry]\nName=F\nExec=ps3-game dsda-doom -iwad f\n"
                                                       "X-Walnut-Game=dsda-doom\n")
            (apps / "doom-dsda.desktop").write_text("[Desktop Entry]\nName=D\nExec=ps3-game dsda-doom -iwad d\n"
                                                    "X-Walnut-Game=doom\n")
            (apps / "plain.desktop").write_text("[Desktop Entry]\nName=P\nExec=ps3-game dsda-doom\n")
            files = lambda: ["/usr/games/dsda-doom"]   # noqa: E731
            label = lambda g: [c[0] for c in app.play_commands(g, None, str(apps), files=files)]   # noqa: E731
            self.assertEqual(label({"id": "doom", "packages": ["dsda-doom"]}), ["D", "P"])
            self.assertEqual(label({"id": "dsda-doom", "packages": ["dsda-doom", "freedoom"]}), ["F", "P"])
            self.assertEqual(label({"packages": ["dsda-doom"]}), ["D", "F", "P"])
            self.assertEqual([c[0] for c in app.play_commands({"id": "doom", "desktop": ["dsda-doom-gl.desktop"]},
                                                              None, str(apps))], [])

    def test_data_present(self):
        g = {"id": "quakespasm", "packages": ["quakespasm"],
             "data_setup": {"required": ["quake-shareware", "quake-registered"], "optional": ["quake-music"],
                            "present_any": ["quake-shareware", "quake-registered", "quake-lic"]}}
        self.assertEqual(app.data_packages(g), ["quake-shareware", "quake-registered", "quake-lic", "quake-music"])
        self.assertFalse(app.data_present(g, {"quakespasm", "quake-music"}))
        self.assertTrue(app.data_present(g, {"quake-registered"}))
        self.assertTrue(app.data_present(g, {"quake-lic"}))          # the licensed suite's build of the data
        self.assertIsNone(app.data_present({"packages": ["x"]}, set()))
        self.assertIsNone(app.data_present({"data_setup": {"required": [], "optional": ["ttd"], "present_any": []}}, set()))

    def test_entry_program(self):
        # a full path stays one (matched against the package's files), a bare name is matched by name
        self.assertEqual(app.entry_program(["ps3-game", "env", "A=1", "B=2", "/usr/games/oa", "+set", "x=1"]), "/usr/games/oa")
        self.assertEqual(app.entry_program(["ps3-game", "/usr/lib/yamagi-quake2/quake2", "-datadir", "/x"]),
                         "/usr/lib/yamagi-quake2/quake2")
        self.assertEqual(app.entry_program(["oa", "--opt=1"]), "oa")
        self.assertIsNone(app.entry_program(["ps3-game"]))

    def test_package_files(self):
        out = "/.\n/usr\n/usr/games/openarena\npackage diverts others to: /x\n"
        self.assertEqual(app.package_files(["openarena"], lambda c: out), ["/.", "/usr", "/usr/games/openarena"])
        self.assertEqual(app.package_files([]), [])

    def test_launchers_best_first(self):
        gold = {"name": "G (GL 1)", "rating": {"medal": "Gold", "score": 84, "graded_by": "session"}}
        silver = {"name": "G (GL 2)", "rating": {"medal": "Silver", "score": 71, "graded_by": "demo"}}
        self.assertEqual(app.launcher_label(gold), "G (GL 1) \u00b7 Gold 84")
        self.assertEqual(app.launcher_label({"name": None, "rating": {"medal": "Untested", "graded_by": None}}),
                         "Default launcher \u00b7 not played yet")
        cmds = [("G (GL 2)", ["ps3-game", "g", "2"], None), ("G (safe)", ["ps3-game", "g", "s"], None),
                ("G (GL 1)", ["ps3-game", "g", "1"], "/tmp")]
        self.assertEqual(app.order_launchers(cmds, [gold, silver]),
                         [("G (GL 1) \u00b7 Gold 84", ["ps3-game", "g", "1"], "/tmp"),
                          ("G (GL 2) \u00b7 Silver 71", ["ps3-game", "g", "2"], None),
                          ("G (safe)", ["ps3-game", "g", "s"], None)])
        self.assertEqual(app.order_launchers(cmds[:1], None), cmds[:1])

    def test_tested_text(self):
        t = {"walnut": "0.1", "package": "xmoto", "version": "0.6.3+repack-1", "date": "2026-10-24"}
        self.assertEqual(app.tested_text({"tested": t}), "Tested on Walnut 0.1 with xmoto 0.6.3+repack-1 (2026-10-24)")
        self.assertEqual(app.tested_text({"tested": dict(t, walnut="pre-0.1", version=None)}),
                         "Tested on a build before Walnut 0.1 (2026-10-24)")
        self.assertEqual(app.tested_text({"tested": None}), "")
        self.assertEqual(app.tested_text({}), "")

    def test_dpkg_installed(self):
        out = "alpha\tii \nbeta\trc \ngamma\thi \n"
        self.assertEqual(app.dpkg_installed({"alpha", "beta", "gamma", "delta"}, lambda c: out), {"alpha", "gamma"})
        self.assertEqual(app.dpkg_installed(set()), set())

    def test_filters(self):
        games = [
            {"title": "Zed", "summary": "racing fun", "categories": ["racing"], "tags": ["kart"], "packages": ["zed"],
             "patched": True, "rating": {"medal": "Gold", "score": 85}},
            {"title": "Abe", "summary": "shooter", "categories": ["shooter"], "tags": [], "packages": ["abe"],
             "patched": False, "rating": {"medal": "Untested", "score": None}},
            {"title": "Mid", "summary": "a shooter too", "categories": ["shooter"], "tags": ["fps"], "packages": ["mid"],
             "patched": False, "rating": {"medal": "Bronze", "score": 62}}]
        names = lambda **kw: [g["title"] for g in app.filter_games(games, {"mid"}, **kw)]   # noqa: E731
        self.assertEqual(names(), ["Zed", "Mid", "Abe"])
        self.assertEqual(names(sort="Name"), ["Abe", "Mid", "Zed"])
        self.assertEqual(names(text="SHOOTER"), ["Mid", "Abe"])
        self.assertEqual(names(text="shooter fps"), ["Mid"])
        self.assertEqual(names(category="racing"), ["Zed"])
        self.assertEqual(names(medal="Untested"), ["Abe"])
        self.assertEqual(names(installed_only=True), ["Mid"])

    def test_helper_lines(self):
        self.assertEqual(app.parse_helper_line("progress 42 installing x\n"), ("progress", 42, "installing x"))
        self.assertEqual(app.parse_helper_line("progress 400 x"), ("progress", 100, "x"))
        self.assertIsNone(app.parse_helper_line("progress nope"))
        self.assertEqual(app.parse_helper_line("error refused: x"), ("error", "refused: x"))
        self.assertEqual(app.parse_helper_line("sudo: a password is required"), ("log", "sudo: a password is required"))
        self.assertIsNone(app.parse_helper_line("\n"))

    def test_screenshots_and_colours(self):
        g = {"tests": [{"date": "2026-10-20", "ps3": ["rating/sessions/t/ps3-40s.jpg"], "pc": ["rating/sessions/t/pc-30s.jpg"]}]}
        s = app.screenshots(g)
        self.assertEqual([r for _, r in s], ["rating/sessions/t/ps3-40s.jpg"])   # the PS3's pictures only
        self.assertEqual(s[0][0], "Tested 2026-10-20")
        self.assertEqual(app.pill_text_color(app.MEDAL_COLORS["Gold"]), "#14304d")
        self.assertEqual(app.pill_text_color(app.MEDAL_COLORS["Scrap"]), "#f4f4f4")
        for m in app.MEDAL_ORDER:
            self.assertIn(m, app.MEDAL_COLORS)

    def test_the_packaged_catalog_of_the_repository(self):
        sys.path.insert(0, str(REPO / "tools" / "gamedb"))
        import catalog
        cat = catalog.build(REPO / "website" / "gamedb" / "content" / "games", REPO)
        for g in cat["games"]:
            helper.game_packages(g["id"], {x["id"]: x for x in cat["games"]})   # the helper accepts every entry
            self.assertIn(app.medal_of(g), app.MEDAL_ORDER)


QS = {"id": "quakespasm", "title": "QuakeSpasm", "packages": ["quakespasm"], "data_setup": {
    "original": "Quake",
    "gdp": "quake", "required": ["quake-shareware", "quake-registered"], "optional": ["quake-music"], "downloads": ["quake-registered"],
    "fetch": ["quake-shareware"], "hint": "Quake's id1/pak0.pak.",
    "editions": {"quake-shareware": "shareware", "quake-registered": "full", "quake-music": "music"},
    "present_any": ["quake-shareware", "quake-registered", "quake-licensed-data"]}}
TTD = {"id": "openttd", "title": "OpenTTD", "packages": ["openttd"], "data_setup": {
    "original": "Transport Tycoon Deluxe",
    "gdp": "ttd", "required": [], "optional": ["transport-tycoon-deluxe-data", "transport-tycoon-deluxe-music"],
    "downloads": [], "hint": "TTD for Windows.", "present_any": []}}


class DataSetup(unittest.TestCase):
    def test_buttons_for_each_state(self):
        A = app.data_actions
        self.assertEqual(A(QS, set(), set()), ["setup"])                              # missing
        self.assertEqual(A(QS, {"quake-registered"}, set()), [])                       # present
        # only the shareware: Play, and the same button for the full game, which replaces it
        self.assertEqual(A(QS, {"quake-shareware"}, set()), ["full"])
        self.assertEqual(A(QS, {"quake-shareware", "quake-registered"}, set()), [])
        demo_only = dict(QS, data_setup=dict(QS["data_setup"], required=["quake-shareware"]))
        self.assertEqual(A(demo_only, {"quake-shareware"}, set()), [])                 # no full edition to add
        self.assertEqual(app.data_note(QS, True, True)[0], "OPTIONAL:")
        self.assertIn("full game from the original Quake", app.data_note(QS, True, True)[1])
        self.assertEqual(A(QS, {"quake-licensed-data"}, set()), [])                    # the licensed build counts
        self.assertEqual(A(QS, set(), {"quake-licensed-data"}), ["repo", "setup"])     # the repository has it
        self.assertEqual(A(QS, set(), set(), importing="quakespasm"), ["importing"])
        self.assertEqual(A(QS, set(), set(), importing="openttd"), ["setup"])          # another game's import
        self.assertEqual(A(TTD, set(), set()), ["add"])                                # optional data only
        self.assertEqual(A(TTD, {"transport-tycoon-deluxe-music"}, set()), [])
        self.assertEqual(A({"id": "x", "title": "X", "packages": ["x"]}, set(), set()), [])
        self.assertEqual(app.data_title(QS), "Quake")
        self.assertEqual(app.data_title({"title": "Zed", "data_setup": {"gdp": "zed"}}), "Zed")
        self.assertEqual(app.data_note(QS, True)[0], "REQUIRED:")
        self.assertEqual(app.data_note(QS, True)[1], "the files from your copy of the original Quake")
        self.assertEqual(app.data_note(TTD, False), ("OPTIONAL:", "the original Transport Tycoon Deluxe graphics and sound"))

    def test_apt_policy(self):
        out = ("quake-licensed-data:\n  Installed: (none)\n  Candidate: 1.06-1\n  Version table:\n"
               "     1.06-1 500\n        500 https://walnut.nanobyte.dev/apt licensed/main ppc64 Packages\n"
               "quake-shareware:\n  Installed: (none)\n  Candidate: (none)\n"
               # installed, from no source: its own candidate, not the repository's
               "quake2-demo-data:\n  Installed: 89\n  Candidate: 89\n  Version table:\n *** 89 100\n"
               "        100 /var/lib/dpkg/status\n"
               "rct2-data:\n  Installed: 1\n  Candidate: 2\n  Version table:\n     2 500\n"
               "        500 https://walnut.nanobyte.dev/apt licensed/main ppc64 Packages\n *** 1 100\n"
               "        100 /var/lib/dpkg/status\n")
        self.assertEqual(app.parse_apt_policy(out), {"quake-licensed-data", "rct2-data"})
        self.assertEqual(app.repo_candidates({"quake-licensed-data", "x"}, runner=lambda c: out), {"quake-licensed-data"})
        self.assertEqual(app.repo_candidates(set(), runner=lambda c: 1 / 0), set())

        def missing(c):
            raise OSError("no apt-cache")
        self.assertEqual(app.repo_candidates({"a"}, runner=missing), set())

    def test_editions_and_the_remove_question(self):
        calls = []

        def dpkg(c):
            calls.append(c)
            return ("quake-shareware\tii \tshareware\t18300\nquake-registered\tii \tfull\t1048576\n"
                    "quake-licensed-data\tii \t\t500\nother\tun \tfull\t9\n")
        info = app.data_info({"quake-shareware", "quake-registered", "quake-licensed-data", "other"}, runner=dpkg)
        self.assertIn("${X-Walnut-Edition}", calls[0][3])
        self.assertEqual(info, {"quake-shareware": ("shareware", 18300), "quake-registered": ("full", 1048576),
                                "quake-licensed-data": ("", 500)})
        self.assertEqual(app.edition_text(QS, info), "Full game")                  # full wins over shareware
        self.assertEqual(app.edition_text(QS, {"quake-shareware": ("shareware", 1)}), "Shareware")
        self.assertEqual(app.edition_text(QS, {"quake-licensed-data": ("", 1)}), "")   # the licensed suite's: nothing
        self.assertEqual(app.edition_text(TTD, {"transport-tycoon-deluxe-data": ("full", 1),
                                                "transport-tycoon-deluxe-music": ("music", 1)}), "Full game, Music")
        q, names = app.remove_question(QS, {"quake-shareware": ("shareware", 18300)})
        self.assertEqual(names, ["quake-shareware"])
        self.assertIn("also removes your imported game data (quake-shareware: 18 MB)", q)
        self.assertIn("you'd need the disc or your copy again", q)
        self.assertEqual(app.remove_question(QS, {}), ("Remove QuakeSpasm?", []))
        self.assertEqual(app.human_size(1258291), "1.2 GB")

    def test_sources(self):
        with tempfile.TemporaryDirectory() as d:
            home, media = Path(d, "home"), Path(d, "media")
            for sub in ("Downloads", "GOG Games/Quake", ".steam/steam/steamapps/common/Quake",
                        ".local/share/Steam/steamapps/common/RollerCoaster Tycoon 2", ".hidden-dir"):
                (home / sub).mkdir(parents=True)
            (media / "QUAKE").mkdir(parents=True)
            (media / "USBSTICK").mkdir()
            for i, name in enumerate(["Downloads/quake106.zip", "Downloads/setup_quake_2.0.exe", "Downloads/notes.txt",
                                      "Downloads/gog_rct2_1.sh", "Downloads/script.sh", "RCT2.ISO", "home.img",
                                      "Downloads/QUAKE106.cue", "Downloads/QUAKE106.bin", "Downloads/Q3.mds",
                                      "Downloads/Q3.mdf"]):
                f = home / name
                f.write_bytes(b"x")
                os.utime(f, (1000 + i, 1000 + i))
            disc = ("QUAKE101", str(media / "QUAKE"))       # the disc, mounted: listed once
            got = app.find_sources(str(home), "u", disc, [str(media)])
            self.assertEqual([l for l, _ in got], [
                "Disc in the drive: QUAKE101", "Disc or drive: USBSTICK", "GOG: Quake", "Steam: Quake",
                "Steam: RollerCoaster Tycoon 2", "Disc image: Q3.mdf", "Disc image: QUAKE106.cue", "Disc image: home.img", "Disc image: RCT2.ISO",
                "File: gog_rct2_1.sh",
                "File: setup_quake_2.0.exe", "File: quake106.zip"])
            self.assertEqual(got[0][1], str(media / "QUAKE"))
            # a disc that isn't mounted: walnut-data mounts the drive
            self.assertEqual(app.find_sources(str(home), "u", ("", None), [])[0], ("Disc in the drive", "/dev/sr0"))
            self.assertEqual(app.find_sources(str(Path(d, "nobody")), "u", None, []), [])

    def test_lsblk(self):
        j = '{"blockdevices": [{"name": "sr0", "label": "QUAKE101", "mountpoint": null, "type": "rom", "size": 612345856}]}'
        self.assertEqual(app.parse_lsblk_disc(j), ("QUAKE101", None))
        self.assertIsNone(app.parse_lsblk_disc('{"blockdevices": [{"name": "sr0", "type": "rom", "size": 0}]}'))
        self.assertIsNone(app.parse_lsblk_disc('{"blockdevices": [{"name": "sr0", "type": "rom", "size": null}]}'))
        self.assertEqual(app.parse_lsblk_disc(
            '{"blockdevices": [{"name": "sr0", "label": "X", "mountpoint": "/media/u/X", "type": "rom", "size": "5"}]}'),
            ("X", "/media/u/X"))
        self.assertIsNone(app.parse_lsblk_disc(""))

    def test_progress_lines(self):
        P = app.parse_import_line
        self.assertEqual(P("gdp step searching /media/u/QUAKE\n"), ("step", "searching /media/u/QUAKE"))
        self.assertEqual(P("gdp file id1/pak0.pak 18689235"), ("file", "id1/pak0.pak", 18689235))
        self.assertEqual(P("gdp file My Game/a b.dat -1"), ("file", "My Game/a b.dat", -1))
        self.assertEqual(P("gdp generated /c/out/quake-shareware_89_all.deb"),
                         ("generated", "/c/out/quake-shareware_89_all.deb"))
        self.assertEqual(P("gdp error not-found none of the files were found"),
                         ("gdp-error", "not-found", "none of the files were found"))
        self.assertEqual(P("progress 40 installing quake-shareware"), ("progress", 40, "installing quake-shareware"))
        self.assertEqual(P("log mounted /dev/sr0 at /media/u/Q"), ("log", "mounted /dev/sr0 at /media/u/Q"))
        self.assertEqual(P("error busy: another import is running (doom)"), ("error", "busy: another import is running (doom)"))
        self.assertIsNone(P(""))
        v = app.ImportView("quakespasm", t0=0)
        for line in ("gdp step searching /m", "gdp file quake106.zip 9094045", "log one", "log two", "log three",
                     "gdp error needs-download pak0.pak needs a download", "error game-data-packager made no package"):
            v.feed(P(line))
        self.assertEqual(v.step, "Searching /m")
        self.assertEqual(v.file, "quake106.zip (9 MB)")
        self.assertEqual(v.tail, ["three", "pak0.pak needs a download", "game-data-packager made no package"])
        self.assertEqual(v.kind(), "needs-download")
        self.assertEqual(P("warning missing quake-extra: no such file"), ("missing", "quake-extra", "no such file"))
        self.assertEqual(P("warning missing p"), ("missing", "p", ""))
        v = app.ImportView("final-doom", t0=0)
        for line in ("gdp error needs-download tnt-wad: its official patch files have to be downloaded",
                     "warning missing plutonia-wad: game-data-packager did not make it", "log done"):
            v.feed(P(line))
        self.assertEqual((v.download, v.missing), (["tnt-wad"], [("plutonia-wad", "game-data-packager did not make it")]))
        self.assertEqual(app.partial_notice(v.download, v.missing),
                         "TNT: Evilution needs its official fix file (tnt31.wad) from the network: Try again with download\n"
                         "Not on this copy: The Plutonia Experiment (game-data-packager did not make it)")
        self.assertEqual(app.partial_notice([], []), "")
        miss = [("ttd-music", "no"), ("tnt-wad", "no")]
        alt = [["ttd-music", "ttd-music-fm"]]
        self.assertEqual(app.drop_alternatives(miss, alt, {"ttd-music-fm"}), [("tnt-wad", "no")])
        self.assertEqual(app.drop_alternatives(miss, alt, set()), miss)
        self.assertEqual(app.drop_alternatives(miss, None, {"ttd-music-fm"}), miss)
        self.assertEqual(app.partial_notice(["quake-registered"], []),
                         "Quake Registered needs its official patch files from the network: Try again with download")
        self.assertEqual(P("addon available quake-music"), ("addon", "quake-music"))
        v = app.ImportView("quakespasm", t0=0)
        for line in ("addon available quake-music", "addon available quake-music", "log x"):
            v.feed(P(line))
        self.assertEqual(v.addons, ["quake-music"])
        self.assertEqual(app.addon_label(QS, "quake-music"), "Also add: Music (music)")
        self.assertEqual(app.addon_label({"data_setup": {"gdp": "q", "required": ["quake-rerelease"]}},
                                         "quake-rerelease-armagon"), "Also add: Armagon")
        self.assertEqual(app.elapsed_text(75), "1:15")
        self.assertEqual(app.elapsed_text(3725), "1:02:05")
        self.assertEqual(app.import_command("quakespasm", "/m", ["quake-registered"], "wd"),
                         ["wd", "import", "-p", "quake-registered", "quakespasm", "/m"])

    def test_error_messages(self):
        M = app.import_error_message
        self.assertEqual(M("not-found", "Quake"), "No Quake files found here.")
        self.assertEqual(M("unknown-version", "Quake"), "This copy is a version Walnut doesn't know yet.")
        self.assertEqual(M("needs-download", "Quake II"), "Quake II needs its official patch files: connect to the "
                         "network and try again, or add the patch file.")
        d = "hexen.wad Hexen 1.0: Hexen 1.1 (registered) is required"
        self.assertEqual(M("unsuitable", "Hexen", detail=d),
                         "This copy is Hexen 1.0, which Hexen can't use: Hexen 1.1 (registered) is required")
        self.assertEqual(M("unsuitable", "Hexen", detail="odd"), "This copy is a release Hexen can't use: odd")
        self.assertEqual(M("unsuitable", "Hexen"), "This copy is a release Hexen can't use.")
        self.assertEqual(app.unsuitable_parts("doom2.wad Doom2 1.7: Doom 2 v1.9 is required"),
                         ("Doom2 1.7", "Doom 2 v1.9 is required"))
        v = app.ImportView("hexen", t0=0)
        v.feed(app.parse_import_line("gdp error unsuitable " + d))
        self.assertEqual((v.kind(), v.gdp_text), ("unsuitable", d))
        self.assertTrue(M("no-space", "Quake").startswith("Not enough space"))
        e = ["not enough space: importing this data needs about 3.7 GB free on the disk of /c, and 2.9 GB is free"]
        self.assertEqual(app.import_error_kind(None, e), "no-space")
        self.assertEqual(M("no-space", "Quake", errors=e),
                         "Not enough space: importing this data needs about 3.7 GB free on the disk of /c, and 2.9 GB is free.")
        self.assertEqual(M("failed", "Quake", ["a", "b", "c", "d"]), "The import failed.\nb\nc\nd")
        self.assertEqual(M("failed", "Quake"), "The import failed.")
        self.assertEqual(app.import_error_kind(None, ["busy: another import is running"]), "busy")
        self.assertEqual(app.import_error_kind(None, ["helper refused"]), "failed")
        self.assertEqual(app.import_error_kind("unknown-version", ["x"]), "unknown-version")

    def test_import_lock(self):
        sys.path.insert(0, str(PKG / "files/bin"))
        loader = importlib.machinery.SourceFileLoader("walnut_data", str(PKG / "files/bin/walnut-data"))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        wd = importlib.util.module_from_spec(spec)
        loader.exec_module(wd)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "import.lock")
            self.assertIsNone(app.import_lock_state(path))
            fd = wd.take_import_lock(d, "quakespasm")
            try:
                self.assertEqual(app.import_lock_state(path), (os.getpid(), "quakespasm"))
            finally:
                os.close(fd)
            self.assertIsNone(app.import_lock_state(path))


class PaneActions(unittest.TestCase):
    """the pane's main button and the smaller ones, the glance's numbers (PLAN 5.11)"""

    def test_main_button_in_every_state(self):
        M = app.main_action
        self.assertIsNone(M(None, False, True, [], 0, True, None))
        self.assertEqual(M(QS, False, True, ["setup"], 0, True, False), ("install", "Install", True))
        self.assertEqual(M(QS, False, False, [], 0, True, None), ("install", "Install", False))   # web-only game
        self.assertEqual(M(QS, False, True, [], 0, False, None), ("install", "Install", False))   # helper busy
        self.assertEqual(M(QS, True, True, ["setup"], 1, True, False), ("add-data", "Add game files\u2026", True))
        self.assertEqual(M(QS, True, True, ["repo", "setup"], 1, False, False)[:2], ("add-data", "Add game files\u2026"))
        self.assertFalse(M(QS, True, True, ["setup"], 1, False, False)[2])
        self.assertEqual(M(QS, True, True, ["importing"], 1, False, False), ("importing", "Importing\u2026", False))
        self.assertEqual(M(QS, True, True, [], 1, True, True), ("play", "Play", True))
        self.assertEqual(M(QS, True, True, ["full"], 2, True, True), ("play", "Play \u25be", True))  # a menu
        self.assertEqual(M(TTD, True, True, ["add"], 1, True, None), ("play", "Play", True))   # optional data only
        self.assertEqual(M(QS, True, True, [], 0, True, True), ("play", "Play", False))        # no launcher found
        self.assertEqual(M(QS, True, True, [], 1, True, False), ("play", "Play", False))       # data missing
        self.assertEqual(M(QS, True, True, [], 1, False, True), ("play", "Play", False))       # helper busy

    def test_smaller_buttons(self):
        S = app.side_actions
        self.assertEqual(S(False, [], "install", []), [])
        self.assertEqual(S(False, ["repo", "setup"], "install", []), ["add-data", "repo"])     # data before the game
        self.assertEqual(S(True, ["repo", "setup"], "add-data", []), ["repo", "remove"])      # the main one adds it
        self.assertEqual(S(True, ["add"], "play", ["x"]), ["add-data", "remove", "remove-data"])
        self.assertEqual(S(True, ["full"], "play", ["quake-shareware"], retry=True),
                         ["add-data", "retry", "remove", "remove-data"])
        self.assertEqual(S(True, [], "play", []), ["remove"])
        self.assertEqual(S(False, [], "install", ["quake-registered"]), ["remove-data"])      # data, no game

    def test_remove_data_question(self):
        info = {"quake-registered": ("full", 20480), "quake-music": ("music", 1024)}
        q, names = app.remove_data_question(QS, info)
        self.assertEqual(names, ["quake-registered", "quake-music"])
        self.assertIn("QuakeSpasm (quake-registered, quake-music: 21 MB)", q)
        self.assertIn("The game stays installed", q)

    def test_glance_numbers(self):
        r = {"aspects": {"frame": {"score": 5, "detail": "median 24 fps, 1% low 5"},
                         "smooth": {"score": 58.4, "detail": "1.2 s of stutter"}}}
        self.assertEqual(app.glance_numbers(r, 650 * 1024), [("24 fps", "median frame rate"), ("58/100", "smoothness"),
                                                            ("650 MB", "installed size")])
        self.assertEqual(app.glance_numbers({"aspects": {"frame": {"score": 40, "detail": "?"}}}),
                         [("40/100", "frame rate")])
        self.assertEqual(app.glance_numbers({"medal": "Untested", "aspects": {}}), [])

    def test_apt_sizes(self):
        out = ("Package: openarena\nVersion: 1\nInstalled-Size: 2048\n\nPackage: openarena-data\n"
               "Installed-Size: 409600\nDescription: x\n\nPackage: odd\nInstalled-Size: lots\n")
        self.assertEqual(app.parse_apt_sizes(out), {"openarena": 2048, "openarena-data": 409600})
        seen = []
        self.assertEqual(app.package_sizes({"b", "a"}, runner=lambda c: seen.append(c) or out)["openarena"], 2048)
        self.assertEqual(seen[0][1:], ["show", "--no-all-versions", "a", "b"])
        self.assertEqual(app.package_sizes(set()), {})


class Links(unittest.TestCase):
    """walnut:// links of the Details pages, the one walnut:// link taken from outside, the tabs"""

    def test_walnut_links(self):
        P = app.parse_walnut_link
        self.assertEqual(P("walnut://install/openarena"), ("install", "openarena", None))
        self.assertEqual(P("walnut://add-data/quakespasm"), ("add-data", "quakespasm", None))
        self.assertEqual(P("walnut://play/openarena"), ("play", "openarena", None))
        self.assertEqual(P("walnut://play/openarena?launcher=OpenArena%20(GL%201.2)"),
                         ("play", "openarena", "OpenArena (GL 1.2)"))
        self.assertEqual(P("walnut://install/openarena?launcher=x"), ("install", "openarena", None))
        for bad in ("walnut://remove/openarena", "walnut://install/", "walnut://install/../x", "walnut://install/A",
                    "walnut://install/a/b", "walnut://game/openarena", "http://install/openarena", "walnut:install/x",
                    "", "walnut://[bad"):
            self.assertIsNone(P(bad), bad)

    def test_only_game_links_from_outside(self):
        E = app.external_game
        self.assertEqual(E("walnut://game/openarena"), "openarena")
        self.assertEqual(E("walnut://game/openarena/"), "openarena")
        for bad in ("walnut://install/openarena", "walnut://play/openarena", "walnut://add-data/doom",
                    "walnut://game/", "walnut://game/../../etc", "walnut://game/Open", "walnut://game/a?x=1",
                    "walnut://game/a#b", "https://walnut.nanobyte.dev/gamedb/games/openarena/", "walnut://game/a/b",
                    "--help", "/etc/passwd"):
            self.assertIsNone(E(bad), bad)

    def test_single_instance_hand_off(self):
        with tempfile.TemporaryDirectory() as d:
            lock, hand = app.instance_paths(d)
            self.assertEqual(os.path.dirname(lock), os.path.join(d, "walnut-games"))
            fd = app.claim_instance(lock)
            self.assertIsNotNone(fd)
            self.assertEqual(stat.S_IMODE(os.stat(os.path.dirname(lock)).st_mode), 0o700)
            # a second start: the lock is held (flock on another open file description), it hands off
            r = subprocess.run([sys.executable, "-I", "-c", "import sys; sys.path.insert(0, %r); import walnut_games as w; "
                                "print(w.claim_instance(%r))" % (str(PKG / "files/lib"), lock)],
                               capture_output=True, text=True)
            self.assertEqual(r.stdout.strip(), "None", r.stderr)
            self.assertIsNone(app.take_hand_off(hand))
            app.hand_off(hand, "openarena")
            self.assertEqual(app.take_hand_off(hand), "openarena")
            self.assertIsNone(app.take_hand_off(hand))          # read once
            app.hand_off(hand, None)
            self.assertEqual(app.take_hand_off(hand), "")       # just come forward
            Path(hand).write_text("../../x\n")
            self.assertEqual(app.take_hand_off(hand), "")
            os.close(fd)
            # main: a bad link is refused before anything starts; a good one is handed to the running app
            env = dict(os.environ, XDG_RUNTIME_DIR=d, DISPLAY="")
            prog = "import sys; sys.path.insert(0, %r); import walnut_games as w; sys.exit(w.main(sys.argv))" % \
                str(PKG / "files/lib")
            r = subprocess.run([sys.executable, "-I", "-c", prog, "walnut://install/openarena"], env=env,
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 2)
            self.assertIn("walnut://game/<id> only", r.stderr)
            fd = app.claim_instance(lock)
            r = subprocess.run([sys.executable, "-I", "-c", prog, "walnut://game/openarena"], env=env,
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(app.take_hand_off(hand), "openarena")
            os.close(fd)

    def test_desktop_file_handles_the_scheme(self):
        text = (PKG / "files/applications/walnut-games.desktop").read_text()
        self.assertIn("\nMimeType=x-scheme-handler/walnut;\n", text)
        self.assertIn("\nExec=walnut-games %u\n", text)

    def test_tabs_and_link_targets(self):
        with tempfile.TemporaryDirectory() as d:
            pages = Path(d, "pages")
            g = pages / "games/openarena"
            for t in ("index.html", "test-runs/index.html", "technical/index.html"):
                (g / t).parent.mkdir(parents=True, exist_ok=True)
                (g / t).write_text("x")
            (g / "rating/sessions/s/shots").mkdir(parents=True)
            (g / "rating/sessions/s/shots/ps3-60s.jpg").write_text("j")
            (g / "test-runs/s").mkdir()
            (g / "test-runs/s/index.html").write_text("x")
            (pages / "index.html").write_text("home")
            tabs = app.details_tabs(str(pages), "openarena")
            self.assertEqual([os.path.relpath(t, g) for t in tabs], ["index.html", "test-runs/index.html",
                                                                     "technical/index.html"])   # no guides
            self.assertEqual(app.details_tabs(str(pages), "nothere"), [])
            S = lambda cur, d: (lambda r: r and os.path.relpath(r, g))(app.tab_step(str(pages), str(g / cur), d))
            self.assertEqual(S("index.html", 1), "test-runs/index.html")
            self.assertEqual(S("test-runs/index.html", 1), "technical/index.html")
            self.assertIsNone(S("technical/index.html", 1))                  # the last tab
            self.assertIsNone(S("index.html", -1))
            self.assertEqual(S("test-runs/s/index.html", 1), "technical/index.html")    # a session's page
            self.assertEqual(S("rating/sessions/s/shots/ps3-60s.jpg", 1), "test-runs/index.html")   # a picture
            self.assertIsNone(app.tab_step(str(pages), str(pages / "index.html"), 1))
            T = lambda u: app.link_target(u, str(pages))
            f = "file://" + str(g)
            self.assertEqual(T("walnut://install/openarena"), ("walnut", ("install", "openarena", None)))
            self.assertEqual(T("walnut://format/disk"), ("ignore", "walnut://format/disk"))
            self.assertEqual(T(f + "/technical/index.html"), ("page", f + "/technical/index.html"))
            self.assertEqual(T(f + "/rating/sessions/s/shots/ps3-60s.jpg")[0], "page")
            self.assertEqual(T(f + "/index.html#known"), ("page", f + "/index.html#known"))
            self.assertEqual(T("file://" + str(pages) + "/index.html"), ("close", None))
            self.assertEqual(T("file://" + str(pages) + "/about/index.html#rules-2"),
                             ("web", app.WEB + "about/#rules-2"))                 # not in the app's copy
            self.assertEqual(T(f + "/guides/index.html"), ("web", app.WEB + "games/openarena/guides/"))
            self.assertEqual(T("https://openarena.ws/"), ("web", "https://openarena.ws/"))
            self.assertEqual(T("file:///etc/passwd"), ("ignore", "file:///etc/passwd"))
            self.assertEqual(T("file://" + str(pages) + "/../secret"), ("ignore", "file://" + str(pages) + "/../secret"))
            self.assertEqual(T("mailto:x@y"), ("ignore", "mailto:x@y"))


class PadAndFocus(unittest.TestCase):
    def test_nav_pick(self):
        cur = (100, 100, 50, 20)
        cands = [(200, 100, 50, 20), (300, 100, 50, 20), (100, 200, 50, 20), (160, 140, 60, 20), (0, 100, 50, 20),
                 (100, 0, 50, 20)]
        self.assertEqual(app.nav_pick(cur, cands, "right"), 0)      # the nearest in the same row
        self.assertEqual(app.nav_pick(cur, cands, "down"), 3)       # below, overlapping columns win
        self.assertEqual(app.nav_pick(cur, cands, "left"), 4)
        self.assertEqual(app.nav_pick(cur, cands, "up"), 5)
        self.assertIsNone(app.nav_pick(cur, [(0, 0, 10, 10)], "right"))
        self.assertIsNone(app.nav_pick(cur, [], "up"))
        # a wide button under two narrow ones is "down" from either
        self.assertEqual(app.nav_pick((0, 0, 40, 20), [(0, 30, 200, 20)], "down"), 0)
        self.assertEqual(app.nav_pick((150, 0, 40, 20), [(0, 30, 200, 20)], "down"), 0)

    def test_scale(self):
        self.assertEqual(app.ui_scale(1080, env=""), 1.5)
        self.assertEqual(app.ui_scale(720, env=""), 1.0)
        self.assertEqual(app.ui_scale(600, env=""), 1.0)
        self.assertEqual(app.ui_scale(2160, env=""), 2.0)
        self.assertEqual(app.ui_scale(1080, env="1.2"), 1.2)
        self.assertEqual(app.ui_scale(1080, env="9"), 3.0)
        self.assertEqual(app.ui_scale(1080, env="x"), 1.5)

    def test_pad_devices_and_buttons(self):
        text = ("I: Bus=0003 Vendor=054c Product=0268\nN: Name=\"Sony PLAYSTATION(R)3 Controller Motion Sensors\"\n"
                "H: Handlers=event4 \n\nN: Name=\"Sony PLAYSTATION(R)3 Controller\"\nH: Handlers=event5 js0 \n\n"
                "N: Name=\"keyboard\"\nH: Handlers=sysrq kbd event0 leds\n\nH: Handlers=js1 event12 eventx\n")
        self.assertEqual(app.pad_devices(text), ["/dev/input/event5", "/dev/input/event12"])
        self.assertEqual(app.pad_press(1, 310, 1), "l1")
        self.assertEqual(app.pad_press(1, 311, 1), "r1")
        self.assertEqual(app.pad_press(1, 308, 1), "square")
        self.assertEqual(app.pad_press(1, 544, 1), "up")
        self.assertEqual(app.pad_press(1, 547, 1), "right")
        self.assertIsNone(app.pad_press(1, 310, 0))           # a release
        self.assertIsNone(app.pad_press(1, 310, 2))           # autorepeat
        self.assertIsNone(app.pad_press(1, 304, 1))           # Cross: the pointer's click, not ours
        self.assertIsNone(app.pad_press(1, 305, 1))           # Circle: the pointer's right click
        self.assertEqual(app.pad_press(3, 17, -1), "up")      # a hat d-pad (other pads)
        self.assertIsNone(app.pad_press(3, 17, 0))
        self.assertIsNone(app.pad_press(3, 0, 1))             # a stick

    def test_pad_reader_reads_presses_and_drops_old_ones(self):
        import struct
        ev = struct.Struct("@llHHi")
        got = []
        with tempfile.TemporaryDirectory() as d:
            r, w = os.pipe()
            reader = app.PadReader(got.append, devices=os.path.join(d, "none"))
            reader.fds["/dev/input/event99"] = r
            now = time.time()
            sec, usec = int(now), int((now % 1) * 1e6)
            os.write(w, ev.pack(sec, usec, 1, 310, 1) + ev.pack(sec, usec, 0, 0, 0) +
                     ev.pack(sec - 30, usec, 1, 311, 1) + ev.pack(sec, usec, 1, 308, 1))
            os.close(w)
            t = threading.Thread(target=reader.run, daemon=True)
            t.start()
            deadline = time.time() + 5
            while len(got) < 2 and time.time() < deadline:
                time.sleep(0.05)
            time.sleep(0.2)
            self.assertEqual(got, ["l1", "square"])          # the 30 s old R1 was queued while frozen: dropped
            self.assertNotIn("/dev/input/event99", reader.fds)   # the device went away (EOF): closed


class NetSurfStylesheet(unittest.TestCase):
    """postinst puts the rule that shows the site's "Open in Walnut Games" into NetSurf's
    /usr/share/netsurf/user.css between markers (created, appended, replaced), postrm takes it out"""

    def run_script(self, name, arg, root):
        stubs = Path(root, "stubs")
        stubs.mkdir(exist_ok=True)
        for tool in ("visudo", "py3compile"):        # the rest of postinst, made harmless
            (stubs / tool).write_text("#!/bin/sh\nexit 0\n")
            (stubs / tool).chmod(0o755)
        env = dict(os.environ, DPKG_ROOT=str(root), PATH=str(stubs) + ":/usr/bin:/bin")
        r = subprocess.run(["sh", str(PKG / "debian" / name), arg], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_add_append_again_remove(self):
        rule = (PKG / "files/netsurf/user.css").read_text()
        self.assertIn(".walnut-os-only { display: block !important; }", rule)
        self.assertIn("~/.config/netsurf/user.css takes precedence", rule)
        self.assertIn("files/netsurf/user.css $(P)/usr/share/walnut-games/netsurf-user.css",
                      (PKG / "debian/rules").read_text())
        block = "/* walnut-games begin */\n" + rule + "/* walnut-games end */\n"
        with tempfile.TemporaryDirectory() as d:
            share = Path(d, "usr/share/walnut-games")
            share.mkdir(parents=True)
            (share / "netsurf-user.css").write_text(rule)
            css = Path(d, "usr/share/netsurf/user.css")
            self.run_script("postinst", "configure", d)                  # no file yet: made
            self.assertEqual(css.read_text(), block)
            self.assertEqual(stat.S_IMODE(css.stat().st_mode), 0o644)
            self.run_script("postinst", "configure", d)                  # again: the same
            self.assertEqual(css.read_text(), block)
            self.run_script("postrm", "remove", d)                       # only ours: the file goes
            self.assertFalse(css.exists())
            css.parent.mkdir(parents=True, exist_ok=True)
            css.write_text("body { font-size: 20px; }\n")               # somebody's own rules
            self.run_script("postinst", "configure", d)
            self.assertEqual(css.read_text(), "body { font-size: 20px; }\n" + block)
            (share / "netsurf-user.css").write_text(".walnut-os-only { display: inline !important; }\n")
            self.run_script("postinst", "configure", d)                  # a new rule replaces ours
            text = css.read_text()
            self.assertEqual(text.count("walnut-games begin"), 1)
            self.assertIn("display: inline", text)
            self.assertNotIn("display: block", text)
            self.assertTrue(text.startswith("body { font-size: 20px; }\n"))
            self.run_script("postrm", "upgrade", d)                      # an upgrade keeps it
            self.assertIn("walnut-games begin", css.read_text())
            self.run_script("postrm", "purge", d)
            self.assertEqual(css.read_text(), "body { font-size: 20px; }\n")
            self.run_script("postrm", "purge", d)                        # nothing of ours: untouched
            self.assertEqual(css.read_text(), "body { font-size: 20px; }\n")
            self.assertEqual(sorted(p.name for p in css.parent.iterdir()), ["user.css"])   # no temporary left


def x_server():
    """(process, display) of a private Xvfb or Xvnc, or None"""
    for name, extra in (("Xvfb", ["-screen", "0", "1400x900x24"]), ("Xvnc", [])):
        exe = shutil.which(name)
        if not exe:
            continue
        for n in range(80, 100):
            if os.path.exists("/tmp/.X11-unix/X%d" % n) or os.path.exists("/tmp/.X%d-lock" % n):
                continue
            cmd = [exe, ":%d" % n] + extra
            if name == "Xvnc":
                cmd = [exe, ":%d" % n, "-geometry", "1400x900", "-depth", "24", "-SecurityTypes", "None",
                       "-localhost", "-rfbport", str(5900 + n)]
            p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(50):
                if os.path.exists("/tmp/.X11-unix/X%d" % n):
                    return p, ":%d" % n
                if p.poll() is not None:
                    break
                time.sleep(0.1)
            p.terminate()
        return None
    return None


def good_game(gid="alpha", **kw):
    g = {"id": gid, "title": gid.title(), "packages": [gid], "rating": {"medal": "Gold", "score": 80, "aspects": {}},
         "categories": ["racing"], "tests": [{"date": "2026-10-20", "ps3": ["rating/sessions/a/ps3-1.jpg"]}]}
    g.update(kw)
    return g


class Freshness(unittest.TestCase):
    def cat(self, date=None, rev=None, **kw):
        c = {"games": [good_game(**kw)]}
        if date:
            c["content_date"] = date
        if rev:
            c["revision"] = rev
        return c

    def test_validate_accepts_a_good_catalog(self):
        c = self.cat("2026-10-25", "abc")
        self.assertIs(app.validate_catalog(c), c)
        app.validate_catalog({"games": [good_game(launchers=[{"name": "x", "rating": {"medal": "Tin"}}],
                                                  patches=[{"subject": "s"}], known_issues=["k"])]})
        app.validate_catalog({"games": [good_game(thumb=None, data_setup={
            "gdp": "doom", "required": ["doom-wad"], "optional": [], "downloads": [], "hint": None,
            "present_any": ["doom-wad"]})]})

    def test_validate_rejects_bad_ones(self):
        bad = [None, [], {}, {"games": []}, {"games": "x"}, {"games": [1]},
               {"games": [good_game(id="../x")]}, {"games": [good_game(id="A")]},
               {"games": [good_game(), good_game()]},
               {"games": [good_game(title="")]}, {"games": [good_game(packages=[])]},
               {"games": [good_game(packages="alpha")]}, {"games": [good_game(rating={})]},
               {"games": [good_game(rating={"medal": "Gold", "aspects": []})]},
               {"games": [good_game(categories="racing")]}, {"games": [good_game(categories=[1])]},
               {"games": [good_game(patches=[{}])]}, {"games": [good_game(launchers=[{"name": "x"}])]},
               {"games": [good_game(tests=[{"ps3": ["../../etc/passwd"]}])]},
               {"games": [good_game(tests=[{"ps3": ["/etc/passwd"]}])]},
               {"games": [good_game(tests=[{"ps3": "x.jpg"}])]},
               {"games": [good_game(data_setup=["doom-wad"])]}, {"games": [good_game(data_setup={"required": "doom-wad"})]},
               {"games": [good_game(data_setup={"present_any": [1]})]}, {"games": [good_game(data_setup={"hint": 3})]}]
        for b in bad:
            with self.assertRaises(ValueError, msg=repr(b)[:80]):
                app.validate_catalog(b)

    def test_newer_by_date_then_revision(self):
        old, new = self.cat("2026-10-20", "a"), self.cat("2026-10-21", "b")
        self.assertTrue(app.is_newer(new, old))
        self.assertFalse(app.is_newer(old, new))
        self.assertFalse(app.is_newer(old, self.cat("2026-10-20", "a")))      # the same
        self.assertTrue(app.is_newer(self.cat("2026-10-20", "b"), old))       # same day, other content
        self.assertFalse(app.is_newer(self.cat("2026-10-20"), old))           # no revision to tell by

    def test_stamp_of_a_catalog_without_the_fields(self):
        self.assertEqual(app.content_stamp(self.cat()), ("2026-10-20", ""))
        self.assertEqual(app.content_stamp({"games": []}), ("", ""))
        self.assertTrue(app.is_newer(self.cat("2026-10-21", "x"), self.cat()))

    def test_choose_catalog(self):
        pkg, web = self.cat("2026-10-20", "a"), self.cat("2026-10-22", "b")
        self.assertIs(app.choose_catalog(pkg, web), web)
        self.assertIs(app.choose_catalog(web, pkg), web)
        self.assertIs(app.choose_catalog(pkg, None), pkg)
        self.assertIs(app.choose_catalog(pkg, {"games": []}), pkg)     # invalid: ignored
        self.assertIs(app.choose_catalog(pkg, {"games": [{"id": "x"}], "content_date": "2030"}), pkg)

    def test_cached_catalog_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, "catalog.json")
            self.assertIsNone(app.read_cached_catalog(str(p)))
            p.write_text("{not json")
            self.assertIsNone(app.read_cached_catalog(str(p)))
            p.write_text(json.dumps(self.cat("2026-10-25", "z")))
            self.assertEqual(app.read_cached_catalog(str(p))["revision"], "z")


SMOKE = """
import sys, os, json, time
sys.path.insert(0, sys.argv[1])
import tkinter as tk
import walnut_games as w
root = tk.Tk()
opened, launched = [], []
w.webbrowser.open = opened.append
hand = os.path.join(sys.argv[3], "open")
app = w.App(root, data=sys.argv[2], runtime_dir=None, online=False, hand_off_path=hand)
app.launch = lambda g, argv, cwd: launched.append(argv)
root.update()
def mapped(x):
    return bool(x.winfo_ismapped())
def texts(wd, cls=w.ttk.Label):
    out = [wd.cget("text")] if isinstance(wd, cls) else []
    for c in wd.winfo_children():
        out += texts(c, cls)
    return out
assert len(app.shown) == 2, app.shown
assert not mapped(app.b_main) and app.d_title["text"] == "Walnut Games"          # nothing picked yet
# not installed: Install, nothing smaller, the glance
app.select("alpha"); root.update()
assert app.main_kind == "install" and app.b_main["text"] == "Install" and str(app.b_main["state"]) == "normal"
assert not mapped(app.b_remove) and not mapped(app.b_remove_data) and not mapped(app.side_row)
assert mapped(app.b_details) and "Technical notes" in texts(app.links, w.ttk.Button) and mapped(app.b_home)
assert "Open on the web" in texts(app.links, w.ttk.Button) and "Home page" in texts(app.links, w.ttk.Button)
assert texts(app.d_numbers)[:2] == ["60 fps", "median frame rate"], texts(app.d_numbers)
assert "Controller: full" in app.d_info["text"], app.d_info["text"]
assert "1.5 h played, 0 crashes" in [app.d_medal.itemcget(i, "text") for i in app.d_medal.find_all() if app.d_medal.type(i) == "text"]
assert len(app.glance_imgs) == int(sys.argv[4]), app.glance_imgs     # the screenshot, when Pillow is here
app.sizes = {"alpha": 2048, "alpha-data": 10240}; app.show_glance(); root.update()
assert "12 MB" in texts(app.d_numbers), texts(app.d_numbers)
# installed, two launchers: Play with its menu; Remove under it
w.play_commands = lambda g, rt, files=None: [("Alpha", ["alpha"], None), ("Alpha GL1", ["alpha", "-gl1"], None)]
app.have = {"alpha", "alpha-data"}; app.update_buttons(); root.update()
assert app.main_kind == "play" and app.b_main["text"] == "Play \\u25be" and str(app.b_main["state"]) == "normal"
assert mapped(app.b_remove) and not mapped(app.b_remove_data)
assert app.dispatch("play", "alpha", "Alpha GL1") and launched[-1] == ["alpha", "-gl1"], launched
assert app.dispatch("play", "alpha") and app.menu is not None          # several: the menu
app.back(); root.update()
assert app.menu is None
assert not app.dispatch("install", "alpha") and "Alpha is installed." in app.d_status["text"]
assert not app.dispatch("frobnicate", "alpha") and not app.dispatch("play", "nothere")
app.busy = ("alpha", "install"); app.update_buttons()
assert str(app.b_main["state"]) == "disabled" and str(app.b_refresh["state"]) == "disabled"
assert not app.dispatch("play", "alpha")
app.busy = None; app.update_buttons()
# Details without tkinterweb: the website instead
sys.modules["tkinterweb"] = None
assert app.open_details() is False and app.dv is None and opened[-1] == w.WEB + "games/alpha/", opened
assert mapped(app.bar) and "python3-tkinterweb" in app.d_status["text"]
del sys.modules["tkinterweb"]
sys.path.insert(0, sys.argv[3])            # the fake tkinterweb
import tkinterweb as fake
# a game the app's copy doesn't have: the website too
app.select("beta"); root.update()
assert app.open_details() is False and opened[-1] == w.WEB + "games/beta/"
assert app.b_main["text"] == "Install" and not mapped(app.b_home)
# Details: the pages in the HTML frame, tabs, links, back
app.select("alpha"); root.update()
kids = len(root.winfo_children())
pages = os.path.join(sys.argv[2], "pages", "games", "alpha")
root.focus_force(); root.update()
assert app.open_details() is True and app.dv is not None; root.update()
assert not mapped(app.body) and not mapped(app.bar) and fake.LOADS[-1] == "file://" + pages + "/index.html"
assert fake.FRAMES[-1].kw["zoom"] == app.scale and not fake.FRAMES[-1].kw["javascript_enabled"]
html = fake.FRAMES[-1]
app.on_pad("r1"); root.update()
assert fake.LOADS[-1].endswith("/test-runs/index.html"), fake.LOADS[-1]
app.details_tab(1); app.details_tab(1)
assert fake.LOADS[-1].endswith("/technical/index.html") and len([u for u in fake.LOADS if "technical" in u]) == 1
app.on_pad("l1"); root.update()
assert fake.LOADS[-1].endswith("/test-runs/index.html")
app._key_nav(None, "Right")
assert fake.LOADS[-1].endswith("/technical/index.html")
app._key_nav(None, "Down")
assert html.scrolled[-1] == (4, "units")
html.click("file://" + pages + "/rating/sessions/a/ps3-1.jpg")          # the copy has it: in place
assert fake.LOADS[-1].endswith("ps3-1.jpg") and app.dv["history"]
html.click("file://" + pages + "/../../about/index.html#rules-2")        # the copy doesn't: the website
assert opened[-1] == w.WEB + "about/#rules-2", opened[-1]
html.click("https://example.org/")
assert opened[-1] == "https://example.org/"
html.click("walnut://format/alpha")                                      # unknown: ignored
assert app.dv is not None
app.on_pad("square")                                                     # Details is open already
app.back(); root.update()                                                # the page before
assert app.dv is not None and fake.LOADS[-1].endswith("/technical/index.html")
html.click("walnut://install/alpha"); root.update()                     # Details closes, the action runs
assert app.dv is None and mapped(app.bar) and fake.LOADS[-1] == "destroyed"
assert "Alpha is installed." in app.d_status["text"]
for n in range(5):                                                       # nothing left behind
    app.on_pad("square"); root.update()
    assert app.dv is not None, n
    app.back(); root.update()
    assert app.dv is None
assert len(root.winfo_children()) == kids, (kids, root.winfo_children())
if int(sys.argv[4]):                                                     # a thumbnail opens its picture
    app.open_details(shot="rating/sessions/a/ps3-1.jpg"); root.update()
    assert fake.LOADS[-2].endswith("/index.html") and fake.LOADS[-1].endswith("ps3-1.jpg")
    app.back(); app.back(); root.update()
    assert app.dv is None
# Install from a page: the helper (a fake) runs
app.have = set(); app.update_buttons()
app.open_details(); root.update()
fake.FRAMES[-1].click("walnut://install/alpha"); root.update()
assert app.dv is None and app.busy == ("alpha", "install"), app.busy
t = time.time()
while app.busy and time.time() - t < 20:
    root.update(); time.sleep(0.05)
assert "Alpha" in app.d_status["text"], app.d_status["text"]
# the arrows: from the cards to the main button, then on
app.canvas.focus_set(); root.update()
app._key_nav(None, "Right"); root.update()
assert root.focus_get() is app.b_main, root.focus_get()
app._key_nav(None, "Down"); root.update()
f = root.focus_get()
assert f is not app.b_main and f.winfo_class() == "TButton", f
app._key_nav(None, "Left"); root.update()
assert root.focus_get() is app.canvas, root.focus_get()
app._key_nav(None, "Up"); root.update()
assert root.focus_get().winfo_class() in ("TEntry", "TCombobox", "TCheckbutton", "TButton"), root.focus_get()
# another start's walnut://game/<id>: the window comes to that game
w.hand_off(hand, "beta")
t = time.time()
while app.selected != "beta" and time.time() - t < 5:
    root.update(); time.sleep(0.05)
assert app.selected == "beta"
w.hand_off(hand, "no-such-game")
t = time.time()
while os.path.exists(hand) and time.time() - t < 5:
    root.update(); time.sleep(0.05)
assert app.selected == "beta" and not os.path.exists(hand)
# a newer catalog keeps the selection; a game only on the website can't be installed yet
import copy
app.select("alpha")
new = copy.deepcopy(app.cat)
new["content_date"] = "2099-01-01"
new["games"][0]["title"] = "Alpha 2"
new["games"].append(dict(copy.deepcopy(new["games"][1]), id="gamma", title="Gamma"))
app.v_text.set("a"); app.refreshing = True
app.on_catalog(new, json.dumps(new).encode(), True, None)
root.update()
assert app.v_text.get() == "a" and app.selected == "alpha" and app.g["title"] == "Alpha 2"
assert "gamma" in app.by_id and not app.refreshing
app.select("gamma"); root.update()
assert str(app.b_main["state"]) == "disabled" and "next Walnut Games update" in app.d_reason["text"]
assert mapped(app.d_reason)
app.v_text.set("beta"); root.update()
assert [g["id"] for g in app.shown] == ["beta"]
app.v_text.set(""); app.v_medal.set("Gold"); app.apply_filters(); root.update()
assert [g["id"] for g in app.shown] == ["alpha"]
if len(sys.argv) > 5:      # pictures of the pane
    app.v_medal.set("All"); app.apply_filters(); app.select("alpha"); root.update(); time.sleep(0.3); root.update()
    import subprocess
    subprocess.run(["magick", "import", "-window", "root", sys.argv[5]])
root.destroy()
print("smoke ok")
"""
FAKE_TKINTERWEB = """
from tkinter import ttk
LOADS, FRAMES = [], []
class HtmlFrame(ttk.Frame):
    def __init__(self, master, **kw):
        super().__init__(master)
        self.kw, self.scrolled = kw, []
        FRAMES.append(self)
    def load_url(self, url):
        LOADS.append(url)
    def yview_scroll(self, n, what):
        self.scrolled.append((n, what))
    def click(self, url):
        self.kw["on_link_click"](url)
    def destroy(self):
        LOADS.append("destroyed")
        super().destroy()
"""
FAKE_HELPER = """#!/bin/sh
echo "progress 50 Installing"
echo "progress 100 Done"
"""


class WebLinks(unittest.TestCase):
    def test_web_url(self):
        self.assertEqual(app.web_url("etr"), app.WEB + "games/etr/")
        self.assertEqual(app.web_url("etr", technical=True), app.WEB + "games/etr/technical/")
        self.assertEqual(app.web_url(None), app.WEB)
        self.assertEqual(app.web_url(None, technical=True), app.WEB + "technical/")


class RatingText(unittest.TestCase):
    def test_basis_and_problems(self):
        r = {"graded_by": "session", "basis": {"tests": 2, "kind": "session", "minutes": 19},
             "rules_label": "rules 2.1 (2026-10-26)",
             "defects": [{"name": "wrong colours or blending", "points": 25, "what": "the sky", "origin": "also on the PC"}],
             "features_failed": [{"name": "saves load", "what": "", "origin": "PS3/Walnut only"}]}
        self.assertEqual(app.basis_text(r), "2 sessions, 19 min of play; rated under rules 2.1 (2026-10-26)")
        self.assertEqual(app.problem_lines(r), ["Picture: wrong colours or blending (the sky), -25 [also on the PC]",
                                                "Broken: saves load [PS3/Walnut only]"])
        self.assertEqual(app.basis_text({"medal": "Gold"}), "")     # an older catalog
        self.assertEqual(app.problem_lines({}), [])
        self.assertEqual(app.step_parts("Run `ls -l` now."), [("Run ", False), ("ls -l", True), (" now.", False)])
        self.assertEqual(app.step_parts("Plain."), [("Plain.", False)])
        # rules 2.1: broken but playable is Rust; Scrap names why it can't be played
        self.assertEqual(app.problem_lines({"medal": "Rust", "broken": ["picture broken (garbage textures or geometry)"]}),
                         ["Rust: picture broken (garbage textures or geometry)"])
        self.assertEqual(app.problem_lines({"medal": "Scrap", "reason": "crashes in 2 of 2 sessions",
                                            "broken": ["stability broken (2 crashes in 0.0 h)"]}),
                         ["Scrap: crashes in 2 of 2 sessions", "Broken: stability broken (2 crashes in 0.0 h)"])

    def test_medal_choices(self):
        # Tin is rules 1's only: in the filter only when a game has it
        games = [{"rating": {"medal": "Gold"}}, {"rating": {"medal": "Rust"}}]
        self.assertEqual(app.medal_choices(games), ["Platinum", "Gold", "Silver", "Bronze", "Rust", "Scrap", "Untested"])
        self.assertIn("Tin", app.medal_choices(games + [{"rating": {"medal": "Tin"}}]))


class Window(unittest.TestCase):
    def test_window_builds_on_a_private_x_server(self):
        if app.tk is None:
            self.skipTest("python3-tk is not installed")
        srv = x_server()
        if srv is None:
            self.skipTest("no Xvfb or Xvnc installed")
        proc, display = srv
        try:
            with tempfile.TemporaryDirectory() as d:
                shots = []
                try:
                    from PIL import Image
                    p = Path(d, "games/alpha/rating/sessions/a/ps3-1.jpg")
                    p.parent.mkdir(parents=True)
                    Image.new("RGB", (800, 450), (200, 120, 40)).save(p)
                    shots = ["rating/sessions/a/ps3-1.jpg"]
                except ImportError:
                    pass
                games = [dict(GAMES["alpha"], summary="s", categories=["racing"], tags=[], patched=True, patches=[],
                              needs_data=True, data_guide="https://example.org/guide", homepage="https://example.org/",
                              data_steps=["Download it.", "Run `sudo apt install ./x.deb`."],
                              controller="keyboard", known_issues=["slow"],
                              tests=[{"date": "2026-10-20", "ps3": shots}] if shots else [],
                              rating={"medal": "Gold", "score": 85, "capped": False, "reason": None, "hours": 1.5,
                                      "crashes": 0, "provisional": False, "graded_by": "session",
                                      "order": ["frame", "sound", "controls"], "rules_label": "rules 2.0 (2026-10-26)",
                                      "basis": {"tests": 1, "kind": "session", "minutes": 9}, "controller": "full",
                                      "aspects": {"frame": {"name": "Frame rate", "score": 90, "detail": "median 60 fps"},
                                                  "smooth": {"name": "Smoothness", "score": 70, "detail": "x"},
                                                  "sound": {"name": "Sound", "score": None, "detail": None}}}),
                         dict(GAMES["beta"], summary="s", categories=["puzzle"], tags=[], patched=False, patches=[],
                              needs_data=False, controller="unknown", tests=[],
                              rating={"medal": "Untested", "score": None, "aspects": {}})]
                Path(d, "catalog.json").write_text(json.dumps({"games": games}))
                pages = Path(d, "pages/games/alpha")
                for t in ("index.html", "test-runs/index.html", "technical/index.html"):
                    (pages / t).parent.mkdir(parents=True, exist_ok=True)
                    (pages / t).write_text("<p>x</p>")
                if shots:
                    (pages / "rating/sessions/a").mkdir(parents=True)
                    os.symlink(Path(d, "games/alpha", shots[0]), pages / shots[0])
                aux = Path(d, "aux")
                aux.mkdir()
                (aux / "tkinterweb.py").write_text(FAKE_TKINTERWEB)
                helper = aux / "helper"
                helper.write_text(FAKE_HELPER)
                helper.chmod(0o755)
                env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "XDG_RUNTIME_DIR")}
                # no dpkg-query (nothing installed); sudo -n runs the fake helper through a stub
                (aux / "sudo").write_text('#!/bin/sh\nshift; shift; exec %s "$@"\n' % helper)
                (aux / "sudo").chmod(0o755)
                env.update(DISPLAY=display, XDG_CACHE_HOME=d, PATH=str(aux) + ":/nonexistent")
                extra = [os.environ["WALNUT_SHOT"]] if os.environ.get("WALNUT_SHOT") else []
                r = subprocess.run([sys.executable, "-c", SMOKE, str(PKG / "files/lib"), d, str(aux),
                                    str(len(shots))] + extra, env=env, capture_output=True, text=True, timeout=180)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn("smoke ok", r.stdout)
        finally:
            proc.terminate()
            proc.wait()


DATA_SMOKE = """
import sys, os, json, time
sys.path.insert(0, sys.argv[1])
import tkinter as tk
import walnut_games as w
w.WALNUT_DATA = sys.argv[3]
w.DISC = "/nonexistent/sr0"
root = tk.Tk()
app = w.App(root, data=sys.argv[2], runtime_dir=None, online=False)
root.update()
def mapped(b):
    return bool(b.winfo_ismapped())
app.select("quakespasm"); root.update()
assert mapped(app.b_data) and app.b_data["text"] == "Add game files\\u2026" and not mapped(app.b_repo)
assert app.note_tag["text"] == "REQUIRED:" and "original Quake" in app.note_text["text"]
assert mapped(app.b_note) and app.main_kind == "install", "the note by Add game files..., the main button installs"
old_have = app.have
app.have = set(old_have) | {"quakespasm"}; app.update_buttons(); root.update()
assert app.main_kind == "add-data" and app.b_main["text"] == "Add game files\\u2026", "Play waits for the required data"
assert str(app.b_main["state"]) == "normal" and not mapped(app.b_data) and mapped(app.b_note) and mapped(app.b_remove)
assert int(app.b_note.grid_info()["row"]) == int(app.b_main.grid_info()["row"]) + 1, "the note under the main button"
app.repo = {"quake-licensed-data"}; app.update_buttons(); root.update()
assert mapped(app.b_repo) and not mapped(app.b_data), "licensed next to the main button"
app.repo = set()
app.main_pressed(); root.update()
assert app.chooser is not None, "the main button opens the chooser"
app.back(); root.update()
assert app.chooser is None, "Circle closes it"
app.have = set(old_have) | {"quakespasm", "quake-registered"}; app.info = {"quake-registered": ("full", 2048)}
app.update_buttons(); root.update()
assert app.main_kind == "play" and mapped(app.b_remove_data) and not mapped(app.b_note), "data installed"
app.info = {}
app.have = old_have; app.update_buttons()
assert str(app.b_data["state"]) == "normal"
app.repo = {"quake-licensed-data"}; app.update_buttons(); root.update()
assert mapped(app.b_repo) and mapped(app.b_data), "repo button"
app.repo = set(); app.update_buttons()
app.select("openttd"); root.update()
assert mapped(app.b_data) and app.b_data["text"] == "Add game files\\u2026"
assert mapped(app.b_note) and app.note_tag["text"] == "OPTIONAL:" and "Transport Tycoon Deluxe graphics" in app.note_text["text"]
assert app.b_repo["text"] == "Install from Walnut licensed"
app.select("quakespasm"); root.update()
app.open_chooser(); root.update()
labels = [l for l, _ in app.chooser_sources]
assert labels == ["GOG: Quake", "File: quake106.zip"], labels
texts = []
def walk(wd):
    if isinstance(wd, w.ttk.Button):
        texts.append(wd.cget("text"))
    for c in wd.winfo_children():
        walk(c)
walk(app.chooser)
assert "Download the free shareware (needs the network)" in texts, texts
assert any(t.startswith("File: quake106.zip") for t in texts) and "Other file\\u2026" in texts and "Cancel" in texts, texts
if len(sys.argv) > 4:      # a picture of the chooser
    root.update(); time.sleep(0.5); root.update()
    import subprocess
    subprocess.run(["magick", "import", "-window", "root", sys.argv[4]])
app.chooser.destroy(); app.chooser = None
app.start_import(app.g, os.path.join(os.environ["HOME"], "Downloads", "quake106.zip")); root.update()
assert app.imp is not None and not mapped(app.b_data) and str(app.b_play["state"]) == "disabled"
t = time.time()
saw_file = False
while not app.imp.done and time.time() - t < 30:
    root.update(); time.sleep(0.05)
    saw_file = saw_file or "Reading pak0.pak" in app.imp_detail["text"]
assert app.imp.done, "import never ended"
assert saw_file, "no file line shown"
msg = app.d_status["text"]
assert msg.startswith("Quake needs its official patch files"), msg
assert mapped(app.b_retry) and mapped(app.b_data), "retry"
log = open(os.path.join(os.environ["XDG_CACHE_HOME"], "walnut-games", "import.log")).read()
assert "gdp step searching" in log
args = open(os.environ["FAKE_WD_ARGS"]).read().split("\\n")[0]
assert args == "import quakespasm " + os.path.join(os.environ["HOME"], "Downloads", "quake106.zip"), args
app.retry_download(); root.update()
t = time.time()
while not app.imp.done and time.time() - t < 30:
    root.update(); time.sleep(0.05)
args = open(os.environ["FAKE_WD_ARGS"]).read().split("\\n")[1]
assert args.startswith("import -p quake-registered quakespasm "), args
while not app.imp.done:
    root.update(); time.sleep(0.05)
app.open_chooser(); root.update()
def find(wd):
    if isinstance(wd, w.ttk.Button) and wd.cget("text").startswith("Download the free"):
        return wd
    for c in wd.winfo_children():
        r = find(c)
        if r:
            return r
find(app.chooser).invoke(); root.update()
t = time.time()
while not app.imp.done and time.time() - t < 30:
    root.update(); time.sleep(0.05)
args = open(os.environ["FAKE_WD_ARGS"]).read().split("\\n")[2]
assert args == "import -p quake-shareware quakespasm", repr(args)
# a partial import (exit 0 with needs-download and warning lines) says what is missing on the page
open(os.environ["FAKE_WD_ARGS"] + ".partial", "w").close()
src = os.path.join(os.environ["HOME"], "Downloads", "quake106.zip")
app.start_import(app.g, src); root.update()
t = time.time()
while (not app.imp.done or "quake-registered" in app.retry) and time.time() - t < 30:
    root.update(); time.sleep(0.05)
t = time.time()
while "Not everything" not in app.d_status["text"] and time.time() - t < 30:
    root.update(); time.sleep(0.05)
assert "Not everything" in app.d_status["text"], app.d_status["text"]
txt = app.partial_label["text"]
assert "Quake Registered needs its official patch files from the network: Try again with download" in txt, txt
assert "Not on this copy: Quake Extra (no such file)" in txt, txt
assert mapped(app.partial_label) and mapped(app.b_retry), "partial notice"
app.retry_download(); root.update()
t = time.time()
while not app.imp.done and time.time() - t < 30:
    root.update(); time.sleep(0.05)
args = open(os.environ["FAKE_WD_ARGS"]).read().split("\\n")[-2]
assert args == "import -p quake-registered quakespasm " + src, repr(args)
# a plain import whose source also holds an add-on: the "Also add" button runs -p PKG with the same source
open(os.environ["FAKE_WD_ARGS"] + ".addon", "w").close()
src = os.path.join(os.environ["HOME"], "Downloads", "quake106.zip")
app.start_import(app.g, src); root.update()
t = time.time()
while not app.imp.done and time.time() - t < 30:
    root.update(); time.sleep(0.05)
t = time.time()
while not app.addon_buttons and time.time() - t < 30:
    root.update(); time.sleep(0.05)
assert [b["text"] for b in app.addon_buttons] == ["Also add: Music (music)"], [b["text"] for b in app.addon_buttons]
assert mapped(app.addon_buttons[0]), "addon button"
os.remove(os.environ["FAKE_WD_ARGS"] + ".addon")
app.addon_buttons[0].invoke(); root.update()
t = time.time()
while not app.imp.done and time.time() - t < 30:
    root.update(); time.sleep(0.05)
args = open(os.environ["FAKE_WD_ARGS"]).read().split("\\n")[-2]
assert args == "import -p quake-music quakespasm " + src, repr(args)
root.update()
assert not app.addon_buttons, "no add-on left after it was imported"
root.destroy()
print("data smoke ok")
"""
FAKE_WALNUT_DATA = """#!/bin/sh
echo "$*" >> "$FAKE_WD_ARGS"
if [ -e "$FAKE_WD_ARGS.addon" ]; then
echo "log made quake-shareware"
echo "addon available quake-music"
exit 0
fi
if [ -e "$FAKE_WD_ARGS.partial" ]; then
echo "gdp error needs-download quake-registered: its official patch files have to be downloaded"
echo "warning missing quake-extra: no such file"
echo "log made quake-shareware"
exit 0
fi
echo "log running game-data-packager quake for quake-shareware quake-registered"
echo "gdp step searching $3"
sleep 0.5
echo "gdp file id1/pak0.pak 18689235"
sleep 0.5
echo "gdp error needs-download pak0.pak 1.06 needs a download"
echo "error game-data-packager made no package: some files have to be downloaded"
exit 1
"""


class DataWindow(unittest.TestCase):
    def test_chooser_and_an_import(self):
        if app.tk is None:
            self.skipTest("python3-tk is not installed")
        srv = x_server()
        if srv is None:
            self.skipTest("no Xvfb or Xvnc installed")
        proc, display = srv
        try:
            with tempfile.TemporaryDirectory() as d:
                rating = {"medal": "Untested", "score": None, "aspects": {}}
                games = [dict(QS, rating=rating, needs_data=True, data_steps=["Run `walnut-data import quakespasm DIR`."],
                              tests=[]),
                         dict(TTD, rating=rating, tests=[])]
                Path(d, "catalog.json").write_text(json.dumps({"games": games}))
                home = Path(d, "home")
                (home / "Downloads").mkdir(parents=True)
                (home / "GOG Games/Quake").mkdir(parents=True)
                (home / "Downloads/quake106.zip").write_bytes(b"x")
                fake = Path(d, "walnut-data")
                fake.write_text(FAKE_WALNUT_DATA)
                fake.chmod(0o755)
                env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "XDG_RUNTIME_DIR")}
                env.update(DISPLAY=display, XDG_CACHE_HOME=str(Path(d, "cache")), HOME=str(home), USER="nobody-here",
                           PATH="/nonexistent:/bin:/usr/bin" if os.environ.get("WALNUT_SHOT") else "/nonexistent:/bin",
                           FAKE_WD_ARGS=str(Path(d, "args")))
                extra = [os.environ["WALNUT_SHOT"]] if os.environ.get("WALNUT_SHOT") else []
                r = subprocess.run([sys.executable, "-c", DATA_SMOKE, str(PKG / "files/lib"), d, str(fake)] + extra,
                                   env=env, capture_output=True, text=True, timeout=180)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn("data smoke ok", r.stdout)
        finally:
            proc.terminate()
            proc.wait()


if __name__ == "__main__":
    unittest.main()
