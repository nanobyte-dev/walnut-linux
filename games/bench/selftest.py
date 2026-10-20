#!/usr/bin/env python3
"""selftest.py: the steady-state benchmark harness checked without the PS3 (python3 games/bench/selftest.py).
  1. report.py on made-up run directories (HUD frame times, per-second fps only, repeated passes,
     a game's own window, a run from before the steady state): the window's median, 1% low,
     stalls and longest frame come out as computed by hand
  2. matrix.py into a scratch results directory (BENCH_RESULTS): the new metric files get the rows
     of the existing fps.csv and the new row's cells
  3. client.sh end to end with made-up recipes and a fake game (this file with --fake-game: it
     writes the HUD's dump files as Mesa does and prints its lines), WARMUP and MEASURE in seconds:
     the window stop, passes counted and the stop once they cover MEASURE, a game's own steady
     line, a line split by the client's 0.25 s HUD polling; BENCH_GAMEPREFETCH learn then fetch with
     ps3-desktop's ps3-game-prefetch (a list left, read by the next run, report.py's gpf tag and
     its warm-up columns); the SSD arms (BENCH_GAMEDATA, BENCH_SWAP: checks, bench.txt, report.py's
     tags) and ssd-arm.sh's root commands in its dry mode
  4. every recipe and variant sourced as client.sh does: CMD set, and the durations against
     load + WARMUP + MEASURE (a variant that can't hold a window is listed, not an error)
  5. stalls.py on a made-up stalls.rec: one stall per cause the host can't make (GPU fence wait,
     zram and VRAM swap-ins, direct reclaim, GART growth, runnable without CPU) classified as such
  6. ps3-stallrec end to end (built with the host's cc into the scratch directory) on a fake game
     that stalls three known ways: major faults on a file dropped from the page cache
     (POSIX_FADV_DONTNEED, MADV_RANDOM, on a disk-backed directory), a busy loop, a futex wait;
     each stall must come out as disk, cpu and wait, and the recorder's own CPU is printed
  7. perfprof.py on a made-up perf script output (call chains, and the one-line form of the
     system-wide record): folded stacks, the class shares of report.py's perf_* columns by hand;
     frames perf left [unknown] filled from a MEM=1 smaps and a library built here (its symbols)
     irqoff.py on made-up stacks and irq/ snapshots: replay samples by where they ended, the rates
  8. client.sh with PERF and MEM: a fake perf on the control fifos (enable/disable at the window's
     ends, acknowledged), ps3-mallocstat.so (built with the host's cc) preloaded into the fake game,
     the snapshots at the window's start, middle and end, memprof.py's columns from them
     with BENCH_MALLOC=arena2,trim: ps3-malloctrim.so preloaded next to it (both threads in the
     game, its log lines in game.log), malloc= in bench.txt and report.py's variant tag
  9. memprof.py on a real process (a C program, built here) whose thread stacks and malloc arenas
     are known from inside it: every one classified as such
 10. ps3-malloctrim.so on a real process (built here) that leaves holes in its main heap and in a
     thread's arena: the trims give them back (VmRSS before and after, from its own log lines)
 11. the interference detector (intfcheck.py, docs/STALLS.md "Interference"): ps3-intfwatch's input
     filter on recorded event streams in both byte orders, its process, session, VT and kernel log
     rules live on this machine, report.py's intf column and matrix.py's skip, interference-scan.py
 12. the start-up profile (docs/PROFILING.md "Start-up profiling"): perfprof.py collapse --load and
     loadprof.py on made-up stacks (one per group, the load and the warm-up), I/O snapshots, ld.so
     statistics and a stalls.rec, every number by hand, report.py's lp_* columns and --load;
     client.sh with BENCH_PERF_LOAD and a fake perf that acknowledges late: the game held at the gate
     until then, perf off at the window's start, the counters, LD_DEBUG's files; BENCH_SHADERCACHE
     cold, warm (prime first) and off
 13. the failure dump (client.sh BENCH_DUMPON, docs/games/LOG-FINDINGS.md "Catching failures"): a fake
     game printing OpenAL's "Wait timeout" line twice, a fake gdb and sudo on PATH: one capture into
     dump/ (trigger, proc files, threads, a compressed core), dump=trigger, dump_at=, oal_wedge=2,
     the game let go after it; DUMPON off: no dump dir and no dump= line; a game dying of SIGSEGV
 14. the screenshots (client.sh BENCH_SHOTS, docs/games/SUITE.md "Screenshots"), a fake xwd on PATH: the
     shot points after the load's end, after a recipe's SHOT_RE line, after during()'s shot_mark and
     the load's end when during() ends without one; a point past the run's end skipped; the capture
     on the disk (BENCH_SHOT_ROOT) when the run directory is in /dev/shm; off: no shot lines; shots.py's
     parse and frame counts, report.py's @shot tag and columns; with ImageMagick: convert, the sheet
     (1608 px wide, BLACK flagged, the result's shotsheet: line), pending and REVIEW.md's verdict
Exit status 0 when every check passes; scratch files in a temporary directory, removed at the end."""
import os
import re
import shutil
from collections import Counter
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import report  # noqa: E402

FAILS = []


def check(what, cond, got=None):
    print("%s  %s%s" % ("ok  " if cond else "FAIL", what, "" if cond else "  (got %r)" % (got,)))
    if not cond:
        FAILS.append(what)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def bench(d, **kv):
    write(d / "bench.txt", "".join("%s=%s\n" % (k, v) for k, v in kv.items()))


# --- 1. report.py on made-up runs ------------------------------------------------------------
def synthetic(tmp):
    # a HUD game: 40 ms frames until 70 s (warm-up), then 20 ms frames, with a 300 ms and a 150 ms
    # stall in the window and a 2 s stall in the warm-up (not counted)
    d = tmp / "ps3-bench-20261018T000000Z-fake-base"
    ft, t, stalls = [], 5.0, {30: 2000.0, 100: 300.0, 150: 150.0}
    while t < 200:
        v = 40.0 if t < 70 else 20.0
        for at in [a for a in stalls if t >= a]:
            v = stalls.pop(at)
        ft.append(v)
        t += v / 1000
    write(d / "hud" / "frametime_(ms)", "".join("%g\n" % v for v in ft))
    write(d / "hud" / "fps", "".join("%d\n" % (25 if i < 65 else 50) for i in range(195)))
    bench(d, game="fake", variant="base", ended="window", seconds="%.2f" % t, load="8.0",
          hud_skip=12, warmup=60, measure=120, load_end="10.00", hud_t1="5.00", window="70.00 190.00")
    r = report.run(d)
    check("hud: window median 50 fps", r.get("hud_win") == 50, r.get("hud_win"))
    check("hud: steady from the HUD", r.get("steady") == 50 and r.get("src") == "hud", (r.get("steady"), r.get("src")))
    check("hud: 1% low 50.0 (p99 frame 20 ms)", r.get("low1") == 50.0, r.get("low1"))
    check("hud: 2 stalls in the window (the warm-up's 2 s one left out)", r.get("stalls") == 2, r.get("stalls"))
    check("hud: longest 300 ms", r.get("longest") == 300.0, r.get("longest"))
    check("hud: 120 s measured", abs(r.get("win", 0) - 120) < 0.1, r.get("win"))
    check("hud: frame times end with the run (ftdrift ~0)", abs(r.get("ftdrift", 9)) < 0.1, r.get("ftdrift"))
    check("hud: old hud key kept", r.get("hud", "").startswith("50/"), r.get("hud"))
    check("no sensors: no temperature columns", "tmax" not in r and "tcell" not in r, r.get("tmax"))
    runs = [d]

    # the temperatures ps3-bench.sh adds from the kernel's sensors (docs/THERMAL.md)
    dt = tmp / "ps3-bench-20261018T000040Z-faketemp-base"
    bench(dt, game="faketemp", variant="base", ended="exit", temp_max="cell:71.5 rsx:66.0", temp_stop=1)
    r = report.run(dt)
    check("temps: tmax 71.5/66.0, stopped", r.get("tmax") == "71.5/66.0!", r.get("tmax"))
    check("temps: tcell/trsx as numbers", r.get("tcell") == 71.5 and r.get("trsx") == 66.0, (r.get("tcell"), r.get("trsx")))

    # the same frames, the window in two pieces (a game marking its own, WIN_ON_RE/WIN_OFF_RE): the
    # 300 ms stall at 100 s falls between them
    d2 = tmp / "ps3-bench-20261018T000050Z-fakeseg-base"
    shutil.copytree(d / "hud", d2 / "hud")
    bench(d2, game="fakeseg", variant="base", ended="exit", seconds="%.2f" % t, warmup=60, measure=120,
          load_end="10.00", hud_t1="5.00", window="70.00 99.50 101.00 190.00")
    r = report.run(d2)
    check("pieces: the stall between the pieces left out", r.get("stalls") == 1 and r.get("longest") == 150.0,
          (r.get("stalls"), r.get("longest")))
    check("pieces: 118.5 s measured, median 50", r.get("win") == 118.5 and r.get("hud_win") == 50, (r.get("win"), r.get("hud_win")))

    # gameplay-only stalls (report.py *_play): a demo looped in three 20 s passes (10-30, 30-50,
    # 50-70), window 30-70. An 800 ms frame ends just after each pass line (the reload) and a 300 ms
    # one 10 s into pass 2 and 3 (the play); 20 ms frames otherwise
    ft2, t2, st2 = [], 5.0, {30.0: 800.0, 40.0: 300.0, 50.0: 800.0, 60.0: 300.0}
    while t2 < 75:
        v = 20.0
        for at in [x for x in st2 if t2 >= x]:
            v = st2.pop(at)
        ft2.append(v)
        t2 += v / 1000
    passes2 = {"pass1": "10.00 30.00 50 30 0", "pass2": "30.00 50.00 50 30 1", "pass3": "50.00 70.00 50 30 1"}

    def looped(name, game, t0=True, out=True, **extra):
        d = tmp / ("ps3-bench-20261018T0004%s-%s-base" % (name, game))
        write(d / "hud" / "frametime_(ms)", "".join("%g\n" % v for v in ft2))
        kv = dict(game=game, variant="base", ended="window", seconds="%.2f" % t2, warmup=60, measure=120,
                  load_end="10.00", hud_t1="5.00", window="30.00 70.00", passes=3, measured="40.00")
        if t0:
            kv["t0"] = "1000.000000"
        bench(d, **kv, **passes2, **extra)
        if out:   # the load is done 2 s into passes 1, 2 and 3 (the stamps are the epoch)
            write(d / "out.log", "".join("%.3f noise\n%.3f CL_InitCGame:  2.0 seconds\n" % (1000 + s - 1, 1000 + s + 2)
                                         for s in (10, 30, 50)))
        return d
    r = report.run(looped("00", "openarena"))
    check("play: whole window keeps its numbers", r.get("stalls") == 4 and r.get("stall_s") == 2.2, (r.get("stalls"), r.get("stall_s")))
    check("play: openarena: reloads (pass start to the load line + 1 s) left out",
          r.get("stalls_play") == 2 and r.get("stall_play_s") == 0.6 and r.get("longest_play") == 300.0,
          (r.get("stalls_play"), r.get("stall_play_s"), r.get("longest_play")))
    check("play: 6 s of reloads in the window, 34 s of play", r.get("reload_s") == 6.0 and r.get("win_play") == 34.0
          and r.get("reload_n") == 3, (r.get("reload_s"), r.get("win_play"), r.get("reload_n")))
    check("play: 1% low of the play frames (mostly 20 ms)", r.get("low1_play") == 50.0, r.get("low1_play"))
    r = report.run(looped("01", "neverball", out=False))
    check("play: neverball: a fixed second after each pass line",
          r.get("stalls_play") == 2 and r.get("reload_s") == 2.0 and r.get("win_play") == 38.0,
          (r.get("stalls_play"), r.get("reload_s"), r.get("win_play")))
    r = report.run(looped("02", "openarena", t0=False))
    check("play: a load line without t0 can't be placed: no play keys", "stalls_play" not in r and r.get("stalls") == 4, r)
    r = report.run(looped("03", "fakeloop", out=False))
    check("play: a game without a reload: the plain numbers", r.get("stalls_play") == 4 and r.get("reload_s") == 0
          and r.get("stall_play_s") == r.get("stall_s"), (r.get("stalls_play"), r.get("reload_s")))
    r = report.run(looped("04", "fakeloop", out=False, reload_fixed=5))
    check("play: bench.txt's reload_fixed sets a game's own", r.get("stalls_play") == 2 and r.get("reload_s") == 10.0,
          (r.get("stalls_play"), r.get("reload_s")))
    r = report.run(looped("05", "fakeloop", reload_re="LOADED|InitCGame", reload_after=0.5))
    check("play: bench.txt's reload_re/reload_after (segments end 2.5 s into passes 2 and 3)",
          r.get("stalls_play") == 2 and r.get("reload_s") == 5.0, (r.get("stalls_play"), r.get("reload_s")))
    # the load's last frame starting inside a segment and ending after its cut (2.5 s from 32.5 s,
    # the cut at 33 s) is the reload's, and the segment ends with it
    ft2, t2, st2 = [], 5.0, {30.0: 800.0, 32.5: 2500.0, 40.0: 300.0, 50.0: 800.0, 60.0: 300.0}
    while t2 < 75:
        v = 20.0
        for at in [x for x in st2 if t2 >= x]:
            v = st2.pop(at)
        ft2.append(v)
        t2 += v / 1000
    r = report.run(looped("06", "openarena"))
    check("play: a frame that starts in a reload and outlasts its cut is the reload's",
          r.get("stalls") == 5 and r.get("stalls_play") == 2 and r.get("longest_play") == 300.0
          and 7.9 <= (r.get("reload_s") or 0) <= 8.1, (r.get("stalls"), r.get("stalls_play"), r.get("longest_play"), r.get("reload_s")))
    import passsplit   # shares reload_segments: the same segments for a stall record's times
    check("passsplit uses report.reload_segments", passsplit.report.reload_segments is report.reload_segments)

    # per-second fps only (BENCH_HUD=1 style): the 1st percentile and seconds under 10 fps
    d = tmp / "ps3-bench-20261018T000100Z-fake1s-base"
    vals = [20] * 70 + [60] * 100 + [5] * 2 + [60] * 30
    write(d / "hud" / "fps", "".join("%d\n" % v for v in vals))
    bench(d, game="fake1s", variant="base", ended="duration", seconds="205", warmup=60, measure=120,
          load_end="10.00", hud_t1="11.00", window="70.00 190.00")
    r = report.run(d)
    check("1s: window median 60", r.get("hud_win") == 60, r.get("hud_win"))
    check("1s: 1% low = slowest second (5)", r.get("low1") == 5, r.get("low1"))
    check("1s: 2 seconds under 10 fps", r.get("stalls") == 2, r.get("stalls"))
    check("1s: src hud1s, 120 s", r.get("src") == "hud1s" and r.get("win") == 120, (r.get("src"), r.get("win")))
    runs.append(d)

    # passes: the game's own fps per pass, the first ones warm-up
    for n, fps in ((1, [40.0, 50.0, 52.0, 51.0, 53.0]), (2, [42.0, 54.0, 56.0, 55.0, 57.0])):
        d = tmp / ("ps3-bench-20261018T00020%dZ-fakepass-base" % n)
        passes = {"pass%d" % (i + 1): "%.2f %.2f %s 30 %d" % (10 + 40 * i, 50 + 40 * i, f, i >= 2)
                  for i, f in enumerate(fps)}
        bench(d, game="fakepass", variant="base", ended="window", seconds="215", fps=fps[0], maxms=900,
              load="10", load_end="10.00", warmup=60, measure=120, window="90.00 210.00", passes=5,
              measured="120.00", **passes)
        r = report.run(d)
        check("passes %d: steady = median of the counted passes" % n,
              r.get("steady") == sorted(fps[2:])[1] and r.get("src") == "passes", (r.get("steady"), r.get("src")))
        check("passes %d: fps stays the first pass's" % n, r.get("fps") == fps[0], r.get("fps"))
        check("passes %d: 3/5 counted, longest from the passes" % n,
              r.get("passes") == "3/5" and r.get("longest") == 30, (r.get("passes"), r.get("longest")))
        runs.append(d)

    # a game's own window line (STEADY_RE)
    d = tmp / "ps3-bench-20261018T000300Z-fakeown-base"
    bench(d, game="fakeown", variant="base", ended="exit", seconds="250", fps=20.5, warmup=60, measure=120,
          load_end="40.00", window="100.00 220.00", steady_fps=27.25, steady_maxms=412)
    r = report.run(d)
    check("own: steady from the game's line", r.get("steady") == 27.25 and r.get("src") == "game", (r.get("steady"), r.get("src")))
    check("own: longest from its line", r.get("longest") == 412, r.get("longest"))
    runs.append(d)

    # Mesa's NV30_SYSMEM_STATS lines in game.log: the peak, the last line's counts, the hint's frame
    d = tmp / "ps3-bench-20261018T000400Z-fakesys-base"
    bench(d, game="fakesys", variant="base", ended="exit", seconds="100")
    write(d / "game.log", "start\n"
          "nv30-sysmem: hint set frame=3 mode=all hint=1 copies=4 kb=2048 made_create=0 kb_create=0 made_read=4 kb_read=2048 made_migrate=0 readbacks=9 kb_readback=4096\n"
          "nv30-sysmem: frame frame=600 mode=all hint=1 copies=200 kb=65536 made_create=196 kb_create=63488 made_read=4 kb_read=2048 made_migrate=0 readbacks=9 kb_readback=4096\n"
          "nv30-sysmem: end frame=900 mode=all hint=1 copies=100 kb=30720 made_create=300 kb_create=92160 made_read=5 kb_read=3072 made_migrate=1 readbacks=12 kb_readback=5120\n")
    r = report.run(d)
    check("sysmem: peak 64 MiB, last line's counts, hint at frame 3",
          (r.get("sysmem_mb"), r.get("sysmem_create_mb"), r.get("sysmem_read_mb"), r.get("sysmem_readbacks"),
           r.get("sysmem_hint")) == (64.0, 90.0, 3.0, 12, 3),
          {k: v for k, v in r.items() if k.startswith("sysmem")})
    r = report.run(tmp / "ps3-bench-20261018T000300Z-fakeown-base")
    check("sysmem: no columns without the lines", not any(k.startswith("sysmem") for k in r), r)
    check("forcedxt, shcache: no columns without the lines",
          not any(k.startswith(("fdxt_", "shc_")) for k in r), r)

    # Mesa's NV30_FORCE_DXT_STATS and NV30_SHADER_CACHE_STATS lines: the last one counts
    d = tmp / "ps3-bench-20261018T000500Z-fakedxt-base"
    bench(d, game="fakedxt", variant="base", ended="exit", seconds="100")
    write(d / "game.log", "start\n"
          "nv30-forcedxt: 256x256 GL_RGBA GL_RGBA/GL_UNSIGNED_BYTE texture 3 -> dxt1\n"
          "nv30-forcedxt: tex mode=opaque min=64 tex=10 dxt1=10 dxt5=0 levels=90 kb_raw=10240 kb_dxt=1280 sub=0 sub_kb=0 unforced=0 unforced_kb=0 mipgen=0 mipgen_ms=0 scan_ms=3 skip_nodata=4 skip_small=20 skip_npot=0 skip_border=0 skip_transfer=0 skip_format=2 skip_nearest=0 skip_alpha=7 skip_nos3tc=0\n"
          "nv30-forcedxt: unforced why=fbo texture=7 levels=1 mode=opaque min=64 tex=12 dxt1=11 dxt5=1 levels=99 kb_raw=12288 kb_dxt=1536 sub=5 sub_kb=20 unforced=1 unforced_kb=1024 mipgen=2 mipgen_ms=40 scan_ms=4 skip_nodata=4 skip_small=21 skip_npot=1 skip_border=0 skip_transfer=0 skip_format=2 skip_nearest=0 skip_alpha=8 skip_nos3tc=0\n"
          "nv30-forcedxt: 32x32 GL_RGBA GL_RGBA/GL_UNSIGNED_BYTE kept (small)\n"
          "nv30-shcache: end cache=on tgsi_hit=50 tgsi_miss=4 tgsi_hit_ms=20 tgsi_miss_ms=300 fp_hit=30 fp_miss=2 fp_hit_ms=5 fp_miss_ms=40 vp_hit=31 vp_miss=1 vp_hit_ms=6 vp_miss_ms=30\n")
    r = report.run(d)
    check("forcedxt: the last stats line, skips by count",
          (r.get("fdxt_tex"), r.get("fdxt_dxt5"), r.get("fdxt_raw_mb"), r.get("fdxt_dxt_mb"), r.get("fdxt_unforced"),
           r.get("fdxt_sub"), r.get("fdxt_mipgen"), r.get("fdxt_mipgen_ms"), r.get("fdxt_scan_ms"), r.get("fdxt_skip"))
          == (12, 1, 12.0, 1.5, 1, 5, 2, 40, 4, "small21,alpha8,nodata4,format2,npot1"),
          {k: v for k, v in r.items() if k.startswith("fdxt")})
    check("shcache: hits, misses, ms summed",
          (r.get("shc_tgsi_hit"), r.get("shc_fp_miss"), r.get("shc_vp_hit"), r.get("shc_hit_ms"), r.get("shc_miss_ms"))
          == (50, 2, 31, 31, 370), {k: v for k, v in r.items() if k.startswith("shc")})

    # a run from before the steady state: no window, no new keys, old ones as before
    d = tmp / "ps3-bench-20261017T120000Z-fakeold-base"
    write(d / "hud" / "fps", "".join("%d\n" % v for v in [10] * 13 + [30] * 77))
    bench(d, game="fakeold", variant="base", ended="duration", seconds="90.4", fps="", hud_skip=12)
    r = report.run(d)
    check("old: no steady keys", not any(k in r for k in ("steady", "low1", "stalls", "win")), r)
    check("old: hud median as before", r.get("hud_med") == 30, r.get("hud_med"))
    runs.append(d)
    print()
    report.main([str(x) for x in runs])
    print()
    return runs


