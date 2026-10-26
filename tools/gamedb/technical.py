#!/usr/bin/env python3
"""technical.py: technical.json, the data of the Walnut Games site's technical pages (gamedb/README.md
"Generated from here"). tools/gamedb/catalog.py writes it beside catalog.json; Hugo's content adapters
(website/gamedb/content/{technical,engines,research}/_content.gotmpl) make the pages from it. Site only:
the desktop app's catalog (pack-catalog.py) never holds it.

Design
- Pages, every one of them public: per published game (drafts never: no page, no link, no name in a list)
  games/<id>/technical/ with its technical.md, its build facts (build.toml; our source packages with their
  patch lists, as catalog.py lists them, linked to the public repository; licensed data by name only), its
  played test (rating/test.toml) and medal, its benchmark (the recipe's header and variants, the result
  tables of benchrecords.results(), a table per batch: the summary first, then the run batches newest
  first, a row per variant, a column per metric of metrics.toml with its unit) and its investigations
  (games/<id>/investigations/<inv-id>/, front matter and body); per engine engines/<e>/ (engine.md,
  technical.md, its packages, the published games on it, its investigations at
  engines/<e>/investigations/<inv-id>/); per survey research/<slug>/ (gamedb/research/<slug>.md).
- A game without a technical.md still gets its page: the data sections alone, and a line saying there
  are no notes yet. Nothing is made up to fill it.
- Markdown is passed on for Hugo to render; its first "# " heading becomes the page title. Relative
  links are rewritten for the site, relative to the page: a survey, a published game's page, technical
  page or investigation, an engine go to their pages; any other file of the repository to the public
  repository on GitHub; a link into a draft's bundle, a licensed directory, a session's raw/ or a path
  that does not exist keeps its text and loses the link.
- Public-safe: every string of the file is scanned before it is written (tools/gamedb/evidence.py
  leftovers: /home/, private IPv4 and MAC addresses, this checkout's private user and host names;
  and the patterns of tools/public-replace.local.txt, which the public copy of the repository hides);
  a hit stops the build (TechnicalError).
- stdlib only.
"""
import json
import posixpath
import re
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "bench"))
import check          # noqa: E402
import benchrecords   # noqa: E402

SCHEMA = 1
# a file or directory of the repository on the public copy's main branch
GITHUB = "https://github.com/nanobyte-dev/walnut-linux/%s/main/%s"
LINK_RE = re.compile(r"(!?)\[((?:[^\[\]\n]|\[[^\[\]\n]*\])*)\]\(<?([^()\s<>]+(?:\([^()\s]*\)[^()\s<>]*)?)>?((?:\s+\"[^\"\n]*\")?)\)")
VARIANT_RE = re.compile(r"^variant_([A-Za-z0-9_]+)\s*\(\)")


class TechnicalError(Exception):
    pass


def split_md(text):
    """(front matter dict, body) of a markdown file with optional +++ TOML front matter"""
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+\n?", text, re.S)
    return (tomllib.loads(m[1]), text[m.end():]) if m else ({}, text)


def title_body(body):
    """(the first "# " heading, the body without it); (None, body) when it doesn't start with one"""
    m = re.match(r"\s*# +(.+?)\s*#*\s*(?:\n|\Z)", body)
    return (m[1], body[m.end():].lstrip("\n")) if m else (None, body)


