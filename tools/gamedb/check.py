#!/usr/bin/env python3
"""check.py [--content DIR] [--apt-index FILE|URL]: validate the Walnut Games catalog (gamedb/games, and
the engines' build.toml in gamedb/engines) before it is built or published (tools/gamedb/build.sh runs
it first).

Design
- One folder per game (gamedb/README.md): <id>/index.md (TOML front matter between +++ lines), thumb.png,
  and rating/sessions/<name>/result.toml (+ the screenshots it lists), one folder per played session or
  run. The checks:
    * id: ^[a-z0-9][a-z0-9-]{0,63}$ and equal to the folder name; unique
    * required fields present and of the right type, no unknown field (a typo would silently vanish)
    * tags: ids of gamedb/tags.toml (taxonomy(): groups, subgroups, tags; the file itself checked by
      tags_errors()), none of them automatic (catalog.py derives those: AUTOMATIC_TAGS), none twice, a subgroup's
      tag without its parent (the parent is implied); a published game has at least one genre. The old
      `categories` key is refused (genres are tags now)
    * build.toml (BUILD_KEYS, a closed set; gamedb/README.md): packages are Debian package names (they
      must end in [a-z0-9]), none of them licensed (tools/licensed.py names: those go to the licensed
      suite only, never into the public catalog), each one built by a debian/control of ours or, with
      --apt-index (a Packages file or URL: the Debian snapshot's), in that index; engine names an
      engines/<e>/ whose engine.md lists the game (and every game engine.md lists says so); suite one
      of SUITES (licensed: a licensed data package); [[licensed]]
      entries (LICENSED_KEYS: name, builder, leak_files, edition, release, input)
      match the bundle's licensed/<pkg>/ directories (builder source: a package.conf, script: <pkg>.sh;
      game-data-packager:<game> and retired have none), every licensed/ directory declared; a checkout
      without any licensed directory (the public copy) skips that direction. Each entry has an edition
      (EDITIONS: [data_setup] editions, else its own edition key, never both); release only for source and
      script builders (game-data-packager's comes from the files it matched), input only for
      game-data-packager ones; a source package's debian/control gives its binary the same edition and
      release (XB-X-Walnut-Edition, XB-X-Walnut-Release), a script names tools/gamedb/build-info.py fields.
      [data_setup] (DATA_SETUP_KEYS, closed):
      its gdp game and every required/optional package exist in game-data-packager's data, read from the
      committed tools/gamedb/gdp-packages.json (gdp-index.py, make gdp-index: no docker at check time);
      required set exactly when needs_data, downloads among them, an edition (EDITIONS) for every
      required/optional package, the same one tools/gamedb/gdp-manifest.json has for it (make gdp-manifest). A published game has packages,
      suite and needs_data. The catalog's patched_by is not a key: our source packages of the bundle
      (packages/) and of its engine (patched_by() below)
    * a game that needs the user's own data (build.toml needs_data) has a data_guide URL and data_steps
      in index.md (the numbered steps of getting and setting up the data: sentences, with `code` in
      backticks that must pair up)
    * thumb.png (optional) is a PNG; every test has a parsable result.toml of schema 1 whose listed screenshots
      exist (shots/ps3-<point>.jpg), whose source, when there is one, is "submitted" (a tester's session, with
      an optional tester nickname of up to 40 characters), and no stray file sits in a session folder (result.toml, form.toml,
      review.md, shots/, raw/; tools/gamedb/evidence.py, which also scans the record, form.toml, review.md
      and raw/ for what must not be public) or the game's folder (GAME_FILES: index.md, thumb.png,
      build.toml, rating/, packages/, licensed/, upstream.md, upstream/, guides/: guide_errors(), benchmark/: BENCH_FILES,
      desktop/: DESKTOP_FILES, its launchers and migrate-home.txt as tools/gamedb/pack-integration.py reads them); its rules, when given, are "<data>.<formula>" of a
      data version score.py knows, and a data-version-2 test's defects name known categories
    * rating/ holds sessions/ and the played test, rating/test.toml: bench/harness/playdef.py's schema (closed keys,
      process a process name, home relative paths inside ~ that no other game shares, the marks compile,
      command an absolute program, env NAME=value); benchmark/analysis.toml and engines/<e>/analysis.toml
      follow bench/harness/gamedata.py's (closed keys: [legacy], [reload], [[log_ignore]], [[log_strip]],
      [interference]; an engine's only the log rules; every regex compiles); benchmark/results/<batch>.toml and
      gamedb/benchmarks/{batches,metrics}.toml: records_errors() (bench/analysis/benchrecords.py; gamedb/benchmarks/README.md);
      gamedb/benchmarks/caveats.toml: caveats_errors()
    * todo.toml (a game's or an engine's backlog: [[item]] tables): todo_errors() (closed keys TODO_KEYS, kind
      one of TODO_KINDS, added a date, order an int of 1 or more, order/launcher/pc on session items only)
    * drafts (draft = true: a candidate, or a game tried and not published) need only id, title and
      status (one of STATUSES; status_note says why); summary, license, homepage, tags,
      controller, thumb.png and build.toml (or any of its keys) may be missing, and nothing is made up for them;
      whatever a draft has is checked like a published game's. catalog.py (and so pack-catalog.py)
      and Hugo leave drafts out
- Prints every problem with the game it belongs to, exits 1 if there is any.
"""
import argparse
import datetime
import gzip
import importlib.util
import json
import lzma
import re
import sys
import tomllib
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import score   # noqa: E402
import evidence  # noqa: E402  (a session folder's evidence and its public-safe scan)
# the tags (gamedb/tags.toml, taxonomy()); the automatic ones are derived by catalog.py (game_tags()) and never
# listed in index.md: the file must mark exactly these automatic
TAGS_FILE = REPO / "gamedb" / "tags.toml"
AUTOMATIC_TAGS = ("gamepad", "keyboard-mouse", "own-data", "free-download", "low-memory")
GENRE = "genre"
ENGINE = "engine"     # the group whose tags are automatic: each leaf names its engine bundle (`engine`)
CONTROLLERS = ["full", "partial", "keyboard", "unknown"]
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]{0,63}\Z")
PKG_RE = re.compile(r"[a-z0-9][a-z0-9+.-]*[a-z0-9]\Z")
URL_RE = re.compile(r"https?://[^\s]+\Z")
# field: (type, required)
FIELDS = {
    "id": (str, True), "title": (str, True), "summary": (str, True),
    "tags": (list, True), "license": (str, True), "homepage": (str, True), "data_guide": (str, False),
    "data_steps": (list, False),
    "controller": (str, True), "fps_cap": ((int, float), False), "medal_note": (str, False),
    "known_issues": (list, False), "designed_for": (str, False),
    "draft": (bool, False), "status": (str, False), "status_note": (str, False),
}
# build.toml (gamedb/README.md): key: (type, required for a published game). A closed set: a typo would
# silently vanish. recipes: the bench runs that stand for the game, "<id>:<variant>" of its own recipe
BUILD_KEYS = {
    "packages": (list, True), "engine": (str, False), "suite": (str, True), "needs_data": (bool, True),
    "data": (list, False), "desktop": (list, False), "divert": (list, False), "image_base": (bool, False),
    "recipes": (list, False), "licensed": (list, False), "former_packages": (list, False),
    "data_setup": (dict, False),
}
# [data_setup] (gamedb/README.md): the player's own data, imported with game-data-packager (gdp). key: (type,
# required); every package one of the gdp game's (GDP_INDEX, made by gdp-index.py: offline, no docker)
DATA_SETUP_KEYS = {"gdp": (str, True), "required": (list, True), "optional": (list, False),
                   "downloads": (list, False), "fetch": (list, False), "alternatives": (list, False), "hint": (str, False), "original": (str, False), "editions": (dict, True)}
