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
Columns: test, date (of the newest run), runs (count), observations (did the games run, what was wrong, what this row proves), then the game-variants, sorted."""
import csv
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from report import run  # noqa: E402

RESULTS = Path(__file__).resolve().parent / "results"
METRICS = {
    "fps": lambda r: r.get("fps") or r.get("hud_med"),
    "cpu": lambda r: r.get("cpu"),
    "underruns": lambda r: r.get("xrun"),
    "ram_free": lambda r: round(r["avail"] / 1024, 1) if r.get("avail") else None,
    "vram": lambda r: round(r["vram"] / 1024, 1) if r.get("vram") else None,
}
FIXED = ["test", "date", "runs", "observations"]


def load(path):
    if not path.exists():
        return [], {}
    with path.open(newline="") as f:
        rd = csv.DictReader(f)
        rows = {r["test"]: r for r in rd}
        return [c for c in (rd.fieldnames or []) if c not in FIXED], rows


def save(path, cols, rows):
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIXED + sorted(cols))
        w.writeheader()
        for r in rows.values():
            w.writerow({k: r.get(k, "") for k in FIXED + sorted(cols)})


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    row, note, dirs = argv[0], "", []
    it = iter(argv[1:])
    for a in it:
        if a in ("--obs", "--note"):
            note = next(it)
        else:
            dirs.append(a)
    runs = [run(d) for d in dirs]
    date = max((Path(d).name.split("-")[2][:8] for d in dirs if Path(d).name.startswith("ps3-bench-")),
               default="")
    RESULTS.mkdir(exist_ok=True)
    for name, get in METRICS.items():
        path = RESULTS / f"{name}.csv"
        cols, rows = load(path)
        r = rows.setdefault(row, {"test": row})
        r["date"] = max(r.get("date", ""), date)
        r["runs"] = str(int(r.get("runs") or 0) + len(runs)) if name == "fps" else r.get("runs", "")
        if note:
            r["observations"] = note
        for key in dict.fromkeys(f'{x["game"]}-{x["variant"]}' for x in runs):
            vals = [get(x) for x in runs if f'{x["game"]}-{x["variant"]}' == key]
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
        cols, rows = load(path)
        if row in rows:
            rows[row]["runs"] = fr[row]["runs"]
            save(path, cols, rows)
    print(f">> {row}: {len(runs)} runs into {RESULTS}/")


if __name__ == "__main__":
    main(sys.argv[1:])
