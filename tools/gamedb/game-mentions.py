#!/usr/bin/env python3
"""game-mentions.py [--list] [--all-titles] [FILE...]: games named in the docs without a GameDB link near
them (the GameDB plan, step 8: outside gamedb/ a game appears only as a generated block, a reference or
link, frozen history or a test fixture). A warning only: it always exits 0 (make test prints its counts).

What it reads (FILE... instead, when given): docs/**/*.md but not docs/archive/ (frozen history), and
README.md. What a mention is: a game's title as its index.md has it (case-sensitive; a title's part
before " (" counts too, "Diablo (DevilutionX)" -> "Diablo"), an engine's title from its engine.md, or
the id of a published game or an engine as a lower-case word; not inside ``` code blocks, `inline code`,
a heading, a <!-- gamedb:NAME --> generated block or a link's target, and not as part of a longer name,
a path or a package name ("sauerbraten-data", "games/openarena"). With --all-titles every game's title
counts, else only the games whose bundle is published or tried (status tried, blocked or dropped):
a candidate's or an impossible game's name in a survey is not what this looks for.
Linked: its section (from one heading to the next; a generated block in it counts) holds a link into
that game's bundle (gamedb/games/<id>/...) or engine (gamedb/engines/<id>/...), or a reference gamedb:<id> or
gamedb:<id>/<investigation>. A game's engine counts for it, and the engine's games for the engine.
Output: per file the number of (section, game) pairs without a link, then the totals; --list prints each
one (file:line of its first mention: name). --fix links each such section's first title mention (not
one inside a link's text) to the game's bundle, gamedb/games/<id>/ (an engine: gamedb/engines/<e>/).
"""
import argparse
import os
import re
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
GAMEDB = REPO / "gamedb"
BLOCK_RE = re.compile(r"<!--\s*(/?)gamedb:([A-Za-z0-9_-]+)")
FENCE_RE = re.compile(r"\s*(```+|~~~+)")
CODE_RE = re.compile(r"(`+).+?\1")
LINK_TARGET_RE = re.compile(r"\]\([^)\s]*(?:\s+\"[^\"]*\")?\)")
NAME_EDGE = r"[\w/.-]"          # a name this close to a word character, a path or a dash is something else


def front_matter(f):
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+", f.read_text(), re.S)
    return tomllib.loads(m[1]) if m else {}


def names(root=GAMEDB, all_titles=False):
    """[(name, owner id, kind)] for every game and engine; kind is "title" or "id" """
    out = []
    games = {}
    for d in sorted((root / "games").iterdir()) if (root / "games").is_dir() else []:
        f = d / "index.md"
        if not f.is_file():
            continue
        fm = front_matter(f)
        draft = bool(fm.get("draft"))
        if draft and not all_titles and fm.get("status") not in ("tried", "blocked", "dropped"):
            continue
        games[d.name] = fm
        title = fm.get("title") or d.name
        for t in {title, title.split(" (")[0]}:
            out.append((t, d.name, "title"))
        if not draft:
            out.append((d.name, d.name, "id"))
    for d in sorted((root / "engines").iterdir()) if (root / "engines").is_dir() else []:
        f = d / "engine.md"
        if f.is_file():
            fm = front_matter(f)
            out.append((fm.get("title") or d.name, d.name, "title"))
            out.append((d.name, d.name, "id"))
    return out


def related(root=GAMEDB):
    """{id: {ids whose link also counts}}: a game and its engine, both ways"""
    rel = {}
    for d in (root / "engines").iterdir() if (root / "engines").is_dir() else []:
        f = d / "engine.md"
        if f.is_file():
            for g in front_matter(f).get("games", []):
                rel.setdefault(g, set()).add(d.name)
                rel.setdefault(d.name, set()).add(g)
    for d in (root / "games").iterdir() if (root / "games").is_dir() else []:
        b = d / "build.toml"
        if b.is_file():
            e = tomllib.loads(b.read_text()).get("engine")
            if isinstance(e, str):
                rel.setdefault(d.name, set()).add(e)
                rel.setdefault(e, set()).add(d.name)
    return rel


def linked_ids(text):
    """the game and engine ids a paragraph links to"""
    ids = set(re.findall(r"gamedb/(?:games|engines)/([a-z0-9][a-z0-9-]*)", text))
    ids |= set(re.findall(r"(?<![\w/.-])gamedb:([a-z0-9][a-z0-9-]*)", text))
    ids.discard("batch")
    return ids


