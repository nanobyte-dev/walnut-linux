#!/usr/bin/env python3
"""interference-scan.py [--write] [-v] <run dir>...: the retro-scan for runs from before the
interference detector (docs/STALLS.md "Interference"): what the saved files of a run directory still
say about somebody disturbing it. Prints the suspicious runs with their evidence, strong first, then
what the files cannot tell. --write adds the strong ones to results/interfered.txt (BENCH_RESULTS:
elsewhere), which report.py and matrix.py read, so these runs stay out of the medians and matrices.

Evidence, per file (each run directory has what ps3-bench.sh and client.sh saved):
  interference.log  the detector's own verdict (runs since it exists): taken as it is
  Xorg.0.log        the bare X server's log: a VT switch ("Suspending AIGLX clients for VT switch",
                    the user's Ctrl+Alt+F2 of 2026-10-17), an input device added after the start (not
                    the bench's virtual keyboards: /devices/virtual/input/), input devices removed
                    long before the server's end (a device unplugged, or the VT switch's own)
  dmesg.log         the kernel's lines during the run: a USB device connected or disconnected, an
                    input device registered that isn't virtual, Bluetooth HID lines
  game.log          Luanti logs what the player does ("singleplayer digs/places/activates/punches"):
                    the benchmark's flight never digs or clicks, so each line is a mouse button
                    pressed in the game; placed against the run's load, warm-up and window
Strength: strong (input or a switch that reached the game), medium (a device event, no proof it
touched the game). Key presses, mouse motion, controller input and SSH commands leave no trace in
these files: a run without evidence is not proven clean."""
import datetime
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LUANTI_ACT = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d): ACTION\[Server\]: singleplayer "
                        r"(digs|places|activates|punches|uses|moves|takes|puts|crafts|drops)\b(.*)$")
XLINE = re.compile(r"^\[\s*([\d.]+)\] (.*)$")


def kv(path):
    out = {}
    if path.exists():
        for line in path.read_text(errors="replace").splitlines():
            k, _, v = line.partition("=")
            out[k.strip()] = v.strip()
    return out


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def phase(b, rel):
    """where rel (seconds after t0) falls: load, warm-up, window"""
    w = [float(x) for x in (b.get("window") or "").split() if num(x) is not None]
    for i in range(0, len(w) - 1, 2):
        if w[i] <= rel <= w[i + 1]:
            return "window"
    le = num(b.get("load_end"))
    if le is not None and rel < le:
        return "load"
    if w and rel < w[0]:
        return "warm-up"
    if w and rel > w[-1]:
        return "after the window"
    return "run"


def scan_xorg(d, ev):
    f = d / "Xorg.0.log"
    if not f.exists():
        return
    lines = []
    for raw in f.read_text(errors="replace").splitlines():
        if m := XLINE.match(raw):
            lines.append((float(m[1]), m[2]))
    if not lines:
        return
    t_first, t_end = lines[0][0], lines[-1][0]
    adds = [(t, l) for t, l in lines if "config/udev: Adding input device" in l]
    t_init = adds[0][0] if adds else t_first
    for t, l in lines:
        if "Suspending AIGLX clients for VT switch" in l:
            ev.append(("strong", "Xorg.0.log: VT switch away at %.0f s after the X start" % (t - t_first)))
        elif "Resuming AIGLX clients after VT switch" in l:
            ev.append(("strong", "Xorg.0.log: back on the X VT at %.0f s" % (t - t_first)))
    virtual = set()    # event nodes of the bench's evemu keyboards (same name as the real receiver)
    for i, (t, l) in enumerate(lines):
        if "config/udev: Adding input device" in l:
            info = next((x for _, x in lines[i + 1:i + 12] if "config_info" in x), "")
            if "/devices/virtual/input/" in info:
                if m := re.search(r"/dev/input/(event\d+)", l):
                    virtual.add(m[1])
                continue
            if t <= t_init + 10:
                continue
            ev.append(("strong", "Xorg.0.log: input device added mid-run at %.0f s: %s" % (t - t_first, l.split("device ", 1)[-1])))
    # libinput removes and re-adds each device while X starts: only removals after that count
    removed = [(t, l) for t, l in lines if l.endswith("device removed") and t_init + 10 < t < t_end - 5
               and l.split()[1] not in virtual]
    if removed:
        ev.append(("medium", "Xorg.0.log: %d input device removal(s) %.0f s and more before the X end (%s)" % (
            len(removed), t_end - removed[-1][0], removed[0][1].split(" - ", 1)[-1])))


