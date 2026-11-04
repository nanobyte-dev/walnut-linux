#!/usr/bin/env python3
"""walnut-firstboot's setup file (S3, round 3) on a fake root, in its dry run (tools/run-tests.sh suite ps3-firstboot):
where the file came from, what is asked, what a decline or a timeout does, and the helper's own accept/decline rules.
  python3 -I packages/walnut/system/ps3-system/tests/test_firstboot_preseed.py"""
import os, shutil, struct, subprocess, sys, tempfile, unittest
from pathlib import Path

SBIN = Path(__file__).resolve().parent.parent / "files" / "sbin"
FIRSTBOOT, CONFIRM = str(SBIN / "walnut-firstboot"), str(SBIN / "walnut-firstboot-confirm")
EVENT = struct.Struct("@llHHi")
BTN_SOUTH, BTN_START, BTN_NORTH = 0x130, 0x13b, 0x133
PRESEED = "WALNUT_USER=mallory\nWALNUT_PASSWORD_HASH=$6$x$y\nWALNUT_SSH=yes\nWALNUT_HOSTNAME=evilbox\nWALNUT_TIMEZONE=UTC\n"
# what the text wizard asks when the preseed is not used: welcome, user, password twice, host name, time zone, ssh
WIZARD = "\nbob\npw\npw\nbox\nUTC\nno\n"


def events(*evs):
    return b"".join(EVENT.pack(0, 0, t, c, v) for t, c, v in evs)