# [data_setup] editions = {package: edition}: what a data package holds (X-Walnut-Edition in the control of a
# package walnut-games-helper import-data builds; the licensed suite's builders and the session harness read it)
EDITIONS = ("shareware", "demo", "full", "expansion", "music", "remaster")
GDP_MANIFEST = Path(__file__).resolve().parent / "gdp-manifest.json"
GDP_INDEX = Path(__file__).resolve().parent / "gdp-packages.json"
# an engine's build.toml: the packages the games run it from, its licensed data
ENGINE_KEYS = {"packages": (list, False), "licensed": (list, False)}
# [[licensed]] (gamedb/README.md): edition (EDITIONS, for a package [data_setup] editions doesn't name) and
# release (a source or script build's fixed release, RELEASE_RE as walnut-games-helper's) are its
# X-Walnut-Edition and X-Walnut-Release; input: game-data-packager makes it from the owner's copy
# (make licensed-data), never from a download; builder retired: published once, built no more
LICENSED_KEYS = {"name", "builder", "leak_files", "edition", "release", "input"}
RELEASE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,79}\Z")
SUITES = ["free", "licensed", "none"]
BUILDER_RE = re.compile(r"(source|script|retired|game-data-packager:[a-z0-9][a-z0-9-]*)\Z")
# a draft (draft = true: a candidate or a game we tried, not published: catalog.py, pack-catalog.py and
# Hugo leave it out) needs only these; whatever else it has is checked as usual, nothing is filled in
DRAFT_REQUIRED = {"id", "title", "status"}
STATUSES = ["candidate", "tried", "blocked", "dropped", "impossible", "tested"]
# what a game's folder may hold so far (gamedb/README.md; the later migration steps add theirs): the page,
# the thumbnail, the sessions, our source packages, the licensed data packages and builders, upstream offers
GAME_DIRS = ("rating", "packages", "licensed", "upstream", "benchmark", "desktop", "investigations", "guides")
# what desktop/ may hold: our launchers, the config files we install (by absolute path), the migrate-home lines
DESKTOP_FILES = ("launchers", "files", "migrate-home.txt")
# what benchmark/ may hold so far: the recipe (code), its analysis data, its helpers and files, the sources of
# built files (assets.mk), superseded tools, the results (step 5; step 6 adds runs/)
BENCH_FILES = ("recipe.sh", "analysis.toml", "lib", "assets", "assets-src", "legacy", "results")
GAME_FILES = ("index.md", "thumb.png", "build.toml", "upstream.md", "technical.md", "todo.toml") + GAME_DIRS
# todo.toml (gamedb/README.md): [[item]] tables with these keys (a closed set): kind one of TODO_KINDS, text,
# added (a date), source (where it came from: "PLAN 2.6", "TESTS.md Next", ...); a session item may have
# order (its place in the played-session queue, docs/TESTS.md's block), launcher and pc (the PC side)
TODO_KINDS = ("session", "benchmark", "package", "fix", "research", "upstream")
TODO_KEYS = {"kind": str, "text": str, "added": datetime.date, "source": str, "order": int, "launcher": str, "pc": str}
TODO_REQUIRED = ("kind", "text", "added")
TODO_SESSION_ONLY = ("order", "launcher", "pc")
# where a game's sessions (each one's result.toml and screenshots) live, relative to its folder
SESSIONS = "rating/sessions"
CONTENT = REPO / "gamedb" / "games"


def read_build(gdir):
    """a bundle's (or an engine's) build.toml, parsed; {} when it has none"""
    f = Path(gdir) / "build.toml"
    return tomllib.loads(f.read_text()) if f.exists() else {}


def own_packages(d):
    """the names of the source packages of ours in a bundle's or engine's packages/ (sorted)"""
    p = Path(d) / "packages"
    return sorted(x.name for x in p.iterdir() if (x / "package.conf").is_file()) if p.is_dir() else []


def patched_by(gdir, build=None):
    """the source packages of ours that patch a game: the bundle's packages/, then its engine's
    (catalog.py lists the patches of each one that has a patches/series)"""
    build = read_build(gdir) if build is None else build
    out = own_packages(gdir)
    if isinstance(build.get("engine"), str):
        out += [p for p in own_packages(Path(gdir).parent.parent / "engines" / build["engine"]) if p not in out]
    return out


def game_facts(gdir):
    """index.md's front matter with build.toml's keys the catalog shows (packages, needs_data, desktop,
    recipes, data_setup: data_setup_facts) and patched_by, as catalog.py reads a game"""
    fm = front_matter(Path(gdir) / "index.md")
    b = read_build(gdir)
    fm.update({k: b[k] for k in ("packages", "needs_data", "desktop", "recipes", "former_packages") if k in b})
    fm["patched_by"] = patched_by(gdir, b)
    ds = data_setup_facts(b)
    if ds:
        fm["data_setup"] = ds
    return fm


def data_setup_facts(build):
    """a build.toml's [data_setup] as the catalog carries it (every key, defaults filled in), or None:
    gdp, required, optional, downloads, hint and original (None when unset), editions and present_any, the packages any one of
    which means the game's data is installed: required, then the game's data and [[licensed]] names
    (the licensed suite's builds of the same data, e.g. rct2-data next to roller-coaster-tycoon2-data)"""
    ds = build.get("data_setup")
    if not isinstance(ds, dict):
        return None
    own = list(build.get("data", [])) + [e.get("name") for e in build.get("licensed", []) if isinstance(e, dict)]
    return {"gdp": ds.get("gdp"), "required": list(ds.get("required", [])), "optional": list(ds.get("optional", [])),
            "downloads": list(ds.get("downloads", [])), "alternatives": [list(g) for g in ds.get("alternatives", [])], "fetch": list(ds.get("fetch", [])), "hint": ds.get("hint"), "original": ds.get("original"), "editions": dict(ds.get("editions", {})),
            "present_any": list(dict.fromkeys(list(ds.get("required", [])) + [p for p in own if isinstance(p, str)]))}


def control_binaries(repo):
    """{binary package: source directory} of every debian/control in our package directories"""
    out = {}
    for name, d in score.package_dirs(repo).items():
        ctl = Path(repo) / d / "debian" / "control"
        if ctl.is_file():
            for m in re.finditer(r"^Package:\s*(\S+)", ctl.read_text(), re.M):
                out[m[1]] = d
    return out


def apt_index(src):
    """the package names of a Packages file (plain, .gz or .xz; a path or an http(s) URL)"""
    if re.match(r"https?://", src):
        with urllib.request.urlopen(src, timeout=60) as f:
            data = f.read()
    else:
        data = Path(src).read_bytes()
    if src.endswith(".xz"):
        data = lzma.decompress(data)
    elif src.endswith(".gz"):
        data = gzip.decompress(data)
    return set(re.findall(r"^Package:\s*(\S+)", data.decode("utf-8", "replace"), re.M))


def bench_module(name):
    """a module of the bench tree (bench/harness/: playdef.py's and gamedata.py's schemas; bench/analysis/:
    benchrecords.py's records)"""
    f = next(f for f in (REPO / "bench" / d / (name + ".py") for d in ("harness", "analysis")) if f.exists())
    spec = importlib.util.spec_from_file_location(name, f)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def toml_errors(path, what, validate):
    """the problems of a TOML data file (validate: the parsed dict -> [problems]), each prefixed with what"""
    try:
        data = tomllib.loads(Path(path).read_text())
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as e:
        return ["%s: %s" % (what, e)]
    return ["%s: %s" % (what, e) for e in validate(data)]


def licensed_module():
    spec = importlib.util.spec_from_file_location("licensed", Path(__file__).resolve().parent.parent / "licensed.py")
    lic = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lic)
    return lic


