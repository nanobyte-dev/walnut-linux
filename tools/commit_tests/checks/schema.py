"""schema: every GameDB data file validates against its schema in gamedb/schemas/, and every file under
gamedb/ is of a known kind or on the no-schema list (tools/gamedb/schema.py: validate(path), kind_of(path),
coverage, targets_and_shared_inputs()). A changed file is validated alone; a changed schema (or
schema.py, or another shared input it names) validates every file of its kinds. Until schema.py exists
the check says so and passes."""
import importlib.util
import inspect
import sys

import core

NAME = "schema"
TARGETS = ("gamedb/**",)
SHARED_INPUTS = ("gamedb/schemas/**", "tools/gamedb/schema.py")
MODULE = "tools/gamedb/schema.py"


def load(ctx):
    path = ctx.path(MODULE)
    if not path.is_file():
        return None
    spec = importlib.util.spec_from_file_location("gamedb_schema", path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(path.parent))
    try:
        spec.loader.exec_module(m)
    finally:
        sys.path.remove(str(path.parent))
    return m


def globs(m):
    """(targets, shared inputs) from schema.py's targets_and_shared_inputs(), whatever its shape: a pair of
    lists, a dict with targets/shared_inputs, or {kind: pair} / [pair]"""
    t = m.targets_and_shared_inputs() if hasattr(m, "targets_and_shared_inputs") else (TARGETS, SHARED_INPUTS)
    if isinstance(t, dict) and ("targets" in t or "shared_inputs" in t or "shared" in t):
        return tuple(t.get("targets", ())), tuple(t.get("shared_inputs", t.get("shared", ())))
    if isinstance(t, (tuple, list)) and len(t) == 2 and all(isinstance(x, (list, tuple, set)) for x in t) and \
            all(isinstance(g, str) for x in t for g in x):
        return tuple(t[0]), tuple(t[1])
    pairs = t.values() if isinstance(t, dict) else t
    targets, shared = [], []
    for p in pairs:
        if isinstance(p, dict):
            targets += p.get("targets", ()); shared += p.get("shared_inputs", p.get("shared", ()))  # noqa: E702
        else:
            targets += p[0]; shared += p[1]  # noqa: E702
    return tuple(dict.fromkeys(targets)), tuple(dict.fromkeys(shared))


def select(ctx):
    paths = [p for c in ctx.changes for p in c.paths()]
    try:
        m = load(ctx)
    except Exception as e:  # a broken schema.py blocks: run() reports it
        ctx._schema = e
        return True, []
    ctx._schema = m
    if m is None:
        return any(core.matches(p, TARGETS) for p in paths), []
    targets, shared = globs(m)
    shared = shared + SHARED_INPUTS + core.own_inputs(NAME)
    changed = [c for c in ctx.changes if c.status != "D" and core.matches(c.path, targets)]
    gamedb_new = [c for c in ctx.changes if c.status in ("A", "R", "*") and core.matches(c.path, ("gamedb/**",))]
    full = ctx.mode == "all" or any(core.matches(p, shared) for p in paths)
    if full:
        seen = {c.path for c in changed}
        changed += [core.Change("*", p) for p in ctx.tracked() if p not in seen and core.matches(p, targets)]
    return bool(changed or gamedb_new or full), (changed, gamedb_new, full)


def findings(ctx, path, res):
    """schema.py's answer, whatever its shape: [str] errors, (errors, warnings), {"errors":, "warnings":},
    [(severity, text)] or objects with .severity/.message"""
    if not res:
        return
    if isinstance(res, dict):
        errs, warns = res.get("errors", []), res.get("warnings", [])
    elif isinstance(res, tuple) and len(res) == 2 and all(isinstance(x, list) for x in res):
        errs, warns = res
    else:
        errs, warns = [], []
        for x in res if isinstance(res, (list, tuple)) else [res]:
            sev, text = ("error", x) if isinstance(x, str) else (x if isinstance(x, tuple) else (getattr(x, "severity", "error"), getattr(x, "message", str(x))))
            (warns if str(sev).startswith("warn") else errs).append(text)
    for e in errs:
        ctx.error(path, str(e))
    for w in warns:
        ctx.warn(path, str(w))


def call(fn, ctx, path):
    """fn(path) with the path as schema.py takes it: a repository-relative string, or a Path"""
    try:
        params = list(inspect.signature(fn).parameters)
    except (TypeError, ValueError):
        params = []
    if len(params) >= 2 and params[1] in ("repo", "root"):
        return fn(path, ctx.root)
    return fn(ctx.path(path))


def run(ctx, files):
    m = getattr(ctx, "_schema", None)
    if isinstance(m, Exception):
        raise m
    if m is None:
        ctx.notice(None, "%s isn't there yet (the schema work): GameDB files aren't validated against schemas" % MODULE)
        return
    changed, new, full = files
    for c in changed:
        findings(ctx, c.path, call(m.validate, ctx, c.path))
    cov = getattr(m, "coverage", None)
    if cov is None:
        for c in new:
            if hasattr(m, "kind_of") and call(m.kind_of, ctx, c.path) is None:
                ctx.error(c.path, "a file of no known GameDB kind: add its schema (gamedb/schemas/) or put it on schema.py's no-schema list")
        return
    paths = [c.path for c in new] if not full else [p for p in ctx.tracked() if p.startswith("gamedb/")]
    for x in cov(paths, ctx.root) if paths else []:
        p, _, text = str(x).partition(": ")
        ctx.error(p, text or str(x))
