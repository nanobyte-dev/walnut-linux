"""core.py: what every commit test shares (tools/commit_tests/run.py is the command; docs/LAYOUT.md).

The model
- A run looks at a set of changes: the staged files (`git diff --cached --name-status -M`: added,
  modified, renamed with their old path, deleted) when a hook runs, a commit range A..B (`--range`), or
  every tracked file (`--all`). File contents are read from the working tree; the pre-commit hook
  refuses a commit whose staged files also have unstaged changes, so for a hook run the working tree's
  copy of a staged file is what gets committed.
- A check is a module in checks/ with NAME, TARGETS (globs of the files it judges), SHARED_INPUTS
  (globs of files that change how the targets are judged; its own module and this file always count)
  and run(ctx, files). A changed target is checked alone; a changed shared input runs the check over
  every tracked target. Globs are PurePath.full_match patterns (`**` crosses directories, `*` stays in
  one). WHOLE checks (the repository-wide validators: check.py, blocks.py, ...) take no file list: they
  run once when anything they read changed.
- A check reports with ctx.error(path, text) (blocks the commit) and ctx.warn(path, text) (printed).
  A check that raises blocks. A tool it needs that is missing or broken is an error (ctx.tool()).
- A deliberate exception is a line `Commit-Tests-Skip: <check>: <reason>` in the commit message; the
  checks in UNSKIPPABLE ignore it.
"""
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import time
import traceback
from pathlib import Path, PurePath

HERE = Path(__file__).resolve().parent
CHECKS_DIR = HERE / "checks"
# the checks, in the order they run and report (each one is checks/<module>.py)
ORDER = ("toml", "python", "shell", "private", "deletions", "gamedb", "schema", "batches", "desktop",
         "versions", "palette", "patch-register", "licensed", "generated", "docs")
UNSKIPPABLE = {"private"}
SKIP_RE = re.compile(r"^Commit-Tests-Skip:[ \t]*([A-Za-z0-9_-]+)[ \t]*:[ \t]*(\S.*)$", re.M)
EMPTY_TREE = "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
LFS_POINTER = b"version https://git-lfs.github.com/spec/v1"


def matches(path, globs):
    """path (a repository-relative string) matches one of globs (PurePath.full_match)"""
    p = PurePath(path)
    return any(p.full_match(g) for g in globs)


class Change:
    """one changed file: status A (added), M (modified), D (deleted), R (renamed: old is the old path),
    or * (not changed: a target checked again because a shared input changed, or --all)"""
    __slots__ = ("status", "path", "old")

    def __init__(self, status, path, old=None):
        self.status, self.path, self.old = status, path, old

    def paths(self):
        return [self.path] + ([self.old] if self.old else [])

    def __repr__(self):
        return "%s %s%s" % (self.status, self.old + " -> " if self.old else "", self.path)


def git(root, *args, check=True, binary=False, input=None):
    r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, input=input,
                       text=not binary, check=False)
    if check and r.returncode != 0:
        raise RuntimeError("git %s: %s" % (" ".join(args), (r.stderr if not binary else r.stderr.decode(errors="replace")).strip()))
    return r.stdout


def parse_name_status(out):
    """`git diff --name-status -M -z` output as [Change]"""
    parts, out_ = out.split("\0"), []
    i = 0
    while i < len(parts) and parts[i]:
        st = parts[i]
        if st[0] in "RC":
            out_.append(Change("R" if st[0] == "R" else "A", parts[i + 2], parts[i + 1] if st[0] == "R" else None))
            i += 3
        else:
            out_.append(Change({"T": "M"}.get(st[0], st[0]), parts[i + 1]))
            i += 2
    return out_


