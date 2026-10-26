#!/usr/bin/env python3
"""bench-import.py [--from DIR] [--write] | --verify DIR: the benchmark result matrices (the CSV files that were
bench/results/*.csv until 2026-10-26, one per metric, one row per batch, one column per game-variant)
into gamedb's records (gamedb/benchmarks/README.md):

  gamedb/benchmarks/batches.toml                 a [[batch]] per row: name, order (the row's number), kind = "run",
                                                 title (the row's `test`), date, runs, stack (the versions the
                                                 title names, when it does), observations, source = "bench/results row N"
  gamedb/games/<id>/benchmark/results/<batch>.toml   the game's cells of that row: [variant."<variant>"] metric = value,
                                                 every variant and metric it has (a harness suffix, @rec10, stays
                                                 part of the variant)

A column `<game>-<variant>[@harness]` belongs to the game whose name is its longest prefix (followed by "-"):
the gamedb ids and every bundle's benchmark/analysis.toml [legacy] recipe_names. The columns are the runs'
own game= as recorded, before the recipes moved into gamedb, so an old recipe name wins over an id it
equals: `sauerbraten-base` is BananaBread's (its recipe was sauerbraten.sh), and its results file says
column = "sauerbraten" so the matrix comes back the same. `supertuxkart-lean--@rec10` (row 97, one cell in
fps.csv) is SuperTuxKart's variant `lean--@rec10` as recorded.

Batch names: a name batches.toml already has for the row's title is kept (names are edited by hand and never
renamed: gamedb:batch/<name> links point at them), else made from the date and the title's first words.
Batches of kind summary in batches.toml are kept as they are, after the run batches.

Without --write it prints what it would write. --verify DIR checks the records against the CSV files in DIR:
every cell is found in exactly one record with the same value, no record holds a value the files don't, the
fixed columns match the batches, and the matrices made from the records (benchrecords.to_csv) are byte for
byte the files in DIR. Exit 1 on any difference.
"""
import argparse
import csv
import datetime
import re
import sys
import tempfile
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "bench"))
import benchrecords as br  # noqa: E402

FIXED = br.FIXED
# what a title says of the stack: (key, pattern, value format)
STACK = [
    ("kernel", r"\bkernel (?:6\.12\.111\+ps3\.|\.)?(\d{3})\b", "ps3.%s"),
    ("kernel", r"\bSSD root \.(\d{3})\b", "ps3.%s"),
    ("mesa", r"\bMesa (?:ps3)?\.(\d{3}(?:~dev)?)", "ps3.%s"),
    ("ps3rsx", r"\bps3rsx (\d+\.\d+)\b", "%s"),
    ("libspeq", r"\blibspeq (\d+\.\d+)\b", "%s"),
    ("ps3-gpu-tools", r"\bps3-gpu-tools (\d+\.\d+(?:~dev)?)", "%s"),
    ("sdl3", r"\bSDL3 ps3\.(\d+)", "ps3.%s"),
]


def stack(title):
    out = {}
    for key, pat, fmt in STACK:
        if key not in out and (m := re.search(pat, title)):
            out[key] = fmt % m[1]
    return out


def read_csvs(d):
    """{metric: (header, [rows as dicts])}"""
    out = {}
    for f in sorted(Path(d).glob("*.csv")):
        with f.open(newline="") as fh:
            rd = csv.DictReader(fh)
            out[f.stem] = (rd.fieldnames, list(rd))
    return out


def names_map(gamedb):
    """{name: (id, legacy?)}: every id with a benchmark, and the old recipe names (winning over an id)"""
    out = {}
    for g in sorted((gamedb / "games").iterdir()):
        if (g / "benchmark").is_dir() and g.name not in out:
            out[g.name] = (g.name, False)
    for g in sorted((gamedb / "games").iterdir()):
        a = g / "benchmark" / "analysis.toml"
        if a.exists():
            leg = tomllib.loads(a.read_text()).get("legacy", {})
            for n in leg.get("recipe_names", []) + leg.get("csv_columns", []):
                out[n] = (g.name, True)
    return out


def split(col, names):
    """(id, label, variant) of a column, label the name it was recorded under"""
    for n in sorted(names, key=len, reverse=True):
        if col.startswith(n + "-"):
            return names[n][0], n, col[len(n) + 1:]
    raise SystemExit("bench-import: column %r belongs to no game (no id or [legacy] name is its prefix)" % col)


def number(s):
    return int(s) if re.fullmatch(r"-?\d+", s) else float(s)


