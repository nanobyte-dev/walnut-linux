#!/usr/bin/env python3
"""report.py <run dir>...: one line per games/bench run (ps3-bench.sh run directories with the
bench.txt client.sh writes), then the median per game and variant, then, when a game ran in more
than one variant, each variant against the first: fps and load as ratios, so a decision reads
across games. Columns:
  load    seconds to load (the game's own number, or the stamp of its LOAD_MARK line)
  fps     the game's own average; maxms its longest frame
  hud     Gallium HUD fps median/p10/min per second after loading (the whole run after the load)
steady state (client.sh: WARMUP s after the load left out, then at least MEASURE s measured; runs
from before 2026-10-17 have none):
  steady  the steady-state fps: the median of the counted passes' own fps (src passes), the game's
          own result over its window (src game), else the median of the HUD's per-second fps in the
          window (src hud)
  low1    the 1% low in the window: 1000 / the 99th percentile frame time (from the HUD's frame
          times; runs with only per-second fps: the 1st percentile of those seconds, src hud1s)
  stalls  frames over 100 ms in the window (per-second data only: seconds under 10 fps)
  longest the longest frame in the window, ms (HUD frame times, else the passes' own maxms)
  win     seconds measured; ftdrift: run time minus the end of the last HUD frame (s: large when the
          frame times are misplaced against the clock, e.g. a renderer restart's gap)
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
    steady(d, b, r)
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


STALL_MS = 100     # a frame longer than this is a stall
STALL_FPS = 10     # with per-second data only: a second under this many frames


def floats(path):
    out = []
    for x in path.read_text(errors="replace").split():
        try:
            out.append(float(x))
        except ValueError:
            pass
    return out


def steady(d, b, r):
    """the steady-state metrics of run directory d (bench.txt b) into r"""
    w = [float(x) for x in (b.get("window") or "").split()]
    if not w or len(w) % 2:
        return
    segs = list(zip(w[::2], w[1::2]))   # the window, maybe in pieces (WIN_ON_RE/WIN_OFF_RE)
    a, z = segs[0][0], segs[-1][1]

    def inside(e):
        return any(s < e <= t for s, t in segs)
    t1 = num(b.get("hud_t1"))
    secs = num(b.get("seconds"))
    ft_path, fps_path = d / "hud" / "frametime_(ms)", d / "hud" / "fps"
    if ft_path.exists() and (ft := floats(ft_path)):
        # frame i ends at t1 + the frame times after the first (t1: when the dump got its first value)
        ends, t = [], (t1 if t1 is not None else (secs or 0) - sum(ft[1:]) / 1000)
        for i, v in enumerate(ft):
            if i:
                t += v / 1000
            ends.append(t)
        if secs is not None:
            r["ftdrift"] = round(secs - ends[-1], 1)
        inw = [(e, v) for e, v in zip(ends, ft) if inside(e)]
        if inw:
            ms = sorted(v for _, v in inw)
            bins = []   # frames in each whole second of each piece
            for s, t in segs:
                n = [0] * int(t - s)
                for e, _ in inw:
                    if s < e <= t and int(e - s) < len(n):
                        n[int(e - s)] += 1
                bins += n
            bins = sorted(bins) or [len(inw)]
            r["hud_win"] = bins[len(bins) // 2]
            r["low1"] = round(1000 / ms[max(0, -(-len(ms) * 99 // 100) - 1)], 1)
            r["stalls"] = sum(1 for v in ms if v > STALL_MS)
            r["longest"] = round(ms[-1], 1)
            r["win"] = round(sum(min(t, inw[-1][0]) - s for s, t in segs if s < inw[-1][0]), 1)
            r["src"] = "hud"
    elif fps_path.exists() and (f := floats(fps_path)):
        # one value per second: value i ends about i + 1 s after the HUD's first value
        t0 = (t1 - 1) if t1 is not None else 0
        inw = [v for i, v in enumerate(f) if inside(t0 + i + 1)]
        if inw:
            v = sorted(inw)
            r["hud_win"] = int(v[len(v) // 2])
            r["low1"] = round(v[len(v) // 100], 1)
            r["stalls"] = sum(1 for x in v if x < STALL_FPS)
            r["win"] = len(v)
            r["src"] = "hud1s"
    if "hud_win" in r:
        r["steady"] = r["hud_win"]
    # the game's own numbers over the window, when it measures itself
    passes = [b[k].split() for k in sorted(b, key=lambda k: int(k[4:]) if k[4:].isdigit() else 0)
              if re.fullmatch(r"pass\d+", k)]
    counted = [p for p in passes if len(p) == 5 and p[4] == "1"]
    if passes:
        r["passes"] = "%d/%d" % (len(counted), len(passes))
    pf = [x for x in (num(p[2]) for p in counted) if x is not None]
    if pf:
        r["steady"] = round(statistics.median(pf), 2)
        r["src"] = "passes"
        r["win"] = round(sum(t - s for s, t in segs), 1)
        pm = [x for x in (num(p[3]) for p in counted) if x is not None]
        if pm and "longest" not in r:
            r["longest"] = max(pm)
    elif (v := num(b.get("steady_fps"))) is not None:
        r["steady"] = v
        r["src"] = "game"
        if (m := num(b.get("steady_maxms"))) is not None and "longest" not in r:
            r["longest"] = m


COLS = ["load", "fps", "maxms", "hud", "steady", "src", "low1", "stalls", "longest", "win", "passes",
        "ftdrift", "cpu", "rss", "vram", "gart", "avail", "so_load", "so_all",
        "vsw_peak", "vsw_wb", "xrun", "snd", "end"]
RANGED = ("load", "fps", "maxms", "hud_med", "steady", "low1")


def show(r):
    return "  ".join("%s=%s" % (c, r.get(c, "-")) for c in COLS)


def main(dirs):
    runs = [run(d) for d in dirs]
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
        print("\nagainst each game's first variant (fps: game's own, else HUD median; steady: the"
              " steady-state fps):")
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
                sr = "%.3f" % (m["steady"] / a["steady"]) if a.get("steady") and m.get("steady") else "-"
                print("  %-14s %s/%s  fps x%s  steady x%s  load x%s" % (g, k[1], vs[0][1], fr, sr, lr))


if __name__ == "__main__":
    main(sys.argv[1:])
