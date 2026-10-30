#!/usr/bin/env python3
"""submission_check.py: the format check of a tester's played session sent as a pull request (docs/games/SUITE.md
"Played sessions", docs/GAMEDB.md "How a game is tested"). One check, run in three places:
- .github/workflows/validate-submission.yml on every pull request that touches submissions/ (this file from the
  base branch, the pull request's files read as data, python3 -I);
- tools/gamedb/submissions.py show and accept on our PC, before anything is imported;
- walnut-rate export and submit on the console (the walnut-rate package ships this file), on the folder it wrote.

A submission is one folder, submissions/<game>/<id>/ (id: the session's date and a short random suffix,
2026-10-30-k3f9q2), added by a pull request that touches nothing else. It holds the run directory as
tools/gamedb/import-run.py reads it, as plain files a reviewer can read on GitHub:
  bench.txt, versions, cmd, result, samples, temps, out.log, game.log, dmesg.log, input.tsv, input.err, xruns.tsv,
  xruns.log, snap-pre.txt, snap-post.txt, Xorg.0.log, shots/geometry.tsv (evidence.RAW_FILES); session.toml (the
  form), clean-check.txt, watch.log, processes-check.txt (EXTRA_FILES); hud/<name> (the frame times);
  fresh-home/<path> (the game's settings, small text files); shots/<name>.jpg (the screenshots, JPEG);
  submitter.toml (nickname, publish = true: the tester's yes, walnut_rate, exported, run, game, id).
Refused: any other change in the pull request (a second folder, a second game, a file outside submissions/, a
change to an existing file), a game that has no played test (gamedb/games/<game>/rating/test.toml), links,
submodules and executable files, unknown names, a file over FILE_MAX, shots over SHOTS_MAX or the folder over
TOTAL_MAX, a .jpg that isn't a JPEG, NUL in a text file, a submitter.toml that isn't well formed or lacks the
consent, a bench.txt that isn't a played session of the folder's game, and what the scrub should have taken out:
home paths, MAC and LAN addresses (evidence.leftovers' generic patterns) and a host or user name in the places the
logs carry one (Xorg's "Current Operating System: Linux NAME", uname's "Linux NAME 6.12", "user@host:").

Nothing in a pull request is run, checked out or unpacked: `pr` fetches the pull request's merge commit
(refs/pull/N/merge, depth 2, blobs only as they are read) into an empty git directory and reads the change list
(git diff --raw against its first parent, the base) and the blobs from git's object store.

  submission_check.py pr URL NUMBER GITDIR    fetch pull request NUMBER of the repository at URL into GITDIR, check it
  submission_check.py tree GITDIR [REF]       check a fetched commit (default refs/pr) against its first parent
  submission_check.py folder DIR [--games D]  check a folder on disk (DIR: .../submissions/<game>/<id>); --games: the
                                              directory of games/<game>/rating/test.toml (default: this repository's)
Exit 0 when it passes, 1 with the reasons when it doesn't, 2 for a usage or fetch problem. stdlib only.
"""
import os
import re
import stat
import subprocess
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:          # python3 -I leaves the script's directory out
    sys.path.insert(0, str(HERE))
import evidence  # noqa: E402

TOP = "submissions"
REPO = "nanobyte-dev/walnut-linux"      # where testers send their pull requests (walnut-rate submit)
ID_RE = re.compile(r"\A\d{4}-\d{2}-\d{2}-[a-z0-9]{6}\Z")
GAME_RE = re.compile(r"\A[a-z0-9][a-z0-9-]{0,63}\Z")
RUN_RE = re.compile(r"\A(?:ps3-bench-)?\d{8}T\d{6}Z-session-([a-z0-9][a-z0-9-]*)-([a-z0-9]+)\Z")   # ps3-bench.sh's
EXTRA_FILES = ("session.toml", "clean-check.txt", "watch.log", "processes-check.txt")
SUBMITTER = "submitter.toml"
SUBMITTER_KEYS = {"nickname": (str, 40), "publish": (bool, 0), "walnut_rate": (str, 40), "exported": (str, 40),
                  "run": (str, 120), "game": (str, 64), "id": (str, 20)}