def build(csvs, gamedb, old_batches):
    """(batches, {(game, batch): result table}) from the CSV files"""
    if not csvs:
        raise SystemExit("bench-import: no CSV files to import (--from DIR; bench/results/ is gone since the import:\n"
                         "git log -- bench/results names the commit that removed them, or make bench-matrix for the records' own)")
    first = csvs.get("fps") or next(iter(csvs.values()))
    rows = first[1]
    for m, (_, rs) in csvs.items():
        if [[r[k] for k in FIXED] for r in rs] != [[r[k] for k in FIXED] for r in rows]:
            raise SystemExit("bench-import: %s.csv's rows or fixed columns differ from fps.csv's" % m)
    by_title = {b.get("title"): b["name"] for b in old_batches if b.get("kind") == "run"}
    taken, batches = set(by_title.values()), []
    for i, r in enumerate(rows):
        date = datetime.date(int(r["date"][:4]), int(r["date"][4:6]), int(r["date"][6:8])) if r["date"] else None
        name = by_title.get(r["test"]) or br.slug(r["test"], date, taken)
        taken.add(name)
        b = {"name": name, "order": i + 1, "kind": "run", "title": r["test"], "date": date, "runs": int(r["runs"]),
             "stack": stack(r["test"]), "observations": r["observations"], "source": "bench/results row %d" % (i + 1)}
        batches.append({k: v for k, v in b.items() if v not in (None, "", {})})
    names = names_map(gamedb)
    res = {}
    for metric, (header, rs) in sorted(csvs.items()):
        for c in header[len(FIXED):]:
            game, label, variant = split(c, names)
            for b, r in zip(batches, rs):
                if r.get(c, "") == "":
                    continue
                t = res.setdefault((game, b["name"]), {"batch": b["name"], "variant": {}})
                if label != game:
                    if t.get("column", label) != label:
                        raise SystemExit("bench-import: %s has columns under two names in %s" % (game, b["name"]))
                    t["column"] = label
                t["variant"].setdefault(variant, {})[metric] = number(r[c])
    order = [m["name"] for m in br.load_metrics(gamedb)]
    for t in res.values():
        t["variant"] = {v: dict(sorted(cells.items(), key=lambda kv: order.index(kv[0])))
                        for v, cells in sorted(t["variant"].items())}
    return batches, res


def verify(d, gamedb):
    csvs = read_csvs(d)
    probs = []
    batches = [b for b in br.load_batches(gamedb) if b.get("kind") == "run"]
    recs = br.results(gamedb)
    index = {}   # (batch, column, metric) -> [values]
    for bname, games in recs.items():
        for game, t in games.items():
            for v, cells in t["variant"].items():
                for m, x in cells.items():
                    if m not in br.ENTRY_KEYS:
                        index.setdefault((bname, br.column(game, t, v), m), []).append(x)
    cells = seen = 0
    for metric, (header, rs) in csvs.items():
        if len(rs) != len(batches):
            probs.append("%s.csv: %d rows, %d run batches" % (metric, len(rs), len(batches)))
        for b, r in zip(batches, rs):
            want = [b.get("title"), br.csv_date(b.get("date")), str(b.get("runs")), b.get("observations", "")]
            if [r[k] for k in FIXED] != want:
                probs.append("%s.csv row %d: fixed columns differ from batch %s" % (metric, b["order"], b["name"]))
            for c in header[len(FIXED):]:
                if r[c] == "":
                    continue
                cells += 1
                got = index.get((b["name"], c, metric), [])
                if len(got) != 1:
                    probs.append("%s.csv row %d %s: in %d records" % (metric, b["order"], c, len(got)))
                elif br.cell(got[0]) != r[c]:
                    probs.append("%s.csv row %d %s: %s in the CSV, %s in the record" % (metric, b["order"], c, r[c], got[0]))
                else:
                    seen += 1
    known = {(b["name"], c, m) for m, (h, rs) in csvs.items() for b, r in zip(batches, rs) for c in h[len(FIXED):] if r[c] != ""}
    run_names = {b["name"] for b in batches}
    extra = [k for k in index if k[0] in run_names and k not in known]
    for k in extra[:20]:
        probs.append("record %s %s %s: no such CSV cell" % k)
    with tempfile.TemporaryDirectory() as tmp:
        made = br.to_csv(tmp, gamedb)
        files = sorted(p.name for p in Path(d).glob("*.csv"))
        if sorted(made) != files:
            probs.append("matrices made %s, the directory has %s" % (sorted(made), files))
        for f in files:
            if (Path(tmp) / f).exists() and (Path(tmp) / f).read_bytes() != (Path(d) / f).read_bytes():
                probs.append("%s: the matrix made from the records differs" % f)
    for p in probs[:50]:
        print("!! " + p)
    nrec = sum(len(g) for b, g in recs.items() if b in run_names)
    print("bench-import --verify: %d files, %d cells, %d found once with the same value; %d record values "
          "without a cell; %d run batches, %d result files; regenerated matrices %s; %d problems"
          % (len(csvs), cells, seen, len(extra), len(batches), nrec,
             "byte-identical" if not any("differs" in p or "matrices made" in p for p in probs) else "DIFFERENT", len(probs)))
    return 1 if probs else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--from", dest="src", default=str(REPO / "bench" / "results"))
    ap.add_argument("--gamedb", default=str(REPO / "gamedb"), help="the gamedb root to write (default the repository's)")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--verify", metavar="DIR")
    a = ap.parse_args(argv)
    gamedb = Path(a.gamedb)
    if a.verify:
        return verify(a.verify, gamedb)
    old = br.load_batches(gamedb)
    batches, res = build(read_csvs(a.src), gamedb, old)
    batches += [b for b in old if b.get("kind") == "summary"]
    games = sorted({g for g, _ in res})
    print("bench-import: %d batches, %d result files over %d games, %d values" % (
        len(batches), len(res), len(games), sum(len(c) for t in res.values() for c in t["variant"].values())))
    if not a.write:
        for b in batches:
            print("  %3s %s  %s" % (b.get("order", "-"), b["name"], b.get("stack", "")))
        return 0
    run_names = {b["name"] for b in batches if b["kind"] == "run"}
    for f in (gamedb / "games").glob("*/benchmark/results/*.toml"):   # a re-import replaces the run batches' files
        if f.stem in run_names and (f.parents[2].name, f.stem) not in res:
            f.unlink()
    br.save_batches(batches, gamedb)
    for (game, _), t in sorted(res.items()):
        br.save_result(game, t, gamedb)
    print("bench-import: wrote %s and %d result files" % (gamedb / "benchmarks" / "batches.toml", len(res)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