# --- 2. matrix.py into a scratch results directory -----------------------------------------
def matrix(tmp, runs):
    res = tmp / "results"
    res.mkdir()
    if (HERE / "results" / "fps.csv").exists():
        shutil.copy(HERE / "results" / "fps.csv", res / "fps.csv")
    env = dict(os.environ, BENCH_RESULTS=str(res))
    p = subprocess.run([sys.executable, str(HERE / "matrix.py"), "selftest row", "--obs", "made up",
                        *map(str, runs)], env=env, capture_output=True, text=True)
    check("matrix.py ran", p.returncode == 0, p.stderr[-500:])
    import csv
    rows = {}
    for name in ("fps", "fps_steady", "low1", "stalls", "longest_ms"):
        f = res / f"{name}.csv"
        check("matrix: %s.csv written" % name, f.exists())
        if f.exists():
            with f.open(newline="") as fh:
                rows[name] = list(csv.DictReader(fh))
    if "fps" in rows and "fps_steady" in rows:
        check("matrix: fps_steady.csv has fps.csv's rows, in order",
              [r["test"] for r in rows["fps"]] == [r["test"] for r in rows["fps_steady"]],
              len(rows["fps_steady"]))
        last = rows["fps_steady"][-1]
        check("matrix: the new row's cells", last["test"] == "selftest row" and last.get("fake-base") == "50"
              and last.get("fakepass-base") == "54.0" and last.get("fakeown-base") == "27.2", last)
        check("matrix: runs counted once", last["runs"] == "6", last["runs"])
    if "stalls" in rows:
        check("matrix: stalls cell", rows["stalls"][-1].get("fake-base") == "2", rows["stalls"][-1])
    f = res / "stall_play_s.csv"
    check("matrix: stall_play_s.csv (the play numbers) written with the fake-base cell",
          f.exists() and "0.5" in f.read_text().splitlines()[-1].split(","), f.read_text()[-200:] if f.exists() else "-")
    check("matrix: no stall cause files without a stalls.rec", not [f for f in res.glob("stall_*_s.csv") if f.name != "stall_play_s.csv"],
          [x.name for x in res.glob("stall_*_s.csv")])


# --- 3. client.sh with a fake game -----------------------------------------------------------
def fake_game(mode):
    """the fake game: frames every 20 ms (the HUD's dump files, as Mesa writes them) and lines"""
    hud = os.environ.get("GALLIUM_HUD_DUMP_DIR")
    out = sys.stdout
    t0 = time.monotonic()

    def say(s):
        out.write(s + "\n")
        out.flush()
    if mode == "hudwin":   # a line split in two by a 0.6 s pause before the first frame
        out.write("lo")
        out.flush()
        time.sleep(0.6)
        say("aded")
    if os.environ.get("FAKE_T0_FILE"):   # when the game itself started (the start-up profile's gate)
        with open(os.environ["FAKE_T0_FILE"], "w") as f:
            f.write("%.6f\n" % time.time())
    for k in sorted(os.environ):
        if k.startswith(("NV30_", "MESA_SHADER_CACHE", "LD_DEBUG", "ALSOFT_CONF", "FLUIDSYNTH_", "JSIMD_",
                         "LD_BIND_NOW", "LD_LIBRARY_PATH")):
            say("env %s=%s" % (k, os.environ[k]))   # the switches the game got (client.sh's genv, libenv, shadercache)
    ft = open(os.path.join(hud, "frametime_(ms)"), "a", buffering=1) if hud else None
    fp = open(os.path.join(hud, "fps"), "a", buffering=1) if hud else None
    last = sec = time.monotonic()
    frames = passn = wedges = 0
    alive = False
    pass_t = time.monotonic()
    while time.monotonic() - t0 < 30:
        time.sleep(0.02)
        now = time.monotonic()
        if ft and frames:
            ft.write("%.3f\n" % ((now - last) * 1000))
        last = now
        frames += 1
        if fp and now - sec >= 1:
            fp.write("%d\n" % round(frames / (now - sec)))
            sec, frames = now, 0
        if mode == "passes" and now - pass_t >= 1.5:
            passn += 1
            say("pass: %d frames 1.5 seconds %d.0 fps, max %d ms" % (75, 10 * passn, 20 + passn))
            pass_t = now
        if mode in ("wedge", "crash") and wedges < 1 + (mode == "wedge") and now - t0 >= 1 + 0.5 * wedges:
            wedges += 1
            say("[ALSOFT] (EE) Wait timeout... buffer size too low?")
        if mode == "wedge" and 3 <= now - t0 and not alive:
            alive = True
            say("alive after the dump")
        if mode == "crash" and now - t0 >= 2:
            os.kill(os.getpid(), 11)
        if mode == "own" and 1 <= now - t0 < 1.03:
            say("own: window on")
        if mode == "own" and 1.5 <= now - t0 < 1.53:
            say("own: window off")
        if mode == "own" and 2 <= now - t0 < 2.03:
            say("own: window on")
        if mode == "own" and now - t0 >= 3:
            say("own: steady 150 frames in 3000 ms: 49.5 fps, longest 33 ms")
            return


RECIPES = {
    "fakehud": """CMD=("$FAKE" --fake-game hudwin); LOAD_MARK='^loaded'; WARMUP=2; MEASURE=4; WINDOW_STOP=1; DURATION=25
variant_nostop() { WINDOW_STOP=0; DURATION=6; }""",
    "fakepass": """CMD=("$FAKE" --fake-game passes); FPS_RE='^pass: [0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='^pass: .* max ([0-9]+) ms'; PASSES=1; WARMUP=3; MEASURE=4; DURATION=25""",
    "fakewedge": """CMD=("$FAKE" --fake-game wedge); COMM=python3; DURATION=6; WARMUP=1; MEASURE=2""",
    "fakecrash": """CMD=("$FAKE" --fake-game crash); COMM=python3; DURATION=8; WARMUP=1; MEASURE=2""",
    "fakeown": """CMD=("$FAKE" --fake-game own); STEADY_RE='^own: steady [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
STEADY_MAXMS_RE='^own: steady .* longest ([0-9]+) ms'; WARMUP=1; MEASURE=1; DURATION=25
WIN_ON_RE='^own: window on'; WIN_OFF_RE='^own: window off'""",
}


def client(tmp):
    rdir, runs = tmp / "recipes", {}
    rdir.mkdir()
    fake = tmp / "fake-game"
    write(fake, "#!/bin/sh\nexec %s %s \"$@\"\n" % (sys.executable, Path(__file__).resolve()))
    fake.chmod(0o755)
    for g, text in RECIPES.items():
        write(rdir / f"{g}.sh", text.replace("$FAKE", str(fake)) + "\n")
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                            "GALLIUM_HUD_DUMP_DIR")}
    env.update(BENCH_RECIPES=str(rdir), BENCH_BACKUP=str(tmp / "backup"))
    for g, v in (("fakehud", "base"), ("fakehud", "nostop"), ("fakepass", "base"), ("fakeown", "base")):
        d = tmp / ("ps3-bench-20261018T0100%02dZ-%s-%s" % (len(runs), g, v))
        env["BENCH_DIR"] = str(d)
        t = time.monotonic()
        p = subprocess.run(["bash", str(HERE / "client.sh"), g, v], env=env, capture_output=True, text=True)
        runs[(g, v)] = (d, report.kv(d / "bench.txt"), time.monotonic() - t)
        check("client %s %s: exit 0" % (g, v), p.returncode == 0, p.stdout[-800:] + p.stderr[-800:])

    d, b, wall = runs[("fakehud", "base")]
    check("hudwin: the split line seen (load_end set from LOAD_MARK)", b.get("load_end") not in (None, ""), b)
    check("hudwin: ended=window", b.get("ended") == "window", b.get("ended"))
    le, w = float(b.get("load_end") or 0), (b.get("window") or "0 0").split()
    check("hudwin: window = load + 2 .. + 6", abs(float(w[0]) - le - 2) < 0.05 and abs(float(w[1]) - le - 6) < 0.05, w)
    check("hudwin: stopped ~2 s after the window", abs(float(b.get("seconds", 0)) - le - 8) < 0.6, b.get("seconds"))
    check("hudwin: the HUD's first value seen", b.get("hud_t1") not in (None, ""), b.get("hud_t1"))
    r = report.run(d)
    check("hudwin: report: ~50 fps in the window", 40 <= (r.get("steady") or 0) <= 52, r.get("steady"))
    check("hudwin: report: ~4 s measured, frame times placed", 3.5 <= r.get("win", 0) <= 4.05 and abs(r.get("ftdrift", 9)) < 0.5,
          (r.get("win"), r.get("ftdrift")))

    d, b, wall = runs[("fakehud", "nostop")]
    check("nostop: ended=duration at 6 s", b.get("ended") == "duration" and abs(float(b.get("seconds", 0)) - 6) < 0.5,
          (b.get("ended"), b.get("seconds")))
    le, w = float(b.get("load_end") or 0), (b.get("window") or "0 0").split()
    check("nostop: window up to the run's end", abs(float(w[0]) - le - 2) < 0.05 and abs(float(w[1]) - float(b["seconds"])) < 0.05, w)

    d, b, wall = runs[("fakepass", "base")]
    passes = sorted((k for k in b if k.startswith("pass") and k[4:].isdigit()), key=lambda k: int(k[4:]))
    counted = [b[k].split() for k in passes if b[k].split()[-1] == "1"]
    check("passes: ended=window after enough counted passes", b.get("ended") == "window", b.get("ended"))
    check("passes: first counted pass starts >= 0.9 x WARMUP after the load",
          counted and float(counted[0][0]) - float(b["load_end"]) >= 2.7, counted[:1])
    check("passes: counted ~3 passes (4 s less 5%)", len(counted) == 3, [b[k] for k in passes])
    check("passes: fps= the first pass's (10), maxms its 21", b.get("fps") == "10.0" and b.get("maxms") == "21",
          (b.get("fps"), b.get("maxms")))
    r = report.run(d)
    check("passes: report steady = median of the counted", r.get("src") == "passes" and
          r.get("steady") == sorted(float(c[2]) for c in counted)[1], (r.get("steady"), counted))

    d, b, wall = runs[("fakeown", "base")]
    check("own: steady_fps from STEADY_RE", b.get("steady_fps") == "49.5" and b.get("steady_maxms") == "33",
          (b.get("steady_fps"), b.get("steady_maxms")))
    check("own: ended=exit", b.get("ended") == "exit", b.get("ended"))
    w = [float(x) for x in (b.get("window") or "").split()]
    check("own: the window in two pieces, the second closed at the end",
          len(w) == 4 and abs(w[1] - w[0] - 0.5) < 0.1 and abs(w[3] - float(b["seconds"])) < 0.05, w)
    r = report.run(d)
    check("own: report steady 49.5 (game)", r.get("steady") == 49.5 and r.get("src") == "game", (r.get("steady"), r.get("src")))


def client_gprefetch(tmp):
    """client.sh's BENCH_GAMEPREFETCH (docs/STALLS.md "Game prefetch") with ps3-desktop's
    ps3-game-prefetch from this tree: a learn run leaves a list, a fetch run reads it, the tags and
    the warm-up columns in report.py"""
    gp = HERE.parent.parent / "ps3-desktop" / "files" / "libexec" / "ps3-game-prefetch"
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                            "GALLIUM_HUD_DUMP_DIR")}
    lists = tmp / "gprefetch"
    env.update(BENCH_RECIPES=str(tmp / "recipes"), BENCH_BACKUP=str(tmp / "backup"),
               BENCH_GAMEPREFETCH_BIN=str(gp), BENCH_GAMEPREFETCH_DIR=str(lists))
    out = {}
    for mode in ("learn", "1", "bogus", "fetch+mb"):
        d = tmp / ("ps3-bench-20261018T0200%02dZ-fakehud-base" % len(out))
        env.update(BENCH_DIR=str(d), BENCH_GAMEPREFETCH=mode.split("+")[0])
        if mode.endswith("+mb"):
            env["BENCH_GAMEPREFETCH_MB"] = "1"
        p = subprocess.run(["bash", str(HERE / "client.sh"), "fakehud", "base"], env=env, capture_output=True, text=True)
        out[mode] = (d, p)
    d, p = out["learn"]
    b = report.kv(d / "bench.txt")
    check("gprefetch learn: exit 0, gprefetch=learn", p.returncode == 0 and b.get("gprefetch") == "learn",
          (p.returncode, b.get("gprefetch"), p.stdout[-500:]))
    log = (d / "game.log").read_text(errors="replace") if (d / "game.log").exists() else ""
    check("gprefetch learn: the list learnt after the game (game-learn line)", "game-learn" in log, log[-500:])
    names = sorted(x.name for x in lists.iterdir()) if lists.is_dir() else []
    check("gprefetch learn: a list and the command's link to it", any(n.endswith("selftest.py.list") for n in names)
          and any(n.endswith("fake-game.list") and (lists / n).is_symlink() for n in names), names)
    r = report.run(d)
    check("gprefetch learn: report variant base@gpflearn", r.get("variant") == "base@gpflearn", r.get("variant"))
    check("warm-up columns: no stalls in the fake game's 2 s warm-up", r.get("stalls_warm") == 0 and
          r.get("stall_warm_s") == 0 and 15 <= (r.get("longest_warm") or 0) < 100,
          (r.get("stalls_warm"), r.get("stall_warm_s"), r.get("longest_warm")))
    d, p = out["1"]
    b = report.kv(d / "bench.txt")
    log = (d / "game.log").read_text(errors="replace") if (d / "game.log").exists() else ""
    check("gprefetch 1: fetch, the list read (game-fetch line), nothing learnt",
          p.returncode == 0 and b.get("gprefetch") == "fetch" and "game-fetch" in log and "nothing learnt" not in log
          and "game-learn" not in log, (b.get("gprefetch"), log[-500:]))
    check("gprefetch fetch: ended=window as without it", b.get("ended") == "window", b.get("ended"))
    check("gprefetch bogus: refused", out["bogus"][1].returncode == 2, out["bogus"][1].stdout[-300:])
    d, p = out["fetch+mb"]
    b = report.kv(d / "bench.txt")
    log = (d / "game.log").read_text(errors="replace") if (d / "game.log").exists() else ""
    check("gprefetch fetch with BENCH_GAMEPREFETCH_MB=1: the budget 1 MB, tag gpffetch_mb1",
          b.get("gprefetch") == "fetch_mb1" and "budget 1.0 MB" in log and
          report.run(d).get("variant") == "base@gpffetch_mb1", (b.get("gprefetch"), log[-300:]))


