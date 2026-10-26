#!/usr/bin/env python3
"""report.py [--stalls] [--perf] [--mem] [--load] [--include-interfered] <run dir>...: one line per bench run (ps3-bench.sh run directories with the
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
          line plus 1 s (re + after: gamedb:openarena, needs t0 and out.log) or a fixed time after the
          pass's start (fixed: gamedb:neverball, gamedb:dsda-doom, gamedb:xmoto); each game's rule is
          the [reload] of its benchmark/analysis.toml (bench/gamedata.py), and a recipe's parse() may
          print reload_re / reload_after / reload_fixed to bench.txt to set its own. Games without a
          reload in the window (gamedb:bananabread's WIN_ON/OFF pieces already leave it out,
          gamedb:supertuxkart's one race) get the plain numbers; src hud only
  *_warm  the first minute: stalls_warm, stall_warm_s, longest_warm over the WARMUP seconds after the
          load (bench.txt load_end, warmup), which the window leaves out; HUD frame times only
  win     seconds measured; ftdrift: run time minus the end of the last HUD frame (s: large when the
          frame times are misplaced against the clock, e.g. a renderer restart's gap)
  cpu     the client's user+sys seconds (the game and its helpers)
  rss     the game's peak RSS (VmHWM, MiB)
  vram    the game's peak VRAM (KiB, ps3rsx per-client, all its buffers)
  gart    peak "GART backed by memory" (KiB: RAM held for GPU staging)
  avail   lowest MemAvailable (KiB)
  so      pages swapped out until the load ended / during the whole run
  vsw     VRAM swap: peak pages in VRAM, pages written back to the disk (shrinks)
  xrun    ALSA underruns (the sound buffer ran dry; counted, not necessarily heard)
  xaud    the dropouts ps3-xrunlog measured as audible: 20 ms or more dry with sound on a side
          (bench.txt xruns_audible=, docs/games/SUITE.md "Sound dropouts")
  wedge   lines of OpenAL's wedged mixer ("Wait timeout", "mmap commit error": bench.txt oal_wedge=)
  sstall  seconds the sound card's hw pointer stood still while RUNNING (its DMA stopped: bench.txt
          snd_stall=, LOG-FINDINGS.md 1), "rN" after it: snd_ps3's watchdog restarted it N times
  dump    DUMPON=1 runs: what caught the failure dump (trigger, crash, trigger+crash, none) @ seconds
          into the run (bench.txt dump=, dump_at=; bench/dumpana.sh analyses it)
  tmax    the hottest temperature over the run per zone, degrees C, Cell/RSX (ps3-bench.sh temp_max=,
          from the kernel's ps3_sm sensors, docs/THERMAL.md); "!" after it: the run was stopped at
          BENCH_TEMP_MAX. tcell, trsx the same as numbers (medians in the per-variant line)
stall causes (runs with a stalls.rec: ps3-bench.sh BENCH_STALLS=1, run.sh STALLS=1; stalls.py):
  stall_disk_s, _swap_s (zram + VRAM swap), _reclaim_s (direct reclaim, compaction, GART growth),
  _gpu_s, _cpu_s (running, or runnable without CPU), _io_s (the game's own reads), _wait_s (futex,
          poll, sleep), _other_s (faults without I/O, unknown): the window's stall_s split by
          what the game's main thread and the counters did; stall_why the largest of them;
          load_disk_s the load's seconds waiting on the disk (client start to load_end, all of it,
          not only stall frames), warm_disk_s the warm-up's stall frames' disk seconds,
          warmup_disk_s the warm-up's (load_end to the window's start) seconds waiting on the disk
  snd     the sound card state(s) at SOUND_AT seconds; end: how the run ended (exit, end, duration)
  intf    1: somebody disturbed the run (ps3-bench.sh's detector: bench.txt interfered=1, or the
          retro-scan's list results/interfered.txt for runs from before it; docs/STALLS.md
          "Interference"), 0: watched and clean, -: not watched. An interfered run is marked
          !INTERFERED, left out of the medians (--include-interfered keeps it) and listed at the end
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
  sysmem_* runs with ENVS token NV30_SYSMEM_STATS=N (Mesa 0037): MiB of nouveau's system memory
          copies of GPU buffers (sysmem_mb the peak; _create_mb, _read_mb how they were made),
          sysmem_readbacks, sysmem_hint the frame the screen-wide copy hint was set (0: never)
  fdxt_*  runs with ENVS token NV30_FORCE_DXT=opaque|all+NV30_FORCE_DXT_STATS=N (Mesa 0038, texture
          compression at upload): fdxt_tex textures compressed (fdxt_dxt5 of them DXT5), fdxt_raw_mb and
          fdxt_dxt_mb their levels' MiB uncompressed and as stored (uploads, not what is resident),
          fdxt_unforced textures turned back to uncompressed, fdxt_sub sub-image updates recompressed,
          fdxt_mipgen mipmap generations of compressed textures and their CPU ms (fdxt_mipgen_ms),
          fdxt_scan_ms the alpha scans, fdxt_skip the textures kept by reason (small, npot, alpha, ...)
  shc_*   runs with NV30_SHADER_CACHE_STATS=1 (Mesa 0039, nv30's programs in the disk cache; with
          NV30_SHADER_CACHE=on or off): hits and misses of nir_to_tgsi (shc_tgsi_*), the fragment and
          vertex program translations (shc_fp_*, shc_vp_*), shc_hit_ms and shc_miss_ms the time each
          side took (all three; with the cache off every creation is a miss)
  lp_*    runs with PERF_LOAD=1 (the start-up span: exec, loading screens, warm-up; loadprof.py,
          docs/PROFILING.md "Start-up profiling"): lp_cpu, lp_main the load's CPU seconds (all the
          game's threads, its main thread), lp_wcpu the warm-up's; seconds of CPU over the whole span
          (all threads) in lp_link (ld.so, dlopen), lp_glsl (the GL front end's compiles and links),
          lp_shbe (nv30's back end: variants, nir_to_tgsi, nvfx), lp_shcache (the disk cache itself),
          lp_wshader (the three in the warm-up alone), lp_tex (Mesa's texture uploads), lp_mip
          (mipmap generation), lp_dxt, lp_decomp (inflate, PNG, JPEG, audio and other decoders);
          lp_mb, lp_wmb MB read from the disks in the load and the warm-up, lp_majflt the game's major
          faults in the load, lp_ld ld.so's start-up seconds (LD_DEBUG=statistics), lp_disk the main
          thread's seconds waiting for the disk in the load (with STALLS=1)
screenshots (client.sh BENCH_SHOTS, run.sh SHOTS=; shots.py): shot_n captures taken, shot_ms the slowest
          capture (ms), shot_win the captures inside the measured window (the run is tagged @shot and kept
          apart then), shot_frame the longest frame a capture touched (ms; shots.py cost has the frames
          before), review the verdict in the run's REVIEW.md (shots.py pending lists the sheets without one)
  --perf, --mem, --load print perfprof.py's, memprof.py's and loadprof.py's tables for every run that has them
A run with something alongside the game shows it after the variant (base@rec10, base@wksleep10c1,
base@perf: bench.txt harness=, ps3-bench.sh; older runs: rec<ms> from a stalls.rec, perf from
perf.targets), and is grouped apart: the stall recorder alone takes gamedb:openarena's gameplay stalls
from 21-29 s to 7 s (docs/STALLS.md "The recorder removes stalls").
Runs are grouped by their game's id (bench/gamedata.py run_id): a run from before the recipes moved into
gamedb names the old recipe in game=, which [legacy] recipe_names reads (game=sauerbraten then was
gamedb:bananabread); a run without a bench.txt by its label."""
import os
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gamedata  # noqa: E402  (each game's analysis rules: gamedb/games/<id>/benchmark/analysis.toml)