REQUIRED = ("bench.txt", SUBMITTER)
FILE_MAX = 20 << 20          # any one file (GitHub warns from 50 MB)
SHOTS_MAX = 15 << 20         # the screenshots together (walnut-rate makes them smaller to fit)
TOTAL_MAX = 40 << 20         # the whole folder
FILES_MAX = 400
SHOTS_N = 200
SUBMITTER_MAX = 4096
HUD_RE = re.compile(r"\Ahud/[A-Za-z0-9_()+%,=-][A-Za-z0-9_.()+%,= -]{0,79}\Z")
SHOT_RE = re.compile(r"\Ashots/[A-Za-z0-9_-][A-Za-z0-9_.-]{0,79}\.jpg\Z")
FRESH_PART = re.compile(r"\A[A-Za-z0-9_.+@,=~ ()\[\]-]{1,128}\Z")
JPEG_SOI, JPEG_EOI = b"\xff\xd8\xff", b"\xff\xd9"
# never a private name: words of the logs themselves (the scrub leaves a user or host called so as it is)
GENERIC = {"root", "player", "host", "walnut", "ps3", "localhost", "debian", "user", "users", "admin", "test",
           "linux", "game", "games", "sound", "audio", "video", "input", "session", "bench"}
# where the logs name the machine or its user: each group must be one of GENERIC or a word of the run (run_words)
NAME_SPOTS = (("a host name", re.compile(r"Current Operating System: Linux (\S+)")),
              ("a host name", re.compile(r"(?m)^Linux (\S+) \d+\.\d+")),
              ("a user@host", re.compile(r"(?<![\w.@-])([a-z_][a-z0-9_-]{0,31})@([A-Za-z0-9][A-Za-z0-9-]{0,62})(?=:[~/ ]|\s|$)")))


class Refused(Exception):
    """the check's verdict; args[0] is the list of reasons"""

    def __init__(self, reasons):
        super().__init__(list(reasons))
        self.reasons = list(reasons)

    def __str__(self):
        return "; ".join(self.reasons)


def kind_of(rel):
    """(kind, bound) of a file's path in the folder: text, submitter, shot, fresh; (None, 0): not a name we know"""
    if rel in evidence.RAW_FILES or rel in EXTRA_FILES:
        return "text", FILE_MAX
    if rel == SUBMITTER:
        return "submitter", SUBMITTER_MAX
    if HUD_RE.match(rel):
        return "text", FILE_MAX
    if SHOT_RE.match(rel):
        return "shot", FILE_MAX
    parts = rel.split("/")
    if parts[0] == "fresh-home" and 2 <= len(parts) <= 16 and all(FRESH_PART.match(p) and p not in (".", "..")
                                                                   for p in parts[1:]):
        return "fresh", evidence.FRESH_MAX
    return None, 0


def kv(data):
    out = {}
    for line in data.decode("utf-8", "replace").splitlines():
        k, sep, v = line.partition("=")
        if sep:
            out[k.strip()] = v.strip()
    return out


def run_words(bench=b"", versions=b""):
    """the words a run's structure is made of (bench.txt's keys, the versions file's package names): a name like one
    of them is left by the scrub, so it is allowed here too"""
    words = set(kv(bench))
    for line in versions.decode("utf-8", "replace").splitlines():
        if line.split():
            words.add(line.split()[0].removeprefix("data:"))
    return {w.lower() for w in words if w}


def name_leaks(text, allowed):
    """[(what, the match)] of the names in NAME_SPOTS that aren't allowed"""
    out = []
    for what, r in NAME_SPOTS:
        for m in r.finditer(text):
            if any(g.lower() not in allowed for g in m.groups()):
                out.append((what, m[0][:80]))
                break
    return out


