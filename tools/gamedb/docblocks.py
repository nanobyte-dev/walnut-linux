"""docblocks.py: the block generators of the driver and system docs (docs/*.md), registered in
tools/gamedb/blocks.py's GENERATORS. Each is a function (repo, args) -> markdown text, like the others there.

  batch-table BATCH [games=ID,...] [variants=PAT,...] metrics=NAME,... [root=PATH]
      one benchmark batch's numbers (gamedb/games/<id>/benchmark/results/BATCH.toml): a row per game and
      variant (variants: fnmatch patterns, in the order given; games: in the order given, else by title), a
      column per metric (gamedb/benchmarks/metrics.toml, with its unit), and a line naming the batch.
      The evidence tables of HOTPATHS.md, MEMORY.md and the like, where their numbers are records.
  licensed-table [root=PATH]
      every [[licensed]] entry of a game's build.toml: the package, its game, how it is built, its edition
      (and release). docs/PACKAGING.md "Game data".
  gamedb-packages [root=PATH]
      every source package and licensed builder in gamedb/ (games/<id>/packages/, games/<id>/licensed/,
      engines/<e>/packages/): its path, its game or engine, where its source comes from (package.conf),
      its patches. docs/LAYOUT.md.

  reload-rules [root=PATH]
      the [reload] table of every game's benchmark/analysis.toml (bench/harness/gamedata.py reload(), report.py's
      gameplay-only columns): where a looped demo's reload segment ends. docs/STALLS.md.

root is the path from the doc to gamedb/ for the links (default ../gamedb, right for docs/*.md).
"""
import fnmatch
import importlib.util
import re
import tomllib
from pathlib import Path

UNITS = {"frames a second": "fps", "seconds": "s", "MiB": "MiB", "ms": "ms", "frames": "frames",
         "underruns": "", "faults a minute": "a minute"}


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _opts(args, known, positional=0):
    """(positional words, {key: value}) of a block's words; refuses keys not in known"""
    pos, kv = [], {}
    for a in args:
        k, eq, v = a.partition("=")
        if eq:
            if k not in known:
                raise ValueError("unknown option %r (%s)" % (k, ", ".join(sorted(known))))
            kv[k] = v
        else:
            pos.append(a)
    if len(pos) != positional:
        raise ValueError("%d word(s) before the options expected, got %r" % (positional, pos))
    return pos, kv


def _num(x):
    if isinstance(x, float):
        return ("%.2f" % x).rstrip("0").rstrip(".")
    return str(x)


def _front(path):
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+", path.read_text(), re.S)
    return tomllib.loads(m[1]) if m else {}


def _owner(repo, kind, oid):
    """(title, link path from gamedb/) of a game or engine folder"""
    page = Path(repo) / "gamedb" / kind / oid / ("index.md" if kind == "games" else "engine.md")
    title = _front(page).get("title", oid) if page.is_file() else oid
    return title, "%s/%s/" % (kind, oid)


def gen_batch_table(repo, args):
    """a batch's numbers, a row per game and variant, a column per metric"""
    (batch,), o = _opts(args, {"games", "variants", "metrics", "root"}, 1)
    root = o.get("root", "../gamedb").rstrip("/")
    repo = Path(repo)
    br = _load("benchrecords", repo / "bench" / "analysis" / "benchrecords.py")
    info = {b["name"]: b for b in br.load_batches(repo / "gamedb")}
    if batch not in info:
        raise ValueError("no batch %s in gamedb/benchmarks/batches.toml" % batch)
    metrics = {m["name"]: m for m in br.load_metrics(repo / "gamedb")}
    names = [m for m in o.get("metrics", "").split(",") if m]
    if not names:
        raise ValueError("metrics=NAME,... is required")
    bad = [m for m in names if m not in metrics]
    if bad:
        raise ValueError("unknown metric(s) %s (gamedb/benchmarks/metrics.toml)" % ", ".join(bad))
    found = {}
    for f in sorted((repo / "gamedb" / "games").glob("*/benchmark/results/%s.toml" % batch)):
        found[f.parents[2].name] = tomllib.loads(f.read_text()).get("variant", {})
    games = [g for g in o.get("games", "").split(",") if g]
    for g in games:
        if g not in found:
            raise ValueError("game %s has no results in batch %s" % (g, batch))
    if not games:
        games = sorted(found, key=lambda g: _owner(repo, "games", g)[0].lower())
    pats = [p for p in o.get("variants", "").split(",") if p]
    head = ["Game", "Variant"] + ["`%s`%s" % (m, " (%s)" % UNITS.get(metrics[m]["unit"], metrics[m]["unit"])
                                              if UNITS.get(metrics[m]["unit"], metrics[m]["unit"]) else "")
                                  for m in names]
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    rows = 0
    for g in games:
        title, path = _owner(repo, "games", g)
        variants = list(found[g])
        if pats:
            variants = [v for p in pats for v in variants if fnmatch.fnmatchcase(v, p)]
            variants = list(dict.fromkeys(variants))
        for v in variants:
            e = found[g][v]
            out.append("| [%s](%s/%s) | %s | %s |" % (title, root, path, v, " | ".join(
                _num(e[m]) if m in e else "-" for m in names)))
            rows += 1
    if not rows:
        raise ValueError("no row: no variant of %s matches %s in batch %s" % (", ".join(games), pats, batch))
    b = info[batch]
    out.append("")
    out.append("Numbers: gamedb:batch/%s (%s, %s; medians over the runs of each game and variant)." % (
        batch, b.get("title", ""), b.get("date", "")))
    return "\n".join(out) + "\n"