SHOT_RECIPES = {
    "fakeshot": """CMD=("$FAKE" --fake-game hudwin); LOAD_MARK='^loaded'; WARMUP=2; MEASURE=4; WINDOW_STOP=1; DURATION=25
SHOT_POINTS="1 3 60"
variant_re() { SHOT_RE='^env NV30_SHOTTEST'; ENV+=(NV30_SHOTTEST=1); }
variant_mark() { SHOT_FROM=mark; during() { sleep 4; shot_mark; sleep 30; }; }
variant_nomark() { SHOT_FROM=mark; during() { sleep 1; }; }
variant_menu() { SHOT_STATIC=1; }""",
}


def client_shots(tmp):
    """client.sh's screenshots with a fake xwd (0.2 s a capture), shots.py and report.py on the runs"""
    import shots
    print("\nscreenshots (client.sh BENCH_SHOTS, shots.py)")
    rdir, bindir = tmp / "shot-recipes", tmp / "shot-bin"
    fake = tmp / "fake-game"
    if not fake.exists():
        write(fake, "#!/bin/sh\nexec %s %s \"$@\"\n" % (sys.executable, Path(__file__).resolve()))
        fake.chmod(0o755)
    for g, text in SHOT_RECIPES.items():
        write(rdir / f"{g}.sh", text.replace("$FAKE", str(fake)) + "\n")
    write(bindir / "xwd", "#!/bin/sh\nsleep 0.2\nprintf 'XWD %s\\n' \"$*\"\n")
    (bindir / "xwd").chmod(0o755)
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                            "GALLIUM_HUD_DUMP_DIR", "BENCH_SHOTS")}
    env.update(BENCH_RECIPES=str(rdir), BENCH_BACKUP=str(tmp / "backup"), PATH="%s:%s" % (bindir, env["PATH"]),
               BENCH_SHOT_ROOT=str(tmp / "shotroot"))
    runs = {}
    shm = Path("/dev/shm/ps3-bench-selftest-%d-fakeshot-base" % os.getpid())
    for v, shots_env, d in (("base", "auto", shm), ("re", "2", None), ("mark", "1", None), ("nomark", "1", None),
                            ("menu", "off", None), ("menu", "2", None)):
        d = d or tmp / ("ps3-bench-20261020T1200%02dZ-fakeshot-%s" % (len(runs), v))
        e = dict(env, BENCH_DIR=str(d), BENCH_SHOTS=shots_env)
        p = subprocess.run(["bash", str(HERE / "client.sh"), "fakeshot", v], env=e, capture_output=True, text=True)
        check("shots %s (BENCH_SHOTS=%s): exit 0" % (v, shots_env), p.returncode == 0, p.stdout[-600:] + p.stderr[-600:])
        runs[(v, shots_env)] = (d, report.kv(d / "bench.txt"), shots.parse(d))
    try:
        d, b, sh = runs[("base", "auto")]
        sd = tmp / "shotroot" / (".shots-" + shm.name)
        le = float(b.get("load_end") or 0)
        check("shots base: the recipe's points (auto), from the load's end", sh["points"] == [1, 3, 60] and sh["ref"] == "load"
              and abs((sh["ref_t"] or 0) - le) < 0.05, (sh["points"], sh["ref"], sh["ref_t"], le))
        check("shots base: 1 s and 3 s taken, 60 s skipped (the run ended)", sorted(sh["shots"]) == [1, 3] and sh["skipped"] == [60],
              (sorted(sh["shots"]), sh["skipped"]))
        t1, t3 = sh["shots"].get(1, {}).get("t", 0), sh["shots"].get(3, {}).get("t", 0)
        check("shots base: taken at the load + 1 s and + 3 s", abs(t1 - le - 1) < 0.3 and abs(t3 - le - 3) < 0.3, (t1, t3, le))
        txt = (d / "bench.txt").read_text() if (d / "bench.txt").exists() else ""
        check("shots base: the bench.txt line shot=1s:shots/shot-1s.xwd:<ms>, the fake's 200 ms",
              re.search(r"^shot=1s:shots/shot-1s\.xwd:\d+$", txt, re.M) and 150 <= sh["shots"].get(1, {}).get("ms", 0) <= 600,
              sh["shots"].get(1))
        check("shots base: the run directory in /dev/shm: the captures in BENCH_SHOT_ROOT/.shots-<run>, not in RAM",
              (sd / "shot-1s.xwd").read_text().startswith("XWD -root -silent") and not (shm / "shots").exists(),
              sorted(os.listdir(sd)) if sd.exists() else None)
        d, b, sh = runs[("re", "2")]
        check("shots re: from the SHOT_RE line (the game's env line), one point given", sh["ref"] == "re" and sh["points"] == [2]
              and 2 in sh["shots"] and (d / "shots" / "shot-2s.xwd").exists(), (sh["ref"], sh["points"], sh["shots"]))
        cs = shots.costs(d)
        check("shots re: frames touched by the capture counted from the HUD's frame times (~0.2 s at 20 ms: 8-15)",
              cs and 5 <= cs[0].get("frames", 0) <= 20 and cs[0].get("before_med") is not None, cs)
        r = report.run(d)
        inwin = sh["shots"][2]["t"] >= float(b["window"].split()[0])
        check("shots re: report.py's shot columns, @shot only when a capture is in the window",
              r.get("shot_n") == 1 and r.get("shot_ms", 0) >= 150 and r["variant"] == ("re@shot" if inwin else "re"),
              (r.get("shot_n"), r.get("shot_ms"), r["variant"], inwin))
        d, b, sh = runs[("mark", "1")]
        log = (d / "during.log").read_text() if (d / "during.log").exists() else ""
        check("shots mark: from during()'s shot_mark (4 s after the start), not the load's end",
              sh["ref"] == "mark" and 3.9 <= (sh["ref_t"] or 0) <= 4.6 and abs(sh["shots"].get(1, {}).get("t", 0) - sh["ref_t"] - 1) < 0.3,
              (sh["ref"], sh["ref_t"], sh["shots"], log[-200:]))
        d, b, sh = runs[("nomark", "1")]
        check("shots nomark: during() ended without shot_mark: the load's end", sh["ref"] == "load-nomark"
              and abs((sh["ref_t"] or 0) - float(b.get("load_end") or 0)) < 0.05 and 1 in sh["shots"], (sh["ref"], sh["ref_t"], b.get("load_end")))
        d, b, sh = runs[("menu", "off")]
        check("shots off: no shot lines, no shots dir", not any(k.startswith("shot") for k in b) and not (d / "shots").exists(),
              [k for k in b if k.startswith("shot")])
        d, b, sh = runs[("menu", "2")]
        check("shots menu: shot_static=1", sh["static"] and b.get("shot_static") == "1", b.get("shot_static"))
        # an inside-the-window shot in a made-up run: report.py keeps it apart (base@shot)
        x = tmp / "ps3-bench-20261020T130000Z-fakeshot-base"
        bench(x, game="fakeshot", variant="base", window="10.00 130.00", shots="90", shot_ref="load:5.00",
              shot="90s:shots/shot-90s.xwd:640", shot_t="90s:95.10:100:103")
        write(x / "hud" / "frametime_(ms)", "".join("%s\n" % v for v in [33.0] * 100 + [35.0, 600.0, 40.0] + [33.0] * 20))
        r = report.run(x)
        cs = shots.costs(x)
        check("shots made-up: in the window: base@shot, shot_win=1, the longest frame touched 600 ms, 33 ms before",
              r["variant"] == "base@shot" and r.get("shot_win") == 1 and r.get("shot_frame") == 600.0
              and cs and cs[0]["frames"] == 4 and cs[0]["before_med"] == 33.0, (r["variant"], r.get("shot_win"), r.get("shot_frame"), cs))
        if shutil.which("magick"):
            shots_magick(tmp, report, shots)
        else:
            print("(no ImageMagick here: shots.py convert and sheet not checked)")
    finally:
        shutil.rmtree(shm, ignore_errors=True)


def shots_magick(tmp, report, shots):
    """shots.py convert, sheet, pending on a made-up PS3 run (one picture black) and its PC reference"""
    run, ref = tmp / "pulled" / "ps3-bench-20261020T140000Z-smoke-fakeshot-base", tmp / "bench-ref" / "fakeshot-base"
    bench(run, game="fakeshot", variant="base", shots="30 90", shot_ref="load:40.00", shot="30s:shots/shot-30s.xwd:410",
          shot_t="30s:70.20:-:-", shot_skipped="90s")
    write(run / "result", "rc 0 wall 100 s\n")
    write(run / "versions", "mesa-libgallium:ppc64\t26.2.4-1+ps3.628\nfakeshot\t1.0-1+ps3.1\nlibspeq0\t1\n6.12.111+ps3.472\n")
    bench(ref, game="fakeshot", variant="base", shots="30 90", shot_ref="load:4.00", shot="90s:shots/shot-90s.xwd:20",
          shot_t="90s:94.00:-:-")
    with open(ref / "bench.txt", "a") as f:
        f.write("shot=30s:shots/shot-30s.xwd:20\nshot_t=30s:34.00:-:-\n")
    write(ref / "versions", "mesa-libgallium:amd64\t26.2.4-1\nfakeshot\t1.0-1\n")
    (run / "shots").mkdir()
    (ref / "shots").mkdir()
    subprocess.run(["magick", "-size", "1920x1080", "xc:black", str(run / "shots" / "shot-30s.xwd")], check=True)
    for p in (30, 90):
        subprocess.run(["magick", "-size", "1920x1080", "gradient:red-blue", str(ref / "shots" / ("shot-%ds.png" % p))], check=True)
    made = shots.post(tmp / "pulled", tmp / "bench-ref")
    sheet = run / "shots-vs-pc.png"
    w = subprocess.run(["magick", "identify", "-format", "%w", str(sheet)], capture_output=True, text=True).stdout
    check("shots.py post: the .xwd converted to PNG and removed", (run / "shots" / "shot-30s.png").exists()
          and not (run / "shots" / "shot-30s.xwd").exists(), sorted(os.listdir(run / "shots")))
    check("shots.py post: one sheet, 1608 px wide", len(made) == 1 and w == "1608", (made, w))
    res = (run / "result").read_text()
    check("shots.py post: the result's shotsheet: line, the reference, PS3 BLACK at 30s",
          "shotsheet: %s vs %s (PC: " % (sheet, ref) in res and "PS3 BLACK at 30s" in res and res.count("shotsheet:") == 1, res)
    check("shots.py post again: nothing new (the sheet is newer than its shots)", shots.post(tmp / "pulled", tmp / "bench-ref") == [])
    check("shots.py pending: the sheet without a REVIEW.md", shots.pending(tmp / "pulled") == [sheet])
    write(run / "REVIEW.md", "verdict: wrong  the PS3 is black\n")
    check("shots.py pending: none after REVIEW.md; report.py review=wrong", shots.pending(tmp / "pulled") == []
          and report.run(run).get("review") == "wrong", report.run(run).get("review"))


def client_dump(tmp):
    """client.sh's failure dump: the watcher on a fake game that prints a trigger line, with a fake gdb
    (gcore writes a file) and a sudo that refuses"""
    bindir = tmp / "dumpbin"
    bindir.mkdir()
    write(bindir / "sudo", "#!/bin/sh\nexit 1\n")
    write(bindir / "gdb", '''#!/bin/bash
while [ $# -gt 0 ]; do case $1 in -ex) case $2 in "gcore "*) head -c 300000 /dev/zero > "${2#gcore }" ;; esac; shift ;; esac; shift; done
echo "fake gdb backtraces"\n''')
    for f in ("sudo", "gdb"):
        (bindir / f).chmod(0o755)
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                            "GALLIUM_HUD_DUMP_DIR")}
    env.update(BENCH_RECIPES=str(tmp / "recipes"), BENCH_BACKUP=str(tmp / "backup"),
               PATH="%s:%s" % (bindir, os.environ["PATH"]))

    def run(n, game, **kv):
        d = tmp / ("ps3-bench-20261018T0500%02dZ-%s-base" % (n, game))
        p = subprocess.run(["bash", str(HERE / "client.sh"), game, "base"], env=dict(env, BENCH_DIR=str(d), **kv),
                           capture_output=True, text=True)
        return p, d, report.kv(d / "bench.txt") if (d / "bench.txt").exists() else {}
    p, d, b = run(0, "fakewedge", BENCH_DUMPON="1")
    x = d / "dump"
    names = sorted(f.name for f in x.iterdir()) if x.is_dir() else []
    gl = (d / "game.log").read_text(errors="replace") if (d / "game.log").exists() else ""
    check("dump: exit 0, dump=trigger, dump_at about 1 s, oal_wedge=2",
          p.returncode == 0 and b.get("dump") == "trigger" and 0.8 <= float(b.get("dump_at") or 0) <= 2.5
          and b.get("oal_wedge") == "2", (p.returncode, b, p.stdout[-500:], p.stderr[-300:]))
    check("dump: one capture: trigger line, proc files, threads, a compressed core, sizes in bench.txt",
          all(n in names for n in ("trigger.txt", "proc.status", "proc.maps", "proc.limits", "threads.txt", "alsa.txt",
                                   "meminfo", "vmstat", "dmesg.tail", "setup.txt"))
          and any(n in names for n in ("core.zst", "core.xz")) and "core" not in names
          and (x / "trigger.txt").read_text().count("trigger=") == 1
          and "Wait timeout" in (x / "trigger.txt").read_text()
          and re.match(r"300000:\d+", b.get("dump_core", "")), (names, b))
    check("dump: threads.txt lists the game's tasks; the game ran on after the dump (SIGCONT)",
          "== tid " in (x / "threads.txt").read_text() and "alive after the dump" in gl, gl[-300:])
    r = report.run(d)
    check("dump: report.py shows dump and wedge", str(r.get("dump", "")).startswith("trigger@") and r.get("wedge") == 2, r)
    p, d, b = run(1, "fakewedge")
    check("dump: DUMPON off: no dump directory, no dump= line, oal_wedge still counted",
          p.returncode == 0 and not (d / "dump").exists() and "dump" not in b and b.get("oal_wedge") == "2", (b, p.stdout[-300:]))
    p, d, b = run(2, "fakecrash", BENCH_DUMPON="1", BENCH_DUMPON_RE="no such line")
    check("dump: a game dying of SIGSEGV: exit 0, rc=139, dump= crash or none (host core_pattern), setup.txt names it",
          p.returncode == 0 and b.get("rc") == "139" and b.get("dump") in ("crash", "none")
          and "core_pattern=" in (d / "dump" / "setup.txt").read_text(), (b, p.stdout[-300:]))