def licensed(repo):
    """the licensed package names (tools/licensed.py, the one answer; it fails closed: an unreadable
    declaration raises)"""
    return licensed_module().names(repo)


def is_draft(gdir):
    """the game folder holds a draft (index.md front matter draft = true); False when it can't be read"""
    try:
        return front_matter(Path(gdir) / "index.md").get("draft") is True
    except (ValueError, OSError, tomllib.TOMLDecodeError):
        return False


def front_matter(path):
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+", path.read_text(), re.S)
    if not m:
        raise ValueError("no +++ TOML front matter")
    return tomllib.loads(m[1])


def todo_errors(data):
    """the problems of a parsed todo.toml (TODO_KEYS; an engine's has the same form)"""
    errs = ["unknown table %r (todo.toml holds [[item]] tables)" % k for k in data if k != "item"]
    items = data.get("item", [])
    if not isinstance(items, list) or not all(isinstance(i, dict) for i in items):
        return errs + ["item: [[item]] tables"]
    for n, it in enumerate(items, 1):
        for k, v in it.items():
            t = TODO_KEYS.get(k)
            if t is None:
                errs.append("item %d: unknown key %r (%s)" % (n, k, ", ".join(TODO_KEYS)))
            elif not isinstance(v, t) or isinstance(v, bool) or (t is datetime.date and isinstance(v, datetime.datetime)):
                errs.append("item %d: %r should be %s" % (n, k, "a date" if t is datetime.date else t.__name__))
        errs += ["item %d: missing key %r" % (n, k) for k in TODO_REQUIRED if k not in it]
        if it.get("kind") is not None and it.get("kind") not in TODO_KINDS:
            errs.append("item %d: kind %r: one of %s" % (n, it["kind"], ", ".join(TODO_KINDS)))
        if it.get("kind") != "session":
            errs += ["item %d: %s only on a session item" % (n, k) for k in TODO_SESSION_ONLY if k in it]
        if isinstance(it.get("order"), int) and not isinstance(it["order"], bool) and it["order"] < 1:
            errs.append("item %d: order must be 1 or more" % n)
        if isinstance(it.get("text"), str) and not it["text"].strip():
            errs.append("item %d: empty text" % n)
    return errs


# an investigation (investigations/<date>-<slug>.md): TOML front matter with these keys (a closed set); id
# is required, every other key a list of strings; games name game folders, sessions folders of
# rating/sessions/ in one of those games, batches gamedb/benchmarks/batches.toml names
INV_KEYS = ("id", "games", "driver_patches", "game_patches", "sessions", "batches")
INV_FILE_RE = re.compile(r"\d{4}-\d{2}-\d{2}-[a-z0-9][a-z0-9-]*\.md\Z")
# a guide (guides/<slug>.md, the game's Guides tab): markdown without front matter that starts with its "# " title;
# game-data.md is the one the Overview's game data box links to
GUIDE_FILE_RE = re.compile(r"[a-z0-9][a-z0-9-]*\.md\Z")


def guide_errors(gdir):
    """the problems of a game's guides/: only <slug>.md files, each starting with a "# " heading, no front matter"""
    errs = []
    d = Path(gdir) / "guides"
    for f in sorted(d.iterdir()) if d.is_dir() else []:
        where = "guides/%s" % f.name
        if not f.is_file() or not GUIDE_FILE_RE.match(f.name):
            errs.append("%s: a guide is a file <slug>.md (lower case letters, digits, dashes)" % where)
            continue
        text = f.read_text()
        if text.startswith("+++") or text.startswith("---"):
            errs.append("%s: no front matter; the first line is the guide's \"# \" title" % where)
        elif not re.match(r"# +\S", text):
            errs.append("%s: the first line is the guide's \"# \" title" % where)
        elif not text[text.find("\n") + 1:].strip() if "\n" in text else True:
            errs.append("%s: a title and nothing else" % where)
    return errs


def investigation_errors(d, games_root):
    """the problems of a game's or an engine's investigations/*.md front matter"""
    errs, ids = [], {}
    idir = Path(d) / "investigations"
    batches = None
    for f in sorted(idir.glob("*.md")) if idir.is_dir() else []:
        where = "investigations/%s" % f.name
        if not INV_FILE_RE.match(f.name):
            errs.append("%s: the name is <date>-<slug>.md" % where)
        try:
            fm = front_matter(f)
        except (ValueError, tomllib.TOMLDecodeError) as e:
            errs.append("%s: %s" % (where, e))
            continue
        for k, v in fm.items():
            if k not in INV_KEYS:
                errs.append("%s: unknown key %r (%s)" % (where, k, ", ".join(INV_KEYS)))
            elif k == "id":
                if not isinstance(v, str) or not ID_RE.match(v):
                    errs.append("%s: id %r must match %s" % (where, v, ID_RE.pattern))
                elif v in ids:
                    errs.append("%s: id %r is %s's too" % (where, v, ids[v]))
                else:
                    ids[v] = f.name
            elif not isinstance(v, list) or not all(isinstance(x, str) and x for x in v):
                errs.append("%s: %r should be a list of strings" % (where, k))
        if "id" not in fm:
            errs.append("%s: missing key 'id'" % where)
        games = [g for g in fm.get("games", []) if isinstance(g, str)]
        for g in games:
            if not (Path(games_root) / g / "index.md").is_file():
                errs.append("%s: games: no game %r" % (where, g))
        for sname in fm.get("sessions", []) if isinstance(fm.get("sessions"), list) else []:
            if not any((Path(games_root) / g / SESSIONS / str(sname)).is_dir() for g in games):
                errs.append("%s: sessions: %r is a session of none of its games" % (where, sname))
        if isinstance(fm.get("batches"), list) and fm["batches"]:
            if batches is None:
                bf = Path(games_root).parent / "benchmarks" / "batches.toml"
                batches = {b.get("name") for b in tomllib.loads(bf.read_text()).get("batch", [])} if bf.is_file() else set()
            errs += ["%s: batches: no batch %r in benchmarks/batches.toml" % (where, b)
                     for b in fm["batches"] if b not in batches]
    return errs


def read_taxonomy(path=TAGS_FILE):
    """gamedb/tags.toml as {"groups": [{id, label, description, collapsed, tags: [ids, a subgroup's after its
    parent]}], "tags": {id: {id, label, description, group, parent, sub: [ids], automatic}}}, in the file's
    order; raises ValueError (with tags_errors()'s problems) on a broken file"""
    data = tomllib.loads(Path(path).read_text())
    errs = tags_errors(data)
    if errs:
        raise ValueError("; ".join(errs))
    groups, tags = [], {}
    for g in data["group"]:
        ids = []
        for t in g["tag"]:
            for x, parent in [(t, None)] + [(s, t["id"]) for s in t.get("sub", [])]:
                tags[x["id"]] = {"id": x["id"], "label": x["label"], "description": x.get("description"),
                                 "group": g["id"], "parent": parent, "automatic": x.get("automatic", False),
                                 "engine": x.get("engine"),
                                 "sub": [s["id"] for s in x.get("sub", [])]}
                ids.append(x["id"])
        groups.append({"id": g["id"], "label": g["label"], "description": g.get("description"),
                       "collapsed": g.get("collapsed", False), "tags": ids})
    return {"groups": groups, "tags": tags}


_TAXONOMY = {}


def taxonomy(path=TAGS_FILE):
    """read_taxonomy(), read once"""
    if path not in _TAXONOMY:
        _TAXONOMY[path] = read_taxonomy(path)
    return _TAXONOMY[path]


