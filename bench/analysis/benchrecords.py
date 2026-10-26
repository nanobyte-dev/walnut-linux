"""benchrecords.py: the benchmark result records in gamedb (gamedb/benchmarks/README.md), for matrix.py,
tools/gamedb/bench-import.py, check.py, score.py and blocks.py. stdlib only.

The records, under a gamedb root (the repository's gamedb/; BENCH_GAMEDB=<dir> names another, the
self-tests' scratch copies):
  benchmarks/batches.toml           every batch once, [[batch]] tables in their order
  benchmarks/metrics.toml           every metric once, [[metric]] tables in the matrices' order
  benchmarks/interfered.txt         runs found disturbed after the fact (report.py, matrix.py leave them out)
  games/<id>/benchmark/results/<batch>.toml   one game's numbers in one batch

The CSV matrices (one per metric with `matrix = "always"` or `"when-set"`, one row per batch of kind run,
in order; columns test, date, runs, observations, then `<game>-<variant>` sorted, only those with a
cell; csv.DictWriter's quoting and CRLF lines) are made from these by to_csv(): matrix.py --csv DIR,
`make bench-matrix`.
"""
import csv
import datetime
import json
import os
import re
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]   # this is bench/analysis/
FIXED = ["test", "date", "runs", "observations"]
BATCH_KEYS = {"name": str, "order": int, "kind": str, "title": str, "date": datetime.date, "runs": int,
              "stack": dict, "purpose": str, "observations": str, "source": str}
KINDS = ("run", "summary")
NAME_RE = re.compile(r"[a-z0-9][a-z0-9.-]{0,79}\Z")
METRIC_KEYS = {"name": str, "unit": str, "better": str, "matrix": str, "tool": str, "what": str}
MATRIX = ("always", "when-set", "none")
RESULT_KEYS = {"batch": str, "column": str, "variant": dict}
# keys of a variant's table that are no metric: a summary entry's label, its place in the summary and
# where each of its values comes from
ENTRY_KEYS = {"label": str, "order": int, "note": str, "source": dict}


def root():
    if os.environ.get("BENCH_RESULTS"):
        raise SystemExit("BENCH_RESULTS is gone: BENCH_GAMEDB=<dir> names a gamedb root (benchmarks/, games/)")
    return Path(os.environ.get("BENCH_GAMEDB") or REPO / "gamedb")


def interfered_file(r=None):
    return Path(r or root()) / "benchmarks" / "interfered.txt"


# --- TOML out (the few shapes these files have) ---------------------------------------------------
def _key(k):
    return k if re.fullmatch(r"[A-Za-z0-9_-]+", k) else json.dumps(k, ensure_ascii=False)


def _val(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        r = repr(v)
        if isinstance(v, float) and not re.search(r"[.eEn]", r):
            r += ".0"
        return r
    if isinstance(v, datetime.date):
        return v.isoformat()
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, dict):
        return "{ " + ", ".join("%s = %s" % (_key(k), _val(x)) for k, x in v.items()) + " }" if v else "{}"
    if isinstance(v, list):
        return "[" + ", ".join(_val(x) for x in v) + "]"
    raise TypeError("no TOML for %r" % (v,))


def dump_batches(batches, head):
    out = [head]
    for b in batches:
        out.append("\n[[batch]]\n")
        for k in BATCH_KEYS:
            if k in b and b[k] not in (None, "", {}):
                out.append("%s = %s\n" % (k, _val(b[k])))
    return "".join(out)


def dump_result(res, head):
    out = [head, "batch = %s\n" % _val(res["batch"])]
    if res.get("column"):
        out.append("column = %s\n" % _val(res["column"]))
    for v, cells in res["variant"].items():
        out.append("\n[variant.%s]\n" % _key(v))
        sub = {}
        for k, x in cells.items():
            if isinstance(x, dict):
                sub[k] = x
            else:
                out.append("%s = %s\n" % (_key(k), _val(x)))
        for k, x in sub.items():
            out.append("\n[variant.%s.%s]\n" % (_key(v), _key(k)))
            out.extend("%s = %s\n" % (_key(a), _val(y)) for a, y in x.items())
    return "".join(out)


BATCHES_HEAD = ("# gamedb/benchmarks/batches.toml: every benchmark batch once, in the order they were run\n"
                "# (gamedb/benchmarks/README.md). Written by bench/analysis/matrix.py; the observations are edited by hand.\n")
RESULT_HEAD = "# %s's numbers in batch %s (gamedb/benchmarks/README.md)\n"


# --- reading -------------------------------------------------------------------------------------
def load_batches(r=None):
    f = Path(r or root()) / "benchmarks" / "batches.toml"
    return tomllib.loads(f.read_text()).get("batch", []) if f.exists() else []