def client_libenv(tmp):
    """client.sh's library switches (docs/SPU-AUDIO.md, docs/PROFILING.md): the OpenAL token
    (BENCH_OAL, recipes/openal.inc's settings on any recipe), BENCH_FLUIDFX, BENCH_ZLIB, BENCH_BINDNOW,
    BENCH_JSIMD: what the game gets, bench.txt's oal= and libenv=, report.py's tags, and the refusals"""
    lib = tmp / "libfluidsynth.so.3"
    lib.write_bytes(b"\x7fELF...FLUIDSYNTH_SETTINGS...")   # what client.sh greps for: patch 0002's variable
    zdir = tmp / "zlib-ng"
    zdir.mkdir()
    sysz = next((p for p in ("/lib64/libz.so.1", "/usr/lib64/libz.so.1", "/lib/x86_64-linux-gnu/libz.so.1",
                             "/usr/lib/x86_64-linux-gnu/libz.so.1") if os.path.exists(p)), None)
    if sysz:
        os.symlink(sysz, zdir / "libz.so.1")   # a real libz.so.1, so the fake game still starts
    else:
        (zdir / "libz.so.1").write_text("")
    home = tmp / "home"   # oal_setup refuses a ~/.alsoftrc of the player's: an empty home
    home.mkdir()
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                            "GALLIUM_HUD_DUMP_DIR", "LD_LIBRARY_PATH", "XDG_CONFIG_HOME")}
    env.update(BENCH_RECIPES=str(tmp / "recipes"), BENCH_BACKUP=str(tmp / "backup"), HOME=str(home),
               BENCH_FLUIDLIB=str(lib), BENCH_ZLIBNG_DIR=str(zdir))

    def run(n, **kv):
        d = tmp / ("ps3-bench-20261018T0400%02dZ-fakehud-base" % n)
        p = subprocess.run(["bash", str(HERE / "client.sh"), "fakehud", "base"], env=dict(env, BENCH_DIR=str(d), **kv),
                           capture_output=True, text=True)
        gl = (d / "game.log").read_text(errors="replace") if (d / "game.log").exists() else ""
        return p, d, report.kv(d / "bench.txt") if (d / "bench.txt").exists() else {}, gl
    p, d, b, gl = run(0, BENCH_OAL="b16,lin", BENCH_FLUIDFX="off", BENCH_ZLIB="ng", BENCH_BINDNOW="1", BENCH_JSIMD="none")
    check("libenv: exit 0, oal=b16,lin, libenv= lists the four switches",
          p.returncode == 0 and b.get("oal") == "b16,lin" and
          b.get("libenv") == "FLUIDFX=off ZLIB=ng BINDNOW=1 JSIMD=none", (p.returncode, b.get("oal"), b.get("libenv"), p.stdout[-400:]))
    conf = (d / "alsoft.conf").read_text() if (d / "alsoft.conf").exists() else ""
    check("libenv: OpenAL token on a recipe without openal.inc: the run's alsoft.conf, int16 and linear",
          conf.startswith("[general]") and "sample-type=int16" in conf and "resampler=linear" in conf and
          "env ALSOFT_CONF=%s" % (d / "alsoft.conf") in gl, (conf, gl[:400]))
    check("libenv: the game got FLUIDSYNTH_SETTINGS, LD_LIBRARY_PATH, LD_BIND_NOW, JSIMD_FORCENONE",
          "env FLUIDSYNTH_SETTINGS=synth.reverb.active=0,synth.chorus.active=0" in gl and
          "env LD_LIBRARY_PATH=%s" % zdir in gl and "env LD_BIND_NOW=1" in gl and "env JSIMD_FORCENONE=1" in gl, gl[:600])
    check("libenv: report variant base@oalb16_lin+fxoff+zng+bindnow+jsimdnone",
          report.run(d).get("variant") == "base@oalb16_lin+fxoff+zng+bindnow+jsimdnone", report.run(d).get("variant"))
    p, d, b, gl = run(1, BENCH_FLUIDFX="nocho", BENCH_JSIMD="altivec")
    check("libenv: FLUIDFX=nocho, JSIMD=altivec", p.returncode == 0 and
          "env FLUIDSYNTH_SETTINGS=synth.chorus.active=0" in gl and "env JSIMD_FORCEALTIVEC=1" in gl and
          report.run(d).get("variant") == "base@fxnocho+jsimdaltivec", (gl[:400], report.run(d).get("variant")))
    lib.write_bytes(b"\x7fELF stock")
    p, d, b, gl = run(2, BENCH_FLUIDFX="off")
    check("libenv: FLUIDFX with a libfluidsynth without patch 0002: refused",
          p.returncode == 2 and "no FLUIDSYNTH_SETTINGS" in p.stdout, p.stdout[-300:])
    p, d, b, gl = run(3, BENCH_ZLIB="ng", BENCH_ZLIBNG_DIR=str(tmp / "nowhere"))
    check("libenv: ZLIB=ng without libz-ng-compat: refused", p.returncode == 2 and "libz-ng-compat" in p.stdout, p.stdout[-300:])
    p, d, b, gl = run(4, BENCH_OAL="b17")
    check("libenv: OAL with a setting openal.inc doesn't have: refused", p.returncode == 2 and "no b17" in p.stdout, p.stdout[-300:])
    p, d, b, gl = run(5, BENCH_FLUIDFX="loud")
    check("libenv: FLUIDFX=loud: refused", p.returncode == 2, p.stdout[-300:])
    stock = tmp / "stock-openal"
    stock.mkdir()
    if sysz:
        os.symlink(sysz, stock / "libz.so.1")   # any real library: the fake game must still start
    else:
        (stock / "libz.so.1").write_text("")
    p, d, b, gl = run(6, BENCH_LDPATH=str(stock), BENCH_ZLIB="ng")
    check("libenv: LDPATH and ZLIB=ng: LDPATH first, then zlib-ng's dir; tag ldstock-openal",
          p.returncode == 0 and "env LD_LIBRARY_PATH=%s:%s" % (stock, zdir) in gl and
          report.run(d).get("variant") == "base@zng+ldstock-openal", (gl[:400], report.run(d).get("variant")))
    p, d, b, gl = run(7, BENCH_LDPATH=str(tmp / "nowhere"))
    check("libenv: LDPATH without libraries: refused", p.returncode == 2 and "no libraries" in p.stdout, p.stdout[-300:])


def client_ssdarm(tmp):
    """the SSD arms (docs/GAMEOS-HDD.md "Test drive, Phase A"): client.sh's checks and bench.txt lines
    (BENCH_GAMEDATA, BENCH_SWAP; findmnt faked), report.py's gdssd/swssd tags, and ssd-arm.sh's root
    commands in its dry mode (SSDARM_DRY=1): binds, the VRAM swap's table switch and back"""
    rdir, arm = tmp / "recipes", tmp / "arm"
    arm.mkdir()
    fake = tmp / "fake-game"
    write(rdir / "fakedata.sh", RECIPES["fakehud"].replace("$FAKE", str(fake)) + '\nDATA_DIRS=(/srv/a "/srv/b c")\n')
    fm = tmp / "fake-findmnt"   # findmnt -n -o FSROOT --mountpoint DIR: /games/DIR while BOUND exists
    write(fm, '#!/bin/sh\n[ -e %s ] || exit 1\neval d=\\${$#}\necho "/games$d"\n' % (tmp / "BOUND"))
    fm.chmod(0o755)
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                            "GALLIUM_HUD_DUMP_DIR", "BENCH_GAMEDATA", "BENCH_SWAP")}
    env.update(BENCH_RECIPES=str(rdir), BENCH_BACKUP=str(tmp / "backup"), BENCH_FINDMNT=str(fm),
               BENCH_SSDARM_STATE=str(arm))

    def run(n, game="fakedata", **kv):
        d = tmp / ("ps3-bench-20261018T0300%02dZ-%s-base" % (n, game))
        e = dict(env, BENCH_DIR=str(d), **kv)
        p = subprocess.run(["bash", str(HERE / "client.sh"), game, "base"], env=e, capture_output=True, text=True)
        return p, d
    (tmp / "BOUND").write_text("")
    p, d = run(0, BENCH_GAMEDATA="ssd")
    b = report.kv(d / "bench.txt")
    check("ssd arm: GAMEDATA=ssd with the dirs bound: exit 0, gamedata=ssd, both dirs",
          p.returncode == 0 and b.get("gamedata") == "ssd" and b.get("gamedata_dirs") == "/srv/a /srv/b c",
          (p.returncode, b.get("gamedata"), b.get("gamedata_dirs"), p.stdout[-300:]))
    check("ssd arm: report variant base@gdssd", report.run(d).get("variant") == "base@gdssd", report.run(d).get("variant"))
    (tmp / "BOUND").unlink()
    p, d = run(1, BENCH_GAMEDATA="ssd")
    check("ssd arm: GAMEDATA=ssd, the dirs not bound: refused", p.returncode == 2 and "not the SSD copy" in p.stdout, p.stdout[-300:])
    p, d = run(2, game="fakehud", BENCH_GAMEDATA="ssd")
    check("ssd arm: GAMEDATA=ssd for a recipe without DATA_DIRS: refused", p.returncode == 2 and "no DATA_DIRS" in p.stdout, p.stdout[-300:])
    (arm / "ps3-ssdarm.binds").write_text("/srv/a\n")
    p, d = run(3)
    check("ssd arm: a run without GAMEDATA, binds left over: refused", p.returncode == 2 and "binds left" in p.stdout, p.stdout[-300:])
    (arm / "ps3-ssdarm.binds").unlink()
    p, d = run(4, BENCH_SWAP="ssd")
    check("ssd arm: SWAP=ssd while the VRAM swap spills to USB: refused", p.returncode == 2 and "spills to usb" in p.stdout, p.stdout[-300:])
    (arm / "ps3-ssdarm.swap").write_text("ssd\n")
    p, d = run(5)
    check("ssd arm: a run without SWAP while it spills to the SSD: refused", p.returncode == 2 and "spills to ssd" in p.stdout, p.stdout[-300:])
    (tmp / "BOUND").write_text("")
    p, d = run(6, BENCH_SWAP="ssd", BENCH_GAMEDATA="ssd")
    b = report.kv(d / "bench.txt")
    check("ssd arm: GAMEDATA=ssd+SWAP=ssd: swap=ssd, swaps= from /proc/swaps, variant base@gdssd+swssd",
          p.returncode == 0 and b.get("swap") == "ssd" and "swaps" in b and report.run(d).get("variant") == "base@gdssd+swssd",
          (p.returncode, b.get("swap"), b.get("swaps"), report.run(d).get("variant"), p.stdout[-300:]))
    (arm / "ps3-ssdarm.swap").unlink()
    p, d = run(7, game="fakehud")
    b = report.kv(d / "bench.txt")
    check("ssd arm: a plain run records no arm, variant base", p.returncode == 0 and "gamedata" not in b and "swap" not in b
          and report.run(d).get("variant") == "base", (p.returncode, report.run(d).get("variant")))

    # ssd-arm.sh, dry: what it would run as root
    st = tmp / "armdry"
    st.mkdir()
    e = dict(env, SSDARM_DRY="1", BENCH_SSDARM_STATE=str(st))

    def sa(*a):
        p = subprocess.run(["bash", str(HERE / "ssd-arm.sh")] + list(a), env=e, capture_output=True, text=True)
        return p.returncode, [x[6:] for x in p.stderr.splitlines() if x.startswith("root: ")], p.stdout
    rc, cmds, out = sa("set", "fakedata", "base", "ssd", "ssd")
    want = ["mount --bind -- /mnt/ps3ssd/games/srv/a /srv/a", "mount --bind -- /mnt/ps3ssd/games/srv/b c /srv/b c",
            "timeout -s INT 180 swapoff /dev/ps3vswap0", "dmsetup suspend ps3vswap-back",
            "dmsetup load ps3vswap-back --table 0 2097152 linear /dev/mapper/ps3ssd-swap 0",
            "dmsetup resume ps3vswap-back", "mkswap /dev/ps3vswap0", "swapon -p 300 --discard=pages /dev/ps3vswap0"]
    check("ssd-arm set ssd ssd: binds, then the VRAM swap's table to the SSD swap device", rc == 0 and cmds == want, (rc, cmds, out[-400:]))
    check("ssd-arm set: state (binds, swap ssd, the boot's table kept)",
          (st / "ps3-ssdarm.binds").read_text() == "/srv/a\n/srv/b c\n" and (st / "ps3-ssdarm.swap").read_text().strip() == "ssd"
          and (st / "ps3-ssdarm.usb-table").read_text().strip() == "0 2097152 linear 8:3 0", out[-300:])
    rc, cmds, out = sa("set", "fakedata", "base", "ssd", "ssd")
    check("ssd-arm set again: nothing to do", rc == 0 and cmds == [], cmds)
    rc, cmds, out = sa("set", "fakehud", "base", "usb", "usb")
    check("ssd-arm set usb usb: binds undone, the boot's table back",
          rc == 0 and cmds[:2] == ["umount -- /srv/a", "umount -- /srv/b c"] and
          "dmsetup load ps3vswap-back --table 0 2097152 linear 8:3 0" in cmds and not (st / "ps3-ssdarm.binds").exists()
          and (st / "ps3-ssdarm.swap").read_text().strip() == "usb", (rc, cmds))
    rc, cmds, out = sa("set", "fakehud", "base", "ssd", "usb")
    check("ssd-arm set ssd for a recipe without DATA_DIRS: refused", rc == 1 and cmds == [] and "no DATA_DIRS" in out, out[-300:])
    rc, cmds, out = sa("reset")
    check("ssd-arm reset with nothing on: nothing run", rc == 0 and cmds == [], cmds)
    rc, cmds, out = sa("set", "fakedata", "base", "ssd", "usb")
    rc2, cmds2, out2 = sa("set", "fakedata", "base", "ssd", "usb")
    check("ssd-arm GAMEDATA only: binds, no swap commands; again: nothing", rc == 0 and len(cmds) == 2 and
          all(c.startswith("mount --bind") for c in cmds) and cmds2 == [], (cmds, cmds2))
    sa("reset")


# --- 5. stalls.py on a made-up record ---------------------------------------------------------
REC_HEAD = """# stallrec 1 interval_us=10000 slow=10 arch=ppc64 kernel=test locked=1 prio=nice-10
# clock mono=100.000000000 real=1000.000000000
# hz=100
# sys read=3 ioctl=54 futex=221 openat=286
# vmstat 0=pswpin 1=pswpout 2=pgmajfault 3=allocstall_normal 4=pgscan_direct 5=compact_stall 6=cma_alloc_success
# disk 0=sda 1=zram0 2=ps3vswap0 3=sda3
# other 0=Xorg 1=kswapd0
W 1 dma_fence_default_wait
W 2 futex_wait_queue
W 3 folio_wait_bit_common
D 1 /dev/dri/renderD128
P 0 pid=10 tid=10 comm=fake
"""
# each stall: the sample line (without its time) repeated over its 200 ms
REC_STALLS = [
    ("gpu", "s=S w=1 y=54 d=1 c=200"),
    ("swap_zram", "s=R c=9000 mj=40 Mj=40 v0=40 v2=40 k1=40:320:0:0:10:0"),
    ("swap_vswap", "s=D w=3 y=-1 c=100 k2=5:40:0:0:10:1"),
    ("reclaim", "s=R c=9500 v3=1 v4=500 mn=3"),
    ("gart", "s=R c=9500 v6=1 v5=1"),
    ("runq", "s=R c=1000"),
    ("disk", "s=D w=3 y=-1 c=50 k0=3:24:0:0:10:1 k3=3:24:0:0:10:1"),
    ("io", "s=D w=3 y=3 c=50 k0=2:256:0:0:10:1"),
    ("wait", "s=S w=2 y=221"),
]


def stalls_synthetic(tmp):
    import stalls
    d = tmp / "ps3-bench-20261018T020000Z-fakestall-base"
    lines, ft, t = [], [], 0.0
    # 1 s of 20 ms frames, then each stall (200 ms) followed by 0.5 s of 20 ms frames
    frames_at = []

    def normal(n):
        nonlocal t
        for _ in range(n):
            for _ in range(2):
                t += 0.01
                lines.append("%d s=R c=9000" % round(t * 1e6))
            frames_at.append(t)
            ft.append(20.0)
    normal(50)
    for _, sample in REC_STALLS:
        for _ in range(20):
            t += 0.01
            lines.append("%d %s" % (round(t * 1e6), sample))
        frames_at.append(t)
        ft.append(200.0)
        normal(25)
    # the frames as the recorder saw them: f= on the sample where each frame ended
    out, k = [], 0
    for line in lines:
        tt = int(line.split()[0]) / 1e6
        n = 0
        while k < len(frames_at) and frames_at[k] <= tt + 1e-9:
            n += 1
            k += 1
        out.append(line + (" f=%d" % n if n else ""))
    write(d / "stalls.rec", REC_HEAD + "\n".join(out) + "\n# end t=%d samples=%d late=0 maxgap_us=10000 "
          "self_user_us=1000 self_sys_us=2000 maxrss_kb=1000 stacks=0\n" % (t * 1e6, len(out)))
    write(d / "hud" / "frametime_(ms)", "".join("%g\n" % v for v in ft))
    # the client started 0.5 s before the recorder's clock started counting
    bench(d, game="fakestall", variant="base", ended="exit", seconds="%.2f" % (t + 0.5), load_end="0.60",
          hud_t1="%.2f" % (frames_at[0] + 0.5), window="0.50 %.2f" % (t + 0.5), t0="999.5")
    res = stalls.analyze(d)
    check("stalls: aligned by frames, offset -0.5 s", res and res["align"] == "frames" and
          abs(res["offset"] + 0.5) < 0.011, res and (res["align"], res["offset"]))
    got = [x["cause"] for x in sorted(res["stalls"], key=lambda x: x["at"])] if res else []
    check("stalls: %d stalls, causes in order" % len(REC_STALLS), got == [c for c, _ in REC_STALLS], got)
    if res:
        check("stalls: window seconds by cause add up to the stall seconds (1.8 s)",
              abs(sum(res["window"].values()) - 1.8) < 0.01, dict(res["window"]))
        check("stalls: t0 agrees with the frames", abs(res.get("t0_offset", 9)) < 0.02, res.get("t0_offset"))
    r = report.run(d)
    check("report: stall columns (gpu 0.2, swap 0.4, reclaim 0.4, cpu 0.2, other 0)",
          r.get("stall_gpu_s") == 0.2 and r.get("stall_swap_s") == 0.4 and r.get("stall_reclaim_s") == 0.4
          and r.get("stall_cpu_s") == 0.2 and r.get("stall_other_s") == 0.0,
          {k: v for k, v in r.items() if k.startswith("stall_")})
    res2 = tmp / "results-stalls"
    res2.mkdir()
    p = subprocess.run([sys.executable, str(HERE / "matrix.py"), "stall row", str(d)],
                       env=dict(os.environ, BENCH_RESULTS=str(res2)), capture_output=True, text=True)
    import csv
    f = res2 / "stall_gpu_s.csv"
    rows = list(csv.DictReader(f.open())) if f.exists() else []
    check("matrix: stall_gpu_s.csv made by a run with a record, cell 0.2 (column base@rec<ms>)",
          p.returncode == 0 and rows and rows[-1].get("fakestall-base@%s" % r.get("harness")) == "0.2",
          (p.stderr[-300:], rows))
    # the harness tag: inferred from the record for runs without one, bench.txt's harness= wins
    check("report: a run with a stalls.rec is variant base@rec<ms>",
          re.fullmatch(r"base@rec[\d.]+", r["variant"] or "") is not None, r["variant"])
    with (d / "bench.txt").open("a") as f:
        f.write("harness=rec10+wkkick10c1\n")
    r2 = report.run(d)
    check("report: bench.txt harness= names the variant", r2["variant"] == "base@rec10+wkkick10c1", r2["variant"])
    stalls.show(d)
    print()


