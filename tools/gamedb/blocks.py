#!/usr/bin/env python3
"""blocks.py --check | --write [FILE...]: the generated blocks in our docs (the GameDB plan: a game
fact outside gamedb/ is a generated view or a reference). A block is

    <!-- gamedb:NAME ARGS -->
    ...generated text...
    <!-- /gamedb:NAME -->

and its text is whatever the generator NAME (GENERATORS below) makes of ARGS (words after the name).
--write puts every generator's current text in its blocks; --check (make test) exits 1 when a block
is stale, names no known generator, or isn't closed. Files: docs/**/*.md (not docs/archive/),
gamedb/**/*.md, README.md and every top folder's README.md, or FILE....

A generator is a function (repo, args) -> markdown text, registered in GENERATORS. Planned (the plan's
"Generated from gamedb"): the README game table, GAMEDB's calibration, the TESTS queue, driver-doc
evidence tables, research tables, "bugs the games found". Now: licensed-names, calibration (docs/GAMEDB.md:
the rules-1 calibration, score.py over the summary batch), suite-table (README.md: the suite's games, the
summary batch's numbers and each game's medal from its played sessions, as the catalog computes it),
session-queue (docs/TESTS.md: the played-session queue from the games' todo.toml, with each game's medal),
todo-summary (docs/PLAN.md, docs/backlog/games.md: the per-game open work, counted from the todo.toml files).
"""
import argparse
import importlib.util
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
OPEN_RE = re.compile(r"<!-- gamedb:([a-z0-9-]+)((?:\s+[^\s>-][^\s>]*)*)\s*-->")
CLOSE = "<!-- /gamedb:%s -->"


