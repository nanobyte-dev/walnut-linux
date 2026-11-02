#!/usr/bin/env python3
"""public-scan.py REPO [--replace FILE] [--allow FILE] [--enforce]: the public export's post-scan. It
reads every object of every ref in REPO (blobs of any kind, binary ones too, commit and tag messages)
and looks for what must never be public: a match of the private replace list (tools/public-replace.local.txt,
the same patterns the commit hook reads, tools/commit_tests/checks/private.py load_patterns) and a secret
(the hook's SECRETS: key blocks, tokens, passwords in URLs, netrc logins).

A finding names the object's path (or the commit), the kind and the list line, never the text. Paths
listed in the allow file (default tools/public-scan-allow.txt: one exact repository path per line, # comments;
a pattern is refused) are skipped for SECRETS only, and only when every path the blob was ever added at
is listed: the files that hold the patterns themselves and the fixtures that plant fake secrets. A
private-list match is never allowed. File and directory names are scanned too.

Exit status: 0 clean, or findings in the default warn-only mode (printed as WARN); 1 findings with
--enforce; 2 the repository or a list can't be read. tools/public-export.sh passes --enforce when
WALNUT_EXPORT_SCAN=enforce.
Run it as python3 -I tools/public-scan.py.
"""
import argparse
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAX_BLOB = 64 << 20


def load_private():
    spec = importlib.util.spec_from_file_location("private", os.path.join(HERE, "commit_tests", "checks", "private.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_allow(path):
    """the exact blob paths whose secret-like text is allowed (never a pattern: a broad line would switch
    the secret checks off for everything it matches)"""
    if not os.path.isfile(path):
        return []
    out = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if any(c in line for c in "*?[]") or line.startswith("/") or ".." in line.split("/"):
                raise ValueError("%s line %d: only exact repository paths are allowed, not a pattern" % (path, n))
            out.append(line)
    return out


def blob_paths(repo):
    """{blob sha: every path it was added at}: rev-list --objects names a blob once, but the same bytes can
    sit at several paths and only a path-by-path look keeps an allowed path from covering another"""
    out = {}
    raw = git(repo, "log", "--all", "-m", "--raw", "--no-abbrev", "--no-renames", "--format=", "-z")
    parts = raw.split(b"\0")
    i = 0
    while i < len(parts):
        meta = parts[i]
        if meta.startswith(b":"):
            f = meta.split()
            if len(f) >= 5 and i + 1 < len(parts):
                out.setdefault(f[3].decode(), set()).add(parts[i + 1].decode("utf-8", "surrogateescape"))
            i += 2
        else:
            i += 1
    return out


def tree_names(data):
    """the entry names of a raw tree object"""
    names, i = [], 0
    while i < len(data):
        sp = data.index(b" ", i)
        nul = data.index(b"\0", sp)
        names.append(data[sp + 1:nul])
        i = nul + 21
    return names


def git(repo, *args, inp=None):
    r = subprocess.run(["git", "-C", repo, *args], input=inp, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode:
        raise OSError("git %s: %s" % (" ".join(args), r.stderr.decode("utf-8", "replace").strip()))
    return r.stdout


def objects(repo):
    """[(sha, path or None)] of every object reachable from any ref"""
    out = []
    for line in git(repo, "rev-list", "--objects", "--all").decode("utf-8", "surrogateescape").splitlines():
        sha, _, path = line.partition(" ")
        out.append((sha, path or None))
    return out


def batch(repo, shas):
    """(sha, type, data) for each sha, through one cat-file --batch"""
    p = subprocess.Popen(["git", "-C", repo, "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    import threading

    def feed():
        for s in shas:
            p.stdin.write(s.encode() + b"\n")
        p.stdin.close()
    t = threading.Thread(target=feed)
    t.start()
    for _ in shas:
        head = p.stdout.readline().split()
        if len(head) != 3:
            raise OSError("git cat-file: unexpected answer %r" % head)
        size = int(head[2])
        data = p.stdout.read(size)
        p.stdout.read(1)
        yield head[0].decode(), head[1].decode(), data
    t.join()
    p.wait()


def scan(repo, patterns, secrets, allow):
    findings = []
    seen = set()
    objs = objects(repo)
    paths = {}
    for sha, path in objs:
        paths.setdefault(sha, path)
    where_added = blob_paths(repo)
    for sha, typ, data in batch(repo, list(paths)):
        if sha in seen:
            continue
        seen.add(sha)
        if typ == "tree":
            # file and directory names: the replace list does not rewrite paths
            for name in tree_names(data):
                for label, rx in patterns:
                    if rx.search(name):
                        findings.append(("a file or directory name in tree %s" % sha[:10],
                                         "matches a private pattern (%s)" % label, "private"))
                text = name.decode("latin-1")
                for what, rx in secrets:
                    if rx.search(text):
                        findings.append(("a file or directory name in tree %s" % sha[:10], "looks like %s" % what, "secret"))
            continue
        where = paths[sha] or ("%s %s" % (typ, sha[:10]))
        if len(data) > MAX_BLOB:
            findings.append((where, "skipped: larger than 64 MB", "private"))
            continue
        for label, rx in patterns:
            if rx.search(data):
                findings.append((where, "matches a private pattern (%s)" % label, "private"))
        every = set(where_added.get(sha, ())) | ({paths[sha]} if paths[sha] else set())
        if every and all(q in allow for q in every):
            continue
        text = data.decode("latin-1")
        for what, rx in secrets:
            if rx.search(text):
                findings.append((where, "looks like %s" % what, "secret"))
    return findings


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("repo")
    ap.add_argument("--replace", default=os.environ.get("WALNUT_PUBLIC_REPLACE") or os.path.join(HERE, "public-replace.local.txt"))
    ap.add_argument("--allow", default=os.path.join(HERE, "public-scan-allow.txt"))
    ap.add_argument("--enforce", action="store_true")
    a = ap.parse_args(argv)
    try:
        private = load_private()
        if not os.path.isfile(a.replace):
            raise OSError("no private replace list: %s" % a.replace)
        patterns = private.load_patterns(a.replace, "the private replace list")
        if not patterns:
            raise OSError("the private replace list has no pattern")
        findings = scan(a.repo, patterns, private.SECRETS, read_allow(a.allow))
    except (OSError, ValueError) as e:  # ValueError: a bad allow file
        print("!! public-scan: %s" % e, file=sys.stderr)
        return 2
    for where, what, _ in sorted(set(findings)):
        print("%s %s: %s" % ("FOUND" if a.enforce else "WARN", where, what))
    print("public-scan: %d object(s) read, %d finding(s)%s" % (len(set(o for o, _ in objects(a.repo))), len(set(findings)),
                                                               "" if a.enforce or not findings else " (warn-only)"))
    return 1 if findings and a.enforce else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