# --- 6. ps3-stallrec on a fake game that stalls ----------------------------------------------
def fake_stall_game(hud, pidfile, data):
    """a game whose frames stall three ways, 300 ms each, in this order: disk, cpu, futex"""
    import mmap
    import threading
    with open(pidfile, "w") as f:
        f.write("%d\n" % os.getpid())
    ft = open(os.path.join(hud, "frametime_(ms)"), "a", buffering=1)
    last = [time.monotonic()]
    first = [True]

    def frame():
        now = time.monotonic()
        if not first[0]:
            ft.write("%.3f\n" % ((now - last[0]) * 1000))
        first[0] = False
        last[0] = now

    def normal(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            time.sleep(0.02)
            frame()
    normal(1.5)
    fd = os.open(data, os.O_RDONLY)
    size = os.fstat(fd).st_size
    os.posix_fadvise(fd, 0, size, os.POSIX_FADV_DONTNEED)
    m = mmap.mmap(fd, size, prot=mmap.PROT_READ)
    m.madvise(mmap.MADV_RANDOM)
    pages, i, end, x = size // 4096, 0, time.monotonic() + 0.3, 0
    while time.monotonic() < end:     # disk: one major fault per page
        x += m[(i * 7919 % pages) * 4096]
        i += 1
    frame()
    normal(0.5)
    end = time.monotonic() + 0.3
    while time.monotonic() < end:     # cpu
        x += 1
    frame()
    normal(0.5)
    lock = threading.Lock()
    lock.acquire()
    lock.acquire(timeout=0.3)         # futex
    frame()
    normal(0.5)
    print("faulted %d pages" % i)


def block_dev(path):
    """the partition and the disk a path is on (names in /sys/class/block), or ()"""
    st = os.stat(path)
    p = Path("/sys/dev/block/%d:%d" % (os.major(st.st_dev), os.minor(st.st_dev)))
    if not p.exists():
        return ()
    p = p.resolve()
    names = [p.name]
    if (p / "partition").exists():
        names.append(p.parent.name)
    return tuple(names)


def stallrec_e2e(tmp):
    import stalls
    src = HERE.parent.parent / "ps3-gpu-tools" / "src" / "ps3-stallrec.c"
    exe = tmp / "ps3-stallrec"
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc or not src.exists():
        check("stallrec: a C compiler and %s" % src, False, cc)
        return
    p = subprocess.run([cc, "-O2", "-Wall", "-Wextra", "-o", str(exe), str(src)], capture_output=True, text=True)
    check("stallrec: builds without warnings", p.returncode == 0 and not p.stderr.strip(), p.stderr[-800:])
    if p.returncode:
        return
    # the data file on a disk (not tmpfs): faults must read it
    ddir = None
    for c in (os.environ.get("STALLS_DISKDIR"), "/var/tmp", str(HERE)):
        if c and os.path.isdir(c) and block_dev(c):
            ddir = c
            break
    if not ddir:
        print("SKIP stallrec end to end: no disk-backed directory (STALLS_DISKDIR=...)")
        return
    data = Path(tempfile.mkstemp(prefix="stalls-selftest-", dir=ddir)[1])
    try:
        with data.open("wb") as f:
            for _ in range(64):
                f.write(os.urandom(1 << 20))
            f.flush()
            os.fsync(f.fileno())
        d = tmp / "ps3-bench-20261018T030000Z-fakestallrec-base"
        (d / "hud").mkdir(parents=True)
        args = [str(exe), "-o", str(d / "stalls.rec"), "-p", str(d / ".pid"), "-f", str(d / "hud" / "frametime_(ms)"),
                "-s", str(d / "stop"), "-x", "kswapd0", "-n"]
        for n in block_dev(ddir):
            args += ["-d", n]
        rec = subprocess.Popen(args)
        t0 = time.time()
        g = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--fake-stall-game", str(d / "hud"),
                            str(d / ".pid"), str(data)], capture_output=True, text=True)
        secs = time.time() - t0
        (d / "stop").touch()
        try:
            rec.wait(timeout=5)
        except subprocess.TimeoutExpired:
            rec.kill()
            rec.wait()
        check("stallrec: fake game ran", g.returncode == 0, g.stderr[-500:])
        print("  fake game: " + g.stdout.strip())
        # bench.txt as client.sh writes it: the whole run is the window
        b0 = report.floats(d / "hud" / "frametime_(ms)")
        bench(d, game="fakestallrec", variant="base", ended="exit", seconds="%.2f" % secs, load_end="1.00",
              hud_t1="%.2f" % (secs - sum(b0[1:]) / 1000), window="0.00 %.2f" % secs, t0="%.6f" % t0)
        res = stalls.analyze(d)
        got = [x["cause"] for x in sorted(res["stalls"], key=lambda x: x["at"])] if res else []
        check("stallrec: three stalls: disk, cpu, wait", got == ["disk", "cpu", "wait"],
              [(x["cause"], round(x["ms"]), x["evidence"][:150]) for x in (res or {}).get("stalls", [])])
        if res:
            check("stallrec: frames and t0 agree within 0.05 s", abs(res.get("t0_offset", 9)) < 0.05,
                  res.get("t0_offset"))
            e = res["rec"].end
            own = (int(e.get("self_user_us", 0)) + int(e.get("self_sys_us", 0))) / 1e6
            run_s = int(e.get("t", 1)) / 1e6
            print("  recorder: own CPU %.3f s over %.1f s (%.2f%% of one thread), %s samples, cost per "
                  "sample (us): %s" % (own, run_s, 100 * own / run_s, e.get("samples"),
                                      " ".join("%s %s" % kv for kv in res["rec"].cost.items())))
        stalls.show(d)
    finally:
        data.unlink()


# --- 7. perfprof.py on made-up perf script output -------------------------------------------
GL = "/usr/lib/powerpc64-linux-gnu/libgallium-26.2.4-1+ps3.299.so"
NOUV = "/usr/lib/powerpc64-linux-gnu/libdrm_nouveau.so.2.0.0"
LIBC = "/usr/lib/powerpc64-linux-gnu/libc.so.6"
EXE = "/usr/lib/ioquake3/ioquake3"
REND = "/usr/lib/ioquake3/renderer_opengl1_ppc64.so"
OAL = "/usr/lib/powerpc64-linux-gnu/libopenal.so.1.25.2"
# (count, pid, tid, comm, leaf first: (sym, dso)...)
PERF_SAMPLES = [
    (10, 1000, 1000, "ioquake3", [("RB_RenderThread", EXE), ("main", EXE)]),
    (5, 1000, 1000, "ioquake3", [("R_DrawElements", REND), ("RB_RenderThread", EXE), ("main", EXE)]),
    (20, 1000, 1000, "ioquake3", [("nv30_draw_vbo", GL), ("R_DrawElements", REND), ("main", EXE)]),
    (5, 1000, 1000, "ioquake3", [("pushbuf_krel", NOUV), ("nv30_draw_vbo", GL), ("main", EXE)]),
    (6, 1000, 1000, "ioquake3", [("ps3rsx_validate", "[ps3rsx]"), ("drm_ioctl", "[kernel.kallsyms]"),
                                 ("ioctl", LIBC), ("pushbuf_kick", NOUV), ("main", EXE)]),
    (2, 1000, 1000, "ioquake3", [("drm_ioctl", "[kernel.kallsyms]"), ("ioctl", LIBC), ("main", EXE)]),
    (4, 1000, 1000, "ioquake3", [("do_page_fault", "[kernel.kallsyms]"), ("main", EXE)]),
    (3, 1000, 1000, "ioquake3", [("memcpy", LIBC), ("nv30_transfer", GL), ("main", EXE)]),
    (4, 1000, 1000, "ioquake3", [("Mix_C", OAL), ("main", EXE)]),
    (1, 1000, 1000, "ioquake3", [("[unknown]", "[unknown]")]),
    (2, 1000, 1003, "alsoft-mixer", [("Resample_Cubic_C", OAL), ("start_thread", LIBC)]),
    (7, 900, 900, "Xorg", [("glamor_poly", "/usr/lib/xorg/modules/libglamoregl.so"), ("main", "/usr/lib/xorg/Xorg")]),
    (3, 50, 50, "kswapd0", [("zram_write", "[kernel.kallsyms]"), ("kswapd", "[kernel.kallsyms]")]),
]


def perf_script_text(samples):
    out, t = [], 100.0
    for n, pid, tid, comm, frames in samples:
        for _ in range(n):
            t += 0.002
            out.append("%s  %d/%d  %.6f:    2004008 " % (comm, pid, tid, t))
            for i, (sym, dso) in enumerate(frames):
                out.append("\t    %x %s (%s)" % (0x10000000 + 16 * i, sym, dso))
            out.append("")
    return "\n".join(out) + "\n"


def irqoff_synthetic(tmp):
    """irqoff.py on made-up folded stacks and irq/ snapshots: each replay sample to its site, the
    shares by hand, the rates over the window"""
    import io
    import contextlib
    import irqoff
    d = tmp / "ps3-bench-20261019T080000Z-fakeirq-base"
    (d / "irq").mkdir(parents=True)
    R = ".arch_local_irq_restore_[k];.__replay_soft_interrupts_[k]"
    write(d / "perf.folded", "\n".join([
        "game;[libgallium.so];[libc.so.6];interrupt_return_srr_user_[k];.interrupt_exit_user_prepare_[k];"
        ".interrupt_exit_user_prepare_main_[k];[unknown];" + R + " 30",
        "game;[libgallium.so];__munmap;system_call_common_[k];.system_call_exception_[k];.sys_munmap_[k];"
        ".__vm_munmap_[k];.unmap_vmas_[k];.unmap_page_range_[k];.flush_hash_page_[k];.ps3_hpte_invalidate_[k];"
        "._raw_spin_unlock_irqrestore_[k];[unknown];" + R + " 10",
        "kswapd0;.kthread_[k];.kswapd_[k];.shrink_node_[k];.flush_hash_page_[k];.ps3_hpte_invalidate_[k];"
        "._raw_spin_unlock_irqrestore_[k];[unknown];" + R + " 5",
        "game;[libasound.so];ioctl;system_call_common_[k];.system_call_exception_[k];.sys_ioctl_[k];"
        ".snd_pcm_ioctl_[k];.snd_pcm_delay_[k];.snd_pcm_group_unlock_irq_[k];[unknown];" + R + " 4",
        "game;[libc.so.6];system_call_common_[k];.system_call_exception_[k];[unknown];" + R + " 3",
        "game;game_main;draw 48",
        "Xorg;[Xorg] 5",
    ]) + "\n")
    write(d / "irq" / "start.info", "label=start real=1000.0\n")
    write(d / "irq" / "end.info", "label=end real=1100.0\n")
    write(d / "irq" / "start.interrupts", "  CPU0 CPU1\n 16:  100 0  PS3-IRQ  vsync\nLOC:  1000 2000  Local timer interrupts\n")
    write(d / "irq" / "end.interrupts", "  CPU0 CPU1\n 16:  6100 0  PS3-IRQ  vsync\nLOC:  26000 52000  Local timer interrupts\n")
    write(d / "irq" / "start.htab", "inserts 1000 insert_us 2000 evictions 0 readbacks 1000 readback_us 1000 "
          "mismatches 0 invalidates 0 invalidate_us 0 updatepps 0 updatepp_us 0\n")
    write(d / "irq" / "end.htab", "inserts 101000 insert_us 202000 evictions 500 readbacks 1000 readback_us 1000 "
          "mismatches 0 invalidates 50000 invalidate_us 100000 updatepps 0 updatepp_us 0\n")
    tot, game, by, users, other = irqoff.folded(str(d))
    check("irqoff: 105 samples, 95 the game's (not Xorg's or kswapd0's)", tot == 105 and game == 95, (tot, game))
    check("irqoff: hash faults 30, munmap 10, reclaim 5 (kswapd), alsa 4, syscall 3",
          by[("hash fault (user)", True)] == 30 and by[("hpte inval munmap", True)] == 10 and
          by[("hpte inval reclaim", False)] == 5 and by[("alsa pcm lock", True)] == 4 and
          by[("syscall entry/exit", True)] == 3, dict(by))
    check("irqoff: the faulting user frame is libc's", users.get("[libc.so.6]") == 30, dict(users))
    dt, irq, hrate = irqoff.window(str(d))
    rates = dict(irq)
    check("irqoff: 100 s window, vsync 60/s, timer 750/s", dt == 100 and rates.get("16 vsync") == 60 and
          rates.get("LOC") == 750, (dt, rates))
    check("irqoff: htab 1000 inserts/s at 2 us, 500 invalidates/s", hrate["inserts"] == 1000 and
          hrate["insert_us"] == 2000 and hrate["invalidates"] == 500, hrate)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        irqoff.run(str(d))
    check("irqoff: the report prints the shares and the hcall line", "31.58" in out.getvalue() and
          "inserts 1000 (2.0 us each)" in out.getvalue(), out.getvalue()[-400:])


def perf_synthetic(tmp):
    import perfprof
    d = tmp / "ps3-bench-20261018T040000Z-fakeperf-base"
    d.mkdir(parents=True)
    write(d / "perf.targets", "game=1000 xorg=900 kswapd0=50 hz=499\n")
    write(d / "perf.exe", EXE + "\n")
    write(d / "perf-pkgs.txt", "%s\tioquake3\n%s\tioquake3\n%s\tlibc6:ppc64\n" % (EXE, REND, LIBC))
    bench(d, game="fakeperf", variant="base", ended="window", seconds="200", load_end="10.00",
          window="70.00 190.00", perf_win="70.00 190.00", perf_cpu="1.20")
    import io
    n = perfprof.collapse(io.StringIO(perf_script_text(PERF_SAMPLES)), d)
    check("perf: %d samples collapsed" % n, n == sum(x[0] for x in PERF_SAMPLES), n)
    folded = (d / "perf.folded").read_text()
    check("perf: a stack folded outermost first, kernel frames _[k]",
          "ioquake3;main;pushbuf_kick;ioctl;drm_ioctl_[k];ps3rsx_validate_[k] 6\n" in folded, folded[:400])
    check("perf: a thread's stacks under its own name", "alsoft-mixer;start_thread;Resample_Cubic_C 2\n" in folded)
    r = perfprof.summary(d)
    # the game: 62 samples by leaf: game 10+5, mesa 20, drm 5, kgpu 6+2, kernel 4, libc 3 (memcpy), other
    # 4+2, unk 1
    want = {"perf_n": 62, "perf_game": 24.2, "perf_mesa": 32.3, "perf_drm": 8.1, "perf_kgpu": 12.9,
            "perf_kernel": 6.5, "perf_libc": 4.8, "perf_other": 9.7, "perf_unk": 1.6,
            "perf_xorg": 11.3, "perf_kswapd": 4.8, "perf_self": 1.0}
    check("perf: summary columns as by hand", all(r.get(k) == v for k, v in want.items()),
          {k: (r.get(k), v) for k, v in want.items() if r.get(k) != v})
    # the system-wide record: one line per sample, comm right-aligned
    sys_lines = []
    for n, comm, pid, sym in ((60, "swapper", 0, "cpu_idle"), (30, "ioquake3", 1000, "main"), (5, "Xorg", 900, "main"),
                              (3, "kworker/0:1", 12, "worker_thread"), (2, "kswapd0", 50, "kswapd")):
        for i in range(n):
            sys_lines.append("%16s %6d/%-6d %.6f:    2004008      %x %s (%s)" % (comm, pid, pid, 100 + i / 1000, 0xc0000000 + i,
                                                                             sym, "[kernel.kallsyms]"))
    perfprof.collapse(io.StringIO("\n".join(sys_lines) + "\n"), d, sysmode=True)
    r = report.run(d)
    check("perf: system-wide shares (idle 60, game 30, xorg 5, kworker 3, kswapd 2)",
          (r.get("perf_sys_idle"), r.get("perf_sys_game"), r.get("perf_sys_xorg"), r.get("perf_sys_kworker"),
           r.get("perf_sys_kswapd")) == (60.0, 30.0, 5.0, 3.0, 2.0),
          {k: v for k, v in r.items() if k.startswith("perf_sys")})
    line = report.show(r)
    check("report: perf columns shown, mem columns not", "perf_mesa=32.3" in line and "mem_rss" not in line, line[-300:])
    perfprof.show(d)
    print()