def tags_errors(data):
    """the problems of gamedb/tags.toml's content: groups with id and label, tags with id (ID_RE, unique across
    groups) and label, closed key sets, subgroups one level deep, the automatic tags exactly AUTOMATIC_TAGS"""
    errs, seen, auto, auto_engine = [], set(), set(), set()
    if set(data) - {"group"} or not isinstance(data.get("group"), list) or not data["group"]:
        return ["tags.toml: [[group]] tables only"]
    gids = set()
    for g in data["group"]:
        if set(g) - {"id", "label", "description", "collapsed", "tag"}:
            errs.append("tags.toml: group %r: unknown key %s" % (g.get("id"), sorted(set(g) - {"id", "label", "description", "collapsed", "tag"})))
        if not isinstance(g.get("id"), str) or not ID_RE.match(g["id"]) or g["id"] in gids:
            errs.append("tags.toml: group id %r: ^[a-z0-9][a-z0-9-]*$, unique" % (g.get("id"),))
        gids.add(g.get("id"))
        if not isinstance(g.get("label"), str) or not g["label"]:
            errs.append("tags.toml: group %r: no label" % (g.get("id"),))
        if not isinstance(g.get("collapsed", False), bool):
            errs.append("tags.toml: group %r: collapsed is true or false" % (g.get("id"),))
        if not isinstance(g.get("tag"), list) or not g["tag"]:
            errs.append("tags.toml: group %r: no [[group.tag]]" % (g.get("id"),))
            continue
        for t in g["tag"]:
            subs = t.get("sub", [])
            if not isinstance(subs, list):
                errs.append("tags.toml: tag %r: sub is a list of tags" % (t.get("id"),))
                subs = []
            for x, keys in [(t, {"id", "label", "description", "sub", "automatic", "engine"})] + \
                    [(s, {"id", "label", "description", "automatic", "engine"}) for s in subs]:
                if not isinstance(x, dict):
                    errs.append("tags.toml: tag %r: a sub entry is not a table" % (t.get("id"),))
                    continue
                if set(x) - keys:
                    errs.append("tags.toml: tag %r: unknown key %s (subgroups are one level deep)"
                                % (x.get("id"), sorted(set(x) - keys)))
                if not isinstance(x.get("id"), str) or not ID_RE.match(x["id"]) or x["id"] in seen:
                    errs.append("tags.toml: tag id %r: ^[a-z0-9][a-z0-9-]*$, unique across the groups" % (x.get("id"),))
                seen.add(x.get("id"))
                if not isinstance(x.get("label"), str) or not x["label"]:
                    errs.append("tags.toml: tag %r: no label" % (x.get("id"),))
                if not isinstance(x.get("description", ""), str):
                    errs.append("tags.toml: tag %r: description is a string" % (x.get("id"),))
                if x.get("automatic", False) is True:
                    auto.add(x.get("id"))
                elif x.get("automatic", False) is not False:
                    errs.append("tags.toml: tag %r: automatic is true or false" % (x.get("id"),))
                if g.get("id") == ENGINE:
                    auto_engine.add(x.get("id"))
                    if x.get("automatic") is not True:
                        errs.append("tags.toml: engine tag %r: automatic = true" % (x.get("id"),))
                    if "sub" not in x and not isinstance(x.get("engine"), str):
                        errs.append("tags.toml: engine tag %r: no `engine` (the bundle it stands for)" % (x.get("id"),))
                elif "engine" in x:
                    errs.append("tags.toml: tag %r: `engine` belongs to the engine group's tags" % (x.get("id"),))
    if auto != set(AUTOMATIC_TAGS) | auto_engine:
        errs.append("tags.toml: the automatic tags are %s, catalog.py derives %s and the engine group's"
                    % (sorted(auto), sorted(AUTOMATIC_TAGS)))
    if GENRE not in gids:
        errs.append("tags.toml: no group %r" % GENRE)
    return errs


def engine_bundles(root=None):
    """{engine id: where its page is} for gamedb/engines/<e>/ (an engine of its own) and the games that are an
    engine themselves (a game bundle an engine tag names: dsda-doom, quakespasm, yquake2): "engines" or "games" """
    root = Path(root) if root else REPO / "gamedb"
    out = {d.name: "games" for d in sorted((root / "games").iterdir()) if (d / "index.md").is_file()} \
        if (root / "games").is_dir() else {}
    out.update({d.name: "engines" for d in sorted((root / "engines").iterdir()) if (d / "engine.md").is_file()}
               if (root / "engines").is_dir() else {})
    return out


def engine_tags_errors(tax, root=None):
    """the problems of the engine group against the bundles: every engine tag maps to an existing bundle
    (engines/<e>/engine.md, else games/<e>/), an engines/ bundle's title is its tag's label, and every
    engines/ bundle has a tag"""
    errs, bundles = [], engine_bundles(root)
    root = Path(root) if root else REPO / "gamedb"
    seen = {}
    for t in tax["tags"].values():
        if t["group"] == ENGINE and t.get("engine"):
            seen[t["engine"]] = t["id"]
            if t["engine"] not in bundles:
                errs.append("tags.toml: engine tag %r: no gamedb/engines/%s or gamedb/games/%s" % (t["id"], t["engine"], t["engine"]))
            elif bundles[t["engine"]] == "engines":
                title = front_matter(root / "engines" / t["engine"] / "engine.md").get("title")
                if title != t["label"]:
                    errs.append("tags.toml: engine tag %r: label %r, but engine.md's title is %r" % (t["id"], t["label"], title))
    errs += ["tags.toml: engines/%s has no engine tag" % e for e, w in bundles.items() if w == "engines" and e not in seen]
    return errs


def game_tag_errors(tags, draft):
    """the problems of an index.md's tags (None: none given)"""
    if tags is None:
        return []
    try:
        tax = taxonomy()["tags"]
    except (OSError, ValueError, tomllib.TOMLDecodeError) as e:
        return ["tags: gamedb/tags.toml unreadable: %s" % e]
    errs = []
    for t in tags:
        if not isinstance(t, str) or t not in tax:
            errs.append("unknown tag %r (the ids of gamedb/tags.toml)" % (t,))
        elif tax[t]["automatic"]:
            errs.append("tag %r is automatic (catalog.py derives it): leave it out" % t)
        elif tags.count(t) > 1:
            errs.append("tag %r twice" % t)
        elif tax[t]["parent"] in tags:
            errs.append("tag %r: %r is implied by it, leave the parent out" % (t, tax[t]["parent"]))
    if errs:
        return sorted(set(errs), key=errs.index)
    if not draft and not any(tax[t]["group"] == GENRE for t in tags):
        errs.append("tags: no genre (one of the genre group's tags, gamedb/tags.toml)")
    return errs


