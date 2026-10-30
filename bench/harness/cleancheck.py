#!/usr/bin/env python3
"""cleancheck.py: is the console clean for a played session, and does it stay clean while the game runs?
(walnut-rate check and play, docs/games/SUITE.md "Played sessions"). Python stdlib and x11-utils' xprop.

  cleancheck.py check SELF_PID [--close] [--bench DIR] [--helper PATH] [--out DIR]
        before a session: prints what stands in the way, one line each, then "ready" or "not ready";
        exit 0 ready, 1 not ready. --close asks what blocks to close first (a window: WM_DELETE_WINDOW;
        a process of the player's that isn't on the list: SIGTERM; never SIGKILL, never a process on the
        list), waits for them and looks again
  cleancheck.py watch SELF_PID T0
        during a session (client.sh with BENCH_WATCH=1, at nice 19): every WATCH_EVERY seconds (10) the
        windows, the SSH logins and the processes outside the harness; each thing it sees once, a line
        "<seconds after T0>\\t<what>\\t<detail>" on stdout (client.sh's watch.log, which marks the run
        interfered=1); lines starting with # are notes, not interference

What counts (the same rules both times; the allowlist is bench/harness/allowed-processes.txt, the rules
proccheck.py's):
- processes: SELF_PID's ancestors (the terminal, walnut-rate) and descendants (the harness, the game),
  kernel threads, and while a session runs anything in ps3-game.slice (the game's scope) are exempt. A
  name on a "!" line (a browser, a second game, apt, a compiler) blocks however idle; any other name not
  on the list blocks above proccheck's 2% CPU (over its lifetime) or 20 MB RSS. Before a session also any
  process outside the exempt ones that used more than CPU_BUSY of a CPU over SAMPLE_S (the check's own
  sample), on the list or not: wait for it.
- windows: every client in the window manager's _NET_CLIENT_LIST except the desktop's furniture (desktop,
  dock, notification types: PCManFM's desktop, tint2), the harness's and the game's windows (SELF_PID's
  descendants, the game's slice) and of SELF_PID's ancestors only the nearest one with a window (the
  terminal running walnut-rate; a PCManFM window is not exempt because the terminal was started from it). A window that names no _NET_WM_PID is a note, not a block. Only the
  window's class is recorded, never its title.
- SSH logins (sshd-session/sshd "user@tty" processes): interference (docs/games/SUITE.md). The login
  walnut-rate itself runs under is a warning (keep it to the one command); any other blocks, and in the
  watch any login that wasn't there at the start.
- check only: swap in use over SWAP_MAX_MB, the load average over LOAD_MAX, a temperature over TEMP_MAX_C
  (the System Manager's sensors, docs/THERMAL.md), less than DISK_MIN_MB free for the run, another session
  or bench run (ps3-bench.sh, its lock), game mode on (the desktop's slice frozen, a game in ps3-game.slice),
  no desktop (no X, no openbox), running as root, the harness's files, ps3-bench.sh without the root helper
  mode, the root helper itself (sudo -n HELPER check).
Test hooks: CLEANCHECK_PROC (a /proc made of files), CLEANCHECK_SYS (for the hwmon and cgroup files), xprop
from PATH, WATCH_EVERY, WATCH_TICKS (stop after N looks)."""
import ctypes
import fnmatch
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))   # python3 -I leaves the script's directory out
import proccheck   # noqa: E402

ALLOWLIST = HERE / "allowed-processes.txt"
SAMPLE_S = 1.5          # the check's CPU sample
CPU_BUSY = 20.0         # % of one CPU over SAMPLE_S
SWAP_MAX_MB = 256       # swap in use before a session (game mode pushes the desktop out once the game runs)
LOAD_MAX = 2.0          # the 1-minute load average (the PPE has two threads)
TEMP_MAX_C = 70         # the harness stops a game at 80 C (ps3-bench.sh BENCH_TEMP_MAX)
SHOT_MB, SHOT_MAX = 8, 40
DISK_MIN_MB = SHOT_MB * SHOT_MAX + 500   # session.sh's own warning level: the screenshots until exported
CLOSE_WAIT = 5          # seconds --close gives what it asked
FURNITURE = ("_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_NOTIFICATION")
OUT = "/var/tmp/ps3-bench"
GAME_SLICE = "/ps3-game.slice/"