INCLUDE_INTERFERED = False


def interfered_list():
    """results/interfered.txt (BENCH_RESULTS: elsewhere): run directory name, then why; the runs the
    retro-scan (interference-scan.py) found disturbed before the detector existed"""
    f = Path(os.environ.get("BENCH_RESULTS") or Path(__file__).resolve().parent / "results") / "interfered.txt"
    out = {}
    if f.exists():
        for line in f.read_text(errors="replace").splitlines():
            if line.strip() and not line.startswith("#"):
                name, _, why = line.strip().partition(" ")
                out[name] = why.strip()
    return out


RETRO = interfered_list()


def interference(d, b, r):
    """r["intf"]: 1 disturbed, 0 watched and clean; absent: not watched; r["intf_why"] the summary"""
    if b.get("interfered") in ("0", "1"):
        r["intf"] = int(b["interfered"])
        if r["intf"]:
            r["intf_why"] = b.get("interference") or "?"
    if d.name in RETRO:
        r["intf"] = 1
        r["intf_why"] = "results/interfered.txt: " + RETRO[d.name]


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
    tag: rec<ms> when there is a stalls.rec, perf when perf ran; gpf<mode> for client.sh's game prefetch
    (gprefetch=, BENCH_GAMEPREFETCH, docs/STALLS.md "Game prefetch"); gdssd and swssd for the SSD arms
    (gamedata=, swap=: docs/GAMEOS-HDD.md "Test drive, Phase A"); oal<settings> for client.sh's OpenAL token
    (oal=, BENCH_OAL) and fx<mode>, zng, bindnow, jsimd<mode> for its library switches (libenv=). The recorder changes the stalls it
    records (docs/STALLS.md "The recorder removes stalls"), so such runs are not the same variant"""
    m = ("m" + b["malloc"].replace(",", "_")) if b.get("malloc") else ""   # client.sh BENCH_MALLOC presets
    if b.get("genv"):   # client.sh's Mesa driver switches (BENCH_NV30_*): NV30_TRANSLATE_LUT=1 -> translate_lut1
        g = "_".join(re.sub(r"^NV30_", "", x).replace("=", "").lower() for x in b["genv"].split())
        m = "+".join(x for x in (m, g) if x)
    if b.get("kparam"):   # client.sh's ps3rsx module parameters (BENCH_PS3RSX_*): shadow_fast=1 -> rsx_shadow_fast1
        # (BENCH_PS3HTAB_*, the kernel's ps3_htab: htab_insert_readback=0 -> htab_insert_readback0;
        # BENCH_SNDPS3_*, snd_ps3: snd_dma_watchdog_ms=0 -> snd_dma_watchdog_ms0)
        k = "_".join(("" if x.startswith(("htab_", "snd_")) else "rsx_") + x.replace("=", "").lower()
                     for x in b["kparam"].split())
        m = "+".join(x for x in (m, k) if x)
    if b.get("gprefetch"):   # client.sh BENCH_GAMEPREFETCH: the game's files read at its start
        m = "+".join(x for x in (m, "gpf" + b["gprefetch"]) if x)
    if b.get("shadercache"):   # client.sh BENCH_SHADERCACHE: Mesa's disk shader cache cold, warm, prime, off
        m = "+".join(x for x in (m, "sc" + b["shadercache"]) if x)
    for k, t in (("gamedata", "gd"), ("swap", "sw")):   # client.sh's SSD arms (docs/GAMEOS-HDD.md, Phase A)
        if b.get(k) and b[k] != "usb":
            m = "+".join(x for x in (m, t + b[k]) if x)
    if b.get("oal"):   # client.sh BENCH_OAL: OpenAL Soft's settings (openal.inc's names): b16,lin -> oalb16_lin
        m = "+".join(x for x in (m, "oal" + b["oal"].replace(",", "_")) if x)
    for x in (b.get("libenv") or "").split():   # client.sh's library switches: FLUIDFX=off -> fxoff, ZLIB=ng -> zng,
        n, _, v = x.partition("=")              # BINDNOW=1 -> bindnow, JSIMD=none -> jsimdnone
        t = {"FLUIDFX": "fx" + v, "ZLIB": "z" + v, "BINDNOW": "bindnow", "JSIMD": "jsimd" + v,
             "LDPATH": "ld" + v.rstrip("/").rsplit("/", 1)[-1]}.get(n, x.lower())   # LDPATH=/x/stock-openal -> ldstock-openal
        m = "+".join(y for y in (m, t) if y)
    if "shots" in b:   # client.sh's screenshots: one in the window costs frames
        import shots
        if shots.in_window(shots.parse(d)):
            m = "+".join(x for x in (m, "shot") if x)
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
    # game: as the run named it (matrix.py's columns); group: its game's id, which the summary goes by
    r = {"game": b.get("game") or label, "variant": b.get("variant") or "-", "dir": d.name,
         "snd": b.get("sound") or "-", "end": b.get("ended") or "-"}
    r["group"] = gamedata.run_id(b) or r["game"]
    if h := harness(d, b):     # runs with the recorder, a waker or perf are kept apart
        r["variant"] += "@" + h
        r["harness"] = h
    interference(d, b, r)
    if (v := num(b.get("underruns"))) is not None:
        r["xrun"] = int(v)
    if b.get("xrunlog") == "on" and (v := num(b.get("xruns_audible"))) is not None:
        r["xaud"] = int(v)
    if (v := num(b.get("oal_wedge"))) is not None:
        r["wedge"] = int(v)
    if (v := num(b.get("snd_stall"))) is not None:
        rs = num(b.get("snd_restarts"))
        r["sstall"] = str(int(v)) + (f"r{int(rs)}" if rs else "")
    if b.get("dump"):     # DUMPON=1 runs: trigger|crash|trigger+crash|none, and when (s)
        r["dump"] = b["dump"] + ("@" + b["dump_at"] if b.get("dump_at") else "")
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
        try:    # a line cut by a full /dev/shm runs into the next one (20261020T005718Z): skip it
            t, a, so = float(t), int(a), int(so)
            int(g or 0), int(v_in or 0), int(wb or 0)
        except ValueError:
            continue
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
    if (d / "perf-load-stacks.tsv").exists() or (d / "load-io").is_dir():
        import loadprof
        r.update(loadprof.summary(d))
    import shots
    r.update(shots.summary(d))   # the screenshots: shot_n, shot_ms, shot_win, shot_frame; REVIEW.md's verdict
    r.update(sysmem(d))
    r.update(forcedxt(d))
    r.update(shcache(d))
    return r


def mesa_lines(d, prefix, need):
    """the key=value pairs of the last line in game.log starting with prefix and holding need"""
    log = d / "game.log"
    if not log.exists():
        return None
    last = None
    for line in log.read_text(errors="replace").splitlines():
        if line.startswith(prefix) and need in line:
            last = dict(x.split("=", 1) for x in line.split() if "=" in x)
    return last


def forcedxt(d):
    """Mesa's NV30_FORCE_DXT_STATS lines in game.log (docs/MEMORY.md "Texture compression at upload"):
    the last one's counts (they are totals since the game started)"""
    kv = mesa_lines(d, "nv30-forcedxt: ", "kb_raw=")
    if kv is None:
        return {}
    n = lambda k: int(kv.get(k, 0))
    skips = sorted(((int(v), k[5:]) for k, v in kv.items() if k.startswith("skip_") and int(v)), reverse=True)
    return {"fdxt_tex": n("tex"), "fdxt_dxt5": n("dxt5"), "fdxt_raw_mb": round(n("kb_raw") / 1024, 1),
            "fdxt_dxt_mb": round(n("kb_dxt") / 1024, 1), "fdxt_unforced": n("unforced"), "fdxt_sub": n("sub"),
            "fdxt_mipgen": n("mipgen"), "fdxt_mipgen_ms": n("mipgen_ms"), "fdxt_scan_ms": n("scan_ms"),
            "fdxt_skip": ",".join("%s%d" % (k, v) for v, k in skips) or "-"}