def submitter(data, game, sid):
    """submitter.toml's fields, or [reasons]"""
    try:
        s = tomllib.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as e:
        return ["submitter.toml: not TOML (%s)" % str(e)[:80]]
    bad = []
    for k, v in s.items():
        if k not in SUBMITTER_KEYS:
            bad.append("submitter.toml: unknown field %r" % k[:40])
            continue
        typ, n = SUBMITTER_KEYS[k]
        if not isinstance(v, typ) or (typ is str and (len(v) > n or any(ord(c) < 32 or ord(c) == 127 for c in v))):
            bad.append("submitter.toml: %s: %s" % (k, "at most %d characters on one line" % n if typ is str
                                                   else "true or false"))
    if s.get("publish") is not True:
        bad.append("submitter.toml: no consent (publish = true): the tester agrees that the session is published")
    if s.get("game") != game or s.get("id") != sid:
        bad.append("submitter.toml: game and id must be the folder's (%s, %s)" % (game, sid))
    m = RUN_RE.match(str(s.get("run", "")))
    if not m or m[1] != game:
        bad.append("submitter.toml: run must be the session's run directory name, of %s" % game)
    return bad or s


def check_files(game, sid, files, game_exists=None):
    """the check of one submission folder: files {path in the folder: bytes}; game_exists(game) -> bool. Returns
    {game, id, run, nickname, files, bytes}, or raises Refused with every reason found"""
    bad = []
    if not GAME_RE.match(game):
        raise Refused(["%r is not a game id" % game[:64]])
    if not ID_RE.match(sid):
        raise Refused(["%r is not a submission id (the date and six letters or digits: 2026-10-30-k3f9q2)" % sid[:40]])
    if game_exists and not game_exists(game):
        bad.append("%s has no played test in gamedb (gamedb/games/%s/rating/test.toml)" % (game, game))
    if len(files) > FILES_MAX:
        raise Refused(bad + ["%d files, at most %d" % (len(files), FILES_MAX)])
    total = shots = nshots = fresh = 0
    texts = {}
    for rel in sorted(files):
        data = files[rel]
        kind, bound = kind_of(rel)
        if not kind:
            bad.append("%s: not a file a submission has" % rel[:120])
            continue
        total += len(data)
        if len(data) > bound:
            bad.append("%s: %d bytes, at most %d" % (rel, len(data), bound))
            continue
        if kind == "shot":
            nshots += 1
            shots += len(data)
            if not (data.startswith(JPEG_SOI) and data.rstrip(b"\0").endswith(JPEG_EOI)):
                bad.append("%s: not a JPEG picture" % rel)
            continue
        if b"\0" in data:
            bad.append("%s: not a text file" % rel)
            continue
        if kind == "fresh":
            fresh += len(data)
        texts[rel] = data
    if nshots > SHOTS_N:
        bad.append("%d screenshots, at most %d" % (nshots, SHOTS_N))
    if shots > SHOTS_MAX:
        bad.append("the screenshots: %.1f MB, at most %.0f MB" % (shots / 1048576, SHOTS_MAX / 1048576))
    if total > TOTAL_MAX:
        bad.append("the folder: %.1f MB, at most %.0f MB" % (total / 1048576, TOTAL_MAX / 1048576))
    if fresh > evidence.FRESH_TOTAL:
        bad.append("fresh-home/: %d bytes, at most %d" % (fresh, evidence.FRESH_TOTAL))
    for need in REQUIRED:
        if need not in files:
            bad.append("no %s" % need)
    s = submitter(files[SUBMITTER], game, sid) if SUBMITTER in texts else {}
    if isinstance(s, list):
        bad += s
        s = {}
    if "bench.txt" in texts:
        b = kv(texts["bench.txt"])
        if b.get("kind") != "session" or b.get("game") != game:
            bad.append("bench.txt: not a played session of %s (kind=session, game=%s)" % (game, game))
    allowed = GENERIC | run_words(texts.get("bench.txt", b""), texts.get("versions", b""))
    for rel, data in sorted(texts.items()):
        text = data.decode("utf-8", "replace")
        for what, m in evidence.leftovers(text, ((), ())) + name_leaks(text, allowed):
            bad.append("%s: %s left (%r): the scrub missed it" % (rel, what, m))
    for rel in files:          # the names themselves (fresh-home/'s paths come from the console)
        for what, m in evidence.leftovers(rel, ((), ())):
            bad.append("%s: %s in the name" % (rel[:120], what))
    if bad:
        raise Refused(bad)
    return {"game": game, "id": sid, "run": s.get("run"), "nickname": s.get("nickname", ""), "files": len(files),
            "bytes": total}


# ---- a folder on disk (walnut-rate export, our tests) ------------------------------------------------

