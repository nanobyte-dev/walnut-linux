#!/usr/bin/env python3
"""Tests for the walnut-rate package and the harness parts it adds, on a PC without root, sudo or the PS3:
python3 -I packages/walnut/apps/walnut-rate/tests/test_walnut_rate.py (tools/run-tests.sh, suite walnut-rate).

- The root helper (files/sbin/walnut-rate-helper): every argv but the four actions refused (exit 2), the root
  check, python3 -I and no environment read, the census file only under the debugfs directory (a link out is
  not followed), check's line, mm, mm-follow's request and answer protocol, dmesg-follow ending at the end of
  its input and taking dmesg with it; the sudoers line (visudo -cf when there is one) and the path debian/rules
  installs it at.
- ps3-bench.sh session mode with BENCH_ROOT_HELPER: a fake sudo on PATH (it logs and runs its arguments), the
  real helper behind a wrapper (a fake debugfs and dmesg): the census in the samples and the snapshots, the
  kernel log in dmesg.log, every sudo call `-n HELPER action`, the helpers gone after the run; other modes refused.
- The clean-system check (bench/harness/cleancheck.py) against process lists of a Walnut desktop (no recorded
  ones were left in build/bench: lists written from what ps3-session starts, as ps prints them): a clean desktop
  with the terminal running walnut-rate passes; a browser, apt, an unknown big program block and are named; the
  harness's own descendants and, during a session, ps3-game.slice are exempt; SSH logins (one's own a warning);
  xprop's window list and properties; a busy process; check() on a fake /proc, /sys and xprop names Firefox and
  refuses; WM_DELETE_WINDOW closes a Tk window on a private Xvnc (skipped without one).
- The watch (cleancheck.py watch, client.sh BENCH_WATCH=1): its lines on a fake /proc, and client.sh in a played
  session marking bench.txt interfered=1 with what it saw (watch=on, interfered=0 when clean).
- The export: the xwd to PNG conversion (both byte orders, 24 and 32 bits, against ImageMagick's own reading when
  it is installed) and the screenshots as JPEG (made smaller until they fit the check's SHOTS_MAX), the scrub of a
  run (a gamedb session rebuilt as a run, the console's user, full name, host and addresses written into it): the
  folder submissions/<game>/<id>/ with plain files only, none of them, no LAN or MAC address, no home path left,
  submitter.toml without a contact (a pull request is public), the same id on a second export, the check passing;
  a nickname holding the user name and a no to publishing refused; then the folder -> import-run.py gives the same
  result.toml as the run itself, and the same folder marked interfered=1 is refused by import-run.py.
- The CLI: list over the repository's tests, submit without a session and with a bad --repo refused, the
  session's environment without the player's BENCH_/SESSION_ switches.
- The submission check (tools/gamedb/submission_check.py): a good folder; refused: a second folder, a second game,
  files elsewhere, a change to an existing file, links, submodules and executables in the pull request; a game
  without a played test, bad ids, a .jpg that isn't a JPEG, unknown names, NUL in a text file, oversize files and
  screenshots, a host name in Xorg's or uname's line, a user@host, home paths, LAN and MAC addresses, a
  submitter.toml without consent, with a contact, of another game, or missing, a bench.txt that isn't a session;
  on disk: a link and an executable file.
- The pull request round trip: walnut-rate submit without gh and with gh logged out (the steps by hand, nothing
  sent), then through a stubbed gh (the fork, one tree of only the folder on the base's tree, the pull request
  from the fork's branch, not editable by us); that commit as refs/pull/1/merge of a local repository standing in
  for GitHub; tools/gamedb/submissions.py list, show (the checker on the fetched merge commit, the run written
  out), accept (refused without REVIEW.md, then imported: result.toml as our own import's plus source and tester,
  reimport.py's derivation the same, committed alone with the pull request's number, the pull request commented
  on and closed, never merged; a second accept imports nothing), a pull request touching another file refused by
  show and by the workflow's command, reject (asked first, then commented and closed).
"""
import importlib.machinery
import importlib.util
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import tomllib
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG = HERE.parent
REPO = PKG.parents[3]
HARNESS = REPO / "bench" / "harness"
sys.dont_write_bytecode = True
sys.path[:0] = [str(PKG / "files" / "lib"), str(HARNESS), str(REPO / "bench" / "analysis"), str(REPO / "tools" / "gamedb")]
import walnut_rate as wr   # noqa: E402
import cleancheck          # noqa: E402
import proccheck           # noqa: E402
import evidence            # noqa: E402
import submission_check as sc   # noqa: E402

wr.BENCH = REPO / "bench"
HELPER_SRC = PKG / "files" / "sbin" / "walnut-rate-helper"
SCRATCH = REPO / "build" / "tmp" / "walnut-rate-tests"


def load_helper():
    loader = importlib.machinery.SourceFileLoader("walnut_rate_helper", str(HELPER_SRC))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    m = importlib.util.module_from_spec(spec)
    loader.exec_module(m)
    return m


H = load_helper()
MM = ("GART backed by memory: 4096 KiB\nswap: 11 KiB in VRAM, 22 KiB on disk, 33 KiB written\n"
      "Xorg 3 handles, VRAM 1000 KiB, GART 200 KiB\n")


def tmpdir():
    SCRATCH.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(dir=SCRATCH))


def write(p, text, mode=None):
    p = Path(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)
    if mode:
        p.chmod(mode)
    return p


def fake_debugfs(root):
    write(root / "0" / "ps3rsx_mm", MM)
    return root


def helper_wrapper(d, debugfs, dmesg):
    """a program that runs the real helper without root, on a fake debugfs and dmesg"""
    return write(d / "helper", "#!%s -I\nimport importlib.machinery, importlib.util, sys\n"
                 "l = importlib.machinery.SourceFileLoader('h', %r)\ns = importlib.util.spec_from_loader('h', l)\n"
                 "m = importlib.util.module_from_spec(s); l.exec_module(m)\n"
                 "sys.exit(m.main(sys.argv, need_root=False, debugfs=%r, dmesg=(%r,)))\n"
                 % (sys.executable, str(HELPER_SRC), str(debugfs), str(dmesg)), 0o755)


def fake_dmesg(d):
    """prints a kernel line, writes its pid, then waits (dmesg -W)"""
    return write(d / "dmesg", "#!/bin/sh\necho \"$$\" > %s/dmesg.pid\necho '[   12.5] ps3rsx: hello from the kernel'\n"
                 "exec sleep 300\n" % d, 0o755)


def alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    try:   # a zombie of ours isn't alive
        return Path("/proc/%d/stat" % pid).read_text().split(")")[1].split()[0] != "Z"
    except OSError:
        return False