class Site:
    """what the link rewriting needs to know: the published games, the investigations' ids by file"""

    def __init__(self, root, repo):
        self.root, self.repo = Path(root), Path(repo)
        games = self.root / "games"
        self.published = {d.name for d in games.iterdir() if d.is_dir() and not check.is_draft(d)} if games.is_dir() else set()
        self.engines = {d.name for d in (self.root / "engines").iterdir() if d.is_dir()} if (self.root / "engines").is_dir() else set()
        self.inv_ids = {}

    def inv_id(self, path):
        if path not in self.inv_ids:
            fm, _ = split_md(path.read_text())
            self.inv_ids[path] = fm.get("id") or re.sub(r"^\d{4}-\d{2}-\d{2}-", "", path.stem)
        return self.inv_ids[path]

    def target(self, rel):
        """the site path ("research/x/") or a GitHub URL for the repository path rel, or None (no link)"""
        try:
            gd = str(self.root.resolve().relative_to(self.repo.resolve()))
        except ValueError:
            gd = "gamedb"
        if (re.search(r"(^|/)(licensed|raw)(/|$)", rel) and rel.startswith(gd + "/")) or rel.startswith("packages/licensed"):
            return None
        sub = rel[len(gd) + 1:].split("/") if rel.startswith(gd + "/") else None
        if sub and sub[0] == "research" and len(sub) == 2 and sub[1].endswith(".md"):
            return "research/%s/" % sub[1][:-3] if (self.root / rel[len(gd) + 1:]).is_file() else None
        if sub and sub[0] in ("games", "engines") and len(sub) >= 2 and sub[1]:
            kind, name, rest = sub[0], sub[1], [x for x in sub[2:] if x]
            if kind == "games" and name not in self.published:
                return None                          # a draft: not on the site, not linked
            if kind == "engines" and name not in self.engines:
                return None
            base = "%s/%s/" % (kind, name)
            if kind == "games" and rest in ([], ["index.md"]):
                return base
            if rest in ([], ["engine.md"], ["technical.md"]) and kind == "engines":
                return base
            if kind == "games" and rest == ["technical.md"]:
                return base + "technical/"
            if len(rest) == 2 and rest[0] == "investigations" and rest[1].endswith(".md"):
                f = self.root / kind / name / "investigations" / rest[1]
                return "%sinvestigations/%s/" % (base, self.inv_id(f)) if f.is_file() else None
        p = self.repo / rel
        if not p.exists() or rel.startswith(".."):
            return None
        return GITHUB % ("tree" if p.is_dir() else "blob", rel)

    def links(self, md, src, page):
        """md with its relative links rewritten for the page at site path page (src: the file's repository path)"""
        out, fence = [], None
        for line in md.split("\n"):
            m = re.match(r"\s*(```+|~~~+)", line)
            if fence:
                fence = None if m and m[1][0] == fence[0] and len(m[1]) >= len(fence) else fence
                out.append(line)
                continue
            if m:
                fence = m[1]
                out.append(line)
                continue
            out.append(LINK_RE.sub(lambda x: self._link(x, src, page), line))
        return "\n".join(out)

    def _link(self, m, src, page):
        bang, text, dest, title = m[1], m[2], m[3], m[4]
        if re.match(r"[a-zA-Z][a-zA-Z0-9+.-]*:", dest) or dest.startswith("#"):
            return m[0]
        path, _, anchor = dest.partition("#")
        rel = posixpath.normpath(posixpath.join(posixpath.dirname(src), path)) if path else src
        t = self.target(rel)
        if t is None:
            return "" if bang else text
        if not t.startswith("http"):
            t = posixpath.relpath(t.rstrip("/"), page.rstrip("/")) + "/"
        return "%s[%s](%s%s%s)" % (bang, text, t, "#" + anchor if anchor else "", title)


def markdown_page(site, path, page, fallback_title):
    """(title, front matter, markdown body) of a markdown file for the page at site path page"""
    fm, body = split_md(path.read_text())
    title, body = title_body(body)
    rel = str(path.resolve().relative_to(site.repo.resolve()))
    return title or fallback_title, fm, site.links(body, rel, page).strip() + "\n"


def investigations(site, d, owner_url, owner):
    out = []
    idir = Path(d) / "investigations"
    for f in sorted(idir.glob("*.md")) if idir.is_dir() else []:
        iid = site.inv_id(f)
        url = "%sinvestigations/%s/" % (owner_url, iid)
        title, fm, body = markdown_page(site, f, url, iid)
        date = re.match(r"(\d{4}-\d{2}-\d{2})-", f.name)
        facts = {k: v for k, v in fm.items() if k != "id"}
        facts["games"] = [g for g in facts.get("games", []) if g in site.published]
        out.append({"id": iid, "owner": owner, "url": url, "title": title, "date": date[1] if date else None,
                    "facts": facts, "body": body})
    return sorted(out, key=lambda i: (i["date"] or "", i["id"]), reverse=True)


def packages(names, repo, patch_subjects, package_dir):
    """our source packages by name: their directory, its GitHub link and their patches"""
    out = []
    for n in names:
        d = package_dir(repo, n)
        out.append({"name": n, "dir": d, "url": GITHUB % ("tree", d) if d else None,
                    "patches": [{"file": f, "subject": s, "url": GITHUB % ("blob", "%s/patches/%s" % (d, f))}
                                for f, s in patch_subjects(n, repo)]})
    return out


def fmt(v):
    if isinstance(v, float):
        return ("%.1f" % v) if abs(v) < 1e6 else "%g" % v
    return "" if v is None else str(v)


