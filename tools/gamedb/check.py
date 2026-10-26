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
    * categories from the fixed list below, 1 to 3 of them
    * build.toml (BUILD_KEYS, a closed set; gamedb/README.md): packages are Debian package names (they
      must end in [a-z0-9]), none of them licensed (tools/licensed.py names: those go to the licensed
      suite only, never into the public catalog), each one built by a debian/control of ours or, with
      --apt-index (a Packages file or URL: the Debian snapshot's), in that index; engine names an
      engines/<e>/ whose engine.md lists the game (and every game engine.md lists says so); suite one
      of SUITES (licensed: a licensed data package); meta only names packages of its own; [[licensed]]
      entries (name, builder, leak_files, meta)
      match the bundle's licensed/<pkg>/ directories (builder source: a package.conf, script: <pkg>.sh;
      game-data-packager:<game> has none), every licensed/ directory declared; a checkout without any
      licensed directory (the public copy) skips that direction. A published game has packages,
      suite and needs_data. The catalog's patched_by is not a key: our source packages of the bundle
      (packages/) and of its engine (patched_by() below)
    * a game that needs the user's own data (build.toml needs_data) has a data_guide URL and data_steps
      in index.md (the numbered steps of getting and setting up the data: sentences, with `code` in
      backticks that must pair up)
    * thumb.png is a PNG; every test has a parsable result.toml of schema 1 whose listed screenshots
      exist, and no stray file sits in a session folder or the game's folder (GAME_FILES: index.md, thumb.png,
      build.toml, rating/, packages/, licensed/, upstream.md, upstream/, benchmark/: BENCH_FILES,
      desktop/: DESKTOP_FILES, its launchers and migrate-home.txt as tools/gamedb/pack-integration.py reads them); its rules, when given, are "<data>.<formula>" of a
      data version score.py knows, and a data-version-2 test's defects name known categories
    * rating/ holds sessions/ and the played test, rating/test.toml: bench/playdef.py's schema (closed keys,
      process a process name, home relative paths inside ~ that no other game shares, the marks compile,
      command an absolute program, env NAME=value); benchmark/analysis.toml and engines/<e>/analysis.toml
      follow bench/gamedata.py's (closed keys: [legacy], [reload], [[log_ignore]], [[log_strip]],
      [interference]; an engine's only the log rules; every regex compiles)
    * drafts (draft = true: a candidate, or a game tried and not published) need only id, title and
      status (one of STATUSES; status_note says why); summary, license, homepage, categories, tags,
      controller, thumb.png and build.toml (or any of its keys) may be missing, and nothing is made up for them;
      whatever a draft has is checked like a published game's. catalog.py (and so pack-catalog.py)
      and Hugo leave drafts out