def proc_root():
    return Path(os.environ.get("CLEANCHECK_PROC") or "/proc")


def sys_root():
    return Path(os.environ.get("CLEANCHECK_SYS") or "/sys")


def _read(p, limit=1 << 16):
    try:
        with open(p, "rb") as f:
            return f.read(limit).decode("utf-8", "replace")
    except OSError:
        return None


def proc_rows(proc=None):
    """every process as proccheck.py's rows (pid, ppid, rss KB, cpu: % over its lifetime as ps prints it,
    stat, comm, args) plus uid, ticks (utime + stime) and cgroup, read from /proc (no ps forked)"""
    proc = Path(proc or proc_root())
    hz = os.sysconf("SC_CLK_TCK")
    page = os.sysconf("SC_PAGE_SIZE")
    try:
        up = float((_read(proc / "uptime") or "0").split()[0])
    except (ValueError, IndexError):
        up = 0.0
    rows = []
    for e in os.listdir(proc):
        if not e.isdigit():
            continue
        d = proc / e
        st = _read(d / "stat")
        if not st or ")" not in st:
            continue
        comm = st[st.index("(") + 1:st.rindex(")")]
        f = st[st.rindex(")") + 2:].split()
        try:
            ticks = int(f[11]) + int(f[12])
            start = int(f[19]) / hz
            row = dict(pid=int(e), ppid=int(f[1]), stat=f[0], comm=comm[:15], ticks=ticks,
                       rss=int(f[21]) * page // 1024)
        except (ValueError, IndexError):
            continue
        life = up - start
        row["cpu"] = round(100.0 * ticks / hz / life, 1) if life > 0 else 0.0
        args = (_read(d / "cmdline") or "").replace("\0", " ").strip()
        row["args"] = args or "[%s]" % comm
        uid = re.search(r"(?m)^Uid:\s+(\d+)", _read(d / "status") or "")
        row["uid"] = int(uid[1]) if uid else -1
        cg = re.search(r"(?m)^0::(.*)$", _read(d / "cgroup") or "")
        row["cgroup"] = cg[1] if cg else ""
        rows.append(row)
    return rows


def kernel_thread(r):
    return r["pid"] in (0, 1, 2) or r["ppid"] == 2


def exempt(rows, self_pid, game_slice=False):
    """the pids that are never counted: SELF_PID's ancestors and descendants (proccheck.exempt_pids) and,
    during a session (game_slice), every process in ps3-game.slice (the game's scope, whatever forked it)"""
    ok = proccheck.exempt_pids(rows, self_pid)
    if game_slice:
        ok |= {r["pid"] for r in rows if GAME_SLICE in r.get("cgroup", "") + "/"}
    return ok


def process_blockers(rows, self_pid, allow, deny, game_slice=False):
    """[(row, why)] by proccheck.py's rules, with the exempt set above"""
    ok = exempt(rows, self_pid, game_slice)
    out = []
    for r, why in proccheck.check(rows, self_pid, allow, deny):
        if r["pid"] not in ok:
            out.append((r, why))
    return out


def busy(rows_a, rows_b, dt, ok):
    """[(row, % of a CPU over dt)] above CPU_BUSY, outside the exempt pids and the kernel threads"""
    hz = os.sysconf("SC_CLK_TCK")
    before = {r["pid"]: r["ticks"] for r in rows_a}
    out = []
    for r in rows_b:
        if r["pid"] in ok or kernel_thread(r) or r["pid"] not in before:
            continue
        pct = 100.0 * (r["ticks"] - before[r["pid"]]) / hz / dt if dt > 0 else 0.0
        if pct > CPU_BUSY:
            out.append((r, pct))
    return out


def ssh_logins(rows):
    """the processes that stand for an SSH login: sshd-session (OpenSSH 9.8+) or sshd "user@pts/0" ones"""
    return [r for r in rows if r["comm"] in ("sshd-session", "sshd") and "@" in r["args"]
            and not r["args"].startswith("sshd: /")]


def ancestors(rows, pid):
    by = {r["pid"]: r for r in rows}
    out = set()
    while pid in by and pid not in out:
        out.add(pid)
        pid = by[pid]["ppid"]
    return out


# ---- windows (xprop, x11-utils) --------------------------------------------------------------------

def _xprop(*args):
    env = dict(os.environ, DISPLAY=os.environ.get("DISPLAY") or ":0")   # session.sh's default too
    try:
        p = subprocess.run(["xprop"] + list(args), capture_output=True, text=True, timeout=10, env=env,
                           stdin=subprocess.DEVNULL)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return p.stdout if p.returncode == 0 else None


def parse_client_list(text):
    """the window ids of `xprop -root -notype _NET_CLIENT_LIST` (none: [])"""
    if not text or ":" not in text:
        return []
    return re.findall(r"0x[0-9a-fA-F]+", text.split(":", 1)[1])


def parse_window(wid, text):
    """{id, pid, instance, cls, types} from `xprop -id W -notype _NET_WM_PID WM_CLASS _NET_WM_WINDOW_TYPE`"""
    w = dict(id=wid, pid=None, instance="", cls="", types=[])
    for line in (text or "").splitlines():
        k, _, v = line.partition("=")
        k, v = k.strip(), v.strip()
        if k == "_NET_WM_PID" and v.isdigit():
            w["pid"] = int(v)
        elif k == "WM_CLASS":
            parts = re.findall(r'"((?:[^"\\]|\\.)*)"', v)
            if parts:
                w["instance"], w["cls"] = parts[0], parts[-1]
        elif k == "_NET_WM_WINDOW_TYPE":
            w["types"] = [t.strip() for t in v.split(",") if t.strip()]
    return w


def windows():
    """the window manager's clients, or None when there is no X display to ask"""
    root = _xprop("-root", "-notype", "_NET_CLIENT_LIST")
    if root is None:
        return None
    return [parse_window(w, _xprop("-id", w, "-notype", "_NET_WM_PID", "WM_CLASS", "_NET_WM_WINDOW_TYPE"))
            for w in parse_client_list(root)]


def window_exempt(rows, self_pid, wins, game_slice=False):
    """the pids whose windows are fine: SELF_PID and its descendants (the game), during a session the game's
    slice, and of SELF_PID's ancestors only the nearest one with a window (the terminal running walnut-rate:
    not the desktop's pcmanfm or tint2 it was started from)"""
    up = ancestors(rows, self_pid)
    ok = (proccheck.exempt_pids(rows, self_pid) - up) | {self_pid}
    if game_slice:
        ok |= {r["pid"] for r in rows if GAME_SLICE in r.get("cgroup", "") + "/"}
    by = {r["pid"]: r for r in rows}
    owners = {w["pid"] for w in wins}
    p = self_pid
    seen = set()
    while p in by and p not in seen:
        seen.add(p)
        if p in owners:
            ok.add(p)
            break
        p = by[p]["ppid"]
    return ok


def window_blockers(wins, ok):
    """(blockers, notes): the windows that are neither furniture nor an exempt process's; a window with no
    _NET_WM_PID is only a note"""
    out, notes = [], []
    for w in wins:
        if any(t in FURNITURE for t in w["types"]):
            continue
        if w["pid"] is None:
            notes.append(w)
        elif w["pid"] not in ok:
            out.append(w)
    return out, notes


def window_name(w, by=None):
    r = (by or {}).get(w["pid"])
    name = w["cls"] or w["instance"] or (r["comm"] if r else "a window")
    extra = []
    if r:
        if r["comm"].lower() != name.lower():
            extra.append(r["comm"])
        extra.append("pid %d" % r["pid"])
        extra.append("%d MB" % (r["rss"] // 1024))
    elif w["pid"]:
        extra.append("pid %d" % w["pid"])
    return "%s (%s)" % (name, ", ".join(extra)) if extra else name


def close_window(wid):
    """ask a window to close: the ICCCM WM_DELETE_WINDOW message to it (what the title bar's close button
    sends), through libX11 (ctypes). True when the message went out"""
    try:
        x = ctypes.CDLL("libX11.so.6")
    except OSError:
        return False

    class ClientMessage(ctypes.Structure):
        _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int),
                    ("display", ctypes.c_void_p), ("window", ctypes.c_ulong), ("message_type", ctypes.c_ulong),
                    ("format", ctypes.c_int), ("l", ctypes.c_long * 5)]

    class XEvent(ctypes.Union):
        _fields_ = [("xclient", ClientMessage), ("pad", ctypes.c_long * 24)]

    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x.XInternAtom.restype = ctypes.c_ulong
    x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.POINTER(XEvent)]
    x.XFlush.argtypes = x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    dpy = x.XOpenDisplay(None)
    if not dpy:
        return False
    try:
        ev = XEvent()
        ev.xclient.type = 33   # ClientMessage
        ev.xclient.window = int(wid, 16)
        ev.xclient.message_type = x.XInternAtom(dpy, b"WM_PROTOCOLS", 0)
        ev.xclient.format = 32
        ev.xclient.l[0] = x.XInternAtom(dpy, b"WM_DELETE_WINDOW", 0)
        ev.xclient.l[1] = 0    # CurrentTime
        ok = x.XSendEvent(dpy, ev.xclient.window, 0, 0, ctypes.byref(ev)) != 0
        x.XFlush(dpy)
        return ok
    finally:
        x.XCloseDisplay(dpy)


