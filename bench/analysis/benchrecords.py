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
import importlib.util
import json
import os
import re
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]   # this is bench/analysis/
FIXED = ["test", "date", "runs", "observations"]


def _schema_keys():
    """the records' key sets, types, required keys and enums, from gamedb/schemas/ (batches.toml, metrics.toml,
    results.toml: the one source; tools/gamedb/schema.py reads them), loaded on first use so that importing
    this module needs no repository (the PS3's bench tree has none)"""
    if not _KEYS:
        spec = importlib.util.spec_from_file_location("gamedb_schema", REPO / "tools" / "gamedb" / "schema.py")
        s = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(s)
        _KEYS.update(
            BATCH_KEYS=s.types("batches", "batch"),             # in the order dump_batches writes them
            BATCH_REQUIRED=s.required("batches", "batch"),
            # a run batch's required keys (required_when kind = "run")
            RUN_REQUIRED=tuple(k for k, v in s.spec("batches", "batch")["keys"].items()
                               if v.get("required_when") == {"kind": "run"}),
            # the keys written by hand, never by matrix.py (save_batches keeps them); check_warnings warns when a
            # batch lacks a recommended one (tested, conclusion)
            HAND_KEYS=s.keyed("batches", "by=hand", "batch"),
            WARN_KEYS=s.keyed("batches", "recommended", "batch"),
            KINDS=s.enum("batches", "batch", "kind"),
            NAME_RE=re.compile(s.spec("batches", "batch", "name")["pattern"] + r"\Z"),
            METRIC_KEYS=s.types("metrics", "metric"),
            METRIC_REQUIRED=s.required("metrics", "metric"),
            BETTER=s.enum("metrics", "metric", "better"),
            MATRIX=s.enum("metrics", "metric", "matrix"),
            RESULT_KEYS=s.types("results"),
            RESULT_REQUIRED=s.required("results"),
            # keys of a variant's table that are no metric: a summary entry's label, its place in the summary
            # and where each of its values comes from
            ENTRY_KEYS=s.types("results", "variant", "*"))
    return _KEYS


_KEYS = {}


def __getattr__(name):
    """BATCH_KEYS, ENTRY_KEYS, ... as module attributes (bench-import.py reads ENTRY_KEYS)"""
    if name.isupper() and name in _schema_keys():
        return _KEYS[name]
    raise AttributeError(name)


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


def _mlstr(v):
    """a string with line breaks as a TOML multi-line basic string: the lines as they are, backslashes, quotes
    and other control characters escaped"""
    esc = "".join("\\\\" if c == "\\" else '\\"' if c == '"' else c if c in "\n\t" or ord(c) >= 0x20 and c != "\x7f"
                  else "\\u%04x" % ord(c) for c in v)
    return '"""\n%s"""' % esc


def dump_batches(batches, head):
    out = [head]
    for b in batches:
        out.append("\n[[batch]]\n")
        for k in _schema_keys()["BATCH_KEYS"]:
            if k in b and b[k] not in (None, "", {}):
                v = b[k]
                out.append("%s = %s\n" % (k, _mlstr(v) if isinstance(v, str) and "\n" in v else _val(v)))
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
                "# (gamedb/benchmarks/README.md). Written by bench/analysis/matrix.py; tested, conclusion and observations\n"
                "# are edited by hand (matrix.py keeps them).\n")
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
    if not _schema_keys()["NAME_RE"].match(base):
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


def check_warnings(r=None):
    """[warnings] of batches.toml that are no errors: a batch without its hand-written tested or conclusion
    (gamedb/benchmarks/README.md)"""
    try:
        batches = load_batches(r)
    except (OSError, tomllib.TOMLDecodeError):
        return []     # check_records reports it
    want = _schema_keys()["WARN_KEYS"]
    return ["benchmarks/batches.toml batch %s: no %s (what was tried and why, what it showed and what was decided)"
            % (b.get("name", "#%d" % (i + 1)), " and no ".join(k for k in want if not b.get(k)))
            for i, b in enumerate(batches) if not all(b.get(k) for k in want)]


def check_records(r=None, games=None):
    """[problems] of batches.toml, metrics.toml and every results/*.toml under the root; games: the
    known game ids (a result's game must be one)"""
    r = Path(r or root())
    errs = []
    K = _schema_keys()
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
        _typed(b, K["BATCH_KEYS"], what, errs, K["BATCH_REQUIRED"])
        if b.get("kind") not in K["KINDS"]:
            errs.append("%s: kind %r is not one of %s" % (what, b.get("kind"), ", ".join(K["KINDS"])))
        n = b.get("name", "")
        if isinstance(n, str) and not K["NAME_RE"].match(n):
            errs.append("%s: the name is not ^[a-z0-9][a-z0-9.-]*$" % what)
        if n in names:
            errs.append("%s: the name is used twice" % what)
        names[n] = b
        if b.get("kind") == "run":
            for k in K["RUN_REQUIRED"]:
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
        _typed(m, K["METRIC_KEYS"], what, errs, K["METRIC_REQUIRED"])
        if m.get("better") not in K["BETTER"]:
            errs.append("%s: better is %s" % (what, " or ".join(K["BETTER"])))
        if m.get("matrix") not in K["MATRIX"]:
            errs.append("%s: matrix is one of %s" % (what, ", ".join(K["MATRIX"])))
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
        _typed(t, K["RESULT_KEYS"], what, errs, K["RESULT_REQUIRED"])
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
            entry = K["ENTRY_KEYS"]
            for k, x in cells.items():
                if k in entry:
                    if not isinstance(x, entry[k]):
                        errs.append("%s: %s is not a %s" % (vw, k, entry[k].__name__))
                    if b is not None and b.get("kind") == "run":
                        errs.append("%s: %s belongs to a summary batch's entries" % (vw, k))
                elif k not in known:
                    errs.append("%s: unknown metric %r (benchmarks/metrics.toml)" % (vw, k))
                elif isinstance(x, bool) or not isinstance(x, (int, float)):
                    errs.append("%s: %s is not a number" % (vw, k))
            for k in (cells.get("source") or {}) if isinstance(cells.get("source"), dict) else []:
                if k not in cells or k in entry:
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