def shcache(d):
    """Mesa's NV30_SHADER_CACHE_STATS lines in game.log (docs/PROFILING.md "nv30's shader cache"): the last"""
    kv = mesa_lines(d, "nv30-shcache: ", "tgsi_hit=")
    if kv is None:
        return {}
    n = lambda k: int(kv.get(k, 0))
    r = {"shc_" + k: n(k) for k in ("tgsi_hit", "tgsi_miss", "fp_hit", "fp_miss", "vp_hit", "vp_miss")}
    r["shc_hit_ms"] = sum(n(s + "_hit_ms") for s in ("tgsi", "fp", "vp"))
    r["shc_miss_ms"] = sum(n(s + "_miss_ms") for s in ("tgsi", "fp", "vp"))
    return r


def sysmem(d):
    """Mesa's NV30_SYSMEM_STATS lines in game.log (docs/MEMORY.md "nouveau's system memory copies"):
    sysmem_mb the most MiB in copies of GPU buffers at once, and from the last line sysmem_create_mb,
    sysmem_read_mb (MiB of copies made at creation for the hint / at a CPU read), sysmem_readbacks
    (GPU readbacks to bring a copy up to date) and sysmem_hint (the frame the hint was set, 0: never)"""
    log = d / "game.log"
    if not log.exists():
        return {}
    last, peak, hint = None, 0, 0
    for line in log.read_text(errors="replace").splitlines():
        if not line.startswith("nv30-sysmem: "):
            continue
        kv = dict(x.split("=", 1) for x in line.split() if "=" in x)
        last = kv
        peak = max(peak, int(kv.get("kb", 0)))
        if line.startswith("nv30-sysmem: hint set") and not hint:
            hint = int(kv.get("frame", 0)) or 1
    if last is None:
        return {}
    return {"sysmem_mb": round(peak / 1024, 1), "sysmem_create_mb": round(int(last.get("kb_create", 0)) / 1024, 1),
            "sysmem_read_mb": round(int(last.get("kb_read", 0)) / 1024, 1),
            "sysmem_readbacks": int(last.get("readbacks", 0)), "sysmem_hint": hint}


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


