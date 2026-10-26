#!/usr/bin/env python3
"""doclinks.py [--report|--strict] [--paths] [FILE...] | --rewrite MOVEMAP [--paths] [--dry-run]: the
links between our docs, checked or rewritten after files move (the GameDB plan, Phase 0).

What it reads (FILE... instead, when given): docs/**/*.md (not docs/archive/: frozen history),
gamedb/**/*.md, README.md and every top folder's README.md, and of every package (tools/pkg-dir.sh
--all) its package.conf and the header of each patches/**/*.patch (the lines before the diff).
What it checks:
  relative markdown links  [text](path) and [text](path#anchor) in .md files (not http:, mailto:, a
                           bare #anchor, or inside ``` code blocks): the path, resolved from the
                           file's directory, exists; an anchor into a .md file names one of its headings
                           (GitHub's slugs)
  gamedb references        gamedb:<id> (gamedb/games/<id>/ or gamedb/engines/<id>/), gamedb:<id>/<inv-id>
                           (an investigation of that game or engine whose front matter id is <inv-id>),
                           gamedb:batch/<name> (a batch in gamedb/benchmarks/batches.toml); anywhere in
                           those files, but not a <!-- gamedb:NAME --> block marker (tools/gamedb/blocks.py)
  --paths                  also repository paths written out in the text (packages/..., tools/...,
                           docs/..., ...: tracked top folders, not build/): each one exists
Modes: --report (the default) prints what doesn't resolve and exits 0 (make test); --strict exits 1
when anything doesn't. --rewrite MOVEMAP takes `git diff -M --name-status A B` output (R lines;
directory moves are inferred when every file under a directory went to one new directory) and
rewrites, in every file it reads at its new place, the links that pointed at a moved file (and a
moved file's own relative links), keeping anchors; with --paths also the written-out repository
paths. --dry-run prints the changes only.
"""
import argparse
import os
import re
import subprocess
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LINK_RE = re.compile(r"(?<!!)\[(?:[^\]\[]|\[[^\]]*\])*\]\((<[^>]+>|[^)\s]+)(\s+\"[^\"]*\")?\)")
IMG_RE = re.compile(r"!\[[^\]]*\]\((<[^>]+>|[^)\s]+)(\s+\"[^\"]*\")?\)")
GAMEDB_RE = re.compile(r"(?<![\w/.-])(?<!<!-- )(?<!<!-- /)gamedb:(batch/[A-Za-z0-9._-]+|[a-z0-9][a-z0-9-]{0,63}(?:/[a-z0-9][a-z0-9.-]*)?)(?![\w/-])")
TOP = ("packages", "tools", "docs", "bench", "gamedb", "github-ci", "system", "website", "servers",
       "apt-repo", "build-containers")
PATH_RE = re.compile(r"(?<![\w/.~$-])((?:%s)/[A-Za-z0-9_./+@-]*[A-Za-z0-9_/+-])" % "|".join(TOP))
EXTERNAL = re.compile(r"[a-z][a-z0-9+.-]*:", re.I)


def tracked(repo):
    p = subprocess.run(["git", "-C", str(repo), "ls-files"], stdout=subprocess.PIPE, text=True)
    return set(p.stdout.splitlines()) if p.returncode == 0 else set()


