#!/usr/bin/env python3
"""Tests for the walnut-games package, on a PC without root, apt or (unless an X server program is
installed) a display: python3 -I walnut-games/tests/test_walnut_games.py (tools/run-tests.sh, suite
walnut-games).

- The root helper (files/sbin/walnut-games-helper): ids and package names validated, packages only
  from the catalog file, the catalog's owner and mode, install's arguments (--no-remove, the lists'
  age), remove's simulation and its refusal, the metapackage's dependencies marked manual, apt's
  status descriptor relayed (a fake apt-get writes to it), signals ignored, `python3 -I`, sudoers.
- The app's pure functions (files/lib/walnut_games.py): Exec parsing, the play command, filters, dpkg
  state, the helper's output lines.
- A smoke test that builds the window on a private X server (Xvfb or Xvnc, started here on its own
  display); skipped when neither is installed. It never uses the DISPLAY of the person running it.
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
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG = HERE.parent
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
        for bad in ("sauerbraten-nonfree-data", "ps3-games-licensed", "quake-shareware", "quake2-demo-data"):
            with self.assertRaises(helper.Refused, msg=bad):
                helper.game_packages("alpha", {"alpha": {"packages": ["alpha", bad]}})
        helper.game_packages("alpha", {"alpha": {"packages": ["quakespasm", "sauerbraten-data"]}})

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


class Install(unittest.TestCase):
    def test_install_arguments_and_stale_lists(self):
        calls = []
        stream = lambda args: (calls.append(args), (0, []))[1]   # noqa: E731
        _, out = run_say(helper.do_install, "alpha", GAMES, stream, lambda: 3 * 86400)
        self.assertEqual(calls, [["update"], ["install", "--no-remove", "alpha", "alpha-data"]])
        self.assertEqual(out[-1], "progress 100 installed")
        calls.clear()
        run_say(helper.do_install, "alpha", GAMES, stream, lambda: 600)
        self.assertEqual(calls, [["install", "--no-remove", "alpha", "alpha-data"]])
        calls.clear()
        run_say(helper.do_install, "alpha", GAMES, stream, lambda: None)   # no lists at all: update
        self.assertEqual(calls[0], ["update"])

    def test_failed_install(self):
        r, out = run_say(helper.do_install, "beta", GAMES, lambda a: (100, ["E: broken"]), lambda: 1)
        self.assertIsInstance(r, helper.AptFailed)
        self.assertTrue(any(l.startswith("error ") for l in out))

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

    def test_status(self):
        _, out = run_say(helper.do_status, "alpha", GAMES, fake_runner({"alpha"}, ""))
        self.assertEqual(out, ["status partial"])
        _, out = run_say(helper.do_status, "beta", GAMES, fake_runner({"beta"}, ""))
        self.assertEqual(out, ["status installed"])
        _, out = run_say(helper.do_status, "beta", GAMES, fake_runner(set(), ""))
        self.assertEqual(out, ["status not-installed"])


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
            self.assertEqual(app.play_command(GAMES["alpha"], str(rt), str(apps)), (["ps3-game", "alpha", "--fs"], "/tmp"))

    def test_play_falls_back_to_ps3_game(self):
        with tempfile.TemporaryDirectory() as d:
            apps = Path(d) / "apps"
            self.entry(apps, "alpha.desktop", "/usr/games/alpha -f %f")
            self.entry(apps, "beta.desktop", "ps3-game beta")
            self.assertEqual(app.play_command(GAMES["alpha"], str(Path(d) / "none"), str(apps)),
                             (["ps3-game", "/usr/games/alpha", "-f"], None))
            self.assertEqual(app.play_command(GAMES["beta"], None, str(apps)), (["ps3-game", "beta"], None))
            self.assertIsNone(app.play_command({"desktop": ["missing.desktop"]}, None, str(apps)))

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
        self.assertEqual(names(patched_only=True), ["Zed"])

    def test_helper_lines(self):
        self.assertEqual(app.parse_helper_line("progress 42 installing x\n"), ("progress", 42, "installing x"))
        self.assertEqual(app.parse_helper_line("progress 400 x"), ("progress", 100, "x"))
        self.assertIsNone(app.parse_helper_line("progress nope"))
        self.assertEqual(app.parse_helper_line("error refused: x"), ("error", "refused: x"))
        self.assertEqual(app.parse_helper_line("sudo: a password is required"), ("log", "sudo: a password is required"))
        self.assertIsNone(app.parse_helper_line("\n"))

    def test_screenshots_and_colours(self):
        g = {"tests": [{"date": "2026-10-20", "ps3": ["tests/t/ps3-40s.jpg"], "pc": ["tests/t/pc-30s.jpg"]}]}
        s = app.screenshots(g)
        self.assertEqual([r for _, r in s], ["tests/t/ps3-40s.jpg", "tests/t/pc-30s.jpg"])
        self.assertTrue(s[0][0].startswith("PS3") and s[1][0].startswith("PC"))
        self.assertEqual(app.pill_text_color(app.MEDAL_COLORS["Gold"]), "#14304d")
        self.assertEqual(app.pill_text_color(app.MEDAL_COLORS["Scrap"]), "#f4f4f4")
        for m in app.MEDAL_ORDER:
            self.assertIn(m, app.MEDAL_COLORS)

    def test_the_packaged_catalog_of_the_repository(self):
        sys.path.insert(0, str(PKG.parent / "tools" / "gamedb"))
        import catalog
        cat = catalog.build(PKG.parent / "gamedb" / "content" / "games", PKG.parent)
        for g in cat["games"]:
            helper.game_packages(g["id"], {x["id"]: x for x in cat["games"]})   # the helper accepts every entry
            self.assertIn(app.medal_of(g), app.MEDAL_ORDER)


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


SMOKE = """
import sys, os
sys.path.insert(0, sys.argv[1])
import tkinter as tk
import walnut_games as w
root = tk.Tk()
app = w.App(root, data=sys.argv[2], runtime_dir=None)
root.update()
assert len(app.shown) == 2, app.shown
app.select("alpha"); root.update()
assert str(app.b_install["state"]) == "normal" and str(app.b_remove["state"]) == "disabled"
assert str(app.b_play["state"]) == "disabled"
app.v_text.set("beta"); root.update()
assert [g["id"] for g in app.shown] == ["beta"]
app.v_text.set("zzz"); root.update()
assert app.shown == []
app.v_text.set(""); app.v_medal.set("Gold"); app.apply_filters(); root.update()
assert [g["id"] for g in app.shown] == ["alpha"]
app.busy = ("alpha", "install"); app.update_buttons()
assert str(app.b_play["state"]) == "disabled" and str(app.b_install["state"]) == "disabled"
root.destroy()
print("smoke ok")
"""


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
                games = [dict(GAMES["alpha"], summary="s", categories=["racing"], tags=[], patched=True, patches=[],
                              needs_data=False, controller="keyboard", known_issues=["slow"], tests=[],
                              rating={"medal": "Gold", "score": 85, "capped": False, "reason": None, "hours": 1.5,
                                      "crashes": 0, "provisional": False,
                                      "aspects": {"frame": {"name": "Frame rate", "score": 90, "detail": "median 60 fps"},
                                                  "sound": {"name": "Sound", "score": None, "detail": None}}}),
                         dict(GAMES["beta"], summary="s", categories=["puzzle"], tags=[], patched=False, patches=[],
                              needs_data=False, controller="unknown", tests=[],
                              rating={"medal": "Untested", "score": None, "aspects": {}})]
                Path(d, "catalog.json").write_text(json.dumps({"games": games}))
                env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "XDG_RUNTIME_DIR")}
                env.update(DISPLAY=display, XDG_CACHE_HOME=d, PATH="/nonexistent")   # no dpkg-query: nothing installed
                r = subprocess.run([sys.executable, "-c", SMOKE, str(PKG / "files/lib"), d], env=env,
                                   capture_output=True, text=True, timeout=120)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn("smoke ok", r.stdout)
        finally:
            proc.terminate()
            proc.wait()


if __name__ == "__main__":
    unittest.main()