class Helper(unittest.TestCase):
    def setUp(self):
        self.d = tmpdir()
        self.debugfs = fake_debugfs(self.d / "dri")

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def test_refusals(self):
        for argv in (["h"], ["h", "rm"], ["h", "mm", "x"], ["h", "check", "--x"], ["h", "dmesg-follow", "/etc/shadow"],
                     ["h", "MM"], ["h", ""], ["h", "mm-follow", "mm"]):
            self.assertEqual(H.main(argv, need_root=False, debugfs=str(self.debugfs)), 2, argv)
        if os.geteuid() != 0:
            self.assertEqual(H.main(["h", "check"]), 2)   # not root: refused before anything is read

    def test_isolated(self):
        src = HELPER_SRC.read_text()
        self.assertTrue(src.startswith("#!/usr/bin/python3 -I\n"))
        self.assertNotIn("os.environ", src)
        self.assertNotIn("getenv", src)
        self.assertNotIn("shell=True", src)

    def test_check_and_mm(self):
        out = self.d / "out"
        with open(out, "wb") as f:
            self.assertEqual(H.main(["h", "check"], need_root=False, debugfs=str(self.debugfs),
                                    dmesg=("/bin/true",), out=f.fileno()), 0)
        self.assertEqual(out.read_text(), "ok dmesg=yes mm=yes\n")
        with open(out, "wb") as f:
            self.assertEqual(H.main(["h", "mm"], need_root=False, debugfs=str(self.debugfs), out=f.fileno()), 0)
        self.assertEqual(out.read_text(), MM)
        # a census "file" that is a link out of the directory is never read
        other = write(self.d / "secret", "not the census\n")
        bad = self.d / "dri2"
        (bad / "0").mkdir(parents=True)
        (bad / "0" / "ps3rsx_mm").symlink_to(other)
        self.assertIsNone(H.mm_path(str(bad)))
        self.assertEqual(H.main(["h", "mm"], need_root=False, debugfs=str(bad)), 1)

    def run_follow(self, action, inp_lines, dmesg=None):
        w = helper_wrapper(self.d, self.debugfs, dmesg or fake_dmesg(self.d))
        p = subprocess.Popen([str(w), action], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        return p

    def test_mm_follow(self):
        p = self.run_follow("mm-follow", 2)
        out = b""
        p.stdin.write(b"\n")
        p.stdin.flush()
        t = time.monotonic()
        while out.count(b"#end\n") < 1 and time.monotonic() - t < 10:
            out += os.read(p.stdout.fileno(), 65536)
        p.stdin.write(b"\n")
        p.stdin.flush()
        while out.count(b"#end\n") < 2 and time.monotonic() - t < 10:
            out += os.read(p.stdout.fileno(), 65536)
        self.assertEqual(out, (MM + "#end\n").encode() * 2)
        p.stdin.close()
        self.assertEqual(p.wait(10), 0)   # the end of its input ends it
        p.stdout.close()

    def test_dmesg_follow(self):
        log = self.d / "dmesg.log"
        w = helper_wrapper(self.d, self.debugfs, fake_dmesg(self.d))
        with open(log, "wb") as f:
            p = subprocess.Popen([str(w), "dmesg-follow"], stdin=subprocess.PIPE, stdout=f)
            t = time.monotonic()
            while "hello" not in log.read_text() and time.monotonic() - t < 10:
                time.sleep(0.1)
            pid = int((self.d / "dmesg.pid").read_text())
            self.assertTrue(alive(pid))
            p.stdin.close()
            self.assertEqual(p.wait(10), 0)
        self.assertIn("ps3rsx: hello from the kernel", log.read_text())
        t = time.monotonic()
        while alive(pid) and time.monotonic() - t < 5:
            time.sleep(0.1)
        self.assertFalse(alive(pid), "dmesg left running")

    def test_sudoers_and_paths(self):
        s = (PKG / "debian" / "sudoers").read_text()
        rules = [line for line in s.splitlines() if line and not line.startswith("#")]
        self.assertEqual(rules, ["%sudo ALL=(root) NOPASSWD: /usr/lib/walnut-rate/walnut-rate-helper"])
        r = (PKG / "debian" / "rules").read_text()
        self.assertIn("files/sbin/walnut-rate-helper $(L)/walnut-rate-helper", r)
        self.assertIn("L := $(P)/usr/lib/walnut-rate", r)
        self.assertIn("visudo -cf debian/sudoers", r)
        self.assertIn("-m 440 debian/sudoers $(P)/etc/sudoers.d/walnut-rate", r)
        v = shutil.which("visudo") or ("/usr/sbin/visudo" if os.access("/usr/sbin/visudo", os.X_OK) else None)
        if v:
            p = subprocess.run([v, "-cf", str(PKG / "debian" / "sudoers")], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn('sys.path.insert(0, "/usr/lib/walnut-rate")', (PKG / "files" / "bin" / "walnut-rate").read_text())
        conf = (PKG / "package.conf").read_text()
        self.assertIn("build/walnut-rate/bench.tar:built/bench.tar", conf)
        self.assertIn("tools/gamedb/evidence.py:built/evidence.py", conf)
        self.assertIn("walnut-rate-src: walnut-rate-bench", (REPO / "Makefile").read_text())


class BenchHelperMode(unittest.TestCase):
    """ps3-bench.sh session with BENCH_ROOT_HELPER: no sudo of its own, the root reads through the helper"""

    def test_session(self):
        d = tmpdir()
        try:
            bindir = d / "bin"
            write(bindir / "sudo", "#!/bin/sh\necho \"$*\" >> %s/sudo.log\n[ \"$1\" = -n ] && shift\nexec \"$@\"\n" % d, 0o755)
            helper = helper_wrapper(d, fake_debugfs(d / "dri"), fake_dmesg(d))
            env = dict(os.environ, PATH="%s:%s" % (bindir, os.environ["PATH"]), BENCH_ROOT_HELPER=str(helper),
                       BENCH_OUT=str(d / "out"), BENCH_STALLS="1")
            b = REPO / "packages/walnut/tools/ps3-gpu-tools/scripts/ps3-bench.sh"
            p = subprocess.run(["bash", str(b), "session", "helpertest", "--", "bash", "-c", "echo go; sleep 2.2"],
                               env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 0, p.stdout[-800:] + p.stderr[-800:])
            runs = list((d / "out").glob("ps3-bench-*-helpertest"))
            self.assertEqual(len(runs), 1, list((d / "out").iterdir()) if (d / "out").exists() else None)
            r = runs[0]
            samples = (r / "samples").read_text().splitlines()
            self.assertGreaterEqual(len(samples), 2, samples)
            self.assertTrue(all(line.split()[5] == "4096" and line.endswith("| Xorg:1000:200") for line in samples), samples[:3])
            self.assertIn("Xorg 3 handles, VRAM 1000 KiB", (r / "snap-pre.txt").read_text())
            self.assertIn("Xorg 3 handles, VRAM 1000 KiB", (r / "snap-post.txt").read_text())
            t = time.monotonic()
            while "hello" not in (r / "dmesg.log").read_text() and time.monotonic() - t < 5:
                time.sleep(0.1)
            self.assertIn("ps3rsx: hello from the kernel", (r / "dmesg.log").read_text())
            calls = (d / "sudo.log").read_text().splitlines()
            self.assertEqual(sorted(set(calls)), sorted({"-n %s mm" % helper, "-n %s mm-follow" % helper,
                                                         "-n %s dmesg-follow" % helper}), calls)
            self.assertFalse((r / ".mm").exists())
            self.assertFalse((r / "stalls.rec").exists())   # BENCH_STALLS (its own sudo) is off in this mode
            pid = int((d / "dmesg.pid").read_text())
            t = time.monotonic()
            while alive(pid) and time.monotonic() - t < 5:
                time.sleep(0.1)
            self.assertFalse(alive(pid), "the helper's dmesg outlived the session")
            p = subprocess.run(["bash", str(b), "run", "x", "--", "true"], env=env, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(p.returncode, 0)
            self.assertIn("session mode only", p.stderr)
        finally:
            shutil.rmtree(d, ignore_errors=True)


# ---- the clean-system check ------------------------------------------------------------------------

# a Walnut desktop as `ps -eo pid,ppid,rss,pcpu,stat,comm,args` prints it: what ps3-session starts (openbox, the
# helpers in ps3-desktop.slice), the system's services, the terminal with walnut-rate (pid 900) in it
DESKTOP = """  PID  PPID   RSS %CPU STAT COMMAND         COMMAND
    1     0  9800  0.3 Ss   systemd         /sbin/init
    2     0     0  0.0 S    kthreadd        [kthreadd]
   31     2     0  1.2 I    kworker/0:1-eve [kworker/0:1-events]
   52     2     0  0.4 S    kswapd0         [kswapd0]
  210     1  6200  0.0 Ss   systemd-journal /usr/lib/systemd/systemd-journald
  230     1  4100  0.0 Ss   systemd-udevd   /usr/lib/systemd/systemd-udevd
  300     1  3300  0.0 Ss   dbus-daemon     /usr/bin/dbus-daemon --system --address=systemd: --nofork
  305     1 11000  0.1 Ssl  NetworkManager  /usr/sbin/NetworkManager --no-daemon
  307     1  5200  0.0 Ss   systemd-logind  /usr/lib/systemd/systemd-logind
  320     1  4000  0.0 Ss   sshd            sshd: /usr/sbin/sshd -D [listener] 0 of 10-100 startups
  330     1  2900  0.0 Ss   cron            /usr/sbin/cron -f
  340     1  3100  0.0 Ss   chronyd         /usr/sbin/chronyd -F 1
  400     1  6000  0.0 Ss   login           /bin/login -f
  410     1  7700  0.0 Ss   systemd         /usr/lib/systemd/systemd --user
  411   410  2100  0.0 S    (sd-pam)        (sd-pam)
  420   400  4200  0.0 S    bash            -bash
  430   420  1800  0.0 S    xinit           xinit /usr/bin/ps3-session -- :0 vt1
  431   430 52000  2.1 Ssl  Xorg            /usr/lib/xorg/Xorg :0 vt1
  440   430 18000  0.4 S    openbox         openbox
  441   410  3000  0.0 Ss   dbus-daemon     /usr/bin/dbus-daemon --session --address=systemd: --nofork
  450   410 21000  0.2 Sl   tint2           tint2
  451   410 33000  0.1 Sl   pcmanfm         pcmanfm --desktop
  452   410 19000  0.0 Sl   nm-applet       nm-applet
  453   410  9000  0.0 Sl   pnmixer         pnmixer
  454   410 11000  0.0 Sl   lxpolkit        lxpolkit
  455   410  6000  0.0 Sl   dunst           dunst
  456   410  8000  0.0 Ssl  pipewire        /usr/bin/pipewire
  457   410  9000  0.0 Ssl  wireplumber     /usr/bin/wireplumber
  458   410  7000  0.0 Ssl  pipewire-pulse  /usr/bin/pipewire-pulse
  460   410  6000  0.0 Sl   gvfsd           /usr/libexec/gvfsd
  470   410 12000  0.1 S    python3         /usr/bin/python3 -I /usr/lib/ps3/desktop/ps3-game-detect
  800   451 24000  0.5 Sl   lxterminal      lxterminal
  801   800  4400  0.0 Ss   bash            bash
  900   801 14000  3.0 S+   walnut-rate     /usr/bin/python3 -I /usr/bin/walnut-rate check
"""
EXTRA = {
    "firefox": "  950   451 310000  4.0 Sl   firefox-esr     /usr/lib/firefox-esr/firefox-esr\n",
    "badwolf": "  951   451  60000  0.0 Sl   badwolf         badwolf https://example.org\n",
    "apt": "  952     1  40000  9.0 S    apt-get         apt-get -qq update\n",
    "big": "  953   451 300000  0.5 Sl   blender         /usr/bin/blender\n",
    "small": "  954   451   3000  0.1 S    xclock          xclock\n",
    "game": "  960   900 120000 95.0 Rl   supertuxkart    /usr/games/supertuxkart\n",
    "ssh": "  970   320  6000  0.0 Ss   sshd-session    sshd-session: tibi [priv]\n"
           "  971   970  4000  0.0 S    sshd-session    sshd-session: tibi@pts/1\n"
           "  972   971  4000  0.0 Ss   bash            -bash\n",
}
CGROUP = {470: "/user.slice/user-1000.slice/user@1000.service/ps3-desktop.slice/ps3-game-detect.service"}


def rows_of(text, uid=1000, cgroups=None):
    p = tmpdir() / "ps.txt"
    p.write_text(text)
    rows = proccheck.parse(str(p))
    shutil.rmtree(p.parent)
    for r in rows:
        r.setdefault("uid", 0 if r["pid"] < 400 or r["comm"] in ("apt-get", "sshd-session") and "[priv]" in r["args"]
                     or r["comm"] == "apt-get" else uid)
        r.setdefault("ticks", 0)
        r.setdefault("cgroup", (cgroups or CGROUP).get(r["pid"], "/user.slice/user-1000.slice/session-1.scope"))
    return rows


def fake_proc(root, rows, uptime=10000.0):
    """a /proc made of files for the rows (stat, status, cmdline, cgroup; ticks from cpu over a start of 1000 s)"""
    root = Path(root)
    write(root / "uptime", "%.2f 100.00\n" % uptime)
    write(root / "meminfo", "MemTotal: 220000 kB\nMemAvailable: 90000 kB\nSwapTotal: 500000 kB\nSwapFree: 480000 kB\n")
    write(root / "loadavg", "0.35 0.40 0.41 1/150 999\n")
    hz = os.sysconf("SC_CLK_TCK")
    page = os.sysconf("SC_PAGE_SIZE")
    for r in rows:
        start = 1000.0
        ticks = int(r["cpu"] / 100.0 * (uptime - start) * hz)
        f = [r["stat"][0], r["ppid"], r["pid"], 1, 0, -1, 0, 0, 0, 0, 0, ticks, 0, 0, 0, 20, 0, 1, 0, int(start * hz),
             1000, r["rss"] * 1024 // page]
        d = root / str(r["pid"])
        write(d / "stat", "%d (%s) %s\n" % (r["pid"], r["comm"], " ".join(map(str, f))))
        write(d / "status", "Name:\t%s\nUid:\t%d\t%d\t%d\t%d\n" % (r["comm"], r["uid"], r["uid"], r["uid"], r["uid"]))
        (d / "cmdline").write_bytes(b"" if r["args"].startswith("[") else r["args"].replace(" ", "\0").encode() + b"\0")
        write(d / "cgroup", "0::%s\n" % r["cgroup"])
    return root


XPROP_ROOT = "_NET_CLIENT_LIST: window id # 0x1000003, 0x1200004, 0x1400007, 0x1600002%s\n"
XPROP_WIN = {
    "0x1000003": '_NET_WM_PID = 451\nWM_CLASS = "pcmanfm", "Pcmanfm"\n_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP\n',
    "0x1200004": '_NET_WM_PID = 450\nWM_CLASS = "tint2", "Tint2"\n_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DOCK\n',
    "0x1400007": '_NET_WM_PID = 800\nWM_CLASS = "lxterminal", "Lxterminal"\n_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_NORMAL\n',
    "0x1600002": 'WM_CLASS = "xmessage", "Xmessage"\n_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_NORMAL\n',
    "0x1800010": '_NET_WM_PID = 950\nWM_CLASS = "Navigator", "Firefox-esr"\n_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_NORMAL\n',
}


def fake_xprop(bindir, windows, terminal=True):
    """xprop printing a window list: the desktop's (with walnut-rate's terminal) plus `windows` (ids of XPROP_WIN)"""
    extra = "".join(", " + w for w in windows)
    cases = "".join("  %s) cat <<'X'\n%sX\n;;\n" % (k, v) for k, v in XPROP_WIN.items())
    root = (XPROP_ROOT % extra).rstrip("\n")
    if not terminal:
        root = root.replace(" 0x1400007,", "")
    return write(Path(bindir) / "xprop", "#!/bin/bash\nif [ \"$1\" = -root ]; then echo '%s'; exit 0; fi\n"
                 "case $2 in\n%s  *) exit 1 ;;\nesac\n" % (root, cases), 0o755)


class CleanCheck(unittest.TestCase):
    def setUp(self):
        self.allow, self.deny = proccheck.load_allowlist(str(HARNESS / "allowed-processes.txt"))

    def blockers(self, extra="", game_slice=False, cgroups=None):
        rows = rows_of(DESKTOP + extra, cgroups=cgroups)
        return sorted({r["comm"] for r, _ in cleancheck.process_blockers(rows, 900, self.allow, self.deny, game_slice)})

    def test_desktop_is_clean(self):
        self.assertEqual(self.blockers(), [])
        self.assertEqual(self.blockers(EXTRA["small"]), [])   # an idle little program off the list: fine

    def test_blocked_and_named(self):
        self.assertEqual(self.blockers(EXTRA["firefox"]), ["firefox-esr"])
        self.assertEqual(self.blockers(EXTRA["badwolf"]), ["badwolf"])   # idle, but a browser
        self.assertEqual(self.blockers(EXTRA["apt"]), ["apt-get"])
        self.assertEqual(self.blockers(EXTRA["big"]), ["blender"])       # 300 MB, not on the list

    def test_exempt(self):
        self.assertEqual(self.blockers(EXTRA["game"]), [])   # the harness's child (under walnut-rate)
        other = EXTRA["firefox"].replace("  451 ", "  410 ")
        slice_cg = dict(CGROUP)
        slice_cg[950] = "/user.slice/user-1000.slice/user@1000.service/ps3-game.slice/ps3-game-7.scope"
        self.assertEqual(self.blockers(other, game_slice=True, cgroups=slice_cg), [])   # during a session: the game's slice
        self.assertEqual(self.blockers(other, game_slice=False, cgroups=slice_cg), ["firefox-esr"])   # before: a game running

    def test_ssh(self):
        rows = rows_of(DESKTOP + EXTRA["ssh"])
        self.assertEqual([r["pid"] for r in cleancheck.ssh_logins(rows)], [971])
        self.assertIn(971, cleancheck.ancestors(rows_of(DESKTOP + EXTRA["ssh"] + "  990   972 14000 3.0 S+   walnut-rate     "
                                                        "/usr/bin/python3 -I /usr/bin/walnut-rate play x\n"), 990))
        self.assertEqual(cleancheck.ssh_logins(rows_of(DESKTOP)), [])   # the listener is no login

    def test_windows(self):
        ids = cleancheck.parse_client_list(XPROP_ROOT % ", 0x1800010")
        self.assertEqual(ids, ["0x1000003", "0x1200004", "0x1400007", "0x1600002", "0x1800010"])
        self.assertEqual(cleancheck.parse_client_list("_NET_CLIENT_LIST:  not found.\n"), [])
        wins = [cleancheck.parse_window(w, XPROP_WIN[w]) for w in ids]
        self.assertEqual((wins[4]["pid"], wins[4]["cls"], wins[4]["types"]), (950, "Firefox-esr", ["_NET_WM_WINDOW_TYPE_NORMAL"]))
        rows = rows_of(DESKTOP + EXTRA["firefox"])
        ok = cleancheck.window_exempt(rows, 900, wins)
        self.assertEqual(sorted(ok & {451, 800, 801, 900}), [800, 900])   # the terminal, not the pcmanfm it was started from
        bad, notes = cleancheck.window_blockers(wins, ok)
        self.assertEqual([w["id"] for w in bad], ["0x1800010"])   # not the desktop, the panel, or walnut-rate's terminal
        self.assertEqual([w["id"] for w in notes], ["0x1600002"])
        self.assertEqual(cleancheck.window_name(bad[0], {r["pid"]: r for r in rows}), "Firefox-esr (pid 950, 302 MB)")
        # a file manager window of the desktop's pcmanfm (the terminal's parent) is a window like any other
        fm = cleancheck.parse_window("0x1a00001", '_NET_WM_PID = 451\nWM_CLASS = "pcmanfm", "Pcmanfm"\n'
                                     '_NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_NORMAL\n')
        self.assertEqual([w["id"] for w in cleancheck.window_blockers(wins + [fm], cleancheck.window_exempt(rows, 900, wins + [fm]))[0]],
                         ["0x1800010", "0x1a00001"])

    def test_busy(self):
        a = rows_of(DESKTOP)
        b = rows_of(DESKTOP)
        hz = os.sysconf("SC_CLK_TCK")
        for r in b:
            if r["comm"] == "tint2":
                r["ticks"] = int(0.9 * hz)    # 0.9 s of CPU in 1.5 s: 60%
            if r["comm"] == "walnut-rate":
                r["ticks"] = int(1.5 * hz)    # walnut-rate itself: exempt
        got = cleancheck.busy(a, b, 1.5, cleancheck.exempt(b, 900))
        self.assertEqual([(r["comm"], round(p)) for r, p in got], [("tint2", 60)])

    def test_check_fake_system(self):
        """check() on a fake /proc, /sys and xprop: a Firefox window and process makes it refuse, naming it"""
        d = tmpdir()
        try:
            rows = rows_of(DESKTOP + EXTRA["firefox"].replace("firefox-esr     /", "firefox-esr     /"))
            for r in rows:
                if r["pid"] == 900:
                    r["pid"] = os.getpid()   # this test is walnut-rate: its terminal and shell above it
            rows = [dict(r, ppid=os.getpid()) if r["ppid"] == 900 else r for r in rows]
            fake_proc(d / "proc", rows)
            hw = d / "sys" / "class" / "hwmon" / "hwmon0"
            write(hw / "name", "ps3_sm\n")
            write(hw / "temp1_input", "51000\n")
            write(hw / "temp1_label", "Cell BE\n")
            fake_xprop(d / "bin", ["0x1800010"])
            old = dict(os.environ)
            os.environ.update(CLEANCHECK_PROC=str(d / "proc"), CLEANCHECK_SYS=str(d / "sys"),
                              PATH="%s:%s" % (d / "bin", os.environ["PATH"]))
            try:
                f = cleancheck.check(os.getpid(), out_dir=str(d), sample=False)
                os.environ["CLEANCHECK_PROC"] = str(fake_proc(d / "proc2", [r for r in rows if r["pid"] != 950]))
                fake_xprop(d / "bin", [])
                clean = cleancheck.check(os.getpid(), out_dir=str(d), sample=False)
            finally:
                os.environ.clear()
                os.environ.update(old)
            blocks = [x.text for x in f if x.level == "block"]
            self.assertEqual(len(blocks), 1, f)
            self.assertTrue(blocks[0].startswith("close Firefox-esr (pid 950, 302 MB): not allowed"), blocks)
            self.assertEqual([x for x in f if x.close][0].close, ("window", "0x1800010", 950))
            self.assertTrue(any("Cell BE 51 C" in x.text for x in f if x.level == "note"), f)
            # the plain desktop with walnut-rate's terminal: ready
            self.assertEqual([x.text for x in clean if x.level == "block"], [])
            buf = __import__("io").StringIO()
            self.assertTrue(cleancheck.report(clean, buf))
            self.assertTrue(buf.getvalue().endswith("ready\n"), buf.getvalue())
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_close_window(self):
        """WM_DELETE_WINDOW, as the check's --close sends it, closes a Tk window on a private X server"""
        xvnc = shutil.which("Xvnc") or shutil.which("Xvfb")
        if not xvnc:
            self.skipTest("no Xvnc or Xvfb")
        try:
            import tkinter  # noqa: F401
        except ImportError:
            self.skipTest("no tkinter")
        disp = ":%d" % (60 + os.getpid() % 30)
        args = [xvnc, disp, "-SecurityTypes", "None", "-nolisten", "tcp"] if "vnc" in xvnc.lower() else [xvnc, disp, "-nolisten", "tcp"]
        x = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(1.5)
            env = dict(os.environ, DISPLAY=disp)
            app = subprocess.Popen([sys.executable, "-c", "import tkinter as tk\nr = tk.Tk()\n"
                                    "r.protocol('WM_DELETE_WINDOW', lambda: (print('deleted', flush=True), r.destroy()))\n"
                                    "r.update()\nprint(r.wm_frame(), flush=True)\nr.mainloop()\n"],
                                   env=env, stdout=subprocess.PIPE, text=True)
            wid = app.stdout.readline().strip()
            old = os.environ.get("DISPLAY")
            os.environ["DISPLAY"] = disp
            try:
                self.assertTrue(cleancheck.close_window(wid))
            finally:
                if old is None:
                    os.environ.pop("DISPLAY")
                else:
                    os.environ["DISPLAY"] = old
            self.assertEqual(app.wait(10), 0)
            self.assertEqual(app.stdout.read().strip(), "deleted")
        finally:
            x.send_signal(signal.SIGTERM)
            x.wait(10)


class Watch(unittest.TestCase):
    def test_watch_lines(self):
        d = tmpdir()
        try:
            fake_proc(d / "proc", rows_of(DESKTOP + EXTRA["apt"] + EXTRA["firefox"]))
            fake_xprop(d / "bin", ["0x1800010"])
            env = dict(os.environ, CLEANCHECK_PROC=str(d / "proc"), WATCH_TICKS="1", WATCH_EVERY="0.1",
                       PATH="%s:%s" % (d / "bin", os.environ["PATH"]))
            p = subprocess.run([sys.executable, "-I", str(HARNESS / "cleancheck.py"), "watch", "900", str(time.time())],
                               env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(p.returncode, 0, p.stderr)
            lines = [line.split("\t") for line in p.stdout.splitlines() if not line.startswith("#")]
            self.assertEqual(sorted((x[1], x[2].split(" ")[0]) for x in lines),
                             [("process", "apt-get"), ("process", "firefox-esr"), ("window", "Firefox-esr")], p.stdout)
            self.assertIn("a window that names no process: Xmessage", p.stdout)
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def client_session(self, d, extra):
        """client.sh in a played session (the bench selftest's fake game) with BENCH_WATCH=1"""
        sd = d / "s"
        write(sd / "game.sh", '#!/bin/sh\necho starting; sleep 0.3; echo "map go"; sleep 1.2\n', 0o755)
        write(sd / "r" / "fs.sh", "CMD=(/bin/false); COMM=game.sh; SOUND_AT=99\n")
        write(sd / "r" / "fs.test.toml", 'process = "game.sh"\nhome = []\nplay_mark = "^map go"\ncommand = ["%s"]\nhud = false\n'
              % (sd / "game.sh"))
        desktop = "".join(line + "\n" for line in DESKTOP.splitlines() if line.split()[0] not in ("800", "801", "900"))
        fake_proc(d / "proc", rows_of(desktop + extra))
        fake_xprop(d / "bin", ["0x1800010"] if "firefox" in extra else [], terminal=False)
        env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD", "GALLIUM_HUD_DUMP_DIR")}
        env.update(BENCH_RECIPES=str(sd / "r"), BENCH_BACKUP=str(sd / "b"), BENCH_DIR=str(sd / "run"), BENCH_SESSION="1",
                   BENCH_PS3GAME="env", BENCH_INPUTLOG="0", BENCH_PROCS="0", BENCH_WATCH="1", WATCH_EVERY="0.3",
                   CLEANCHECK_PROC=str(d / "proc"), PATH="%s:%s" % (d / "bin", os.environ["PATH"]))
        p = subprocess.run(["bash", str(HARNESS / "client.sh"), "fs", "base"], env=env, capture_output=True, text=True, timeout=60)
        return p, wr.kv(sd / "run" / "bench.txt"), sd / "run"

    def test_client_marks_interfered(self):
        d = tmpdir()
        try:
            p, b, run = self.client_session(d, EXTRA["firefox"])
            self.assertEqual(p.returncode, 0, p.stdout[-500:])
            self.assertEqual((b.get("watch"), b.get("interfered")), ("on", "1"), b)
            self.assertIn("window: Firefox-esr", b.get("interference", ""))
            self.assertIn("process: firefox-esr", b.get("interference", ""))
            self.assertIn("THE SESSION WAS DISTURBED", p.stdout)
            import report
            r = report.run(run)
            self.assertEqual(r.get("intf"), 1, r)
        finally:
            shutil.rmtree(d, ignore_errors=True)
        d = tmpdir()
        try:
            p, b, run = self.client_session(d, "")
            self.assertEqual((b.get("watch"), b.get("interfered")), ("on", "0"), (b, p.stdout[-500:]))
            self.assertNotIn("interference", b)
        finally:
            shutil.rmtree(d, ignore_errors=True)


# ---- the export ------------------------------------------------------------------------------------

def xwd(w, h, rgb, bpp=32, msb=True, pad=0):
    """an X window dump of rgb (bytes, w*h*3) as the X server writes one: TrueColor, masks ff0000/ff00/ff"""
    n = bpp // 8
    bpl = w * n + pad
    name = b"test\0"
    hsize = 100 + len(name)
    hdr = struct.pack(">25I", hsize, 7, 2, 24, w, h, 0, 1 if msb else 0, 32, 1 if msb else 0, 32, bpp, bpl, 4,
                      0xff0000, 0xff00, 0xff, 8, 256, 2, w, h, 0, 0, 0)
    cmap = struct.pack(">IHHHBB", 0, 0, 0, 0, 7, 0) * 2
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            r, g, b = rgb[(y * w + x) * 3:(y * w + x) * 3 + 3]
            px = bytes((0, r, g, b)) if n == 4 else bytes((r, g, b))
            row += px if msb else px[::-1]
        rows.append(bytes(row) + b"\xee" * pad)
    return hdr + name + cmap + b"".join(rows)


def png_rgb(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    return w, h, b"".join(raw[y * stride + 1:(y + 1) * stride] for y in range(h))


class Export(unittest.TestCase):
    IDENT = (("Test Er", "tester42"), ("2001:db8::42", "203.0.113.7", "myconsole"))
    HOME = "/home/tester42"

    def test_xwd_png(self):
        d = tmpdir()
        try:
            w, h = 7, 5
            rgb = bytes((x * 30 + y) % 256 for y in range(h) for x in range(w) for _ in range(3))
            rgb = bytes(((i * 37) ^ (i >> 3)) % 256 for i in range(w * h * 3))
            for bpp, msb, pad in ((32, True, 0), (32, False, 0), (24, True, 3), (32, True, 4)):
                src = d / "s.xwd"
                src.write_bytes(xwd(w, h, rgb, bpp, msb, pad))
                self.assertEqual(wr.xwd_to_png(src, d / "s.png"), (w, h))
                self.assertEqual(png_rgb(d / "s.png"), (w, h, rgb), (bpp, msb, pad))
            bad = d / "bad.xwd"
            bad.write_bytes(xwd(w, h, rgb)[:120])
            with self.assertRaises(ValueError):
                wr.xwd_to_png(bad, d / "b.png")
            if shutil.which("magick"):   # ImageMagick's own reading of our big-endian dump: the same pixels
                p = subprocess.run(["magick", str(src), "-depth", "8", "rgb:-"], capture_output=True)
                if p.returncode == 0:
                    self.assertEqual(p.stdout, rgb)
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def session(self):
        for g, s in (("chromium-bsu", "2026-10-29-session"), ("bomberclone", "2026-10-26-session")):
            sd = REPO / "gamedb/games" / g / "rating/sessions" / s
            if sd.is_dir() and evidence.read_import(sd) and not evidence.lfs_pointers(sd):
                return g, sd
        self.skipTest("no gamedb session with its Git LFS objects (git lfs pull)")

    def rebuilt_run(self, d):
        """a gamedb session rebuilt as a run (evidence.rebuild), the console's identity written into it"""
        g, sd = self.session()
        run = evidence.rebuild(sd, d / "out")
        (run / "REVIEW.md").rename(d / "REVIEW.md")      # the review is made on the PC, after the export
        with open(run / "game.log", "a") as f:
            f.write("loading %s/.local/share/game/save1 for tester42@myconsole\n" % self.HOME)
            f.write("net: myconsole.local 203.0.113.7 [2001:db8::42] 192.168.1.20 aa:bb:cc:dd:ee:ff Test Er\n")
        write(run / "clean-check.txt", "ready\n    memory available 90 MB (checked by tester42 on myconsole)\n")
        write(run / "watch.log", "# watch: every 10 s, harness pid 1; SSH logins at the start: 0\n")
        write(run / "fresh-home" / ".config" / "game" / "tester42.ini", "path=%s/x\n" % self.HOME)
        (run / "pgid").write_text("123\n")   # never in a bundle
        return g, sd, run

    def export(self, run, dest, nick="tester", publish="yes"):
        old = wr.console_identity
        wr.console_identity = lambda run=None: (self.IDENT[0], self.IDENT[1], self.HOME, [])
        try:
            with open(os.devnull, "w") as null:
                so = sys.stdout
                sys.stdout = null
                try:
                    return wr.cmd_export([str(run), str(dest), "--nick", nick, "--publish", publish, "--yes"])
                finally:
                    sys.stdout = so
        finally:
            wr.console_identity = old

    def folder(self, dest):
        """the one submission folder export wrote under dest: (path, game, id)"""
        found = [f for f in (dest / sc.TOP).glob("*/*") if f.is_dir()]
        self.assertEqual(len(found), 1, found)
        return found[0], found[0].parent.name, found[0].name

    def test_scrub(self):
        d = tmpdir()
        try:
            g, sd, run = self.rebuilt_run(d)
            (d / "usb").mkdir()
            self.assertEqual(self.export(run, d / "usb"), 0)
            folder, game, sid = self.folder(d / "usb")
            self.assertEqual(game, g)
            self.assertTrue(sc.ID_RE.match(sid), sid)
            self.assertEqual(sid[:10], "-".join(re.search(r"(\d{4})(\d{2})(\d{2})T", run.name).groups()))
            files = sc.folder_files(folder)             # plain files only, nothing executable
            names = sorted(files)
            for need in ("bench.txt", "versions", "game.log", "session.toml", "clean-check.txt", "watch.log", "submitter.toml"):
                self.assertIn(need, names)
            self.assertNotIn("pgid", names)
            self.assertNotIn("REVIEW.md", names)
            self.assertIn("fresh-home/.config/game/player.ini", names)   # the path scrubbed too
            shots = [n for n in names if n.startswith("shots/") and n != "shots/geometry.tsv"]
            self.assertTrue(shots and all(n.endswith(".jpg") and files[n][:3] == b"\xff\xd8\xff" for n in shots), shots)
            for n in names:
                for s in ("tester42", "myconsole"):
                    self.assertNotIn(s, n)
            for n, data in files.items():
                if n.endswith(".jpg"):
                    continue
                text = data.decode("utf-8", "replace")
                for s in ("tester42", "Test Er", "myconsole", "203.0.113.7", "2001:db8::42", "192.168.1.20",
                          "aa:bb:cc:dd:ee:ff", "/home/"):
                    self.assertNotIn(s, text, n)
                self.assertEqual(evidence.leftovers(text, self.IDENT), [], n)
            self.assertIn("loading ~/.local/share/game/save1 for player@host", files["game.log"].decode())
            sub = tomllib.loads(files["submitter.toml"].decode())
            self.assertEqual((sub["nickname"], sub["publish"], sub["game"], sub["id"], sub["run"]),
                             ("tester", True, g, sid, run.name))
            self.assertNotIn("contact", sub)                          # a pull request is public: no contact field
            self.assertEqual(sc.check_folder(folder, sc.games_dir_exists(REPO / "gamedb/games"))["id"], sid)
            self.assertIn("exported ", (run / wr.LOG).read_text())
            # the same run exported again: the same id; a nickname that is the user name, or no to publishing:
            # refused, nothing written
            (d / "usb2").mkdir()
            self.assertEqual(self.export(run, d / "usb2"), 0)
            self.assertEqual(self.folder(d / "usb2")[2], sid)
            (d / "usb3").mkdir()
            self.assertEqual(self.export(run, d / "usb3", nick="tester42"), 1)
            self.assertEqual(self.export(run, d / "usb3", publish="no"), 2)
            self.assertEqual(list((d / "usb3").iterdir()), [])
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_shots_fit(self):
        """xwd and PNG screenshots become JPEGs; over SHOTS_MAX they are made smaller until they fit"""
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("no Pillow")
        d = tmpdir()
        try:
            w, h = 64, 36
            rgb = bytes(((i * 37) ^ (i >> 3)) % 256 for i in range(w * h * 3))
            (d / "s.xwd").write_bytes(xwd(w, h, rgb))
            wr.shot_jpeg(d / "s.xwd", d / "a.jpg")
            wr.shot_jpeg(d / "s.xwd", d / "b.jpg", 0.5)
            with Image.open(d / "a.jpg") as a, Image.open(d / "b.jpg") as b:
                self.assertEqual((a.format, a.size, b.size), ("JPEG", (w, h), (w // 2, h // 2)))
                self.assertFalse(a.info.get("exif"))
            run = d / "ps3-bench-20261030T120000Z-session-chromium-bsu-base"
            (run / "shots").mkdir(parents=True)
            lines = ["kind=session", "game=chromium-bsu"]
            for k in range(3):
                Image.effect_noise((400, 300), 90).convert("RGB").save(run / "shots" / ("shot-%ds.png" % k))
                lines.append("shot=%d:shots/shot-%ds.xwd:x" % (k, k))
            write(run / "bench.txt", "\n".join(lines) + "\n")
            old = sc.SHOTS_MAX
            sizes = {}
            try:
                for cap in (old, 120 << 10):
                    sc.SHOTS_MAX = cap
                    out = d / ("out-%d" % cap)
                    files, problems = wr.build_folder(run, out, ((), ()), "/home/x", "", "2026-10-30-abcdef")
                    self.assertEqual(problems, [])
                    sizes[cap] = sum(s for r, s in files if r.endswith(".jpg"))
                    self.assertLessEqual(sizes[cap], cap)
                    with Image.open(out / "shots/shot-0s.jpg") as im:
                        sizes[cap, "w"] = im.size[0]
            finally:
                sc.SHOTS_MAX = old
            self.assertEqual(sizes[old, "w"], 400)
            self.assertLess(sizes[120 << 10, "w"], 400)
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_round_trip(self):
        """export -> the folder as a run -> import-run.py: the same record as the run itself; interfered=1: refused"""
        if not shutil.which("magick"):
            self.skipTest("no ImageMagick (import-run.py makes the JPEGs with it)")
        ir = import_run_module()
        d = tmpdir()
        try:
            g, sd, run = self.rebuilt_run(d)
            (d / "usb").mkdir()
            self.assertEqual(self.export(run, d / "usb"), 0)
            folder = self.folder(d / "usb")[0]
            got = d / "pc" / run.name
            shutil.copytree(folder, got)
            shutil.copyfile(d / "REVIEW.md", got / "REVIEW.md")    # the PC's review
            shutil.copyfile(d / "REVIEW.md", run / "REVIEW.md")
            res = {}
            for k, r in (("run", run), ("bundle", got)):
                c = content_dir(d / ("content-" + k), g)
                quiet(ir.import_run, r, g, c, session=sd.name)
                res[k] = (c / g / "rating/sessions" / sd.name / "result.toml").read_text()
            self.assertEqual(res["bundle"], res["run"])
            self.assertEqual(res["run"], (sd / "result.toml").read_text())   # and the record gamedb has
            with open(got / "bench.txt", "a") as f:
                f.write("interfered=1\ninterference=watch: window: Firefox-esr (firefox-esr, pid 950, 302 MB)\n")
            c = content_dir(d / "content-x", g)
            with self.assertRaises(ir.Refused) as e:
                ir.import_run(got, g, c, session=sd.name)
            self.assertIn("Firefox", str(e.exception))
        finally:
            shutil.rmtree(d, ignore_errors=True)


def quiet(f, *a, **kw):
    """f(*a) with its output kept and no terminal to ask on: (its result or the SystemExit, the output)"""
    import contextlib
    import io
    buf = io.StringIO()
    old = sys.stdin
    sys.stdin = io.StringIO("")
    try:
        with contextlib.redirect_stdout(buf):
            try:
                rc = f(*a, **kw)
            except SystemExit as e:
                rc = e
    finally:
        sys.stdin = old
    return rc, buf.getvalue()


def at(call, i=5):
    """a stubbed gh call's argument i (5: the API path)"""
    return call["args"][i] if len(call["args"]) > i else ""


def import_run_module():
    spec = importlib.util.spec_from_file_location("import_run", REPO / "tools/gamedb/import-run.py")
    ir = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ir)
    return ir


def content_dir(c, g):
    """a games directory with game g's index.md and build.toml (what import-run.py needs of it)"""
    (c / g).mkdir(parents=True)
    for f in ("index.md", "build.toml"):
        if (REPO / "gamedb/games" / g / f).exists():
            shutil.copyfile(REPO / "gamedb/games" / g / f, c / g / f)
    return c


class Cli(unittest.TestCase):
    def test_list(self):
        rows = wr.games_list(cat={})
        ids = [r[0] for r in rows]
        self.assertIn("supertuxkart", ids)
        self.assertNotIn("loom", ids)        # a played test without a recipe: no session can run it
        self.assertTrue(all((REPO / "gamedb/games" / g / "rating/test.toml").is_file() for g in ids))

    def test_submit_and_usage(self):
        old = wr.OUT
        wr.OUT = Path("/nonexistent/walnut-rate-test")
        try:
            self.assertEqual(quiet(wr.main, ["submit"])[0], 2)                      # no session on this machine
            self.assertEqual(quiet(wr.main, ["submit", "--repo", "not a repo"])[0], 2)
        finally:
            wr.OUT = old
        self.assertEqual(wr.target_repo({}), "nanobyte-dev/walnut-linux")
        self.assertEqual(wr.target_repo({"--repo": "someone/walnut-test"}), "someone/walnut-test")
        self.assertEqual(wr.main(["submit", "a", "b"]), 2)
        self.assertEqual(wr.main(["export", "x", "y", "--repo", "a/b"]), 2)
        self.assertEqual(wr.main([]), 2)
        self.assertEqual(wr.main(["play"]), 2)
        self.assertEqual(wr.main(["play", "../x"]), 2)
        self.assertEqual(wr.main(["export", "x"]), 2)

    def test_session_env(self):
        old = dict(os.environ)
        os.environ.update(BENCH_STALLS="1", SESSION_FRESH="0", GALLIUM_HUD="fps", PS3_BENCH="/tmp/evil", HOME_X="kept")
        try:
            env = wr.session_env("/tmp/run", "/usr/lib/ps3/bin/ps3-bench.sh")
        finally:
            os.environ.clear()
            os.environ.update(old)
        self.assertNotIn("BENCH_STALLS", env)
        self.assertNotIn("SESSION_FRESH", env)
        self.assertNotIn("GALLIUM_HUD", env)
        self.assertEqual(env["HOME_X"], "kept")
        self.assertEqual((env["BENCH_WATCH"], env["SESSION_FORM"], env["PS3_BENCH"], env["SESSION_RUN_FILE"]),
                         ("1", "0", "/usr/lib/ps3/bin/ps3-bench.sh", "/tmp/run"))
        self.assertTrue(env["BENCH_ROOT_HELPER"].endswith("walnut-rate-helper"))

    def test_run_names(self):
        self.assertTrue(wr.RUN_RE.match("ps3-bench-20261029T203517Z-session-chromium-bsu-base"))
        self.assertFalse(wr.RUN_RE.match("ps3-bench-20261029T203517Z-xmoto-base"))
        self.assertEqual(wr.RUN_RE.pattern.strip("^$"), sc.RUN_RE.pattern.strip("\\AZ"))


# ---- the submission check (tools/gamedb/submission_check.py) ---------------------------------------------

RUN = "ps3-bench-20261030T120000Z-session-chromium-bsu-base"
SID = "2026-10-30-k3f9q2"
JPEG = b"\xff\xd8\xff\xe0" + b"\0" * 60 + b"\xff\xd9"


def good_files(game="chromium-bsu", sid=SID, run=RUN, **sub):
    s = dict(nickname="tester", publish=True, walnut_rate="0.1", exported="2026-10-30T12:30:00Z", run=run, game=game,
             id=sid)
    s.update(sub)
    return {"bench.txt": b"kind=session\ngame=%s\nvariant=base\nshot=60:shots/shot-60s.xwd:x\n" % game.encode(),
            "versions": b"chromium-bsu 0.9.16.1-4+ps3.1\nmesa 25.2+ps3.40\n",
            "game.log": b"hello from player@host:~$\n",
            "Xorg.0.log": b"[  27.5] Current Operating System: Linux walnut 6.12.111+ps3.874 #1 SMP ppc64\n",
            "session.toml": b"saved = true\n",
            "hud/frametime_(ms)": b"1\n2\n",
            "shots/shot-60s.jpg": JPEG,
            "shots/geometry.tsv": b"t\tx\n",
            "fresh-home/.chromium-bsu": b"x=1\n",
            "submitter.toml": evidence.toml_text({k: v for k, v in s.items() if v is not None}, ["t"]).encode()}


def pr_changes(files, top="submissions/chromium-bsu/" + SID):
    return [("000000", "100644", "%040x" % i, "A", "%s/%s" % (top, r)) for i, r in enumerate(sorted(files))]


class SubmissionCheck(unittest.TestCase):
    EXISTS = staticmethod(sc.games_dir_exists(REPO / "gamedb/games"))

    def refused(self, why, files=None, game="chromium-bsu", sid=SID, exists=None):
        with self.assertRaises(sc.Refused) as e:
            sc.check_files(game, sid, good_files() if files is None else files, exists or self.EXISTS)
        self.assertTrue(any(why in r for r in e.exception.reasons), e.exception.reasons)
        return e.exception.reasons

    def test_good(self):
        info = sc.check_files("chromium-bsu", SID, good_files(), self.EXISTS)
        self.assertEqual((info["game"], info["id"], info["run"], info["nickname"], info["files"]),
                         ("chromium-bsu", SID, RUN, "tester", 10))
        self.assertEqual(sc.check_changes(pr_changes(good_files()))[:2], ("chromium-bsu", SID))

    def test_lists_agree(self):
        """walnut-rate writes what the check knows"""
        self.assertEqual(wr._checker().EXTRA_FILES[1], wr.CHECK)
        self.assertEqual(evidence.RAW_GLOBS, ("hud/*",))           # the check's HUD_RE
        for r in evidence.RAW_FILES + sc.EXTRA_FILES:
            self.assertEqual(sc.kind_of(r)[0], "text", r)

    def test_refusals(self):
        g = good_files()
        # the pull request: a second folder, a second game, a file elsewhere, a change, a link, a submodule
        for chs, why in ((pr_changes(g) + pr_changes({"x": 1}, "submissions/chromium-bsu/2026-10-30-zzzzzz"),
                          "more than one folder"),
                         (pr_changes(g) + pr_changes({"bench.txt": 1}, "submissions/xmoto/" + SID), "more than one game"),
                         (pr_changes(g) + [("000000", "100644", "0" * 40, "A", "tools/gamedb/check.py")],
                          "outside submissions"),
                         (pr_changes(g) + [("100644", "100644", "0" * 40, "M", "README.md")], "outside submissions"),
                         (pr_changes(g)[:-1] + [("100644", "100644", "0" * 40, "M",
                                                 "submissions/chromium-bsu/%s/submitter.toml" % SID)], "only adds"),
                         (pr_changes(g) + [("000000", "120000", "0" * 40, "A",
                                            "submissions/chromium-bsu/%s/shots/x.jpg" % SID)], "a link"),
                         (pr_changes(g) + [("000000", "160000", "0" * 40, "A",
                                            "submissions/chromium-bsu/%s/sub" % SID)], "a submodule"),
                         (pr_changes(g) + [("000000", "100755", "0" * 40, "A",
                                            "submissions/chromium-bsu/%s/cmd" % SID)], "executable"),
                         ([], "changes nothing")):
            with self.assertRaises(sc.Refused) as e:
                sc.check_changes(chs)
            self.assertTrue(any(why in r for r in e.exception.reasons), (why, e.exception.reasons))
        # the folder
        self.refused("no played test", exists=lambda game: False)
        self.refused("not a game id", game="../etc")
        self.refused("not a submission id", sid="20261030")
        self.refused("not a JPEG", dict(g, **{"shots/shot-60s.jpg": b"\x89PNG\r\n\x1a\n" + b"\0" * 40}))
        self.refused("not a file a submission has", dict(g, **{"shots/shot-60s.png": b"\x89PNG"}))
        self.refused("not a file a submission has", dict(g, **{"run.sh": b"#!/bin/sh\n"}))
        self.refused("not a text file", dict(g, **{"cmd": b"a\0b"}))
        self.refused("at most", dict(g, **{"game.log": b"x" * (sc.FILE_MAX + 1)}))
        self.refused("at most", dict(g, **{"fresh-home/big": b"x" * (evidence.FRESH_MAX + 1)}))
        many = dict(g, **{"shots/s%d.jpg" % i: JPEG[:-2] + b"\0" * (1 << 20) + JPEG[-2:] for i in range(16)})
        self.refused("the screenshots", many)
        # the scrub: a host name, a user@host, a home path, LAN and MAC addresses
        self.refused("a host name", dict(g, **{"Xorg.0.log": b"Current Operating System: Linux myconsole 6.12 #1\n"}))
        self.refused("a host name", dict(g, **{"versions": g["versions"] + b"Linux myconsole 6.12.111 #1 SMP\n"}))
        self.refused("user@host", dict(g, **{"game.log": b"tester42@myconsole:~$ ls\n"}))
        self.refused("a home directory", dict(g, **{"dmesg.log": b"open /home/tester42/x\n"}))
        self.refused("a LAN address", dict(g, **{"out.log": b"connected to 192.168.1.20\n"}))
        self.refused("a MAC address", dict(g, **{"dmesg.log": b"eth0: aa:bb:cc:dd:ee:ff\n"}))
        # submitter.toml: no consent, unknown fields (a contact is public on GitHub), another game, not TOML
        self.refused("no consent", good_files(publish=False))
        self.refused("no consent", good_files(publish=None))
        self.refused("unknown field", good_files(contact="me@example.org"))
        self.refused("the folder's", good_files(game="xmoto"))
        self.refused("one line", good_files(nickname="a\nb"))
        self.refused("not TOML", dict(g, **{"submitter.toml": b"nickname = \n"}))
        self.refused("no submitter.toml", {k: v for k, v in g.items() if k != "submitter.toml"})
        self.refused("not a played session", dict(g, **{"bench.txt": b"kind=bench\ngame=chromium-bsu\n"}))
        # a word of the run itself (a package's name) is not a host name the scrub missed
        sc.check_files("chromium-bsu", SID, dict(g, **{"Xorg.0.log": b"Current Operating System: Linux mesa 6.12\n"}),
                       self.EXISTS)

    def test_folder_on_disk(self):
        d = tmpdir()
        try:
            f = d / "submissions" / "chromium-bsu" / SID
            for r, data in good_files().items():
                (f / r).parent.mkdir(parents=True, exist_ok=True)
                (f / r).write_bytes(data)
            self.assertEqual(sc.check_folder(f, self.EXISTS)["files"], 10)
            self.assertEqual(quiet(sc.main, ["folder", str(f)])[0], 0)
            (f / "shots" / "evil.jpg").symlink_to("/etc/passwd")
            with self.assertRaises(sc.Refused) as e:
                sc.check_folder(f, self.EXISTS)
            self.assertIn("a link", str(e.exception))
            (f / "shots" / "evil.jpg").unlink()
            (f / "cmd").write_text("x\n")
            (f / "cmd").chmod(0o755)
            rc, out = quiet(sc.main, ["folder", str(f)])
            self.assertEqual(rc, 1)
            self.assertIn("executable", out)
        finally:
            shutil.rmtree(d, ignore_errors=True)


# ---- a pull request: walnut-rate submit (gh stubbed) -> a local "GitHub" -> submissions.py show, accept ----

GH_STUB = r'''
import hashlib, json, os, sys
from pathlib import Path
st = Path(os.environ["GH_STUB"])
args = sys.argv[1:]
body = json.loads(sys.stdin.read() or "null") if "--input" in args else None
with open(st / "calls.jsonl", "a") as f:
    f.write(json.dumps({"args": args, "body": body}) + "\n")
if args[:2] == ["auth", "status"]:
    sys.exit(0 if (st / "logged-in").exists() else 1)
if args[:1] != ["api"] or "-X" not in args:
    sys.exit(2)
method, path = args[args.index("-X") + 1], args[5]
key = "%s %s" % (method, path)
canned = json.loads((st / "responses.json").read_text())
if key in canned:
    print(json.dumps(canned[key]))
    sys.exit(0)
def sha(x):
    return hashlib.sha1(json.dumps(x, sort_keys=True).encode()).hexdigest()
if method == "POST" and path.endswith("/git/blobs"):
    s = sha(body)
    (st / "blobs").mkdir(exist_ok=True)
    (st / "blobs" / s).write_text(body["content"])
    print(json.dumps({"sha": s}))
elif method == "POST" and path.endswith(("/git/trees", "/git/commits", "/git/refs")):
    print(json.dumps({"sha": sha(body)}))
elif method == "POST" and path.endswith("/pulls"):
    print(json.dumps({"number": 1, "html_url": "https://github.com/%s/%s/pull/1" % tuple(path.split("/")[1:3])}))
elif method in ("POST", "PATCH"):
    print(json.dumps({}))
else:
    sys.stderr.write("gh: HTTP 404: Not Found (%s)\n" % path)
    sys.exit(1)
'''


class PullRequest(unittest.TestCase):
    """walnut-rate submit through a stubbed gh: a fork, one commit adding only the folder, the pull request; that
    commit as the merge commit of a local repository standing in for GitHub; submissions.py show (the checker on it)
    and accept (gh stubbed): the same record as our own import, committed, the pull request commented on and closed"""

    def gh_stub(self, d, responses):
        st = d / "gh"
        (st / "bin").mkdir(parents=True)
        write(st / "bin" / "gh", "#!%s -I\n%s" % (sys.executable, GH_STUB), 0o755)
        (st / "responses.json").write_text(json.dumps(responses))
        env = {"GH_STUB": str(st), "PATH": "%s:%s" % (st / "bin", os.environ["PATH"])}
        old = {k: os.environ.get(k) for k in env}
        os.environ.update(env)
        self.addCleanup(lambda: [os.environ.pop(k) if v is None else os.environ.__setitem__(k, v) for k, v in old.items()])
        self.assertEqual(shutil.which("gh"), str(st / "bin" / "gh"))       # never the real one
        return st

    def calls(self, st):
        return [json.loads(x) for x in (st / "calls.jsonl").read_text().splitlines()] if (st / "calls.jsonl").exists() else []

    def git(self, d, *a):
        return subprocess.run(["git", "-C", str(d)] + list(a), check=True, capture_output=True, text=True).stdout.strip()

    def test_round_trip(self):
        if not shutil.which("magick") or not shutil.which("git"):
            self.skipTest("no ImageMagick or git")
        import base64
        ex = Export()
        d = tmpdir()
        try:
            g, sd, run = ex.rebuilt_run(d)
            up = "nanobyte-dev/walnut-linux"
            st = self.gh_stub(d, {
                "GET user": {"login": "tester"},
                "GET repos/" + up: {"owner": {"login": "nanobyte-dev"}, "default_branch": "main"},
                "GET repos/%s/git/ref/heads/main" % up: {"object": {"sha": "b" * 40}},
                "GET repos/%s/git/commits/%s" % (up, "b" * 40): {"tree": {"sha": "c" * 40}},
                "POST repos/%s/forks" % up: {"full_name": "tester/walnut-linux"},
                "GET repos/tester/walnut-linux/git/ref/heads/main": {"object": {"sha": "b" * 40}}})
            old_ident, old_out = wr.console_identity, wr.OUT
            wr.console_identity = lambda run=None: (ex.IDENT[0], ex.IDENT[1], ex.HOME, [])
            wr.OUT = run.parent
            self.addCleanup(setattr, wr, "console_identity", old_ident)
            self.addCleanup(setattr, wr, "OUT", old_out)
            sub = [run.name, "--nick", "tester", "--publish", "yes", "--yes"]
            # no gh, then gh not logged in: the steps by hand, nothing sent
            old_cmd = wr.gh_cmd
            wr.gh_cmd = lambda: None
            rc, out = quiet(wr.cmd_submit, sub)
            wr.gh_cmd = old_cmd
            self.assertEqual(rc, 1, out)
            self.assertIn("isn't installed", out)
            self.assertIn("Upload files", out)
            rc, out = quiet(wr.cmd_submit, sub)
            self.assertEqual(rc, 1, out)
            self.assertIn("gh auth login", out)
            self.assertFalse([c for c in self.calls(st) if "POST" in c["args"]])
            # logged in: the pull request
            (st / "logged-in").touch()
            rc, out = quiet(wr.cmd_submit, sub)
            self.assertEqual(rc, 0, out)
            url = wr.submissions(run)[-1][1]
            self.assertEqual(url, "https://github.com/nanobyte-dev/walnut-linux/pull/1")
            self.assertIn("sent: " + url, quiet(wr.cmd_status, [])[1])
            self.assertEqual(quiet(wr.cmd_submit, [run.name, "--yes"])[0], 1)          # sent already
            calls = self.calls(st)
            posts = {at(c): c["body"] for c in calls if c["args"][:3] == ["api", "-X", "POST"]}
            tree = posts["repos/tester/walnut-linux/git/trees"]
            sid = wr.submission_id(run)
            top = "submissions/%s/%s/" % (g, sid)
            self.assertEqual(tree["base_tree"], "c" * 40)
            self.assertTrue(all(e["path"].startswith(top) and e["mode"] == "100644" for e in tree["tree"]))
            self.assertEqual(posts["repos/tester/walnut-linux/git/commits"]["parents"], ["b" * 40])
            pr = posts["repos/%s/pulls" % up]
            self.assertEqual((pr["head"], pr["base"], pr["maintainer_can_modify"]),
                             ("tester:submission-%s-%s" % (g, sid), "main", False))
            self.assertIn("publish = true", pr["body"])
            # "GitHub": a base commit with the game's played test, the pull request's commit of exactly the blobs
            # sent, refs/pull/1/merge
            gh_repo = d / "github"
            gh_repo.mkdir()
            self.git(gh_repo, "init", "-q", "-b", "main")
            for k, v in (("user.name", "t"), ("user.email", "t@example.org"), ("uploadpack.allowFilter", "true"),
                         ("uploadpack.allowAnySHA1InWant", "true")):
                self.git(gh_repo, "config", k, v)
            t = gh_repo / "gamedb/games" / g / "rating/test.toml"
            t.parent.mkdir(parents=True)
            shutil.copyfile(REPO / "gamedb/games" / g / "rating/test.toml", t)
            self.git(gh_repo, "add", "-A")
            self.git(gh_repo, "commit", "-qm", "base")
            self.git(gh_repo, "switch", "-qc", "pr")
            for e in tree["tree"]:
                f = gh_repo / e["path"]
                f.parent.mkdir(parents=True, exist_ok=True)
                f.write_bytes(base64.b64decode((st / "blobs" / e["sha"]).read_text()))
            self.git(gh_repo, "add", "-A")
            self.git(gh_repo, "commit", "-qm", "a played session")
            head = self.git(gh_repo, "rev-parse", "HEAD")
            self.git(gh_repo, "switch", "-q", "main")
            self.git(gh_repo, "merge", "-q", "--no-ff", "-m", "merge", "pr")
            self.git(gh_repo, "update-ref", "refs/pull/1/merge", "HEAD")
            self.git(gh_repo, "reset", "-q", "--hard", "HEAD^1")
            # our side
            ours = d / "ours"
            content = content_dir(ours / "gamedb" / "games", g)
            self.git(ours, "init", "-q", "-b", "main")
            for k, v in (("user.name", "t"), ("user.email", "t@example.org"), ("commit.gpgsign", "false")):
                self.git(ours, "config", k, v)
            self.git(ours, "add", "-A")
            self.git(ours, "commit", "-qm", "base")
            (st / "responses.json").write_text(json.dumps({
                "GET repos/%s/pulls/1" % up: {"number": 1, "state": "open", "head": {"sha": head},
                                               "user": {"login": "tester"}, "title": pr["title"]},
                "GET repos/%s/pulls?state=open&per_page=100" % up: [
                    {"number": 1, "head": {"sha": head}, "user": {"login": "tester"}, "title": pr["title"]}],
                "GET repos/%s/pulls/1/files?per_page=100" % up: [{"filename": e["path"]} for e in tree["tree"]],
                "GET repos/%s/commits/%s/check-runs?per_page=100" % (up, head): {
                    "check_runs": [{"name": "validate", "conclusion": "success"}]}}))
            env = {"WALNUT_SUBMISSIONS_GIT": "file://%s" % gh_repo, "WALNUT_SUBMISSIONS_DIR": str(d / "work")}
            os.environ.update(env)
            self.addCleanup(lambda: [os.environ.pop(k, None) for k in env])
            sm = load_submissions()
            rc, out = quiet(sm.main, ["list"])
            self.assertEqual(rc, 0, out)
            self.assertIn("#1", out)
            self.assertIn("%s/%s" % (g, sid), out)
            self.assertIn("success", out)
            rc, out = quiet(sm.main, ["show", "1"])
            self.assertEqual(rc, 0, out)
            self.assertIn("the check passes", out)
            got = d / "work" / "pr-1" / run.name
            self.assertTrue((got / "bench.txt").is_file())
            acc = ["accept", "1", "--content", str(content), "--session", sd.name, "--yes"]
            rc, out = quiet(sm.main, acc)
            self.assertEqual(rc, 1, out)
            self.assertIn("no REVIEW.md", out)
            shutil.copyfile(d / "REVIEW.md", got / "REVIEW.md")
            rc, out = quiet(sm.main, acc)
            self.assertEqual(rc, 0, out)
            folder = content / g / "rating/sessions" / sd.name
            text = (folder / "result.toml").read_text()
            rec = tomllib.loads(text)
            self.assertEqual((rec["source"], rec["tester"]), ("submitted", "tester"))
            strip = lambda x: "\n".join(line for line in x.splitlines() if not line.startswith(("source =", "tester =")))
            self.assertEqual(strip(text), strip((sd / "result.toml").read_text()))   # the same record as ours
            self.assertEqual(evidence.read_import(folder)["submission"], "%s#1 submissions/%s/%s" % (up, g, sid))
            self.assertEqual(import_run_module().derive_session(folder, g, content)[0], text)   # reimport.py keeps it
            # committed, that folder only, the pull request's number in the message
            self.assertIn("(%s#1)" % up, self.git(ours, "log", "-1", "--format=%s"))
            changed = self.git(ours, "show", "--name-only", "--format=", "HEAD").splitlines()
            self.assertTrue(changed and all(c.startswith("gamedb/games/%s/rating/sessions/%s/" % (g, sd.name))
                                            for c in changed), changed)
            self.assertEqual(self.git(ours, "status", "--porcelain"), "")
            calls = self.calls(st)
            self.assertIn(("PATCH", "repos/%s/pulls/1" % up, {"state": "closed"}), [(c["args"][2], at(c), c["body"]) for c in calls])
            comment = [c["body"]["body"] for c in calls if at(c) == "repos/%s/issues/1/comments" % up]
            self.assertTrue(comment and "credited to tester" in comment[0], comment)
            self.assertFalse([c for c in calls if at(c).endswith("/merge")])       # never merged
            # accept again: imported already, no second commit
            n = self.git(ours, "rev-list", "--count", "HEAD")
            self.assertEqual(quiet(sm.main, acc)[0], 0)
            self.assertEqual(self.git(ours, "rev-list", "--count", "HEAD"), n)
            # the site's session page data credits the tester
            sys.path.insert(0, str(REPO / "tools/gamedb"))
            import sessions
            ss = {x["id"]: x for x in sessions.game_sessions(content / g)}
            self.assertEqual((ss[sd.name]["source"], ss[sd.name]["tester"]), ("submitted", "tester"))
            # a pull request that touches another file too: refused by show
            self.git(gh_repo, "switch", "-q", "pr")
            write(gh_repo / "README.md", "x\n")
            self.git(gh_repo, "add", "-A")
            self.git(gh_repo, "commit", "-qm", "more")
            head2 = self.git(gh_repo, "rev-parse", "HEAD")
            self.git(gh_repo, "switch", "-q", "main")
            self.git(gh_repo, "merge", "-q", "--no-ff", "-m", "merge", "pr")
            self.git(gh_repo, "update-ref", "refs/pull/1/merge", "HEAD")
            resp = json.loads((st / "responses.json").read_text())
            resp["GET repos/%s/pulls/1" % up]["head"]["sha"] = head2
            (st / "responses.json").write_text(json.dumps(resp))
            rc, out = quiet(sm.main, ["show", "1"])
            self.assertIn("outside submissions", out)
            self.assertNotEqual(rc, 0)
            # the workflow's own command on it
            rc, out = quiet(sc.main, ["pr", "file://%s" % gh_repo, "1", str(d / "wf")])
            self.assertEqual(rc, 1, out)
            # reject: asked first (not a terminal: nothing done), then with --yes
            before = len(self.calls(st))
            self.assertEqual(quiet(sm.main, ["reject", "1", "the pictures are black"])[0], 1)
            self.assertFalse([c for c in self.calls(st)[before:] if at(c, 2) in ("POST", "PATCH")])
            self.assertEqual(quiet(sm.main, ["reject", "1", "the pictures are black", "--yes"])[0], 0)
            comment = [c["body"]["body"] for c in self.calls(st)[before:] if at(c, 2) == "POST"]
            self.assertIn("the pictures are black", comment[0])
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def test_credit_refusals(self):
        ir = import_run_module()
        self.assertEqual(ir.credit_keys(None), {})
        self.assertEqual(ir.credit_keys({"source": "submitted", "tester": ""}), {"source": "submitted", "tester": None})
        for bad in ({"source": "ours"}, {"source": "submitted", "tester": "x" * 41},
                    {"source": "submitted", "tester": "a\nb"}):
            with self.assertRaises(ir.Refused):
                ir.credit_keys(bad)


def load_submissions():
    spec = importlib.util.spec_from_file_location("submissions", REPO / "tools/gamedb/submissions.py")
    sm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sm)
    return sm


if __name__ == "__main__":
    unittest.main(verbosity=1)
