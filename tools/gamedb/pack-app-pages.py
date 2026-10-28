#!/usr/bin/env python3
"""pack-app-pages.py [--catalog TAR] [--out FILE]: the site's pages the desktop app shows in Details,
build/gamedb/app-pages.tar.

Design
- `make gamedb-app-pages` runs this after `make gamedb-catalog`; packages/walnut/apps/walnut-games/package.conf
  COPY_IN takes the tarball into the package, whose rules unpack it to /usr/share/walnut-games/pages/
  (docs/GAMEDB.md "The desktop app", docs/PACKAGING.md "Walnut Games' catalog and pages").
- The pages are tools/gamedb/build.sh --app's (Hugo, on the host or the CI runner: the PS3 build chroot has
  no Hugo), built into build/gamedb/app-pages/: no site header, footer or script, the game's actions as
  walnut:// links, every link relative and naming its index.html, screenshots at the size shown (352x198).
- What goes in: the HTML pages, the stylesheet and the small pictures. A file the packed catalog
  (build/gamedb/catalog.tar, unpacked to /usr/share/walnut-games/) has with the same path and the same
  bytes, a full-size screenshot or a thumbnail, goes in as a relative symlink to the catalog's copy, so
  the package carries each picture once. The feeds, the sitemap, robots.txt and catalog.json stay out
  (the app reads its own catalog.json).
- A Git LFS pointer among the files stops the pack (a checkout without the screenshots).
- Reproducible: sorted entries, the time SOURCE_DATE_EPOCH (else the newest commit's), root owner.
- stdlib only (python3 -I; the CI runner and a developer's PC both have it).
"""
import argparse
import hashlib
import io
import os
import subprocess
import sys
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
LFS_MAGIC = b"version https://git-lfs.github.com/spec/"
SKIP = {"catalog.json", "robots.txt", "index.xml", "sitemap.xml"}


def mtime():
    e = os.environ.get("SOURCE_DATE_EPOCH")
    if e:
        return int(e)
    r = subprocess.run(["git", "-C", str(REPO), "log", "-1", "--format=%ct", "--", "gamedb", "website/gamedb"],
                       capture_output=True, text=True)
    return int(r.stdout.strip() or 0)


def catalog_files(tar_path):
    """{path: sha256} of the regular files in the packed catalog"""
    out = {}
    with tarfile.open(tar_path) as tar:
        for m in tar.getmembers():
            if m.isfile():
                out[m.name] = hashlib.sha256(tar.extractfile(m).read()).hexdigest()
    return out


def pack(site, catalog, out, when=None):
    """the tarball of the site's directory `site`; returns (files, links)"""
    when = mtime() if when is None else when
    entries = []
    for f in sorted(Path(site).rglob("*")):
        if f.is_dir():
            continue
        rel = f.relative_to(site).as_posix()
        if rel in SKIP or rel.endswith(".xml"):
            continue
        data = f.read_bytes()
        if data.startswith(LFS_MAGIC):
            raise SystemExit("%s is a Git LFS pointer: run `git lfs pull` (CI: the gallery's files)" % rel)
        if catalog.get(rel) == hashlib.sha256(data).hexdigest():
            entries.append((rel, None, "../" * rel.count("/") + "../" + rel))    # pages/<rel> -> <rel>
        else:
            entries.append((rel, data, None))
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    tmp = str(out) + ".tmp"
    with tarfile.open(tmp, "w", format=tarfile.PAX_FORMAT) as tar:
        for rel, data, link in entries:
            ti = tarfile.TarInfo(rel)
            ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname = when, 0, 0, "root", "root"
            if link:
                ti.type, ti.linkname, ti.mode = tarfile.SYMTYPE, link, 0o777
                tar.addfile(ti)
            else:
                ti.size, ti.mode = len(data), 0o644
                tar.addfile(ti, io.BytesIO(data))
    os.replace(tmp, out)
    return sum(1 for e in entries if e[2] is None), sum(1 for e in entries if e[2])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--catalog", default=str(REPO / "build" / "gamedb" / "catalog.tar"))
    ap.add_argument("--site", default=str(REPO / "build" / "gamedb" / "app-pages"),
                    help="where build.sh --app writes the pages (made again)")
    ap.add_argument("--out", default=str(REPO / "build" / "gamedb" / "app-pages.tar"))
    a = ap.parse_args()
    subprocess.run([str(HERE / "build.sh"), "--app", a.site], check=True, stdout=subprocess.DEVNULL)
    files, links = pack(a.site, catalog_files(a.catalog), a.out)
    print("pack-app-pages: %d files, %d links to the catalog's pictures -> %s (%.1f MB)"
          % (files, links, a.out, os.path.getsize(a.out) / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
