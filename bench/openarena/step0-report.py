#!/usr/bin/env python3
"""Superseded by bench/report.py; reads only run directories from before 2026-10-17, when
ps3-bench.sh still grepped OpenArena's lines into `result`.

step0-report.py <run dir>...: one line per ps3-bench.sh run (copied from the PS3's
/var/tmp/ps3-bench), then the median per label. Columns:
  load    CL_InitCGame seconds (the level load)
  fps     the timedemo's average; maxms its longest frame; hud median/p10/min per second
          after loading
  cpu     the game's user+sys seconds
  vram    the game's peak VRAM (KiB, ps3rsx per-client, all its buffers)
  gart    peak "GART backed by memory" (KiB: RAM held for GPU staging)
  avail   lowest MemAvailable (KiB)
  so      pages swapped out until the load ended / during the whole run
  vsw     VRAM swap: peak pages in VRAM, pages written back to the disk (shrinks)"""
import re
import statistics
import sys
from pathlib import Path


def run(d):
    d = Path(d)
    out = (d / "out.log").read_text(errors="replace").splitlines()
    res = (d / "result").read_text(errors="replace")
    r = {"label": re.sub(r"^(ps3-bench-)?\d{8}T\d{6}Z-", "", d.name)}
    t_load = None
    for line in out:
        t, _, text = line.partition(" ")
        if m := re.search(r"CL_InitCGame: ([\d.]+) seconds", text):
            r["load"] = float(m[1])
            t_load = float(t)
    if m := re.search(r"frames ([\d.]+) seconds ([\d.]+) fps [\d.]+/[\d.]+/([\d.]+)/", res):
        r["fps"] = float(m[2])
        r["maxms"] = float(m[3])
    if m := re.search(r"hud fps: median (\d+) p10 (\d+) min (\d+)", res):
        r["hud"] = "%s/%s/%s" % m.groups()
    if m := re.search(r"client: real [\d.]+ s, user ([\d.]+) s, sys ([\d.]+) s", res):
        r["cpu"] = float(m[1]) + float(m[2])
    vram = gart = vv = 0
    avail = None
    so0 = so_load = so_end = wb0 = wb_end = None
    for line in (d / "samples").read_text().splitlines():
        head, _, clients = line.partition("|")
        f = head.split()
        if len(f) < 9:
            continue
        t, a, _, _, so, g, v_in, _, wb = f[:9]
        t, a, so = float(t), int(a), int(so)
        avail = a if avail is None else min(avail, a)
        gart = max(gart, int(g or 0))
        vv = max(vv, int(v_in or 0))
        if so0 is None:
            so0, wb0 = so, int(wb or 0)
        if t_load is None or t <= t_load:
            so_load = so
        so_end, wb_end = so, int(wb or 0)
        for c in clients.split():
            comm, v, _ = c.rsplit(":", 2)
            if comm == "ioquake3":
                vram = max(vram, int(v))
    r.update(vram=vram, gart=gart, avail=avail, vsw_peak=vv,
             so_load=(so_load or 0) - (so0 or 0), so_all=(so_end or 0) - (so0 or 0),
             vsw_wb=(wb_end or 0) - (wb0 or 0))
    return r


COLS = ["load", "fps", "maxms", "hud", "cpu", "vram", "gart", "avail", "so_load", "so_all", "vsw_peak", "vsw_wb"]


def show(r):
    return "  ".join("%s=%s" % (c, r.get(c, "-")) for c in COLS)


runs = [run(d) for d in sys.argv[1:]]
for r in runs:
    print("%-16s %s" % (r["label"], show(r)))
print()
for label in sorted({r["label"] for r in runs}):
    rs = [r for r in runs if r["label"] == label]
    med = {}
    for c in COLS:
        vals = [r[c] for r in rs if isinstance(r.get(c), (int, float))]
        if vals:
            med[c] = round(statistics.median(vals), 2)
            if c in ("load", "fps", "maxms") and len(vals) > 1:
                med[c] = "%s [%s..%s]" % (med[c], min(vals), max(vals))
    print("%-16s n=%d %s" % (label, len(rs), show(med)))
