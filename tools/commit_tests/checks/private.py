"""private: nothing private gets into a commit (it can't be skipped). It reads the added lines
(`git diff --cached -U0`; --all: every tracked file) and the commit message, and refuses
  - a match of the private patterns: tools/public-replace.local.txt (untracked: the text the public export
    replaces, $WALNUT_PUBLIC_REPLACE as tools/public-export.sh reads it) plus, when it exists, the
    untracked tools/commit-private.local.txt; both in git filter-repo's --replace-text syntax (one per
    line: literal text, literal:TEXT, regex:RE or glob:G, each optionally followed by ==>REPLACEMENT).
    The replace list is required, with at least one pattern: without it nothing can be checked, so every
    commit is refused. Binary files are scanned as bytes (up to 64 MB each).
    A finding names the list and its line, never the text it matched;
  - secrets: private key blocks, GitHub, GitLab, AWS, Slack, Google and Anthropic/OpenAI API tokens;
  - new paths under backups/ (the NOR dump), firmware/ or .local/ (at any depth), and files the public
    export refuses by name (.bin, .img, .pup, .nor, .nand: flash dumps and disk images);
  - images under gamedb/games/**/rating/ committed as files instead of Git LFS pointers;
  - paths of licensed game data (tools/licensed.py file: the leak globs)."""
import fnmatch
import os
import re
import subprocess
import sys
from pathlib import PurePath

NAME = "private"
TARGETS = ("**",)
SHARED_INPUTS = ()
REPLACE = "tools/public-replace.local.txt"
LOCAL = "tools/commit-private.local.txt"
PRIVATE_DIRS = ("backups/", "firmware/", ".local/")
DUMP_RE = re.compile(r"\.(bin|img|pup|nor|nand)$", re.I)
SCISSORS = "# ------------------------ >8 ------------------------"
LFS_IMAGES = ("gamedb/games/**/rating/**/*.jpg", "gamedb/games/**/rating/**/*.jpeg", "gamedb/games/**/rating/**/*.png",
              "gamedb/games/**/rating/sessions/**/shots/**", "gamedb/games/**/rating/sessions/**/raw/**")
DASH5 = "-" * 5
SECRETS = (
    ("a private key block", re.compile(DASH5 + "BEGIN ([A-Z0-9]+ )*PRIVATE KEY( BLOCK)?" + DASH5)),
    ("a GitHub token", re.compile(r"\b(gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{50,})\b")),
    ("a GitLab token", re.compile(r"\bglpat-[A-Za-z0-9_-]{20,}\b")),
    ("an AWS access key", re.compile(r"\b(AKIA|ASIA)[0-9A-Z]{16}\b")),
    ("a Slack token", re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}")),
    ("a Google API key", re.compile(r"\bAIza[0-9A-Za-z_-]{35}\b")),
    ("an Anthropic or OpenAI API key", re.compile(r"\bsk-(ant-|proj-|svcacct-|admin-)[A-Za-z0-9_-]{20,}|\bsk-[A-Za-z0-9]{40,}")),
    ("a password in a URL", re.compile(r"\b[a-z][a-z0-9+.-]*://[A-Za-z0-9._~%-]+:[^\s/@$]{3,}@")),
    ("a netrc or apt auth.conf login", re.compile(r"\bmachine\s+\S+\s+login\s+[^\s%<$]\S*\s+password\s+[^\s%<$\"'{]\S*")),
)


def glob_to_regex(g):
    """git filter-repo's glob: fnmatch without the anchors"""
    rx = fnmatch.translate(g)
    for end in (r")\Z", r")\z"):
        if rx.startswith("(?s:") and rx.endswith(end):
            return rx[4:-len(end)]
    return rx


def select(ctx):
    """always: the message is scanned even when nothing is staged (--allow-empty, a merge)"""
    return True, [c for c in ctx.changes]


def load_patterns(path, label):
    """[(label:line, compiled regex)] of a replace-text file, filter-repo's way (rsplit on ==>)"""
    out = []
    with open(path, "rb") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip(b"\r\n").rstrip(b"\r")
            if b"==>" in line:
                line = line.rsplit(b"==>", 1)[0]
            if line.startswith(b"regex:"):
                rx = re.compile(line[6:])
            elif line.startswith(b"glob:"):
                rx = re.compile(glob_to_regex(line[5:].decode("utf-8", "surrogateescape")).encode("utf-8", "surrogateescape"))
            else:
                if line.startswith(b"literal:"):
                    line = line[8:]
                if not line:
                    continue
                rx = re.compile(re.escape(line))
            out.append(("%s line %d" % (label, n), rx))
    return out


def patterns(ctx):
    repl = os.environ.get("WALNUT_PUBLIC_REPLACE") or str(ctx.path(REPLACE))
    if not os.path.isfile(repl):
        ctx.error(None, "no %s (the private replace list, untracked): nothing can be checked for private text, so "
                  "nothing is committed. Copy it from your private notes (tools/public-export.sh reads it too)" % REPLACE)
        return None
    pats = load_patterns(repl, REPLACE)
    if not pats:
        ctx.error(None, "%s has no pattern: nothing can be checked for private text, so nothing is committed" % REPLACE)
        return None
    if ctx.exists(LOCAL):
        pats += load_patterns(ctx.path(LOCAL), LOCAL)
    return pats


