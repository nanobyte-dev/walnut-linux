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
  The benchmark tables show on the game's Test runs tab (games/<id>/test-runs/, content/tabs/), from the
  same entry. A game's guides (guides/*.md: how-tos, tips, recommended settings) are its entry's guides[],
  {slug, title, body} in file name order, for its Guides tab (games/<id>/guides/): each one's first "# "
  heading is its title and its other headings move one level down, under that title on the page.
  A game with a [data_setup] has a data_guide (tools/gamedb/dataguide.py: its three ways to the data, made from
  the [data_setup], gdp-manifest.json and our game-data-packager patches) and always a game-data guide, first,
  whose guides/game-data.md (when there is one) holds the hand-written notes under the generated part.
- A game without a technical.md still gets its page: the data sections alone, and a line saying there
  are no notes yet. Nothing is made up to fill it.
- Markdown is passed on for Hugo to render; its first "# " heading becomes the page title. Relative
  links are rewritten for the site, relative to the page: a survey, a published game's page, technical
  page, guide or investigation, an engine go to their pages; any other file of the repository to the public
  repository on GitHub; a link into a draft's bundle, a licensed directory, a session's raw/ or a path
  that does not exist keeps its text and loses the link.
- References (gamedb/README.md: `gamedb:<id>`, `gamedb:<id>/<investigation-id>`, `gamedb:batch/<name>`)
  outside code become links: a published game to its technical page, an engine to its page, an
  investigation to its page (its title the text); a batch to its table (`#batch-<name>`) on the Test runs
  tab of the page's own game when that game has results in it, else of the first published game that has
  (the engine's games first), else plain text. A draft's game or investigation is its id as plain text.
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
sys.path.insert(0, str(REPO / "bench" / "analysis"))
import check          # noqa: E402
import benchrecords   # noqa: E402
import dataguide      # noqa: E402

SCHEMA = 1
# a file or directory of the repository on the public copy's main branch
GITHUB = "https://github.com/nanobyte-dev/walnut-linux/%s/main/%s"
LINK_RE = re.compile(r"(!?)\[((?:[^\[\]\n]|\[[^\[\]\n]*\])*)\]\(<?([^()\s<>]+(?:\([^()\s]*\)[^()\s<>]*)?)>?((?:\s+\"[^\"\n]*\")?)\)")
VARIANT_RE = re.compile(r"^variant_([A-Za-z0-9_]+)\s*\(\)")
# a gamedb reference as tools/doclinks.py reads it (not a <!-- gamedb:NAME --> block marker)
REF_RE = re.compile(r"(?<![\w/.-])(?<!<!-- )(?<!<!-- /)gamedb:(batch/[A-Za-z0-9._-]+|[a-z0-9][a-z0-9-]{0,63}"
                    r"(?:/[a-z0-9][a-z0-9.-]*)?)(?![\w/-])")
# what references are not replaced in: inline code, and links already written
SKIP_RE = re.compile(r"(`+).+?\1|" + LINK_RE.pattern)


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
        self.batch_games = {}        # batch name: the published games with results in it (build() fills it)
        self.engine_games = {}       # engine: the published games on it (engine.md's games)
        for e in self.engines:
            f = self.root / "engines" / e / "engine.md"
            if f.is_file():
                self.engine_games[e] = [g for g in split_md(f.read_text())[0].get("games", []) if g in self.published]

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
            if kind == "games" and len(rest) == 2 and rest[0] == "guides" and rest[1].endswith(".md"):
                return base + "guides/" if (self.root / kind / name / "guides" / rest[1]).is_file() else None
            if len(rest) == 2 and rest[0] == "investigations" and rest[1].endswith(".md"):
                f = self.root / kind / name / "investigations" / rest[1]
                return "%sinvestigations/%s/" % (base, self.inv_id(f)) if f.is_file() else None
        p = self.repo / rel
        if not p.exists() or rel.startswith(".."):
            return None
        return GITHUB % ("tree" if p.is_dir() else "blob", rel)

    def rel(self, target, page):
        """the site path target ("games/x/technical/#a") relative to the page at site path page"""
        path, _, anchor = target.partition("#")
        if path.rstrip("/") == page.rstrip("/"):
            return "#" + anchor if anchor else "./"
        return posixpath.relpath(path.rstrip("/"), page.rstrip("/")) + "/" + ("#" + anchor if anchor else "")

    def inv_by_id(self, owner, iid):
        """the investigation file with id iid of game or engine owner, or None"""
        for kind in ("games", "engines"):
            d = self.root / kind / owner / "investigations"
            for f in sorted(d.glob("*.md")) if d.is_dir() else []:
                if self.inv_id(f) == iid:
                    return kind, f
        return None

    def ref(self, m, page):
        """a gamedb: reference as a link for the page at site path page, or its plain text (a sentence's
        full stop after it is not part of it)"""
        what = m[1].rstrip(".")
        return self._ref(what, "gamedb:" + what, page) + m[1][len(what):]

    def _ref(self, what, raw, page):
        if what.startswith("batch/"):
            name = what[6:]
            have = self.batch_games.get(name, set())
            parts = page.split("/")
            prefer = [parts[1]] if len(parts) > 1 and parts[0] == "games" else []
            if len(parts) > 1 and parts[0] == "engines":
                prefer = self.engine_games.get(parts[1], [])
            gid = next((g for g in prefer if g in have), None) or next(iter(sorted(have)), None)
            text = "batch %s" % name
            return "[%s](%s)" % (text, self.rel("games/%s/test-runs/#batch-%s" % (gid, name), page)) if gid else text
        owner, _, iid = what.partition("/")
        if not iid:
            if owner in self.published:
                fm, _ = split_md((self.root / "games" / owner / "index.md").read_text())
                return "[%s](%s)" % (fm.get("title", owner), self.rel("games/%s/technical/" % owner, page))
            if owner in self.engines:
                f = self.root / "engines" / owner / "engine.md"
                title = split_md(f.read_text())[0].get("title", owner) if f.is_file() else owner
                return "[%s](%s)" % (title, self.rel("engines/%s/" % owner, page))
            return owner if (self.root / "games" / owner).is_dir() else raw
        found = self.inv_by_id(owner, iid)
        if not found:
            return raw
        kind, f = found
        if kind == "games" and owner not in self.published:
            return iid
        title, _ = title_body(split_md(f.read_text())[1])
        return "[%s](%s)" % (title or iid, self.rel("%s/%s/investigations/%s/" % (kind, owner, iid), page))

    def refs(self, line, page):
        """line with its gamedb: references as links, outside inline code and links"""
        out, pos = [], 0
        for m in SKIP_RE.finditer(line):
            out.append(REF_RE.sub(lambda r: self.ref(r, page), line[pos:m.start()]))
            out.append(m[0])
            pos = m.end()
        out.append(REF_RE.sub(lambda r: self.ref(r, page), line[pos:]))
        return "".join(out)

    def links(self, md, src, page):
        """md with its relative links rewritten for the page at site path page (src: the file's repository
        path), its gamedb: references made links"""
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
            out.append(self.refs(LINK_RE.sub(lambda x: self._link(x, src, page), line), page))
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


def demote(md):
    """md with each ATX heading one level down (## -> ###), outside fenced code"""
    out, fence = [], None
    for line in md.split("\n"):
        m = re.match(r"\s*(```+|~~~+)", line)
        if fence:
            fence = None if m and m[1][0] == fence[0] and len(m[1]) >= len(fence) else fence
        elif m:
            fence = m[1]
        elif re.match(r"#{1,5} ", line):
            line = "#" + line
        out.append(line)
    return "\n".join(out)


DATA_GUIDE = "game-data"
DATA_GUIDE_TITLE = "Setting up the game data"


def guides(site, gdir, data_guide=False):
    """a game's guides (guides/*.md), in file name order: [{slug, title, body}], the body's headings one level
    down (the page puts each guide under its title). With data_guide (a game with a [data_setup]: its data
    guide is generated, dataguide.py) the game-data guide comes first and exists even without a
    guides/game-data.md, whose body is then the hand-written notes under the generated part"""
    url = "games/%s/guides/" % gdir.name
    out = []
    for f in sorted((gdir / "guides").glob("*.md")) if (gdir / "guides").is_dir() else []:
        title, _, body = markdown_page(site, f, url, f.stem)
        out.append({"slug": f.stem, "title": title, "body": demote(body)})
    if data_guide:
        mine = [g for g in out if g["slug"] == DATA_GUIDE] or [{"slug": DATA_GUIDE, "title": DATA_GUIDE_TITLE, "body": ""}]
        out = mine + [g for g in out if g["slug"] != DATA_GUIDE]
    return out


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


# a harness suffix of a variant (report.py: "@" something alongside the game, "+" an ENVS switch): its words
HARNESS_TOKENS = [(re.compile(r"rec(\d+)"), "stall recorder at %s ms"), (re.compile(r"wkkick(\d+)"), "waker kick at %s ms"),
                  (re.compile(r"wksleep(\d+)"), "waker sleep at %s ms"), (re.compile(r"rtprio(\d+)"), "real-time priority %s"),
                  (re.compile(r"perf"), "perf recording"), (re.compile(r"perfload"), "start-up profile"),
                  (re.compile(r"shot"), "screenshots in the window")]
# the steady-state window (gamedb/benchmarks/README.md): a row with fps_steady was measured this way
STEADY_LENGTH = "warm-up + 120 s"


def caveats(root):
    """[[caveat]] of gamedb/benchmarks/caveats.toml (README.md "Caveats"), [] without the file"""
    f = Path(root) / "benchmarks" / "caveats.toml"
    return tomllib.loads(f.read_text()).get("caveat", []) if f.is_file() else []


def caveat_of(cavs, batch, gid, variant):
    """the caveat that covers a row: the most specific one (variant, then game, then batch), or None"""
    best = None
    for c in cavs:
        if c.get("batch") != batch or c.get("game", gid) != gid or c.get("variant", variant) != variant:
            continue
        rank = ("game" in c) + ("variant" in c)
        if best is None or rank > best[0]:
            best = (rank, c)
    return {"kind": best[1]["kind"], "why": best[1]["why"]} if best else None


def recipe_env(gdir):
    """({variant: [ENV tokens]}, the base ENV tokens, the resolution the recipe names or None) of the
    game's benchmark/recipe.sh: a variant's tokens are its function's ENV+=(...) words"""
    f = Path(gdir) / "benchmark" / "recipe.sh"
    if not f.is_file():
        return {}, [], None
    text = f.read_text(errors="replace")
    base = re.search(r"^ENV=\(([^)]*)\)", text, re.M)
    out = {}
    lines = text.split("\n")
    for i, line in enumerate(lines):
        m = VARIANT_RE.match(line)
        if not m:
            continue
        body = [line]
        if not line.rstrip().endswith("}"):     # a function over several lines: up to its closing brace
            for nxt in lines[i + 1:]:
                body.append(nxt)
                if nxt.strip() == "}":
                    break
        out[m[1]] = [w for e in re.findall(r"ENV\+=\(([^)]*)\)", "\n".join(body)) for w in e.split()]
    cmd = re.search(r"^CMD=\((.*?)\)\s*$", text, re.M | re.S)
    res = re.search(r"(?<![0-9])([1-9][0-9]{2,3})x([1-9][0-9]{2,3})(?![0-9])", cmd[1]) if cmd else None
    return out, base[1].split() if base else [], ("%sx%s" % (res[1], res[2]) if res else None)


def conditions(variant, env, base_env):
    """(the recipe's variant name, [condition words]) of a recorded variant such as base@rec10+rsx_shadow_fast1:
    what the variant adds to the recipe (its ENV+= words; base adds nothing), then each harness suffix in words.
    The recipe's own ENV (base_env) is today's, so it is shown once for the game, not on old rows"""
    name = re.split(r"[@+]", variant, 1)[0]
    words = list(env.get(name, []))
    for tok in re.findall(r"[@+][^@+]+", variant):
        t = tok[1:]
        for rx, text in HARNESS_TOKENS:
            m = rx.fullmatch(t)
            if m:
                words.append(text % m.groups() if m.groups() else text)
                break
        else:
            words.append(("switch " if tok[0] == "+" else "") + t)
    return name, words


def sessions_of(gdir, repo):
    """the game's played sessions in detail and as CSV (tools/gamedb/sessions.py), for its Test runs tab"""
    import sessions
    ss = sessions.game_sessions(gdir, repo)
    return {"list": ss, "csv": sessions.to_csv(ss) if ss else None}


def bench_tables(gid, results, batches, metrics, gdir=None, cavs=()):
    """the game's result tables: [{batch facts, columns, rows}], the summary batches first, then the run
    batches newest first. A run batch's row also says what was tested: the variant, its conditions (the
    recipe's ENV, the harness suffixes), the batch's Mesa and kernel build, the recipe's resolution, the run
    length (steady state or the whole run), the main numbers, and the batch's caveat (caveats.toml)"""
    info = {b["name"]: b for b in batches}
    env, base_env, res = recipe_env(gdir) if gdir else ({}, [], None)
    out = []
    for name, games in results.items():
        t = games.get(gid)
        if not t:
            continue
        b = info.get(name, {})
        variants = t.get("variant", {})
        cols = [m for m in metrics if any(m["name"] in e for e in variants.values())]
        stack = b.get("stack", {})
        build = ", ".join("%s %s" % (k.capitalize() if k in ("mesa", "kernel") else k, v)
                          for k, v in stack.items() if k in ("mesa", "kernel", "ps3rsx"))
        rows = []
        for v, e in variants.items():
            vname, words = conditions(v, env, base_env)
            rows.append({"variant": v, "name": vname, "conditions": words, "label": e.get("label"), "note": e.get("note"),
                         "order": e.get("order", 0), "build": build or None, "resolution": res,
                         "length": STEADY_LENGTH if "fps_steady" in e else "whole run",
                         "fps": fmt(e.get("fps")), "steady": fmt(e.get("fps_steady")),
                         "low1": fmt(e.get("low1_play", e.get("low1"))),
                         "stalls": fmt(e.get("stalls_play", e.get("stalls"))),
                         "caveat": caveat_of(cavs, name, gid, v),
                         "values": [fmt(e.get(m["name"])) for m in cols]})
        if b.get("kind") == "summary":
            rows.sort(key=lambda r: (r["order"], r["variant"]))
        else:
            rows.sort(key=lambda r: (r["name"] != "base", r["variant"]))
        for r in rows:
            del r["order"]
        cav = caveat_of(cavs, name, gid, None) if all(r["caveat"] for r in rows) else None
        main = {"fps", "fps_steady", "low1", "low1_play", "stalls", "stalls_play"}
        out.append({"batch": name, "kind": b.get("kind", "run"), "order": b.get("order", 0), "title": b.get("title"),
                    "date": str(b["date"]) if b.get("date") else None, "runs": b.get("runs"),
                    "stack": [{"name": k, "version": str(v)} for k, v in stack.items()],
                    "observations": b.get("observations"), "purpose": b.get("purpose"),
                    "tested": b.get("tested"), "conclusion": b.get("conclusion"), "column": t.get("column"),
                    "caveat": cav,
                    "columns": [{"name": m["name"], "unit": m.get("unit"), "better": m.get("better"),
                                 "what": m.get("what"), "main": m["name"] in main} for m in cols],
                    "rows": rows})
    out.sort(key=lambda x: (x["kind"] != "summary", -x["order"], x["batch"]))
    for x in out:
        del x["order"]
    return out


def comment_text(lines):
    return " ".join(l.lstrip("#").strip() for l in lines).strip()


def header_short(text, limit=240):
    """the recipe header in a line or two: the parentheses dropped, the first sentence, cut at a word"""
    if not text:
        return None
    t = text
    while True:
        u = re.sub(r"\s*\([^()]*\)", "", t)
        if u == t:
            break
        t = u
    parts = re.split(r"(?<=[A-Za-z0-9\"`])\.\s+(?=[A-Z])", t.strip())
    t = parts[0].strip()
    if len(t) < 60 and len(parts) > 1:      # "SuperTuxKart 1.5": say a little more
        t += ". " + parts[1].strip()
    if len(t) > limit:
        t = t[:limit].rsplit(" ", 1)[0].rstrip(",;:") + "..."
    return t if t.endswith(".") or t.endswith("...") else t + "."


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


def game_page(site, gdir, g, results, batches, metrics, patch_subjects, package_dir, manifest=None):
    gid = gdir.name
    dg = dataguide.build(gid, g["title"], g.get("data_setup"), manifest or {}, site.repo, patch_subjects, package_dir)
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
        "build": {"packages": b.get("packages", []), "suite": b.get("suite"),
                  "needs_data": b.get("needs_data", False), "data": b.get("data", []), "desktop": b.get("desktop", []),
                  "divert": b.get("divert", []), "image_base": b.get("image_base", False),
                  "licensed": [x["name"] for x in b.get("licensed", []) if isinstance(x, dict) and "name" in x]},
        "our_packages": packages(check.patched_by(gdir, b), site.repo, patch_subjects, package_dir),
        "patch_count": len(g.get("patches", [])),
        "test": test, "sessions": len(sessions), "tests": len(g["tests"]),
        "benchmark": {"header": header, "header_short": header_short(header), "variants": variants, "recipes": recipes,
                      "tables": bench_tables(gid, results, batches, metrics, gdir, site.caveats),
                      "resolution": recipe_env(gdir)[2], "env": recipe_env(gdir)[1]},
        "played": sessions_of(gdir, site.repo),
        "investigations": [{k: i[k] for k in ("id", "title", "url", "date")} for i in invs],
        "guides": guides(site, gdir, dg is not None),
        "data_guide": dg,
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
    site.batch_games = {b: {g for g in t if g in site.published} for b, t in results.items()}
    batches = benchrecords.load_batches(root)
    metrics = benchrecords.load_metrics(root)
    site.caveats = caveats(root)
    manifest = dataguide.load_manifest(repo)
    games, invs = [], []
    for g in cat["games"]:
        gdir = content / g["id"]
        page, gi = game_page(site, gdir, g, results, batches, metrics, patch_subjects, package_dir, manifest)
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
