#!/usr/bin/env python3
"""report.py [--stalls] [--perf] [--mem] <run dir>...: one line per games/bench run (ps3-bench.sh run directories with the
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
  smooth  fps over the window's frames of 100 ms or less (HUD frame times): the rendering rate with
          the stalls left out; stall_s the seconds the window spent in frames over 100 ms. A
          change in steady with smooth unchanged is a change in stalls, not in the cost of a frame
  longest the longest frame in the window, ms (HUD frame times, else the passes' own maxms)
  *_play  the same over the gameplay only: the window less the reload segments of a game that loops
          its demo (stalls_play, stall_play_s, longest_play, low1_play, win_play; reload_s the
          seconds left out, reload_n the segments). A segment is a pass's start to the load-done
          line plus 1 s (OpenArena: CL_InitCGame, needs t0 and out.log) or a fixed time after the
          pass's start (Neverball, dsda-doom, X-Moto); RELOADS below holds each game's, and a recipe's
          parse() may print reload_re / reload_after / reload_fixed to bench.txt to set its own.
          Games without a reload in the window (Sauerbraten's WIN_ON/OFF pieces already leave it
          out, SuperTuxKart's one race) get the plain numbers; src hud only
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
  tmax    the hottest temperature over the run per zone, degrees C, Cell/RSX (ps3-bench.sh temp_max=,
          from the kernel's ps3_sm sensors, docs/THERMAL.md); "!" after it: the run was stopped at
          BENCH_TEMP_MAX. tcell, trsx the same as numbers (medians in the per-variant line)
stall causes (runs with a stalls.rec: ps3-bench.sh BENCH_STALLS=1, run.sh STALLS=1; stalls.py):
  stall_disk_s, _swap_s (zram + VRAM swap), _reclaim_s (direct reclaim, compaction, GART growth),
  _gpu_s, _cpu_s (running, or runnable without CPU), _io_s (the game's own reads), _wait_s (futex,
          poll, sleep), _other_s (faults without I/O, unknown): the window's stall_s split by
          what the game's main thread and the counters did; stall_why the largest of them
  snd     the sound card state(s) at SOUND_AT seconds; end: how the run ended (exit, end, duration)
profiles (docs/PROFILING.md; shown only for runs that have them):
  perf_*  runs with PERF=1 (after perf-post.sh): percent of the game's samples in the window by
          class: perf_game, _mesa, _drm (libdrm, libdrm_nouveau), _kgpu (ps3rsx and the kernel's
          drm/dma_fence), _kernel (the rest), _libc, _other, _unk (unplaced); perf_xorg and
          perf_kswapd their samples as percent of the game's; perf_n samples; perf_self perf's own CPU
          (% of a thread); perf_sys_* (PERF_ALL=1) shares of both CPUs: game, idle, xorg, kswapd,
          kworker, vswap, other (perfprof.py)
  mem_*   runs with MEM=1: at the window's middle, MiB of Rss and swap by mapping class:
          mem_<game|mesa|libs|heap|anon|stack|gpu|shm|files|other>_rss and _swap, totals mem_rss,
          mem_swap, mem_anon; mem_topswap the 5 mappings with the most swap; mem_grow (end - start,
          Rss + swap); mem_malloc_used/_free and mem_arenas (glibc); mem_gpu_vram/_gart (the
          game's ps3rsx buffers); mem_smaps_ms the longest smaps read (memprof.py)
  --perf, --mem print perfprof.py's and memprof.py's tables for every run that has them
A run with something alongside the game shows it after the variant (base@rec10, base@wksleep10c1,
base@perf: bench.txt harness=, ps3-bench.sh; older runs: rec<ms> from a stalls.rec, perf from
perf.targets), and is grouped apart: the stall recorder alone takes OpenArena's gameplay stalls from
21-29 s to 7 s (docs/STALLS.md "The recorder removes stalls")."""
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


def harness(d, b):
    """what ran alongside the game (ps3-bench.sh's harness= tag in bench.txt: rec<ms>[c<cpus>][nw],
    wk<sleep|kick><ms>[c<cpus>], probe<ms>[c<cpus>], perf, joined by +; then m<presets> for client.sh's
    malloc= (BENCH_MALLOC, docs/MEMORY.md), which changes the game itself, and the Mesa switches of its
    genv= (BENCH_NV30_*, docs/HOTPATHS.md: NV30_TRANSLATE_LUT=1 -> translate_lut1); for runs from before the
    tag: rec<ms> when there is a stalls.rec, perf when perf ran. The recorder changes the stalls it
    records (docs/STALLS.md "The recorder removes stalls"), so such runs are not the same variant"""
    m = ("m" + b["malloc"].replace(",", "_")) if b.get("malloc") else ""   # client.sh BENCH_MALLOC presets
    if b.get("genv"):   # client.sh's Mesa driver switches (BENCH_NV30_*): NV30_TRANSLATE_LUT=1 -> translate_lut1
        g = "_".join(re.sub(r"^NV30_", "", x).replace("=", "").lower() for x in b["genv"].split())
        m = "+".join(x for x in (m, g) if x)
    if "harness" in b:
        return "+".join(x for x in (b["harness"], m) if x)
    t = []
    rec = d / "stalls.rec"
    if rec.exists():
        with rec.open(errors="replace") as f:
            iv = re.search(r"interval_us=(\d+)", f.readline())
        t.append("rec%g" % (int(iv[1]) / 1000) if iv else "rec")
    if (d / "perf.targets").exists():
        t.append("perf")
    if m:
        t.append(m)
    return "+".join(t)