def scan_dmesg(d, ev):
    f = d / "dmesg.log"
    if not f.exists():
        return
    for l in f.read_text(errors="replace").splitlines():
        if "USB disconnect" in l or re.search(r"new .*USB device number", l):
            ev.append(("strong", "dmesg.log: " + l.strip()[:160]))
        elif re.search(r"input: .* as /devices/", l) and "/devices/virtual/input/" not in l:
            ev.append(("strong", "dmesg.log: " + l.strip()[:160]))
        elif re.search(r"Bluetooth: |hid-sony|\bsony \d|hidraw", l):
            ev.append(("medium", "dmesg.log: " + l.strip()[:160]))


def scan_game(d, b, ev, verbose):
    f = d / "game.log"
    if not f.exists() or (b.get("game") or "") != "luanti":
        return
    t0 = num(b.get("t0"))
    acts = []
    stamp = lambda x: datetime.datetime.strptime(x, "%Y-%m-%d %H:%M:%S").replace(tzinfo=datetime.timezone.utc).timestamp()
    for l in f.read_text(errors="replace").splitlines():
        if m := LUANTI_ACT.match(l):
            acts.append((stamp(m[1]), m[2], m[3].strip()))
        elif t0 is None and "ps3bench: path start" in l and num(b.get("load_end")) is not None:
            t0 = stamp(l[:19]) - num(b["load_end"])   # runs before t0=: the load mark is load_end
    if not acts:
        return
    by = {}
    for ts, what, _ in acts:
        p = phase(b, ts - t0) if t0 else "run"
        by.setdefault(p, {}).setdefault(what, 0)
        by[p][what] += 1
    first = acts[0][0] - t0 if t0 else None
    desc = "; ".join("%s: %s" % (p, ", ".join("%d %s" % (n, w) for w, n in sorted(c.items()))) for p, c in by.items())
    ev.append(("strong", "game.log: Luanti logged %d player action(s) (mouse buttons in the game)%s: %s" % (
        len(acts), " from %.0f s" % first if first is not None else "", desc)))
    if verbose:
        for ts, what, rest in acts[:10]:
            ev.append(("detail", "   %+.1f s %s %s" % (ts - t0 if t0 else 0, what, rest)))


def scan(d, verbose=False):
    d = Path(d)
    b = kv(d / "bench.txt")
    ev = []
    il = d / "interference.log"
    if il.exists():
        t = il.read_text(errors="replace")
        m = re.search(r"^# interfered=(\d)", t, re.M)
        s = re.search(r"^# interference: (.*)$", t, re.M)
        if m and m[1] == "1":
            ev.append(("strong", "interference.log: " + (s[1] if s else "?")))
        elif not m:
            ev.append(("medium", "interference.log without a summary (the detector didn't finish)"))
    scan_xorg(d, ev)
    scan_dmesg(d, ev)
    scan_game(d, b, ev, verbose)
    return b, ev


def main(argv):
    write = "--write" in argv
    verbose = "-v" in argv
    dirs = sorted(a for a in argv if not a.startswith("-"))
    if not dirs:
        sys.exit(__doc__)
    have = {k: 0 for k in ("bench.txt", "Xorg.0.log", "dmesg.log", "game.log", "interference.log")}
    found = []
    for d in dirs:
        for k in have:
            have[k] += (Path(d) / k).exists()
        b, ev = scan(d, verbose)
        if ev:
            found.append((d, b, ev))
    print("%d run directories scanned; files: %s" % (len(dirs), ", ".join("%s %d" % kv_ for kv_ in have.items())))
    strong = []
    for d, b, ev in sorted(found, key=lambda x: (not any(e[0] == "strong" for e in x[2]), x[0])):
        s = any(e[0] == "strong" for e in ev)
        print("\n%s %s (%s %s)" % ("STRONG" if s else "medium", Path(d).name, b.get("game", "?"), b.get("variant", "?")))
        for k, e in ev:
            print("   %s%s" % ("" if k == "detail" else "[%s] " % k, e))
        if s:
            strong.append((Path(d).name, next(e for k, e in ev if k == "strong")))
    print("\n%d suspicious run(s), %d with strong evidence." % (len(found), len(strong)))
    print("Not knowable from these files: key presses, mouse motion and controller input that left no "
          "game-side trace, SSH logins and commands during a run (the journal is not saved per run), the "
          "game window losing the focus.")
    if write and strong:
        f = Path(os.environ.get("BENCH_RESULTS") or HERE / "results") / "interfered.txt"
        old = f.read_text().splitlines() if f.exists() else [
            "# runs found disturbed after the fact (bench/interference-scan.py --write, docs/STALLS.md",
            "# \"Interference\"): run directory, then the evidence. report.py and matrix.py leave them out."]
        names = {l.split(" ", 1)[0] for l in old if l and not l.startswith("#")}
        new = old + ["%s %s" % (n, why) for n, why in strong if n not in names]
        f.write_text("\n".join(new) + "\n")
        print(">> %s: %d run(s) listed" % (f, len([l for l in new if l and not l.startswith("#")])))


if __name__ == "__main__":
    main(sys.argv[1:])