# where a looped demo reloads (docs/STALLS.md): each game's [reload] (benchmark/analysis.toml,
# bench/gamedata.py), "re" the output line that says the load is done (the pass's reload ends "after" s
# later, default 1), "fixed" else a time after the pass's start, in which the frames ending are the
# reload's (the long load frame ends just after the pass line). None: nothing to leave out. bench.txt's
# reload_re / reload_after / reload_fixed win.
def reload_segments(d, b):
    """the reload segments [(start, end)] of run directory d (bench.txt b), seconds from the client's
    start: [] when the game has none in its window, None when they can't be told (the load-done
    line needs t0 and out.log)"""
    cfg = gamedata.reload(gamedata.run_id(b))
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
    # a frame that starts inside a segment is the reload's too, and the segment ends with it: the
    # load's last frame can outlast the cut (gamedb:openarena opengl2: the cgame load, cl_preloadLate and
    # RB_ShowImages in one 8-16 s frame that ends 4-5 s after "CL_InitCGame:" + 1 s)
    rel = [(s, max([t] + [e for e, v in inw if s < e - v / 1000 < t])) for s, t in rel]
    keep = sorted(v for e, v in inw if not any(s < e <= t for s, t in rel))
    if not keep:
        return
    r["low1_play"], r["stalls_play"], r["stall_play_s"], r["longest_play"] = frame_stats(keep)
    cut = sum(max(0, min(t, wt) - max(s, ws)) for s, t in rel for ws, wt in segs)
    r["reload_n"] = len(rel)
    r["reload_s"] = round(float(cut), 1)
    r["win_play"] = round(sum(t - s for s, t in segs) - cut, 1)


