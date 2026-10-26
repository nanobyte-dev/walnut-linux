#!/usr/bin/env python3
"""licensed.py: the one answer to "is this package, or this file, licensed content we may not
redistribute" (docs/PACKAGING.md "Licensed packages"). Every guard asks it: tools/leak-check.py (and
through it apt-repo/licensed-publish.sh and tools/build-image.sh), github-ci/build-package.sh,
github-ci/gen-workflows.py, github-ci/build-release.sh, tools/public-export.sh, tools/image-packages.sh,
tools/gamedb/check.py.

  licensed.py names               the licensed package names, one per line
  licensed.py globs               the leak globs: paths of licensed content in source tarballs
  licensed.py dirs                the licensed package directories this checkout has
  licensed.py is-licensed NAME [--section S]
                                  exit 0 when NAME (with Section S) is licensed, 1 when it is free
  licensed.py file PATH...        prints the glob each PATH matches (exit 1 when any does)
  licensed.py check [--since REV] the declarations agree (below); with --since, also that no name
                                  licensed at REV has left the set (it only grows)

Names, the union of:
  - every directory under packages/licensed/ and gamedb/<games|engines>/<id>/licensed/ (a licensed
    package or data builder lives there and nowhere else)
  - [[licensed]] name = "..." in gamedb/<games|engines>/<id>/build.toml: every game's licensed data,
    also the names with no directory of ours (game-data-packager's quake-shareware) and, in the public
    copy (no licensed directory), all of them
  - GENERIC: the names that are no game's, licensed in every checkout (the metapackage
    ps3-games-licensed, whose directory packages/licensed/ the public copy doesn't have)
A binary package is also licensed when its Section is non-free/* or local/*.
Leak globs, the union of: tools/licensed-files.txt (the generic ones: Sony's SDK modules); [[licensed]]
leak_files in build.toml; every dfsg-exclude.txt of a package of ours (an entry E as E and E/*). Shell
patterns (fnmatch: * crosses /), matched against a tarball path with and without its top directory.

check: every gamedb licensed/<pkg>/ directory is declared in its own bundle's build.toml [[licensed]]
(packages/licensed/<pkg>/ is licensed by where it is); no package directory outside a licensed
location carries a licensed name; every debian/control of ours gives its binaries a Section
local/* or non-free/* exactly when the source is licensed (and a free source builds no licensed name);
all or none: a checkout has every licensed directory git tracks or (the public copy) none tracked and
none present.
Fails closed: a file that can't be read or parsed, or a bad name, is exit 2, never an empty answer.
Python: import it by path (python3 -I leaves the script's directory off sys.path):
  spec_from_file_location("licensed", <repo>/tools/licensed.py); names(repo), globs(repo),
  is_licensed(name, section, names), glob_hit(path, globs).
"""
import argparse
import fnmatch
import os
import re
import subprocess
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FILES = "tools/licensed-files.txt"
# licensed names that are no game's (gamedb holds the games'): in every checkout, the public copy too
GENERIC = ("ps3-games-licensed",)
# where licensed package directories live (relative glob patterns, one directory level each)
LICENSED_DIR_GLOBS = ("packages/licensed/*", "gamedb/games/*/licensed/*", "gamedb/engines/*/licensed/*")
BUILD_TOML_GLOBS = ("gamedb/games/*/build.toml", "gamedb/engines/*/build.toml")
NAME_RE = re.compile(r"[a-z0-9][a-z0-9+.-]+\Z")
LICENSED_SECTION = re.compile(r"(non-free|local)/")


class Error(Exception):
    """a declaration that can't be read or parsed: callers exit 2"""