def perf_fill(tmp):
    """7b. the 2026-10-18 batch's gaps: frames perf left [unknown] (a library dlopened while perf was
    disabled) filled from the MEM=1 smaps and the library's symbols, the link register entry perf
    could not check dropped, ppc64's dot names for the kernel's DRM functions and the [drm] module
    counted as kgpu, the recipe command's package (versions) counted as the game"""
    import io
    import perfprof
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        check("perf fill: a C compiler", False)
        return
    d = tmp / "ps3-bench-20261018T050000Z-fakefill-base"
    (d / "mem").mkdir(parents=True)
    lib = d / "libfakegl.so"
    src = d / "fakegl.c"
    write(src, "volatile int v;\n__attribute__((noinline)) void fake_leaf(void) { for (int i = 0; i < 9; i++) v += i; }\n"
          "__attribute__((noinline)) void fake_draw(void) { fake_leaf(); v++; fake_leaf(); }\n")
    p = subprocess.run([cc, "-O1", "-fPIC", "-shared", "-o", str(lib), str(src)], capture_output=True, text=True)
    if p.returncode:
        check("perf fill: the library builds", False, p.stderr[-400:])
        return
    sys.path.insert(0, str(HERE / "hotpaths"))
    import elfsym
    el = elfsym.Elf(str(lib))
    sym = {n: v for n, _, v, _ in el.symbols()}
    # the mapping as smaps shows it: the whole file executable from BASE (offset 0), so a symbol at
    # vaddr V is at BASE + its file offset
    def off(va):
        for typ, _, poff, pva, filesz, _ in el.phdrs:
            if typ == 1 and pva <= va < pva + filesz:
                return va - pva + poff
        return va
    base = 0x3fff70000000
    size = (lib.stat().st_size + 0xffff) & ~0xffff
    write(d / "mem" / "mid.smaps", "%x-%x r-xp 00000000 08:02 4242   %s\nSize: 64 kB\n" % (base, base + size, lib))
    write(d / "perf.targets", "game=1000 xorg=900 kswapd0=50 hz=499\n")
    write(d / "perf.exe", EXE + "\n")
    cg = "/usr/lib/openarena/baseoa/pak6-patch088/cgameppc64.so"
    write(d / "perf-pkgs.txt", "%s\tioquake3\n%s\topenarena\n" % (EXE, cg))
    write(d / "versions", "libopenal1:ppc64\t1:1.25.2-1+ps3.273\nmesa-libgallium:ppc64\t26.2.4-1+ps3.299\n"
          "openarena\t0.8.8+dfsg-13\nps3-gpu-tools\t0.412\n6.12.111+ps3.178\n")
    leaf = base + off(sym["fake_leaf"]) + 4
    ret = base + off(sym["fake_draw"]) + 8      # a return address into fake_draw
    lr_own = base + off(sym["fake_leaf"]) + 8   # the link register still pointing into the leaf
    mainip = 0x10001000
    lines, t = [], 100.0

    def sample(n, pid, comm, frames):
        nonlocal t
        for _ in range(n):
            t += 0.002
            lines.append("%s  %d/%d  %.6f:    2004008 " % (comm, pid, pid, t))
            lines.extend("\t    %x %s (%s)" % f for f in frames)
            lines.append("")
    sample(6, 1000, "ioquake3", [(leaf, "[unknown]", "[unknown]"), (lr_own, "[unknown]", "[unknown]"),
                                 (ret, "[unknown]", "[unknown]"), (mainip, "main", EXE)])
    sample(3, 1000, "ioquake3", [(leaf, "[unknown]", "[unknown]"), (ret, "[unknown]", "[unknown]"),
                                 (ret, "[unknown]", "[unknown]"), (mainip, "main", EXE)])
    sample(4, 1000, "ioquake3", [(0xc000000000123450, ".drm_ioctl", "[kernel.kallsyms]"),
                                 (0xc000000000123460, ".ps3rsx_pushbuf", "[kernel.kallsyms]"), (leaf, "[unknown]", "[unknown]")])
    sample(2, 1000, "ioquake3", [(0xd000000000001000, "[unknown]", "[drm]"), (mainip, "main", EXE)])
    sample(3, 1000, "ioquake3", [(0x3fff60001000, "CG_DrawActive", cg), (mainip, "main", EXE)])
    sample(1, 1000, "ioquake3", [(0x7fff00000000, "[unknown]", "[unknown]"), (mainip, "main", EXE)])
    sample(2, 900, "Xorg", [(leaf, "[unknown]", "[unknown]")])
    n = perfprof.collapse(io.StringIO("\n".join(lines) + "\n"), d)
    check("perf fill: %d samples collapsed" % n, n == 21, n)
    folded = (d / "perf.folded").read_text()
    # 6 with the LR entry in the leaf's own function, 3 with it equal to the next entry: both dropped
    check("perf fill: the leaf and its caller named from the library, the unchecked LR entries dropped",
          "ioquake3;main;fake_draw;fake_leaf 9\n" in folded, folded)
    check("perf fill: a kernel sample's user address filled, not deduplicated",
          "ioquake3;fake_leaf;.ps3rsx_pushbuf_[k];.drm_ioctl_[k] 4\n" in folded, folded)
    check("perf fill: an address in no mapping stays [unknown], other processes untouched",
          "ioquake3;main;[unknown] 1\n" in folded and "Xorg;[unknown] 2\n" in folded, folded)
    head = (d / "perf-leaf.txt").read_text()
    check("perf fill: perf-leaf.txt names the filled leaves", "# filled_from_smaps=libfakegl.so:9\n" in head
          and "\t1000\tioquake3\t%s\tfake_leaf\n" % lib in head, head)
    r = perfprof.summary(d)
    # 19 game samples: other (the library, outside the game's directories) 9, kgpu 4 (.drm_ioctl) + 2 ([drm]),
    # game 3 (cgame: openarena from versions), unk 1
    want = {"perf_n": 19, "perf_other": 47.4, "perf_kgpu": 31.6, "perf_game": 15.8, "perf_unk": 5.3, "perf_kernel": 0.0}
    check("perf fill: classes (dot names and [drm] as kgpu, the recipe's package as game)",
          all(r.get(k) == v for k, v in want.items()), {k: (r.get(k), v) for k, v in want.items() if r.get(k) != v})
    perfprof.show(d)
    print()


# --- 8. client.sh with PERF and MEM ---------------------------------------------------------
def fake_perf(ctl, ack, log, delay=0.0):
    """perf record --control's side: reads enable/disable lines, writes ack for each (the first one
    delay s late: perf attaching)"""
    import threading
    fd = os.open(ctl, os.O_RDWR)
    afd = os.open(ack, os.O_RDWR)

    def run():
        buf = b""
        while True:
            b = os.read(fd, 256)
            if not b:
                return
            buf += b
            while b"\n" in buf:
                cmd, buf = buf.split(b"\n", 1)
                if delay and not log:
                    time.sleep(delay)
                log.append((time.time(), cmd.decode()))
                os.write(afd, b"ack\n")
                if cmd == b"stop":
                    return
    th = threading.Thread(target=run, daemon=True)
    th.start()
    return fd, afd, th


def client_prof(tmp):
    import memprof
    cc = shutil.which("cc") or shutil.which("gcc")
    so = tmp / "ps3-mallocstat.so"
    src = HERE.parent.parent / "ps3-gpu-tools" / "preload" / "ps3-mallocstat.c"
    p = subprocess.run([cc, "-O2", "-Wall", "-Wextra", "-fPIC", "-shared", "-o", str(so), str(src), "-lpthread"],
                       capture_output=True, text=True) if cc else None
    check("mallocstat: builds without warnings", p is not None and p.returncode == 0 and not p.stderr.strip(),
          p and p.stderr[-600:])
    trim = tmp / "ps3-malloctrim.so"
    src = HERE.parent.parent / "ps3-gpu-tools" / "preload" / "ps3-malloctrim.c"
    p = subprocess.run([cc, "-O2", "-Wall", "-Wextra", "-fPIC", "-shared", "-o", str(trim), str(src), "-lpthread"],
                       capture_output=True, text=True) if cc else None
    check("malloctrim: builds without warnings", p is not None and p.returncode == 0 and not p.stderr.strip(),
          p and p.stderr[-600:])
    rdir = tmp / "recipes-prof"
    rdir.mkdir()
    fake = tmp / "fake-game"   # made by client()
    comm = Path(sys.executable).name[:15]
    write(rdir / "fakeprof.sh", 'CMD=("%s" --fake-game hudwin); COMM=%s; LOAD_MARK=\'^loaded\'; WARMUP=2; MEASURE=6\n'
          'WINDOW_STOP=1; DURATION=25\n' % (fake, comm))
    d = tmp / "ps3-bench-20261018T050000Z-fakeprof-base"
    d.mkdir()
    os.mkfifo(d / ".perf.ctl")
    os.mkfifo(d / ".perf.ack")
    write(d / ".perf.pid", "%d\n" % os.getpid())
    log = []
    kpd = tmp / "kparams"
    kpd.mkdir(exist_ok=True)
    write(kpd / "test_param", "N\n")
    kph = tmp / "kparams-htab"   # the kernel's ps3_htab parameters (local/0046)
    kph.mkdir(exist_ok=True)
    write(kph / "insert_readback", "1\n")
    write(kph / "stats", "inserts 10 insert_us 20 evictions 0 readbacks 10 readback_us 10 mismatches 0 "
          "invalidates 0 invalidate_us 0 updatepps 0 updatepp_us 0\n")
    kps = tmp / "kparams-snd"   # snd_ps3's parameters (local/0047)
    kps.mkdir(exist_ok=True)
    write(kps / "dma_watchdog_ms", "200\n")
    write(kps / "dma_stats", "empty=100 both=0 uonly=0 none=0 restarts=2 last_kick=0,0,0,0,0,0,0,0 "
          "last_ax_is=0 last_intr0=0 last_3wmctrl=0\n")
    fds = fake_perf(str(d / ".perf.ctl"), str(d / ".perf.ack"), log)
    env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                            "GALLIUM_HUD_DUMP_DIR", "LD_PRELOAD")}
    env.update(BENCH_RECIPES=str(rdir), BENCH_BACKUP=str(tmp / "backup"), BENCH_DIR=str(d), BENCH_MEM="1",
               BENCH_MALLOCSTAT=str(so), BENCH_PERF_CTL="%s:%s" % (d / ".perf.ctl", d / ".perf.ack"),
               BENCH_MALLOC="arena2,trim", BENCH_MALLOCTRIM=str(trim), BENCH_MALLOC_TRIM_S="1",   # 1 s: not inside the fake game's split line (0-0.7 s)
               BENCH_NV30_TEST_SWITCH="16",   # a Mesa driver switch (docs/HOTPATHS.md): to the game as NV30_TEST_SWITCH
               BENCH_PS3RSX_TEST_PARAM="1", BENCH_KPARAM_DIR=str(kpd),   # a ps3rsx parameter, put back after
               BENCH_PS3HTAB_INSERT_READBACK="0", BENCH_KPARAM_HTAB_DIR=str(kph),   # and a ps3_htab one
               BENCH_SNDPS3_DMA_WATCHDOG_MS="0", BENCH_KPARAM_SND_DIR=str(kps))   # and snd_ps3's
    t0 = time.time()
    p = subprocess.run(["bash", str(HERE / "client.sh"), "fakeprof", "base"], env=env, capture_output=True, text=True)
    check("prof client: exit 0", p.returncode == 0, p.stdout[-800:] + p.stderr[-800:])
    os.close(fds[0])
    b = report.kv(d / "bench.txt")
    le = float(b.get("load_end") or 0)
    w = [float(x) for x in (b.get("perf_win") or "").split()]
    check("perf: on at load + WARMUP, off at + WARMUP + MEASURE",
          len(w) == 2 and abs(w[0] - le - 2) < 0.1 and abs(w[1] - le - 8) < 0.15, (le, w))
    check("perf: the fake perf got enable, then disable, each acknowledged",
          [c for _, c in log] == ["enable", "disable"] and b.get("perf_ack") == "ok", (log, b.get("perf_ack")))
    if len(log) == 2 and len(w) == 2:
        check("perf: client stamps agree with the fake perf's clock",
              abs(log[0][0] - t0 - w[0]) < 0.3 and abs(log[1][0] - t0 - w[1]) < 0.3,
              [x - t0 for x, _ in log])
    check("perf: perf_cpu written", b.get("perf_cpu") not in (None, ""), b.get("perf_cpu"))
    snaps = dict(x.split(":", 1) for x in (b.get("mem_snaps") or "").split())
    check("mem: start, mid and end snapshots", sorted(snaps) == ["end", "mid", "start"], b.get("mem_snaps"))
    at = {k: float(v.split(":")[0]) - le - 2 for k, v in snaps.items()}
    check("mem: at window + 1, + 3, + 4 s", abs(at.get("start", 9) - 1) < 0.2 and abs(at.get("mid", 9) - 3) < 0.2
          and abs(at.get("end", 9) - 4) < 0.2, at)
    mem = d / "mem"
    for lb in ("start", "mid", "end"):
        have = [x for x in ("smaps", "rollup", "status", "tasks", "info", "meminfo", "malloc") if (mem / f"{lb}.{x}").exists()]
        check("mem: %s has smaps, rollup, status, tasks, info, meminfo, malloc" % lb, len(have) == 7, have)
    tk = (mem / "mid.tasks").read_text() if (mem / "mid.tasks").exists() else ""
    check("malloc presets: ps3-mallocstat and ps3-malloctrim threads both in the game",
          "\tps3-mallocstat\t" in tk and "\tps3-malloctrim\t" in tk, tk[-400:])
    gl = (d / "game.log").read_text(errors="replace") if (d / "game.log").exists() else ""
    check("malloc presets: trim log lines in game.log", len(re.findall(r"^ps3-malloctrim: \d+ ", gl, re.M)) >= 4,
          gl[-300:])
    check("malloc presets and driver switches: malloc= and genv= in bench.txt, the tags after the variant",
          b.get("malloc") == "arena2,trim" and b.get("genv") == "NV30_TEST_SWITCH=16" and
          report.run(d).get("variant") == "base@marena2_trim+test_switch16+rsx_test_param1_htab_insert_readback0_snd_dma_watchdog_ms0",
          (b.get("malloc"), b.get("genv"), report.run(d).get("variant")))
    check("driver switches: the game got NV30_TEST_SWITCH=16", "env NV30_TEST_SWITCH=16" in gl, gl[:300])
    check("ps3rsx parameters: kparam= in bench.txt, the tag after the variant, the old value back",
          (b.get("kparam") or "").split()[:1] == ["test_param=1"] and "rsx_test_param1" in (report.run(d).get("variant") or "") and
          (kpd / "test_param").read_text() == "N\n",
          (b.get("kparam"), report.run(d).get("variant"), (kpd / "test_param").read_text()))
    check("snd_ps3 parameters: kparam snd_dma_watchdog_ms=0, the old value back; snd_restarts= from dma_stats",
          "snd_dma_watchdog_ms=0" in (b.get("kparam") or "") and (kps / "dma_watchdog_ms").read_text() == "200\n"
          and b.get("snd_restarts") == "0" and b.get("snd_wd") == "0" and b.get("snd_stall") == "0",
          (b.get("kparam"), (kps / "dma_watchdog_ms").read_text(), b.get("snd_restarts"), b.get("snd_wd"), b.get("snd_stall")))
    check("ps3_htab parameters: kparam htab_insert_readback=0, the old value back",
          "htab_insert_readback=0" in (b.get("kparam") or "") and (kph / "insert_readback").read_text() == "1\n",
          (b.get("kparam"), (kph / "insert_readback").read_text()))
    check("irq snapshots at perf's on and off: interrupts and the htab stats",
          all((d / "irq" / ("%s.%s" % (w, x))).exists() for w in ("start", "end") for x in ("info", "interrupts", "htab")),
          sorted(p.name for p in (d / "irq").glob("*")) if (d / "irq").exists() else "no irq/")
    ml = (mem / "mid.malloc").read_text() if (mem / "mid.malloc").exists() else ""
    check("mallocstat: mallinfo2 line and malloc_info XML", ml.startswith("mallinfo2 ") and '<heap nr="0">' in ml, ml[:200])
    r = memprof.summary(d)
    check("mem: summary: rss, heap, malloc in use, arenas, smaps read time",
          r.get("mem_rss", 0) > 3 and r.get("mem_heap_rss", 0) > 0 and r.get("mem_malloc_used", 0) > 0
          and r.get("mem_arenas", 0) >= 1 and r.get("mem_smaps_ms") is not None,
          {k: v for k, v in r.items() if k in ("mem_rss", "mem_heap_rss", "mem_malloc_used", "mem_arenas", "mem_smaps_ms")})
    st = memprof.snapshot(d, "mid")
    if st:
        tot = sum(st["tot"][c]["Rss"] for c in memprof.CLASSES)
        roll = 0
        for line in (mem / "mid.rollup").read_text().splitlines():
            if line.startswith("Rss:"):
                roll = int(line.split()[1])
        check("mem: the classes add up to smaps_rollup's Rss (within 2%)", roll and abs(tot - roll) <= 0.02 * roll,
              (tot, roll))
        check("mem: the game's interpreter classified as game", st["tot"]["game"]["Rss"] > 0, st["tot"]["game"])
    print("  mem snapshot cost: smaps read in %s ms (python, %.0f MiB Rss)" % (r.get("mem_smaps_ms"), r.get("mem_rss", 0)))
    memprof.show(d)
    print()