class Base(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp(prefix="fb-test-"))
        self.root = self.t / "root"
        for d in ("boot", "etc/walnut", "var/petitboot/mnt/ps3dc", "mnt/dev_hdd1", "run", "tmp"):
            (self.root / d).mkdir(parents=True)
        self.ps3dc = self.t / "ps3dc"
        self.ps3dc.mkdir()
        self.ev = self.t / "events"
        self.ev.write_bytes(b"")

    def tearDown(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def run_fb(self, stdin=WIZARD, evs=None, seconds="3", extra=(), env=None, trust=None):
        if evs is not None:
            self.ev.write_bytes(evs)
        e = dict(os.environ, DRY_RUN="1", WALNUT_FB_ROOT=str(self.root), WALNUT_FB_PS3DC_DIR=str(self.ps3dc),
                 WALNUT_FB_TRUST_UID=str(os.getuid() if trust is None else trust), WALNUT_FB_CONFIRM=CONFIRM,
                 WALNUT_FB_CONFIRM_ARGS="--no-tty --events %s" % self.ev, WALNUT_FB_CONFIRM_SECONDS=seconds, TERM="dumb")
        e.pop("PRESEED_FILE", None)
        e.update(env or {})
        p = subprocess.run(["bash", FIRSTBOOT, "--dry-run", "--force", *extra], input=stdin, env=e,
                           capture_output=True, text=True, timeout=60)
        self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
        return p

    def create(self, rel, text=PRESEED, mode=0o644):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        p.chmod(mode)
        return p

    def used(self, p):
        """the account the dry run reports"""
        return [l.split(":")[1].strip() for l in p.stdout.splitlines() if l.startswith("  User:")][0]

    def leftovers(self):
        return sorted(os.listdir(self.root / "run"))


class Sources(Base):
    def test_trusted_file_asks_nothing(self):
        self.create("boot/walnut-setup.conf")
        p = self.run_fb(stdin="")                                   # no answer to give: it is used
        self.assertEqual(self.used(p), "mallory")
        self.assertNotIn("Setup file on", p.stderr)

    def test_etc_file_and_explicit_path_are_trusted(self):
        self.create("etc/walnut/walnut-setup.conf")
        self.assertEqual(self.used(self.run_fb(stdin="")), "mallory")
        shutil.rmtree(self.root / "etc/walnut")
        f = self.create("elsewhere.conf")
        self.assertEqual(self.used(self.run_fb(stdin="", extra=("--preseed", str(f)))), "mallory")

    def test_a_root_file_not_owned_by_root_is_asked_about(self):
        self.create("boot/walnut-setup.conf")
        p = self.run_fb(trust=os.getuid() + 1, evs=events((1, BTN_SOUTH, 1)))
        self.assertIn("Setup file on this disk", p.stderr)          # not proven: asked, though not on dev_hdd1

    def test_a_group_writable_root_file_is_asked_about(self):
        self.create("boot/walnut-setup.conf", mode=0o664)
        p = self.run_fb(evs=b"", seconds="1")
        self.assertIn("Setup file on this disk", p.stderr)
        self.assertEqual(self.used(p), "bob")

    def test_the_three_hdd1_paths_are_asked_about(self):
        for rel in ("var/petitboot/mnt/ps3dc/walnut-setup.conf", "mnt/dev_hdd1/walnut-setup.conf"):
            f = self.create(rel)
            p = self.run_fb(evs=events((1, BTN_SOUTH, 1)))
            self.assertIn("Setup file on dev_hdd1", p.stderr, rel)
            self.assertEqual(self.used(p), "mallory", rel)
            f.unlink()
        (self.ps3dc / "walnut-setup.conf").write_text(PRESEED)      # the probe of /dev/ps3dc
        p = self.run_fb(evs=events((1, BTN_SOUTH, 1)))
        self.assertIn("Setup file on dev_hdd1", p.stderr)
        self.assertEqual(self.used(p), "mallory")
        self.assertEqual(self.leftovers(), [])                      # the private directory is gone

    def test_a_symlink_into_dev_hdd1_is_asked_about(self):
        real = self.create("mnt/dev_hdd1/walnut-setup.conf")
        os.symlink(real, self.root / "boot/walnut-setup.conf")
        p = self.run_fb(evs=b"", seconds="1")
        self.assertIn("Setup file on dev_hdd1", p.stderr)
        self.assertEqual(self.used(p), "bob")

    def test_the_environment_variable_is_classified_not_trusted(self):
        f = self.create("mnt/dev_hdd1/walnut-setup.conf")
        p = self.run_fb(evs=b"", seconds="1", env={"PRESEED_FILE": str(f)})
        self.assertIn("Setup file on dev_hdd1", p.stderr)
        self.assertEqual(self.used(p), "bob")


class Decisions(Base):
    def setUp(self):
        super().setUp()
        self.create("mnt/dev_hdd1/walnut-setup.conf")

    def test_enter_style_accept_by_pad_cross_and_start(self):
        for code in (BTN_SOUTH, BTN_START):
            self.assertEqual(self.used(self.run_fb(evs=events((1, code, 1)))), "mallory", code)

    def test_other_buttons_and_releases_do_not_accept(self):
        p = self.run_fb(evs=events((1, BTN_NORTH, 1), (1, BTN_SOUTH, 0), (3, BTN_SOUTH, 1)), seconds="1")
        self.assertEqual(self.used(p), "bob")

    def test_timeout_ignores_everything_and_runs_the_wizard_in_full(self):
        p = self.run_fb(evs=b"", seconds="1")
        self.assertEqual(self.used(p), "bob")                       # the wizard's user, not mallory
        self.assertIn("Hostname: box", p.stdout)
        self.assertIn("SSH:      no", p.stdout)
        self.assertNotIn("mallory", p.stdout + p.stderr.replace('"mallory"', ""))
        self.assertIn("Walnut Linux Setup", p.stderr + p.stdout)    # the welcome box shows again (PRESEED_FILE cleared)
        self.assertIn("the setup file is ignored", p.stderr)

    def test_decline_clears_the_clock_setting_too(self):
        self.create("mnt/dev_hdd1/walnut-setup.conf", PRESEED + "WALNUT_DATETIME=2001-01-01 00:00:00\n")
        p = self.run_fb(evs=b"", seconds="1")
        self.assertNotIn("setting date/time from preseed", p.stdout + p.stderr)

    def test_the_question_is_short_and_quotes_the_filtered_name(self):
        p = self.run_fb(evs=events((1, BTN_SOUTH, 1)))
        q = p.stderr[p.stderr.index("Setup file on dev_hdd1"):]
        self.assertIn('It would create the admin user "mallory" and turn SSH on.', q)
        self.assertIn("Press Enter, or Cross on the controller, to use it. It is ignored after 60 s.", q)
        self.assertNotIn("evilbox", p.stderr)

    def test_odd_user_name_is_refused_whole(self):
        self.create("mnt/dev_hdd1/walnut-setup.conf", "WALNUT_USER=ma\x1b[31mllory\nWALNUT_SSH=yes\n")
        p = self.run_fb(evs=events((1, BTN_SOUTH, 1)))
        self.assertEqual(self.used(p), "bob")
        self.assertNotIn("\x1b[31m", p.stderr + p.stdout)

    def test_no_user_line(self):
        self.create("mnt/dev_hdd1/walnut-setup.conf", "WALNUT_HOSTNAME=evilbox\n")
        p = self.run_fb(evs=b"", seconds="1")
        self.assertIn("It would only set the host name and time zone.", p.stderr)

    def test_too_long_file_is_ignored_and_not_slurped(self):
        self.create("mnt/dev_hdd1/walnut-setup.conf", "A" * 70000)
        p = self.run_fb()
        self.assertIn("longer than 64 KiB", p.stderr)
        self.assertEqual(self.used(p), "bob")
        self.assertNotIn("Setup file on", p.stderr)

    def test_exactly_64_kib_is_accepted_as_a_size(self):
        text = PRESEED + "#" * (65536 - len(PRESEED) - 1) + "\n"
        self.assertEqual(len(text), 65536)
        self.create("mnt/dev_hdd1/walnut-setup.conf", text)
        p = self.run_fb(evs=events((1, BTN_SOUTH, 1)))
        self.assertEqual(self.used(p), "mallory")

    def test_the_probe_does_not_follow_a_planted_tmp_symlink(self):
        victim = self.t / "victim"
        victim.write_text("keep")
        os.symlink(victim, self.root / "tmp/walnut-preseed.conf")
        (self.root / "mnt/dev_hdd1/walnut-setup.conf").unlink()
        (self.ps3dc / "walnut-setup.conf").write_text(PRESEED)
        self.run_fb(evs=events((1, BTN_SOUTH, 1)))
        self.assertEqual(victim.read_text(), "keep")


class Special(Base):
    def test_a_fifo_named_by_the_environment_is_ignored_not_waited_for(self):
        f = self.root / "mnt/dev_hdd1/walnut-setup.conf"
        os.mkfifo(f)
        p = self.run_fb(env={"PRESEED_FILE": str(f)})
        self.assertIn("not a regular file", p.stderr)
        self.assertEqual(self.used(p), "bob")


class Cleanup(Base):
    def test_cleanup_never_walks_into_a_mount_that_stayed(self):
        """umount, rmdir, rm -f of our own file, rmdir: a directory that is still full (an unmountable mount) keeps its files"""
        text = Path(FIRSTBOOT).read_text()
        fn = text[text.index("cleanup_work() {"):text.index("trap cleanup_work EXIT")]
        self.assertNotIn("rm -r", fn)
        work = self.t / "work"
        (work / "mnt").mkdir(parents=True)
        (work / "mnt" / "GameOS-file").write_text("not ours")
        (work / "preseed.conf").write_text("ours")
        script = 'FB_PS3DC_DIR=x\nWORK=%s\n%s\ncleanup_work\n' % (work, fn)
        subprocess.run(["bash", "-c", script], check=True)
        self.assertTrue((work / "mnt" / "GameOS-file").exists())
        self.assertFalse((work / "preseed.conf").exists())


class Helper(Base):
    def helper(self, *args, stdin=b"", env=None, evs=None):
        if evs is not None:
            self.ev.write_bytes(evs)
        e = dict(os.environ, **(env or {}))
        e.pop("DRY_RUN", None) if env is None or "DRY_RUN" not in env else None
        return subprocess.run([sys.executable, "-I", CONFIRM, *args], input=stdin, env=e, capture_output=True, timeout=30).returncode

    def test_seams_are_refused_outside_a_dry_run(self):
        self.assertEqual(self.helper("2", "--events", str(self.ev), "--no-tty", evs=events((1, BTN_SOUTH, 1))), 1)
        self.assertEqual(self.helper("2", "--events", str(self.ev), "--no-tty", evs=events((1, BTN_SOUTH, 1)), env={"DRY_RUN": "1"}), 0)

    def test_nothing_can_answer(self):
        self.assertEqual(self.helper("1", "--no-tty", "--events", str(self.ev), evs=b"", env={"DRY_RUN": "1"}), 1)
        self.assertEqual(self.helper("1"), 1)                        # stdin is not a terminal and no pad
        self.assertEqual(self.helper("nonsense"), 1)
        self.assertEqual(self.helper(), 1)

    def test_events_are_read_whole_and_in_order(self):
        e = {"DRY_RUN": "1"}
        evs = events((1, BTN_NORTH, 1), (1, BTN_SOUTH, 1))
        self.assertEqual(self.helper("2", "--no-tty", "--events", str(self.ev), evs=evs, env=e), 0)
        self.assertEqual(self.helper("1", "--no-tty", "--events", str(self.ev), evs=evs[:-1], env=e), 1)   # a cut record is nothing

    def test_pad_nodes_come_from_sysfs_capabilities(self):
        import importlib.machinery, importlib.util
        spec = importlib.util.spec_from_loader("fbc", importlib.machinery.SourceFileLoader("fbc", CONFIRM))
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        words = struct.calcsize("l") * 8
        n = 0x13c // words + 1
        for name, bits in (("event3", {BTN_SOUTH}), ("event4", {0x1e}), ("event5", {BTN_SOUTH, BTN_START})):
            d = self.t / "sys" / name / "device" / "capabilities"
            d.mkdir(parents=True)
            ws = [0] * n
            for b in bits:
                ws[b // words] |= 1 << (b % words)
            (d / "key").write_text(" ".join("%x" % w for w in reversed(ws)) + "\n")
        self.assertEqual(sorted(m.pad_nodes(str(self.t / "sys"))), ["/dev/input/event3", "/dev/input/event5"])

    def test_a_console_enter_accepts_only_an_empty_line(self):
        import pty
        for typed, want in ((b"\n", 0), (b"abc\n", 1)):
            pid, fd = pty.fork()
            if pid == 0:
                os.execv(sys.executable, [sys.executable, "-I", CONFIRM, "1.5"])
            import time
            time.sleep(0.4)
            os.write(fd, typed)
            _, status = os.waitpid(pid, 0)
            os.close(fd)
            self.assertEqual(os.waitstatus_to_exitcode(status), want, typed)

    def test_typeahead_before_the_question_is_flushed(self):
        import pty, time
        pid, fd = pty.fork()
        if pid == 0:
            time.sleep(0.5)                                          # the question comes after the typing
            os.execv(sys.executable, [sys.executable, "-I", CONFIRM, "1.5"])
        os.write(fd, b"\n")
        _, status = os.waitpid(pid, 0)
        os.close(fd)
        self.assertEqual(os.waitstatus_to_exitcode(status), 1)


if __name__ == "__main__":
    unittest.main()
