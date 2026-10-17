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
     line, a line split by the client's 0.25 s HUD polling
  4. every recipe and variant sourced as client.sh does: CMD set, and the durations against
     load + WARMUP + MEASURE (a variant that can't hold a window is listed, not an error)
Exit status 0 when every check passes; scratch files in a temporary directory, removed at the end."""
import os
import shutil
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
    write(d / "hud" / "frametime(ms)", "".join("%g\n" % v for v in ft))
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
    runs = [d]

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
    ft = open(os.path.join(hud, "frametime(ms)"), "a", buffering=1) if hud else None
    fp = open(os.path.join(hud, "fps"), "a", buffering=1) if hud else None
    last = sec = time.monotonic()
    frames = passn = 0
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
        if mode == "own" and now - t0 >= 3:
            say("own: steady 150 frames in 3000 ms: 49.5 fps, longest 33 ms")
            return


RECIPES = {
    "fakehud": """CMD=("$FAKE" --fake-game hudwin); LOAD_MARK='^loaded'; WARMUP=2; MEASURE=4; WINDOW_STOP=1; DURATION=25
variant_nostop() { WINDOW_STOP=0; DURATION=6; }""",
    "fakepass": """CMD=("$FAKE" --fake-game passes); FPS_RE='^pass: [0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='^pass: .* max ([0-9]+) ms'; PASSES=1; WARMUP=3; MEASURE=4; DURATION=25""",
    "fakeown": """CMD=("$FAKE" --fake-game own); STEADY_RE='^own: steady [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
STEADY_MAXMS_RE='^own: steady .* longest ([0-9]+) ms'; WARMUP=1; MEASURE=1; DURATION=25""",
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
    r = report.run(d)
    check("own: report steady 49.5 (game)", r.get("steady") == 49.5 and r.get("src") == "game", (r.get("steady"), r.get("src")))


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
    tmp = Path(tempfile.mkdtemp(prefix="bench-selftest-"))
    try:
        runs = synthetic(tmp)
        matrix(tmp, runs)
        client(tmp)
        lint()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d check(s) failed: %s" % (len(FAILS), ", ".join(FAILS)) if FAILS else "\nall checks passed")
    sys.exit(1 if FAILS else 0)