# --- reading, from the working tree or (check --since) from a commit --------------------------------
class Tree:
    def __init__(self, repo, rev=None):
        self.repo, self.rev = Path(repo), rev
        if rev:
            p = subprocess.run(["git", "-C", str(repo), "ls-tree", "-r", "--name-only", rev],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if p.returncode:
                raise Error("git ls-tree %s: %s" % (rev, p.stderr.strip()))
            self.files = p.stdout.splitlines()

    def glob_dirs(self, pattern):
        """directories matching pattern (fnmatch per path component)"""
        if not self.rev:
            return sorted(p.relative_to(self.repo).as_posix() for p in self.repo.glob(pattern) if p.is_dir())
        n = pattern.count("/") + 1
        out = set()
        for f in self.files:
            parts = f.split("/")
            if len(parts) > n and all(fnmatch.fnmatchcase(a, b) for a, b in zip(parts[:n], pattern.split("/"))):
                out.add("/".join(parts[:n]))
        return sorted(out)

    def glob_files(self, pattern):
        if not self.rev:
            return sorted(p.relative_to(self.repo).as_posix() for p in self.repo.glob(pattern) if p.is_file())
        pat = pattern.split("/")
        return sorted(f for f in self.files if len(f.split("/")) == len(pat)
                      and all(fnmatch.fnmatchcase(a, b) for a, b in zip(f.split("/"), pat)))

    def exists(self, path):
        return (self.repo / path).is_file() if not self.rev else path in self.files

    def read(self, path):
        if not self.rev:
            try:
                return (self.repo / path).read_text()
            except (OSError, UnicodeDecodeError) as e:
                raise Error("%s: %s" % (path, e))
        p = subprocess.run(["git", "-C", str(self.repo), "show", "%s:%s" % (self.rev, path)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if p.returncode:
            raise Error("%s:%s: %s" % (self.rev, path, p.stderr.strip()))
        return p.stdout


def read_list(tree, path, what):
    """the entries of a list file: one per line, # starts a comment"""
    out = []
    for i, ln in enumerate(tree.read(path).splitlines(), 1):
        ln = ln.split("#", 1)[0].strip()
        if not ln:
            continue
        if what == "name" and not NAME_RE.match(ln):
            raise Error("%s:%d: %r is not a package name" % (path, i, ln))
        if what == "glob" and (" " in ln or ln.startswith("/")):
            raise Error("%s:%d: %r is not a relative path pattern" % (path, i, ln))
        out.append(ln)
    return out


def build_tomls(tree):
    """[(path, parsed build.toml)] of every bundle and engine"""
    out = []
    for pat in BUILD_TOML_GLOBS:
        for f in tree.glob_files(pat):
            try:
                out.append((f, tomllib.loads(tree.read(f))))
            except tomllib.TOMLDecodeError as e:
                raise Error("%s: %s" % (f, e))
    return out


def declared(tree):
    """{name: the build.toml that declares it} from build.toml [[licensed]], and GENERIC"""
    out = {}
    for f, t in build_tomls(tree):
        lic = t.get("licensed", [])
        if not isinstance(lic, list):
            raise Error("%s: licensed must be an array of tables ([[licensed]])" % f)
        for i, e in enumerate(lic):
            n = e.get("name") if isinstance(e, dict) else None
            if not isinstance(n, str) or not NAME_RE.match(n):
                raise Error("%s: [[licensed]] %d: name %r is not a package name" % (f, i + 1, n))
            out.setdefault(n, f)
    for n in GENERIC:
        out.setdefault(n, "tools/licensed.py GENERIC")
    return out


def licensed_dirs(tree):
    return [d for pat in LICENSED_DIR_GLOBS for d in tree.glob_dirs(pat)]


def names_of(tree):
    names = set(declared(tree))
    for d in licensed_dirs(tree):
        n = d.rsplit("/", 1)[1]
        if not NAME_RE.match(n):
            raise Error("%s: the directory name is not a package name" % d)
        names.add(n)
    return names


def globs_of(tree):
    out = list(read_list(tree, FILES, "glob"))
    for f, t in build_tomls(tree):
        for e in t.get("licensed", []):
            lf = e.get("leak_files", [])
            if not isinstance(lf, list) or not all(isinstance(x, str) and x and not x.startswith("/") for x in lf):
                raise Error("%s: [[licensed]] %s: leak_files must be a list of relative path patterns" % (f, e.get("name")))
            out += lf
    for pat in ("packages/*/*/dfsg-exclude.txt", "packages/*/*/*/dfsg-exclude.txt",
                "gamedb/games/*/packages/*/dfsg-exclude.txt", "gamedb/engines/*/packages/*/dfsg-exclude.txt"):
        for f in tree.glob_files(pat):
            for e in read_list(tree, f, "glob"):
                out += [e] if e.endswith("*") else [e, e.rstrip("/") + "/*"]
    seen, uniq = set(), []
    for g in out:
        if g not in seen:
            seen.add(g)
            uniq.append(g)
    return uniq


# --- the API ---------------------------------------------------------------------------------------
def names(repo=REPO):
    return names_of(Tree(repo))


def globs(repo=REPO):
    return globs_of(Tree(repo))


def is_licensed(name, section=None, names_set=None):
    """a binary or source package NAME (Section section) is licensed"""
    if names_set is None:
        names_set = names()
    return bool(LICENSED_SECTION.match(section or "")) or name in names_set


def glob_hit(path, globs_list):
    """the first leak glob matching path (a tarball member: also tried without its top directory)"""
    rel = path.split("/", 1)[1] if "/" in path else path
    for g in globs_list:
        if fnmatch.fnmatch(rel, g) or fnmatch.fnmatch(path, g):
            return g
    return None


def control_stanzas(text):
    out = []
    for block in re.split(r"\n\s*\n", text):
        f, key = {}, None
        for ln in block.splitlines():
            if ln.startswith("#"):
                continue
            if ln[:1] in (" ", "\t") and key:
                continue
            m = re.match(r"([A-Za-z0-9-]+):\s*(.*)$", ln)
            if m:
                key = m[1]
                f[key] = m[2].strip()
        if f:
            out.append(f)
    return out


def check(repo=REPO, since=None):
    """a list of problems (empty: consistent)"""
    tree, probs = Tree(repo), []
    decl = declared(tree)
    names_now = names_of(tree)
    lic_dirs = licensed_dirs(tree)
    for d in lic_dirs:
        n = d.rsplit("/", 1)[1]
        if d.startswith("gamedb/"):
            own = "/".join(d.split("/")[:3]) + "/build.toml"
            if decl.get(n) != own and not any(n == e.get("name") for f, t in build_tomls(tree) if f == own
                                               for e in t.get("licensed", []) if isinstance(e, dict)):
                probs.append("%s: a licensed directory its bundle doesn't declare (%s [[licensed]] name = %r)" % (d, own, n))
    # every package directory of ours
    p = subprocess.run([str(Path(repo) / "tools" / "pkg-dir.sh"), "--all"], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True)
    if p.returncode:
        raise Error("tools/pkg-dir.sh --all: %s" % p.stderr.strip())
    for d in p.stdout.split():
        n = d.rsplit("/", 1)[1]
        in_lic = any(fnmatch.fnmatchcase(d, g) for g in LICENSED_DIR_GLOBS)
        if n in names_now and not in_lic:
            probs.append("%s: licensed package %s outside packages/licensed/ and gamedb/*/*/licensed/ "
                         "(the public copy would keep it)" % (d, n))
        ctl = Path(repo) / d / "debian" / "control"
        if not ctl.is_file():
            continue
        st = control_stanzas(ctl.read_text())
        if not st or "Source" not in st[0]:
            raise Error("%s/debian/control: no Source stanza" % d)
        src_lic = st[0]["Source"] in names_now or in_lic
        src_section = st[0].get("Section", "")
        for b in st[1:]:
            if "Package" not in b:
                continue
            sec = b.get("Section", src_section)
            sec_lic = bool(LICENSED_SECTION.match(sec))
            if src_lic and not sec_lic:
                probs.append("%s: %s is licensed but its Section %r isn't local/* or non-free/*" % (d, b["Package"], sec))
            if not src_lic and sec_lic:
                probs.append("%s: %s has Section %r but the source %s isn't declared licensed" % (d, b["Package"], sec, st[0]["Source"]))
            if not src_lic and b["Package"] in names_now:
                probs.append("%s: the free source %s builds the licensed name %s" % (d, st[0]["Source"], b["Package"]))
    # all or none (the public copy has none of them)
    g = subprocess.run(["git", "-C", str(repo), "ls-files", "--"] + [x + "/package.conf" for x in LICENSED_DIR_GLOBS],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if g.returncode == 0:
        tracked = sorted({os.path.dirname(f) for f in g.stdout.split()})
        missing = [t for t in tracked if not (Path(repo) / t / "package.conf").is_file()]
        if tracked and missing:
            probs.append("a partial checkout: these licensed packages are tracked but missing: %s" % " ".join(missing))
        if not tracked:
            present = [d for d in lic_dirs if (Path(repo) / d / "package.conf").is_file()]
            if present:
                probs.append("a public checkout (no licensed package tracked) with licensed directories present: %s"
                             % " ".join(present))
    if since:
        old = names_of(Tree(repo, since))
        gone = sorted(old - names_now)
        if gone:
            probs.append("licensed at %s but not now (the set only grows): %s" % (since, " ".join(gone)))
    return probs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", default=str(REPO), help=argparse.SUPPRESS)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("names")
    sub.add_parser("globs")
    sub.add_parser("dirs")
    s = sub.add_parser("is-licensed")
    s.add_argument("name")
    s.add_argument("--section")
    s = sub.add_parser("file")
    s.add_argument("paths", nargs="+")
    s = sub.add_parser("check")
    s.add_argument("--since")
    a = ap.parse_args(argv)
    repo = Path(a.repo)
    try:
        if a.cmd == "names":
            print("\n".join(sorted(names(repo))))
        elif a.cmd == "globs":
            print("\n".join(globs(repo)))
        elif a.cmd == "dirs":
            print("\n".join(licensed_dirs(Tree(repo))))
        elif a.cmd == "is-licensed":
            return 0 if is_licensed(a.name, a.section, names(repo)) else 1
        elif a.cmd == "file":
            gl, hit = globs(repo), 0
            for p in a.paths:
                g = glob_hit(p, gl)
                if g:
                    print("%s %s" % (p, g))
                    hit = 1
            return hit
        elif a.cmd == "check":
            probs = check(repo, a.since)
            for p in probs:
                print("!! " + p, file=sys.stderr)
            n = names(repo)
            print("licensed.py: %d names, %d leak globs, %d licensed directories%s: %s" % (
                len(n), len(globs(repo)), len(licensed_dirs(Tree(repo))), " (since %s)" % a.since if a.since else "",
                "%d problems" % len(probs) if probs else "consistent"))
            return 1 if probs else 0
    except Error as e:
        print("licensed.py: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
