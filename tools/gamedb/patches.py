#!/usr/bin/env python3
"""patches.py [--list]: the patch register (gamedb/patches/<slug>/patch.md) against the patch series
under packages/ (the plan's Phase 3 "Register"; docs/GAMEDB.md).

What it reads: every `series` file under packages/ (the quilt series of the kernel, Mesa and the
patched libraries and tools), every directory under packages/ that holds *.patch files but no series
(the SPU toolchain's: applied in name order), and the front matter of every gamedb/patches/*/patch.md.
What it checks (each an error, exit 1):
  - a series lists only files that exist, the folder holds no patch its series doesn't list, and the
    series ends in a newline;
  - every patch of every series is claimed by exactly one register entry, and every path an entry
    lists is a patch of its component's series (paths relative to the package folder, as
    "patches/0046-....patch", "patches/local/0004-....patch", "packaging/0001-....patch");
  - the front matter parses, `component` is known and `category` one of CATEGORIES; a Mesa entry has
    `gating`, a kernel-ps3 entry `authors`; only ps3rsx (our own driver, no series) has no patches;
  - `tests` names suites of tools/run-tests.sh (PS3 tests are written "ps3:<program>") and `batches`
    names batches of gamedb/benchmarks/batches.toml;
  - a slug is lower-case words joined by "-", never a patch number (no word like 0046).
Unknown front matter keys are warnings (gamedb/schemas/patch.toml is the schema).
Output: the counts per category per component, then "patches: ok" or the errors. --list prints every
entry with its component, category and patch count.
Stdlib only. Not an input of walnut-games (its VERSION_PATHS), so register work doesn't rebuild it.
"""
import argparse
import re
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
PACKAGES = REPO / "packages"
REGISTER = REPO / "gamedb" / "patches"

CATEGORIES = ("stability", "correctness", "performance", "feature", "port", "build", "security",
              "diagnostics", "config")
KEYS = ("component", "patches", "aliases", "category", "switch", "default", "gating", "authors",
        "forwarded", "tests", "batches")
# component: (package folder under packages/, the path prefixes of its patches in that folder or None for all)
COMPONENTS = {
    "kernel": ("patched/kernel/linux", ("patches/local/", "packaging/")),
    "kernel-ps3": ("patched/kernel/linux", ("patches/ps3/",)),
    "mesa": ("patched/graphics/mesa", None),
    "sdl3": ("patched/graphics/libsdl3", None),
    "sdl2-compat": ("patched/graphics/sdl2-compat", None),
    "sdl12-compat": ("patched/graphics/sdl12-compat", None),
    "libpng": ("patched/graphics/libpng1.6", None),
    "webkit2gtk": ("patched/desktop/webkit2gtk", None),
    "badwolf": ("patched/desktop/badwolf", None),
    "plymouth": ("patched/desktop/plymouth", None),
    "pnmixer": ("patched/desktop/pnmixer", None),
    "fluidsynth": ("patched/audio/fluidsynth", None),
    "libsfml": ("patched/audio/libsfml", None),
    "libvorbis": ("patched/audio/libvorbis", None),
    "openal-soft": ("patched/audio/openal-soft", None),
    "boost-context": ("patched/libs/boost-context", None),
    "luajit": ("patched/libs/luajit", None),
    "game-data-packager": ("patched/tools/game-data-packager", None),
    "spu-binutils": ("patched/toolchain/spu/binutils", None),
    "spu-gcc": ("patched/toolchain/spu/gcc", None),
    "spu-newlib": ("patched/toolchain/spu/newlib-4.6", None),
    "ps3rsx": (None, None),	# our own driver: entries name its commits, no series
}
SLUG_RE = re.compile(r"^[a-z][a-z0-9]*(-[a-z0-9]+)*$")
FM_RE = re.compile(r"\A\+\+\+\n(.*?)\n\+\+\+\n", re.S)


def series_entries(path):
    """The patch names a series file lists, and whether it ends in a newline."""
    text = path.read_text()
    names = [ln.split()[0] for ln in text.splitlines() if ln.strip() and not ln.lstrip().startswith("#")]
    return names, text == "" or text.endswith("\n")


def read_series(errors):
    """{(package folder, path in it): series file} for every patch of every series under packages/."""
    patches = {}
    dirs_with_series = set()
    for s in sorted(PACKAGES.rglob("series")):
        if not s.is_file():
            continue
        d = s.parent
        dirs_with_series.add(d)
        pkg = d.parent	# <pkg>/patches/series, <pkg>/packaging/series
        names, newline = series_entries(s)
        rel_s = s.relative_to(REPO)
        if not newline:
            errors.append(f"{rel_s}: doesn't end in a newline")
        listed = set()
        for n in names:
            f = d / n
            listed.add(f.resolve())
            if not f.is_file():
                errors.append(f"{rel_s}: lists {n}, which doesn't exist")
            patches[(pkg, f.relative_to(pkg).as_posix())] = rel_s
        for f in sorted(d.rglob("*.patch")):
            if f.resolve() not in listed:
                errors.append(f"{f.relative_to(REPO)}: not in its series ({rel_s})")
    # folders with patches and no series (the SPU toolchain): every *.patch, in name order
    for f in sorted(PACKAGES.rglob("*.patch")):
        if any(p in dirs_with_series for p in f.parents):
            continue
        patches[(f.parent, f.name)] = f.parent.relative_to(REPO)
    return patches


