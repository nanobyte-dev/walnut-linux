"""deletions: what is kept forever stays (a rename is no deletion). Refused:
  - a published game's or engine's bundle: its index.md (engine.md) deleted while the base commit had it
    published (no draft = true);
  - a played session's evidence: any file under gamedb/games/*/rating/sessions/;
  - benchmark records: gamedb/games/*/benchmark/results/*.toml, and a [[batch]] that leaves
    gamedb/benchmarks/batches.toml;
  - a patch still in a series: a deleted *.patch that its directory's series file still names."""
import re
import tomllib
from pathlib import PurePath

NAME = "deletions"
TARGETS = ("gamedb/games/*/index.md", "gamedb/engines/*/engine.md", "gamedb/games/*/rating/sessions/**",
           "gamedb/games/*/benchmark/results/*.toml", "gamedb/benchmarks/batches.toml", "**/*.patch")
SHARED_INPUTS = ()


def front_matter(text):
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+", text or "", re.S)
    return tomllib.loads(m[1]) if m else {}


def batch_names(text):
    try:
        return {b.get("name") for b in tomllib.loads(text).get("batch", [])} if text else set()
    except tomllib.TOMLDecodeError:
        return set()


def series_names(ctx, d):
    text = ctx.read(str(PurePath(d) / "series"))
    return {ln.split()[0] for ln in (text or "").splitlines() if ln.strip() and not ln.lstrip().startswith("#")}


def run(ctx, files):
    if ctx.mode == "all":
        return   # nothing is deleted in a whole-tree run
    for c in files:
        p = PurePath(c.path)
        if c.path == "gamedb/benchmarks/batches.toml":
            gone = batch_names(ctx.base_text(c.path)) - batch_names(ctx.read(c.path) or "")
            for n in sorted(x for x in gone if x):
                ctx.error(c.path, "batch %r removed: benchmark records are kept forever" % n)
            continue
        if c.status != "D":
            continue
        if p.full_match("gamedb/games/*/index.md") or p.full_match("gamedb/engines/*/engine.md"):
            try:
                fm = front_matter(ctx.base_text(c.path))
            except tomllib.TOMLDecodeError:
                fm = {}
            if fm and fm.get("draft") is not True:
                ctx.error(c.path, "deletes a published bundle (%s): published games stay; make it a draft only with "
                          "the user's say" % p.parent)
        elif p.full_match("gamedb/games/*/rating/sessions/**"):
            ctx.error(c.path, "deletes a played session's evidence: sessions are kept forever")
        elif p.full_match("gamedb/games/*/benchmark/results/*.toml"):
            ctx.error(c.path, "deletes benchmark records: they are kept forever")
        elif c.path.endswith(".patch"):
            for up in range(1, min(4, len(p.parts))):   # the series next to it, or in a folder above (ps3/0010-...)
                d = PurePath(*p.parts[:-up])
                if str(p.relative_to(d)) in series_names(ctx, d):
                    ctx.error(c.path, "deleted, but %s still names it" % (d / "series"))