def run(d):
    d = Path(d)
    b = kv(d / "bench.txt")
    label = re.sub(r"^(ps3-bench-)?\d{8}T\d{6}Z-", "", d.name)
    r = {"game": b.get("game") or label, "variant": b.get("variant") or "-", "dir": d.name,
         "snd": b.get("sound") or "-", "end": b.get("ended") or "-"}
    if h := harness(d, b):     # runs with the recorder, a waker or perf are kept apart
        r["variant"] += "@" + h
        r["harness"] = h
    if (v := num(b.get("underruns"))) is not None:
        r["xrun"] = int(v)
    if b.get("temp_max"):   # ps3-bench.sh: cell:71.5 rsx:66.0 (degrees C)
        t = dict(x.split(":", 1) for x in b["temp_max"].split() if ":" in x)
        r["tmax"] = "/".join(t.values()) + ("!" if b.get("temp_stop") == "1" else "")
        for k, c in (("cell", "tcell"), ("rsx", "trsx")):
            if (v := num(t.get(k))) is not None:
                r[c] = v
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
    if (d / "stalls.rec").exists():
        import stalls
        r.update(stalls.summary(d))
    if (d / "perf-leaf.txt").exists():
        import perfprof
        r.update(perfprof.summary(d))
    if (d / "mem").is_dir():
        import memprof
        r.update(memprof.summary(d))
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


def window(b):
    """the window's pieces [(start, end)] in seconds from the client's start, or None"""
    w = [float(x) for x in (b.get("window") or "").split()]
    if not w or len(w) % 2:
        return None
    return list(zip(w[::2], w[1::2]))   # maybe in pieces (WIN_ON_RE/WIN_OFF_RE)


# where a looped demo reloads (docs/STALLS.md): per game, "re" the output line that says the load is
# done (the pass's reload ends "after" s later, default 1), "fixed" else a time after the pass's start,
# in which the frames ending are the reload's (the long load frame ends just after the pass line).
# No entry: nothing to leave out. bench.txt's reload_re / reload_after / reload_fixed win.
RELOADS = {
    "openarena": {"re": r"CL_InitCGame:", "after": 1.0},
    "neverball": {"fixed": 1.0},     # the replay restarts: frames of 0.15-2 s end 0.4 s after the pass line
    "dsda-doom": {"fixed": 1.0},     # G_DoLoadLevel after each timed pass (no data from the PS3 yet)
    "xmoto": {"fixed": 2.0},         # a new StatePreplayingReplay per pass (no data from the PS3 yet)
}


def reload_segments(d, b):
    """the reload segments [(start, end)] of run directory d (bench.txt b), seconds from the client's
    start: [] when the game has none in its window, None when they can't be told (the load-done
    line needs t0 and out.log)"""
    cfg = dict(RELOADS.get(b.get("game") or "", {}))
    if b.get("reload_re"):
        cfg["re"] = b["reload_re"]
    if (v := num(b.get("reload_after"))) is not None:
        cfg["after"] = v
    if (v := num(b.get("reload_fixed"))) is not None:
        cfg["fixed"] = v
    passes = []
    for k, v in b.items():
        f = v.split()
        if re.fullmatch(r"pass\d+", k) and len(f) >= 2 and num(f[0]) is not None and num(f[1]) is not None:
            passes.append((float(f[0]), float(f[1])))
    passes.sort()
    if not cfg or not passes:
        return []
    if "re" in cfg:
        t0, out = num(b.get("t0")), Path(d) / "out.log"
        if t0 is None or not out.exists():
            return None
        rx = re.compile(cfg["re"])
        loads = []
        for line in out.read_text(errors="replace").splitlines():
            stamp, _, text = line.partition(" ")
            if rx.search(text):
                try:
                    loads.append(float(stamp) - t0)
                except ValueError:
                    pass
        segs = []
        for s, e in passes:
            ld = next((t for t in loads if s <= t < e), None)
            if ld is not None:
                segs.append((s, min(e, ld + cfg.get("after", 1.0))))
        return segs
    return [(s, min(e, s + cfg["fixed"])) for s, e in passes]


def frame_ends(ft, b):
    """when each HUD frame ended, seconds from the client's start: frame i ends at t1 + the frame
    times after the first (t1: when the dump got its first value)"""
    t1, secs = num(b.get("hud_t1")), num(b.get("seconds"))
    ends, t = [], (t1 if t1 is not None else (secs or 0) - sum(ft[1:]) / 1000)
    for i, v in enumerate(ft):
        if i:
            t += v / 1000
        ends.append(t)
    return ends


