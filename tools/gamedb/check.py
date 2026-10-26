#!/usr/bin/env python3
"""check.py [--content DIR]: validate the Walnut Games catalog (gamedb/content/games) before it is
built or published (tools/gamedb/build.sh runs it first).

Design
- One folder per game: <id>/index.md (TOML front matter between +++ lines), thumb.png, and
  tests/<name>/result.toml (+ the screenshots it lists). The checks:
    * id: ^[a-z0-9][a-z0-9-]{0,63}$ and equal to the folder name; unique
    * required fields present and of the right type, no unknown field (a typo would silently vanish)
    * categories from the fixed list below, 1 to 3 of them
    * packages: Debian package names (they must end in [a-z0-9]); none of them, and no patched_by
      source, in tools/licensed-packages.txt (those go to ps3-licensed only, never into the public
      catalog). Offline: only that file is read, Sections aren't checked
    * a game that needs the user's own data has a data_guide URL and offers no data package
    * thumb.png is a PNG; every test has a parsable result.toml of schema 1 whose listed screenshots
      exist, and no stray file sits in a test folder; its rules, when given, are "<data>.<formula>" of a
      data version score.py knows, and a data-version-2 test's defects name known categories
    * patched_by names directories of ours with a package.conf (and patches/series)
- Prints every problem with the game it belongs to, exits 1 if there is any.
"""
import argparse
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
    "controller": (str, True), "fps_cap": ((int, float), False), "medal_note": (str, False),
    "known_issues": (list, False), "patched_by": (list, False), "designed_for": (str, False),
}
TEST_FILES = {"result.toml"}


def licensed(repo):
    out = set()
    for line in (repo / "tools" / "licensed-packages.txt").read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.add(line)
    return out


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
        if k not in FIELDS and k not in ("title", "description", "date", "draft"):
            errs.append("unknown field %r" % k)
    for k, (t, req) in FIELDS.items():
        if k not in fm:
            if req:
                errs.append("missing field %r" % k)
        elif not isinstance(fm[k], t) or (t is not bool and isinstance(fm[k], bool)):
            errs.append("field %r should be %s" % (k, t.__name__ if isinstance(t, type) else "a number"))
    if any(not e.startswith("unknown") for e in errs):
        return errs   # the later checks need the fields
    if not ID_RE.match(fm["id"]) or fm["id"] != gdir.name:
        errs.append("id %r must match ^[a-z0-9][a-z0-9-]{0,63}$ and the folder name %r" % (fm["id"], gdir.name))
    if not 1 <= len(fm["categories"]) <= 3 or any(c not in CATEGORIES for c in fm["categories"]):
        errs.append("categories %r: one to three of %s" % (fm["categories"], ", ".join(CATEGORIES)))
    for t in fm["tags"]:
        if not isinstance(t, str) or not TAG_RE.match(t):
            errs.append("bad tag %r (lower case letters, digits, space, dash)" % (t,))
    if not URL_RE.match(fm["homepage"]):
        errs.append("homepage is not an http(s) URL")
    if not fm["packages"]:
        errs.append("packages is empty")
    for p in fm["packages"] + fm.get("patched_by", []):
        if not isinstance(p, str) or not PKG_RE.match(p):
            errs.append("package name %r: lower case letters, digits, + . - and it must end in [a-z0-9]" % (p,))
        elif p in bad:
            errs.append("package %r is licensed (tools/licensed-packages.txt): not for the public catalog" % p)
    for p in fm.get("patched_by", []):
        if not (repo / p / "package.conf").exists():
            errs.append("patched_by %r: no %s/package.conf in the repository" % (p, p))
    for d in fm.get("desktop", []):
        if not isinstance(d, str) or not re.fullmatch(r"[A-Za-z0-9._-]+\.desktop", d):
            errs.append("desktop entry %r" % (d,))
    if fm.get("designed_for", "pad") not in score.DESIGNED_FOR:
        errs.append("designed_for %r: one of %s (default pad)" % (fm["designed_for"], ", ".join(score.DESIGNED_FOR)))
    if fm["controller"] not in CONTROLLERS:
        errs.append("controller %r: one of %s" % (fm["controller"], ", ".join(CONTROLLERS)))
    if fm["needs_data"] and not URL_RE.match(fm.get("data_guide", "")):
        errs.append("needs_data = true needs a data_guide URL (the data itself is never offered)")
    if fm.get("fps_cap", 1) <= 0:
        errs.append("fps_cap must be positive")
    thumb = gdir / "thumb.png"
    if not thumb.exists():
        errs.append("no thumb.png")
    elif thumb.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        errs.append("thumb.png is not a PNG")
    tests = gdir / "tests"
    for t in sorted(tests.iterdir()) if tests.is_dir() else []:
        if not (t / "result.toml").exists():
            errs.append("tests/%s: no result.toml" % t.name)
            continue
        try:
            r = tomllib.loads((t / "result.toml").read_text())
        except tomllib.TOMLDecodeError as e:
            errs.append("tests/%s/result.toml: %s" % (t.name, e))
            continue
        if r.get("schema") != 1 or r.get("kind") not in ("session", "demo", "capture", "script") or r.get("game") != fm["id"]:
            errs.append("tests/%s/result.toml: schema 1, a kind and this game's id are required" % t.name)
        rules = str(r.get("rules", "1.0"))
        if not re.fullmatch(r"\d+\.\d+", rules) or score.data_version(r) not in score.FORMULA:
            errs.append("tests/%s/result.toml: rules %r: \"<data>.<formula>\" of data version %s" % (
                t.name, rules, " or ".join(map(str, score.FORMULA))))
        elif score.data_version(r) >= 2:
            bad_defects = [x for x in r.get("review", {}).get("defects", [])
                           if len(score.parse_defects([x])) != 1 or not score.parse_defects([x])[0][1]]
            if bad_defects:
                errs.append("tests/%s/result.toml: defects %r: \"<category> <pc|ps3>: <what>\"" % (t.name, bad_defects))
        listed = set(r.get("shots", {}).get("ps3", [])) | set(r.get("shots", {}).get("pc", []))
        for f in sorted(listed):
            if not (t / f).exists():
                errs.append("tests/%s: screenshot %s is listed but missing" % (t.name, f))
        for f in sorted(x.name for x in t.iterdir()):
            if f not in listed and f not in TEST_FILES:
                errs.append("tests/%s: unexpected file %s" % (t.name, f))
    for f in sorted(x.name for x in gdir.iterdir()):
        if f not in ("index.md", "thumb.png", "tests"):
            errs.append("unexpected file %s (a game folder holds index.md, thumb.png and tests/)" % f)
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
    ap.add_argument("--content", default=str(REPO / "gamedb" / "content" / "games"))
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
