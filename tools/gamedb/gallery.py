#!/usr/bin/env python3
"""gallery.py [--lfs-include] [--content DIR]: the screenshots the catalog shows, one repository path per
line: every published game's sessions' gallery (result.toml [shots] ps3, files in the session's shots/).
--lfs-include: the same as one comma-separated list for `git lfs pull -I`, so CI fetches from Git LFS only
the pictures the site and the walnut-games catalog use, not every shot and raw/ file
(.github/workflows/walnut-site.yml, walnut-package.yml)."""
import argparse
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import check  # noqa: E402


def gallery(content=check.CONTENT):
    """the repository-relative paths of every published game's gallery shots, sorted"""
    out = []
    for g in sorted(Path(content).iterdir()):
        if not (g / "index.md").is_file() or check.is_draft(g):
            continue
        for r in sorted((g / check.SESSIONS).glob("*/result.toml")):
            for f in tomllib.loads(r.read_text()).get("shots", {}).get("ps3", []):
                p = (r.parent / f).resolve()
                out.append(str(p.relative_to(REPO)) if p.is_relative_to(REPO) else str(p))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--content", default=str(check.CONTENT))
    ap.add_argument("--lfs-include", action="store_true")
    a = ap.parse_args()
    paths = gallery(a.content)
    if not paths:
        print("gallery.py: no gallery shots", file=sys.stderr)
        return 1
    print(",".join(paths) if a.lfs_include else "\n".join(paths))
    return 0


if __name__ == "__main__":
    sys.exit(main())