def frame_stats(ms):
    """(1% low, frames over STALL_MS, seconds in them, longest ms) of the sorted frame times ms"""
    return (round(1000 / ms[max(0, -(-len(ms) * 99 // 100) - 1)], 1), sum(1 for v in ms if v > STALL_MS),
            round(sum(v for v in ms if v > STALL_MS) / 1000, 1), round(ms[-1], 1))


def play(d, b, r, segs, inw):
    """the gameplay-only stall metrics (*_play) of the window's frames inw [(end, ms)] into r"""
    rel = reload_segments(d, b)
    if rel is None:
        return
    keep = sorted(v for e, v in inw if not any(s < e <= t for s, t in rel))
    if not keep:
        return
    r["low1_play"], r["stalls_play"], r["stall_play_s"], r["longest_play"] = frame_stats(keep)
    cut = sum(max(0, min(t, wt) - max(s, ws)) for s, t in rel for ws, wt in segs)
    r["reload_n"] = len(rel)
    r["reload_s"] = round(float(cut), 1)
    r["win_play"] = round(sum(t - s for s, t in segs) - cut, 1)


def steady(d, b, r):
    """the steady-state metrics of run directory d (bench.txt b) into r"""
    segs = window(b)
    if not segs:
        return

    def inside(e):
        return any(s < e <= t for s, t in segs)
    t1 = num(b.get("hud_t1"))
    secs = num(b.get("seconds"))
    ft_path, fps_path = d / "hud" / "frametime_(ms)", d / "hud" / "fps"
    if ft_path.exists() and (ft := floats(ft_path)):
        ends = frame_ends(ft, b)
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
            short = [v for v in ms if v <= STALL_MS]
            if short:
                r["smooth"] = round(1000 * len(short) / sum(short), 1)
            r["stall_s"] = round(sum(v for v in ms if v > STALL_MS) / 1000, 1)
            r["longest"] = round(ms[-1], 1)
            r["win"] = round(sum(min(t, inw[-1][0]) - s for s, t in segs if s < inw[-1][0]), 1)
            r["src"] = "hud"
            play(d, b, r, segs, inw)
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


COLS = ["load", "fps", "maxms", "hud", "steady", "src", "low1", "stalls", "smooth", "stall_s", "longest", "stalls_play", "stall_play_s", "longest_play", "low1_play", "win_play",
        "reload_s", "win", "passes",
        "ftdrift", "cpu", "rss", "vram", "gart", "avail", "so_load", "so_all",
        "vsw_peak", "vsw_wb", "xrun", "tmax", "tcell", "trsx", "snd", "end", "stall_disk_s", "stall_swap_s", "stall_reclaim_s",
        "stall_gpu_s", "stall_cpu_s", "stall_io_s", "stall_wait_s", "stall_other_s", "stall_why"]
# the profiles' columns: printed only for the runs that have them
PROF_COLS = (["perf_n", "perf_game", "perf_mesa", "perf_drm", "perf_kgpu", "perf_kernel", "perf_libc", "perf_other",
              "perf_unk", "perf_xorg", "perf_kswapd", "perf_self"]
             + ["perf_sys_" + k for k in ("game", "idle", "xorg", "kswapd", "kworker", "vswap", "other")]
             + ["mem_rss", "mem_swap", "mem_anon"]
             + ["mem_%s_%s" % (c, k) for c in ("game", "mesa", "libs", "heap", "anon", "stack", "gpu", "shm",
                                              "files", "other") for k in ("rss", "swap")]
             + ["mem_grow", "mem_malloc_used", "mem_malloc_free", "mem_arenas", "mem_gpu_vram", "mem_gpu_gart",
                "mem_smaps_ms", "mem_topswap"])
COLS += PROF_COLS
RANGED = ("load", "fps", "maxms", "hud_med", "steady", "low1", "smooth", "stall_s", "stall_play_s", "low1_play")


def show(r):
    return "  ".join("%s=%s" % (c, r.get(c, "-")) for c in COLS if c not in PROF_COLS or c in r)


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
    args = sys.argv[1:]
    detail = "--stalls" in args     # stalls.py's tables for every run with a record, after the summary
    pdetail, mdetail = "--perf" in args, "--mem" in args
    args = [a for a in args if a not in ("--stalls", "--perf", "--mem")]
    main(args)
    if detail:
        import stalls
        for d in args:
            if (Path(d) / "stalls.rec").exists():
                print()
                stalls.show(d)
    if pdetail:
        import perfprof
        for d in args:
            if (Path(d) / "perf-leaf.txt").exists():
                print()
                perfprof.show(d)
    if mdetail:
        import memprof
        for d in args:
            if (Path(d) / "mem").is_dir():
                print()
                memprof.show(d)