def startup_end(b):
    """where the start-up span ends (seconds from the client's start): the start-up profile's end
    (client.sh perf_load_win, PERF_LOAD=1: where the window's profile would start), else the window's
    first piece, else load_end + WARMUP; None without a load end"""
    le = num(b.get("load_end"))
    if le is None:
        return None
    w = [num(x) for x in (b.get("perf_load_win") or "").split()]
    if len(w) >= 2 and w[1] is not None and w[1] >= le:
        return w[1]
    segs = window(b)
    if segs and segs[0][0] >= le:
        return segs[0][0]
    wu = num(b.get("warmup"))
    return le + wu if wu else None


def warm_segment(b):
    """(start, end) of the warm-up: the load's end (bench.txt load_end) and WARMUP s after, or None"""
    le, w = num(b.get("load_end")), num(b.get("warmup"))
    if le is None or not w:
        return None
    return le, le + w


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
    if ft_path.exists() and (ft := floats(ft_path)) and (wseg := warm_segment(b)):
        # the first minute: the WARMUP seconds after the load, which the window leaves out (where a
        # prefetch of the game's files shows, docs/STALLS.md "Game prefetch")
        ws, we = wseg
        warm = sorted(v for e, v in zip(frame_ends(ft, b), ft) if ws < e <= we)
        if warm:
            r["stalls_warm"] = sum(1 for v in warm if v > STALL_MS)
            r["stall_warm_s"] = round(sum(v for v in warm if v > STALL_MS) / 1000, 1)
            r["longest_warm"] = round(warm[-1], 1)
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
        "reload_s", "win", "passes", "stalls_warm", "stall_warm_s", "longest_warm",
        "ftdrift", "cpu", "rss", "vram", "gart", "avail", "so_load", "so_all",
        "vsw_peak", "vsw_wb", "xrun", "xaud", "wedge", "sstall", "dump", "tmax", "tcell", "trsx", "snd", "end", "intf", "stall_disk_s", "stall_swap_s", "stall_reclaim_s",
        "stall_gpu_s", "stall_cpu_s", "stall_io_s", "stall_wait_s", "stall_other_s", "stall_why",
        "load_disk_s", "warm_disk_s", "warmup_disk_s"]
