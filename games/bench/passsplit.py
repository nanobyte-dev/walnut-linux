#!/usr/bin/env python3
"""passsplit.py [--re REGEX] [--after S] [-v] <run dir>...: for a game whose recipe repeats a demo
(PASSES=1) and reloads at every pass, the window's stalls (stalls.py) split into the reload and
the demo. The reload segments are report.py's (reload_segments: each game's RELOADS entry; OpenArena:
a pass's start to S seconds (default 1) after its first out.log line matching "CL_InitCGame:";
Neverball, dsda-doom, X-Moto: a fixed time after the pass's start). --re/--after replace the
game's with a load-done REGEX. Needs the stall record (STALLS=1) and, for a REGEX, bench.txt's t0.
-v lists every stall with its pass, the seconds into it and the part."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import report  # noqa: E402
import stalls  # noqa: E402


def split(d, load_re, after):
    """(reload stalls, demo stalls, rows) of run directory d, or None without what it needs. The
    reload segments are report.reload_segments' (load_re/after override the game's, None: its own)"""
    d = Path(d)
    res = stalls.analyze(d)
    b = report.kv(d / "bench.txt")
    if load_re is not None:
        b["reload_re"] = load_re.pattern
        b["reload_after"] = str(after)
        b.pop("reload_fixed", None)
    segs = report.reload_segments(d, b) if res and res["measured"] else None
    if segs is None:
        return None
    passes = []
    for k, v in b.items():
        if k.startswith("pass") and k[4:].isdigit():
            f = v.split()
            passes.append((float(f[0]), float(f[1]), k))
    passes.sort()
    reload, demo, rows = [], [], []
    for x in sorted(res["stalls"], key=lambda x: x["at"]):
        p = next((q for q in passes if q[0] <= x["at"] < q[1]), None)
        part = "reload" if any(s <= x["at"] < t for s, t in segs) else "demo"
        (reload if part == "reload" else demo).append(x)
        rows.append("  %8.2f s  %-7s +%6.2f s  %6.0f ms  %-7s %s" % (
            x["at"], p[2] if p else "-", x["at"] - p[0] if p else 0, x["ms"], x["cause"], part))
    return reload, demo, rows


def main(argv):
    load_re, after, verbose, dirs = None, 1.0, False, []
    it = iter(argv)
    for a in it:
        if a == "--re":
            load_re = re.compile(next(it))
        elif a == "--after":
            after = float(next(it))
        elif a == "-v":
            verbose = True
        else:
            dirs.append(a)
    if not dirs:
        sys.exit(__doc__)
    for d in dirs:
        r = split(d, load_re, after)
        if r is None:
            print("== %s: no stall record, window or t0" % d)
            continue
        reload, demo, rows = r
        print("== %s: reload %d stalls %.1f s, demo %d stalls %.1f s" % (
            d, len(reload), sum(x["ms"] for x in reload) / 1000, len(demo),
            sum(x["ms"] for x in demo) / 1000))
        if verbose:
            print("\n".join(rows))


if __name__ == "__main__":
    main(sys.argv[1:])
