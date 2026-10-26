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
evidence tables, research tables, "bugs the games found". For now there is one, licensed-names.
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


GENERATORS = {
    "licensed-names": gen_licensed_names,
}


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
