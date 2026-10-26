#!/usr/bin/env python3
"""inputlog.py T0 PID [REQUEST_FILE]: the input devices during a played session, for the Controls checks and
the level load (docs/GAMEDB.md "How a game is rated", docs/games/SUITE.md "Played sessions"). Started by
client.sh in a session (BENCH_SESSION=1) next to the game, at nice 19, as the player: it reads what the
player may read (the pads through logind's uaccess; keyboards and mice with the `input` group), and a
class it can't read stays unread (the header says which): its items are "not tested", never "no". It
exits by itself when PID (the game's process group leader) is gone. Read-only: it never grabs a device,
so the game gets every event as before. It counts presses and keeps their times, never which key: no
key codes, no text.

The one exception: the player's screenshot chords, Alt+F12 on a keyboard and Select+Start on a gamepad
(both buttons within 0.3 s). Only for them does it look at key codes (it holds the state of Alt, F12,
Select and Start, nothing else, and keeps nothing of it), and all it writes is a `mark` row with the time
of the chord, never a code. With REQUEST_FILE (client.sh gives it one in a session) each chord also appends
its time to that file, and client.sh takes a marked screenshot (shots/mark-<t>s.xwd, bench.txt shot_mark=).
Shots at least 2 s apart; the game still gets the keys (nothing is grabbed): a game that uses Alt+F12 or
Select+Start itself sees them as well, and the player gets a shot too.

Output on stdout, TSV, the time in seconds after T0 (client.sh's $EPOCHREALTIME at the game's start):
  # inputlog readable=pad,keyboard unreadable=mouse
  t_s  what     dev      class     n
  0.4  present  event3   pad       Sony PLAYSTATION(R)3 Controller      (n: the device's name)
  95.1 added    event7   pad       ...                                  (turned on after the start)
  2.0  opened   event3   pad       -                                    (the game, or a child of it, opened it)
  31.207 key    event3   pad       -                                    (a press, at the event's own time)
  10   press    -        pad       14                                   (key/button presses in the 10 s from t_s)
  10   move     -        pad       230                                  (axis and relative events in those 10 s)
  62.4 mark     event3   pad       -                                    (a screenshot chord, at the event's time)
  .    removed  event7   pad       -
Classes from the device's capabilities (/sys/class/input/eventN/device): pad (BTN_GAMEPAD or BTN_JOYSTICK),
motion (an accelerometer: the DualShock's motion sensors, never read), mouse (REL_X, REL_Y, BTN_LEFT),
keyboard (KEY_Q, KEY_A, KEY_Z), other. /dev/input/jsN (the joydev interface older SDL opens) counts as its
eventN sibling. The game's open devices are read from /proc/<pid>/fd of PID and its descendants every 2 s.
Every 0.25 s the readable devices are drained (non-blocking reads: an overflowing buffer drops events,
the counts are approximate; a press's time is the kernel's timestamp, exact to the microsecond)."""
import io
import os
import struct
import sys
import time

EV = struct.Struct("@llHHi")     # struct input_event on a 64-bit kernel: timeval, type, code, value
EV_KEY, EV_REL, EV_ABS = 1, 2, 3
BTN_JOYSTICK, BTN_GAMEPAD, BTN_LEFT = 0x120, 0x130, 0x110
KEY_Q, KEY_A, KEY_Z = 16, 30, 44
INPUT_PROP_ACCELEROMETER = 6
KEY_LEFTALT, KEY_RIGHTALT, KEY_F12 = 56, 100, 88
BTN_SELECT, BTN_START = 0x13A, 0x13B
CHORD_GAP, MARK_GAP = 0.3, 2.0      # Select and Start within this; shots at least this far apart (seconds)
CHORD_KEYS = (KEY_LEFTALT, KEY_RIGHTALT, KEY_F12, BTN_SELECT, BTN_START)
TICK, SCAN, FDS, BUCKET = 0.25, 1.0, 2.0, 10
LONG_BITS = struct.calcsize("l") * 8
READ = ("pad", "keyboard", "mouse")


def bits(path):
    """a sysfs capability bitmap (hex longs, most significant first) as an int; 0 when unreadable"""
    try:
        with open(path) as f:
            words = f.read().split()
    except OSError:
        return 0
    n = 0
    for w in words:
        n = (n << LONG_BITS) | int(w, 16)
    return n