def check_game(gdir, bad, repo, ctx=None):
    """a list of problems of one game folder"""
    errs = []
    index = gdir / "index.md"
    if not index.exists():
        return ["no index.md"]
    try:
        fm = front_matter(index)
    except (ValueError, tomllib.TOMLDecodeError) as e:
        return ["index.md: %s" % e]
    for k in fm:
        if k == "categories":
            errs.append("unknown field 'categories': genres are tags now (gamedb/tags.toml, group genre)")
        elif k not in FIELDS and k not in ("title", "description", "date"):
            errs.append("unknown field %r" % k)
    draft = fm.get("draft") is True
    for k, (t, req) in FIELDS.items():
        if k not in fm:
            if (k in DRAFT_REQUIRED) if draft else req:
                errs.append("missing field %r%s" % (k, " (a draft needs id, title and status)" if draft else ""))
        elif not isinstance(fm[k], t) or (t is not bool and isinstance(fm[k], bool)):
            errs.append("field %r should be %s" % (k, t.__name__ if isinstance(t, type) else "a number"))
    if any(not e.startswith("unknown") for e in errs):
        return errs   # the later checks need the fields
    if not ID_RE.match(fm["id"]) or fm["id"] != gdir.name:
        errs.append("id %r must match ^[a-z0-9][a-z0-9-]{0,63}$ and the folder name %r" % (fm["id"], gdir.name))
    if "status" in fm and fm["status"] not in STATUSES:
        errs.append("status %r: one of %s" % (fm["status"], ", ".join(STATUSES)))
    errs += game_tag_errors(fm.get("tags"), draft)
    if "homepage" in fm and not URL_RE.match(fm["homepage"]):
        errs.append("homepage is not an http(s) URL")
    try:
        build = read_build(gdir)
    except tomllib.TOMLDecodeError as e:
        return errs + ["build.toml: %s" % e]
    errs += build_errors(gdir, build, bad, ctx, draft)
    if not draft and not (gdir / "technical.md").is_file():
        errs.append("no technical.md (a published game needs its current technical state: gamedb/README.md)")
    if fm.get("designed_for", "pad") not in score.DESIGNED_FOR:
        errs.append("designed_for %r: one of %s (default pad)" % (fm["designed_for"], ", ".join(score.DESIGNED_FOR)))
    if "controller" in fm and fm["controller"] not in CONTROLLERS:
        errs.append("controller %r: one of %s" % (fm["controller"], ", ".join(CONTROLLERS)))
    needs_data = build.get("needs_data") is True
    if needs_data and not URL_RE.match(fm.get("data_guide", "")):
        errs.append("build.toml needs_data = true needs a data_guide URL in index.md (the data itself is never offered)")
    steps = fm.get("data_steps", [])
    if needs_data and not steps:
        errs.append("build.toml needs_data = true needs data_steps in index.md (how to get the data and where it goes)")
    for st in steps:
        if not isinstance(st, str) or not st.strip():
            errs.append("data_steps: each step is a non-empty string")
        elif st.count("`") % 2:
            errs.append("data_steps %r: the backticks around code do not pair up" % st[:40])
    if steps and not needs_data:
        errs.append("data_steps without needs_data = true (build.toml)")
    if fm.get("fps_cap", 1) <= 0:
        errs.append("fps_cap must be positive")
    thumb = gdir / "thumb.png"
    if not thumb.exists():
        pass   # optional: the catalog's thumb is then null, the site and the app show none
    elif thumb.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        errs.append("thumb.png is not a PNG")
    tests = gdir / SESSIONS
    for t in sorted(tests.iterdir()) if tests.is_dir() else []:
        if not (t / "result.toml").exists():
            errs.append(SESSIONS + "/%s: no result.toml" % t.name)
            continue
        try:
            r = tomllib.loads((t / "result.toml").read_text())
        except tomllib.TOMLDecodeError as e:
            errs.append(SESSIONS + "/%s/result.toml: %s" % (t.name, e))
            continue
        if r.get("schema") != 1 or r.get("kind") not in ("session", "demo", "capture", "script") or r.get("game") != fm["id"]:
            errs.append(SESSIONS + "/%s/result.toml: schema 1, a kind and this game's id are required" % t.name)
        rules = str(r.get("rules", "1.0"))
        if not re.fullmatch(r"\d+\.\d+", rules) or score.data_version(r) not in score.FORMULA:
            errs.append(SESSIONS + "/%s/result.toml: rules %r: \"<data>.<formula>\" of data version %s" % (
                t.name, rules, " or ".join(map(str, score.FORMULA))))
        elif score.data_version(r) >= 2:
            bad_defects = [x for x in r.get("review", {}).get("defects", [])
                           if len(score.parse_defects([x])) != 1 or not score.parse_defects([x])[0][1]]
            if bad_defects:
                errs.append(SESSIONS + "/%s/result.toml: defects %r: \"<category> <pc|ps3>: <what>\"" % (t.name, bad_defects))
        if r.get("source", "submitted") != "submitted" or ("tester" in r and (
                r.get("source") != "submitted" or not isinstance(r["tester"], str) or not 0 < len(r["tester"]) <= 40)):
            errs.append(SESSIONS + "/%s/result.toml: source is \"submitted\" (a tester's session) or absent (ours); "
                        "tester, a nickname of 1 to 40 characters, only with it" % t.name)
        listed = set(r.get("shots", {}).get("ps3", []))
        for f in sorted(listed):
            if not (t / f).exists():
                errs.append(SESSIONS + "/%s: screenshot %s is listed but missing" % (t.name, f))
        # the evidence: form.toml, review.md, shots/, raw/; the public-safe scan (tools/gamedb/evidence.py)
        errs += [SESSIONS + "/%s: %s" % (t.name, e) for e in evidence.folder_errors(t, listed)]
    for f in sorted(x.name for x in gdir.iterdir()):
        if f not in GAME_FILES:
            errs.append("unexpected file %s (a game folder holds %s)" % (f, ", ".join(GAME_FILES)))
        elif f in GAME_DIRS and not (gdir / f).is_dir():
            errs.append("%s is not a folder" % f)
    errs += benchmark_errors(gdir, build)
    errs += desktop_errors(gdir, build, ctx)
    rating = gdir / "rating"
    for f in sorted(x.name for x in rating.iterdir()) if rating.is_dir() else []:
        if f not in ("sessions", "test.toml"):
            errs.append("unexpected file rating/%s (rating/ holds sessions/ and test.toml)" % f)
    if (rating / "test.toml").is_file():
        errs += toml_errors(rating / "test.toml", "rating/test.toml", bench_module("playdef").validate)
    if (gdir / "todo.toml").is_file():
        errs += toml_errors(gdir / "todo.toml", "todo.toml", todo_errors)
    errs += investigation_errors(gdir, gdir.parent)
    errs += guide_errors(gdir)
    return errs


def context(repo, content, apt=None):
    """what the build.toml checks look things up in: our debian/control binaries, the apt index (None:
    not checked), whether this checkout has licensed directories (the public copy has none), the engines"""
    lic = licensed_module()
    engines = {}
    edir = Path(content).parent / "engines"
    for e in sorted(edir.iterdir()) if edir.is_dir() else []:
        if (e / "engine.md").is_file():
            try:
                engines[e.name] = front_matter(e / "engine.md").get("games", [])
            except (ValueError, tomllib.TOMLDecodeError):
                engines[e.name] = []
    return {"ours": control_binaries(repo), "apt": apt, "engines": engines, "gdp": gdp_index(), "manifest": gdp_manifest(),
            "has_licensed": bool(lic.licensed_dirs(lic.Tree(repo))) or any(Path(content).glob("*/licensed/*"))
            or any(edir.glob("*/licensed/*"))}


def gdp_index(path=None):
    """{game: {its package names}} game-data-packager knows (tools/gamedb/gdp-packages.json), with its
    version under None; None when the file is missing or broken (then every [data_setup] is an error)"""
    try:
        data = json.loads(Path(path or GDP_INDEX).read_text())
        out = {g: set(p) for g, p in data["games"].items()}
        out[None] = str(data["gdp_version"])
        return out
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return None