def save_batches(batches, r=None):
    f = Path(r or root()) / "benchmarks" / "batches.toml"
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(dump_batches(batches, BATCHES_HEAD))


def load_metrics(r=None):
    f = Path(r or root()) / "benchmarks" / "metrics.toml"
    if not f.exists():
        f = REPO / "gamedb" / "benchmarks" / "metrics.toml"   # a scratch root has no metrics of its own
    return tomllib.loads(f.read_text()).get("metric", [])


def result_path(game, batch, r=None):
    return Path(r or root()) / "games" / game / "benchmark" / "results" / ("%s.toml" % batch)


def load_result(game, batch, r=None):
    f = result_path(game, batch, r)
    return tomllib.loads(f.read_text()) if f.exists() else None


def save_result(game, res, r=None):
    f = result_path(game, res["batch"], r)
    if not res["variant"]:
        if f.exists():
            f.unlink()
        return
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(dump_result(res, RESULT_HEAD % (game, res["batch"])))


def results(r=None):
    """{batch: {game: the result file's table}} of every game's results/"""
    out = {}
    for f in sorted((Path(r or root()) / "games").glob("*/benchmark/results/*.toml")):
        t = tomllib.loads(f.read_text())
        out.setdefault(t.get("batch", f.stem), {})[f.parents[2].name] = t
    return out


def column(game, res, variant):
    return "%s-%s" % (res.get("column") or game, variant)


def csv_date(d):
    return d.strftime("%Y%m%d") if isinstance(d, datetime.date) else (d or "")


def cell(v):
    return str(v)


# --- the CSV matrices ----------------------------------------------------------------------------
def matrices(r=None):
    """{metric: (columns, rows)}: what the CSV files hold, rows as dicts of strings"""
    r = Path(r or root())
    batches = [b for b in load_batches(r) if b.get("kind", "run") == "run"]
    res = results(r)
    out = {}
    for m in load_metrics(r):
        if m.get("matrix", "none") == "none":
            continue
        name, rows, cols = m["name"], [], set()
        for b in batches:
            row = {"test": b.get("title", b["name"]), "date": csv_date(b.get("date")), "runs": str(b.get("runs", "")),
                   "observations": b.get("observations", "")}
            for game, t in res.get(b["name"], {}).items():
                for v, cells in t.get("variant", {}).items():
                    if name in cells:
                        c = column(game, t, v)
                        row[c] = cell(cells[name])
                        cols.add(c)
            rows.append(row)
        if m["matrix"] == "when-set" and not cols:
            continue
        out[name] = (sorted(cols), rows)
    return out


def write_csv(path, cols, rows):
    with Path(path).open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIXED + cols)
        w.writeheader()
        for row in rows:
            w.writerow({k: row.get(k, "") for k in FIXED + cols})


def to_csv(outdir, r=None):
    """the CSV matrices into outdir; [file names]"""
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    names = []
    for name, (cols, rows) in matrices(r).items():
        write_csv(outdir / ("%s.csv" % name), cols, rows)
        names.append("%s.csv" % name)
    return names


# --- names ---------------------------------------------------------------------------------------
def slug(title, date, taken):
    """a new batch's name: its date and the first words of its title, unique among taken"""
    t = re.sub(r"\([^)]*\)", " ", title).lower().replace("+", " ").replace(".", "-")
    words = [w.strip("-") for w in re.split(r"[^a-z0-9-]+", t) if w.strip("-")]
    d = date.isoformat() if isinstance(date, datetime.date) else ""
    base = "-".join(([d] if d else []) + words[:5])[:60].strip("-") or "batch"
    if not NAME_RE.match(base):
        base = "batch-" + base
    name, n = base, 2
    while name in taken:
        name, n = "%s-%d" % (base, n), n + 1
    return name


# --- checks (check.py) ---------------------------------------------------------------------------
def _typed(t, keys, what, errs, required=()):
    for k, v in t.items():
        if k not in keys:
            errs.append("%s: unknown key %r" % (what, k))
        elif not isinstance(v, keys[k]) or (keys[k] is int and isinstance(v, bool)):
            errs.append("%s: %s is not a %s" % (what, k, keys[k].__name__))
    for k in required:
        if k not in t:
            errs.append("%s: no %s" % (what, k))


