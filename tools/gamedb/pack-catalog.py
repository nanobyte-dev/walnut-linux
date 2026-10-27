#!/usr/bin/env python3
"""pack-catalog.py [--out FILE]: the catalog the desktop app ships, build/gamedb/catalog.tar.

Design
- `make gamedb-catalog` runs this; packages/walnut/apps/walnut-games/package.conf COPY_IN takes the tarball into the
  package, whose rules unpack it to /usr/share/walnut-games/ (docs/GAMEDB.md "The desktop app").
- Contents: catalog.json (catalog.py's, without the .deb links: the app installs from the apt
  repository), and for every game games/<id>/thumb.png plus the screenshots its sessions list
  (games/<id>/rating/sessions/<run>/shots/<file>.jpg, the paths as in gamedb/games/<id>/ and on the site). The
  rest of a game's folder (index.md, result.toml) stays out.
- The catalog is checked first (check.py): a catalog that does not validate is not packed. Drafts
  (index.md draft = true) are left out, as catalog.py leaves them out, pictures included.
- Screenshots are Git LFS files. A checkout without the LFS objects holds 130-byte pointer files;
  packing those would ship broken pictures, so they stop the build with a message (CI fetches just these
  from LFS for this package: tools/gamedb/gallery.py, .github/workflows/walnut-package.yml).
- Reproducible: sorted entries, the time SOURCE_DATE_EPOCH (else the newest commit's), root owner.
- stdlib only (python3 -I; the CI runner and a developer's PC both have it).
"""
import argparse
import io
import json
import os
import subprocess
import sys
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import catalog   # noqa: E402
import check     # noqa: E402

LFS_MAGIC = b"version https://git-lfs.github.com/spec/"


def mtime():
    e = os.environ.get("SOURCE_DATE_EPOCH")
    if e:
        return int(e)
    r = subprocess.run(["git", "-C", str(REPO), "log", "-1", "--format=%ct", "--", "gamedb/games"],
                       capture_output=True, text=True)
    return int(r.stdout.strip() or 0)


def add(tar, name, data, when):
    ti = tarfile.TarInfo(name)
    ti.size, ti.mtime, ti.mode = len(data), when, 0o644
    ti.uid = ti.gid = 0
    ti.uname = ti.gname = "root"
    tar.addfile(ti, io.BytesIO(data))


def pack(content, out, repo=REPO):
    problems = check.check(content, repo)
    if problems:
        raise SystemExit("\n".join("%s: %s" % (g, e) for g, errs in problems.items() for e in errs))
    cat = catalog.build(content, repo)
    when = mtime()
    files = {}
    for g in cat["games"]:
        gdir = Path(content) / g["id"]
        if check.is_draft(gdir):
            raise SystemExit("%s is a draft and must not be packed (catalog.py leaves drafts out)" % g["id"])
        names = [g["thumb"].split("/", 2)[2]] if g.get("thumb") else []   # a game may have no thumbnail
        for t in g["tests"]:
            names += t["ps3"]   # only the PS3's pictures are published
        for n in dict.fromkeys(names):
            data = (gdir / n).read_bytes()
            if data.startswith(LFS_MAGIC):
                raise SystemExit("%s/%s is a Git LFS pointer: run `git lfs pull` (CI: checkout with lfs: true)"
                                 % (g["id"], n))
            files["games/%s/%s" % (g["id"], n)] = data
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    tmp = str(out) + ".tmp"
    with tarfile.open(tmp, "w", format=tarfile.PAX_FORMAT) as tar:
        add(tar, "catalog.json", (json.dumps(cat, indent=1, ensure_ascii=False) + "\n").encode(), when)
        for name in sorted(files):
            add(tar, name, files[name], when)
    os.replace(tmp, out)
    return len(cat["games"]), len(files)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--content", default=str(check.CONTENT))
    ap.add_argument("--out", default=str(REPO / "build" / "gamedb" / "catalog.tar"))
    a = ap.parse_args()
    n, f = pack(a.content, a.out)
    print("pack-catalog: %d games, %d files -> %s" % (n, f, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
