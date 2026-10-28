#!/usr/bin/env python3
"""dataguide.py: the generated part of a game's data guide (its Guides tab, games/<id>/guides/#game-data, and the
Overview's Installation section), technical.json's data_guide entry. Made from data only, so it can't drift from
what the tools do: the game's [data_setup] (build.toml, as the catalog carries it), tools/gamedb/gdp-manifest.json
(every package's files as our game-data-packager builds them) and our game-data-packager patches.

Design
- One entry per game with a [data_setup] (None for the others): the gdp game, the original release's name, the
  hint, which packages are free downloads (fetch) or may download official patch files (downloads), and per
  package (required ones first, then the optional ones) its edition, the walnut-data and game-data-packager
  command lines that make it, and its files where a manual install puts them.
- Files: the manifest's required files and its optional ones outside usr/share/doc (gdp's copies of manuals and
  licences are only counted), each with every version gdp accepts (release, size, the strongest checksum it
  has). A package with more than MAX_FILES of them is summed up per folder instead (RollerCoaster Tycoon 2:
  over a thousand files) with a link to the manifest on the public repository. Music packages (the manifest's
  tracks: ripped audio, no checksum) list the track paths and the size bound.
- Patches: our game-data-packager patches that touch the game (its data/<gdp>.yaml or games/<gdp>.py, or a
  subject naming the original release), with the packages they add (a "+  <name>:" line in data/<gdp>.yaml),
  which Debian's own game-data-packager doesn't have.
- stdlib only; the walnut-data commands are those of packages/walnut/apps/walnut-games/files/bin/walnut-data
  (import [-p PKG]... GAME [SOURCE...], remove GAME).
"""
import json
import re
from pathlib import Path

MANIFEST = Path("tools") / "gamedb" / "gdp-manifest.json"
GDP_SOURCE = "game-data-packager"      # our patched source package (packages/patched/tools/game-data-packager)
MAX_FILES = 24                         # more files than this in a package: a summary per folder instead
BIG_FILE = 1 << 20                     # ... and its files of this size or more one by one
GITHUB = "https://github.com/nanobyte-dev/walnut-linux/blob/main/%s"
SOURCE = "/path/to/your/copy"
ADDED_RE = re.compile(r"^\+  ([a-z0-9][a-z0-9-]+):\s*$", re.M)


def human(n):
    """bytes as a short size: 18689235 -> 17.8 MB"""
    for unit, size in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if n >= size:
            return "%.1f %s" % (n / size, unit)
    return "%d bytes" % n


def load_manifest(repo):
    f = Path(repo) / MANIFEST
    return json.loads(f.read_text()) if f.is_file() else {"gdp_version": None, "packages": {}}


def alt(a):
    """one accepted version of a file: {release, size, size_h, algo, sum}"""
    algo = next((k for k in ("sha256", "sha1", "md5") if k in a), None)
    size = a.get("size")
    return {"release": a.get("release"), "size": size, "size_h": human(size) if size is not None else None,
            "bytes": "{:,}".format(size) if size is not None else None,     # exact, as text (Hugo reads numbers as floats)
            "algo": algo, "sum": a.get(algo) if algo else None}


def compact(alts):
    """a file's row in the compact table: its versions as a short list, the size range, and the checksum column's
    label ("sha1"; "checksum" with the algorithm in front of each sum where the versions differ)"""
    rel = [a["release"] for a in alts if a["release"]]
    sizes = [a["size"] for a in alts if a["size"] is not None]
    lo, hi = (min(sizes), max(sizes)) if sizes else (None, None)
    if lo is None:
        size_h = None
    elif lo == hi:
        size_h = human(lo)
    elif human(lo).split()[1] == human(hi).split()[1]:
        size_h = "%s\u2013%s" % (human(lo).split()[0], human(hi))      # 10.6\u201313.8 MB
    else:
        size_h = "%s\u2013%s" % (human(lo), human(hi))
    algos = {a["algo"] for a in alts if a["algo"]}
    mixed = len(algos) > 1
    for a in alts:
        a["sum_show"] = ("%s %s" % (a["algo"], a["sum"]) if mixed else a["sum"]) if a["sum"] else None
    return {"versions": rel, "size_range": size_h, "sum_label": "checksum" if mixed else (next(iter(algos), "checksum"))}


def files(m):
    """(the package's game files [{path, name, required, alts}], how many doc files gdp adds besides)"""
    req = set(m.get("required", []))
    out, docs = [], 0
    for p in sorted(m.get("files", {})):
        if p not in req and p.startswith("usr/share/doc/"):
            docs += 1
            continue
        alts = [alt(a) for a in m["files"][p]]
        out.append({"path": "/" + p, "name": p.rsplit("/", 1)[-1], "required": p in req, "alts": alts, **compact(alts)})
    out.sort(key=lambda f: (not f["required"], f["path"]))
    return out, docs


def folders(fs):
    """a long file list summed up per folder: [{dir, count, size_h}] (each file's first version's size)"""
    d = {}
    for f in fs:
        k = f["path"].rsplit("/", 1)[0]
        n, s = d.get(k, (0, 0))
        d[k] = (n + 1, s + (f["alts"][0]["size"] or 0 if f["alts"] else 0))
    return [{"dir": k, "count": n, "size_h": human(s)} for k, (n, s) in sorted(d.items())]


def common_dir(paths):
    """the folder the paths share ("/usr/share/games/quake/id1"), or None"""
    if not paths:
        return None
    parts = [p.split("/")[:-1] for p in paths]
    out = []
    for xs in zip(*parts):
        if len(set(xs)) != 1:
            break
        out.append(xs[0])
    return "/".join(out) or None


