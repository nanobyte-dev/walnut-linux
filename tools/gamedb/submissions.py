#!/usr/bin/env python3
"""submissions.py list|show|accept|reject: testers' played sessions, sent as GitHub pull requests to
nanobyte-dev/walnut-linux (`walnut-rate submit`, or the exported folder uploaded by hand), into gamedb, each one
reviewed and accepted or rejected by the maintainer first (docs/GAMEDB.md "How a game is tested", docs/games/SUITE.md
"Played sessions"). Decided by the user, 2026-10-30: pull requests rather than a receiver of ours, an optional
nickname for the credit, an accepted session counting like ours (catalog.py SUBMITTED_COUNTS).

  list                    the open pull requests that add a submission: number, author, game and id, files, and
                          what their check (.github/workflows/validate-submission.yml) said
  show PR                 the pull request's merge commit fetched into build/tmp/submissions/pr-<N>/git (git, depth 2,
                          nothing checked out), checked by tools/gamedb/submission_check.py (the workflow's check),
                          and written out as the run directory build/tmp/submissions/pr-<N>/<run>/ for the review;
                          the submitter's answers and the next steps
  accept PR [--yes] [--session NAME] [--launcher NAME] [--content DIR]
                          fetched and checked again (refused when it changed since show), then import-run.py on the
                          run once it has its REVIEW.md (tools/gamedb/review.py, by the importing agent as for ours):
                          result.toml gets source = "submitted" and the tester's nickname (none: the site says "a
                          tester"), raw/import.toml the pull request; the session folder (only it) committed with the
                          pull request's number in the message; then, asked first, a comment on the pull request and
                          it is closed. Never merged: the raw files of a bundle don't go into main
  reject PR REASON [--yes] the reason as a comment and the pull request closed, asked first

Nothing in a pull request is run or checked out; the run directory is written from the checked blobs. gh (logged
in) for GitHub's API; git for the fetch. WALNUT_SUBMISSIONS_REPO (default submission_check.REPO), WALNUT_SUBMISSIONS_GIT
(the URL fetched, default https://github.com/<repo>) and WALNUT_SUBMISSIONS_DIR (default build/tmp/submissions)
point it elsewhere (the tests).
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import submission_check as sc  # noqa: E402

HEAD = "head"            # in build/tmp/submissions/pr-<N>/: the pull request's head commit the run was written from


def import_run_module():
    spec = importlib.util.spec_from_file_location("import_run", HERE / "import-run.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def repo():
    r = os.environ.get("WALNUT_SUBMISSIONS_REPO") or sc.REPO
    if not re.fullmatch(r"[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}", r):
        raise SystemExit("!! WALNUT_SUBMISSIONS_REPO=%r: OWNER/NAME" % r)
    return r


def git_url():
    return os.environ.get("WALNUT_SUBMISSIONS_GIT") or "https://github.com/%s" % repo()


def work(n):
    return Path(os.environ.get("WALNUT_SUBMISSIONS_DIR") or REPO / "build" / "tmp" / "submissions") / ("pr-%d" % n)


# ---- GitHub through gh -----------------------------------------------------------------------------

def gh(args, body=None):
    if not shutil.which("gh"):
        raise SystemExit("!! gh (GitHub's command line) isn't installed")
    p = subprocess.run(["gh"] + args, input=json.dumps(body) if body is not None else None, capture_output=True,
                       text=True, timeout=300)
    if p.returncode:
        raise SystemExit("!! gh %s: %s" % (" ".join(args[:4]), (p.stderr or p.stdout).strip()[-400:]))
    return json.loads(p.stdout) if p.stdout.strip() else None


def api(method, path, body=None):
    return gh(["api", "-X", method, "-H", "Accept: application/vnd.github+json", path]
              + (["--input", "-"] if body is not None else []), body)


def pull(n):
    return api("GET", "repos/%s/pulls/%d" % (repo(), n))


def check_state(sha):
    """what validate-submission said about commit sha: success, failure, pending, or - (no run)"""
    try:
        runs = api("GET", "repos/%s/commits/%s/check-runs?per_page=100" % (repo(), sha)) or {}
    except SystemExit:
        return "?"
    for r in runs.get("check_runs", []):
        if r.get("name") in ("validate", "validate-submission"):
            return r.get("conclusion") or r.get("status") or "pending"
    return "-"


def ask(prompt, yes):
    if yes:
        return True
    if not sys.stdin.isatty():
        print("(not a terminal: --yes to do it without asking)")
        return False
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False


# ---- fetch and check -------------------------------------------------------------------------------

def fetch_checked(n):
    """(pull request, info, {path: bytes}): the pull request open, its merge commit fetched anew and checked; the
    merge commit's second parent must be the pull request's head as GitHub has it now"""
    pr = pull(n)
    if pr.get("state") != "open":
        raise SystemExit("!! #%d is %s" % (n, pr.get("state")))
    w = work(n)
    gd = w / "git"
    shutil.rmtree(gd, ignore_errors=True)
    w.mkdir(parents=True, exist_ok=True)
    try:
        sc.fetch_pr(git_url(), n, gd)
        head = sc.git(gd, "rev-parse", "refs/pr^2").strip()
    except (RuntimeError, ValueError) as e:
        raise SystemExit("!! #%d: %s (GitHub has no merge commit for a pull request that conflicts)" % (n, e))
    if head != pr["head"]["sha"]:
        raise SystemExit("!! #%d: the merge commit isn't of its head %s yet (GitHub is still making it): try again"
                         % (n, pr["head"]["sha"][:12]))
    try:
        info, files = sc.check_tree(gd)
    except sc.Refused as e:
        print("!! #%d doesn't pass the submission check:" % n)
        for r in e.reasons:
            print("   " + r)
        raise SystemExit(1)
    return pr, info, files


def write_run(n, info, files, head):
    """the run directory build/tmp/submissions/pr-<N>/<run>/ from the checked files (once per head: a review made
    on it stays; a new head writes it again without the old review)"""
    w = work(n)
    run = w / info["run"]
    mark = w / HEAD
    if run.is_dir() and mark.is_file() and mark.read_text().strip() == head:
        return run, False
    for d in w.iterdir():
        if d.is_dir() and sc.RUN_RE.match(d.name):
            shutil.rmtree(d)
    for rel, data in files.items():
        p = run / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    mark.write_text(head + "\n")
    return run, True


def rel(p):
    return p.relative_to(REPO) if p.is_relative_to(REPO) else p


def submitter(files):
    return tomllib.loads(files[sc.SUBMITTER].decode())


def next_steps(n, run, info):
    print("next, as for our own sessions (docs/games/SUITE.md \"Played sessions\"): the PC reference if the game has "
          "none (make bench-ref GAME=%s), then python3 tools/gamedb/review.py %s and its questions answered by "
          "looking (REVIEW.md in the run); then submissions.py accept %d, or reject %d \"why\""
          % (info["game"], rel(run), n, n))


# ---- the commands ----------------------------------------------------------------------------------

def cmd_list(a):
    prs = api("GET", "repos/%s/pulls?state=open&per_page=100" % repo()) or []
    rows = []
    for pr in prs:
        files = api("GET", "repos/%s/pulls/%d/files?per_page=100" % (repo(), pr["number"])) or []
        subs = sorted({"/".join(f["filename"].split("/")[1:3]) for f in files
                       if f["filename"].startswith(sc.TOP + "/")})
        if subs:
            rows.append((pr["number"], pr["user"]["login"], ", ".join(subs), len(files), check_state(pr["head"]["sha"]),
                         pr["title"]))
    if not rows:
        print("no open pull requests with a submission (%s)" % repo())
        return 0
    print("%-6s  %-16s  %-34s  %5s  %-8s  %s" % ("PR", "AUTHOR", "SUBMISSION", "FILES", "CHECK", "TITLE"))
    for r in rows:
        print("#%-5d  %-16s  %-34s  %5d  %-8s  %s" % (r[0], r[1][:16], r[2][:34], r[3], r[4], r[5][:60]))
    return 0


def cmd_show(a):
    pr, info, files = fetch_checked(a.pr)
    run, fresh = write_run(a.pr, info, files, pr["head"]["sha"])
    s = submitter(files)
    print("#%d by %s: %s/%s/%s, %d files, %.1f MB, the check passes" % (a.pr, pr["user"]["login"], sc.TOP, info["game"],
                                                                     info["id"], info["files"], info["bytes"] / 1048576))
    print("  run %s, nickname %r, publish %s, walnut-rate %s, exported %s" % (
        info["run"], s.get("nickname", ""), "yes" if s.get("publish") else "no", s.get("walnut_rate", "?"),
        s.get("exported", "?")))
    for r in sorted(files):
        print("  %9d  %s" % (len(files[r]), r))
    print("the run: %s%s" % (rel(run), "" if fresh else " (as reviewed: the pull request hasn't changed)"))
    next_steps(a.pr, run, info)
    return 0


def session_folder(content, game, ref):
    """the session folder import-run.py wrote for the submission ref (raw/import.toml submission)"""
    for imp in sorted((Path(content) / game / "rating" / "sessions").glob("*/raw/import.toml")):
        try:
            if tomllib.loads(imp.read_text()).get("submission") == ref:
                return imp.parent.parent
        except (OSError, tomllib.TOMLDecodeError):
            continue
    return None


def commit(folder, message):
    """the session folder (only it) committed in the git repository that holds it; the commit's short id"""
    top = Path(subprocess.run(["git", "-C", str(folder), "rev-parse", "--show-toplevel"], capture_output=True,
                              text=True, check=True).stdout.strip())
    path = str(folder.resolve().relative_to(top.resolve()))
    subprocess.run(["git", "-C", str(top), "add", "--", path], check=True)
    subprocess.run(["git", "-C", str(top), "commit", "-q", "-m", message, "--", path], check=True)
    return subprocess.run(["git", "-C", str(top), "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                          check=True).stdout.strip()


def comment_and_close(n, body):
    api("POST", "repos/%s/issues/%d/comments" % (repo(), n), {"body": body})
    api("PATCH", "repos/%s/pulls/%d" % (repo(), n), {"state": "closed"})


def cmd_accept(a):
    pr, info, files = fetch_checked(a.pr)
    w = work(a.pr)
    mark = w / HEAD
    run = w / info["run"]
    if not run.is_dir() or not mark.is_file() or mark.read_text().strip() != pr["head"]["sha"]:
        run, _ = write_run(a.pr, info, files, pr["head"]["sha"])
        print("#%d: written out as %s (new, or changed since show), no review yet" % (a.pr, rel(run)))
        next_steps(a.pr, run, info)
        return 1
    if not (run / "REVIEW.md").is_file():
        print("#%d: %s has no REVIEW.md yet" % (a.pr, rel(run)))
        next_steps(a.pr, run, info)
        return 1
    s = submitter(files)
    nick = s.get("nickname", "")
    ref = "%s#%d %s/%s/%s" % (repo(), a.pr, sc.TOP, info["game"], info["id"])
    content = Path(a.content) if a.content else REPO / "gamedb" / "games"
    who = nick or "a tester"
    folder = session_folder(content, info["game"], ref)
    if folder:              # accepted before, the comment and close left for later
        print("#%d is imported already: %s" % (a.pr, rel(folder)))
    else:
        ir = import_run_module()
        try:
            ir.import_run(run, info["game"], content, launcher=a.launcher, session=a.session, tester=nick,
                          source="submitted", submission=ref)
        except ir.Refused as e:
            raise SystemExit("!! %s" % e)
        folder = session_folder(content, info["game"], ref)
        if not folder:
            raise SystemExit("!! the imported session folder isn't to be found under %s" % content)
        short = commit(folder, "GameDB: %s session %s from a tester's pull request (%s#%d), credited to %s"
                       % (info["game"], folder.name, repo(), a.pr, who))
        print("accepted #%d: %s, credited to %s, committed as %s" % (a.pr, rel(folder), who, short))
    body = ("Thank you! This session is in Walnut's GameDB now: %s, session %s, credited to %s. It shows on "
            "https://walnut.nanobyte.dev/gamedb/ after the site's next update.\n\n"
            "We close the pull request rather than merge it: the session's record goes into the repository, the "
            "submitted files don't." % (info["game"], folder.name, who))
    if not ask("Comment on #%d and close it (public)? [y/N] " % a.pr, a.yes):
        print("not commented or closed yet: submissions.py accept %d again asks once more (the import is kept)" % a.pr)
        return 0
    comment_and_close(a.pr, body)
    print("commented on #%d and closed it" % a.pr)
    return 0


def cmd_reject(a):
    pr = pull(a.pr)
    if pr.get("state") != "open":
        raise SystemExit("!! #%d is %s" % (a.pr, pr.get("state")))
    reason = " ".join(a.reason.split())
    if not reason:
        raise SystemExit("!! the reason, please: it goes into the comment")
    body = ("Thank you for sending it. We can't use this session: %s\n\n"
            "docs/games/SUITE.md (\"Played sessions\") says what a session needs; a new one is welcome." % reason)
    print("#%d by %s, %r: the comment would be:\n%s" % (a.pr, pr["user"]["login"], pr["title"], body))
    if not ask("Comment on #%d and close it (public)? [y/N] " % a.pr, a.yes):
        print("nothing done")
        return 1
    comment_and_close(a.pr, body)
    print("rejected #%d: commented and closed" % a.pr)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    p = sub.add_parser("show")
    p.add_argument("pr", type=int)
    p = sub.add_parser("accept")
    p.add_argument("pr", type=int)
    p.add_argument("--yes", action="store_true", help="comment and close without asking")
    p.add_argument("--session", help="the session folder's name (import-run.py --session)")
    p.add_argument("--launcher", help="import-run.py --launcher, for a run that recorded none")
    p.add_argument("--content", help="the games' directory (default gamedb/games)")
    p = sub.add_parser("reject")
    p.add_argument("pr", type=int)
    p.add_argument("reason")
    p.add_argument("--yes", action="store_true", help="comment and close without asking")
    a = ap.parse_args(argv)
    return {"list": cmd_list, "show": cmd_show, "accept": cmd_accept, "reject": cmd_reject}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