def scan(ctx, pats, where, lines):
    """lines: [(line number, bytes)]"""
    for n, line in lines:
        for label, rx in pats:
            if rx.search(line):
                ctx.error(where, "line %s: matches a private pattern (%s)" % (n, label))
        s = line.decode("utf-8", "replace")
        for what, rx in SECRETS:
            if rx.search(s):
                ctx.error(where, "line %s: looks like %s" % (n, what))


def added_lines(ctx):
    """{path: [(line number, bytes)]} of the lines the changes add"""
    if ctx.mode == "all":
        out = {}
        for p in ctx.tracked():
            b = ctx.read_bytes(p)
            if b is None or b"\0" in b[:8192] or len(b) > 8 << 20:
                continue
            out[p] = list(enumerate(b.split(b"\n"), 1))
        return out
    if ctx.mode == "hook":
        args = ["diff", "--cached", ctx.base or "4b825dc642cb6eb9a060e54bf8d69288fbee4904"]
    else:
        args = ["diff", ctx.base, ctx.head]
    r = subprocess.run(["git", "-C", str(ctx.root), "-c", "core.quotepath=off", *args, "-U0", "--no-color",
                        "--no-ext-diff", "--no-textconv", "-M", "--src-prefix=a/", "--dst-prefix=b/"],
                       capture_output=True, check=True)
    out, cur, n, prev = {}, None, 0, b""
    for line in r.stdout.split(b"\n"):
        if line.startswith(b"+++ ") and prev.startswith(b"--- "):   # a file header (an added "++ x" line is no header)
            cur = unquote(line[4:])
            cur = cur[2:] if cur and cur.startswith("b/") else None
        elif line.startswith(b"diff --git "):
            cur = None
        elif line.startswith(b"@@"):
            m = re.match(rb"@@ -\S+ \+(\d+)", line)
            n = int(m[1]) if m else 0
        elif cur and line.startswith(b"+"):
            out.setdefault(cur, []).append((n, line[1:]))
            n += 1
        prev = line
    # binary files: git shows no lines, so their bytes (as they will be committed) are scanned whole
    for c in ctx.changes:
        if c.status in ("A", "M", "R") and c.path not in out:
            b = ctx.staged_bytes(c.path)
            if b and b"\0" in b[:8192] and len(b) <= 64 << 20:
                out[c.path] = [("(binary)", b)]
    return out


def unquote(b):
    """a diff header's path: git's C-style quoting ("b/a\"b") undone"""
    if not b.startswith(b'"'):
        return b.decode("utf-8", "surrogateescape")
    body, out, i = b[1:-1] if b.endswith(b'"') else b[1:], bytearray(), 0
    esc = {b"n": b"\n", b"t": b"\t", b'"': b'"', b"\\": b"\\", b"a": b"\a", b"b": b"\b", b"f": b"\f", b"r": b"\r", b"v": b"\v"}
    while i < len(body):
        ch = body[i:i + 1]
        if ch == b"\\" and i + 1 < len(body):
            nx = body[i + 1:i + 2]
            if nx in esc:
                out += esc[nx]; i += 2  # noqa: E702
                continue
            if body[i + 1:i + 4].isdigit():
                out.append(int(body[i + 1:i + 4], 8)); i += 4  # noqa: E702
                continue
        out += ch
        i += 1
    return bytes(out).decode("utf-8", "surrogateescape")


def licensed_hits(ctx, paths):
    if not paths:
        return []
    r = subprocess.run([sys.executable, "-I", str(ctx.path("tools/licensed.py")), "file", *paths], cwd=ctx.root,
                       capture_output=True, text=True)
    hits = [ln.rsplit(" ", 1) for ln in r.stdout.splitlines() if " " in ln]
    # exit 1 is "a path matched"; a crash exits 1 too (a traceback, no hit printed): that is a failure
    if r.returncode not in (0, 1) or (r.returncode == 1 and not hits) or "Traceback" in r.stderr:
        ctx.error(None, "tools/licensed.py file failed (exit %d): %s" % (r.returncode, r.stderr.strip()[-300:]))
        return []
    return hits


def run(ctx, files):
    pats = patterns(ctx)
    if pats is not None:
        for path, lines in sorted(added_lines(ctx).items()):
            scan(ctx, pats, path, lines)
        if ctx.message:
            msg = ctx.message.split("\n" + SCISSORS, 1)[0]   # git commit -v: the diff below the line is not the message
            scan(ctx, pats, "the commit message", list(enumerate(msg.encode("utf-8", "surrogateescape").split(b"\n"), 1)))
    new = [c.path for c in files if c.status in ("A", "R", "*", "M")]
    for p in new:
        if any(("/" + p).find("/" + d) >= 0 for d in PRIVATE_DIRS):
            ctx.error(p, "a private path (%s never go into the repository)" % ", ".join(PRIVATE_DIRS))
        elif DUMP_RE.search(p):
            ctx.error(p, "a flash dump or disk image by its name (the public export refuses .bin, .img, .pup, .nor, .nand)")
        if any(PurePath(p).full_match(g) for g in LFS_IMAGES):
            b = ctx.staged_bytes(p)
            if b is not None and not b.startswith(b"version https://git-lfs.github.com/spec/v1"):
                ctx.error(p, "a rating image or session file stored as a plain file, not a Git LFS pointer "
                          "(.gitattributes: is git-lfs installed? git add it again)")
    for p, g in licensed_hits(ctx, [c.path for c in files if c.status in ("A", "R", "*")]):
        ctx.error(p, "licensed game data (tools/licensed.py leak glob %s): never in the repository" % g)
