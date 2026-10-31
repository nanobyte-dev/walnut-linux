#!/usr/bin/env python3
"""run.py: the commit tests (make commit-tests; the git hooks tools/hooks/pre-commit and commit-msg).

  run.py --hook pre-commit            refuse a commit whose staged files also have unstaged changes
  run.py --hook commit-msg MSGFILE    strip AI attribution lines from the message, then run every check
                                      the staged changes select; exit 1 on an error
  run.py --staged                     the checks over the staged changes, as commit-msg runs them (no message)
  run.py --range A..B                 the checks over what changed from A to B (the messages of the
                                      commits in the range count as the message)
  run.py --all                        every check over every tracked file (the backlog)
  options: --only CHECK[,CHECK...], -v (each check's time), --list (the checks and their globs)

Why two hooks: git runs pre-commit before the message exists and commit-msg after (with the index the
commit will have), so the checks run in commit-msg, where a `Commit-Tests-Skip: <check>: <reason>`
line can be read; pre-commit keeps the one refusal that needs no message. Both hooks are skipped by
--no-verify, which agents never use (CLAUDE.md). What the checks are: core.py and checks/*.py.
"""
import argparse
import re
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import core  # noqa: E402

REPO = HERE.parent.parent
# attribution lines git must never keep (CLAUDE.md): an AI co-author, a session link, a generated-by line
TRAILER_RE = re.compile(
    r"^[ \t]*(Co-Authored-By:.*\b(claude|anthropic|openai|chatgpt|gpt-?\d|gemini|copilot|codex|opus|sonnet|haiku|fable)\b.*"
    r"|Claude-Session:.*|.*Generated with \[Claude Code\].*)$", re.I)


def strip_trailers(text):
    """(text without the attribution lines, how many went)"""
    lines = text.split("\n")
    kept = [ln for ln in lines if not TRAILER_RE.match(ln)]
    n = len(lines) - len(kept)
    if not n:
        return text, 0
    out = "\n".join(kept).rstrip("\n") + "\n"
    return out, n


def staged_changes(root):
    head = core.git(root, "rev-parse", "--verify", "-q", "HEAD", check=False).strip() or None
    out = core.git(root, "diff", "--cached", "--name-status", "-M", "-z", "--no-ext-diff", head or core.EMPTY_TREE)
    return head, core.parse_name_status(out)


def staged_and_unstaged(root):
    staged = set(core.git(root, "diff", "--cached", "--name-only", "-z", "--no-ext-diff").split("\0")) - {""}
    unstaged = set(core.git(root, "diff", "--name-only", "-z", "--no-ext-diff").split("\0")) - {""}
    return sorted(staged & unstaged)


def pre_commit(root):
    both = staged_and_unstaged(root)
    if not both:
        return 0
    print("commit-tests: refused: these staged files also have unstaged changes (another agent's work in\n"
          "the shared tree, or a partial add); the checks read the working tree, and the commit would not hold\n"
          "what was checked. Stage all of a file or none of it (git add <file>, or commit by explicit pathspec):", file=sys.stderr)
    for p in both:
        print("   " + p, file=sys.stderr)
    return 1


def finish(ctx, times, t0, what):
    n = core.report(ctx, times=times)
    dt = time.monotonic() - t0
    if n:
        names = sorted({c for c, k, _, _ in ctx.findings if k == "error" and c})
        print("commit-tests: %d error(s) in %s (%.1f s): %s. A deliberate exception is a line\n"
              "   Commit-Tests-Skip: <check>: <reason>\n"
              "in the commit message (private can't be skipped)." % (n, ", ".join(names), dt, what), file=sys.stderr)
        return 1
    if ctx.verbose or ctx.message is None:
        print("commit-tests: %d check(s) clean in %.1f s" % (len(times), dt), file=sys.stderr)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--hook", choices=("pre-commit", "commit-msg"))
    ap.add_argument("msgfile", nargs="?")
    ap.add_argument("--staged", action="store_true")
    ap.add_argument("--range", metavar="A..B")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--repo", default=None, help="the repository (default: the current directory's top)")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args(argv)
    t0 = time.monotonic()
    root = Path(a.repo or core.git(".", "rev-parse", "--show-toplevel").strip())
    only = set(a.only.split(",")) if a.only else None
    if only and only - set(core.ORDER):
        ap.error("unknown check(s): %s" % ", ".join(sorted(only - set(core.ORDER))))
    if a.list:
        for n in core.ORDER:
            m = core.load_check(n)
            print("%-15s targets %s; shared inputs %s%s" % (n, " ".join(m.TARGETS), " ".join(getattr(m, "SHARED_INPUTS", ())) or "-",
                                                         " (repository-wide)" if getattr(m, "WHOLE", False) else ""))
        return 0
    if a.hook == "pre-commit":
        return pre_commit(root)
    if a.hook == "commit-msg" or a.staged:
        msg, refused = None, 0
        if a.hook:
            if not a.msgfile:
                ap.error("commit-msg needs the message file")
            msg = Path(a.msgfile).read_text(encoding="utf-8", errors="replace")
            msg, n = strip_trailers(msg)
            if n:
                Path(a.msgfile).write_text(msg, encoding="utf-8")
                print("commit-tests: commit-msg: removed %d AI attribution line(s) from the message (CLAUDE.md: no trailers)" % n,
                      file=sys.stderr)
        else:
            refused = pre_commit(root)
        head, changes = staged_changes(root)
        ctx = core.Ctx(root, "hook", changes, base=head, message=msg, verbose=a.verbose)
        times = core.run_checks(ctx, only)
        return finish(ctx, times, t0, "commit refused") or refused
    if a.range:
        if ".." not in a.range:
            ap.error("--range A..B")
        base, head = a.range.split("..", 1)
        head = head or "HEAD"
        changes = core.parse_name_status(core.git(root, "diff", "--name-status", "-M", "-z", "--no-ext-diff", base, head))
        msgs = core.git(root, "log", "--format=%B%x00", "%s..%s" % (base, head))
        ctx = core.Ctx(root, "range", changes, base=base, head=head, message=msgs, verbose=a.verbose)
        times = core.run_checks(ctx, only)
        return finish(ctx, times, t0, "in the range")
    if a.all:
        ctx = core.Ctx(root, "all", [], verbose=a.verbose)
        ctx.changes = [core.Change("*", p) for p in ctx.tracked()]
        times = core.run_checks(ctx, only)
        return finish(ctx, times, t0, "in the tree")
    ap.error("one of --hook, --staged, --range, --all, --list")


if __name__ == "__main__":
    sys.exit(main())