def bench_tables(gid, results, batches, metrics):
    """the game's result tables: [{batch facts, columns, rows}], the summary batches first, then the run
    batches newest first"""
    info = {b["name"]: b for b in batches}
    out = []
    for name, games in results.items():
        t = games.get(gid)
        if not t:
            continue
        b = info.get(name, {})
        variants = t.get("variant", {})
        cols = [m for m in metrics if any(m["name"] in e for e in variants.values())]
        rows = [{"variant": v, "label": e.get("label"), "note": e.get("note"), "order": e.get("order", 0),
                 "values": [fmt(e.get(m["name"])) for m in cols]} for v, e in variants.items()]
        if b.get("kind") == "summary":
            rows.sort(key=lambda r: (r["order"], r["variant"]))
        for r in rows:
            del r["order"]
        out.append({"batch": name, "kind": b.get("kind", "run"), "order": b.get("order", 0), "title": b.get("title"),
                    "date": str(b["date"]) if b.get("date") else None, "runs": b.get("runs"),
                    "stack": [{"name": k, "version": str(v)} for k, v in b.get("stack", {}).items()],
                    "observations": b.get("observations"), "purpose": b.get("purpose"), "column": t.get("column"),
                    "columns": [{"name": m["name"], "unit": m.get("unit"), "better": m.get("better"),
                                 "what": m.get("what")} for m in cols],
                    "rows": rows})
    out.sort(key=lambda x: (x["kind"] != "summary", -x["order"], x["batch"]))
    for x in out:
        del x["order"]
    return out


def comment_text(lines):
    return " ".join(l.lstrip("#").strip() for l in lines).strip()


def recipe(gdir):
    """(the recipe's header comment, [{name, note}] of its variants, base first) or (None, [])"""
    f = Path(gdir) / "benchmark" / "recipe.sh"
    if not f.is_file():
        return None, []
    lines = f.read_text(errors="replace").split("\n")
    head, end = [], 0
    for end, line in enumerate(lines):
        if not line.startswith("#"):
            break
        if not line.startswith("#!"):
            head.append(line)
    variants = [{"name": "base", "note": "the recipe as it is, without a variant"}]
    for i, line in enumerate(lines):
        m = VARIANT_RE.match(line)
        if not m:
            continue
        above = []
        j = i - 1
        while j >= end and lines[j].startswith("#"):
            above.insert(0, lines[j])
            j -= 1
        if m[1] != "base":
            variants.append({"name": m[1], "note": comment_text(above) or None})
    return comment_text(head) or None, variants


def game_page(site, gdir, g, results, batches, metrics, patch_subjects, package_dir):
    gid = gdir.name
    url = "games/%s/technical/" % gid
    b = check.read_build(gdir)
    notes = None
    if (gdir / "technical.md").is_file():
        _, _, notes = markdown_page(site, gdir / "technical.md", url, g["title"])
    engine = b.get("engine")
    eng = None
    if isinstance(engine, str) and (site.root / "engines" / engine / "engine.md").is_file():
        efm, _ = split_md((site.root / "engines" / engine / "engine.md").read_text())
        eng = {"id": engine, "title": efm.get("title", engine), "url": "engines/%s/" % engine}
    test = None
    tf = gdir / "rating" / "test.toml"
    if tf.is_file():
        test = tomllib.loads(tf.read_text())
    header, variants = recipe(gdir)
    recipes = b.get("recipes", [])
    for v in variants:
        v["catalog"] = "%s:%s" % (gid, v["name"]) in recipes
    invs = investigations(site, gdir, "games/%s/" % gid, gid)
    sessions = [t for t in g["tests"] if t["kind"] == "session"]
    r = g["rating"]
    return {
        "id": gid, "title": g["title"], "url": url, "page": "games/%s/" % gid, "notes": notes,
        "medal": {"medal": r["medal"], "score": r["score"]}, "graded_by": r.get("graded_by"),
        "engine": eng,
        "build": {"packages": b.get("packages", []), "meta": b.get("meta", []), "suite": b.get("suite"),
                  "needs_data": b.get("needs_data", False), "data": b.get("data", []), "desktop": b.get("desktop", []),
                  "divert": b.get("divert", []), "image_base": b.get("image_base", False),
                  "licensed": [x["name"] for x in b.get("licensed", []) if isinstance(x, dict) and "name" in x]},
        "our_packages": packages(check.patched_by(gdir, b), site.repo, patch_subjects, package_dir),
        "patch_count": len(g.get("patches", [])),
        "test": test, "sessions": len(sessions), "tests": len(g["tests"]),
        "benchmark": {"header": header, "variants": variants, "recipes": recipes,
                      "tables": bench_tables(gid, results, batches, metrics)},
        "investigations": [{k: i[k] for k in ("id", "title", "url", "date")} for i in invs],
    }, invs