def data_setup_errors(ds, build, bad, ctx):
    """build.toml [data_setup] (DATA_SETUP_KEYS): its gdp game and every package in gdp's data (ctx["gdp"]);
    required non-empty exactly when needs_data; optional apart from required; downloads among them"""
    errs = []
    for k in ds:
        if k not in DATA_SETUP_KEYS:
            errs.append("[data_setup]: unknown key %r (%s)" % (k, ", ".join(DATA_SETUP_KEYS)))
    for k, (t, req) in DATA_SETUP_KEYS.items():
        if k not in ds:
            if req:
                errs.append("[data_setup]: missing key %r" % k)
        elif not isinstance(ds[k], t):
            errs.append("[data_setup]: %r should be %s" % (k, t.__name__))
        elif t is list and k != "alternatives" and not all(isinstance(x, str) and PKG_RE.match(x) for x in ds[k]):
            errs.append("[data_setup]: %r is a list of package names" % k)
        elif t is list and k != "alternatives" and len(set(ds[k])) != len(ds[k]):
            errs.append("[data_setup]: %r names a package twice" % k)
    if errs:
        return errs
    ed = ds["editions"]
    for p in sorted(set(ds["required"] + ds.get("optional", [])) - set(ed)):
        errs.append("[data_setup]: editions has no %s (one of %s)" % (p, ", ".join(EDITIONS)))
    for p in sorted(set(ed) - set(ds["required"] + ds.get("optional", []))):
        errs.append("[data_setup]: editions names %s, which is neither required nor optional" % p)
    for p, e in sorted(ed.items()):
        if e not in EDITIONS:
            errs.append("[data_setup]: edition of %s is %r, not one of %s" % (p, e, ", ".join(EDITIONS)))
    req, opt, dl = ds["required"], ds.get("optional", []), ds.get("downloads", [])
    if "hint" in ds and not ds["hint"].strip():
        errs.append("[data_setup]: hint is empty")
    if "original" in ds and not ds["original"].strip():
        errs.append("[data_setup]: original is empty")
    both = sorted(set(req) & set(opt))
    if both:
        errs.append("[data_setup]: %s both required and optional" % ", ".join(both))
    if not req and not opt:
        errs.append("[data_setup]: neither required nor optional names a package")
    if bool(req) != (build.get("needs_data") is True):
        errs.append("[data_setup]: required is empty exactly when needs_data is false (now %s, needs_data %s)"
                    % ("empty" if not req else "set", build.get("needs_data")))
    seen = set()
    for grp in ds.get("alternatives", []):
        if not isinstance(grp, list) or len(grp) < 2 or not all(isinstance(x, str) for x in grp):
            errs.append("[data_setup]: alternatives is a list of groups of two or more package names")
            continue
        for p in grp:
            if p not in req and p not in opt:
                errs.append("[data_setup]: alternatives names %s, which is neither required nor optional" % p)
            if p in seen:
                errs.append("[data_setup]: %s is in two alternatives groups" % p)
            seen.add(p)
    for p in dl:
        if p not in req and p not in opt:
            errs.append("[data_setup]: downloads %s is neither required nor optional" % p)
    for p in ds.get("fetch", []):
        if p not in req and p not in opt:
            errs.append("[data_setup]: fetch %s is neither required nor optional" % p)
        elif ed.get(p) not in ("shareware", "demo"):
            errs.append("[data_setup]: fetch %s is not a shareware or demo edition" % p)
    for p in req + opt:
        if p in build.get("packages", []):
            errs.append("[data_setup]: %s is also in packages (the data never comes from the catalog's install)" % p)
    gdp = ctx.get("gdp")
    if gdp is None:
        errs.append("[data_setup]: no readable %s (make gdp-index)" % GDP_INDEX)
    elif ds["gdp"] not in gdp:
        errs.append("[data_setup]: game-data-packager %s has no game %r" % (gdp[None], ds["gdp"]))
    else:
        for p in req + opt:
            if p not in gdp[ds["gdp"]]:
                errs.append("[data_setup]: %s is not a package of game-data-packager %s's %s"
                            % (p, gdp[None], ds["gdp"]))
    man = ctx.get("manifest")
    if man is not None:
        for p in req + opt:
            if p not in man:
                errs.append("[data_setup]: %s is not in %s (make gdp-manifest)" % (p, GDP_MANIFEST.name))
            elif man[p].get("edition") != ds["editions"].get(p):
                errs.append("[data_setup]: %s's edition in %s is %r, not %r (make gdp-manifest)"
                            % (p, GDP_MANIFEST.name, man[p].get("edition"), ds["editions"].get(p)))
    return errs


def gdp_manifest(path=None):
    """{package: its gdp-manifest.json entry} (gdp-manifest.py); None when the file is missing or broken
    (then the [data_setup] packages are not checked against it)"""
    try:
        return dict(json.loads(Path(path or GDP_MANIFEST).read_text())["packages"])
    except (OSError, ValueError, KeyError, TypeError):
        return None


def control_stanza(control, pkg):
    """{field: value} of debian/control's binary stanza for pkg (continuation lines dropped), or None"""
    for para in re.split(r"\n\s*\n", control):
        f = dict(m.groups() for m in re.finditer(r"(?m)^([A-Za-z0-9-]+):[ \t]*(.*)$", para))
        if f.get("Package") == pkg:
            return f
    return None


def licensed_errors(d, entries, ctx, editions=None):
    """[[licensed]] of a bundle or engine folder d against its licensed/<pkg>/ directories; editions: the
    bundle's [data_setup] editions"""
    errs = []
    editions = editions if isinstance(editions, dict) else {}
    if not isinstance(entries, list) or not all(isinstance(e, dict) for e in entries):
        return ["licensed must be an array of tables ([[licensed]])"]
    declared = {}
    for e in entries:
        for k in sorted(set(e) - LICENSED_KEYS):
            errs.append("[[licensed]] %s: unknown key %r (%s)" % (e.get("name"), k, ", ".join(sorted(LICENSED_KEYS))))
        n, b = e.get("name"), e.get("builder")
        if not isinstance(n, str) or not PKG_RE.match(n):
            errs.append("[[licensed]] name %r is not a package name" % (n,))
            continue
        if n in declared:
            errs.append("[[licensed]] %s is declared twice" % n)
        declared[n] = b
        if not isinstance(b, str) or not BUILDER_RE.match(b):
            errs.append("[[licensed]] %s: builder %r: source, script, retired or game-data-packager:<game>" % (n, b))
            continue
        ed, rel = e.get("edition"), e.get("release")
        if ed is not None and n in editions:
            errs.append("[[licensed]] %s: edition: [data_setup] editions has it already" % n)
        elif ed is not None and ed not in EDITIONS:
            errs.append("[[licensed]] %s: edition %r, not one of %s" % (n, ed, ", ".join(EDITIONS)))
        elif ed is None and n not in editions:
            errs.append("[[licensed]] %s: no edition (its edition key, one of %s)" % (n, ", ".join(EDITIONS)))
        ed = editions.get(n, ed)
        if rel is not None and (not isinstance(rel, str) or not RELEASE_RE.match(rel)):
            errs.append("[[licensed]] %s: release %r is not a release name (%s)" % (n, rel, RELEASE_RE.pattern))
        elif rel is not None and b not in ("source", "script", "retired"):
            errs.append("[[licensed]] %s: release: game-data-packager's comes from the files it matched" % n)
        if "input" in e and (e["input"] is not True or not b.startswith("game-data-packager:")):
            errs.append("[[licensed]] %s: input = true only for a game-data-packager builder" % n)
        lf = e.get("leak_files", [])
        if not isinstance(lf, list) or not all(isinstance(x, str) and x and not x.startswith("/") and " " not in x for x in lf):
            errs.append("[[licensed]] %s: leak_files is a list of relative path patterns" % n)
        ldir = Path(d) / "licensed" / n
        if b.startswith("game-data-packager:") or b == "retired":
            if ldir.exists():
                errs.append("[[licensed]] %s: builder %s has no directory, but licensed/%s/ exists" % (n, b, n))
        elif ctx["has_licensed"]:
            want = ldir / ("package.conf" if b == "source" else n + ".sh")
            if not want.is_file():
                errs.append("[[licensed]] %s: builder %s needs licensed/%s/%s" % (n, b, n, want.name))
            elif b == "script" and "build-info.py fields" not in want.read_text(errors="replace"):
                errs.append("[[licensed]] %s: %s writes no X-Walnut fields (tools/gamedb/build-info.py fields)"
                            % (n, want.name))
            elif b == "source":
                ctl = ldir / "debian" / "control"
                st = control_stanza(ctl.read_text(errors="replace"), n) if ctl.is_file() else None
                if st is None:
                    errs.append("[[licensed]] %s: licensed/%s/debian/control has no Package: %s" % (n, n, n))
                else:
                    for fld, want_v in (("XB-X-Walnut-Edition", ed), ("XB-X-Walnut-Release", rel)):
                        if st.get(fld) != want_v:
                            errs.append("[[licensed]] %s: debian/control %s is %r, build.toml says %r"
                                        % (n, fld, st.get(fld), want_v))
    lic = Path(d) / "licensed"
    for x in sorted(lic.iterdir()) if lic.is_dir() else []:
        if x.name not in declared:
            errs.append("licensed/%s is not declared in build.toml ([[licensed]] name = %r)" % (x.name, x.name))
    return errs


