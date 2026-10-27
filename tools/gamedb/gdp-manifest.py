#!/usr/bin/env python3
"""gdp-manifest.py --image IMAGE [--out FILE]: write tools/gamedb/gdp-manifest.json, the files of every
GameDB [data_setup] package as game-data-packager builds them, for walnut-games-helper import-data
(packed into walnut-games as /usr/share/walnut-games/data-manifest.json, root-owned).

Design
- The packages: every required and optional name of a [data_setup] in gamedb/games/*/build.toml (drafts
  too). Read here, on the host; the names go into IMAGE as one argument.
- Runs game-data-packager's own loader inside IMAGE (the games builder, `make gdp-manifest` passes
  $(GAMES_BUILDER_TAG); `docker run --rm --network none`, read only), as gdp-index.py does: the game's
  file data (load_file_data), each package's install and optional files with their alternatives,
  placed the way its build places them (install_to and install_as through the deb packaging's
  substitutions: $assets is /usr/share/games, $pkgdocdir /usr/share/doc/<package>).
- Per package: game (gdp's), version (the deb's Version: gdp's, or the package's own version
  followed by +<gdp version>), edition (the [data_setup] editions entry: X-Walnut-Edition), files
  {path relative to /: [alternatives, each {size, the strongest checksum gdp has: sha256, else sha1,
  else md5, and release: gdp's name for that version of the file when it has one, the part after
  "?" in "doom.wad?1.9ud" (X-Walnut-Release)}]}, required (the paths of its install files; the
  rest are optional), conflicts and replaces (the deb's own, from gdp's control generation, kept only
  when they name another package of the same gdp game: quake-registered and quake-shareware; the
  freedoom and chocolate-doom ones and every Provides are left out), launchers (true for the Doom
  family, whose builds add .desktop files, pixmaps and icons the helper skips), symlinks ({} in v1:
  a package with symlinks stops the generator, as does an executable file, a file without size or
  checksum, two different files on one path, or a file where the helper writes its own (doc/<pkg>/copyright,
  changelog*)).
- The committed text: the version, then each package with one file per line, sorted, so a
  regeneration after a game-data-packager upgrade diffs as the files that changed. Regenerate when
  the image's game-data-packager changes or a [data_setup] names a new package (gamedb/README.md
  "[data_setup]"); check.py says when a [data_setup] package is missing from it.
"""
import argparse
import json
import subprocess
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = HERE / "gdp-manifest.json"
INSIDE = r'''
import json, os, re, sys, tempfile
sys.path.insert(0, "/usr/share/games/game-data-packager")
from game_data_packager.game import load_games
from game_data_packager.packaging.deb import DebPackaging
from game_data_packager.version import GAME_PACKAGE_VERSION
want, editions = json.loads(sys.argv[1])
games = load_games()
deb = DebPackaging()
out, errs = {}, []

def names(field):
    return {m.group(1) for part in (field or "").split(",") for alt in part.split("|")
            if (m := re.match(r"\s*([a-z0-9][a-z0-9+.-]*)", alt))}

def strongest(f):
    for k in ("sha256", "sha1", "md5"):
        v = getattr(f, k)
        if v:
            return k, v
    return None, None

for name in want:
    owners = [(gn, g) for gn, g in sorted(games.items()) if name in g.packages]
    if len(owners) != 1:
        errs.append("%s: %d games have it" % (name, len(owners)))
        continue
    gname, g = owners[0]
    g.load_file_data()
    p = g.packages[name]
    install_to = deb.substitute(p.install_to, name).strip("/")
    files, required = {}, set()
    for tag, wanted in (("required", p.install_files), ("optional", p.optional_files)):
        for w in sorted(wanted, key=lambda w: w.name):
            if w.install_as == "$alternative":
                errs.append("%s: %s installs as $alternative" % (name, w.name))
                continue
            to = install_to if w.install_to is None else deb.substitute(w.install_to, name, install_to=install_to)
            path = os.path.join(to.strip("/"), w.install_as).strip("/")
            alts = []
            for a in ([g.files[x] for x in w.alternatives] or [w]):
                k, v = strongest(a)
                if a.size is None or k is None:
                    errs.append("%s: %s (%s) has no size or checksum" % (name, path, a.name))
                    continue
                if a.executable or w.executable:
                    errs.append("%s: %s is executable" % (name, path))
                alt = {"size": a.size, k: v}
                # gdp's name for one accepted version of a file: "doom.wad?1.9ud", "Data/ch.dat?cd"
                rel = a.name.partition("?")[2]
                if rel:
                    if not re.match(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,39}\Z", rel):
                        errs.append("%s: %s has an odd version name %r" % (name, path, rel))
                    alt["release"] = rel
                alts.append(alt)
            alts = sorted(alts, key=lambda a: (a["size"], json.dumps(a, sort_keys=True)))
            if files.get(path, alts) != alts:   # one file both required and optional is fine (gdp: required)
                errs.append("%s: two files on %s" % (name, path))
            files[path] = alts
            if tag == "required":
                required.add(path)
    doc = "usr/share/doc/%s/" % name
    for path in files:
        if path.startswith(doc) and (path == doc + "copyright" or path[len(doc):].startswith("changelog")):
            errs.append("%s: %s is where the helper writes its own file" % (name, path))
    if p.symlinks:
        errs.append("%s: has symlinks %r (not in v1)" % (name, p.symlinks))
    with tempfile.TemporaryDirectory() as d:
        control = deb._DebPackaging__generate_control(g, p, d, p.component)
    same = set(g.packages) - {name}
    out[name] = {"game": gname, "version": control["Version"], "files": files, "required": sorted(required),
                 "edition": editions[name],
                 "conflicts": sorted(names(control.get("Conflicts")) & same),
                 "replaces": sorted(names(control.get("Replaces")) & same),
                 "launchers": type(g).__module__.endswith("doom_common"), "symlinks": {}}
json.dump({"gdp_version": str(GAME_PACKAGE_VERSION), "packages": out, "errors": errs}, sys.stdout)
'''


