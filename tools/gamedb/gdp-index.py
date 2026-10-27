#!/usr/bin/env python3
"""gdp-index.py --image IMAGE [--deb DEB] [--out FILE]: write tools/gamedb/gdp-packages.json, the games
and package names our game-data-packager knows, so tools/gamedb/check.py can check build.toml [data_setup]
offline (no docker, no network at test time).

Design
- Runs game-data-packager's own loader (game_data_packager.game.load_games, the data in its vfs.zip)
  inside IMAGE, an image with game-data-packager's dependencies installed: the games builder (`make
  gdp-index` passes $(GAMES_BUILDER_TAG), build-containers/Dockerfile.games, which has Debian's gdp).
  The gdp that answers is ours (packages/patched/tools/game-data-packager, the one the PS3 runs):
  DEB, by default the one build/debs/game-data-packager/game-data-packager_*_all.deb (make
  game-data-packager-deb), is installed over Debian's with dpkg -i inside the throwaway container
  first. `docker run --rm --network none`, the repository not mounted, the deb read only.
- The file: {"gdp_version": "<its version>", "games": {"<game>": ["<package>", ...]}}, sorted, one game
  per line, so a regeneration after a game-data-packager change diffs as the games that changed.
- Regenerate it when our game-data-packager changes (gamedb/README.md "[data_setup]") and commit it;
  check.py names the version it checked against.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = HERE / "gdp-packages.json"
DEBS = REPO / "build" / "debs" / "game-data-packager"
INSIDE = r'''
import json, sys
sys.path.insert(0, "/usr/share/games/game-data-packager")
from game_data_packager.game import load_games
from game_data_packager.version import GAME_PACKAGE_VERSION
games = load_games()
json.dump({"gdp_version": str(GAME_PACKAGE_VERSION),
           "games": {n: sorted(g.packages) for n, g in games.items()}}, sys.stdout)
'''


def built_deb(debs=DEBS):
    """the one game-data-packager_*_all.deb in DEBS (a rebuild empties it); [] or all of them otherwise"""
    found = sorted(Path(debs).glob("game-data-packager_*_all.deb"))
    return found[0] if len(found) == 1 else found


def run_gdp_python(image, deb, code, *args):
    """runs python3 -I -c CODE ARGS in a throwaway IMAGE container with DEB (our game-data-packager)
    installed over the image's: --network none, the deb mounted read only. CompletedProcess (text)."""
    deb = Path(deb).resolve()
    return subprocess.run(
        ["docker", "run", "--rm", "--network", "none", "--user", "0", "-v", "%s:/gdp/%s:ro" % (deb, deb.name),
         "--entrypoint", "sh", image, "-ec",
         'dpkg -i "/gdp/$1" >/dev/null 2>&1 || { dpkg -i "/gdp/$1" >&2; exit 1; }; shift; exec python3 -I -c "$0" "$@"',
         code, deb.name, *args], capture_output=True, text=True)


def our_deb(arg, prog):
    """--deb or the one built; prints why and returns None when there is none or more than one"""
    deb = Path(arg) if arg else built_deb()
    if isinstance(deb, list):
        print("%s: %s game-data-packager_*_all.deb in %s: make game-data-packager-deb, or pass --deb" % (
            prog, "more than one" if deb else "no", DEBS), file=sys.stderr)
        return None
    if not deb.is_file():
        print("%s: no %s" % (prog, deb), file=sys.stderr)
        return None
    return deb


def render(data):
    """the committed text: the version, then one game per line"""
    games = data["games"]
    lines = ['  %s: %s' % (json.dumps(n), json.dumps(sorted(games[n]))) for n in sorted(games)]
    return '{\n "gdp_version": %s,\n "games": {\n%s\n }\n}\n' % (
        json.dumps(data["gdp_version"]), ",\n".join(" " + l for l in lines))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", required=True, help="a docker image with game-data-packager's dependencies")
    ap.add_argument("--deb", help="our game-data-packager_*_all.deb (default: the one in %s)" % DEBS)
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args(argv)
    deb = our_deb(a.deb, "gdp-index.py")
    if deb is None:
        return 2
    print("gdp-index: %s in %s" % (deb.name, a.image))
    p = run_gdp_python(a.image, deb, INSIDE)
    if p.returncode:
        print("gdp-index.py: docker run failed (%d): %s" % (p.returncode, p.stderr.strip()[-500:]), file=sys.stderr)
        return 2
    data = json.loads(p.stdout)
    if not data.get("games"):
        print("gdp-index.py: game-data-packager listed no games", file=sys.stderr)
        return 2
    Path(a.out).write_text(render(data))
    print("gdp-index: game-data-packager %s, %d games, %d packages -> %s" % (
        data["gdp_version"], len(data["games"]), sum(len(v) for v in data["games"].values()), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