# ---- the system's state ----------------------------------------------------------------------------

def meminfo():
    out = {}
    for line in (_read(proc_root() / "meminfo") or "").splitlines():
        k, _, v = line.partition(":")
        if v.split():
            out[k] = int(v.split()[0])
    return out


def loadavg():
    try:
        return float((_read(proc_root() / "loadavg") or "0").split()[0])
    except (ValueError, IndexError):
        return 0.0


def temps():
    """[(zone, degrees C)] of the System Manager's sensors (hwmon ps3_sm), [] without them"""
    out = []
    for h in sorted((sys_root() / "class" / "hwmon").glob("hwmon*")):
        if (_read(h / "name") or "").strip() != "ps3_sm":
            continue
        for f in sorted(h.glob("temp*_input")):
            v = (_read(f) or "").strip()
            if v.lstrip("-").isdigit():
                out.append(((_read(Path(str(f)[:-6] + "_label")) or f.name).strip(), int(v) / 1000))
    return out


def desktop_frozen(uid):
    f = sys_root() / "fs" / "cgroup" / "user.slice" / ("user-%d.slice" % uid) / ("user@%d.service" % uid) / \
        "ps3-desktop.slice" / "cgroup.freeze"
    return (_read(f) or "0").strip() == "1"


def free_mb(path):
    p = Path(path)
    while not p.exists() and p != p.parent:
        p = p.parent
    try:
        return shutil.disk_usage(p).free // (1 << 20)
    except OSError:
        return None


