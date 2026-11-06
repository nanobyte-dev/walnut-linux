#!/usr/bin/env python3
"""ps3-game-mode on (tools/run-tests.sh suite game-mode-on): when it comes through sudo (SUDO_UID, here the test
seam PS3_GAME_MODE_TEST_UID), the pid must be the game mode guard's own service (its cgroup is
user@UID.service/ps3-game.slice/ps3-game-mode.service, read from a fake /proc) and a second `on` is refused while
the first one's pid lives and its watcher is active. A direct root run (no SUDO_UID) is as before.
  python3 -I packages/walnut/system/ps3-desktop/tests/test_game_mode_on.py"""
import os, shutil, subprocess, tempfile, unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "files" / "sbin" / "ps3-game-mode"
UID = str(os.getuid())
GUARD_CG = "0::/user.slice/user-%s.slice/user@%s.service/ps3-game.slice/ps3-game-mode.service" % (UID, UID)


class On(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp(prefix="gmon-"))
        self.proc, self.run, self.bin = self.t / "proc", self.t / "run", self.t / "bin"
        for d in (self.proc, self.run, self.bin):
            d.mkdir()
        self.log = self.t / "log"
        self.active = self.t / "watch-active"
        (self.bin / "systemctl").write_text('#!/bin/sh\necho "systemctl $*" >> %s\ncase $1 in is-active) [ -e %s ];; esac\n' % (self.log, self.active))
        (self.bin / "systemd-run").write_text('#!/bin/sh\necho "systemd-run $*" >> %s\ntouch %s\n' % (self.log, self.active))
        for f in ("systemctl", "systemd-run"):
            (self.bin / f).chmod(0o755)

    def tearDown(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def fake(self, pid, cgroup):
        (self.proc / str(pid)).mkdir()
        (self.proc / str(pid) / "cgroup").write_text(cgroup + "\n")

    def on(self, pid, sudo=True, args=()):
        e = {k: v for k, v in os.environ.items() if k not in ("SUDO_UID",)}
        e.update(PATH="%s:%s" % (self.bin, os.environ["PATH"]), PS3_GAME_MODE_RUN=str(self.run), PS3_GAME_MODE_PROC=str(self.proc),
                 PS3_GAME_MODE_SELF="/bin/true", PS3_KICK_BIN="/nonexistent")
        if sudo:
            e["PS3_GAME_MODE_TEST_UID"] = UID
        return subprocess.run(["sh", str(SCRIPT), "on", str(pid), *args], env=e, capture_output=True, text=True, timeout=60)

    def test_the_guard_service_may(self):
        self.fake(100, GUARD_CG)
        p = self.on(100)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("ps3-game-mode-watch", self.log.read_text())

    def test_another_cgroup_may_not(self):
        for cg in ("0::/user.slice/user-%s.slice/user@%s.service/app.slice/foo.service" % (UID, UID),
                   "0::/user.slice/user-%s.slice/user@%s.service/ps3-game.slice/ps3-game-12.scope" % (UID, UID),
                   GUARD_CG.replace("user-" + UID, "user-" + str(int(UID) + 1), 1), "0::/"):
            with self.subTest(cg):
                self.fake(101, cg)
                p = self.on(101)
                self.assertEqual(p.returncode, 1, p.stderr)
                self.assertIn("isn't the game mode guard's service", p.stderr)
                self.assertFalse(self.log.exists())
                shutil.rmtree(self.proc / "101")

    def test_second_on_refused_while_the_first_lives(self):
        self.fake(100, GUARD_CG)
        self.assertEqual(self.on(100).returncode, 0)
        self.fake(200, GUARD_CG)
        p = self.on(200)
        self.assertEqual(p.returncode, 1, p.stderr)
        self.assertIn("already on", p.stderr)

    def test_second_on_after_the_first_pid_is_gone(self):
        self.fake(100, GUARD_CG)
        self.assertEqual(self.on(100).returncode, 0)
        shutil.rmtree(self.proc / "100")
        self.fake(200, GUARD_CG)
        self.assertEqual(self.on(200).returncode, 0)

    def test_second_on_after_off(self):
        self.fake(100, GUARD_CG)
        self.assertEqual(self.on(100).returncode, 0)
        subprocess.run(["sh", str(SCRIPT), "off"], env=dict(os.environ, PATH="%s:%s" % (self.bin, os.environ["PATH"]),
                       PS3_GAME_MODE_RUN=str(self.run)), capture_output=True, timeout=60)
        self.active.unlink()
        self.fake(200, GUARD_CG)
        self.assertEqual(self.on(200).returncode, 0)

    def test_direct_root_run_is_as_before(self):
        self.fake(100, "0::/anything")
        self.assertEqual(self.on(100, sudo=False).returncode, 0)
        self.active.unlink()
        self.assertEqual(self.on(100, sudo=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
