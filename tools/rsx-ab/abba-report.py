#!/usr/bin/env python3
"""abba-report.py ARMS_LOG RUN_DIR...: a same-boot ps3rsx A/B (abba-batch.sh) read on the PC after
`make bench-pull`. Each run directory goes to the arm loaded at its start (rsx-arm.sh's log, by UTC
stamp), the arms numbered in load order (A1, B1, B2, A2, then extra arms); the bench runs are read
with bench/analysis/report.py, the desktop cases from their "rsxab:" lines. One table per case, a
column per arm, then the verdict per metric:
  significant  a B arm worse than both A arms by more than the A1-to-A2 difference (the boot's own
               noise), for stall time, 1% low, load time, mpv's dropped frames and the X loop's p99
               and total; or steady fps more than 3% below the lower A arm
  ok           neither
A run the interference detector flagged is shown but left out (report.py's rule). Exit 1 when any
metric of any case is significant: the change waits until it is optimised (round-2 plan)."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bench" / "analysis"))
import report  # noqa: E402

STAMP = re.compile(r"(\d{8}T\d{6}Z)")
# metric: (label, worse when higher?)
BENCH = [("steady", "steady fps", False), ("low1", "1% low", False), ("stall_s", "stall s", True),
         ("load", "load s", True), ("longest", "longest ms", True)]
DESK = {"mpv": [("drops", "dropped frames", True), ("delayed", "delayed frames", True)],
        "xloop": [("p99_ms", "step p99 ms", True), ("median_ms", "step median ms", True),
                  ("total_s", "total s", True)]}
VERDICT = {"stall_s", "low1", "load", "drops", "p99_ms", "total_s"}


def arms_of(log):
    """[(stamp, arm instance)] in load order: A1, B1, B2, A2, ..."""
    seen, out = {}, []
    for line in Path(log).read_text().splitlines():
        f = line.split("\t")
        if len(f) < 2:
            continue
        seen[f[1]] = seen.get(f[1], 0) + 1
        params = f[3].strip() if len(f) > 3 else ""
        out.append((f[0], "%s%d%s" % (f[1], seen[f[1]], "(" + params + ")" if params else "")))
    return out


def arm_at(arms, stamp):
    cur = None
    for s, a in arms:
        if s <= stamp:
            cur = a
    return cur


def desk_row(d):
    m = re.search(r"rsxab-(mpv|xloop)-", d.name)
    out = (d / "out.log").read_text(errors="replace") if (d / "out.log").exists() else ""
    vals = {}
    for line in out.splitlines():
        if "rsxab:" in line:
            for k, v in re.findall(r"(\w+)=([-\d.]+)", line.split("rsxab:", 1)[1]):
                try:
                    vals[k] = float(v)
                except ValueError:
                    pass
    intf = (d / "bench.txt").exists() and "interfered=1" in (d / "bench.txt").read_text()
    return m.group(1), vals, intf


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    arms = arms_of(argv[0])
    order = [a for _, a in arms]
    cases = {}		# case -> arm -> metrics
    for d in map(Path, argv[1:]):
        st = STAMP.search(d.name)
        a = arm_at(arms, st.group(1)) if st else None
        if not a:
            continue
        if "rsxab-" in d.name:
            case, vals, intf = desk_row(d)
        else:
            r = report.run(d)
            case, intf = "%s-%s" % (r["group"], r["variant"].split("@")[0]), r.get("intf") == 1
            vals = {k: r[k] for k, _, _ in BENCH if isinstance(r.get(k), (int, float))}
        if intf:
            print("!! %s: interfered, left out" % d.name)
            continue
        cases.setdefault(case, {})[a] = vals
    bad = False
    for case, by_arm in cases.items():
        metrics = DESK.get(case, BENCH)
        cols = [a for a in order if a in by_arm]
        print("\n%s" % case)
        print("  %-16s %s" % ("", "  ".join("%10s" % c for c in cols)))
        # arms loaded with extra parameters (attribution, a VRAM swap setting) are shown, not judged
        a_arms = [c for c in cols if c.startswith("A") and "(" not in c]
        b_arms = [c for c in cols if c.startswith("B") and "(" not in c]
        for key, label, higher_worse in metrics:
            row = [by_arm[c].get(key) for c in cols]
            line = "  %-16s %s" % (label, "  ".join("%10s" % ("-" if v is None else round(v, 2)) for v in row))
            av = [by_arm[c][key] for c in a_arms if key in by_arm[c]]
            bv = [by_arm[c][key] for c in b_arms if key in by_arm[c]]
            verdict = ""
            if len(av) == 2 and bv:
                noise = abs(av[0] - av[1])
                if key == "steady":
                    sig = any(b < 0.97 * min(av) for b in bv)
                elif higher_worse:
                    sig = any(b - max(av) > noise for b in bv)
                else:
                    sig = any(min(av) - b > noise for b in bv)
                if key in VERDICT or key == "steady":
                    verdict = "  SIGNIFICANT" if sig else "  ok"
                    bad |= sig
                verdict += " (A/A noise %.2f)" % noise
            elif bv:
                verdict = "  (needs both A arms)"
            print(line + verdict)
    print("\n%s" % ("a significant regression: the change waits until it's optimised" if bad else
                    "no significant regression"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