def data_setup_packages(games_dir=None):
    """{package: its edition} for the required and optional names of every gamedb/games/*/build.toml
    [data_setup] (its editions table; check.py makes sure each has one); None for a missing one"""
    names = {}
    for b in sorted(Path(games_dir or REPO / "gamedb" / "games").glob("*/build.toml")):
        ds = tomllib.loads(b.read_text()).get("data_setup")
        if isinstance(ds, dict):
            for p in ds.get("required", []) + ds.get("optional", []):
                names[p] = (ds.get("editions") or {}).get(p)
    return dict(sorted(names.items()))


def render(data):
    """the committed text: the version, then per package its fields and one file per line"""
    blocks = []
    for name in sorted(data["packages"]):
        p = data["packages"][name]
        head = ['   %s: %s' % (json.dumps(k), json.dumps(p[k], sort_keys=True)) for k in sorted(p) if k != "files"]
        files = ['    %s: %s' % (json.dumps(f), json.dumps(p["files"][f], sort_keys=True)) for f in sorted(p["files"])]
        blocks.append('  %s: {\n%s,\n   "files": {\n%s\n   }\n  }' % (
            json.dumps(name), ",\n".join(head), ",\n".join(files)))
    return '{\n "gdp_version": %s,\n "packages": {\n%s\n }\n}\n' % (json.dumps(data["gdp_version"]), ",\n".join(blocks))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--image", required=True, help="a docker image with game-data-packager installed")
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args(argv)
    want = data_setup_packages()
    if None in want.values():
        print("gdp-manifest.py: no [data_setup] editions entry for %s" % sorted(p for p, e in want.items() if e is None),
              file=sys.stderr)
        return 2
    p = subprocess.run(["docker", "run", "--rm", "--network", "none", "--entrypoint", "python3", a.image,
                        "-I", "-c", INSIDE, json.dumps([sorted(want), want])], capture_output=True, text=True)
    if p.returncode:
        print("gdp-manifest.py: docker run failed (%d): %s" % (p.returncode, p.stderr.strip()[-500:]), file=sys.stderr)
        return 2
    data = json.loads(p.stdout)
    if data.pop("errors"):
        for e in json.loads(p.stdout)["errors"]:
            print("gdp-manifest.py: " + e, file=sys.stderr)
        return 2
    if sorted(data["packages"]) != sorted(want):
        print("gdp-manifest.py: game-data-packager has no %s" % sorted(set(want) - set(data["packages"])), file=sys.stderr)
        return 2
    Path(a.out).write_text(render(data))
    print("gdp-manifest: game-data-packager %s, %d packages, %d files -> %s" % (
        data["gdp_version"], len(data["packages"]), sum(len(v["files"]) for v in data["packages"].values()), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
