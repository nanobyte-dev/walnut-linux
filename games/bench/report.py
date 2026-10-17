#!/usr/bin/env python3
"""report.py <run dir>...: one line per games/bench run (ps3-bench.sh run directories with the
bench.txt client.sh writes), then the median per game and variant, then, when a game ran in more
than one variant, each variant against the first: fps and load as ratios, so a decision reads
across games. Columns:
  load    seconds to load (the game's own number, or the stamp of its LOAD_MARK line)
  fps     the game's own average; maxms its longest frame
  hud     Gallium HUD fps median/p10/min per second after loading
  cpu     the client's user+sys seconds (the game and its helpers)
  rss     the game's peak RSS (VmHWM, MiB)
  vram    the game's peak VRAM (KiB, ps3rsx per-client, all its buffers)
  gart    peak "GART backed by memory" (KiB: RAM held for GPU staging)
  avail   lowest MemAvailable (KiB)
  so      pages swapped out until the load ended / during the whole run
  vsw     VRAM swap: peak pages in VRAM, pages written back to the disk (shrinks)
  xrun    ALSA underruns (the sound buffer ran dry: audible chopping)
  snd     the sound card state(s) at SOUND_AT seconds; end: how the run ended (exit, end, duration)"""
import re
import statistics
import sys
from pathlib import Path


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


def run(d):
    d = Path(d)
    b = kv(d / "bench.txt")
    label = re.sub(r"^(ps3-bench-)?\d{8}T\d{6}Z-", "", d.name)
    r = {"game": b.get("game") or label, "variant": b.get("variant") or "-", "dir": d.name,
         "snd": b.get("sound") or "-", "end": b.get("ended") or "-"}
    if (v := num(b.get("underruns"))) is not None:
        r["xrun"] = int(v)
    for k in ("fps", "maxms", "load"):
        if (v := num(b.get(k))) is not None:
            r[k] = v
    if (v := num(b.get("hwm"))) is not None:
        r["rss"] = round(v / 1024, 1)
    res = (d / "result").read_text(errors="replace") if (d / "result").exists() else ""
    if m := re.search(r"client: real [\d.]+ s, user ([\d.]+) s, sys ([\d.]+) s", res):
        r["cpu"] = round(float(m[1]) + float(m[2]), 2)
    # the HUD after loading: the load time if known, else the recipe's HUD_SKIP
    skip = int(r.get("load") or num(b.get("hud_skip")) or 12) + 1
    hud = d / "hud" / "fps"
    if hud.exists():
        f = sorted(int(float(x.split()[0])) for x in hud.read_text().splitlines()[skip:] if x.strip())
        if f:
            r["hud"] = "%d/%d/%d" % (f[len(f) // 2], f[len(f) // 10], f[0])
            r["hud_med"] = f[len(f) // 2]
    # samples: the start is the first stamped output line, the load ends LOAD seconds later
    t0 = None
    out = d / "out.log"
    if out.exists():
        for line in out.read_text(errors="replace").splitlines():
            if (t0 := num(line.partition(" ")[0])) is not None:
                break
    t_load = t0 + r["load"] if t0 is not None and "load" in r else None
    comm = (b.get("comm") or "")[:15]
    vram = gart = vv = 0
    avail = None
    so0 = so_load = so_end = wb0 = wb_end = None
    samples = d / "samples"
    for line in (samples.read_text().splitlines() if samples.exists() else []):
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
            name, v, _ = c.rsplit(":", 2)
            if comm and name[:15] == comm:
                vram = max(vram, int(v))
    r.update(vram=vram, gart=gart, avail=avail, vsw_peak=vv,
             so_load=(so_load or 0) - (so0 or 0), so_all=(so_end or 0) - (so0 or 0),
             vsw_wb=(wb_end or 0) - (wb0 or 0))
    return r


COLS = ["load", "fps", "maxms", "hud", "cpu", "rss", "vram", "gart", "avail", "so_load", "so_all",
        "vsw_peak", "vsw_wb", "xrun", "snd", "end"]
RANGED = ("load", "fps", "maxms", "hud_med")


def show(r):
    return "  ".join("%s=%s" % (c, r.get(c, "-")) for c in COLS)


runs = [run(d) for d in sys.argv[1:]]
for r in runs:
    print("%-24s %s" % ("%s-%s" % (r["game"], r["variant"]), show(r)))
print()
med = {}
keys = list(dict.fromkeys((r["game"], r["variant"]) for r in runs))
for key in keys:
    rs = [r for r in runs if (r["game"], r["variant"]) == key]
    m = {}
    for c in COLS + ["hud_med"]:
        vals = [r[c] for r in rs if isinstance(r.get(c), (int, float))]
        if vals:
            m[c] = round(statistics.median(vals), 2)
    med[key] = m
    shown = dict(m)
    for c in RANGED:
        vals = [r[c] for r in rs if isinstance(r.get(c), (int, float))]
        if c in shown and len(vals) > 1:
            shown[c] = "%s [%s..%s]" % (shown[c], min(vals), max(vals))
    shown["hud"] = shown.pop("hud_med", "-")
    print("%-24s n=%d %s" % ("%s-%s" % key, len(rs), show(shown)))

games = list(dict.fromkeys(g for g, _ in keys))
if any(len([k for k in keys if k[0] == g]) > 1 for g in games):
    print("\nagainst each game's first variant (fps: game's own, else HUD median):")
    for g in games:
        vs = [k for k in keys if k[0] == g]
        if len(vs) < 2:
            continue
        a = med[vs[0]]
        fa = a.get("fps") or a.get("hud_med")
        for k in vs[1:]:
            m = med[k]
            fb = m.get("fps") or m.get("hud_med")
            fr = "%.3f" % (fb / fa) if fa and fb else "-"
            lr = "%.3f" % (m["load"] / a["load"]) if a.get("load") and m.get("load") else "-"
            print("  %-14s %s/%s  fps x%s  load x%s" % (g, k[1], vs[0][1], fr, lr))