def classify(sysdev):
    """the class of an input device from its sysfs directory (…/eventN/device)"""
    if bits(os.path.join(sysdev, "properties")) >> INPUT_PROP_ACCELEROMETER & 1:
        return "motion"
    key = bits(os.path.join(sysdev, "capabilities", "key"))
    rel = bits(os.path.join(sysdev, "capabilities", "rel"))
    if key >> BTN_GAMEPAD & 1 or key >> BTN_JOYSTICK & 1:
        return "pad"
    if rel & 3 == 3 and key >> BTN_LEFT & 1:
        return "mouse"
    if all(key >> k & 1 for k in (KEY_Q, KEY_A, KEY_Z)):
        return "keyboard"
    return "other"


def device_name(sysdev):
    try:
        with open(os.path.join(sysdev, "name")) as f:
            return " ".join(f.read().split())[:60] or "-"
    except OSError:
        return "-"


def event_of(path, sysroot="/sys/class/input"):
    """/dev/input/eventN or /dev/input/jsN -> "eventN" (a js device by its sibling under the same input device)"""
    name = os.path.basename(path)
    if name.startswith("event"):
        return name
    if not name.startswith("js"):
        return None
    try:
        parent = os.path.realpath(os.path.join(sysroot, name, "device"))
        for e in os.listdir(parent):
            if e.startswith("event"):
                return e
    except OSError:
        pass
    return None


def family(pid, proc="/proc"):
    kids = {}
    for p in os.listdir(proc):
        if p.isdigit():
            try:
                with open(os.path.join(proc, p, "stat")) as f:
                    ppid = int(f.read().rsplit(")", 1)[1].split()[1])
            except (OSError, ValueError, IndexError):
                continue
            kids.setdefault(ppid, []).append(int(p))
    out, todo = set(), [pid]
    while todo:
        p = todo.pop()
        if p not in out:
            out.add(p)
            todo += kids.get(p, [])
    return out


def opened_devices(pids, proc="/proc"):
    """the eventN names the processes have open"""
    out = set()
    for p in pids:
        d = os.path.join(proc, str(p), "fd")
        try:
            fds = os.listdir(d)
        except OSError:
            continue
        for fd in fds:
            try:
                target = os.readlink(os.path.join(d, fd))
            except OSError:
                continue
            if target.startswith("/dev/input/"):
                e = event_of(target)
                if e:
                    out.add(e)
    return out


def count(data, counts, presses=None):
    """add the events in data (whole input_events) to counts {"press", "move"}; presses: a list the press
    times (the event's own timestamp, CLOCK_REALTIME: evdev's default) are appended to"""
    for sec, usec, typ, code, value in EV.iter_unpack(data[:len(data) - len(data) % EV.size]):
        if typ == EV_KEY and value == 1:
            counts["press"] += 1
            if presses is not None:
                presses.append(sec + usec / 1e6)
        elif typ in (EV_ABS, EV_REL):
            counts["move"] += 1


class Chords:
    """the screenshot chords of one device: Alt (either) held while F12 goes down, or Select and Start down
    within CHORD_GAP of each other. Holds the state of those five keys and no other; feed() returns the
    times (the events' own) at which a chord completed"""

    def __init__(self):
        self.down = {}

    def feed(self, data):
        out = []
        for sec, usec, typ, code, value in EV.iter_unpack(data[:len(data) - len(data) % EV.size]):
            if typ != EV_KEY or code not in CHORD_KEYS:
                continue
            t = sec + usec / 1e6
            if value == 0:
                self.down.pop(code, None)
            elif value == 1:
                self.down[code] = t
                if code == KEY_F12 and (KEY_LEFTALT in self.down or KEY_RIGHTALT in self.down):
                    out.append(t)
                elif code in (BTN_SELECT, BTN_START):
                    other = BTN_START if code == BTN_SELECT else BTN_SELECT
                    if other in self.down and t - self.down[other] <= CHORD_GAP:
                        out.append(t)
        return out