def package_errors(names, what, bad, ctx, licensed_ok=False):
    errs = []
    for p in names:
        if not isinstance(p, str) or not PKG_RE.match(p):
            errs.append("%s: package name %r: lower case letters, digits, + . - and it must end in [a-z0-9]" % (what, p))
        elif p in bad and not licensed_ok:
            errs.append("%s: package %r is licensed (tools/licensed.py): not for the public catalog" % (what, p))
        elif p not in bad and ctx["apt"] is not None and p not in ctx["apt"] and p not in ctx["ours"]:
            errs.append("%s: package %r is neither in the apt index nor built by a debian/control of ours" % (what, p))
    return errs


def build_errors(gdir, build, bad, ctx, draft):
    """the problems of a game's build.toml (gamedb/README.md "build.toml")"""
    errs = []
    ctx = ctx or context(REPO, Path(gdir).parent)
    if not build and not (Path(gdir) / "build.toml").exists():
        return [] if draft else ["no build.toml (a published game needs packages, suite and needs_data)"]
    for k in build:
        if k not in BUILD_KEYS:
            errs.append("build.toml: unknown key %r" % k)
    for k, (t, req) in BUILD_KEYS.items():
        if k not in build:
            if req and not draft:
                errs.append("build.toml: missing key %r" % k)
        elif not isinstance(build[k], t) or (t is not bool and isinstance(build[k], bool)):
            errs.append("build.toml: %r should be %s" % (k, t.__name__))
    if errs:
        return errs
    if "packages" in build and not build["packages"]:
        errs.append("build.toml: packages is empty")
    errs += package_errors(build.get("packages", []), "build.toml packages", bad, ctx)
    errs += package_errors(build.get("data", []), "build.toml data", bad, ctx, licensed_ok=True)
    if "suite" in build and build["suite"] not in SUITES:
        errs.append("build.toml: suite %r: one of %s" % (build["suite"], ", ".join(SUITES)))
    if build.get("suite") == "licensed" and not any(p in bad for p in build.get("data", [])):
        errs.append("build.toml: suite licensed, but no data package is licensed")
    e = build.get("engine")
    if e is not None:
        if e not in ctx["engines"]:
            errs.append("build.toml: engine %r: no gamedb/engines/%s/engine.md" % (e, e))
        elif Path(gdir).name not in ctx["engines"][e]:
            errs.append("build.toml: engine %r, but its engine.md games doesn't list %s" % (e, Path(gdir).name))
    for d in build.get("desktop", []):
        if not isinstance(d, str) or not re.fullmatch(r"[A-Za-z0-9._-]+\.desktop", d):
            errs.append("build.toml: desktop entry %r" % (d,))
    for d in build.get("divert", []):
        if not isinstance(d, str) or not re.fullmatch(r"(/[A-Za-z0-9._+-]+)+", d):
            errs.append("build.toml: divert %r is not an absolute path" % (d,))
    ds = build.get("data_setup")
    errs += ["build.toml: " + x for x in licensed_errors(gdir, build.get("licensed", []), ctx,
                                                          ds.get("editions") if isinstance(ds, dict) else None)]
    if "data_setup" in build:
        errs += ["build.toml: " + x for x in data_setup_errors(build["data_setup"], build, bad, ctx)]
    return errs


def engine_errors(edir, bad, ctx):
    """the problems of an engine's build.toml, and of the games its engine.md lists"""
    errs = []
    try:
        build = read_build(edir)
    except tomllib.TOMLDecodeError as e:
        return ["build.toml: %s" % e]
    for k in build:
        if k not in ENGINE_KEYS:
            errs.append("build.toml: unknown key %r (an engine's: %s)" % (k, ", ".join(ENGINE_KEYS)))
        elif not isinstance(build[k], ENGINE_KEYS[k][0]):
            errs.append("build.toml: %r should be %s" % (k, ENGINE_KEYS[k][0].__name__))
    if errs:
        return errs
    errs += package_errors(build.get("packages", []), "build.toml packages", bad, ctx)
    errs += ["build.toml: " + x for x in licensed_errors(edir, build.get("licensed", []), ctx)]
    if (Path(edir) / "analysis.toml").exists():
        errs += toml_errors(Path(edir) / "analysis.toml", "analysis.toml",
                            lambda a: bench_module("gamedata").validate_analysis(a, engine=True))
    if (Path(edir) / "todo.toml").is_file():
        errs += toml_errors(Path(edir) / "todo.toml", "todo.toml", todo_errors)
    errs += investigation_errors(edir, Path(edir).parent.parent / "games")
    for g in ctx["engines"].get(Path(edir).name, []):
        gdir = Path(edir).parent.parent / "games" / str(g)
        try:
            ge = read_build(gdir).get("engine") if gdir.is_dir() else None
        except tomllib.TOMLDecodeError:
            continue   # the game's own check says so
        if ge != Path(edir).name:
            errs.append("engine.md games lists %s, whose build.toml doesn't say engine = %r" % (g, Path(edir).name))
    return errs


