"""games.py: where a game's benchmark is, for the Python tools (bench/games.sh is the same for the shell;
gamedb/README.md "games/<id>/benchmark/").

A game's benchmark is a directory holding recipe.sh: gamedb/games/<id>/benchmark/ in the repository,
games/<id>/ in the bench tree make bench-push copies to the PS3. BENCH_GAMES names another root
(<id>/recipe.sh or <id>/benchmark/recipe.sh).

Names: a game is named by its gamedb id. The recipes that the move into gamedb renamed (step 1c,
2026-10-26: sauerbraten.sh became bananabread's, sauerbraten-edition.sh sauerbraten's) list their old
file names in their analysis.toml ([legacy] recipe_names). A run from before the move (bench.txt without
recipe=, or no bench.txt at all) names its game by the old file, so it is read through those names
first: game=sauerbraten in such a run is BananaBread. Any other name is an id first.
"""
import os
import re
from pathlib import Path

try:
    import tomllib
except ImportError:  # Python < 3.11: only [legacy] recipe_names is read, by a pattern
    tomllib = None

HERE = Path(__file__).resolve().parent


def root():
    """the directory of the game directories"""
    if os.environ.get("BENCH_GAMES"):
        return Path(os.environ["BENCH_GAMES"])
    if (HERE / "games").is_dir():
        return HERE / "games"
    return HERE.parent / "gamedb" / "games"


def dirs():
    """{id: the game's benchmark directory} for every game with a recipe"""
    r, out = root(), {}
    for pat in ("*/recipe.sh", "*/benchmark/recipe.sh"):
        for f in r.glob(pat):
            d = f.parent
            out[(d.parent if d.name == "benchmark" else d).name] = d
    return dict(sorted(out.items()))


def _analysis(d):
    f = d / "analysis.toml"
    if not f.exists():
        return {}
    text = f.read_text()
    if tomllib:
        return tomllib.loads(text)
    m = re.search(r"^recipe_names\s*=\s*\[(.*?)\]", text, re.M | re.S)
    return {"legacy": {"recipe_names": re.findall(r'"([^"]+)"', m[1])}} if m else {}


def legacy():
    """{old recipe name: id} from every benchmark's analysis.toml [legacy] recipe_names"""
    out = {}
    for g, d in dirs().items():
        for n in _analysis(d).get("legacy", {}).get("recipe_names", []):
            out[n] = g
    return out


def names():
    """every name a run directory may carry: the ids and the old recipe names"""
    return sorted(set(dirs()) | set(legacy()))


def resolve(name, old=False):
    """a game's id from a name (None when nothing has it); old=True: the name comes from a run made
    before the recipes moved into gamedb, so the old recipe names come first"""
    if not name:
        return None
    ids, leg = dirs(), legacy()
    if old and name in leg:
        return leg[name]
    if name in ids:
        return name
    return leg.get(name)


def run_game(bench):
    """the id of a run's game from its bench.txt (a dict): recipe= in runs since the move, else game=
    read as an old name"""
    if bench.get("recipe"):
        return resolve(bench["recipe"])
    return resolve(bench.get("game"), old=True)


def recipe(name, old=False):
    """the recipe file of the game a name resolves to, or None"""
    g = resolve(name, old)
    return dirs()[g] / "recipe.sh" if g else None