def sources(repo):
    """the files doclinks reads, relative to repo"""
    out = set()
    for pat in ("docs/**/*.md", "gamedb/**/*.md", "README.md", "*/README.md"):
        for f in repo.glob(pat):
            rel = f.relative_to(repo).as_posix()
            if f.is_file() and not rel.startswith("docs/archive/"):
                out.add(rel)
    p = subprocess.run([str(repo / "tools" / "pkg-dir.sh"), "--all"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True, env=dict(os.environ, PKG_DIR_ROOT=str(repo)))
    for d in p.stdout.split():
        if (repo / d / "package.conf").is_file():
            out.add(d + "/package.conf")
        for f in (repo / d / "patches").rglob("*.patch") if (repo / d / "patches").is_dir() else []:
            out.add(f.relative_to(repo).as_posix())
    return sorted(out)


def patch_header(text):
    """a patch's text before its first diff (the part people write)"""
    m = re.search(r"^(diff --git |--- |Index: )", text, re.M)
    return text[:m.start()] if m else text


def slug(heading):
    """GitHub's anchor of a heading"""
    h = re.sub(r"[`*]|<[^>]+>", "", heading.strip().lower())
    h = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", h)
    h = re.sub(r"[^\w\- ]", "", h)
    return h.replace(" ", "-")


_anchor_cache = {}


def anchors(path):
    if path not in _anchor_cache:
        out, seen, fence = set(), {}, False
        try:
            lines = path.read_text(errors="replace").splitlines()
        except OSError:
            lines = []
        for ln in lines:
            if ln.lstrip().startswith("```"):
                fence = not fence
                continue
            m = None if fence else re.match(r"#{1,6}\s+(.*?)\s*#*\s*$", ln)
            if m:
                s = slug(m[1])
                n = seen.get(s, 0)
                seen[s] = n + 1
                out.add(s if n == 0 else "%s-%d" % (s, n))
            for a in re.findall(r"<a\s+(?:name|id)=\"([^\"]+)\"", ln):
                out.add(a)
        _anchor_cache[path] = out
    return _anchor_cache[path]


def unfenced(text):
    """(line number, line) outside ``` code blocks"""
    fence = False
    for i, ln in enumerate(text.splitlines(), 1):
        if ln.lstrip().startswith("```"):
            fence = not fence
            continue
        if not fence:
            yield i, ln


def body_of(repo, rel):
    text = (repo / rel).read_text(errors="replace")
    return patch_header(text) if rel.endswith(".patch") else text


class Gamedb:
    def __init__(self, repo):
        self.repo = repo
        self._inv = {}
        self._batches = None

    def owner(self, gid):
        for kind in ("games", "engines"):
            d = self.repo / "gamedb" / kind / gid
            if d.is_dir():
                return d
        return None

    def investigations(self, d):
        if d not in self._inv:
            ids = set()
            for f in (d / "investigations").glob("*.md") if (d / "investigations").is_dir() else []:
                m = re.match(r"(?:\+\+\+|---)\n(.*?)\n(?:\+\+\+|---)", f.read_text(errors="replace"), re.S)
                if m:
                    im = re.search(r"^id\s*[:=]\s*[\"']?([^\"'\s]+)", m[1], re.M)
                    if im:
                        ids.add(im[1])
            self._inv[d] = ids
        return self._inv[d]

    def batches(self):
        if self._batches is None:
            f = self.repo / "gamedb" / "benchmarks" / "batches.toml"
            self._batches = set()
            if f.exists():
                t = tomllib.loads(f.read_text())
                for b in t.get("batch", []):
                    if isinstance(b, dict) and "name" in b:
                        self._batches.add(b["name"])
        return self._batches

    def problem(self, ref):
        if ref.startswith("batch/"):
            return None if ref[6:] in self.batches() else "no batch %s in gamedb/benchmarks/batches.toml" % ref[6:]
        gid, _, inv = ref.partition("/")
        d = self.owner(gid)
        if d is None:
            return "no gamedb/games/%s/ or gamedb/engines/%s/" % (gid, gid)
        if inv and inv not in self.investigations(d):
            return "no investigation with id %s in %s/investigations/" % (inv, d.relative_to(self.repo))
        return None


def split_target(t):
    t = t[1:-1] if t.startswith("<") and t.endswith(">") else t
    path, _, anchor = t.partition("#")
    return path, anchor


def scan(repo, files, paths=False):
    """[(file, line, kind, target, why)] of everything that doesn't resolve"""
    gdb, bad = Gamedb(repo), []
    have = tracked(repo) if paths else set()
    for rel in files:
        try:
            text = body_of(repo, rel)
        except OSError as e:
            bad.append((rel, 0, "file", rel, str(e)))
            continue
        base = (repo / rel).parent
        is_md = rel.endswith(".md")
        for i, ln in unfenced(text):
            if is_md:
                for rx in (LINK_RE, IMG_RE):
                    for m in rx.finditer(ln):
                        target = m[1]
                        if EXTERNAL.match(target) or target.startswith("#") and not target[1:]:
                            continue
                        path, anchor = split_target(target)
                        if not path:		# #anchor in this file
                            if anchor and anchor not in anchors(repo / rel):
                                bad.append((rel, i, "anchor", target, "no heading #%s here" % anchor))
                            continue
                        dest = (base / path).resolve() if not path.startswith("/") else (repo / path.lstrip("/")).resolve()
                        if not dest.exists():
                            bad.append((rel, i, "link", target, "no %s" % os.path.relpath(dest, repo)))
                        elif anchor and dest.suffix == ".md" and anchor not in anchors(dest):
                            bad.append((rel, i, "anchor", target, "no heading #%s in %s" % (anchor, os.path.relpath(dest, repo))))
            for m in GAMEDB_RE.finditer(ln):
                why = gdb.problem(m[1].rstrip("."))
                if why:
                    bad.append((rel, i, "gamedb", "gamedb:" + m[1], why))
            if paths:
                for m in PATH_RE.finditer(ln):
                    p = m[1].rstrip("/")
                    if "*" in p or "<" in p or p in have or (repo / p).exists():
                        continue
                    if any(h.startswith(p + "/") for h in have):
                        continue
                    bad.append((rel, i, "path", m[1], "no such file or directory"))
    return bad


# --- rewriting -------------------------------------------------------------------------------------
def read_movemap(text, repo):
    files = {}
    for ln in text.splitlines():
        parts = ln.split("\t")
        if len(parts) == 3 and parts[0].startswith("R"):
            files[parts[1]] = parts[2]
    # a directory moved: every rename under it (with the same remaining path) went to one new
    # directory, and nothing tracked is left under the old one
    dirs = {}
    cand = {}
    for old, new in files.items():
        o, n = old.split("/"), new.split("/")
        k = 1
        while k < min(len(o), len(n)) and o[-k:] == n[-k:]:
            od, nd = "/".join(o[:-k]), "/".join(n[:-k])
            cand.setdefault(od, set()).add(nd)
            k += 1
    now = tracked(repo)
    for od, nds in cand.items():
        if len(nds) != 1:
            continue
        nd = next(iter(nds))
        under = [o for o in files if o.startswith(od + "/")]
        if not under or any(f.startswith(od + "/") for f in now):
            continue
        if all(files[o] == nd + o[len(od):] for o in under):
            dirs[od] = nd
    return files, dirs


def map_path(p, files, dirs):
    """the new repository path of old path p (None: unmoved)"""
    p = p.rstrip("/")
    if p in files:
        return files[p]
    best = None
    for od in dirs:
        if (p == od or p.startswith(od + "/")) and (best is None or len(od) > len(best)):
            best = od
    return dirs[best] + p[len(best):] if best else None


def rewrite(repo, movemap, paths=False, dry=False):
    files, dirs = read_movemap(movemap, repo)
    back = {v: k for k, v in files.items()}
    changed = 0
    for rel in sources(repo):
        old_rel = back.get(rel, rel)
        old_base = os.path.dirname(old_rel)
        new_base = os.path.dirname(rel)
        text = (repo / rel).read_text(errors="replace")
        head_end = len(patch_header(text)) if rel.endswith(".patch") else len(text)
        head, tail = text[:head_end], text[head_end:]

        def fix_link(m):
            target = m.group(1)
            if EXTERNAL.match(target) or target.startswith("#"):
                return m.group(0)
            path, anchor = split_target(target)
            if not path or path.startswith("/"):
                return m.group(0)
            old_t = os.path.normpath(os.path.join(old_base, path))
            new_t = map_path(old_t, files, dirs) or old_t
            new_rel = os.path.relpath(new_t, new_base or ".")
            if path.endswith("/") and not new_rel.endswith("/"):
                new_rel += "/"
            if os.path.normpath(os.path.join(new_base, path)) == os.path.normpath(new_t):
                return m.group(0)		# still right
            new_target = new_rel + ("#" + anchor if anchor else "")
            return m.group(0).replace("(" + target, "(" + new_target, 1)

        def fix_path(m):
            p = m.group(1)
            q = map_path(p, files, dirs)
            return q + ("/" if p.endswith("/") and not q.endswith("/") else "") if q else p

        out_lines, fence = [], False
        for ln in head.split("\n"):
            if ln.lstrip().startswith("```"):
                fence = not fence
            elif not fence:
                if rel.endswith(".md"):
                    ln = LINK_RE.sub(fix_link, ln)
                    ln = IMG_RE.sub(fix_link, ln)
                if paths:
                    ln = PATH_RE.sub(fix_path, ln)
            elif paths:
                ln = PATH_RE.sub(fix_path, ln)
            out_lines.append(ln)
        new_text = "\n".join(out_lines) + tail
        if new_text != text:
            changed += 1
            print("rewrote %s" % rel)
            if dry:
                import difflib
                sys.stdout.writelines(difflib.unified_diff(text.splitlines(True), new_text.splitlines(True), rel, rel, n=0))
            else:
                (repo / rel).write_text(new_text)
    print("doclinks: %d files %s (%d file moves, %d directory moves)" % (
        changed, "would change" if dry else "rewritten", len(files), len(dirs)))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--paths", action="store_true")
    ap.add_argument("--rewrite", metavar="MOVEMAP")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--repo", default=str(REPO), help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    repo = Path(a.repo).resolve()
    if a.rewrite:
        mm = sys.stdin.read() if a.rewrite == "-" else Path(a.rewrite).read_text()
        return rewrite(repo, mm, a.paths, a.dry_run)
    files = a.files or sources(repo)
    bad = scan(repo, files, a.paths)
    for f, i, kind, target, why in bad:
        print("%s:%d: %s %s: %s" % (f, i, kind, target, why))
    kinds = {}
    for b in bad:
        kinds[b[2]] = kinds.get(b[2], 0) + 1
    print("doclinks: %d files, %d unresolved%s" % (len(files), len(bad),
          " (%s)" % ", ".join("%s %d" % kv for kv in sorted(kinds.items())) if bad else ""))
    return 1 if a.strict and bad else 0


if __name__ == "__main__":
    sys.exit(main())
