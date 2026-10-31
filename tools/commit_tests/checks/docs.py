"""docs: the links of each changed doc resolve (tools/doclinks.py --strict on it). Deleting or moving a
doc, or changing doclinks.py, checks every doc it reads (a link elsewhere may point at the old place)."""
import subprocess
import sys

import core

NAME = "docs"
TARGETS = ("docs/**/*.md", "gamedb/**/*.md", "README.md", "*/README.md")
SHARED_INPUTS = ("tools/doclinks.py",)
EXCLUDE = ("docs/archive/**",)


def select(ctx):
    paths = [p for c in ctx.changes for p in c.paths()]
    moved = any(c.status in ("D", "R") and core.matches(c.old or c.path, TARGETS) for c in ctx.changes)
    full = ctx.mode == "all" or moved or any(core.matches(p, SHARED_INPUTS + core.own_inputs(NAME)) for p in paths)
    changed = [c.path for c in ctx.changes if c.status != "D" and core.matches(c.path, TARGETS) and not core.matches(c.path, EXCLUDE)]
    return full or bool(changed), (full, changed)


def run(ctx, files):
    full, changed = files
    r = subprocess.run([sys.executable, "-I", "tools/doclinks.py", "--strict"] + ([] if full else changed),
                       cwd=ctx.root, capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        lines = [ln for ln in (r.stdout + r.stderr).splitlines() if ln.strip() and not ln.startswith("doclinks:")]
        for ln in lines[:200] or ["exit %d" % r.returncode]:
            ctx.error(None, "doclinks: " + ln.strip())