def folder_files(d):
    """{path: bytes} of the folder d, or raises Refused: anything but plain files and directories (links,
    devices, fifos) and executable files are refused, a link is never followed"""
    d = Path(d)
    out, bad = {}, []
    for root, dirs, names in os.walk(d, followlinks=False):
        for n in sorted(dirs + names):
            p = Path(root) / n
            rel = str(p.relative_to(d))
            st = os.lstat(p)
            if stat.S_ISLNK(st.st_mode):
                bad.append("%s: a link" % rel)
                if n in dirs:
                    dirs.remove(n)
            elif stat.S_ISDIR(st.st_mode):
                continue
            elif not stat.S_ISREG(st.st_mode):
                bad.append("%s: not a plain file" % rel)
            elif st.st_mode & 0o111:
                bad.append("%s: executable" % rel)
            elif st.st_size > FILE_MAX:
                bad.append("%s: %d bytes, at most %d" % (rel, st.st_size, FILE_MAX))
            else:
                out[rel] = p.read_bytes()
    if bad:
        raise Refused(bad)
    return out


def games_dir_exists(games):
    return lambda g: (Path(games) / g / "rating" / "test.toml").is_file()


def check_folder(d, game_exists=None):
    """the check of a folder DIR = .../submissions/<game>/<id>"""
    d = Path(d)
    return check_files(d.parent.name, d.name, folder_files(d), game_exists)


# ---- a pull request (the workflow, submissions.py) ----------------------------------------------------

def git(gitdir, *args, data=False, check=True, timeout=300):
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1", GIT_ASKPASS="true", LC_ALL="C")
    p = subprocess.run(["git", "-C", str(gitdir), "-c", "core.quotepath=off", *args], capture_output=True,
                       timeout=timeout, env=env)
    if check and p.returncode:
        raise RuntimeError("git %s: %s" % (" ".join(args[:3]), p.stderr.decode("utf-8", "replace").strip()[-300:]))
    return p.stdout if data else p.stdout.decode("utf-8", "replace")


def fetch_pr(url, number, gitdir, ref="refs/pr"):
    """pull request number's merge commit (refs/pull/N/merge: the pull request on top of its base) from url into an
    empty git directory gitdir as ref; depth 2, the blobs fetched only when read, nothing checked out"""
    if not str(number).isdigit():
        raise ValueError("%r: not a pull request number" % number)
    gitdir = Path(gitdir)
    gitdir.mkdir(parents=True, exist_ok=True)
    git(gitdir, "init", "-q")
    for k, v in (("remote.origin.url", url), ("remote.origin.promisor", "true"),
                 ("remote.origin.partialclonefilter", "blob:none"), ("core.hooksPath", "/dev/null"),
                 ("protocol.allow", "never"), ("protocol.https.allow", "always"), ("protocol.file.allow", "always")):
        git(gitdir, "config", k, v)
    git(gitdir, "fetch", "-q", "--depth=2", "--no-tags", "--filter=blob:none", "origin",
        "+refs/pull/%s/merge:%s" % (number, ref), timeout=600)
    return gitdir


def changes(gitdir, ref="refs/pr"):
    """[(old mode, new mode, blob, status, path)] of ref against its first parent"""
    out = []
    raw = git(gitdir, "diff", "--raw", "-z", "--no-renames", "--no-abbrev", "--no-ext-diff", "--no-textconv",
              ref + "^1", ref, data=True)
    parts = raw.split(b"\0")
    i = 0
    while i + 1 < len(parts) and parts[i]:
        meta = parts[i].decode().lstrip(":").split()
        out.append((meta[0], meta[1], meta[3], meta[4], parts[i + 1].decode("utf-8", "surrogateescape")))
        i += 2
    return out