class Log:
    def __init__(self, t0, out=sys.stdout, dev="/dev/input", sysroot="/sys/class/input", request=None):
        self.t0, self.out, self.dev, self.sysroot = t0, out, dev, sysroot
        self.request = request     # a file each chord's time is appended to (client.sh takes the shot)
        self.last_mark = None
        self.devs = {}        # eventN -> {"class", "fd"}
        self.opened = set()
        self.unreadable = set()
        self.bucket = None
        self.counts = {}
        self.first = True

    def now(self):
        return time.time() - self.t0

    def row(self, *cols):
        self.out.write("\t".join(str(c) for c in cols) + "\n")
        self.out.flush()

    def scan(self):
        try:
            names = {n for n in os.listdir(self.dev) if n.startswith("event")}
        except OSError:
            names = set()
        t = "%.1f" % self.now()
        for n in sorted(names - set(self.devs)):
            sysdev = os.path.join(self.sysroot, n, "device")
            cls = classify(sysdev)
            fd = None
            if cls in READ:
                try:
                    fd = os.open(os.path.join(self.dev, n), os.O_RDONLY | os.O_NONBLOCK)
                except OSError:
                    self.unreadable.add(cls)
            self.devs[n] = {"class": cls, "fd": fd, "chords": Chords() if cls in ("pad", "keyboard") else None}
            self.row(t, "present" if self.first else "added", n, cls, device_name(sysdev))
        for n in sorted(set(self.devs) - names):
            d = self.devs.pop(n)
            if d["fd"] is not None:
                os.close(d["fd"])
            self.row(t, "removed", n, d["class"], "-")
        self.first = False

    def drain(self):
        b = int(self.now() // BUCKET) * BUCKET
        if self.bucket is not None and b != self.bucket:
            self.flush()
        self.bucket = b
        for n, d in self.devs.items():
            if d["fd"] is None:
                continue
            c = self.counts.setdefault(d["class"], {"press": 0, "move": 0})
            presses = []
            while True:
                try:
                    data = os.read(d["fd"], EV.size * 512)
                except BlockingIOError:
                    break
                except OSError:      # unplugged: the next scan removes it
                    break
                if not data:
                    break
                count(data, c, presses)
                if d["chords"] is not None:
                    for t in d["chords"].feed(data):
                        self.mark(t, n, d["class"])
            for p in presses:      # each press at its own time: Load's level load ends at the play mark
                self.row("%.3f" % (p - self.t0), "key", n, d["class"], "-")

    def mark(self, t, dev, cls):
        """a screenshot chord at t (epoch seconds): a row, and a request line; one every MARK_GAP at most"""
        if self.last_mark is not None and t - self.last_mark < MARK_GAP:
            return False
        self.last_mark = t
        self.row("%.3f" % (t - self.t0), "mark", dev, cls, "-")
        if self.request:
            try:
                with open(self.request, "a") as f:
                    f.write("%.6f\n" % t)
            except OSError:
                pass
        return True

    def flush(self):
        for cls, c in sorted(self.counts.items()):
            for k in ("press", "move"):
                if c[k]:
                    self.row(self.bucket, k, "-", cls, c[k])
        self.counts = {}

    def game_fds(self, pids):
        t = "%.1f" % self.now()
        for n in sorted(opened_devices(pids) - self.opened):
            self.opened.add(n)
            self.row(t, "opened", n, self.devs.get(n, {}).get("class", "?"), "-")


def main(argv):
    if len(argv) < 3 or not argv[2].isdigit():
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        return 2
    t0, pid = float(argv[1]), int(argv[2])
    first = io.StringIO()
    log = Log(t0, first, request=argv[3] if len(argv) > 3 else None)
    log.scan()      # the devices there at the start, under the header
    readable = sorted({d["class"] for d in log.devs.values() if d["fd"] is not None})
    log.out = sys.stdout
    log.row("# inputlog readable=%s unreadable=%s" % (",".join(readable) or "-", ",".join(sorted(log.unreadable)) or "-"))
    log.row("t_s", "what", "dev", "class", "n")
    sys.stdout.write(first.getvalue())
    last_scan = last_fds = last_family = 0.0
    pids = {pid}
    deadline = time.time() + 12 * 3600
    while os.path.exists("/proc/%d" % pid) and time.time() < deadline:
        now = time.time()
        if now - last_scan >= SCAN:
            log.scan()
            last_scan = now
        if now - last_family >= 10:
            pids = family(pid)
            last_family = now
        if now - last_fds >= FDS:
            log.game_fds(pids)
            last_fds = now
        log.drain()
        time.sleep(TICK)
    log.drain()
    log.flush()
    return 0


if __name__ == "__main__":
    try:
        os.nice(19)
    except OSError:
        pass
    sys.exit(main(sys.argv))
