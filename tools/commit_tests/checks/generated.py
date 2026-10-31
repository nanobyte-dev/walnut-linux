"""generated: what is generated from the tree is in step with it, each part run when its inputs changed:
  - .github/workflows (github-ci/gen-workflows.py --check): packages, their package.conf, the licensed names;
  - the docs' generated blocks (tools/gamedb/blocks.py --check): the docs and the GameDB data they show;
  - tools/gamedb/gdp-manifest.json against game-data-packager's patch series: the series hash
    gdp-manifest.py wrote (tools/gamedb/gdp-manifest.series) is the series' hash now."""
import importlib.util
import subprocess
import sys

import core

NAME = "generated"
WORKFLOW_INPUTS = ("**/package.conf", "github-ci/**", ".github/**", "tools/pkg-dir.sh", "tools/licensed.py",
                   "gamedb/*/*/build.toml", "gamedb/*/*/licensed/**", "packages/licensed/**")
BLOCK_INPUTS = ("**/*.md", "gamedb/**", "tools/gamedb/*.py", "bench/analysis/*.py", "tools/licensed.py")
GDP_INPUTS = ("packages/patched/tools/game-data-packager/patches/**", "tools/gamedb/gdp-manifest.json",
              "tools/gamedb/gdp-manifest.series", "tools/gamedb/gdp-manifest.py")
TARGETS = WORKFLOW_INPUTS + BLOCK_INPUTS + GDP_INPUTS
SHARED_INPUTS = ()
PARTS = (("workflows", WORKFLOW_INPUTS), ("blocks", BLOCK_INPUTS), ("gdp-manifest", GDP_INPUTS))


def select(ctx):
    paths = [p for c in ctx.changes for p in c.paths()]
    own = ctx.mode == "all" or any(core.matches(p, core.own_inputs(NAME)) for p in paths)
    parts = [n for n, globs in PARTS if own or any(core.matches(p, globs) for p in paths)]
    return bool(parts), parts


def tool(ctx, what, args):
    r = subprocess.run([sys.executable, "-I"] + args, cwd=ctx.root, capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        lines = [ln for ln in (r.stdout + r.stderr).splitlines() if ln.strip()]
        for ln in lines[-40:] or ["exit %d" % r.returncode]:
            ctx.error(None, "%s: %s" % (what, ln.strip()))


def gdp(ctx):
    spec = importlib.util.spec_from_file_location("gdp_manifest", ctx.path("tools/gamedb/gdp-manifest.py"))
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ctx.path("tools/gamedb")))
    try:
        spec.loader.exec_module(m)
    finally:
        sys.path.remove(str(ctx.path("tools/gamedb")))
    want = m.series_hash(ctx.root)
    have = (ctx.read("tools/gamedb/gdp-manifest.series") or "").split()
    if not have or have[0] != want:
        ctx.error("tools/gamedb/gdp-manifest.json", "not in step with game-data-packager's patch series (the series "
                  "hash in tools/gamedb/gdp-manifest.series is %s): make game-data-packager-deb, then make gdp-manifest" % (
                      "missing" if not have else "an older one"))


def run(ctx, parts):
    if "workflows" in parts:
        tool(ctx, "gen-workflows.py --check", ["github-ci/gen-workflows.py", "--check"])
    if "blocks" in parts:
        tool(ctx, "blocks.py --check", ["tools/gamedb/blocks.py", "--check"])
    if "gdp-manifest" in parts:
        gdp(ctx)