# ---- the check -------------------------------------------------------------------------------------

class Finding:
    def __init__(self, level, text, close=None):
        self.level, self.text, self.close = level, text, close   # level: block | warn | note

    def __repr__(self):
        return "%s: %s" % (self.level, self.text)


def harness_findings(bench, helper):
    """the harness's own files, ps3-bench.sh's helper mode and the root helper"""
    out = []
    for f in ("harness/session.sh", "harness/client.sh", "harness/session-form.py", "harness/allowed-processes.txt"):
        if not (Path(bench) / f).is_file():
            out.append(Finding("block", "the harness is incomplete: no %s (reinstall walnut-rate)" % (Path(bench) / f)))
    b = ps3_bench()
    if not b:
        out.append(Finding("block", "no ps3-bench.sh (sudo apt install ps3-gpu-tools)"))
    elif "BENCH_ROOT_HELPER" not in (_read(b, 1 << 20) or ""):
        out.append(Finding("block", "%s has no root helper mode: ps3-gpu-tools is too old (sudo apt upgrade)" % b))
    if helper:
        try:
            p = subprocess.run(["sudo", "-n", helper, "check"], stdin=subprocess.DEVNULL, capture_output=True,
                               text=True, timeout=30)
            ok = p.returncode == 0 and p.stdout.startswith("ok")
            msg = (p.stdout or p.stderr).strip().splitlines()[-1:] if not ok else []
        except (OSError, subprocess.TimeoutExpired) as e:
            ok, msg = False, [str(e)]
        if not ok:
            out.append(Finding("block", "the root helper doesn't run (sudo -n %s check%s): is the player in group "
                                        "sudo? (/etc/sudoers.d/walnut-rate)" % (helper, ": " + msg[0] if msg else "")))
        elif "mm=no" in p.stdout:
            out.append(Finding("note", "no RSX memory census (ps3rsx's debugfs): VRAM isn't sampled"))
    return out