def gen_licensed_table(repo, args):
    """every licensed data package that a game's build.toml declares, with its edition (X-Walnut-Edition)"""
    _, o = _opts(args, {"root"})
    root = o.get("root", "../gamedb").rstrip("/")
    repo = Path(repo)
    out = ["| Package | Game | Built by | Edition |", "|---|---|---|---|"]
    for f in sorted((repo / "gamedb" / "games").glob("*/build.toml")):
        b = tomllib.loads(f.read_text())
        gid = f.parent.name
        editions = (b.get("data_setup") or {}).get("editions") or {}
        for e in b.get("licensed", []):
            title, path = _owner(repo, "games", gid)
            d = f.parent / "licensed" / e["name"]
            builder = e.get("builder", "")
            if builder.startswith("game-data-packager:") and e.get("input"):
                how = "game-data-packager, from the owner's copy (`make licensed-data`)"
            elif builder.startswith("game-data-packager:"):
                how = "game-data-packager (`make game-data`)"
            elif builder == "retired":
                how = "retired: its published versions stay, built no more"
            elif d.is_dir():
                how = "`%s/licensed/%s/`" % (path.rstrip("/"), e["name"])
            else:
                how = builder
            ed = editions.get(e["name"]) or e.get("edition") or "?"
            if e.get("release"):
                ed += " (%s)" % e["release"]
            out.append("| `%s` | [%s](%s/%s) | %s | %s |" % (e["name"], title, root, path, how, ed))
    return "\n".join(out) + "\n"


def _conf(path):
    """package.conf's KEY=value lines"""
    out = {}
    for line in path.read_text().splitlines() if path.is_file() else []:
        m = re.match(r"([A-Z_]+)=(.*)", line)
        if m:
            out[m[1]] = m[2].strip().strip('"')
    return out


def gen_gamedb_packages(repo, args):
    """every source package and licensed builder in gamedb/"""
    _, o = _opts(args, {"root"})
    root = o.get("root", "../gamedb").rstrip("/")
    repo = Path(repo)
    out = ["| Path (in gamedb/) | For | Source | Patches |", "|---|---|---|---|"]
    rows = []
    for kind in ("engines", "games"):
        for sub in ("packages", "licensed"):
            for d in sorted((repo / "gamedb" / kind).glob("*/%s/*/" % sub)):
                rows.append((kind, d))
    for kind, d in rows:
        oid = d.parent.parent.name
        title, path = _owner(repo, kind, oid)
        c = _conf(d / "package.conf")
        t = c.get("TYPE")
        if t == "debsrc":
            src = "Debian's `%s` %s" % (c.get("SOURCE", d.name), c.get("VERSION", ""))
        elif t == "upstream":
            src = "upstream tarball (`upstream.mk`)"
        elif t == "native":
            src = "native (ours)"
        else:
            scripts = sorted(x.name for x in d.glob("*.sh"))
            src = "script %s" % ", ".join("`%s`" % s for s in scripts) if scripts else "?"
        n = len(list((d / "patches").glob("*.patch"))) if (d / "patches").is_dir() else 0
        extra = [x for x in ("packaging", "debian") if (d / x).is_dir()]
        patches = str(n) if n else "none"
        if extra:
            patches += " (+ %s)" % ", ".join("`%s/`" % x for x in extra)
        rel = d.relative_to(repo / "gamedb").as_posix() + "/"
        what = "%s%s" % (title, " (engine)" if kind == "engines" else "")
        if d.parent.name == "licensed":
            what += ", licensed data"
        out.append("| `%s` | [%s](%s/%s) | %s | %s |" % (rel, what, root, path, src, patches))
    return "\n".join(out) + "\n"


def gen_reload_rules(repo, args):
    """the reload segment of every game whose benchmark has one"""
    _, o = _opts(args, {"root"})
    root = o.get("root", "../gamedb").rstrip("/")
    repo = Path(repo)
    out = ["| Game | Reload segment of a pass |", "|---|---|"]
    rows = []
    for f in sorted((repo / "gamedb" / "games").glob("*/benchmark/analysis.toml")):
        r = tomllib.loads(f.read_text()).get("reload")
        if not r:
            continue
        gid = f.parents[1].name
        title, path = _owner(repo, "games", gid)
        if "re" in r:
            seg = "pass start to %s s after its first out.log line matching `%s`" % (_num(r.get("after", 0)), r["re"])
        elif "fixed" in r:
            seg = "pass start + %s s" % _num(r["fixed"])
        else:
            raise ValueError("%s: [reload] has neither re nor fixed" % f)
        rows.append((title.lower(), "| [%s](%s/%s) | %s |" % (title, root, path, seg)))
    out += [r for _, r in sorted(rows)]
    out.append("| every other game | none: one race or session, or a window that leaves the reload out |")
    return "\n".join(out) + "\n"


GENERATORS = {
    "reload-rules": gen_reload_rules,
    "batch-table": gen_batch_table,
    "licensed-table": gen_licensed_table,
    "gamedb-packages": gen_gamedb_packages,
}