def pack_integration():
    spec = importlib.util.spec_from_file_location("pack_integration", Path(__file__).resolve().parent / "pack-integration.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def requires_errors(name, text, build, ctx):
    """a launcher's X-Walnut-Requires= must be a file of one of the game's [data_setup] packages in the data
    manifest (files or required, no leading /); skipped when none of those is in the manifest"""
    manifest = (ctx or {}).get("manifest")
    ds = build.get("data_setup")
    if not manifest or not isinstance(ds, dict):
        return []
    pkgs = [p for p in list(ds.get("required", [])) + list(ds.get("optional", [])) if p in manifest]
    if not pkgs:
        return []
    known = set()
    for p in pkgs:
        known |= set(manifest[p].get("files", {})) | set(manifest[p].get("required", []))
    errs = []
    for l in text.splitlines():
        if l.startswith("X-Walnut-Requires=") and l.split("=", 1)[1].lstrip("/") not in known:
            errs.append("desktop/launchers/%s: X-Walnut-Requires %s is no file of %s in %s" % (
                name, l.split("=", 1)[1], ", ".join(pkgs), GDP_MANIFEST.name))
    return errs


def desktop_errors(gdir, build=None, ctx=None):
    """desktop/: only DESKTOP_FILES; launchers/*.desktop that start the game through ps3-game (as
    pack-integration.py reads them), files/ holding files only, migrate-home.txt lines it can read"""
    errs, d = [], gdir / "desktop"
    if not d.is_dir():
        return errs
    pi = pack_integration()
    for f in sorted(x.name for x in d.iterdir()):
        if f not in DESKTOP_FILES:
            errs.append("unexpected file desktop/%s (desktop/ holds %s)" % (f, ", ".join(DESKTOP_FILES)))
    for f in sorted((d / "launchers").iterdir()) if (d / "launchers").is_dir() else []:
        try:
            if not f.name.endswith(".desktop") or not f.is_file():
                raise pi.Error("desktop/launchers/%s is not a .desktop file" % f.name)
            pi.launcher(gdir.name, f)
            errs += requires_errors(f.name, f.read_text(), build or {}, ctx)
        except pi.Error as e:
            errs.append(str(e))
    if (d / "migrate-home.txt").is_file():
        try:
            pi.migrate_lines(gdir.name, d / "migrate-home.txt")
        except pi.Error as e:
            errs.append(str(e))
    return errs


def benchmark_errors(gdir, build):
    """benchmark/: a recipe.sh, an analysis.toml in gamedata.py's schema, only BENCH_FILES;
    every `recipes` entry of build.toml ("<recipe>:<variant>") names this game's own recipe and one of its variants"""
    errs, b = [], gdir / "benchmark"
    if b.is_dir():
        if not (b / "recipe.sh").is_file():
            errs.append("benchmark/ has no recipe.sh")
        for f in sorted(x.name for x in b.iterdir()):
            if f not in BENCH_FILES:
                errs.append("unexpected file benchmark/%s (benchmark/ holds %s)" % (f, ", ".join(BENCH_FILES)))
        if (b / "analysis.toml").exists():
            errs += toml_errors(b / "analysis.toml", "benchmark/analysis.toml", bench_module("gamedata").validate_analysis)
    recipe = (b / "recipe.sh").read_text(errors="replace") if (b / "recipe.sh").is_file() else None
    for r in build.get("recipes", []) if isinstance(build.get("recipes"), list) else []:
        name, _, variant = str(r).partition(":")
        if name != gdir.name or recipe is None:
            errs.append("recipes: %r is not this game's recipe (benchmark/recipe.sh, named %s)" % (r, gdir.name))
        elif variant and variant != "base" and not re.search(r"^variant_%s\(\)" % re.escape(variant), recipe, re.M):
            errs.append("recipes: %r: the recipe has no variant %s" % (r, variant))
    return errs


def records_errors(root, games=None):
    """the benchmark records under a gamedb root (bench/analysis/benchrecords.py check_records): benchmarks/batches.toml
    and metrics.toml (closed keys, unique names, run batches in order), every games/<id>/benchmark/results/
    <batch>.toml (closed keys, its batch exists, metric names known, numbers; label, order, note and source
    only in a summary's entries, a source for values the entry has)"""
    if not (Path(root) / "benchmarks").is_dir():
        return []
    return bench_module("benchrecords").check_records(root, games)


def records_warnings(root):
    """warnings, not errors, on the benchmark records (benchrecords.py check_warnings): a batch without its
    hand-written tested or conclusion"""
    if not (Path(root) / "benchmarks").is_dir():
        return []
    return bench_module("benchrecords").check_warnings(root)


CAVEAT_KEYS = {"batch": str, "game": str, "variant": str, "kind": str, "why": str}
CAVEAT_KINDS = ("invalid", "not-comparable")


def caveats_errors(root):
    """gamedb/benchmarks/caveats.toml (gamedb/benchmarks/README.md "Caveats"): [[caveat]] tables of closed keys
    CAVEAT_KEYS, batch, kind and why required, kind one of CAVEAT_KINDS, the batch in batches.toml, a game
    (when named) with results in that batch, a variant (only with a game) in them"""
    f = Path(root) / "benchmarks" / "caveats.toml"
    if not f.is_file():
        return []
    try:
        cav = tomllib.loads(f.read_text())
    except tomllib.TOMLDecodeError as e:
        return ["caveats.toml: %s" % e]
    errs = ["caveats.toml: unknown key %r" % k for k in cav if k != "caveat"]
    try:
        batches = {b.get("name") for b in tomllib.loads((Path(root) / "benchmarks" / "batches.toml").read_text()).get("batch", [])}
    except (OSError, tomllib.TOMLDecodeError):
        batches = set()
    for i, c in enumerate(cav.get("caveat", [])):
        where = "caveats.toml caveat %d" % (i + 1)
        for k, v in c.items():
            if k not in CAVEAT_KEYS:
                errs.append("%s: unknown key %r" % (where, k))
            elif not isinstance(v, CAVEAT_KEYS[k]):
                errs.append("%s: %s must be a %s" % (where, k, CAVEAT_KEYS[k].__name__))
        for k in ("batch", "kind", "why"):
            if k not in c:
                errs.append("%s: %s missing" % (where, k))
        if c.get("kind") is not None and c.get("kind") not in CAVEAT_KINDS:
            errs.append("%s: kind %r is not one of %s" % (where, c["kind"], ", ".join(CAVEAT_KINDS)))
        if c.get("batch") is not None and c["batch"] not in batches:
            errs.append("%s: batch %r is not in batches.toml" % (where, c["batch"]))
        if "variant" in c and "game" not in c:
            errs.append("%s: a variant needs its game" % where)
        if "game" in c and isinstance(c.get("batch"), str):
            res = Path(root) / "games" / str(c["game"]) / "benchmark" / "results" / ("%s.toml" % c["batch"])
            if not res.is_file():
                errs.append("%s: %s has no results in %s" % (where, c["game"], c["batch"]))
            elif "variant" in c and c["variant"] not in tomllib.loads(res.read_text()).get("variant", {}):
                errs.append("%s: %s has no variant %r in %s" % (where, c["game"], c["variant"], c["batch"]))
    return errs


def check(content, repo=REPO, apt=None):
    """{game folder: [problems]} for the whole catalog (empty lists left out), the engines as
    "engines/<e>"; apt: the package names of an apt index (None: not checked)"""
    bad, seen, out = licensed(repo), {}, {}
    ctx = context(repo, content, apt)
    try:
        errs = tags_errors(tomllib.loads(TAGS_FILE.read_text()))
        if not errs and Path(content).resolve() == (REPO / "gamedb" / "games").resolve():
            errs = engine_tags_errors(taxonomy(), Path(content).parent)
    except (OSError, tomllib.TOMLDecodeError) as e:
        errs = ["tags.toml: %s" % e]
    if errs:
        out["tags.toml"] = errs
    edir = Path(content).parent / "engines"
    for e in sorted(edir.iterdir()) if edir.is_dir() else []:
        if e.is_dir():
            errs = engine_errors(e, bad, ctx)
            if errs:
                out["engines/" + e.name] = errs
    errs = records_errors(Path(content).parent, {d.name for d in Path(content).iterdir() if d.is_dir()})
    errs += caveats_errors(Path(content).parent)
    if errs:
        out["benchmarks"] = errs
    for gdir in sorted(Path(content).iterdir()):
        if not gdir.is_dir():
            continue
        errs = check_game(gdir, bad, repo, ctx)
        try:
            gid = front_matter(gdir / "index.md").get("id")
            if gid in seen:
                errs.append("id %r is also used by %s" % (gid, seen[gid]))
            seen[gid] = gdir.name
        except (ValueError, OSError, tomllib.TOMLDecodeError):
            pass
        if errs:
            out[gdir.name] = errs
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--content", default=str(CONTENT))
    ap.add_argument("--apt-index", metavar="FILE|URL", action="append",
                    help="a Packages file (plain, .gz, .xz) or its URL: build.toml packages must be in one of them or ours")
    a = ap.parse_args()
    apt = set().union(*(apt_index(x) for x in a.apt_index)) if a.apt_index else None
    problems = check(a.content, apt=apt)
    for g, errs in problems.items():
        for e in errs:
            print("%s: %s" % (g, e), file=sys.stderr)
    warns = records_warnings(Path(a.content).parent)
    for w in warns:
        print("benchmarks: warning: %s" % w, file=sys.stderr)
    n = len([d for d in Path(a.content).iterdir() if d.is_dir()])
    print("check.py: %d game(s), %d with problems%s" % (n, len(problems), ", %d warning(s)" % len(warns) if warns else ""))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