# --- 9. memprof.py on a real process ---------------------------------------------------------
THR_C = r"""
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static pthread_barrier_t b;
static void *work(void *a) {
	void **out = a; pthread_attr_t at; void *sa; size_t ss;
	out[0] = malloc(1000);	/* from this thread's arena */
	pthread_getattr_np(pthread_self(), &at); pthread_attr_getstack(&at, &sa, &ss);
	out[1] = (char *)sa + ss / 2;
	pthread_barrier_wait(&b); sleep(2); return 0;
}
int main(void) {
	enum { N = 4 }; pthread_t t[N]; void *o[N][2];
	pthread_barrier_init(&b, 0, N + 1);
	for (int i = 0; i < N; i++) pthread_create(&t[i], 0, work, o[i]);
	pthread_barrier_wait(&b);
	void *small = malloc(100);
	for (int i = 0; i < N; i++) printf("heap %p\nstack %p\n", o[i][0], o[i][1]);
	printf("heap %p\nready\n", small); fflush(stdout);
	for (int i = 0; i < N; i++) pthread_join(t[i], 0);
	return 0;
}
"""


def memprof_real(tmp):
    import glob
    import memprof
    cc = shutil.which("cc") or shutil.which("gcc")
    src, exe = tmp / "thr.c", tmp / "thr"
    write(src, THR_C)
    p = subprocess.run([cc, "-O2", "-o", str(exe), str(src), "-lpthread"], capture_output=True, text=True)
    if p.returncode:
        check("memprof: test program builds", False, p.stderr[-400:])
        return
    g = subprocess.Popen([str(exe)], stdout=subprocess.PIPE, text=True)
    truth = []
    for line in g.stdout:
        if line.strip() == "ready":
            break
        truth.append(line.split())
    sm = Path("/proc/%d/smaps" % g.pid).read_text()
    tasks = ""
    for t in sorted(glob.glob("/proc/%d/task/*" % g.pid)):
        tasks += "%s\t%s\t%s\n" % (t.rsplit("/", 1)[1], Path(t, "comm").read_text().strip(),
                                    Path(t, "syscall").read_text().strip())
    g.wait()
    write(tmp / "thr.tasks", tasks)
    maps = memprof.classify(memprof.parse_smaps(sm), str(exe), memprof.stack_pointers(tmp / "thr.tasks"))
    got = [(want, [m["cls"] for m in maps if m["start"] <= int(a, 16) < m["end"]]) for want, a in truth]
    check("memprof: 4 thread stacks and 5 malloc arenas (main + 4) found in a real process",
          all(c == [w] for w, c in got), got)
    check("memprof: the executable classified as game", any(m["cls"] == "game" for m in maps))


# --- 10. ps3-malloctrim.so on a real process -------------------------------------------------
FRAG_C = r"""
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void *work(void *a) {	/* 48 MB in a thread's arena, 98% freed in holes */
	enum { N = 3000 }; static char *p[N]; (void)a;
	for (int i = 0; i < N; i++) { p[i] = malloc(16000); memset(p[i], 1, 16000); }
	for (int i = 0; i < N; i++) if (i % 50) free(p[i]);
	usleep(1500000); return 0;
}
int main(void) {	/* and 40 MB in the main heap, the same way */
	enum { M = 2000 }; static char *m[M]; pthread_t t;
	pthread_create(&t, 0, work, 0);
	for (int i = 0; i < M; i++) { m[i] = malloc(20000); memset(m[i], 2, 20000); }
	for (int i = 0; i < M; i++) if (i % 40) free(m[i]);
	pthread_join(t, 0);
	return 0;
}
"""


