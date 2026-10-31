"""python: every Python file compiles (compile(): no __pycache__ written), and the pinned ruff
(tools/ruff-version) finds no unused import (F401), redefinition (F811), undefined name (F821) or unused
local (F841). Python files: *.py, and scripts whose first line names python."""
import os
import re
import shutil
import warnings
from pathlib import Path

NAME = "python"
TARGETS = ("**",)
SHARED_INPUTS = ("tools/ruff-version",)
RULES = "F401,F811,F821,F841"


def accept(ctx, path):
    if path.endswith(".py"):
        return True
    if "." in Path(path).name:
        return False
    b = ctx.read_bytes(path)
    return bool(b) and b.startswith(b"#!") and b"python" in b.split(b"\n", 1)[0]


def ruff(ctx):
    pin = ctx.read("tools/ruff-version")
    want = pin.strip() if pin else None
    exe = shutil.which("ruff") or str(Path.home() / ".local" / "bin" / "ruff")
    hint = "the pinned ruff %s (tools/ruff-version): pip install --user ruff==%s" % (want, want) if want else \
        "ruff (tools/ruff-version names the version to install)"
    if not os.path.exists(exe):
        exe = "ruff"
    return ctx.tool("ruff", [exe], hint, want=r"\bruff %s\b" % re.escape(want) if want else None)


def run(ctx, files):
    paths = []
    for c in files:
        if c.status == "D":
            continue
        b = ctx.read_bytes(c.path)
        if b is None:
            continue
        paths.append(c.path)
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            try:
                compile(b, c.path, "exec", dont_inherit=True)
            except SyntaxError as e:
                ctx.error(c.path, "doesn't compile: line %s: %s" % (e.lineno, e.msg))
            except ValueError as e:
                ctx.error(c.path, "doesn't compile: %s" % e)
        for x in w:
            ctx.warn(c.path, "line %s: %s" % (getattr(x, "lineno", "?"), x.message))
    if not paths:
        return
    exe = ruff(ctx)
    if not exe:
        return
    for i in range(0, len(paths), 400):
        r = ctx.run_tool(exe + ["check", "--isolated", "--no-cache", "--select", RULES, "--output-format", "concise",
                                "--quiet", "--"] + paths[i:i + 400])
        if r.returncode not in (0, 1):
            ctx.error(None, "ruff failed (exit %d): %s" % (r.returncode, (r.stderr or r.stdout).strip()[:500]))
            continue
        for line in r.stdout.splitlines():
            m = re.match(r"(.+?):(\d+):\d+: (F\d+) (.*)", line)
            if m:
                ctx.error(m[1], "line %s: %s %s" % (m[2], m[3], m[4].replace("[*] ", "")))
