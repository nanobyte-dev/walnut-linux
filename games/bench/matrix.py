#!/usr/bin/env python3
"""matrix.py ROW [--obs TEXT] <run dir>...: puts the runs into the results matrices, one CSV per
metric in games/bench/results/, one row per tested configuration (ROW: what was installed or
switched on, e.g. "Mesa ps3.262"), one column per game-variant. A cell is the median over the
given runs of that game-variant; cells of other columns in the row stay as they were, so a row
can be filled over several sessions. Files:
  fps.csv        the game's own fps, else the Gallium HUD median (frames per second, higher is better)
  cpu.csv        the client's user+sys seconds for the run (lower is better)
  underruns.csv  ALSA underruns in the run (audible chopping; lower is better)
  ram_free.csv   lowest MemAvailable during the run, MiB (higher is better)
  vram.csv       the game's peak VRAM, MiB
the steady state (report.py: WARMUP s after the load left out, then at least MEASURE s; runs from
before 2026-10-17 have no such cells):
  fps_steady.csv the steady-state fps: the game's own over its counted passes or window, else the HUD
                 window's per-second median (higher is better)
  low1.csv       the 1% low: 1000 / the 99th percentile frame time in the window (higher is better)
  stalls.csv     frames over 100 ms in the window (lower is better)
  longest_ms.csv the longest frame in the window, ms (lower is better)
the stall causes (runs recorded with STALLS=1, stalls.py; a file is created by the first run that has
the number): seconds of the window's stalls spent on each cause (lower is better)
  stall_disk_s.csv, stall_swap_s.csv, stall_reclaim_s.csv, stall_gpu_s.csv, stall_cpu_s.csv
the gameplay only (report.py *_play: the window less the reloads of a game that loops its demo,
OpenArena's map reload at every pass, Neverball's replay restart; a game without one has the plain
numbers; runs before the reload rules or without t0 where they need it have none; a file is created by
the first run that has the number): stalls_play.csv, stall_play_s.csv (lower is better), low1_play.csv
(higher), longest_play_ms.csv (lower). The old stalls/low1/longest_ms files keep the whole window.
A new metric file starts with the rows fps.csv has (cells empty), so every file has the same rows.
BENCH_RESULTS=<dir> writes the files there instead (the self-test).
Columns: test, date (of the newest run), runs (count), observations (did the games run, what was wrong, what this row proves), then the game-variants, sorted.
  --obs TEXT   the row's observations (plain sentences: did the games run, picture, sound, the bug found, the fix proven)
  --replace    drop the row's old cells and runs first (default: the new runs join what the row has)
  --hud GAMES  comma-separated games whose fps and fps_steady cells are the Gallium HUD medians even when
               the run has the game's own fps (Armagetron's benchmark average and its HUD median are
               different numbers; X-Moto's average covers its whole replay since 2026-10-17)
Run directories of the first day have no bench.txt (the old OpenArena harness, or a run cut short): the
game and variant come from the label (oa-tc1, smoke-dsda-doom-base), OpenArena's fps from the timedemo
line, ALSA underruns from the game.log lines. Test runs (gl-*, t-*, xtest*, speq-*, ...) count in
`runs` but make no cell."""
import csv
import os
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from report import run  # noqa: E402

RESULTS = Path(os.environ.get("BENCH_RESULTS") or Path(__file__).resolve().parent / "results")
HUD_GAMES = set()
METRICS = {
    "fps": lambda r: r.get("hud_med") if r["game"] in HUD_GAMES else r.get("fps") or r.get("hud_med"),
    "cpu": lambda r: r.get("cpu"),
    "underruns": lambda r: r.get("xrun"),
    "ram_free": lambda r: round(r["avail"] / 1024, 1) if r.get("avail") else None,
    "vram": lambda r: round(r["vram"] / 1024, 1) if r.get("vram") else None,
    "fps_steady": lambda r: r.get("hud_win") if r["game"] in HUD_GAMES else r.get("steady"),
    "low1": lambda r: r.get("low1"),
    "stalls": lambda r: r.get("stalls"),
    "longest_ms": lambda r: r.get("longest"),
}
OPTIONAL = ("stall_disk_s", "stall_swap_s", "stall_reclaim_s", "stall_gpu_s", "stall_cpu_s",
            "stalls_play", "stall_play_s", "low1_play", "longest_play_ms")
for _m in OPTIONAL:
    METRICS[_m] = lambda r, m=_m: r.get(m.replace("_ms", ""))