def check_records(r=None, games=None):
    """[problems] of batches.toml, metrics.toml and every results/*.toml under the root; games: the
    known game ids (a result's game must be one)"""
    r = Path(r or root())
    errs = []
    try:
        batches = load_batches(r)
    except (OSError, tomllib.TOMLDecodeError) as e:
        return ["benchmarks/batches.toml: %s" % e]
    try:
        metrics = load_metrics(r)
    except (OSError, tomllib.TOMLDecodeError) as e:
        return ["benchmarks/metrics.toml: %s" % e]
    names, last = {}, 0
    for i, b in enumerate(batches):
        what = "benchmarks/batches.toml batch %s" % b.get("name", "#%d" % (i + 1))
        _typed(b, BATCH_KEYS, what, errs, ("name", "kind", "source"))
        if b.get("kind") not in KINDS:
            errs.append("%s: kind %r is not one of %s" % (what, b.get("kind"), ", ".join(KINDS)))
        n = b.get("name", "")
        if isinstance(n, str) and not NAME_RE.match(n):
            errs.append("%s: the name is not ^[a-z0-9][a-z0-9.-]*$" % what)
        if n in names:
            errs.append("%s: the name is used twice" % what)
        names[n] = b
        if b.get("kind") == "run":
            for k in ("order", "title", "runs"):
                if k not in b:
                    errs.append("%s: a run batch has no %s" % (what, k))
            if isinstance(b.get("order"), int):
                if b["order"] <= last:
                    errs.append("%s: order %d after %d (the file is in order)" % (what, b["order"], last))
                last = b["order"]
        for k, v in (b.get("stack") or {}).items() if isinstance(b.get("stack"), dict) else []:
            if not isinstance(v, str):
                errs.append("%s: stack.%s is not a string" % (what, k))
    known = {}
    for m in metrics:
        what = "benchmarks/metrics.toml metric %s" % m.get("name", "?")
        _typed(m, METRIC_KEYS, what, errs, ("name", "unit", "better", "matrix", "tool"))
        if m.get("better") not in ("higher", "lower"):
            errs.append("%s: better is higher or lower" % what)
        if m.get("matrix") not in MATRIX:
            errs.append("%s: matrix is one of %s" % (what, ", ".join(MATRIX)))
        if m.get("name") in known:
            errs.append("%s: named twice" % what)
        known[m.get("name")] = m
    for f in sorted((r / "games").glob("*/benchmark/results/*")):
        game = f.parents[2].name
        what = "games/%s/benchmark/results/%s" % (game, f.name)
        if f.suffix != ".toml":
            errs.append("%s: results/ holds <batch>.toml files only" % what)
            continue
        if games is not None and game not in games:
            errs.append("%s: no game %s" % (what, game))
        try:
            t = tomllib.loads(f.read_text())
        except tomllib.TOMLDecodeError as e:
            errs.append("%s: %s" % (what, e))
            continue
        _typed(t, RESULT_KEYS, what, errs, ("batch", "variant"))
        b = names.get(t.get("batch"))
        if t.get("batch") != f.stem:
            errs.append("%s: batch %r is not the file's name" % (what, t.get("batch")))
        if b is None:
            errs.append("%s: no batch %r in benchmarks/batches.toml" % (what, t.get("batch")))
        for v, cells in (t.get("variant") or {}).items() if isinstance(t.get("variant"), dict) else []:
            vw = "%s variant %s" % (what, v)
            if not isinstance(cells, dict) or not cells:
                errs.append("%s: an empty table" % vw)
                continue
            for k, x in cells.items():
                if k in ENTRY_KEYS:
                    if not isinstance(x, ENTRY_KEYS[k]):
                        errs.append("%s: %s is not a %s" % (vw, k, ENTRY_KEYS[k].__name__))
                    if b is not None and b.get("kind") == "run":
                        errs.append("%s: %s belongs to a summary batch's entries" % (vw, k))
                elif k not in known:
                    errs.append("%s: unknown metric %r (benchmarks/metrics.toml)" % (vw, k))
                elif isinstance(x, bool) or not isinstance(x, (int, float)):
                    errs.append("%s: %s is not a number" % (vw, k))
            for k in (cells.get("source") or {}) if isinstance(cells.get("source"), dict) else []:
                if k not in cells or k in ENTRY_KEYS:
                    errs.append("%s: source.%s names no value of the entry" % (vw, k))
            if b is not None and b.get("kind") == "summary" and not {"label", "order"} <= set(cells):
                errs.append("%s: a summary entry has a label and an order" % vw)
    return errs


# --- a summary batch ------------------------------------------------------------------------------
def summary(batch, r=None):
    """[(game, variant, entry)] of a summary batch, in the entries' order"""
    out = []
    for game, t in results(r).get(batch, {}).items():
        for v, e in t.get("variant", {}).items():
            out.append((game, v, e))
    return sorted(out, key=lambda x: (x[2].get("order", 0), x[0], x[1]))
