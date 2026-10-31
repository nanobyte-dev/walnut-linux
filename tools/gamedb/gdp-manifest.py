#!/usr/bin/env python3
"""gdp-manifest.py --image IMAGE [--deb DEB] [--out FILE]: write tools/gamedb/gdp-manifest.json, the files of every
GameDB [data_setup] package as game-data-packager builds them, for walnut-games-helper import-data
(packed into walnut-games as /usr/share/walnut-games/data-manifest.json, root-owned).

Design
- The packages: every required and optional name of a [data_setup] in gamedb/games/*/build.toml (drafts
  too). Read here, on the host; the names go into IMAGE as one argument.
- Runs our game-data-packager's own loader inside IMAGE (the games builder, `make gdp-manifest` passes
  $(GAMES_BUILDER_TAG); DEB, by default the one in build/debs/game-data-packager, installed over the
  image's Debian gdp first; `docker run --rm --network none`), as gdp-index.py does: the game's
  file data (load_file_data), each package's install and optional files with their alternatives,
  placed the way its build places them (install_to and install_as through the deb packaging's
  substitutions: $assets is /usr/share/games, $pkgdocdir /usr/share/doc/<package>).
- Per package: game (gdp's), version (the deb's Version: gdp's, or the package's own version
  followed by +<gdp version>), edition (the [data_setup] editions entry: X-Walnut-Edition), files
  {path relative to /: [alternatives, each {size, the strongest checksum gdp has: sha256, else sha1,
  else md5, and release: gdp's name for that version of the file when it has one, the part after
  "?" in "doom.wad?1.9ud" (X-Walnut-Release)}]}, required (the paths of its install files; the
  rest are optional; an install file gdp puts under the name of whichever alternative it found,
  install_as $alternative (Day of the Tentacle's monster.sou or monster.sof), is each alternative at
  its own path, optional, since any one will do; a file the game's plugin writes itself, its generated_files(package) {path under
  install_to: bytes} (ttd's orig_win_fm.obm), is a required file with its size and sha256), conflicts and replaces (the deb's own, from gdp's control generation, kept only
  when they name another package of the same gdp game without a version: quake-registered and
  quake-shareware, not Team Arena's quake3-data (<< 77~), which is about older gdp builds; the
  freedoom and chocolate-doom ones and every Provides are left out), launchers (true for the Doom
  family, whose builds add .desktop files, pixmaps and icons the helper skips), symlinks ({link
  path: its relative target}, gdp's own links placed as its build places them: Quake III's
  /usr/lib/quake3/base/baseq3/pak*.pk3; a link pointing out of its top directory or sitting where a
  file or another link goes stops the generator, as does an executable file, a file without size or
  checksum, two different files on one path, or a file where the helper writes its own (doc/<pkg>/copyright,
  changelog*)), archives (the installers and archives gdp knows that hold its files, through any archive in
  between: [{format: gdp's unpacker, name, top: no other file of the game holds it}], for Walnut Games'
  chooser, which names the kinds of file a player can give).
- Music packages (gdp's rip_cd: quake-music, quake2-music): tracks {source: gdp's filename_format, the
  path gdp looks for ripped tracks under in a SOURCE folder (id1/music/track%02d.ogg), first, last (the
  CD track numbers), max_size (TRACK_MAX), paths: the exact installed path of every track}. Their audio
  has no checksum (every rip and encode differs): the helper accepts each listed path once, at most
  max_size bytes, only as an Ogg Vorbis stream, and wants all of them. A rip gdp would install in another
  way (mp3, alternatives, symlinks, not under a music/ directory) stops the generator.
- The committed text: the version, then each package with one file per line, sorted, so a
  regeneration after a game-data-packager upgrade diffs as the files that changed. Regenerate when
  our game-data-packager changes or a [data_setup] names a new package (gamedb/README.md
  "[data_setup]"); check.py says when a [data_setup] package is missing from it.
"""
import argparse
import importlib.util
import json
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = HERE / "gdp-manifest.json"
# the most a music package's Ogg Vorbis track may hold (no checksum: ripped and encoded audio differs from
# copy to copy): ten minutes at about 850 kbit/s, far above oggenc's default quality (~112 kbit/s)
TRACK_MAX = 64 << 20
_spec = importlib.util.spec_from_file_location("gdp_index", HERE / "gdp-index.py")
gdp_index = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gdp_index)
INSIDE = r'''
import hashlib, json, os, re, sys, tempfile
sys.path.insert(0, "/usr/share/games/game-data-packager")
from game_data_packager.game import load_games
from game_data_packager.packaging.deb import DebPackaging
from game_data_packager.version import GAME_PACKAGE_VERSION
want, editions, TRACK_MAX = json.loads(sys.argv[1])
games = load_games()
deb = DebPackaging()
out, errs = {}, []

def names(field):
    # unversioned relations only: a versioned one is about an older build of the other package (Team
    # Arena's Conflicts/Replaces quake3-data (<< 77~), files that moved), never the one the console makes
    return {m.group(1) for part in (field or "").split(",") for alt in part.split("|")
            if (m := re.match(r"\s*([a-z0-9][a-z0-9+.-]*)\s*(?::any\s*)?$", alt))}

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
            to = install_to if w.install_to is None else deb.substitute(w.install_to, name, install_to=install_to)
            if w.install_as == "$alternative":
                # each alternative under its own name (Day of the Tentacle's monster.sou or its FLAC
                # repack monster.sof): every one a file of its own, optional, since any one of them will do
                # and the manifest has no "one of" (gdp itself builds the package only with one of them)
                if not w.alternatives:
                    errs.append("%s: %s installs as $alternative without alternatives" % (name, w.name))
                for x in w.alternatives:
                    a = g.files[x]
                    k, v = strongest(a)
                    apath = os.path.join(to.strip("/"), x.partition("?")[0]).strip("/")
                    if a.size is None or k is None or a.alternatives:
                        errs.append("%s: %s (%s) has no size or checksum" % (name, apath, a.name))
                        continue
                    if a.executable or w.executable:
                        errs.append("%s: %s is executable" % (name, apath))
                    alt = {"size": a.size, k: v}
                    rel = a.name.partition("?")[2]
                    if rel:
                        alt["release"] = rel
                    files.setdefault(apath, [])
                    if alt not in files[apath]:
                        files[apath] = sorted(files[apath] + [alt], key=lambda a: (a["size"], json.dumps(a, sort_keys=True)))
                continue
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
    # files the game's plugin writes into the package itself (ttd: the FM music's orig_win_fm.obm):
    # their exact bytes, so the helper checks them like the game's files
    gen = getattr(g, "generated_files", None)
    for rel, data in sorted((gen(p) if gen else {}).items()):
        path = os.path.join(install_to, rel).strip("/")
        if path in files:
            errs.append("%s: two files on %s" % (name, path))
        files[path] = [{"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}]
        required.add(path)
    doc = "usr/share/doc/%s/" % name
    for path in files:
        if path.startswith(doc) and (path == doc + "copyright" or path[len(doc):].startswith("changelog")):
            errs.append("%s: %s is where the helper writes its own file" % (name, path))
    # gdp's own links (Quake III's pk3s under /usr/lib/quake3), placed as its build places them
    # (build.py fill_dest_dir: both ends substituted, a relative target within one top directory)
    links = {}
    full_to = deb.substitute(p.install_to, name)
    for link, real in sorted(p.symlinks.items()):
        link = deb.substitute(link, name, install_to=full_to).strip("/")
        real = deb.substitute(real, name, install_to=full_to, libdir=deb.get_libdir(p.architecture)).strip("/")
        top = link.split("/", 1)[0]
        if not real.startswith(top + "/"):
            errs.append("%s: its link %s points out of /%s (%s)" % (name, link, top, real))
            continue
        a, b = link.split("/"), real.split("/")
        while a and b and a[0] == b[0]:
            a.pop(0)
            b.pop(0)
        links[link] = "../" * (len(a) - 1) + "/".join(b)
    for link in links:
        if link in files or any(q.startswith(link + "/") for q in list(files) + list(links)):
            errs.append("%s: its link %s is where a file or another link goes" % (name, link))
    tracks = None
    rip = p.rip_cd or {}
    if rip:
        fmt = rip.get("filename_format", "")
        if rip.get("encoding") != "vorbis" or "last_track" not in rip or "alternatives" in rip or "symlinks" in rip \
                or fmt.count("%02d") != 1 or not fmt.endswith(".ogg") or "/music/" not in "/" + fmt:
            errs.append("%s: its CD rip %r is not one the helper takes (vorbis, numbered music/*.ogg)" % (name, rip))
        else:
            first, last = rip.get("first_track", 2), rip["last_track"]
            to = install_to.strip("/")
            tracks = {"source": fmt, "first": first, "last": last, "max_size": TRACK_MAX,
                      "paths": [os.path.join(to, fmt % i) for i in range(first, last + 1)]}
            clash = [t for t in tracks["paths"] if t in files]
            if clash:
                errs.append("%s: %s is both a file and a CD track" % (name, clash[0]))
    # the installers and archives gdp knows that hold the package's files, for Walnut Games' chooser (it
    # names their kinds): gdp's unpacker, the file's name, top when no other file of the game holds it (a
    # file the player may have, not data1.cab inside a disc)
    need = set()
    for w in list(p.install_files) + list(p.optional_files):
        need.add(w.name)
        need.update(w.alternatives or [])
    def provided(f):          # the names of the files an archive holds (gdp's provides_files: WantedFiles)
        return {getattr(x, "name", x) for x in (getattr(f, "provides_files", None) or ())}
    held = {n for f in g.files.values() for n in provided(f)}
    archives, changed = {}, True
    while changed:
        changed = False
        for f in g.files.values():
            pf = provided(f)
            if f.unpack and f.name not in need and pf & need:
                need.add(f.name)
                archives[f.name] = {"format": str(f.unpack.get("format") or ""), "name": f.name.partition("?")[0],
                                    "top": f.name not in held}
                changed = True
    with tempfile.TemporaryDirectory() as d:
        control = deb._DebPackaging__generate_control(g, p, d, p.component)
    same = set(g.packages) - {name}
    out[name] = {"game": gname, "version": control["Version"], "files": files, "required": sorted(required),
                 "edition": editions[name],
                 "conflicts": sorted(names(control.get("Conflicts")) & same),
                 "replaces": sorted(names(control.get("Replaces")) & same),
                 "launchers": type(g).__module__.endswith("doom_common"), "symlinks": links,
                 "archives": sorted(archives.values(), key=lambda a: (a["name"], a["format"]))}
    if tracks:
        out[name]["tracks"] = tracks
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
    ap.add_argument("--image", required=True, help="a docker image with game-data-packager's dependencies")
    ap.add_argument("--deb", help="our game-data-packager_*_all.deb (default: the one in %s)" % gdp_index.DEBS)
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args(argv)
    deb = gdp_index.our_deb(a.deb, "gdp-manifest.py")
    if deb is None:
        return 2
    want = data_setup_packages()
    if None in want.values():
        print("gdp-manifest.py: no [data_setup] editions entry for %s" % sorted(p for p, e in want.items() if e is None),
              file=sys.stderr)
        return 2
    print("gdp-manifest: %s in %s" % (deb.name, a.image))
    p = gdp_index.run_gdp_python(a.image, deb, INSIDE, json.dumps([sorted(want), want, TRACK_MAX]))
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
