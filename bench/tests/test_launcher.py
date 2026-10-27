#!/usr/bin/env python3
"""test_launcher.py: launcher.py (a game's menu launchers) and session.sh's choice of one, with
temporary .desktop files and a fake dpkg (python3 -I bench/tests/test_launcher.py)."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HARNESS = Path(__file__).resolve().parent.parent / "harness"   # this is bench/tests/
sys.path.insert(0, str(HARNESS))
import launcher   # noqa: E402


def entry(path, name, exec_, **extra):
    lines = ["[Desktop Entry]", "Type=Application", "Name=" + name, "Exec=" + exec_]
    lines += ["%s=%s" % kv for kv in extra.items()]
    Path(path).write_text("\n".join(lines) + "\n")


class Launchers(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.apps, self.rt = t / "apps", t / "run"
        (self.rt / "applications").mkdir(parents=True)
        self.apps.mkdir()
        entry(self.apps / "oa-gl2.desktop", "OpenArena (GL 2.0)", "ps3-game openarena +set cl_renderer opengl2 %U")
        entry(self.apps / "oa-gl1.desktop", "OpenArena (GL 1.2)", "ps3-game openarena +set cl_renderer opengl1")
        entry(self.apps / "oa-extra.desktop", "OA tweak", "env FOO=1 openarena +set x 1")
        entry(self.apps / "other.desktop", "Other", "ps3-game othergame")
        entry(self.apps / "hidden.desktop", "Hidden", "openarena", NoDisplay="true")
        # the dpkg fake: a script listing what the game's package ships
        self.files = [str(self.apps / "oa-gl1.desktop"), str(self.apps / "oa-gl2.desktop"),
                      str(self.apps / "gone.desktop"), "/usr/games/openarena", "/usr/share/games/openarena/x.pk3"]
        (t / "files").write_text("\n".join(self.files) + "\n")
        self.dpkg = t / "dpkg"
        self.dpkg.write_text("#!/bin/sh\ncat '%s'\n" % (t / "files"))
        self.dpkg.chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def found(self):
        return launcher.launchers(launcher.package_files(["openarena"], str(self.dpkg)), str(self.rt), str(self.apps))

    def test_order_and_filters(self):
        f = self.found()
        # the shipped ones by file name (gone.desktop doesn't exist), then the other shown one running openarena
        self.assertEqual([x[0] for x in f], ["oa-gl1.desktop", "oa-gl2.desktop", "oa-extra.desktop"])
        self.assertEqual(f[1][1], "OpenArena (GL 2.0)")

    def test_runtime_copy_wins(self):
        entry(self.rt / "applications" / "oa-gl1.desktop", "OpenArena (GL 1.2)", "ps3-game openarena --rt")
        self.assertEqual(self.found()[0][2], ["ps3-game", "openarena", "--rt"])

    def test_no_programs_no_extras(self):
        (Path(self.tmp.name) / "files").write_text(str(self.apps / "oa-gl1.desktop") + "\n")
        self.assertEqual([x[0] for x in self.found()], ["oa-gl1.desktop"])

    def test_command(self):
        f = {x[0]: x[2] for x in self.found()}
        self.assertEqual(launcher.game_argv(f["oa-gl2.desktop"]), ["openarena", "+set", "cl_renderer", "opengl2"])
        self.assertEqual(launcher.game_argv(f["oa-extra.desktop"]), ["env", "FOO=1", "openarena", "+set", "x", "1"])
        self.assertEqual(launcher.game_argv(["FOO=1", "game"]), ["env", "FOO=1", "game"])

    def test_game_stamp(self):
        """X-Walnut-Game: with --game, an entry stamped for another game is left out (games sharing an
        engine), its own and unstamped ones stay; without --game every entry counts as before"""
        entry(self.apps / "oa-gl1.desktop", "OpenArena (GL 1.2)", "ps3-game openarena", **{"X-Walnut-Game": "openarena"})
        entry(self.apps / "oa-mod.desktop", "A mod", "ps3-game openarena +set fs_game mod", **{"X-Walnut-Game": "mod"})
        files = launcher.package_files(["openarena"], str(self.dpkg))
        names = lambda g: [x[0] for x in launcher.launchers(files, str(self.rt), str(self.apps), g)]   # noqa: E731
        self.assertEqual(names("openarena"), ["oa-gl1.desktop", "oa-gl2.desktop", "oa-extra.desktop"])
        self.assertEqual(names("mod"), ["oa-gl2.desktop", "oa-extra.desktop", "oa-mod.desktop"])
        self.assertEqual(names(None), ["oa-gl1.desktop", "oa-gl2.desktop", "oa-extra.desktop", "oa-mod.desktop"])
        # a runtime copy carries the stamp too: it is checked, not the installed one's
        entry(self.rt / "applications" / "oa-mod.desktop", "A mod", "ps3-game openarena --rt", **{"X-Walnut-Game": "mod"})
        self.assertNotIn("oa-mod.desktop", names("openarena"))

    def test_choose(self):
        f = self.found()
        for want in ("oa-gl2.desktop", "oa-gl2", "OpenArena (GL 2.0)"):
            self.assertEqual(launcher.choose(f, want)[0], "oa-gl2.desktop")
        self.assertIsNone(launcher.choose(f, "nope"))


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.apps = t / "apps"
        self.apps.mkdir()
        entry(self.apps / "a.desktop", "Game A", "ps3-game game --a")
        entry(self.apps / "b.desktop", "Game B", "ps3-game game --b")
        (t / "dpkg").write_text("#!/bin/sh\nprintf '%%s\\n' '%s' '%s' /usr/games/game\n" % (self.apps / "a.desktop", self.apps / "b.desktop"))
        (t / "dpkg").chmod(0o755)
        (t / "recipes").mkdir()
        (t / "recipes" / "game.sh").write_text("CMD=(/usr/games/game --bench)\nvariant_fast() { ENV+=(X=1); }\n")
        self.args = "--apps %s --runtime-dir %s/none --dpkg %s" % (self.apps, t, t / "dpkg")

    def tearDown(self):
        self.tmp.cleanup()

    def run_launcher(self, *a):
        return subprocess.run([sys.executable, "-I", str(HARNESS / "launcher.py")] + self.args.split() + list(a),
                              capture_output=True, text=True)

    def test_default_and_argv_file(self):
        out = Path(self.tmp.name) / "argv"
        p = self.run_launcher("--argv-out", str(out), "game")
        self.assertEqual(p.returncode, 0)
        self.assertEqual(p.stdout.splitlines(), ["a.desktop\tGame A", "a.desktop\tGame A", "b.desktop\tGame B"])
        self.assertEqual(out.read_bytes(), b"game\0--a\0")

    def test_unknown_exits_2(self):
        p = self.run_launcher("--choose", "zzz", "game")
        self.assertEqual((p.returncode, p.stdout), (2, ""))
        self.assertIn("Game B", p.stderr)

    def test_game_option(self):
        entry(self.apps / "b.desktop", "Game B", "ps3-game game --b", **{"X-Walnut-Game": "other"})
        p = self.run_launcher("--game", "game", "game")
        self.assertEqual(p.stdout.splitlines(), ["a.desktop\tGame A", "a.desktop\tGame A"])
        p = self.session("Game B")                     # session.sh passes its game's id
        self.assertEqual(p.returncode, 2)

    def test_none_exits_3(self):
        self.assertEqual(subprocess.run([sys.executable, "-I", str(HARNESS / "launcher.py"), "--apps", str(self.apps),
                                         "--dpkg", "true"], capture_output=True).returncode, 3)

    def session(self, *a, **env):
        e = dict(os.environ, SESSION_DRY="1", BENCH_RECIPES=str(Path(self.tmp.name) / "recipes"),
                 SESSION_PACKAGES="game", SESSION_LAUNCHER_ARGS=self.args)
        e.update(env)
        return subprocess.run(["bash", str(HARNESS / "session.sh"), "game"] + list(a), capture_output=True, text=True, env=e)

    def test_session_dry(self):
        p = self.session()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("launcher: Game A (a.desktop)", p.stdout)
        self.assertIn("several launchers", p.stdout)
        self.assertIn("command: game --a", p.stdout)
        self.assertIn("BENCH_LAUNCHER='Game A'", p.stdout)
        p = self.session("Game B")
        self.assertIn("command: game --b", p.stdout)
        self.assertIn("client.sh game base", p.stdout)
        p = self.session("b.desktop", "fast")          # no recipe variants any more: a launcher only
        self.assertEqual(p.returncode, 2)
        self.assertIn("no recipe variants", p.stderr)

    def test_session_variant_is_a_launcher_name(self):
        p = self.session("fast")                       # a variant's name is no launcher (launchers replaced them)
        self.assertEqual(p.returncode, 2)
        self.assertIn("no launcher 'fast'", p.stderr)

    def test_session_test_toml(self):
        t = Path(self.tmp.name) / "recipes"
        (t / "game.test.toml").write_text('process = "game"\nhome = [".game"]\ncommand = ["/usr/games/game", "--play"]\n')
        p = self.session(SESSION_LAUNCHER_ARGS="--apps %s/none --runtime-dir %s/none --dpkg true" % (t, t))
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("its test's command runs", p.stdout)
        self.assertIn("command: /usr/games/game --play", p.stdout)
        (t / "game.test.toml").write_text('process = "game"\nhome = ["/etc"]\n')
        p = self.session()
        self.assertEqual(p.returncode, 2)
        self.assertIn("not a valid played test", p.stderr)

    def test_session_unknown(self):
        p = self.session("zzz")
        self.assertEqual(p.returncode, 2)
        self.assertIn("Game A", p.stderr)
        self.assertIn("b.desktop", p.stderr)


if __name__ == "__main__":
    unittest.main()