def sections(lines):
    """[([(line number, text to search)], the section's whole text)]: the prose from one heading to the
    next, without fenced code, the headings themselves and generated blocks"""
    out, cur, raw, fence, block = [], [], [], None, None

    def flush():
        if cur or raw:
            out.append((list(cur), "\n".join(raw)))
        cur.clear()
        raw.clear()

    for n, line in enumerate(lines, 1):
        m = FENCE_RE.match(line)
        if fence:
            if m and m[1][0] == fence[0] and len(m[1]) >= len(fence):
                fence = None
            continue
        if m:
            fence = m[1]
            continue
        b = BLOCK_RE.search(line)
        if b:
            block = None if b[1] else b[2]
            continue
        if block:
            raw.append(line)        # a generated block's links count for the prose around it
            continue
        if re.match(r"#{1,6} ", line):
            flush()
            continue
        cur.append((n, line))
        raw.append(line)
    flush()
    return out


def prose(text):
    """text without inline code and link targets (their characters blanked, so columns stay)"""
    text = CODE_RE.sub(lambda m: " " * len(m[0]), text)
    return LINK_TARGET_RE.sub(lambda m: "]" + " " * (len(m[0]) - 1), text)


LINK_RE = re.compile(r"!?\[(?:[^\[\]\n]|\[[^\[\]\n]*\])*\]\([^)\s]*(?:\s+\"[^\"]*\")?\)")


def scan(files, table, rel):
    """{file: [{line, name, id, count, col, kind}]}: per section and game without a link there, its first
    mention (col: where a link can go, the first title mention outside a link's text; None if none) and
    how many lines name it"""
    pats = [(re.compile(r"(?<!%s)%s(?!%s)" % (NAME_EDGE, re.escape(name), r"[\w/-]|\.\w")), name, owner, kind)
            for name, owner, kind in sorted(table, key=lambda t: -len(t[0]))]
    found = {}
    for f in files:
        try:
            lines = (REPO / f).read_text(errors="replace").split("\n")
        except OSError:
            continue
        for para, whole in sections(lines):
            ok = set(linked_ids(whole))
            for i in list(ok):
                ok |= rel.get(i, set())
            hits = {}
            for n, line in para:
                text = prose(line)
                inlink = [(m.start(), m.end()) for m in LINK_RE.finditer(line)]
                named = set()
                for rx, name, owner, kind in pats:
                    if owner in ok:
                        continue
                    for m in rx.finditer(text):
                        if any(a <= m.start() < b for a, b in named_spans(named, n)):
                            continue
                        h = hits.setdefault(owner, {"line": n, "name": name, "id": owner, "count": 0,
                                                    "col": None, "kind": kind, "seen": set()})
                        if n not in h["seen"]:
                            h["seen"].add(n)
                            h["count"] += 1
                        named.add((n, m.start(), m.end()))
                        if h["col"] is None and kind == "title" and not any(a <= m.start() < b for a, b in inlink):
                            h["col"] = (n, m.start(), m.end())
            for h in hits.values():
                del h["seen"]
                found.setdefault(f, []).append(h)
    return found


def named_spans(named, n):
    """the spans of line n already taken by a longer name"""
    return [(a, b) for ln, a, b in named if ln == n]


def fix(found):
    """link each section's first title mention to the game's bundle (or the engine's folder) in place;
    returns the number of links added"""
    added = 0
    for f, hits in found.items():
        path = REPO / f
        lines = path.read_text().split("\n")
        todo = sorted((h for h in hits if h["col"]), key=lambda h: h["col"], reverse=True)
        for h in todo:
            n, a, b = h["col"]
            kind = "games" if (GAMEDB / "games" / h["id"]).is_dir() else "engines"
            target = os.path.relpath(GAMEDB / kind / h["id"], path.parent) + "/"
            line = lines[n - 1]
            lines[n - 1] = "%s[%s](%s)%s" % (line[:a], line[a:b], target, line[b:])
            added += 1
        path.write_text("\n".join(lines))
    return added


def default_files():
    out = sorted(p.relative_to(REPO).as_posix() for p in (REPO / "docs").glob("**/*.md")
                 if not p.relative_to(REPO).as_posix().startswith("docs/archive/"))
    return out + (["README.md"] if (REPO / "README.md").is_file() else [])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true", help="print every unlinked mention")
    ap.add_argument("--all-titles", action="store_true", help="every game's title, candidates included")
    ap.add_argument("--fix", action="store_true", help="link each section's first title mention to its bundle")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args(argv)
    files = a.files or default_files()
    found = scan(files, names(all_titles=a.all_titles), related())
    if a.fix:
        print("game-mentions: %d links added" % fix(found))
        found = scan(files, names(all_titles=a.all_titles), related())
    games = set()
    for f in sorted(found):
        hits = found[f]
        games |= {h["id"] for h in hits}
        print("%s: %d" % (f, len(hits)))
        if a.list:
            for h in sorted(hits, key=lambda h: h["line"]):
                print("  %s:%d: %s (%s; %d line%s in the section)" % (f, h["line"], h["name"], h["id"], h["count"],
                                                                    "" if h["count"] == 1 else "s"))
    total = sum(len(v) for v in found.values())
    print("game-mentions: %d sections name a game without linking it (%d games, %d of %d files; warning only)"
          % (total, len(games), len(found), len(files)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