def _licensed(repo):
    spec = importlib.util.spec_from_file_location("licensed", Path(repo) / "tools" / "licensed.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def gen_licensed_names(repo, args):
    """the licensed package names (tools/licensed.py names) as a list of `name` items"""
    if args:
        raise ValueError("licensed-names takes no arguments")
    return "".join("- `%s`\n" % n for n in sorted(_licensed(repo).names(repo)))


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _num(x):
    """a number as the tables write it: 0.05, 2.5, 43, 3900"""
    return ("%.2f" % x).rstrip("0").rstrip(".") if isinstance(x, float) else str(x)


def gen_calibration(repo, args):
    """docs/GAMEDB.md's calibration table: each row of the summary batch (score.CALIBRATION_BATCH) scored
    by score.calibrate(), the numbers behind each aspect next to its score"""
    if args:
        raise ValueError("calibration takes no arguments")
    score = _load("score", Path(repo) / "tools" / "gamedb" / "score.py")
    out = ["| Game | Frame rate | Smoothness | Sound | Mean | Weakest | Medal |", "|---|---|---|---|---|---|---|"]
    for row in score.calibration(Path(repo) / "gamedb"):
        label, median, low, stall_s, longest, faults = row
        s, r = score.calibrate(row)
        medal = r["medal"] + (" (capped)" if r["capped"] else " (%s)" % r["reason"] if r["reason"] else "")
        out.append("| %s | %.0f (%s fps, 1%% low %s) | %.0f (%s s of stalls, longest %s ms) | %.0f (%s faults a minute) "
                   "| %.0f | %.0f (%s) | %s |" % (
                       label, s["frame"], _num(median), _num(low), s["smooth"], _num(stall_s), _num(longest),
                       s["sound"], _num(faults), r["score"], s[r["weakest"]], r["weakest"], medal))
    return "\n".join(out) + "\n"


SITE = "https://walnut.nanobyte.dev/gamedb/games/%s/"


def gen_suite_table(repo, args):
    """README.md's suite table: the summary batch's rows (label, steady fps, stalls / 1% low / longest,
    free RAM / VRAM, the note on picture and sound) and the game's medal from its played sessions
    (tools/gamedb/catalog.py's rating; a draft or a game without sessions has none yet)"""
    if args:
        raise ValueError("suite-table takes no arguments")
    repo = Path(repo)
    score = _load("score", repo / "tools" / "gamedb" / "score.py")
    br = _load("benchrecords", repo / "bench" / "benchrecords.py")
    catalog = _load("catalog", repo / "tools" / "gamedb" / "catalog.py")
    games = {g["id"]: g for g in catalog.build(repo / "gamedb" / "games", repo)["games"]}
    out = ["| Game | Medal (played) | Steady fps | Stalls / 1% low / longest | Free RAM / VRAM (MB) | Picture, sound |",
           "|---|---|---|---|---|---|"]
    for gid, _, e in br.summary(score.CALIBRATION_BATCH, repo / "gamedb"):
        g = games.get(gid)
        name = "[%s](%s)" % (e["label"], SITE % gid) if g else e["label"]
        rating = (g or {}).get("rating") or {}
        medal = "%s · %d" % (rating["medal"], rating["score"]) if rating.get("score") is not None else "not rated yet"
        longest = "%s s" % _num(e["longest_play_ms"] / 1000) if e["longest_play_ms"] >= 1000 else "%s ms" % _num(e["longest_play_ms"])
        out.append("| %s | %s | %s | %s s / %s / %s | %s / %s | %s |" % (
            name, medal, _num(e["fps_steady"]), _num(e["stall_play_s"]), _num(e["low1_play"]), longest,
            _num(e["ram_free"]), _num(e["vram"]), e.get("note", "")))
    return "\n".join(out) + "\n"


def _todos(repo):
    """[(kind of folder, id, title, [items])] of every gamedb/games/<id>/todo.toml and engines/<e>/todo.toml
    (the title from index.md or engine.md)"""
    import tomllib
    check = _load("check", Path(repo) / "tools" / "gamedb" / "check.py")
    out = []
    for kind, page in (("games", "index.md"), ("engines", "engine.md")):
        for d in sorted((Path(repo) / "gamedb" / kind).glob("*/todo.toml")):
            items = tomllib.loads(d.read_text()).get("item", [])
            out.append((kind, d.parent.name, check.front_matter(d.parent / page).get("title", d.parent.name), items))
    return out


def _cell(text):
    """a todo text as one markdown table cell"""
    return " ".join(str(text).split()).replace("|", "\\|")


def gen_session_queue(repo, args):
    """docs/TESTS.md's played-session queue: every session item with an order (gamedb/games/<id>/todo.toml), in
    that order, with the game's medal now (catalog.py's rating from its played sessions)"""
    if args:
        raise ValueError("session-queue takes no arguments")
    repo = Path(repo)
    catalog = _load("catalog", repo / "tools" / "gamedb" / "catalog.py")
    games = {g["id"]: g for g in catalog.build(repo / "gamedb" / "games", repo)["games"]}
    rows = [(it["order"], gid, title, it) for kind, gid, title, items in _todos(repo) if kind == "games"
            for it in items if it.get("kind") == "session" and "order" in it]
    out = ["| # | Game | Medal now | Launcher (PS3 and PC) | PC side | Notes |", "|---|---|---|---|---|---|"]
    for order, gid, title, it in sorted(rows, key=lambda r: (r[0], r[1])):
        g = games.get(gid)
        rating = (g or {}).get("rating") or {}
        medal = "%s · %d" % (rating["medal"], rating["score"]) if rating.get("score") is not None else "not rated yet"
        out.append("| %d | %s | %s | %s | %s | %s |" % (
            order, "[%s](%s)" % (title, SITE % gid) if g else title, medal, _cell(it.get("launcher", "")),
            _cell(it.get("pc", "")), _cell(it["text"])))
    return "\n".join(out) + "\n"


def gen_todo_summary(repo, args):
    """the per-game open work: a row per game (or engine) with a todo.toml, its items by kind and the PLAN items
    they came from, linking the file. Args: the path from the doc to gamedb/ (default ../gamedb); `plan`: only
    the games with an item from docs/PLAN.md (source "PLAN N.N")"""
    words = list(args)
    plan_only = "plan" in words
    words = [w for w in words if w != "plan"]
    if len(words) > 1:
        raise ValueError("todo-summary takes [PATH-TO-GAMEDB] [plan]")
    root = (words or ["../gamedb"])[0].rstrip("/")
    plan_re = re.compile(r"PLAN (\d+\.\d+)")
    out = ["| Game | Open items | Kinds | From the plan |", "|---|---|---|---|"]
    for kind, gid, title, items in sorted(_todos(repo), key=lambda t: (t[0] != "games", t[2].lower())):
        plan = sorted({m for it in items for m in plan_re.findall(it.get("source", ""))},
                      key=lambda x: tuple(map(int, x.split("."))))
        if not items or (plan_only and not plan):
            continue
        kinds = {}
        for it in items:
            kinds[it["kind"]] = kinds.get(it["kind"], 0) + 1
        out.append("| [%s](%s/%s/%s/todo.toml) | %d | %s | %s |" % (
            title + (" (engine)" if kind == "engines" else ""), root, kind, gid, len(items),
            ", ".join("%d %s" % (n, k) for k, n in sorted(kinds.items())), ", ".join(plan)))
    return "\n".join(out) + "\n"


GENERATORS = {
    "licensed-names": gen_licensed_names,
    "calibration": gen_calibration,
    "suite-table": gen_suite_table,
    "session-queue": gen_session_queue,
    "todo-summary": gen_todo_summary,
}
# docs/games/'s tables: driver-bugs, suite-recipes (tools/gamedb/gamesblocks.py)
GENERATORS.update(_load("gamesblocks", Path(__file__).resolve().with_name("gamesblocks.py")).GENERATORS)


def sources(repo):
    out = set()
    for pat in ("docs/**/*.md", "gamedb/**/*.md", "README.md", "*/README.md"):
        for f in Path(repo).glob(pat):
            rel = f.relative_to(repo).as_posix()
            if f.is_file() and not rel.startswith("docs/archive/"):
                out.add(rel)
    return sorted(out)


def render(text, repo, where):
    """(new text, [problems]) with every block's contents regenerated"""
    out, pos, probs = [], 0, []
    for m in OPEN_RE.finditer(text):
        if m.start() < pos:
            continue		# inside a block already handled
        name, args = m[1], m[2].split()
        line = text.count("\n", 0, m.start()) + 1
        close = CLOSE % name
        end = text.find(close, m.end())
        if end < 0:
            probs.append("%s:%d: <!-- gamedb:%s --> is never closed (%s)" % (where, line, name, close))
            continue
        gen = GENERATORS.get(name)
        if gen is None:
            probs.append("%s:%d: no block generator %r (tools/gamedb/blocks.py GENERATORS)" % (where, line, name))
            continue
        try:
            body = gen(repo, args)
        except Exception as e:  # a generator that fails is a problem of this block, reported with it
            probs.append("%s:%d: gamedb:%s %s: %s" % (where, line, name, " ".join(args), e))
            continue
        if body and not body.endswith("\n"):
            body += "\n"
        out.append(text[pos:m.end()] + "\n" + body)
        pos = end
    out.append(text[pos:])
    return "".join(out), probs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true")
    g.add_argument("--write", action="store_true")
    ap.add_argument("files", nargs="*")
    ap.add_argument("--repo", default=str(REPO), help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    repo = Path(a.repo).resolve()
    files = a.files or sources(repo)
    stale, probs, blocks = [], [], 0
    for rel in files:
        p = Path(rel) if Path(rel).is_absolute() else repo / rel
        text = p.read_text()
        blocks += len(OPEN_RE.findall(text))
        new, pr = render(text, repo, rel)
        probs += pr
        if new != text:
            if a.write:
                p.write_text(new)
                print("blocks: wrote %s" % rel)
            else:
                stale.append(rel)
    for s in stale:
        print("!! %s: a generated block is stale (python3 tools/gamedb/blocks.py --write)" % s)
    for pr in probs:
        print("!! " + pr)
    print("blocks.py: %d files, %d blocks, %d stale, %d problems" % (len(files), blocks, len(stale), len(probs)))
    return 1 if probs or (stale and a.check) else 0


if __name__ == "__main__":
    sys.exit(main())