def malloctrim_real(tmp):
    cc = shutil.which("cc") or shutil.which("gcc")
    so, src, exe = tmp / "trim.so", tmp / "frag.c", tmp / "frag"
    write(src, FRAG_C)
    p = subprocess.run([cc, "-O2", "-o", str(exe), str(src), "-lpthread"], capture_output=True, text=True)
    q = subprocess.run([cc, "-O2", "-fPIC", "-shared", "-o", str(so),
                        str(HERE.parent.parent / "ps3-gpu-tools" / "preload" / "ps3-malloctrim.c"), "-lpthread"],
                       capture_output=True, text=True)
    if p.returncode or q.returncode:
        check("malloctrim: test program and library build", False, (p.stderr + q.stderr)[-400:])
        return
    env = dict(os.environ, LD_PRELOAD=str(so), PS3_MALLOC_TRIM="0.4", PS3_MALLOC_TRIM_LOG="1",
               PS3_MALLOC_TRIM_COMM="frag")
    r = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=30)
    lines = re.findall(r"^ps3-malloctrim: \d+ ([0-9.]+) ms rss (\d+) -> (\d+)", r.stderr, re.M)
    drop = max((int(a) - int(b) for _, a, b in lines), default=0)
    check("malloctrim: trims ran in the named process and gave back the holes (> 60 MB of 88)",
          len(lines) >= 2 and drop > 60000, r.stderr[-400:])
    env["PS3_MALLOC_TRIM_COMM"] = "other"
    r = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=30)
    check("malloctrim: nothing in a process of another name", "ps3-malloctrim" not in r.stderr, r.stderr[-200:])
    print("  malloctrim: first trim %s ms for %d MB (host)" % (lines[0][0] if lines else "-", drop // 1024))


# --- 4. every recipe and variant ---------------------------------------------------------------
LINT = r'''
here=$1; ASSETS=$here/assets; D=/nonexistent; g=$2
for v in base $(grep -o '^variant_[a-z0-9_]*' "$here/recipes/$g.sh" | sed 's/^variant_//'); do
	(
	CMD=(); ENV=(); BACKUP=(); GAMEMODE=0; HUD=1; COMM=; WM=openbox
	END_RE=; DURATION=270; HUD_SKIP=12; WARMUP=60; MEASURE=120; PASSES=0; WINDOW_STOP=0; STEADY_RE=
	STEADY_MAXMS_RE=; FPS_RE=; MAXMS_RE=; LOAD_RE=; LOAD_MARK=; RESULT_FILE=; SOUND_AT=15
	setup() { :; }; parse() { :; }
	. "$here/recipes/$g.sh" || exit 3
	[ "$v" = base ] || "variant_$v" || exit 4
	[ ${#CMD[@]} -gt 0 ] || exit 5
	how=hud; [ "$PASSES" = 1 ] && how=passes; [ -n "$STEADY_RE" ] && how=own; [ "$HUD" = 1 ] || how=none
	echo "$g $v $DURATION $WARMUP $MEASURE $how $WINDOW_STOP"
	) || echo "$g $v ERROR $?"
done
'''


# --- 12. the start-up profile: loadprof.py on made-up data, client.sh's gate -----------------
LP_EXE = "/usr/lib/ioquake3/ioquake3"
LP_GL = "/usr/lib/powerpc64-linux-gnu/libgallium-26.2.4-1+ps3.299.so"
LP_LIBC = "/usr/lib/powerpc64-linux-gnu/libc.so.6"
LP_LD = "/usr/lib/powerpc64-linux-gnu/ld64.so.2"
LP_K = "[kernel.kallsyms]"
# (samples, phase, pid, tid, comm, the group expected, frames leaf first: (sym, dso))
LP_SAMPLES = [
    (50, "load", 2000, 2000, "ioquake3", "zlib", [("inflate_fast", "/usr/lib/powerpc64-linux-gnu/libz.so.1.3.1"),
                                                  ("unzReadCurrentFile", LP_EXE), ("FS_ReadFile", LP_EXE), ("main", LP_EXE)]),
    (20, "load", 2000, 2000, "ioquake3", "png", [("png_read_filter_row_paeth4", "/usr/lib/powerpc64-linux-gnu/libpng16.so.16"),
                                                 ("png_read_row", "/usr/lib/powerpc64-linux-gnu/libpng16.so.16"),
                                                 ("R_LoadImage", LP_EXE), ("main", LP_EXE)]),
    (30, "load", 2000, 2000, "ioquake3", "link", [("_dl_relocate_object", LP_LD), ("dl_main", LP_LD)]),
    (10, "load", 2000, 2000, "ioquake3", "link", [(".do_page_fault", LP_K), ("_dl_relocate_object", LP_LD), ("dl_main", LP_LD)]),
    (15, "load", 2000, 2000, "ioquake3", "tex", [("memcpy", LP_LIBC), ("_mesa_texstore_rgba8888", LP_GL),
                                                 ("st_TexImage", LP_GL), ("qglTexImage2D", LP_EXE), ("main", LP_EXE)]),
    (25, "load", 2000, 2000, "ioquake3", "shader.glsl", [
        ("(anonymous namespace)::ir_constant_propagation_visitor::visit(ir_assignment*)", LP_GL),
        ("do_common_optimization(exec_list*, bool)", LP_GL), ("_mesa_glsl_link_shader", LP_GL), ("main", LP_EXE)]),
    (10, "load", 2000, 2000, "ioquake3", "shader.glsl", [("nir_opt_algebraic", LP_GL), ("st_link_glsl_to_nir", LP_GL),
                                                         ("st_link_shader", LP_GL), ("main", LP_EXE)]),
    (12, "load", 2000, 2000, "ioquake3", "shader.backend", [("ntt_emit_alu", LP_GL), ("nir_to_tgsi_options", LP_GL),
                                                            ("nv30_fp_state_create", LP_GL), ("st_create_fp_variant", LP_GL),
                                                            ("st_link_shader", LP_GL), ("main", LP_EXE)]),
    (8, "load", 2000, 2001, "disk$0", "shader.cache", [("ZSTD_compressBlock_internal", "/usr/lib/powerpc64-linux-gnu/libzstd.so.1.5.7"),
                                                        ("util_compress_deflate", LP_GL), ("cache_put_job", LP_GL),
                                                        ("util_queue_thread_func", LP_GL)]),
    (6, "load", 2000, 2000, "ioquake3", "k.read", [(".__copy_tofrom_user", LP_K), (".filemap_read", LP_K), (".ksys_read", LP_K),
                                                   ("read", LP_LIBC), ("FS_Read", LP_EXE), ("main", LP_EXE)]),
    (5, "load", 2000, 2000, "ioquake3", "k.fault", [(".clear_user_page", LP_K), (".handle_mm_fault", LP_K),
                                                    (".do_page_fault", LP_K), ("R_ResampleTexture", LP_EXE), ("main", LP_EXE)]),
    (9, "load", 2000, 2000, "ioquake3", "dxt", [("speq_dxt_wait", "/usr/lib/powerpc64-linux-gnu/libspeq.so.0"),
                                                ("st_TexImage", LP_GL), ("main", LP_EXE)]),
    (7, "load", 2000, 2000, "ioquake3", "mipmap", [("do_row", LP_GL), ("make_2d_mipmap", LP_GL),
                                                   ("_mesa_generate_mipmap", LP_GL), ("main", LP_EXE)]),
    (10, "load", 2000, 2000, "ioquake3", "game", [("CL_ParseServerMessage", LP_EXE), ("main", LP_EXE)]),
    (4, "load", 2000, 2000, "ioquake3", "jpeg", [("jsimd_idct_islow_altivec", "/usr/lib/powerpc64-linux-gnu/libjpeg.so.62.4.0"),
                                                 ("jpeg_read_scanlines", "/usr/lib/powerpc64-linux-gnu/libjpeg.so.62.4.0"),
                                                 ("main", LP_EXE)]),
    (3, "load", 2000, 2002, "ioquake3", "audio", [("ov_read", "/usr/lib/powerpc64-linux-gnu/libvorbisfile.so.3.3.8"),
                                                  ("S_OGG_CodecReadStream", LP_EXE)]),
    (6, "load", 900, 900, "Xorg", None, [("glamor_poly", "/usr/lib/xorg/modules/libglamoregl.so"), ("main", "/usr/lib/xorg/Xorg")]),
    (3, "load", 50, 50, "kswapd0", None, [(".shrink_folio_list", LP_K), (".kswapd", LP_K)]),
    (5, "load", 2000, 2000, "ioquake3", "init", [("some_ctor", "/usr/lib/powerpc64-linux-gnu/libfoo.so.1"),
                                                 ("call_init", LP_LD), ("_dl_init", LP_LD), ("dl_open_worker", LP_LD),
                                                 ("dlopen", LP_LIBC), ("main", LP_EXE)]),
    (4, "load", 2000, 2000, "ioquake3", "libc", [("malloc", LP_LIBC), ("main", LP_EXE)]),
    (40, "warmup", 2000, 2000, "ioquake3", "mesa", [("nv30_draw_vbo", LP_GL), ("main", LP_EXE)]),
    (15, "warmup", 2000, 2000, "ioquake3", "shader.glsl", [("ast_expression::hir(exec_list*, _mesa_glsl_parse_state*)", LP_GL),
                                                           ("_mesa_ast_to_hir", LP_GL), ("_mesa_glsl_compile_shader", LP_GL),
                                                           ("main", LP_EXE)]),
    (10, "warmup", 2000, 2000, "ioquake3", "game", [("CL_Frame", LP_EXE), ("main", LP_EXE)]),
]


def lp_stat(pid, minflt, majflt, utime, stime, threads=3):
    # /proc/<pid>/stat: fields 3.. after "(comm)"; minflt 10, majflt 12, utime 14, stime 15, threads 20
    f = ["S", "1", str(pid), str(pid), "0", "-1", "0", str(minflt), "0", str(majflt), "0", str(utime), str(stime),
         "0", "0", "20", "0", str(threads)] + ["0"] * 34
    return "%d (ioquake3 x) %s\n" % (pid, " ".join(f))


def lp_disks(reads, sectors, ms, io_ms, zram_reads=0):
    z = "   8       0 sda %d 0 %d %d 10 0 80 5 0 %d %d 0 0 0 0 0 0\n" % (reads, sectors, ms, io_ms, io_ms)
    z += "   8       1 sda1 %d 0 %d %d 10 0 80 5 0 %d %d 0 0 0 0 0 0\n" % (reads, sectors, ms, io_ms, io_ms)
    z += " 252       0 zram0 %d 0 %d 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n" % (zram_reads, zram_reads * 8)
    return z


def loadprof_synthetic(tmp):
    import io
    import loadprof
    import perfprof
    d = tmp / "ps3-bench-20261018T060000Z-fakeload-base"
    d.mkdir(parents=True)
    write(d / "perf-load.targets", "game=1999 xorg=900 kswapd0=50 hz=499 launch=1\n")
    write(d / "perf-load.exe", LP_EXE + "\n")
    # the clock: mono_t0 1000.00, the load ends 5 s later, the warm-up until 8 s (perf_load_win)
    bench(d, game="fakeload", variant="base", ended="window", seconds="10.5", load_end="5.00", warmup="3",
          window="8.00 10.00", t0="1000.0", mono_t0="1000.00", perf_load_win="0.00 8.00", perf_load_ack="ok",
          perf_load_cpu="0.40", game_pid="2000", shadercache="cold", shadercache_kb="4 2316", harness="rec10+perfload")
    lines, tl, tw = [], 1000.001, 1005.001
    for n, ph, pid, tid, comm, _, frames in LP_SAMPLES:
        for _ in range(n):
            if ph == "load":
                tl += 0.004
                t = tl
            else:
                tw += 0.004
                t = tw
            lines.append((t, "%s  %d/%d  %.6f:    2000000 " % (comm, pid, tid, t),
                          ["\t    %x %s (%s)" % (0x10000000 + 16 * i, sym, dso) for i, (sym, dso) in enumerate(frames)]))
    check("loadprof: the made-up load fits in its 5 s", tl < 1005.0, tl)
    lines.sort(key=lambda x: x[0])   # perf script's order: by time
    text = "".join("%s\n%s\n\n" % (h, "\n".join(fr)) for _, h, fr in lines)
    n = perfprof.collapse(io.StringIO(text), d, prefix="perf-load", split=(1000.0, 5.0))
    check("loadprof: %d samples collapsed" % n, n == sum(x[0] for x in LP_SAMPLES), n)
    folded = (d / "perf-load.folded").read_text()
    check("loadprof: perf-load.folded has the phase as the outermost frame",
          "load;ioquake3;main;FS_ReadFile;unzReadCurrentFile;inflate_fast 50\n" in folded and
          "warmup;ioquake3;main;CL_Frame 10\n" in folded, folded[:300])
    check("loadprof: perf-load-leaf.txt and perf-load-stacks.tsv written",
          (d / "perf-load-leaf.txt").exists() and (d / "perf-load-stacks.tsv").read_text().startswith("# boundary=1005.000000"))
    # the I/O snapshots: 100 MB read in the load, 10 MB in the warm-up; 500 + 20 major faults
    write(d / "load-io" / "timebase", "79800000\n")
    for lab, disk, vm in (("start", lp_disks(100, 2000, 50, 30), "pgmajfault 10\npswpin 0\npgscan_direct 0\nallocstall_normal 0\n"),
                          ("load", lp_disks(1100, 2000 + 195313, 4050, 3030, 5), "pgmajfault 510\npswpin 7\npgscan_direct 64\nallocstall_normal 2\n"),
                          ("warm", lp_disks(1200, 2000 + 195313 + 19531, 4450, 3430, 5), "pgmajfault 530\npswpin 7\npgscan_direct 64\nallocstall_normal 2\n")):
        write(d / "load-io" / (lab + ".diskstats"), disk)
        write(d / "load-io" / (lab + ".vmstat"), vm)
        write(d / "load-io" / (lab + ".info"), "label=%s real=1000.0 pid=2000\n" % lab)
    write(d / "load-io" / "load.stat", lp_stat(2000, 40000, 500, 450, 50))
    write(d / "load-io" / "load.mainstat", lp_stat(2000, 30000, 400, 380, 40))
    write(d / "load-io" / "load.io", "rchar: 300000000\nwchar: 0\nsyscr: 5000\nsyscw: 0\nread_bytes: 100000000\n")
    write(d / "load-io" / "load.status", "Name:\tioquake3\nvoluntary_ctxt_switches:\t900\nnonvoluntary_ctxt_switches:\t50\n")
    write(d / "load-io" / "warm.stat", lp_stat(2000, 41000, 520, 700, 60))
    write(d / "load-io" / "warm.mainstat", lp_stat(2000, 30500, 410, 600, 45))
    write(d / "load-io" / "warm.io", "rchar: 320000000\nwchar: 0\nsyscr: 5400\nsyscw: 0\nread_bytes: 110000000\n")
    write(d / "load-io" / "warm.status", "Name:\tioquake3\nvoluntary_ctxt_switches:\t1900\nnonvoluntary_ctxt_switches:\t80\n")
    # ld.so's statistics: the shell's exec first, then the game's (79.8 MHz timebase: 0.1 s, 0.05 s relocation)
    blk = ("      2000:\t\n      2000:\truntime linker statistics:\n"
           "      2000:\t  total startup time in dynamic loader: %d cycles\n"
           "      2000:\t            time needed for relocation: %d cycles (50.0%%)\n"
           "      2000:\t                 number of relocations: %d\n"
           "      2000:\t      number of relocations from cache: 3\n"
           "      2000:\t        number of relative relocations: 1000\n"
           "      2000:\t           time needed to load objects: %d cycles (25.0%%)\n")
    write(d / "ld" / "stats.2000", blk % (79800, 39900, 100, 19950) + blk % (7980000, 3990000, 4321, 1995000) +
          "      2000:\t\n      2000:\truntime linker statistics:\n      2000:\t           final number of relocations: 54321\n"
          "      2000:\tfinal number of relocations from cache: 99\n")
    write(d / "ld" / "stats.2005", blk % (1000, 500, 10, 200))
    # the main thread's states: disk for 1 s, then on the CPU; the warm-up: 1 s asleep, 2 s on the CPU
    rec = []
    for i in range(1, 801):
        t = i * 0.01
        if t <= 1.0:
            smp = "s=D w=3 y=-1 c=50 k0=3:24:0:0:10:1"
        elif t <= 5.0:
            smp = "s=R c=9500"
        elif t <= 6.0:
            smp = "s=S w=2 y=221 c=0"
        else:
            smp = "s=R c=9500"
        rec.append("%d %s" % (round(t * 1e6), smp))
    write(d / "stalls.rec", REC_HEAD + "\n".join(rec) + "\n# end t=8000000 samples=800 late=0 maxgap_us=10000 "
          "self_user_us=1000 self_sys_us=2000 maxrss_kb=1000 stacks=0\n")
    import stalls
    st = stalls.analyze(d)
    check("stalls: the load (disk ~1 s, cpu 4 s) and the warm-up (wait 1 s, cpu 2 s) apart",
          st and st["load"] and abs(st["load"][1].get("disk", 0) - 1.0) < 0.03 and abs(st["load"][1].get("cpu", 0) - 4.0) < 0.03
          and st["warmup"] and abs(st["warmup"][2].get("wait", 0) - 1.0) < 0.03 and abs(st["warmup"][2].get("cpu", 0) - 2.0) < 0.03,
          st and (st["load"] and dict(st["load"][1]), st["warmup"] and dict(st["warmup"][2])))
    res = loadprof.analyze(d)
    want = Counter()
    for n, ph, pid, tid, comm, g, frames in LP_SAMPLES:
        if g:
            want[(ph, g)] += n * 0.002
    got = Counter()
    for ph in loadprof.PHASES:
        for g, v in res["ph"][ph]["grp"].items():
            got[(ph, g)] = v
    bad = {k: (round(got[k], 4), round(v, 4)) for k, v in want.items() if abs(got[k] - v) > 1e-6}
    bad.update({k: (round(v, 4), 0) for k, v in got.items() if k not in want and v > 1e-6})
    check("loadprof: every made-up stack in its group, per phase", not bad, bad)
    L, W = res["ph"]["load"], res["ph"]["warmup"]
    main_want = sum(n * 0.002 for n, ph, pid, tid, *_ in LP_SAMPLES if ph == "load" and tid == 2000)
    check("loadprof: the main thread's load CPU (%.3f s) and the disk$0 thread apart" % main_want,
          abs(L["cpu_main"] - main_want) < 1e-6 and L["grp_main"]["shader.cache"] == 0 and L["grp"]["shader.cache"] > 0,
          (L["cpu_main"], dict(L["grp_main"])))
    check("loadprof: Xorg and kswapd0 counted apart (0.012, 0.006 s)",
          abs(L["others"]["xorg"] - 0.012) < 1e-6 and abs(L["others"]["kswapd0"] - 0.006) < 1e-6, dict(L["others"]))
    check("loadprof: AltiVec jpeg samples seen", abs(L["altivec"] - 0.008) < 1e-6, L["altivec"])
    io_ = res["io"]
    check("loadprof: disk MB per phase (100.0, 10.0), sda1 and zram0 not counted as the disk",
          abs(io_["load"]["hdd_mb"] - 100.0) < 0.01 and abs(io_["warmup"]["hdd_mb"] - 10.0) < 0.01
          and io_["load"]["zram_reads"] == 5 and io_["load"]["hdd_reads"] == 1000,
          (io_.get("load"), io_.get("warmup")))
    check("loadprof: the game's counters (majflt 500 then 20, CPU 5.0 s then 2.6 s, read_bytes)",
          io_["load"]["stat_majflt"] == 500 and io_["warmup"]["stat_majflt"] == 20
          and abs(io_["load"]["stat_utime"] + io_["load"]["stat_stime"] - 5.0) < 1e-9
          and abs(io_["warmup"]["stat_utime"] + io_["warmup"]["stat_stime"] - 2.6) < 1e-9
          and io_["load"]["io_read_bytes"] == 100000000 and io_["warmup"]["io_read_bytes"] == 10000000
          and io_["load"]["pswpin"] == 7 and io_["load"]["direct_scan"] == 64, (io_["load"], io_["warmup"]))
    ld = res["ld"]
    check("loadprof: ld.so's statistics of the game's exec (the second block: 0.1 s, relocation 0.05 s, 4321, final 54321)",
          ld and ld.get("execs") == 2 and abs(ld.get("startup_s", 0) - 0.1) < 1e-9 and abs(ld.get("reloc_time_s", 0) - 0.05) < 1e-9
          and ld.get("relocs") == 4321 and ld.get("final_relocs") == 54321 and ld.get("file") == "stats.2000", ld)
    r = report.run(d)
    check("report: lp_* columns (link 0.08, glsl 0.1, shbe 0.024, tex 0.03, decomp 0.154, lp_mb 100.0, lp_ld 0.1, lp_disk ~1)",
          r.get("lp_link") == 0.08 and r.get("lp_glsl") == 0.1 and r.get("lp_shbe") == 0.02 and r.get("lp_tex") == 0.03
          and r.get("lp_decomp") == 0.15 and r.get("lp_mb") == 100.0 and r.get("lp_wmb") == 10.0 and r.get("lp_ld") == 0.1
          and r.get("lp_majflt") == 500 and abs((r.get("lp_disk") or 0) - 1.0) < 0.05 and r.get("lp_wshader") == 0.03,
          {k: v for k, v in r.items() if k.startswith("lp_")})
    check("report: the shader cache and the start-up profile in the variant (base@rec10+perfload+sccold)",
          r.get("variant") == "base@rec10+perfload+sccold", r.get("variant"))
    check("report: warmup_disk_s 0", r.get("warmup_disk_s") == 0.0, r.get("warmup_disk_s"))
    p = subprocess.run([sys.executable, str(HERE / "report.py"), "--load", str(d)], capture_output=True, text=True)
    check("report --load: loadprof's tables printed", p.returncode == 0 and "levers over the start-up span" in p.stdout
          and "shader.glsl" in p.stdout, p.stdout[-600:] + p.stderr[-600:])
    print(p.stdout[p.stdout.find("== "):])


def client_load(tmp):
    """client.sh with BENCH_PERF_LOAD: the gate holds the game until the fake perf acknowledged enable,
    perf off at the window's start, the counters and ld.so's statistics; BENCH_SHADERCACHE cold, warm
    (its first run prime), off"""
    rdir = tmp / "recipes-load"
    rdir.mkdir()
    fake = tmp / "fake-game"   # made by client()
    comm = Path(sys.executable).name[:15]
    write(rdir / "fakeload.sh", 'CMD=("%s" --fake-game hudwin); COMM=%s; LOAD_MARK=\'^loaded\'; WARMUP=2; MEASURE=2\n'
          'WINDOW_STOP=1; DURATION=25\n' % (fake, comm))
    env0 = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "GALLIUM_HUD",
                                                             "GALLIUM_HUD_DUMP_DIR", "LD_PRELOAD", "LD_DEBUG")}
    env0.update(BENCH_RECIPES=str(rdir), BENCH_BACKUP=str(tmp / "backup"), BENCH_SHADERCACHE_ROOT=str(tmp / "shc"))
    d = tmp / "ps3-bench-20261018T070000Z-fakeload-base"
    d.mkdir()
    os.mkfifo(d / ".perfload.ctl")
    os.mkfifo(d / ".perfload.ack")
    write(d / ".perfload.pid", "%d\n" % os.getpid())
    log = []
    # the ack 1.5 s late: longer than the fake game takes to start, so only the gate makes it wait
    fds = fake_perf(str(d / ".perfload.ctl"), str(d / ".perfload.ack"), log, delay=1.5)
    env = dict(env0, BENCH_DIR=str(d), BENCH_PERF_LOAD="1", FAKE_T0_FILE=str(d / "fake.t0"), BENCH_SHADERCACHE="cold",
               BENCH_PERF_LOAD_CTL="%s:%s" % (d / ".perfload.ctl", d / ".perfload.ack"))
    t0 = time.time()
    p = subprocess.run(["bash", str(HERE / "client.sh"), "fakeload", "base"], env=env, capture_output=True, text=True)
    check("load client: exit 0", p.returncode == 0, p.stdout[-800:] + p.stderr[-800:])
    os.close(fds[0])
    b = report.kv(d / "bench.txt")
    le = float(b.get("load_end") or 0)
    w = [float(x) for x in (b.get("perf_load_win") or "").split()]
    check("perf-load: on from the exec (0.00) to the window's start (load + WARMUP)",
          len(w) == 2 and w[0] == 0.0 and abs(w[1] - le - 2) < 0.15, (le, w))
    check("perf-load: the fake perf got enable, then disable, acknowledged",
          [c for _, c in log] == ["enable", "disable"] and b.get("perf_load_ack") == "ok", (log, b.get("perf_load_ack")))
    gt = float((d / "fake.t0").read_text()) if (d / "fake.t0").exists() else 0
    check("perf-load: the game started only after perf's ack (the gate), the clock with it",
          log and gt >= log[0][0] and 0 <= float(b.get("t0") or 0) - log[0][0] < 0.2, (log[:1], gt, b.get("t0")))
    check("perf-load: launch pid, game_pid, mono_t0, perf_load_cpu in bench.txt",
          b.get("game_pid", "").isdigit() and re.fullmatch(r"\d+\.\d\d", b.get("mono_t0") or "") and
          b.get("perf_load_cpu") not in (None, ""), (b.get("game_pid"), b.get("mono_t0"), b.get("perf_load_cpu")))
    have = sorted(x.name for x in (d / "load-io").glob("*")) if (d / "load-io").is_dir() else []
    check("perf-load: the counters at the gate, the load's end and the window's start",
          all(x in have for x in ("start.diskstats", "start.vmstat", "load.diskstats", "load.stat", "load.io",
                                  "load.mainstat", "warm.diskstats", "warm.stat", "warm.vmstat")), have)
    lds = list((d / "ld").glob("stats.*")) if (d / "ld").is_dir() else []
    gl = (d / "game.log").read_text(errors="replace") if (d / "game.log").exists() else ""
    check("perf-load: ld.so's statistics written for the game's pid (LD_DEBUG not in its output)",
          any(x.name == "stats.%s" % b.get("game_pid") and "runtime linker statistics" in x.read_text(errors="replace")
              for x in lds) and "runtime linker" not in gl, ([x.name for x in lds], gl[:200]))
    check("perf-load: perf-load.exe names the game", (d / "perf-load.exe").exists() and
          comm[:6] in (d / "perf-load.exe").read_text(), (d / "perf-load.exe").exists() and (d / "perf-load.exe").read_text())
    import loadprof
    res = loadprof.analyze(d)
    check("loadprof: a run without perf-load-stacks.tsv: the I/O and ld.so still read",
          res and "load" in res["io"] and "stat_majflt" in res["io"]["load"] and res["ld"] and "startup" in res["ld"],
          res and (res["io"], res["ld"]))
    m = re.search(r"^env MESA_SHADER_CACHE_DIR=(\S+)$", gl, re.M)
    check("shadercache cold: an empty directory for the game, removed after; shadercache= in bench.txt",
          m and "/cold." in m[1] and not Path(m[1]).exists() and b.get("shadercache") == "cold", (m and m[1], b.get("shadercache")))
    # warm: the first run fills the directory (prime), the second finds it (warm); off disables the cache
    tags = []
    for i, mode in enumerate(("warm", "warm", "off")):
        d2 = tmp / ("ps3-bench-20261018T07010%dZ-fakeload-base" % i)
        env = dict(env0, BENCH_DIR=str(d2), BENCH_SHADERCACHE=mode)
        p = subprocess.run(["bash", str(HERE / "client.sh"), "fakeload", "base"], env=env, capture_output=True, text=True)
        b2 = report.kv(d2 / "bench.txt")
        g2 = (d2 / "game.log").read_text(errors="replace") if (d2 / "game.log").exists() else ""
        tags.append((b2.get("shadercache"), report.run(d2).get("variant"), "MESA_SHADER_CACHE_DISABLE=true" in g2))
        if i == 0:   # what Mesa would have written
            write(tmp / "shc" / "warm-fakeload" / "mesa_shader_cache" / "index", "x" * 5000)
    check("shadercache warm: prime first, then warm (base@scprime, base@scwarm), off: MESA_SHADER_CACHE_DISABLE",
          tags == [("prime", "base@scprime", False), ("warm", "base@scwarm", False), ("off", "base@scoff", True)], tags)
    print()


def lint():
    print("\nrecipes: game variant DURATION WARMUP MEASURE steady-source WINDOW_STOP")
    short = []
    for r in sorted((HERE / "recipes").glob("*.sh")):
        p = subprocess.run(["bash", "-c", LINT, "lint", str(HERE), r.stem], capture_output=True, text=True)
        for line in p.stdout.splitlines():
            f = line.split()
            if f[2] == "ERROR":
                check("recipe %s variant %s sources and sets CMD" % (f[0], f[1]), False, line)
                continue
            dur, warm, meas = int(f[2]), int(f[3]), int(f[4])
            if f[5] != "none" and 0 < dur < warm + meas + 15:
                short.append("%s-%s (%d s)" % (f[0], f[1], dur))
        check("recipe %s: every variant sources and sets CMD" % r.stem, "ERROR" not in p.stdout and p.returncode == 0,
              p.stderr[-300:])
    print("variants too short for a window (cut before load + WARMUP + MEASURE; not fps runs or "
          "kept short on purpose):\n  " + ", ".join(short))


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--fake-game":
        fake_game(sys.argv[2])
        sys.exit(0)
    if len(sys.argv) > 4 and sys.argv[1] == "--fake-stall-game":
        fake_stall_game(*sys.argv[2:5])
        sys.exit(0)
    tmp = Path(tempfile.mkdtemp(prefix="bench-selftest-"))
    try:
        runs = synthetic(tmp)
        matrix(tmp, runs)
        client(tmp)
        client_gprefetch(tmp)
        client_libenv(tmp)
        client_dump(tmp)
        client_shots(tmp)
        client_ssdarm(tmp)
        lint()
        stalls_synthetic(tmp)
        stallrec_e2e(tmp)
        perf_synthetic(tmp)
        irqoff_synthetic(tmp)
        perf_fill(tmp)
        client_prof(tmp)
        loadprof_synthetic(tmp)
        client_load(tmp)
        memprof_real(tmp)
        malloctrim_real(tmp)
        import intfcheck
        print("\ninterference detector (intfcheck.py)")
        intfcheck.run(tmp, check)
        import logscan
        logscan.selftest(tmp, check)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d check(s) failed: %s" % (len(FAILS), ", ".join(FAILS)) if FAILS else "\nall checks passed")
    sys.exit(1 if FAILS else 0)
