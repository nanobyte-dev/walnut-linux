#!/usr/bin/env python3
"""gdp-index.py --image IMAGE [--out FILE]: write tools/gamedb/gdp-packages.json, the games and package
names Debian's game-data-packager knows, so tools/gamedb/check.py can check build.toml [data_setup]
offline (no docker, no network at test time).

Design
- Runs game-data-packager's own loader (game_data_packager.game.load_games, the data in its vfs.zip)
  inside IMAGE, an image with game-data-packager installed: the games builder (`make gdp-index` passes
  $(GAMES_BUILDER_TAG), build-containers/Dockerfile.games). `docker run --rm --network none`, read only.
- The file: {"gdp_version": "<its version>", "games": {"<game>": ["<package>", ...]}}, sorted, one game
  per line, so a regeneration after a game-data-packager upgrade diffs as the games that changed.
- Regenerate it when the image's game-data-packager changes (gamedb/README.md "[data_setup]") and
  commit it; check.py names the version it checked against.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "gdp-packages.json"
INSIDE = r'''
import json, sys
sys.path.insert(0, "/usr/share/games/game-data-packager")
from game_data_packager.game import load_games
from game_data_packager.version import GAME_PACKAGE_VERSION
games = load_games()
json.dump({"gdp_version": str(GAME_PACKAGE_VERSION),
           "games": {n: sorted(g.packages) for n, g in games.items()}}, sys.stdout)
'''


def render(data):
    """the committed text: the version, then one game per line"""
    games = data["games"]
    lines = ['  %s: %s' % (json.dumps(n), json.dumps(sorted(games[n]))) for n in sorted(games)]
    return '{\n "gdp_version": %s,\n "games": {\n%s\n }\n}\n' % (
        json.dumps(data["gdp_version"]), ",\n".join(" " + l for l in lines))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", required=True, help="a docker image with game-data-packager installed")
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args(argv)
    p = subprocess.run(["docker", "run", "--rm", "--network", "none", "--entrypoint", "python3", a.image,
                        "-I", "-c", INSIDE], capture_output=True, text=True)
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
