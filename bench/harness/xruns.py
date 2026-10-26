#!/usr/bin/env python3
"""xruns.py LOG T0 [TSV]: the sound dropouts ps3-xrunlog.so logged in a run (client.sh: $D/xruns.log),
summed up as bench.txt lines on stdout, and one row per dropout in TSV (client.sh: $D/xruns.tsv).
docs/games/SUITE.md "Sound dropouts".

A dropout is a moment the game's sound output ran dry before the game wrote more: an ALSA underrun
(SDL's "underrun occurred", OpenAL's xruns) or a restart of the stream. The plugin measures how long
the output stayed dry (gap_ms) and how loud the 20 ms written before and after it were (RMS dBFS).
A count says nothing about what was heard; this rule decides it:

  audible = gap >= AUDIBLE_GAP_MS and (before or after >= AUDIBLE_DBFS)

A gap of a few ms is inaudible, tens of ms is a click or a chop; a dropout in silence can't be heard,
one with sound on either side can (a cut into silence too). When neither side's loudness could be read
(nan: a sample format the plugin doesn't decode) only the gap decides.

bench.txt lines: xrunlog=on (the plugin saw a playback stream) or unused (it never did: no sound, or
the game opened another device than default), xruns_logged, xruns_audible, xrun_gap_ms_total (all
dropouts), xrun_gap_ms_max, xrun_audible_ms (the audible ones' gaps summed), xrun_rule. TSV columns:
at_s (the dry moment, seconds after T0, the run's $EPOCHREALTIME start), gap_ms, before_db, after_db,
audible (0/1), via (prepare: a recover or restart; late: a write after the queue ran out), resumed,
pid, comm.
"""
import math
import sys

AUDIBLE_GAP_MS = 20.0
AUDIBLE_DBFS = -50.0
RULE = "gap>=%gms,loud>=%gdBFS" % (AUDIBLE_GAP_MS, AUDIBLE_DBFS)


def fields(line):
    parts = line.rstrip("\n").split("\t")
    return parts[0], dict(p.split("=", 1) for p in parts[1:] if "=" in p)


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return math.nan


def audible(gap_ms, before_db, after_db):
    if gap_ms < AUDIBLE_GAP_MS:
        return False
    if math.isnan(before_db) and math.isnan(after_db):
        return True     # loudness unknown: the gap alone
    return before_db >= AUDIBLE_DBFS or after_db >= AUDIBLE_DBFS


def parse(text):
    """(streams opened, [dropout dicts]) of a log's text; broken lines are skipped"""
    opens, events = 0, []
    for line in text.splitlines():
        kind, f = fields(line)
        if kind == "open":
            opens += 1
        elif kind == "xrun" and "gap_ms" in f:
            g, b, a = num(f.get("gap_ms")), num(f.get("before_db")), num(f.get("after_db"))
            if math.isnan(g):
                continue
            events.append({"dry_real": num(f.get("dry_real")), "gap_ms": max(0.0, g), "before_db": b,
                           "after_db": a, "audible": audible(g, b, a), "via": f.get("via", "-"),
                           "resumed": f.get("resumed", "-"), "pid": f.get("pid", "-"), "comm": f.get("comm", "-")})
    return opens, events


def summary(text, t0):
    """(bench.txt lines, TSV text)"""
    opens, ev = parse(text)
    if not opens and not ev:
        return ["xrunlog=unused"], ""
    gaps = [e["gap_ms"] for e in ev]
    lines = ["xrunlog=on", "xruns_logged=%d" % len(ev), "xruns_audible=%d" % sum(e["audible"] for e in ev),
             "xrun_gap_ms_total=%.1f" % sum(gaps), "xrun_gap_ms_max=%.1f" % max(gaps, default=0.0),
             "xrun_audible_ms=%.1f" % sum(e["gap_ms"] for e in ev if e["audible"]), "xrun_rule=" + RULE]
    db = lambda v: "nan" if math.isnan(v) else ("-inf" if math.isinf(v) else "%.1f" % v)
    rows = ["at_s\tgap_ms\tbefore_db\tafter_db\taudible\tvia\tresumed\tpid\tcomm"]
    for e in ev:
        at = e["dry_real"] - t0 if t0 is not None and not math.isnan(e["dry_real"]) else math.nan
        rows.append("\t".join(["nan" if math.isnan(at) else "%.3f" % at, "%.1f" % e["gap_ms"], db(e["before_db"]),
                               db(e["after_db"]), str(int(e["audible"])), e["via"], e["resumed"], e["pid"],
                               e["comm"]]))
    return lines, "\n".join(rows) + "\n"


def main(argv):
    if len(argv) < 3:
        print(__doc__.split("\n")[0], file=sys.stderr)
        return 2
    try:
        with open(argv[1], errors="replace") as f:
            text = f.read()
    except FileNotFoundError:
        text = ""
    lines, tsv = summary(text, num(argv[2]) if not math.isnan(num(argv[2])) else None)
    print("\n".join(lines))
    if len(argv) > 3 and tsv:
        with open(argv[3], "w") as f:
            f.write(tsv)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