def gdp_patches(repo, gdp, original, patch_subjects, package_dir):
    """(our gdp patches that touch the game: [{file, subject, url}], the packages they add)"""
    d = package_dir(repo, GDP_SOURCE) if package_dir else None
    if not d:
        return [], []
    out, added = [], []
    name_re = re.compile(r"\b%s\b(?! I)" % re.escape(original), re.I) if original else None
    for f, subject in patch_subjects(GDP_SOURCE, repo):
        text = (Path(repo) / d / "patches" / f).read_text(errors="replace")
        touched = set(re.findall(r"^\+\+\+ b/(\S+)", text, re.M))
        mine = {"data/%s.yaml" % gdp, "game_data_packager/games/%s.py" % gdp}
        if touched & mine or (name_re and name_re.search(subject)):
            out.append({"file": f, "subject": subject, "url": GITHUB % ("%s/patches/%s" % (d, f))})
            # the packages a patch adds to the game's yaml (a new entry under packages:)
            part = re.split(r"^diff --git ", text, flags=re.M)
            for chunk in part:
                if chunk.startswith("a/data/%s.yaml " % gdp):
                    added += [p for p in ADDED_RE.findall(chunk) if p not in added]
    return out, added


def package(name, gid, ds, m, role, added):
    """one package's guide entry"""
    fetch = name in (ds.get("fetch") or [])
    download = name in (ds.get("downloads") or [])
    gdp = ds["gdp"]
    fs, docs = files(m)
    tracks = m.get("tracks")
    walnut = "walnut-data import -p %s %s%s" % (name, gid, "" if fetch else " " + SOURCE)
    if role == "required" and not fetch:     # a plain import makes every required package the source holds
        walnut = "walnut-data import %s %s" % (gid, SOURCE)
    gdp_cmd = "game-data-packager -d .%s -p %s %s%s" % ("" if (fetch or download) else " --no-download", name, gdp,
                                                        "" if fetch else " " + SOURCE)
    paths = [f["path"] for f in fs] + ["/" + p for p in (tracks or {}).get("paths", [])]
    d = common_dir(paths)
    for f in fs:        # the path under the package's folder (the guide says "Into <folder>")
        f["rel"] = f["path"][len(d) + 1:] if d and f["path"].startswith(d + "/") else f["path"]
    big = len(fs) > MAX_FILES
    links = sorted(m.get("symlinks", {}).items())
    e = {"name": name, "edition": ds.get("editions", {}).get(name) or m.get("edition"), "role": role,
         "fetch": fetch, "download": download, "ours": name in added, "version": m.get("version"),
         "walnut": walnut, "gdp": gdp_cmd, "deb": "%s_*.deb" % name,
         "dir": d, "replaces": m.get("replaces", []),
         "file_count": len(fs), "doc_count": docs, "known": bool(m),
         "size_h": human(sum((f["alts"][0]["size"] or 0) for f in fs if f["required"] and f["alts"])) if fs else None,
         "files": [] if big else fs, "folders": folders(fs) if big else [],
         "symlinks": [{"path": "/" + p, "target": t} for p, t in links[:MAX_FILES]], "symlink_count": len(links)}
    if tracks:
        e["tracks"] = {"first": tracks.get("first"), "last": tracks.get("last"), "max_size_h": human(tracks.get("max_size", 0)),
                       "source": tracks.get("source"), "paths": ["/" + p for p in tracks.get("paths", [])]}
    if big:     # the files of 1 MB and more (the paks, the big data files) are still listed one by one
        e["key_files"] = [f for f in fs if f["alts"] and (f["alts"][0]["size"] or 0) >= BIG_FILE][:MAX_FILES]
        e["checksum_algos"] = sorted({a["algo"] for f in fs for a in f["alts"] if a["algo"]})
    return e


def build(gid, title, ds, manifest, repo, patch_subjects=None, package_dir=None):
    """technical.json's data_guide for one game (ds: its catalog data_setup), or None without one"""
    if not ds or not ds.get("gdp"):
        return None
    pk = manifest.get("packages", {})
    original = ds.get("original") or title
    patches, added = gdp_patches(repo, ds["gdp"], original, patch_subjects, package_dir) if patch_subjects else ([], [])
    req, opt = list(ds.get("required") or []), list(ds.get("optional") or [])
    pkgs = [package(n, gid, ds, pk.get(n, {}), "required", added) for n in req] + \
           [package(n, gid, ds, pk.get(n, {}), "optional", added) for n in opt]
    music = [p["name"] for p in pkgs if "tracks" in p]
    # the .deb the install line names: the first package made from the player's copy
    first = next((p for p in pkgs if p["role"] == "required" and not p["fetch"]), None) or \
        next((p for p in pkgs if not p["fetch"]), None) or (pkgs[0] if pkgs else None)
    return {"gdp": ds["gdp"], "original": original, "hint": ds.get("hint"), "gdp_version": manifest.get("gdp_version"),
            "needs_data": bool(req), "fetch": [p for p in pkgs if p["fetch"]], "packages": pkgs,
            "addons": [p["name"] for p in pkgs if p["role"] == "optional" and not p["fetch"]],
            "music": music, "alternatives": ds.get("alternatives") or [],
            "patches": patches, "ours": [p for p in added if p in req + opt],
            "manifest_url": GITHUB % MANIFEST.as_posix(), "install_deb": first["deb"] if first else None,
            "walnut_base": "walnut-data import %s %s" % (gid, SOURCE), "walnut_remove": "walnut-data remove %s" % gid}