def read_register(errors, warnings, suites, batches):
    entries = {}
    if not REGISTER.is_dir():
        return entries
    for d in sorted(p for p in REGISTER.iterdir() if p.is_dir()):
        slug = d.name
        f = d / "patch.md"
        rel = f.relative_to(REPO)
        if not f.is_file():
            errors.append(f"{d.relative_to(REPO)}: no patch.md")
            continue
        if not SLUG_RE.match(slug) or any(re.fullmatch(r"0\d{3}", w) for w in slug.split("-")):
            errors.append(f"{rel}: slug {slug!r} isn't lower-case words joined by '-' (and never a patch number)")
        m = FM_RE.match(f.read_text())
        if not m:
            errors.append(f"{rel}: no +++ front matter")
            continue
        try:
            fm = tomllib.loads(m.group(1))
        except tomllib.TOMLDecodeError as e:
            errors.append(f"{rel}: front matter: {e}")
            continue
        for k in fm:
            if k not in KEYS:
                warnings.append(f"{rel}: unknown key {k!r}")
        comp = fm.get("component")
        if comp not in COMPONENTS:
            errors.append(f"{rel}: component {comp!r} unknown (one of {', '.join(COMPONENTS)})")
            continue
        if fm.get("category") not in CATEGORIES:
            errors.append(f"{rel}: category {fm.get('category')!r} not one of {', '.join(CATEGORIES)}")
        pl = fm.get("patches")
        if not isinstance(pl, list) or not all(isinstance(p, str) for p in pl):
            errors.append(f"{rel}: patches must be a list of paths")
            pl = []
        if comp == "ps3rsx" and pl:
            errors.append(f"{rel}: ps3rsx has no patch series (patches = [], the commits in the text)")
        if comp != "ps3rsx" and not pl:
            errors.append(f"{rel}: no patches listed")
        if comp == "mesa" and not fm.get("gating"):
            errors.append(f"{rel}: a Mesa entry needs gating (is_ps3 | switch NAME | ps3rsx-only | none: why)")
        if comp == "kernel-ps3" and not fm.get("authors"):
            errors.append(f"{rel}: a kernel-ps3 entry needs authors (credit the OtherOS++ and psx-place authors)")
        for t in fm.get("tests", []):
            if not t.startswith("ps3:") and t not in suites:
                errors.append(f"{rel}: test {t!r} is no suite of tools/run-tests.sh (a PS3 test is 'ps3:<program>')")
        for b in fm.get("batches", []):
            if b not in batches:
                errors.append(f"{rel}: batch {b!r} not in gamedb/benchmarks/batches.toml")
        entries[slug] = (rel, fm)
    return entries


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--list", action="store_true", help="print every entry")
    a = ap.parse_args()
    errors, warnings = [], []
    rt = (REPO / "tools" / "run-tests.sh").read_text()
    suites = set(re.findall(r"^(?:\[[^\]]*\] && )?run ([A-Za-z0-9_-]+) ", rt, re.M))
    batches = {b["name"] for b in tomllib.loads((REPO / "gamedb" / "benchmarks" / "batches.toml").read_text())["batch"]}
    series = read_series(errors)
    entries = read_register(errors, warnings, suites, batches)

    claimed = {}
    for slug, (rel, fm) in entries.items():
        folder, prefixes = COMPONENTS[fm["component"]]
        for p in fm.get("patches", []) if isinstance(fm.get("patches"), list) else []:
            key = (PACKAGES / folder, p) if folder else None
            if key is None or key not in series:
                errors.append(f"{rel}: {p} is not a patch of {fm['component']}'s series (packages/{folder})")
                continue
            if prefixes and not p.startswith(prefixes):
                errors.append(f"{rel}: {p} belongs to another component than {fm['component']}")
            if key in claimed:
                errors.append(f"{rel}: {p} is also claimed by {claimed[key]}")
            else:
                claimed[key] = slug
    for key, s in sorted(series.items(), key=lambda kv: (str(kv[0][0]), kv[0][1])):
        if key not in claimed:
            errors.append(f"packages/{key[0].relative_to(PACKAGES)}/{key[1]}: in {s} but in no register entry")

    counts = {}
    for slug, (rel, fm) in entries.items():
        c = counts.setdefault(fm["component"], {})
        c[fm.get("category")] = c.get(fm.get("category"), 0) + 1
    if a.list:
        for slug, (rel, fm) in sorted(entries.items()):
            print(f"{slug:44} {fm['component']:18} {str(fm.get('category')):12} {len(fm.get('patches', []))}")
        print()
    print(f"{'component':18} " + " ".join(f"{c[:5]:>5}" for c in CATEGORIES) + "  entries")
    for comp in sorted(counts):
        row = counts[comp]
        print(f"{comp:18} " + " ".join(f"{row.get(c, 0) or '':>5}" for c in CATEGORIES) + f"  {sum(row.values()):7}")
    print(f"{len(entries)} entries, {len(claimed)} of {len(series)} series patches claimed")
    for w in warnings:
        print("warning: " + w)
    for e in errors:
        print("error: " + e)
    print("patches: ok" if not errors else f"patches: {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