# the profiles' columns: printed only for the runs that have them
PROF_COLS = (["perf_n", "perf_game", "perf_mesa", "perf_drm", "perf_kgpu", "perf_kernel", "perf_libc", "perf_other",
              "perf_unk", "perf_xorg", "perf_kswapd", "perf_self"]
             + ["perf_sys_" + k for k in ("game", "idle", "xorg", "kswapd", "kworker", "vswap", "other")]
             + ["mem_rss", "mem_swap", "mem_anon"]
             + ["mem_%s_%s" % (c, k) for c in ("game", "mesa", "libs", "heap", "anon", "stack", "gpu", "shm",
                                              "files", "other") for k in ("rss", "swap")]
             + ["mem_grow", "mem_malloc_used", "mem_malloc_free", "mem_arenas", "mem_gpu_vram", "mem_gpu_gart",
                "mem_smaps_ms", "mem_topswap"]
             + ["sysmem_mb", "sysmem_create_mb", "sysmem_read_mb", "sysmem_readbacks", "sysmem_hint"]
             + ["fdxt_tex", "fdxt_dxt5", "fdxt_raw_mb", "fdxt_dxt_mb", "fdxt_unforced", "fdxt_sub", "fdxt_mipgen",
                "fdxt_mipgen_ms", "fdxt_scan_ms", "fdxt_skip"]
             + ["shc_tgsi_hit", "shc_tgsi_miss", "shc_fp_hit", "shc_fp_miss", "shc_vp_hit", "shc_vp_miss",
                "shc_hit_ms", "shc_miss_ms"]
             + ["lp_cpu", "lp_main", "lp_wcpu", "lp_link", "lp_glsl", "lp_shbe", "lp_shcache", "lp_wshader", "lp_tex",
                "lp_mip", "lp_dxt", "lp_decomp", "lp_mb", "lp_wmb", "lp_majflt", "lp_ld", "lp_disk"]
             + ["shot_n", "shot_ms", "shot_win", "shot_frame", "review"])
COLS += PROF_COLS
RANGED = ("load", "fps", "maxms", "hud_med", "steady", "low1", "smooth", "stall_s", "stall_play_s", "low1_play",
          "stall_warm_s")


def show(r):
    return "  ".join("%s=%s" % (c, r.get(c, "-")) for c in COLS if c not in PROF_COLS or c in r)


def bad(r):
    """left out of the medians: disturbed (unless --include-interfered)"""
    return r.get("intf") == 1 and not INCLUDE_INTERFERED


def main(dirs):
    allruns = [run(d) for d in dirs]
    for r in allruns:
        print("%-24s %s%s" % ("%s-%s" % (r["group"], r["variant"]), "!INTERFERED " if r.get("intf") == 1 else "", show(r)))
    print()
    runs = [r for r in allruns if not bad(r)]
    med = {}
    keys = list(dict.fromkeys((r["group"], r["variant"]) for r in runs))
    for key in keys:
        rs = [r for r in runs if (r["group"], r["variant"]) == key]
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
        shown.pop("intf", None)
        left = len([r for r in allruns if (r["group"], r["variant"]) == key and bad(r)])
        print("%-24s n=%d%s %s" % ("%s-%s" % key, len(rs), " (+%d interfered left out)" % left if left else "", show(shown)))

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
    intf = [r for r in allruns if r.get("intf") == 1]
    if intf:
        print("\n!! %d interfered run(s)%s (docs/STALLS.md \"Interference\"):" % (
            len(intf), ", kept in the medians (--include-interfered)" if INCLUDE_INTERFERED else ", not in the medians"))
        for r in intf:
            print("!!   %s: %s" % (r["dir"], r.get("intf_why", "?")))


if __name__ == "__main__":
    args = sys.argv[1:]
    detail = "--stalls" in args     # stalls.py's tables for every run with a record, after the summary
    pdetail, mdetail, ldetail = "--perf" in args, "--mem" in args, "--load" in args
    INCLUDE_INTERFERED = "--include-interfered" in args
    args = [a for a in args if a not in ("--stalls", "--perf", "--mem", "--load", "--include-interfered")]
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
    if ldetail:
        import loadprof
        for d in args:
            if (Path(d) / "perf-load-stacks.tsv").exists() or (Path(d) / "load-io").is_dir():
                print()
                loadprof.show(d)