def engine_page(site, edir, patch_subjects, package_dir):
    e = edir.name
    url = "engines/%s/" % e
    title, fm, body = markdown_page(site, edir / "engine.md", url, e)
    notes = None
    if (edir / "technical.md").is_file():
        _, _, notes = markdown_page(site, edir / "technical.md", url, title)
    invs = investigations(site, edir, url, e)
    games = [g for g in fm.get("games", []) if g in site.published]
    b = check.read_build(edir)
    return {"id": e, "title": fm.get("title", title), "url": url, "homepage": fm.get("homepage"),
            "license": fm.get("license"), "games": games, "body": body, "notes": notes,
            "packages": b.get("packages", []),
            "licensed": [x["name"] for x in b.get("licensed", []) if isinstance(x, dict) and "name" in x],
            "our_packages": packages(check.own_packages(edir), site.repo, patch_subjects, package_dir),
            "investigations": [{k: i[k] for k in ("id", "title", "url", "date")} for i in invs]}, invs


def research_pages(site):
    out = []
    rdir = site.root / "research"
    for f in sorted(rdir.glob("*.md")) if rdir.is_dir() else []:
        url = "research/%s/" % f.stem
        title, _, body = markdown_page(site, f, url, f.stem)
        out.append({"slug": f.stem, "title": title, "url": url, "body": body})
    return out


def build(content, cat, repo=REPO, patch_subjects=None, package_dir=None):
    """technical.json's dict from the gamedb games folder content and catalog.py's catalog cat"""
    import catalog
    import score
    patch_subjects = patch_subjects or catalog.patch_subjects
    package_dir = package_dir or score.package_dir
    content = Path(content)
    root = content.parent
    site = Site(root, repo)
    results = benchrecords.results(root)
    batches = benchrecords.load_batches(root)
    metrics = benchrecords.load_metrics(root)
    games, invs = [], []
    for g in cat["games"]:
        gdir = content / g["id"]
        page, gi = game_page(site, gdir, g, results, batches, metrics, patch_subjects, package_dir)
        games.append(page)
        invs += gi
    engines = []
    for edir in sorted((root / "engines").iterdir()) if (root / "engines").is_dir() else []:
        if (edir / "engine.md").is_file():
            page, ei = engine_page(site, edir, patch_subjects, package_dir)
            engines.append(page)
            invs += ei
    titles = {g["id"]: g["title"] for g in games}
    for e in engines:
        e["games"] = [{"id": x, "title": titles.get(x, x), "url": "games/%s/technical/" % x} for x in e["games"]]
    etitles = {e["id"]: e["title"] for e in engines}
    for i in invs:
        i["owner_title"] = (etitles if i["url"].startswith("engines/") else titles).get(i["owner"], i["owner"])
        i["facts"]["games"] = [{"id": x, "title": titles.get(x, x), "url": "games/%s/technical/" % x}
                               for x in i["facts"].get("games", [])]
    return {"schema": SCHEMA, "games": games, "engines": engines, "investigations": invs,
            "research": research_pages(site)}


def replace_patterns(repo):
    """the regexes of tools/public-replace.local.txt (untracked: what the public copy hides), or []"""
    f = Path(repo) / "tools" / "public-replace.local.txt"
    out = []
    for line in (f.read_text(errors="replace").splitlines() if f.exists() else []):
        if not line.strip() or line.startswith("#"):
            continue
        pat = line.split("==>", 1)[0]
        out.append(re.compile(pat[6:]) if pat.startswith("regex:") else re.compile(re.escape(pat[8:] if pat.startswith("literal:") else pat)))
    return out


def strings(v, path=""):
    if isinstance(v, str):
        yield path, v
    elif isinstance(v, dict):
        for k, x in v.items():
            yield from strings(x, "%s.%s" % (path, k) if path else k)
    elif isinstance(v, list):
        for n, x in enumerate(v):
            yield from strings(x, "%s[%d]" % (path, n))


def scan(tech, repo=REPO, ident=None):
    """the public-safe scan of every string in tech: ["<json path>: <what> (<match>)"]"""
    import evidence
    pats = replace_patterns(repo)
    errs = []
    for p, s in strings(tech):
        for what, m in evidence.leftovers(s, ident):
            errs.append("%s: %s (%r)" % (p, what, m))
        for r in pats:
            if r.search(s):
                errs.append("%s: a text tools/public-replace.local.txt hides" % p)
    return errs


def write(content, cat, out, repo=REPO):
    tech = build(content, cat, repo)
    errs = scan(tech, repo)
    if errs:
        raise TechnicalError("technical.json is not public-safe:\n  " + "\n  ".join(errs))
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_text(json.dumps(tech, indent=1, ensure_ascii=False) + "\n")
    return tech
