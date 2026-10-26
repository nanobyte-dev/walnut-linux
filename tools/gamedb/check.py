#!/usr/bin/env python3
"""check.py [--content DIR]: validate the Walnut Games catalog (gamedb/games) before it is
built or published (tools/gamedb/build.sh runs it first).

Design
- One folder per game (gamedb/README.md): <id>/index.md (TOML front matter between +++ lines), thumb.png,
  and rating/sessions/<name>/result.toml (+ the screenshots it lists), one folder per played session or
  run. The checks:
    * id: ^[a-z0-9][a-z0-9-]{0,63}$ and equal to the folder name; unique
    * required fields present and of the right type, no unknown field (a typo would silently vanish)
    * categories from the fixed list below, 1 to 3 of them
    * packages: Debian package names (they must end in [a-z0-9]); none of them, and no patched_by
      source, licensed (tools/licensed.py names: those go to the licensed suite only, never into the
      public catalog). Offline: the declarations are read, Sections aren't checked
    * a game that needs the user's own data has a data_guide URL, data_steps (the numbered steps of
      getting and setting up the data: sentences, with `code` in backticks that must pair up) and
      offers no data package
    * thumb.png is a PNG; every test has a parsable result.toml of schema 1 whose listed screenshots
      exist, and no stray file sits in a session folder or the game's folder (GAME_FILES: index.md, thumb.png,
      rating/, packages/, licensed/, upstream.md, upstream/, benchmark/: BENCH_FILES); its rules, when given, are "<data>.<formula>" of a
      data version score.py knows, and a data-version-2 test's defects name known categories
    * patched_by names source packages of ours (tools/pkg-dir.sh, and patches/series)
    * drafts (draft = true: a candidate, or a game tried and not published) need only id, title and
      status (one of STATUSES; status_note says why); summary, license, homepage, categories, tags,
      packages, needs_data, controller and thumb.png may be missing, and nothing is made up for them;
      whatever a draft has is checked like a published game's. catalog.py (and so pack-catalog.py)
      and Hugo leave drafts out
- Prints every problem with the game it belongs to, exits 1 if there is any.
"""
import argparse
import importlib.util
import re
import sys
import tomllib
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
    "tags": (list, True), "license": (str, True), "homepage": (str, True), "packages": (list, True),
    "desktop": (list, False), "recipes": (list, False), "needs_data": (bool, True), "data_guide": (str, False),
    "data_steps": (list, False),
    "controller": (str, True), "fps_cap": ((int, float), False), "medal_note": (str, False),
    "known_issues": (list, False), "patched_by": (list, False), "designed_for": (str, False),
    "draft": (bool, False), "status": (str, False), "status_note": (str, False),
}
# a draft (draft = true: a candidate or a game we tried, not published: catalog.py, pack-catalog.py and
# Hugo leave it out) needs only these; whatever else it has is checked as usual, nothing is filled in
DRAFT_REQUIRED = {"id", "title", "status"}
STATUSES = ["candidate", "tried", "blocked", "dropped", "impossible", "tested"]
TEST_FILES = {"result.toml"}
# what a game's folder may hold so far (gamedb/README.md; the later migration steps add theirs): the page,
# the thumbnail, the sessions, our source packages, the licensed data packages and builders, upstream offers
GAME_DIRS = ("rating", "packages", "licensed", "upstream", "benchmark")
# what benchmark/ may hold so far: the recipe (code), its analysis data, its helpers and files, the sources of
# built files (assets.mk), superseded tools (steps 5 and 6 add results/ and runs/)
BENCH_FILES = ("recipe.sh", "analysis.toml", "lib", "assets", "assets-src", "legacy")
GAME_FILES = ("index.md", "thumb.png", "upstream.md") + GAME_DIRS
# where a game's sessions (each one's result.toml and screenshots) live, relative to its folder
SESSIONS = "rating/sessions"
CONTENT = REPO / "gamedb" / "games"


def licensed(repo):
    """the licensed package names (tools/licensed.py, the one answer; it fails closed: an unreadable
    declaration raises)"""
    spec = importlib.util.spec_from_file_location("licensed", Path(__file__).resolve().parent.parent / "licensed.py")
    lic = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lic)
    return lic.names(repo)


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


