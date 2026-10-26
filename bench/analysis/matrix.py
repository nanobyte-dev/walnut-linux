#!/usr/bin/env python3
"""matrix.py ROW [--obs TEXT] [--name NAME] <run dir>... | --csv DIR: puts the runs into the benchmark records
(gamedb/benchmarks/README.md): ROW is a batch (a tested configuration: what was installed or switched on,
e.g. "Mesa ps3.262"), named by its name or its title; a new one is added to gamedb/benchmarks/batches.toml
after the others (its name: --name, else the date and the title's first words). Each game-variant's
numbers go to gamedb/games/<id>/benchmark/results/<batch>.toml: a value is the median over the given runs
of that game-variant, rounded to 0.1; values the runs don't have stay as they were, so a batch can be
filled over several sessions. The metrics are gamedb/benchmarks/metrics.toml's (METRICS below has the
getter of each one with a matrix):
  fps          the game's own fps, else the Gallium HUD median (frames per second, higher is better)
  cpu          the client's user+sys seconds for the run (lower is better)
  underruns    ALSA underruns in the run (audible chopping; lower is better)
  ram_free     lowest MemAvailable during the run, MiB (higher is better)
  vram         the game's peak VRAM, MiB
the steady state (report.py: WARMUP s after the load left out, then at least MEASURE s; runs from
before 2026-10-17 have none):
  fps_steady   the steady-state fps: the game's own over its counted passes or window, else the HUD
               window's per-second median (higher is better)
  low1         the 1% low: 1000 / the 99th percentile frame time in the window (higher is better)
  stalls       frames over 100 ms in the window (lower is better)
  longest_ms   the longest frame in the window, ms (lower is better)
the stall causes (runs recorded with STALLS=1, stalls.py): seconds of the window's stalls spent on each
cause (lower is better): stall_disk_s, stall_swap_s, stall_reclaim_s, stall_gpu_s, stall_cpu_s
the gameplay only (report.py *_play: the window less the reloads of a game that loops its demo, its
[reload] in benchmark/analysis.toml: gamedb:openarena's map reload at every pass, gamedb:neverball's
replay restart; a game without one has the plain numbers; runs before the reload rules or without t0
where they need it have none): stalls_play, stall_play_s (lower is better), low1_play (higher),
longest_play_ms (lower). stalls, low1 and longest_ms keep the whole window.
A batch records the date of its newest run, its run count and its observations (did the games run, what
was wrong, what it proves). The game's id is the run's (bench.txt recipe=, else game= read as an old name);
when the run names the game otherwise (game=sauerbraten in a BananaBread run from before the move), the
results file says column = "<that name>", so the CSV matrices keep their columns.
  --obs TEXT   the batch's observations (plain sentences: did the games run, picture, sound, the bug found, the fix proven)
  --name NAME  a new batch's name (^[a-z0-9][a-z0-9.-]*$; never renamed later: gamedb:batch/<name> links point at it)
  --replace    drop the batch's old values, runs, date and observations first (default: the new runs join what it has)
  --include-interfered  put disturbed runs in too (report.py intf=1: ps3-bench.sh's detector or
               gamedb/benchmarks/interfered.txt; docs/STALLS.md "Interference"); by default they are skipped
               and listed, and don't count in `runs`
  --hud GAMES  comma-separated games whose fps and fps_steady values are the Gallium HUD medians even when
               the run has the game's own fps (gamedb:armagetronad's benchmark average and its HUD median
               are different numbers; gamedb:xmoto's average covers its whole replay since 2026-10-17)
  --csv DIR    write the CSV matrices made from the records into DIR (benchrecords.to_csv; `make bench-matrix`),
               nothing else
BENCH_GAMEDB=<dir> reads and writes the records of another gamedb root (the self-tests' scratch copy).
Run directories of the first day have no bench.txt (gamedb:openarena's old harness, or a run cut short):
the game and variant come from the label, through the games' [legacy] run_labels (benchmark/analysis.toml,
bench/harness/gamedata.py: oa-tc1) or a game's id or old recipe name and the variant after it
(smoke-<id>-base), the fps from a timedemo's result line, ALSA underruns from the game.log lines. Test runs
(gl-*, t-*, xtest*, speq-*, ...) count in `runs` but make no value."""
import datetime
import re
import statistics
import sys
from pathlib import Path

BENCH = Path(__file__).resolve().parent.parent   # the bench tree: this is bench/analysis/
sys.path[:0] = [str(BENCH / "analysis"), str(BENCH / "harness")]
from report import run  # noqa: E402
import games  # noqa: E402
import gamedata  # noqa: E402
import benchrecords as br  # noqa: E402

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
# the names a run directory's label may start with: every game's id and the recipes' old names (games.py;
# a label is the name the run was made under, kept as it is)
RECIPES = sorted(games.names(), key=len, reverse=True)


def label_game(d):
    """(game, variant) from a run directory's label when it has no bench.txt, else None (a test)"""
    label = re.sub(r"^(ps3-bench-)?\d{8}T\d{6}Z-", "", Path(d).name)
    label = re.sub(r"^smoke-", "", label)
    if gv := gamedata.run_label(label):  # a game's [legacy] run_labels (an older harness's names)
        return gv
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


