#!/usr/bin/env python3
"""gamesblocks.py: the generated blocks of docs/games/ (tools/gamedb/blocks.py registers GENERATORS):
driver-bugs (README.md's "driver and library bugs the games found", from every investigation's
driver_patches) and suite-recipes (SUITE.md's per-game table, from the recipes with SUITE=1). Each takes one
argument, the directory of the file the block is in, for its relative links."""
import posixpath
import re
from pathlib import Path

FM_RE = re.compile(r"\+\+\+\n(.*?)\n\+\+\+\n?", re.S)


def _front(path):
    """(TOML front matter, body) of a gamedb markdown file"""
    import tomllib
    text = Path(path).read_text()
    m = FM_RE.match(text)
    return (tomllib.loads(m[1]), text[m.end():]) if m else ({}, text)


def _title(gdir):
    """a bundle's title (index.md) or an engine's (engine.md), else its id"""
    for f in ("index.md", "engine.md"):
        if (Path(gdir) / f).is_file():
            return _front(Path(gdir) / f)[0].get("title") or Path(gdir).name
    return Path(gdir).name


def _link(base, rel):
    """the repository path rel as a link from the repository directory base"""
    return posixpath.relpath(rel, base)


def gen_driver_bugs(repo, args):
    """docs/games/README.md's "driver bugs the games found": every investigation of a game or engine
    (gamedb/*/*/investigations/*.md) whose front matter names driver_patches, oldest first: its title (a link
    from the directory args[0], the file's own), the games it names, the driver patches that fixed it"""
    if len(args) != 1:
        raise ValueError("driver-bugs takes one argument: the directory of the file it is in")
    repo, base = Path(repo), args[0]
    rows = []
    for f in sorted((repo / "gamedb").glob("*/*/investigations/*.md")):
        fm, body = _front(f)
        if not fm.get("driver_patches"):
            continue
        m = re.search(r"^# +(.+?)\s*$", body, re.M)
        title = m[1] if m else fm.get("id", f.stem)
        games = []
        for g in fm.get("games") or [f.parent.parent.name]:
            d = repo / "gamedb" / "games" / g
            games.append(_title(d) if d.is_dir() else g)
        rows.append((f.name[:10], title, "| [%s](%s) | %s | %s |" % (
            title.replace("|", "\\|"), _link(base, f.relative_to(repo).as_posix()), ", ".join(games),
            ", ".join(str(x).replace("/", " ", 1) for x in fm["driver_patches"]))))
    out = ["| Bug (the investigation) | Found by | Fixed by |", "|---|---|---|"]
    return "\n".join(out + [r for _, _, r in sorted(rows)]) + "\n"


def _recipe_vars(text):
    """a recipe's top-level NAME=value lines (the first of each name)"""
    out = {}
    for line in text.split("\n"):
        m = re.match(r"([A-Z_]+)=(.*)$", line)
        if m and m[1] not in out:
            out[m[1]] = m[2].strip()
    return out


def gen_suite_recipes(repo, args):
    """docs/games/SUITE.md's per-game table: every recipe with SUITE=1 (gamedb/games/*/benchmark/recipe.sh):
    its command when the recipe sets it on one line, how a run ends, what the result is read from, its
    variants (a link to the recipe); links from the directory args[0]"""
    if len(args) != 1:
        raise ValueError("suite-recipes takes one argument: the directory of the file it is in")
    repo, base = Path(repo), args[0]
    out = ["| Game | Command | Ends | Result read from | Variants |", "|---|---|---|---|---|"]
    for f in sorted((repo / "gamedb" / "games").glob("*/benchmark/recipe.sh")):
        text = f.read_text(errors="replace")
        v = _recipe_vars(text)
        if v.get("SUITE") != "1":
            continue
        gdir = f.parent.parent
        page = gdir / ("technical.md" if (gdir / "technical.md").is_file() else "index.md")
        cmd = v.get("CMD", "")
        cmd = "`%s`" % cmd[1:-1].replace("|", "\\|") if cmd.startswith("(") and cmd.endswith(")") else "set in `setup()`"
        if v.get("PASSES") == "1":
            ends = "passes (the game repeats its demo)"
        elif v.get("WINDOW_STOP") == "1":
            ends = "the window (`WINDOW_STOP`)"
        elif "END_RE" in v:
            ends = "its own last line (`END_RE`)"
        else:
            ends = "the game quits"
        if "STEADY_RE" in v:
            res = "the game's steady line (`STEADY_RE`)"
        elif "FPS_RE" in v and v.get("WINDOW_STOP") != "1":
            res = "the game's own line (`FPS_RE`)"
        else:
            res = "the Gallium HUD"
        n = len(set(re.findall(r"^variant_([A-Za-z0-9_]+)\s*\(\)", text, re.M)) - {"base"}) + 1
        out.append("| [%s](%s) | %s | %s | %s | [%d](%s) |" % (
            _title(gdir), _link(base, page.relative_to(repo).as_posix()), cmd, ends, res, n,
            _link(base, f.relative_to(repo).as_posix())))
    return "\n".join(out) + "\n"


GENERATORS = {
    "driver-bugs": gen_driver_bugs,
    "suite-recipes": gen_suite_recipes,
}