FIXED = ["test", "date", "runs", "observations"]
RECIPES = sorted((p.stem for p in (Path(__file__).resolve().parent / "recipes").glob("*.sh")),
                 key=len, reverse=True)


def label_game(d):
    """(game, variant) from a run directory's label when it has no bench.txt, else None (a test)"""
    label = re.sub(r"^(ps3-bench-)?\d{8}T\d{6}Z-", "", Path(d).name)
    label = re.sub(r"^smoke-", "", label)
    if label == "oa":  # step 0's first launch: the tc0 settings
        return "openarena", "tc0"
    if m := re.match(r"oa-(.+)$", label):  # step 0's OpenArena harness
        return "openarena", m[1].replace("-first", "").replace("-", "")
    for g in RECIPES:
        if label.startswith(g + "-"):
            return g, label[len(g) + 1:]
    return None


def fill(r, d):
    """runs without a bench.txt: game and variant from the label (None for tests) and the timedemo fps
    from the result file; runs whose bench.txt has no underruns= (before 2026-10-17 03:50): the
    underruns counted in game.log"""
    d = Path(d)
    if not (d / "bench.txt").exists():
        gv = label_game(d)
        r["game"], r["variant"] = gv if gv else (None, None)
        res = (d / "result").read_text(errors="replace") if (d / "result").exists() else ""
        if m := re.search(r"^\d+ frames [\d.]+ seconds ([\d.]+) fps", res, re.M):
            r["fps"] = float(m[1])
    log = d / "game.log"
    if r["game"] and "xrun" not in r and log.exists():
        n = log.read_text(errors="replace").count("underrun occurred")
        if n:
            r["xrun"] = n
    return r


def load(path):
    if not path.exists():
        return [], {}
    with path.open(newline="") as f:
        rd = csv.DictReader(f)
        rows = {r["test"]: r for r in rd}
        return [c for c in (rd.fieldnames or []) if c not in FIXED], rows


def save(path, cols, rows):
    cols = [c for c in cols if any(r.get(c) for r in rows.values())]  # no empty columns left by --replace
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIXED + sorted(cols))
        w.writeheader()
        for r in rows.values():
            w.writerow({k: r.get(k, "") for k in FIXED + sorted(cols)})


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    row, note, dirs, replace = argv[0], "", [], False
    it = iter(argv[1:])
    for a in it:
        if a in ("--obs", "--note"):
            note = next(it)
        elif a == "--replace":
            replace = True
        elif a == "--hud":
            HUD_GAMES.update(next(it).split(","))
        else:
            dirs.append(a)
    runs = [fill(run(d), d) for d in dirs]
    date = max((Path(d).name.split("-")[2][:8] for d in dirs if Path(d).name.startswith("ps3-bench-")),
               default="")
    RESULTS.mkdir(exist_ok=True)
    for name, get in METRICS.items():
        path = RESULTS / f"{name}.csv"
        if name in OPTIONAL and not path.exists() and not any(
                isinstance(get(x), (int, float)) for x in runs if x["game"]):
            continue    # no run has it yet: no file of empty cells
        cols, rows = load(path)
        if not path.exists() and name != "fps":  # a new metric: the rows fps.csv has, no cells
            rows = {t: {k: r.get(k, "") for k in FIXED} for t, r in load(RESULTS / "fps.csv")[1].items()}
        if replace and row in rows:  # keeps the row's place in the history
            rows[row] = {"test": row}
        r = rows.setdefault(row, {"test": row})
        r["date"] = max(r.get("date", ""), date)
        r["runs"] = str(int(r.get("runs") or 0) + len(runs)) if name == "fps" else r.get("runs", "")
        if note:
            r["observations"] = note
        games = [x for x in runs if x["game"]]
        for key in dict.fromkeys(f'{x["game"]}-{x["variant"]}' for x in games):
            vals = [get(x) for x in games if f'{x["game"]}-{x["variant"]}' == key]
            vals = [v for v in vals if isinstance(v, (int, float))]
            if vals:
                r[key] = str(round(statistics.median(vals), 1))
                if key not in cols:
                    cols.append(key)
        save(path, cols, rows)
    # runs counted once (in fps.csv); copy the count to the other files
    _, fr = load(RESULTS / "fps.csv")
    for name in METRICS:
        path = RESULTS / f"{name}.csv"
        if not path.exists():
            continue
        cols, rows = load(path)
        if row in rows:
            rows[row]["runs"] = fr[row]["runs"]
            save(path, cols, rows)
    print(f">> {row}: {len(runs)} runs into {RESULTS}/")


if __name__ == "__main__":
    main(sys.argv[1:])