- Prints every problem with the game it belongs to, exits 1 if there is any.
"""
import argparse
import gzip
import importlib.util
import lzma
import re
import sys
import tomllib
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import score   # noqa: E402
CATEGORIES = ["action", "adventure", "arcade", "board-card", "platformer", "puzzle", "racing", "rpg",
              "sandbox", "shooter", "simulation", "sports", "strategy"]
CONTROLLERS = ["full", "partial", "keyboard", "unknown"]
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]{0,63}\Z")
PKG_RE = re.compile(r"[a-z0-9][a-z0-9+.-]*[a-z0-9]\Z")
TAG_RE = re.compile(r"[a-z0-9][a-z0-9 -]{0,30}\Z")
URL_RE = re.compile(r"https?://[^\s]+\Z")
# field: (type, required)
FIELDS = {
    "id": (str, True), "title": (str, True), "summary": (str, True), "categories": (list, True),
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
    "recipes": (list, False), "licensed": (list, False), "meta": (list, False),
}
# an engine's build.toml: the packages the games run it from, its licensed data
ENGINE_KEYS = {"packages": (list, False), "licensed": (list, False)}
LICENSED_KEYS = {"name", "builder", "leak_files", "meta"}
SUITES = ["free", "licensed", "none"]
BUILDER_RE = re.compile(r"(source|script|game-data-packager:[a-z0-9][a-z0-9-]*)\Z")
# a draft (draft = true: a candidate or a game we tried, not published: catalog.py, pack-catalog.py and
# Hugo leave it out) needs only these; whatever else it has is checked as usual, nothing is filled in
DRAFT_REQUIRED = {"id", "title", "status"}
STATUSES = ["candidate", "tried", "blocked", "dropped", "impossible", "tested"]
TEST_FILES = {"result.toml"}
# what a game's folder may hold so far (gamedb/README.md; the later migration steps add theirs): the page,
# the thumbnail, the sessions, our source packages, the licensed data packages and builders, upstream offers
GAME_DIRS = ("rating", "packages", "licensed", "upstream", "benchmark", "desktop", "investigations")
# what desktop/ may hold: our launchers, the config files we install (by absolute path), the migrate-home lines
DESKTOP_FILES = ("launchers", "files", "migrate-home.txt")
# what benchmark/ may hold so far: the recipe (code), its analysis data, its helpers and files, the sources of
# built files (assets.mk), superseded tools (steps 5 and 6 add results/ and runs/)
BENCH_FILES = ("recipe.sh", "analysis.toml", "lib", "assets", "assets-src", "legacy")
GAME_FILES = ("index.md", "thumb.png", "build.toml", "upstream.md", "technical.md", "todo.toml") + GAME_DIRS
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
    recipes) and patched_by, as catalog.py reads a game"""
    fm = front_matter(Path(gdir) / "index.md")
    b = read_build(gdir)
    fm.update({k: b[k] for k in ("packages", "needs_data", "desktop", "recipes") if k in b})
    fm["patched_by"] = patched_by(gdir, b)
    return fm


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
    """a module of the harness (bench/: playdef.py's and gamedata.py's schemas)"""
    spec = importlib.util.spec_from_file_location(name, REPO / "bench" / (name + ".py"))
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
        if k not in FIELDS and k not in ("title", "description", "date"):
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
    if "categories" in fm and (not 1 <= len(fm["categories"]) <= 3 or any(c not in CATEGORIES for c in fm["categories"])):
        errs.append("categories %r: one to three of %s" % (fm["categories"], ", ".join(CATEGORIES)))
    for t in fm.get("tags", []):
        if not isinstance(t, str) or not TAG_RE.match(t):
            errs.append("bad tag %r (lower case letters, digits, space, dash)" % (t,))
    if "homepage" in fm and not URL_RE.match(fm["homepage"]):
        errs.append("homepage is not an http(s) URL")
    try:
        build = read_build(gdir)
    except tomllib.TOMLDecodeError as e:
        return errs + ["build.toml: %s" % e]
    errs += build_errors(gdir, build, bad, ctx, draft)
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
        if not draft:
            errs.append("no thumb.png")
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
        listed = set(r.get("shots", {}).get("ps3", [])) | set(r.get("shots", {}).get("pc", []))
        for f in sorted(listed):
            if not (t / f).exists():
                errs.append(SESSIONS + "/%s: screenshot %s is listed but missing" % (t.name, f))
        for f in sorted(x.name for x in t.iterdir()):
            if f not in listed and f not in TEST_FILES:
                errs.append(SESSIONS + "/%s: unexpected file %s" % (t.name, f))
    for f in sorted(x.name for x in gdir.iterdir()):
        if f not in GAME_FILES:
            errs.append("unexpected file %s (a game folder holds %s)" % (f, ", ".join(GAME_FILES)))
        elif f in GAME_DIRS and not (gdir / f).is_dir():
            errs.append("%s is not a folder" % f)
    errs += benchmark_errors(gdir, build)
    errs += desktop_errors(gdir)
    rating = gdir / "rating"
    for f in sorted(x.name for x in rating.iterdir()) if rating.is_dir() else []:
        if f not in ("sessions", "test.toml"):
            errs.append("unexpected file rating/%s (rating/ holds sessions/ and test.toml)" % f)
    if (rating / "test.toml").is_file():
        errs += toml_errors(rating / "test.toml", "rating/test.toml", bench_module("playdef").validate)
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
    return {"ours": control_binaries(repo), "apt": apt, "engines": engines,
            "has_licensed": bool(lic.licensed_dirs(lic.Tree(repo))) or any(Path(content).glob("*/licensed/*"))
            or any(edir.glob("*/licensed/*"))}


def licensed_errors(d, entries, ctx):
    """[[licensed]] of a bundle or engine folder d against its licensed/<pkg>/ directories"""
    errs = []
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
            errs.append("[[licensed]] %s: builder %r: source, script or game-data-packager:<game>" % (n, b))
            continue
        if not isinstance(e.get("meta", False), bool):
            errs.append("[[licensed]] %s: meta is true or false (in ps3-games-licensed or not)" % n)
        lf = e.get("leak_files", [])
        if not isinstance(lf, list) or not all(isinstance(x, str) and x and not x.startswith("/") and " " not in x for x in lf):
            errs.append("[[licensed]] %s: leak_files is a list of relative path patterns" % n)
        ldir = Path(d) / "licensed" / n
        if b.startswith("game-data-packager:"):
            if ldir.exists():
                errs.append("[[licensed]] %s: game-data-packager makes it, but licensed/%s/ exists" % (n, n))
        elif ctx["has_licensed"]:
            want = ldir / ("package.conf" if b == "source" else n + ".sh")
            if not want.is_file():
                errs.append("[[licensed]] %s: builder %s needs licensed/%s/%s" % (n, b, n, want.name))
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
    bad_meta = [m for m in build.get("meta", []) if m not in build.get("packages", [])]
    if bad_meta:
        errs.append("build.toml: meta %r: the metapackages name only the game's own packages" % (bad_meta,))
    for d in build.get("divert", []):
        if not isinstance(d, str) or not re.fullmatch(r"(/[A-Za-z0-9._+-]+)+", d):
            errs.append("build.toml: divert %r is not an absolute path" % (d,))
    errs += ["build.toml: " + x for x in licensed_errors(gdir, build.get("licensed", []), ctx)]
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


def desktop_errors(gdir):
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


def check(content, repo=REPO, apt=None):
    """{game folder: [problems]} for the whole catalog (empty lists left out), the engines as
    "engines/<e>"; apt: the package names of an apt index (None: not checked)"""
    bad, seen, out = licensed(repo), {}, {}
    ctx = context(repo, content, apt)
    edir = Path(content).parent / "engines"
    for e in sorted(edir.iterdir()) if edir.is_dir() else []:
        if e.is_dir():
            errs = engine_errors(e, bad, ctx)
            if errs:
                out["engines/" + e.name] = errs
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
    n = len([d for d in Path(a.content).iterdir() if d.is_dir()])
    print("check.py: %d game(s), %d with problems" % (n, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
