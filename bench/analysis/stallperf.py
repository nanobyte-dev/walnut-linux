#!/usr/bin/env python3
"""stallperf.py RUN_DIR [PERF_SCRIPT] [--top N]: where the CPU went in a run's stall frames.

A run recorded with STALLS=1 and PERF=1 (docs/STALLS.md, docs/PROFILING.md): the stall frames are
stalls.py's (frames over 100 ms in the window, placed on the recorder's clock), the samples perf's
(cpu-clock, -k mono: CLOCK_MONOTONIC, the recorder's clock too; the recorder's header has the
monotonic time of its start). For the stall frames and for the rest of the window it prints

- the CPU seconds of every recorded thread (the game's, Xorg, kswapd0): who had the PPE;
- the main thread's samples, inclusive (anywhere on the stack) and self, by function.

The recorder's `cpu` cause means the main thread was in state R, running or waiting to run; `runq`
needs the whole process to have been starved, so a game whose other threads keep the CPU busy shows
`cpu` while its main thread waits (gamedb:luanti 2026-10-29: the main thread ran 22% of its stall
time). The first table here tells the two apart.

PERF_SCRIPT is `perf script -f --no-inline -F comm,tid,time,ip,sym,dso` of the run's perf.data, plain
or gzipped (default: RUN_DIR/perf-script.txt.gz). On the PS3, as root, about 30 s a run:
    sudo perf script -f -i D/perf.data --no-inline -F comm,tid,time,ip,sym,dso | gzip -1 > D/perf-script.txt.gz
"""
import argparse
import bisect
import gzip
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import report  # noqa: E402
import stalls  # noqa: E402

HDR = re.compile(r"^(\S.*?)\s+(\d+)\s+(\d+\.\d+):")


def samples(path):
    """(comm, tid, time, [frames, leaf first]) from perf script output"""
    op = gzip.open if str(path).endswith(".gz") else open
    cur = None
    with op(path, "rt", errors="replace") as f:
        for line in f:
            if not line.strip():
                continue
            if not line[0].isspace():
                m = HDR.match(line)
                if m:
                    if cur:
                        yield cur
                    cur = (m.group(1), int(m.group(2)), float(m.group(3)), [])
                continue
            if cur is not None:
                p = line.strip().split(None, 1)
                if len(p) == 2:
                    sym = re.sub(r"\+0x[0-9a-f]+", "", p[1])
                    sym = re.sub(r"\s+\((\S+)\)$", lambda m: " [" + m.group(1).rsplit("/", 1)[-1] + "]", sym)
                    cur[3].append(sym)
    if cur:
        yield cur


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("run")
    ap.add_argument("script", nargs="?")
    ap.add_argument("--top", type=int, default=30)
    a = ap.parse_args()
    d = Path(a.run)
    res = stalls.analyze(d)
    if not res or not res["measured"]:
        sys.exit(f"{d}: no stall record with a window")
    head = (d / "stalls.rec").read_text(errors="replace")[:4000]
    m = re.search(r"# clock mono=([0-9.]+)", head)
    if not m:
        sys.exit(f"{d}: stalls.rec has no monotonic clock line")
    off = res["offset"] + float(m.group(1))  # client seconds -> CLOCK_MONOTONIC
    iv = sorted((s["at"] + off, s["at"] + off + s["ms"] / 1000) for s in res["stalls"])
    starts = [x for x, _ in iv]
    b = report.kv(d / "bench.txt")
    segs = [(x + off, y + off) for x, y in report.window(b)]
    pid = int(b["game_pid"])
    hz = float(b.get("perf_hz") or 499)

    def in_stall(t):
        k = bisect.bisect_right(starts, t) - 1
        return k >= 0 and t <= iv[k][1]

    thr = {"stall": Counter(), "other": Counter()}
    incl = {"stall": Counter(), "other": Counter()}
    self_ = {"stall": Counter(), "other": Counter()}
    nmain = Counter()
    for comm, tid, t, fr in samples(a.script or d / "perf-script.txt.gz"):
        if not any(x <= t <= y for x, y in segs):
            continue
        g = "stall" if in_stall(t) else "other"
        thr[g]["main thread" if tid == pid else comm] += 1
        if tid != pid:
            continue
        nmain[g] += 1
        if fr:
            self_[g][fr[0]] += 1
        for s in set(fr):
            incl[g][s] += 1
    st = sum(y - x for x, y in iv)
    win = sum(y - x for x, y in segs)
    wall = {"stall": st, "other": win - st}
    print(f"{d.name}: {len(iv)} stall frames, {st:.1f} s of the {win:.1f} s window; {hz:g} samples/s a thread")
    for g in ("stall", "other"):
        print(f"\nCPU seconds in the {g} frames ({wall[g]:.1f} s of wall time):")
        for k, v in thr[g].most_common(12):
            print(f"  {k:<18} {v / hz:6.1f} s  {100 * v / hz / wall[g]:5.1f}% of the time")
    for g in ("stall", "other"):
        n = nmain[g] or 1
        print(f"\nmain thread, {g} frames: {nmain[g]} samples; inclusive")
        for s, c in incl[g].most_common(a.top):
            print(f"  {100 * c / n:5.1f}%  {s[:140]}")
        print("  self")
        for s, c in self_[g].most_common(a.top // 2):
            print(f"  {100 * c / n:5.1f}%  {s[:140]}")


if __name__ == "__main__":
    main()
