#!/usr/bin/env python3
"""repo.conf's log-only snapshot allowlist: the requests our builds make are not logged, others are.

The map's regexes are read from repo.conf itself (nginx uses PCRE; these ones are also valid Python re).
"""
import pathlib
import re
import sys

CONF = pathlib.Path(__file__).with_name("repo.conf")


def map_regexes(text):
    block = re.search(r"map \$uri \$snapshot_outside \{(.*?)\n\}", text, re.S)
    if not block:
        sys.exit("FAIL: no snapshot_outside map in repo.conf")
    rx = [re.compile(m.group(1)) for m in re.finditer(r"^\s*~(\S+)\s+1;$", block.group(1), re.M)]
    if len(rx) != 6:
        sys.exit(f"FAIL: expected 6 regexes in the map, found {len(rx)}")
    return rx


def outside(rx, uri):
    return any(r.search(uri) for r in rx)


A = "/debian-snapshot/archive/debian/20261102T000000Z"
P = "/debian-snapshot/archive/debian-ports/20261013T000000Z"
S = "/debian-snapshot/archive/debian-security/20261102T000000Z"
H = "by-hash/SHA256/" + "0" * 64

ALLOWED = [
    # config.mk's debian-ports mirror for the PS3's packages
    f"{P}/dists/unstable/InRelease",
    f"{P}/dists/sid/main/binary-ppc64/Packages.xz",
    f"{P}/dists/unstable/main/binary-all/{H}",
    f"{P}/pool-ppc64/main/b/busybox/busybox_1.37.0-4_ppc64.deb",
    # the builder image
    f"{A}/dists/sid/InRelease",
    f"{A}/dists/unstable/main/binary-ppc64/Packages.xz",
    f"{A}/dists/sid/main/binary-amd64/{H}",
    f"{A}/dists/sid/main/binary-all/Packages.xz",
    f"{A}/dists/sid/main/source/{H}",
    f"{A}/dists/sid/main/Contents-ppc64.gz",
    # the builder image's deb-src lines for the 6.12 kernel (seen in the log 2026-11-06/07)
    f"{A}/dists/trixie/InRelease",
    f"{A}/dists/trixie/main/source/{H}",
    f"{A}/dists/trixie/contrib/source/{H}",
    f"{S}/dists/trixie-security/InRelease",
    f"{S}/dists/trixie-security/main/source/{H}",
    f"{S}/dists/trixie-security/contrib/source/{H}",
    # outside dists/: pool files are never logged
    f"{A}/pool/main/l/linux/linux_6.12.115.orig.tar.xz",
]
OUTSIDE = [
    f"{A}/dists/bookworm/InRelease",
    f"{A}/dists/experimental/main/source/{H}",
    f"{A}/dists/trixie-backports/InRelease",
    f"{A}/dists/trixiex/InRelease",
    f"{A}/dists/sid/main/binary-armhf/Packages.xz",
    f"{A}/dists/sid/main/Contents-arm64.gz",
    f"{A}/dists/trixie/main/binary-ppc64el/Packages.xz",
    # each suite only in its own archive
    f"{A}/dists/trixie-security/InRelease",
    f"{S}/dists/trixie/InRelease",
    f"{S}/dists/sid/InRelease",
    f"{P}/dists/trixie/InRelease",
    f"/debian-snapshot/archive/debian-debug/20261102T000000Z/dists/sid/InRelease",
    # trixie: source indices only
    f"{A}/dists/trixie/main/binary-amd64/Packages.xz",
    f"{A}/dists/trixie/main/binary-all/{H}",
    f"{S}/dists/trixie-security/main/binary-ppc64/Packages.xz",
    f"{A}/dists/trixie/main/Contents-amd64.gz",
]


def main():
    rx = map_regexes(CONF.read_text())
    bad = [u for u in ALLOWED if outside(rx, u)] + [u for u in OUTSIDE if not outside(rx, u)]
    for u in bad:
        print("FAIL:", "logged" if u in ALLOWED else "not logged", u)
    if bad:
        sys.exit(1)
    print(f"ok: {len(ALLOWED)} build requests not logged, {len(OUTSIDE)} others logged")


if __name__ == "__main__":
    main()