def run_gid(r, d):
    """the game id of a run (None for a test), its label (the run's own game name)"""
    if not r["game"]:
        return None
    if (Path(d) / "bench.txt").exists() and r.get("group"):
        return r["group"]
    return games.resolve(r["game"], old=True) or r["game"]


def check_metrics():
    want = [m["name"] for m in br.load_metrics() if m.get("matrix", "none") != "none"]
    if sorted(want) != sorted(METRICS):
        sys.exit("matrix.py: gamedb/benchmarks/metrics.toml's metrics with a matrix (%s) are not METRICS' (%s)"
                 % (" ".join(sorted(want)), " ".join(sorted(METRICS))))


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    if argv[0] == "--csv":
        names = br.to_csv(argv[1])
        print(">> %d matrices into %s/" % (len(names), argv[1]))
        return
    row, note, dirs, replace, keep_intf, name = argv[0], "", [], False, False, None
    it = iter(argv[1:])
    for a in it:
        if a in ("--obs", "--note"):
            note = next(it)
        elif a == "--name":
            name = next(it)
        elif a == "--replace":
            replace = True
        elif a == "--include-interfered":
            keep_intf = True
        elif a == "--hud":
            HUD_GAMES.update(next(it).split(","))
        else:
            dirs.append(a)
    check_metrics()
    runs = [fill(run(d), d) for d in dirs]
    for r in runs:
        if r.get("intf") == 1:
            print(">> %s %s (interfered): %s" % ("kept" if keep_intf else "skipped", r["dir"], r.get("intf_why", "?")))
    if not keep_intf:
        dirs = [d for d, r in zip(dirs, runs) if r.get("intf") != 1]
        runs = [r for r in runs if r.get("intf") != 1]
    date = max((Path(d).name.split("-")[2][:8] for d in dirs if Path(d).name.startswith("ps3-bench-")),
               default="")
    root = br.root()
    batches = br.load_batches(root)
    b = next((x for x in batches if x.get("kind") == "run" and row in (x["name"], x.get("title"))), None)
    if b is None:
        taken = {x["name"] for x in batches}
        d = datetime.date(int(date[:4]), int(date[4:6]), int(date[6:8])) if date else None
        name = name or br.slug(row, d, taken)
        if name in taken or not br.NAME_RE.match(name):
            sys.exit("matrix.py: %r is no new batch name (^[a-z0-9][a-z0-9.-]*$, not used yet)" % name)
        b = {"name": name, "order": max((x.get("order", 0) for x in batches), default=0) + 1, "kind": "run",
             "title": row, "runs": 0, "source": "bench/analysis/matrix.py"}
        batches.insert(next((i for i, x in enumerate(batches) if x.get("kind") != "run"), len(batches)), b)
    if replace:
        for k in ("date", "runs", "observations"):
            b.pop(k, None)
        for f in sorted((root / "games").glob("*/benchmark/results/%s.toml" % b["name"])):
            f.unlink()
    d = max(br.csv_date(b.get("date")), date)
    if d:
        b["date"] = datetime.date(int(d[:4]), int(d[4:6]), int(d[6:8]))
    b["runs"] = int(b.get("runs") or 0) + len(runs)
    if note:
        b["observations"] = note
    results, labels = {}, {}
    gruns = [(x, run_gid(x, dd)) for x, dd in zip(runs, dirs) if x["game"]]
    for x, gid in gruns:
        if gid not in results:
            results[gid] = br.load_result(gid, b["name"], root) or {"batch": b["name"], "variant": {}}
            if results[gid]["variant"]:
                labels[gid] = {results[gid].get("column") or gid}
        labels.setdefault(gid, set()).add(x["game"])
        if len(labels[gid]) > 1:
            sys.exit("matrix.py: %s's runs in batch %s name it %s: put them in separate batches"
                     % (gid, b["name"], " and ".join(sorted(labels[gid]))))
        if x["game"] != gid:
            results[gid]["column"] = x["game"]
    order = list(METRICS)
    for key in dict.fromkeys((gid, x["variant"]) for x, gid in gruns):
        cells = results[key[0]]["variant"].setdefault(key[1], {})
        for mname, get in METRICS.items():
            vals = [get(x) for x, gid in gruns if (gid, x["variant"]) == key]
            vals = [v for v in vals if isinstance(v, (int, float))]
            if vals:
                cells[mname] = round(statistics.median(vals), 1)
        results[key[0]]["variant"][key[1]] = dict(sorted(cells.items(), key=lambda kv: order.index(kv[0])
                                                         if kv[0] in order else len(order)))
    for gid, t in results.items():
        t["variant"] = {v: c for v, c in sorted(t["variant"].items()) if c}
        br.save_result(gid, t, root)
    br.save_batches(batches, root)
    print(f">> {row}: {len(runs)} runs into {root}/")


if __name__ == "__main__":
    main(sys.argv[1:])
