#!/usr/bin/env python3
"""build-info.py CMD: what the build needs to know from gamedb's build.toml files (gamedb/README.md
"build.toml"), for the Makefile and the tools that used to keep game lists of their own.

  build-info.py game-data          "<game>:<package>" for every [[licensed]] with builder
                                   game-data-packager:<game> (make game-data, tools/game-data.sh),
                                   "<game>:<package>:input" for one made from the owner's copy (input = true)
  build-info.py scripts            the [[licensed]] names make licensed-data builds: builder = "script",
                                   and game-data-packager ones with input = true
  build-info.py script PKG         the builder script of one: gamedb/<games|engines>/<id>/licensed/PKG/PKG.sh
  build-info.py builder PKG        the builder of a [[licensed]] package (source, script, retired,
                                   game-data-packager:<game>)
  build-info.py fields PKG         the control fields our builders give a licensed data package:
                                   "X-Walnut-Edition: <e>" ([data_setup] editions, else the [[licensed]]
                                   edition) and "X-Walnut-Release: <r>" (the [[licensed]] release, if any)
  build-info.py bench-data DIR     DIR/<id>/data.txt for every game with data packages: "<pkg> <edition>
                                   [<release>]" a line ([data_setup] required and optional, data, [[licensed]]
                                   names), for the session harness's versions file (make bench-push)
  build-info.py needs-licensed     the packages of every game with suite = "licensed" (its data is licensed):
                                   the public image never installs them (tools/image-packages.sh)
  build-info.py image-base         the packages of every game with image_base = true (the image's package
                                   layer: the Makefile's ROOTFS_GAMES, tools/build-rootfs.sh)
  build-info.py own-packages ID    the source packages of ours in game ID's bundle: packages/<pkg>/ and
                                   licensed/<pkg>/ with a package.conf (make game-debs GAME=ID)
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


def data_fields(root, pkg):
    """{"X-Walnut-Edition": e, "X-Walnut-Release": r or None} of a licensed data package: the edition from
    its game's [data_setup] editions, else from its [[licensed]] entry; the release from that entry (a
    source or script build's fixed release; game-data-packager's comes from the files it matched)"""
    ed = rel = None
    for _, b in builds(root):
        ds = b.get("data_setup") or {}
        ed = ed or (ds.get("editions") or {}).get(pkg)
        for e in b.get("licensed", []):
            if e.get("name") == pkg:
                ed = ed or e.get("edition")
                rel = rel or e.get("release")
    if not ed:
        raise Error("%s: no edition in gamedb ([data_setup] editions or its [[licensed]] edition)" % pkg)
    return {"X-Walnut-Edition": ed, "X-Walnut-Release": rel}


def game_data(b):
    """[(package, edition, release)] of a game's data packages, as bench-data writes them: [data_setup]
    required and optional, then data and the [[licensed]] names (the licensed suite's builds of the same
    data); an edition from [data_setup] editions or the [[licensed]] entry, else "-"; the release from it"""
    ds = b.get("data_setup") or {}
    lic = {e.get("name"): e for e in b.get("licensed", []) if isinstance(e, dict)}
    names = list(ds.get("required", [])) + list(ds.get("optional", [])) + list(b.get("data", [])) + list(lic)
    out = []
    for p in dict.fromkeys(n for n in names if isinstance(n, str)):
        e = lic.get(p, {})
        out.append((p, (ds.get("editions") or {}).get(p) or e.get("edition") or "-", e.get("release")))
    return out


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
    sub.add_parser("builder").add_argument("pkg")
    sub.add_parser("fields").add_argument("pkg")
    sub.add_parser("bench-data").add_argument("dir")
    sub.add_parser("needs-licensed")
    sub.add_parser("image-base")
    sub.add_parser("own-packages").add_argument("id")
    sub.add_parser("data").add_argument("id")
    sub.add_parser("packages").add_argument("id")
    a = ap.parse_args(argv)
    root = Path(a.root)
    try:
        if a.cmd == "game-data":
            for _, e in licensed(root):
                b = str(e.get("builder", ""))
                if b.startswith("game-data-packager:"):
                    print("%s:%s%s" % (b.split(":", 1)[1], e["name"], ":input" if e.get("input") is True else ""))
        elif a.cmd == "scripts":
            for _, e in licensed(root):
                if e.get("builder") == "script" or (str(e.get("builder", "")).startswith("game-data-packager:")
                                                    and e.get("input") is True):
                    print(e["name"])
        elif a.cmd == "builder":
            hits = [e.get("builder") for _, e in licensed(root) if e.get("name") == a.pkg]
            if not hits:
                raise Error("%s: no build.toml [[licensed]] names it" % a.pkg)
            print(hits[0])
        elif a.cmd == "fields":
            for k, v in data_fields(root, a.pkg).items():
                if v:
                    print("%s: %s" % (k, v))
        elif a.cmd == "bench-data":
            for d, b in builds(root):
                rows = game_data(b) if d.parent.name == "games" else []
                if rows:
                    (Path(a.dir) / d.name).mkdir(parents=True, exist_ok=True)
                    (Path(a.dir) / d.name / "data.txt").write_text(
                        "# the game's data packages: package, edition, release (tools/gamedb/build-info.py bench-data)\n"
                        + "".join(" ".join(x for x in r if x) + "\n" for r in rows))
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
        elif a.cmd == "image-base":
            for d, b in builds(root):
                if d.parent.name == "games" and b.get("image_base") is True:
                    print("\n".join(b.get("packages", [])))
        elif a.cmd == "own-packages":
            game(root, a.id)
            gdir = Path(root) / "games" / a.id
            print(" ".join(sorted(p.parent.name for k in ("packages", "licensed")
                                  for p in (gdir / k).glob("*/package.conf"))))
        elif a.cmd in ("data", "packages"):
            print(" ".join(game(root, a.id).get(a.cmd, [])))
    except Error as e:
        print("build-info.py: %s" % e, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