def check_changes(chs):
    """(game, id, {path in the folder: blob}) of a pull request's changes, or raises Refused: it must only add
    plain files under one submissions/<game>/<id>/"""
    bad, folders, blobs = [], set(), {}
    for old, new, blob, st, path in chs:
        parts = path.split("/")
        if len(parts) < 4 or parts[0] != TOP or "" in parts:
            bad.append("%s: outside submissions/<game>/<id>/ (a submission changes nothing else)" % path[:120])
            continue
        folders.add((parts[1], parts[2]))
        if st != "A":
            bad.append("%s: %s (a submission only adds files)" % (path[:120], {"M": "changed", "D": "deleted"}.get(st, st)))
        elif new != "100644":
            bad.append("%s: %s" % (path[:120], {"120000": "a link", "160000": "a submodule",
                                                "100755": "executable"}.get(new, "mode " + new)))
        else:
            blobs["/".join(parts[3:])] = blob
    if not chs:
        bad.append("the pull request changes nothing")
    if len({f[0] for f in folders}) > 1:
        bad.append("more than one game: %s (one session per pull request)" % ", ".join(sorted({f[0] for f in folders})))
    elif len(folders) > 1:
        bad.append("more than one folder: %s (one session per pull request)" % ", ".join("/".join(f) for f in sorted(folders)))
    if bad:
        raise Refused(bad)
    (game, sid), = folders
    return game, sid, blobs


def blobs_read(gitdir, blobs):
    """{path: bytes} of {path: blob id}; sizes looked at first, nothing over FILE_MAX read"""
    if not blobs:
        return {}
    ids = sorted(set(blobs.values()))
    git(gitdir, "fetch", "-q", "--no-tags", "--filter=blob:none", "origin", *ids, check=False, timeout=600)
    sizes = {}
    p = subprocess.run(["git", "-C", str(gitdir), "cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)"],
                       input="".join(i + "\n" for i in ids), capture_output=True, text=True, timeout=600)
    for line in p.stdout.splitlines():
        f = line.split()
        if len(f) == 3 and f[1] == "blob":
            sizes[f[0]] = int(f[2])
    out, bad = {}, []
    for rel, b in sorted(blobs.items()):
        if b not in sizes:
            bad.append("%s: its blob can't be read" % rel)
        elif sizes[b] > FILE_MAX:
            bad.append("%s: %d bytes, at most %d" % (rel, sizes[b], FILE_MAX))
        else:
            out[rel] = git(gitdir, "cat-file", "blob", b, data=True)
    if bad:
        raise Refused(bad)
    return out


def check_tree(gitdir, ref="refs/pr"):
    """(info, {path: bytes}) of the pull request fetched as ref, or raises Refused"""
    game, sid, blobs = check_changes(changes(gitdir, ref))
    files = blobs_read(gitdir, blobs)

    def exists(g):
        return subprocess.run(["git", "-C", str(gitdir), "cat-file", "-e", "%s^1:gamedb/games/%s/rating/test.toml" % (ref, g)],
                              capture_output=True, timeout=120).returncode == 0
    return check_files(game, sid, files, exists), files


# ---- the command line -----------------------------------------------------------------------------------

def report(info=None, err=None):
    if err is not None:
        gh = os.environ.get("GITHUB_ACTIONS") == "true"
        print("!! the submission doesn't pass the check (docs/games/SUITE.md \"Played sessions\"):")
        for r in err.reasons:
            print(("::error::%s" if gh else "   %s") % r)
        return 1
    print("ok: %s/%s/%s, %d files, %.1f MB, run %s, credited to %s" % (
        TOP, info["game"], info["id"], info["files"], info["bytes"] / 1048576, info["run"], info["nickname"] or "a tester"))
    return 0


def main(argv):
    try:
        if len(argv) == 4 and argv[0] == "pr":
            try:
                fetch_pr(argv[1], argv[2], argv[3])
            except (RuntimeError, ValueError, subprocess.TimeoutExpired) as e:
                print("!! %s" % e)
                return 2
            return report(check_tree(argv[3])[0])
        if len(argv) in (2, 3) and argv[0] == "tree":
            return report(check_tree(argv[1], *argv[2:])[0])
        if argv[:1] == ["folder"] and len(argv) in (2, 4) and (len(argv) == 2 or argv[2] == "--games"):
            games = argv[3] if len(argv) == 4 else HERE.parent.parent / "gamedb" / "games"
            return report(check_folder(argv[1], games_dir_exists(games)))
    except Refused as e:
        return report(err=e)
    print(__doc__.split("\n\n")[-1].split("Exit 0")[0].rstrip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
