#!/usr/bin/env python3
"""build-info.py CMD: what the build needs to know from gamedb's build.toml files (gamedb/README.md
"build.toml"), for the Makefile and the tools that used to keep game lists of their own.

  build-info.py game-data          "<game>:<package>" for every [[licensed]] with builder
                                   game-data-packager:<game> (make game-data, tools/game-data.sh)
  build-info.py scripts            the [[licensed]] names with builder = "script" (make licensed-data)
  build-info.py script PKG         the builder script of one: gamedb/<games|engines>/<id>/licensed/PKG/PKG.sh
  build-info.py needs-licensed     the packages of every game with suite = "licensed" (in ps3-games, but
                                   not in the public ps3-games-free or the public image: tools/image-packages.sh)
  build-info.py data ID            game ID's data packages (tools/play-pc.sh)
  build-info.py packages ID        game ID's packages (tools/play-pc.sh)

Reads gamedb/games/*/build.toml and gamedb/engines/*/build.toml. Fails closed: a build.toml that
doesn't parse, or a game that has none, is exit 2 with a message, never an empty answer that looks like
"nothing". tools/gamedb/check.py validates the files (closed key set, types); this only reads them.
"""
import argparse
import sys
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
GAMEDB = REPO / "gamedb"


class Error(Exception):
    pass


def builds(root=GAMEDB):
    """[(bundle dir, parsed build.toml)] of every game and engine, in path order"""
    out = []
    for kind in ("games", "engines"):
        for f in sorted((Path(root) / kind).glob("*/build.toml")):
            try:
                out.append((f.parent, tomllib.loads(f.read_text())))
            except (OSError, tomllib.TOMLDecodeError) as e:
                raise Error("%s: %s" % (f.relative_to(root.parent), e))
    return out


def licensed(root=GAMEDB):
    """[(bundle dir, [[licensed]] entry)]"""
    return [(d, e) for d, b in builds(root) for e in b.get("licensed", [])]


def game(root, gid):
    f = Path(root) / "games" / gid / "build.toml"
    if not f.is_file():
        raise Error("no gamedb/games/%s/build.toml" % gid)
    try:
        return tomllib.loads(f.read_text())
    except tomllib.TOMLDecodeError as e:
        raise Error("gamedb/games/%s/build.toml: %s" % (gid, e))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(GAMEDB), help=argparse.SUPPRESS)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("game-data")
    sub.add_parser("scripts")
    sub.add_parser("script").add_argument("pkg")
    sub.add_parser("needs-licensed")
    sub.add_parser("data").add_argument("id")
    sub.add_parser("packages").add_argument("id")
    a = ap.parse_args(argv)
    root = Path(a.root)
    try:
        if a.cmd == "game-data":
            for _, e in licensed(root):
                b = str(e.get("builder", ""))
                if b.startswith("game-data-packager:"):
                    print("%s:%s" % (b.split(":", 1)[1], e["name"]))
        elif a.cmd == "scripts":
            for _, e in licensed(root):
                if e.get("builder") == "script":
                    print(e["name"])
        elif a.cmd == "script":
            hits = [d for d, e in licensed(root) if e.get("name") == a.pkg and e.get("builder") == "script"]
            if not hits:
                raise Error("%s: no build.toml [[licensed]] with builder = \"script\" names it" % a.pkg)
            s = hits[0] / "licensed" / a.pkg / (a.pkg + ".sh")
            if not s.is_file():
                raise Error("%s: no %s (a checkout without the licensed directories?)" % (a.pkg, s.relative_to(root.parent)))
            print(s.relative_to(root.parent))
        elif a.cmd == "needs-licensed":
            for _, b in builds(root):
                if b.get("suite") == "licensed":
                    print("\n".join(b.get("packages", [])))
        elif a.cmd in ("data", "packages"):
            print(" ".join(game(root, a.id).get(a.cmd, [])))
    except Error as e:
        print("build-info.py: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