class Ctx:
    """one run: the changes, the message, where file contents come from, the findings"""

    def __init__(self, root, mode, changes, base=None, head=None, message=None, verbose=False):
        self.root = Path(root)
        self.mode = mode            # "hook", "range" or "all"
        self.changes = changes
        self.base = base            # the commit the changes are against (None: --all, or a first commit)
        self.head = head            # range mode: the end of the range
        self.message = message      # the commit message (hook: the file's text; range: every message)
        self.verbose = verbose
        self.findings = []          # (check, "error"|"warning"|"notice", path, text)
        self.check = None           # the check running now
        self._tools = {}
        self._tracked = None

    # --- findings
    def error(self, path, text):
        self.findings.append((self.check, "error", path, text))

    def warn(self, path, text):
        self.findings.append((self.check, "warning", path, text))

    def notice(self, path, text):
        self.findings.append((self.check, "notice", path, text))

    # --- files
    def path(self, rel):
        return self.root / rel

    def exists(self, rel):
        return (self.root / rel).is_file()

    def read_bytes(self, rel):
        """the working tree's bytes of rel, None when it isn't there"""
        try:
            return (self.root / rel).read_bytes()
        except (FileNotFoundError, IsADirectoryError, NotADirectoryError):
            return None

    def read(self, rel):
        b = self.read_bytes(rel)
        return None if b is None else b.decode("utf-8", errors="replace")

    def base_bytes(self, rel):
        """rel as the base commit has it (None: not there, or no base)"""
        if not self.base:
            return None
        r = subprocess.run(["git", "-C", str(self.root), "cat-file", "blob", "%s:%s" % (self.base, rel)],
                           capture_output=True)
        return r.stdout if r.returncode == 0 else None

    def base_text(self, rel):
        b = self.base_bytes(rel)
        return None if b is None else b.decode("utf-8", errors="replace")

    def staged_bytes(self, rel):
        """rel as it will be committed: the index's blob (hook), the range end's (range), the
        working tree's (--all reads the index too, for the LFS pointer check)"""
        spec = ":%s" % rel if self.mode in ("hook", "all") else "%s:%s" % (self.head, rel)
        r = subprocess.run(["git", "-C", str(self.root), "cat-file", "blob", spec], capture_output=True)
        return r.stdout if r.returncode == 0 else None

    def tracked(self):
        """every file the commit will hold (the index; a range's end commit)"""
        if self._tracked is None:
            if self.mode == "range":
                out = git(self.root, "ls-tree", "-r", "-z", "--name-only", self.head)
            else:
                out = git(self.root, "ls-files", "-z")
            self._tracked = [p for p in out.split("\0") if p]
        return self._tracked

    # --- tools
    def tool(self, name, cmd, hint, version_args=("--version",), want=None):
        """the command list to run tool `name` (cmd: a list or a path), after probing `cmd --version`
        once; None (and an error naming `hint`, how to install it) when it is missing or broken, or when
        want (a regex) doesn't match its version output"""
        if name not in self._tools:
            exe = list(cmd) if isinstance(cmd, (list, tuple)) else [cmd]
            ok, why = False, "not found"
            if exe and exe[0] and (os.path.isabs(exe[0]) or shutil.which(exe[0])):
                try:
                    r = subprocess.run(exe + list(version_args), capture_output=True, text=True, timeout=30)
                    out = (r.stdout + r.stderr).strip()
                    if r.returncode != 0:
                        why = "`%s %s` exits %d" % (" ".join(exe), " ".join(version_args), r.returncode)
                    elif want and not re.search(want, out):
                        why = "it is %r, not the pinned version (%s)" % (out.splitlines()[0] if out else "", want)
                    else:
                        ok = True
                except (OSError, subprocess.TimeoutExpired) as e:
                    why = str(e)
            self._tools[name] = exe if ok else None
            if not ok:
                self.error(None, "the tool %s is missing or broken (%s): %s" % (name, why, hint))
        return self._tools[name]

    def run_tool(self, args, timeout=600, **kw):
        return subprocess.run(args, cwd=self.root, capture_output=True, text=True, timeout=timeout, **kw)

    # --- the message
    def skips(self):
        """{check: reason} of the message's Commit-Tests-Skip lines"""
        return {m[1]: m[2].strip() for m in SKIP_RE.finditer(self.message or "")}


def load_check(name):
    mod = name.replace("-", "_")
    spec = importlib.util.spec_from_file_location("commit_tests_check_" + mod, CHECKS_DIR / ("%s.py" % mod))
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(HERE))
    try:
        spec.loader.exec_module(m)
    finally:
        sys.path.remove(str(HERE))
    assert m.NAME == name, "%s: NAME is %r" % (name, m.NAME)
    return m


def own_inputs(name):
    return ("tools/commit_tests/checks/%s.py" % name.replace("-", "_"), "tools/commit_tests/core.py")


def select(check, ctx):
    """(run it?, [Change] of its targets): the changed targets, or every tracked target when a shared
    input changed (or --all); a check with its own select(ctx) decides itself"""
    if hasattr(check, "select"):
        return check.select(ctx)
    targets = tuple(check.TARGETS)
    shared = tuple(getattr(check, "SHARED_INPUTS", ())) + own_inputs(check.NAME)
    accept = getattr(check, "accept", None)

    def wanted(p):
        return matches(p, targets) and (accept is None or accept(ctx, p))
    changed = [c for c in ctx.changes if any(wanted(p) for p in c.paths())]
    full = ctx.mode == "all" or any(matches(p, shared) for c in ctx.changes for p in c.paths())
    if getattr(check, "WHOLE", False):
        return bool(changed) or full, []
    if full:
        seen = {c.path for c in changed}
        changed += [Change("*", p) for p in ctx.tracked() if p not in seen and wanted(p)]
    return bool(changed), changed


def run_checks(ctx, only=None):
    """runs every selected check; returns {check: seconds}"""
    skips = ctx.skips()
    names = [n for n in ORDER if only is None or n in only]
    times = {}
    for s in skips:
        if s not in ORDER:
            ctx.check = "commit-tests"
            ctx.error(None, "Commit-Tests-Skip names %r: no such check (%s)" % (s, ", ".join(ORDER)))
    for name in names:
        ctx.check = name
        t0 = time.monotonic()
        try:
            check = load_check(name)
            go, files = select(check, ctx)
            if not go:
                continue
            if name in skips:
                if name in UNSKIPPABLE:
                    ctx.notice(None, "can't be skipped: Commit-Tests-Skip ignored")
                else:
                    ctx.notice(None, "skipped by the message: %s" % skips[name])
                    continue
            check.run(ctx, files)
        except (Exception, SystemExit):  # a check that raises (or exits) blocks
            ctx.error(None, "the check raised:\n" + traceback.format_exc().rstrip())
        times[name] = time.monotonic() - t0
    ctx.check = None
    return times


def report(ctx, out=sys.stderr, times=None):
    """prints the findings; the number of errors"""
    n = 0
    for check, kind, path, text in ctx.findings:
        n += kind == "error"
        where = "%s: " % path if path else ""
        print("commit-tests: %s: %s: %s%s" % (check, kind, where, text), file=out)
    if ctx.verbose and times:
        print("commit-tests: times: " + ", ".join("%s %.2fs" % kv for kv in times.items() if kv[1] >= 0.005), file=out)
    return n