def ps3_bench():
    b = shutil.which("ps3-bench.sh")
    if not b and os.access("/usr/lib/ps3/bin/ps3-bench.sh", os.X_OK):
        b = "/usr/lib/ps3/bin/ps3-bench.sh"
    return b


def check(self_pid, bench=None, helper=None, out_dir=OUT, sample=True, allowlist=ALLOWLIST):
    """[Finding]: everything that stands in the way of a session, and the warnings"""
    allow, deny = proccheck.load_allowlist(str(allowlist))
    f = []
    uid = os.getuid()
    if uid == 0:
        f.append(Finding("block", "running as root: run walnut-rate as the player, in a terminal of the desktop"))
    a = proc_rows()
    t0 = time.monotonic()
    rows = a
    if sample:
        time.sleep(SAMPLE_S)
        rows = proc_rows()
    dt = time.monotonic() - t0
    by = {r["pid"]: r for r in rows}
    ok = exempt(rows, self_pid)
    mine = ancestors(rows, self_pid)
    # another session or bench run, game mode, the desktop
    if Path("/dev/shm/ps3-bench.lock").exists() or any(r["comm"] == "ps3-bench.sh" and r["pid"] not in ok for r in rows):
        f.append(Finding("block", "another session or bench run is going (ps3-bench.sh, /dev/shm/ps3-bench.lock)"))
    games = [r for r in rows if GAME_SLICE in r.get("cgroup", "") + "/" and r["pid"] not in ok and not kernel_thread(r)]
    if games:
        f.append(Finding("block", "a game is running in game mode: %s; quit it first" %
                         ", ".join(sorted({r["comm"] for r in games}))))
    elif desktop_frozen(uid):
        f.append(Finding("block", "game mode is on (the desktop is frozen); wait for it to end"))
    if not any(r["comm"] == "openbox" for r in rows):
        f.append(Finding("block", "no Walnut desktop (openbox isn't running): start walnut-rate from a terminal of the desktop"))
    # processes: the player's own off the list can be asked to stop (SIGTERM), anything else is waited for
    procs = {}
    for r, why in process_blockers(rows, self_pid, allow, deny):
        on_list = any(fnmatch.fnmatchcase(r["comm"], n) and (rx is None or rx.search(r["args"])) for n, rx in allow)
        procs[r["pid"]] = (r, why, r.get("uid") == uid and not on_list)
    # windows (a process with a window is named once, by its window)
    wins = windows()
    if wins is None:
        f.append(Finding("block", "no X display to look at (DISPLAY=%s): run walnut-rate in a terminal of the desktop"
                         % (os.environ.get("DISPLAY") or ":0")))
    else:
        bad, notes = window_blockers(wins, window_exempt(rows, self_pid, wins))
        for w in bad:
            r, why, term = procs.pop(w["pid"], (None, None, False))
            if r is None:
                r = by.get(w["pid"])
                term = bool(r) and r.get("uid") == uid and not any(
                    fnmatch.fnmatchcase(r["comm"], n) and (rx is None or rx.search(r["args"])) for n, rx in allow)
            f.append(Finding("block", "close %s%s" % (window_name(w, by), ": " + why if why else ""),
                             ("window", w["id"], w["pid"] if term else None)))
        for w in notes:
            f.append(Finding("note", "a window that names no process: %s" % (w["cls"] or w["id"])))
    for r, why, term in procs.values():
        if term:
            f.append(Finding("block", "close %s (pid %d, %d MB): %s" % (r["comm"], r["pid"], r["rss"] // 1024, why),
                             ("process", r["pid"])))
        else:
            f.append(Finding("block", "%s (pid %d, %d MB) is running: %s; wait for it to finish" %
                             (r["comm"], r["pid"], r["rss"] // 1024, why)))
    blocked = {x.close[-1] for x in f if x.close} | {x.close[1] for x in f if x.close and x.close[0] == "process"}
    for r, pct in busy(a, rows, dt, ok):
        if r["pid"] not in blocked:
            f.append(Finding("block", "%s (pid %d) is busy: %.0f%% of a CPU over %.1f s; wait for it to settle" %
                             (r["comm"], r["pid"], pct, dt)))
    # SSH logins
    for r in ssh_logins(rows):
        if r["pid"] in mine:
            f.append(Finding("warn", "this terminal is an SSH login: start the session and leave it (no more commands)"))
        else:
            f.append(Finding("block", "an SSH login is open (%s, pid %d): log out of it (a login is interference)" %
                             (r["comm"], r["pid"])))
    # memory, load, temperatures, disk
    m = meminfo()
    used = (m.get("SwapTotal", 0) - m.get("SwapFree", 0)) // 1024
    if used > SWAP_MAX_MB:
        f.append(Finding("block", "%d MB of swap in use (more than %d): something big ran; reboot, or wait" % (used, SWAP_MAX_MB)))
    la = loadavg()
    if la > LOAD_MAX:
        f.append(Finding("block", "the load average is %.2f (more than %.1f): something is working; wait a minute" % (la, LOAD_MAX)))
    tt = temps()
    hot = [(z, c) for z, c in tt if c > TEMP_MAX_C]
    if hot:
        f.append(Finding("block", "too warm: %s (more than %d C); let the PS3 cool down" %
                         (", ".join("%s %.0f C" % zc for zc in hot), TEMP_MAX_C)))
    elif not tt:
        f.append(Finding("note", "no temperature sensors (ps3_sm): the harness can't watch the temperature"))
    fm = free_mb(out_dir)
    if fm is not None and fm < DISK_MIN_MB:
        f.append(Finding("block", "only %d MB free for the run in %s (%d MB wanted: the screenshots)" % (fm, out_dir, DISK_MIN_MB)))
    if bench:
        f += harness_findings(bench, helper)
    f.append(Finding("note", "memory available %d MB, swap in use %d MB, load %.2f%s" %
                     (m.get("MemAvailable", 0) // 1024, used, la,
                      ", " + ", ".join("%s %.0f C" % zc for zc in tt) if tt else "")))
    return f


def close_all(findings, term_windows=False):
    """ask the closable blockers to close: a window by WM_DELETE_WINDOW, a process of the player's that isn't
    on the list by SIGTERM (a process with a window only when term_windows: the second round, after its window
    was asked); never SIGKILL. [str] what was asked"""
    asked = []
    for x in findings:
        if not x.close:
            continue
        name = x.text[len("close "):].split(":")[0]
        if x.close[0] == "window" and not term_windows:
            if close_window(x.close[1]):
                asked.append("asked %s to close" % name)
            continue
        pid = x.close[1] if x.close[0] == "process" else x.close[2]
        if pid is None:
            continue
        try:
            os.kill(pid, signal.SIGTERM)
            asked.append("sent %s SIGTERM" % name)
        except OSError as e:
            asked.append("couldn't stop pid %d: %s" % (pid, e.strerror))
    return asked


def report(findings, out=sys.stdout):
    blocks = [x for x in findings if x.level == "block"]
    for lvl, mark in (("block", "  - "), ("warn", "  ! "), ("note", "    ")):
        for x in findings:
            if x.level == lvl:
                print(mark + x.text, file=out)
    print("ready" if not blocks else "not ready: %d thing%s to fix" % (len(blocks), "" if len(blocks) == 1 else "s"), file=out)
    return not blocks


# ---- the watch -------------------------------------------------------------------------------------

def watch(self_pid, t0, every=None, ticks=None, out=sys.stdout):
    every = float(os.environ.get("WATCH_EVERY", every or 10))
    ticks = int(os.environ.get("WATCH_TICKS", ticks or 0))
    allow, deny = proccheck.load_allowlist(str(ALLOWLIST))
    stop = []
    signal.signal(signal.SIGTERM, lambda *_: stop.append(1))
    signal.signal(signal.SIGINT, lambda *_: stop.append(1))
    seen = set()
    rows = proc_rows()
    base_ssh = {r["pid"] for r in ssh_logins(rows)}
    print("# watch: every %g s, harness pid %d; SSH logins at the start: %d" % (every, self_pid, len(base_ssh)), file=out)
    n = 0
    while not stop:
        rows = proc_rows() if n else rows
        by = {r["pid"]: r for r in rows}
        ok = exempt(rows, self_pid, game_slice=True)
        t = time.time() - t0
        events = []
        for r, why in process_blockers(rows, self_pid, allow, deny, game_slice=True):
            events.append(("process:" + r["comm"], "process", "%s (pid %d, %d MB): %s" % (r["comm"], r["pid"], r["rss"] // 1024, why)))
        for r in ssh_logins(rows):
            if r["pid"] not in base_ssh and r["pid"] not in ok:
                events.append(("ssh:%d" % r["pid"], "ssh", "an SSH login (%s, pid %d)" % (r["comm"], r["pid"])))
        wins = windows()
        if wins:
            bad, notes = window_blockers(wins, window_exempt(rows, self_pid, wins, game_slice=True))
            for w in bad:
                events.append(("window:%s:%s" % (w["cls"], w["pid"]), "window", window_name(w, by)))
            for w in notes:
                if ("note:" + w["id"]) not in seen:
                    seen.add("note:" + w["id"])
                    print("# %.1f\ta window that names no process: %s" % (t, w["cls"] or w["id"]), file=out)
        for key, what, detail in events:
            if key not in seen:
                seen.add(key)
                print("%.1f\t%s\t%s" % (t, what, detail), file=out)
        out.flush()
        n += 1
        if ticks and n >= ticks:
            break
        end = time.monotonic() + every
        while not stop and time.monotonic() < end:
            time.sleep(min(0.5, max(0.0, end - time.monotonic())))
    return 0


def main(argv):
    if len(argv) < 2 or argv[0] not in ("check", "watch"):
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        return 2
    try:
        pid = int(argv[1])
    except ValueError:
        return 2
    if argv[0] == "watch":
        return watch(pid, float(argv[2]) if len(argv) > 2 else time.time())
    opts = {"--bench": None, "--helper": None, "--out": OUT}
    rest = argv[2:]
    close = "--close" in rest
    rest = [a for a in rest if a != "--close"]
    while rest:
        k = rest.pop(0)
        if k not in opts or not rest:
            return 2
        opts[k] = rest.pop(0)
    f = check(pid, opts["--bench"], opts["--helper"], opts["--out"])
    if close:
        f = close_and_check(f, lambda: check(pid, opts["--bench"], opts["--helper"], opts["--out"]))
    return 0 if report(f) else 1


def close_and_check(f, again, wait=CLOSE_WAIT, out=sys.stdout):
    """--close: windows asked first (and the windowless processes stopped), a look again after `wait` s; then
    SIGTERM to what is left of the player's own, and the last look. Returns the last findings"""
    for rnd in (False, True):
        if not any(x.close for x in f):
            break
        asked = close_all(f, term_windows=rnd)
        if not asked:
            break
        for line in asked:
            print("  " + line, file=out)
        time.sleep(wait)
        f = again()
    return f


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
