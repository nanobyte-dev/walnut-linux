#!/usr/bin/env python3
"""intfcheck.py: the interference detector checked on the host (selftest.py runs it; alone:
python3 bench/intfcheck.py). docs/STALLS.md "Interference".
  1. ps3-intfwatch builds with the host's cc without warnings
  2. its input filter on recorded event streams (-r), each in both byte orders: the 64-bit struct
     input_event is 24 bytes on ppc64 (big-endian) and x86-64 (little-endian), same layout; both
     must give the same verdict. DualShock 3 sticks jittering a few units stay quiet, a real move
     counts once per excursion, triggers and hats, buttons, keys (repeats and releases not counted,
     no key code in the log), the motion sensors not watched, mouse bumps recorded but not counted,
     a 2.5 s motion, a fast 1800-count flick, three 600-count strokes within 10 s, buttons and the
     wheel counted
  3. the live part on this machine (no root: an empty input directory, a fake VT file, sessions
     directory and kernel log): the harness's own processes (descendants, an orphan carrying
     BENCH_DIR), bench-cam's watcher (exempt) and a foreign command; a new login session; the VT
     rules of an x run (the X server's own switches expected, the others counted); a USB line;
     an older login process busy for more than 1 s
  4. report.py and matrix.py with interfered runs (bench.txt interfered=1, results/interfered.txt):
     intf column, !INTERFERED, the medians without them, matrix.py skips them (and keeps them with
     --include-interfered)
  5. interference-scan.py on made-up run directories: a VT switch in Xorg.0.log, a USB line in
     dmesg.log, Luanti's player actions placed in the window, the evemu keyboard not counted"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "packages" / "walnut" / "tools" / "ps3-gpu-tools" / "src" / "ps3-intfwatch.c"
FAILS = []


def _check(what, cond, got=None):
    print("%s  %s%s" % ("ok  " if cond else "FAIL", what, "" if cond else "  (got %r)" % (got,)))
    if not cond:
        FAILS.append(what)


check = _check
EV_SYN, EV_KEY, EV_REL, EV_ABS = 0, 1, 2, 3


def build(tmp):
    exe = tmp / "ps3-intfwatch"
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        check("intfwatch: a C compiler", False, None)
        return None
    p = subprocess.run([cc, "-O2", "-Wall", "-Wextra", "-o", str(exe), str(SRC)], capture_output=True, text=True)
    check("intfwatch: builds without warnings", p.returncode == 0 and not p.stderr.strip(), p.stderr[-800:])
    return exe if p.returncode == 0 else None


class Stream:
    """input_event records: (t, type, code, value), SYN_REPORT after each"""
    def __init__(self, t0=1792300000.0):
        self.t, self.ev = t0, []

    def at(self, dt):
        self.t += dt
        return self

    def put(self, typ, code, value, syn=True):
        self.ev.append((self.t, typ, code, value))
        if syn:
            self.ev.append((self.t, EV_SYN, 0, 0))
        return self

    def write(self, path, order):
        fmt = (">" if order == "be" else "<") + "QQHHi"
        with open(path, "wb") as f:
            for t, typ, code, value in self.ev:
                sec = int(t)
                f.write(struct.pack(fmt, sec, int(round((t - sec) * 1e6)), typ, code, value))


def replay(exe, tmp, name, devlines, stream):
    """the same stream in both byte orders; returns (log text, both verdicts equal)"""
    logs = []
    for order in ("be", "le"):
        ev = tmp / ("%s.%s.ev" % (name, order))
        stream.write(ev, order)
        spec = tmp / ("%s.%s.spec" % (name, order))
        spec.write_text("\n".join(devlines + ["events %s %s" % (ev, order)]) + "\n")
        p = subprocess.run([str(exe), "-r", str(spec)], capture_output=True, text=True)
        logs.append(p.stdout)
    tail = lambda s: [line for line in s.splitlines() if not line.startswith("# ps3-intfwatch")]
    return logs[0], tail(logs[0]) == tail(logs[1])


def summary(log):
    out = {}
    for line in log.splitlines():
        for k in ("interfered=", "interference: ", "info: "):
            if line.startswith("# " + k):
                out[k.strip(":= ")] = line[len(k) + 2:]
    return out


def replay_checks(exe, tmp):
    import random
    rnd = random.Random(7)
    # 1. a DualShock 3 left alone: sticks jitter +-3 around 128 for 30 s at 100 Hz, L2 at 0
    ds3 = ["device Sony PLAYSTATION(R)3 Controller", "abs 0 0 255 128 15", "abs 1 0 255 127 15",
           "abs 2 0 255 0 15", "abs 3 0 255 129 15", "abs 4 0 255 128 15"]
    s = Stream()
    for _ in range(3000):
        s.at(0.01).put(EV_ABS, 0, 128 + rnd.randint(-3, 3), syn=False).put(EV_ABS, 1, 127 + rnd.randint(-3, 3))
    log, same = replay(exe, tmp, "ds3idle", ds3, s)
    sm = summary(log)
    check("intfwatch: DS3 jitter alone is no interference (both byte orders agree)",
          sm.get("interfered") == "0" and same, (sm, same))
    # 2. the stick pushed right, held with jitter, back, pushed left; L2 pressed; X and PS buttons
    s.at(1).put(EV_ABS, 0, 200).at(0.02).put(EV_ABS, 0, 250)
    for _ in range(100):
        s.at(0.01).put(EV_ABS, 0, 250 + rnd.randint(-3, 3) if rnd.random() < 0.5 else 247)
    s.at(0.05).put(EV_ABS, 0, 130).at(1).put(EV_ABS, 0, 5).at(0.2).put(EV_ABS, 0, 128)
    s.at(1).put(EV_ABS, 2, 200).at(0.2).put(EV_ABS, 2, 0)
    s.at(1).put(EV_KEY, 0x130, 1).at(0.1).put(EV_KEY, 0x130, 0).at(1).put(EV_KEY, 0x13c, 1).at(0.1).put(EV_KEY, 0x13c, 0)
    log, same = replay(exe, tmp, "ds3play", ds3, s)
    sm = summary(log)
    check("intfwatch: DS3 played: 3 stick/trigger moves (one per excursion), 2 buttons, both byte orders",
          sm.get("interfered") == "1" and "3 stick moves" in sm.get("interference", "") and
          "2 controller buttons" in sm.get("interference", "") and same, (sm, same))
    check("intfwatch: button codes are logged", "BUTTON dev=\"Sony PLAYSTATION(R)3 Controller\" code=0x130" in log, log[-600:])
    # 3. the motion sensors: large noise, never counted
    s = Stream()
    for _ in range(500):
        s.at(0.01).put(EV_ABS, 0, rnd.randint(-400, 400), syn=False).put(EV_ABS, 1, rnd.randint(-400, 400))
    log, same = replay(exe, tmp, "motion", ["device Sony PLAYSTATION(R)3 Controller Motion Sensors", "prop accel",
                                            "abs 0 -512 511 0 0", "abs 1 -512 511 0 0"], s)
    sm = summary(log)
    check("intfwatch: accelerometer device not watched", sm.get("interfered") == "0" and "motion sensor" in sm.get("info", "")
          and same, sm)
    # 4. a keyboard: one press with auto-repeat, then released
    s = Stream().at(1).put(EV_KEY, 30, 1)
    for _ in range(5):
        s.at(0.03).put(EV_KEY, 30, 2)
    s.at(0.03).put(EV_KEY, 30, 0)
    log, same = replay(exe, tmp, "kbd", ["device Logitech USB Receiver"], s)
    sm = summary(log)
    check("intfwatch: one key press (repeats, release not counted)", sm.get("interference") == "1 key presses" and same, sm)
    check("intfwatch: no key code in the log", "KEY dev=\"Logitech USB Receiver\"" in log and "0x1e" not in log
          and "code=30" not in log, log[-300:])
    # 5. a mouse: a bump (0.2 s, 30 counts), then 2.5 s of slow motion, then a fast 1800-count flick
    mouse = ["device Logitech USB Receiver Mouse", "rel"]
    s = Stream()
    for _ in range(10):
        s.at(0.02).put(EV_REL, 0, 3)
    log, same = replay(exe, tmp, "bump", mouse, s)
    sm = summary(log)
    check("intfwatch: a mouse bump is recorded, not counted", sm.get("interfered") == "0" and "1 bump(s), 30 counts"
          in sm.get("info", "") and same, sm)
    s.at(20)
    for _ in range(250):
        s.at(0.01).put(EV_REL, 0, 1)
    s.at(20)
    for _ in range(30):
        s.at(0.01).put(EV_REL, 0, 40, syn=False).put(EV_REL, 1, 20)
    log, same = replay(exe, tmp, "mouse", mouse, s)
    sm = summary(log)
    check("intfwatch: 2.5 s of motion and a 1800-count flick are deliberate (2)",
          "2 deliberate mouse moves" in sm.get("interference", "") and same, sm)
    # three 600-count strokes 1 s apart: the third makes 1800 within 10 s
    s = Stream()
    for _ in range(3):
        s.at(1)
        for _ in range(20):
            s.at(0.01).put(EV_REL, 0, 30)
    log, same = replay(exe, tmp, "strokes", mouse, s)
    sm = summary(log)
    check("intfwatch: three 600-count strokes within 10 s: the third counts",
          "1 deliberate mouse moves" in sm.get("interference", "") and "2 bump(s)" in sm.get("info", "") and same, sm)
    s = Stream().at(1).put(EV_KEY, 0x110, 1).at(0.1).put(EV_KEY, 0x110, 0).at(1).put(EV_REL, 8, -1)
    log, same = replay(exe, tmp, "mbtn", mouse, s)
    sm = summary(log)
    check("intfwatch: a mouse button and a wheel step count", "1 mouse buttons" in sm.get("interference", "")
          and "1 wheel steps" in sm.get("interference", "") and same, sm)
    # 6. a pad's d-pad as a hat: right, centre, left
    s = Stream().at(1).put(EV_ABS, 0x10, 1).at(0.1).put(EV_ABS, 0x10, 0).at(0.5).put(EV_ABS, 0x10, -1).at(0.1).put(EV_ABS, 0x10, 0)
    log, same = replay(exe, tmp, "hat", ["device Generic Pad", "abs 0x10 -1 1 0 0"], s)
    sm = summary(log)
    check("intfwatch: hat presses count (2)", "2 d-pad (hat) presses" in sm.get("interference", "") and same, sm)


def live_checks(exe, tmp):
    d = tmp / "live"
    (d / "input").mkdir(parents=True)
    (d / "sess").mkdir()
    (d / "sess" / "5").write_text("USER=tibi\nSERVICE=sshd\n")
    vt, kmsg, pidf, stop, log = d / "active", d / "kmsg", d / ".pid", d / "stop", d / "interference.log"
    vt.write_text("tty8\n")
    kmsg.write_text("6,1,1,-;old line before the start: USB disconnect\n")
    mark = "BENCH_DIR=%s" % d
    # an older login process (here: anything in user.slice, -S) that keeps a CPU busy
    busy = subprocess.Popen([sys.executable, "-c", "import time\nt = time.time()\nwhile time.time() - t < 30: pass"])
    # the harness: its own process group; its children are the run's
    root = subprocess.Popen(["bash", "-c", "sleep 1.2; sleep 4.1 & wait"], start_new_session=True,
                            env=dict(os.environ, BENCH_DIR=str(d)))
    time.sleep(0.3)
    w = subprocess.Popen([str(exe), "-o", str(log), "-s", str(stop), "-P", str(root.pid), "-e", mark,
                          "-g", str(pidf), "-x", "1", "-H", "8", "-d", str(d / "input"), "-L", str(d / "sess"),
                          "-t", str(vt), "-k", str(kmsg), "-S", "/user.slice/", "-q", "100"],
                         stderr=subprocess.PIPE, text=True)
    for _ in range(50):
        if log.exists() and " ready" in log.read_text():
            break
        time.sleep(0.1)
    check("intfwatch live: ready", log.exists() and " ready" in log.read_text(), log.read_text()[-300:] if log.exists() else "-")
    game = subprocess.Popen(["sleep", "30"])
    pidf.write_text("%d\n" % game.pid)
    foreign = subprocess.Popen(["sleep", "2.51"])     # a command typed in a login session
    orphan = subprocess.Popen(["sh", "-c", "sleep 2.52 & exit 0"], env=dict(os.environ, BENCH_DIR=str(d)))
    cam = subprocess.Popen(["bash", "-c", 'x="pgrep -x run.sh; ls /dev/shm/ps3-bench-*"; sleep 2.53; true'])

    def setvt(n):
        vt.write_text("tty%d\n" % n)
        time.sleep(0.35)
    time.sleep(0.4)
    setvt(1)    # the X server takes vt1: expected
    setvt(2)    # Ctrl+Alt+F2: counted
    setvt(1)    # back: counted
    setvt(8)    # the console while the game runs: counted
    setvt(1)    # and back again: counted
    with kmsg.open("a") as f:
        f.write("6,2,2,-;usb 3-2: new full-speed USB device number 4 using ehci-pci\n"
                "6,3,3,-;input: Sony PLAYSTATION(R)3 Controller as /devices/ps3_system/sb_08/usb3/3-2/input/input9\n")
    tmpf = d / "sess" / ".#77abc"
    tmpf.write_text("USER=walnut\nSERVICE=sshd\nREMOTE_HOST=192.0.2.45\nCLASS=user\n")
    tmpf.rename(d / "sess" / "77")
    (d / "sess" / "77.ref").write_text("")
    game.kill()
    game.wait()
    time.sleep(0.3)
    setvt(8)    # the X server gives the VT back after the game: expected
    for p in (foreign, orphan, cam, root):
        p.wait()
    time.sleep(0.3)
    stop.touch()
    try:
        w.wait(timeout=5)
    except subprocess.TimeoutExpired:
        w.kill()
    busy.kill()
    busy.wait()
    t = log.read_text()
    sm = summary(t)
    flagged = [line for line in t.splitlines() if " ! " in line]
    check("intfwatch live: interfered", sm.get("interfered") == "1", sm)
    check("intfwatch live: the foreign command counted with its command line",
          any("PROC" in line and "sleep 2.51" in line for line in flagged), flagged)
    check("intfwatch live: the harness's children not counted",
          not any("sleep 4.1" in line or "sleep 1.2" in line for line in flagged), flagged)
    check("intfwatch live: an orphan with BENCH_DIR not counted", not any("sleep 2.52" in line for line in flagged), flagged)
    check("intfwatch live: bench-cam's watcher and its children exempt",
          not any("sleep 2.53" in line for line in flagged) and "exempt" in sm.get("info", ""), (flagged, sm))
    vts = [line.split(" VT ", 1)[1] for line in t.splitlines() if " VT " in line]
    exp = [v for v in vts if "(the X server" in v]
    check("intfwatch live: VT 8->1 and 1->8 (after the game) expected, 4 others counted",
          len(exp) == 2 and sm.get("interference", "").count("VT switch") == 4,
          (vts, sm.get("interference")))
    check("intfwatch live: the USB connect line counted, the input line recorded",
          "usb connect" in sm.get("interference", "") and "kernel input/HID/Bluetooth" in sm.get("info", ""), sm)
    check("intfwatch live: the old kernel line (before the start) not counted", "usb disconnect" not in sm.get("interference", ""), sm)
    check("intfwatch live: the new session 77 counted (its .ref and temp file not)",
          "1 login(s): session 77 (walnut, sshd from 192.0.2.45, user)" in sm.get("interference", ""), sm.get("interference"))
    check("intfwatch live: the busy older process counted", any("BUSY" in line and "python" in line for line in flagged), flagged)
    check("intfwatch live: own CPU reported", "# self cpu=" in t, t[-200:])
    check("intfwatch live: the summary has the command and the session",
          # the summary's command list is capped (256 bytes): on a busy host other commands of the
          # login sessions fill it before "sleep 2.51", which the PROC line above already proves
          "command(s) in login sessions: " in sm.get("interference", ""),
          sm.get("interference"))


def run_dir(tmp, name, bench="", files=None):
    d = tmp / name
    d.mkdir(parents=True)
    (d / "bench.txt").write_text(bench)
    for k, v in (files or {}).items():
        (d / k).write_text(v)
    return d


def report_checks(tmp):
    res = tmp / "res"
    res.mkdir()
    (res / "interfered.txt").write_text("# test\nps3-bench-20261018T000003Z-fakei-base retro-scan said so\n")
    common = "game=fakei\nvariant=base\nended=exit\nseconds=100\nfps=%s\n"
    a = run_dir(tmp, "ps3-bench-20261018T000001Z-fakei-base", common % 50 + "interfered=0\ninterference=none\n")
    b = run_dir(tmp, "ps3-bench-20261018T000002Z-fakei-base", common % 10 + "interfered=1\ninterference=3 key presses\n")
    c = run_dir(tmp, "ps3-bench-20261018T000003Z-fakei-base", common % 20)
    e = run_dir(tmp, "ps3-bench-20261018T000004Z-fakei-base", common % 60)
    env = dict(os.environ, BENCH_RESULTS=str(res))
    p = subprocess.run([sys.executable, str(HERE / "report.py"), *map(str, (a, b, c, e))], capture_output=True, text=True, env=env)
    lines = p.stdout.splitlines()
    check("report: intf column (1 for the detector's and the retro list's, 0 clean, - unwatched)",
          [line.split("intf=")[1].split()[0] for line in lines[:4] if "intf=" in line] == ["0", "1", "1", "-"], lines[:4])
    check("report: !INTERFERED marker", sum("!INTERFERED" in line for line in lines[:4]) == 2, lines[:4])
    med = next((line for line in lines if " n=" in line), "")
    check("report: the median leaves the interfered out (50, 60 -> 55)", "n=2 (+2 interfered left out)" in med and "fps=55" in med, med)
    check("report: the interfered listed at the end", "3 key presses" in p.stdout and "retro-scan said so" in p.stdout, p.stdout[-400:])
    p = subprocess.run([sys.executable, str(HERE / "report.py"), "--include-interfered", *map(str, (a, b, c, e))],
                       capture_output=True, text=True, env=env)
    med = next((line for line in p.stdout.splitlines() if " n=" in line), "")
    check("report --include-interfered: all four in the median (35)", "n=4" in med and "fps=35" in med, med)
    p = subprocess.run([sys.executable, str(HERE / "matrix.py"), "intf row", *map(str, (a, b, c, e))],
                       capture_output=True, text=True, env=env)
    fps = (res / "fps.csv").read_text() if (res / "fps.csv").exists() else ""
    check("matrix: interfered runs skipped and listed", p.returncode == 0 and p.stdout.count("skipped") == 2 and
          "intf row" in fps and ",2,," in fps and "55" in fps, (p.stdout, p.stderr[-300:], fps))
    p = subprocess.run([sys.executable, str(HERE / "matrix.py"), "intf row2", "--include-interfered", *map(str, (a, b, c, e))],
                       capture_output=True, text=True, env=env)
    fps = (res / "fps.csv").read_text()
    check("matrix --include-interfered: kept", p.stdout.count("kept") == 2 and "intf row2" in fps and "35" in fps, (p.stdout, fps))


def scan_checks(tmp):
    base = tmp / "scan"
    xorg = ("[  100.000] (II) config/udev: Adding input device Logitech USB Receiver (/dev/input/event0)\n"
            "[  100.100] (**) Option \"config_info\" \"udev:/sys/devices/ps3_system/usb3/3-1/input/input0/event0\"\n"
            "[  100.200] (II) event0  - Logitech USB Receiver: device removed\n"
            "%s"
            "[  300.000] (II) Server terminated successfully (0). Closing log file.\n")
    vt = ("[  200.000] (II) event0  - Logitech USB Receiver: device removed\n"
          "[  200.100] (II) AIGLX: Suspending AIGLX clients for VT switch\n"
          "[  204.000] (II) AIGLX: Resuming AIGLX clients after VT switch\n")
    evemu = ("[  150.000] (II) config/udev: Adding input device Logitech USB Receiver (/dev/input/event4)\n"
             "[  150.100] (**) Option \"config_info\" \"udev:/sys/devices/virtual/input/input9/event4\"\n"
             "[  250.000] (II) event4  - Logitech USB Receiver: device removed\n")
    d1 = run_dir(base, "ps3-bench-20261018T010000Z-a-base", "game=a\nvariant=base\n", {"Xorg.0.log": xorg % vt})
    d2 = run_dir(base, "ps3-bench-20261018T010100Z-b-base", "game=b\nvariant=base\n", {"Xorg.0.log": xorg % evemu,
                 "dmesg.log": "[ 9.1] input: Logitech USB Receiver as /devices/virtual/input/input9\n"})
    d3 = run_dir(base, "ps3-bench-20261018T010200Z-c-base", "game=c\nvariant=base\n",
                 {"dmesg.log": "[ 9.2] usb 3-2: new full-speed USB device number 4 using ehci-pci\n"})
    t0 = 1792303200.0   # 2026-10-18 06:00:00 UTC
    d4 = run_dir(base, "ps3-bench-20261018T060000Z-luanti-base",
                 "game=luanti\nvariant=base\nt0=%.1f\nload_end=40\nwindow=85 220\n" % t0,
                 {"game.log": "2026-10-18 06:00:41: ACTION[Server]: ps3bench: path start\n"
                              "2026-10-18 06:01:00: ACTION[Server]: singleplayer digs default:stone at (0,1,2)\n"
                              "2026-10-18 06:02:30: ACTION[Server]: singleplayer activates \n"})
    p = subprocess.run([sys.executable, str(HERE / "interference-scan.py"), *map(str, (d1, d2, d3, d4))],
                       capture_output=True, text=True, env=dict(os.environ, BENCH_RESULTS=str(base)))
    o = p.stdout
    check("scan: VT switch found", "STRONG %s" % d1.name in o and "VT switch away at 100 s" in o, o)
    check("scan: the evemu keyboard (added, removed, kernel line) not counted", d2.name not in o, o)
    check("scan: USB connect found", "STRONG %s" % d3.name in o, o)
    check("scan: Luanti's actions placed (warm-up dig, window activate)",
          "warm-up: 1 digs; window: 1 activates" in o, o)
    p = subprocess.run([sys.executable, str(HERE / "interference-scan.py"), "--write", *map(str, (d1, d2, d3, d4))],
                       capture_output=True, text=True, env=dict(os.environ, BENCH_RESULTS=str(base)))
    lst = (base / "interfered.txt").read_text() if (base / "interfered.txt").exists() else ""
    check("scan --write: three runs listed", len([x for x in lst.splitlines() if x and not x.startswith("#")]) == 3, lst)


def run(tmp, chk=None):
    global check
    if chk:
        check = chk
    exe = build(tmp)
    if exe:
        replay_checks(exe, tmp)
        live_checks(exe, tmp)
    report_checks(tmp)
    scan_checks(tmp)


if __name__ == "__main__":
    t = Path(tempfile.mkdtemp(prefix="intfcheck-"))
    try:
        run(t)
    finally:
        shutil.rmtree(t, ignore_errors=True)
    print("\n%d check(s) failed: %s" % (len(FAILS), ", ".join(FAILS)) if FAILS else "\nall checks passed")
    sys.exit(1 if FAILS else 0)