def check_game(gdir, bad, repo):
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
    if not fm.get("packages") and not draft:
        errs.append("packages is empty")
    for p in fm.get("packages", []) + fm.get("patched_by", []):
        if not isinstance(p, str) or not PKG_RE.match(p):
            errs.append("package name %r: lower case letters, digits, + . - and it must end in [a-z0-9]" % (p,))
        elif p in bad:
            errs.append("package %r is licensed (tools/licensed.py): not for the public catalog" % p)
    for p in fm.get("patched_by", []):
        if isinstance(p, str) and not score.package_dir(repo, p):
            errs.append("patched_by %r: no package %s in the repository (tools/pkg-dir.sh)" % (p, p))
    for d in fm.get("desktop", []):
        if not isinstance(d, str) or not re.fullmatch(r"[A-Za-z0-9._-]+\.desktop", d):
            errs.append("desktop entry %r" % (d,))
    if fm.get("designed_for", "pad") not in score.DESIGNED_FOR:
        errs.append("designed_for %r: one of %s (default pad)" % (fm["designed_for"], ", ".join(score.DESIGNED_FOR)))
    if "controller" in fm and fm["controller"] not in CONTROLLERS:
        errs.append("controller %r: one of %s" % (fm["controller"], ", ".join(CONTROLLERS)))
    if fm.get("needs_data") and not URL_RE.match(fm.get("data_guide", "")):
        errs.append("needs_data = true needs a data_guide URL (the data itself is never offered)")
    steps = fm.get("data_steps", [])
    if fm.get("needs_data") and not steps:
        errs.append("needs_data = true needs data_steps (how to get the data and where it goes)")
    for st in steps:
        if not isinstance(st, str) or not st.strip():
            errs.append("data_steps: each step is a non-empty string")
        elif st.count("`") % 2:
            errs.append("data_steps %r: the backticks around code do not pair up" % st[:40])
    if steps and not fm.get("needs_data"):
        errs.append("data_steps without needs_data = true")
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
    errs += benchmark_errors(gdir, fm)
    rating = gdir / "rating"
    for f in sorted(x.name for x in rating.iterdir()) if rating.is_dir() else []:
        if f != "sessions":
            errs.append("unexpected file rating/%s (rating/ holds sessions/)" % f)
    return errs


def benchmark_errors(gdir, fm):
    """benchmark/: a recipe.sh, an analysis.toml that parses ([legacy] recipe_names: names), only BENCH_FILES;
    every `recipes` entry of index.md ("<recipe>:<variant>") names this game's own recipe and one of its variants"""
    errs, b = [], gdir / "benchmark"
    if b.is_dir():
        if not (b / "recipe.sh").is_file():
            errs.append("benchmark/ has no recipe.sh")
        for f in sorted(x.name for x in b.iterdir()):
            if f not in BENCH_FILES:
                errs.append("unexpected file benchmark/%s (benchmark/ holds %s)" % (f, ", ".join(BENCH_FILES)))
        if (b / "analysis.toml").exists():
            try:
                names = tomllib.loads((b / "analysis.toml").read_text()).get("legacy", {}).get("recipe_names", [])
                if not isinstance(names, list) or not all(isinstance(n, str) and n for n in names):
                    errs.append("benchmark/analysis.toml: [legacy] recipe_names is a list of names")
            except tomllib.TOMLDecodeError as e:
                errs.append("benchmark/analysis.toml: %s" % e)
    recipe = (b / "recipe.sh").read_text(errors="replace") if (b / "recipe.sh").is_file() else None
    for r in fm.get("recipes", []):
        name, _, variant = str(r).partition(":")
        if name != gdir.name or recipe is None:
            errs.append("recipes: %r is not this game's recipe (benchmark/recipe.sh, named %s)" % (r, gdir.name))
        elif variant and variant != "base" and not re.search(r"^variant_%s\(\)" % re.escape(variant), recipe, re.M):
            errs.append("recipes: %r: the recipe has no variant %s" % (r, variant))
    return errs


def check(content, repo=REPO):
    """{game folder: [problems]} for the whole catalog (empty lists left out)"""
    bad, seen, out = licensed(repo), {}, {}
    for gdir in sorted(Path(content).iterdir()):
        if not gdir.is_dir():
            continue
        errs = check_game(gdir, bad, repo)
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
    a = ap.parse_args()
    problems = check(a.content)
    for g, errs in problems.items():
        for e in errs:
            print("%s: %s" % (g, e), file=sys.stderr)
    n = len([d for d in Path(a.content).iterdir() if d.is_dir()])
    print("check.py: %d game(s), %d with problems" % (n, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
