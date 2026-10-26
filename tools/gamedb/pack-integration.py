#!/usr/bin/env python3
"""pack-integration.py [--out FILE] [--licensed-out FILE] [--tree DIR]: what the ps3-games source package
(packages/walnut/system/ps3-games/) and ps3-games-licensed (packages/licensed/) take from gamedb.

Design
- `make gamedb-integration` runs this; ps3-games' package.conf COPY_IN takes build/gamedb/integration.tar
  into the source (like walnut-games' catalog.tar), ps3-games-licensed's takes
  build/gamedb/ps3-games-licensed.substvars. Both debian/rules read them at build time.
- Read: every game's build.toml (gamedb/README.md "build.toml": suite, packages, meta, divert,
  [[licensed]] meta) and desktop/ (launchers/*.desktop, files/<absolute path>, migrate-home.txt), drafts
  included: a game's suite, not whether the catalog shows it, puts it in the metapackages (ClassiCube is a
  draft in ps3-games). Engines have no desktop/ and name no metapackage member.
- integration.tar:
    tree/usr/share/applications/<name>.desktop  each game's launchers, with TryExec= the program their
                                    Exec runs through ps3-game (so a launcher of a game that isn't installed
                                    stays out of the menu); every launcher starts its game through ps3-game
    tree/<path>                     desktop/files/<path>: config files installed as they are
    tree/usr/share/ps3-games/divert.list        the divert paths, one per line (ps3-games-integration's
                                    preinst diverts each one; ps3-desktop's tests/menu-check.sh reads it)
    tree/usr/share/ps3-games/migrate-home.list  the migrate-home.txt lines (ps3-desktop's ps3-migrate-home)
    control/ps3-games.substvars, control/ps3-games-free.substvars
                                    games:Depends (and games:Suggests) for debian/control: ps3-games names the
                                    meta packages of every game with suite free or licensed, ps3-games-free
                                    those of the free ones and Suggests the licensed ones'
- ps3-games-licensed.substvars: games:Depends, the [[licensed]] names ps3-games-licensed pulls in (an
  entry's meta, by default true for a game with suite licensed). Kept apart from integration.tar, which
  goes into a public source package.
- Fails closed (exit 2): a build.toml that doesn't parse, a key of the wrong type, a meta package not in
  packages, a launcher that doesn't start through ps3-game, two games installing the same file.
- Reproducible: sorted entries, the time SOURCE_DATE_EPOCH (else the newest commit's), root owner.
- stdlib only (python3 -I; CI runs it before the package's source is made).
"""
import argparse
import io
import os
import re
import shlex
import subprocess
import sys
import tarfile
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
GAMES = REPO / "gamedb" / "games"
APPS = "usr/share/applications"
SHARE = "usr/share/ps3-games"
SUITES = ("free", "licensed", "none")
ASSIGN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*=")


class Error(Exception):
    pass


def games(root=GAMES):
    """[(id, bundle dir, build.toml)] of every game with a build.toml (drafts too), by id"""
    out = []
    for f in sorted(Path(root).glob("*/build.toml")):
        try:
            b = tomllib.loads(f.read_text())
        except (OSError, tomllib.TOMLDecodeError) as e:
            raise Error("%s: %s" % (f, e))
        out.append((f.parent.name, f.parent, b))
    return out


def strings(b, key, gid):
    v = b.get(key, [])
    if not isinstance(v, list) or not all(isinstance(x, str) and x for x in v):
        raise Error("%s: build.toml %s must be a list of strings" % (gid, key))
    return v


def meta_packages(gid, b):
    """the packages ps3-games names for a game: build.toml meta, else its packages"""
    pkgs = strings(b, "packages", gid)
    if "meta" not in b:
        return pkgs
    meta = strings(b, "meta", gid)
    bad = [p for p in meta if p not in pkgs]
    if bad:
        raise Error("%s: build.toml meta %s not in its packages" % (gid, ", ".join(bad)))
    return meta


def depends(gs):
    """{binary: {"Depends": [...], "Suggests": [...]}} for ps3-games, ps3-games-free, ps3-games-licensed"""
    out = {p: {"Depends": [], "Suggests": []} for p in ("ps3-games", "ps3-games-free", "ps3-games-licensed")}

    def add(pkg, field, names):
        out[pkg][field] += [n for n in names if n not in out[pkg][field]]

    for gid, _, b in gs:
        suite = b.get("suite", "none")
        if suite not in SUITES:
            raise Error("%s: build.toml suite %r" % (gid, suite))
        if suite != "none":
            m = meta_packages(gid, b)
            add("ps3-games", "Depends", m)
            add("ps3-games-free", "Depends" if suite == "free" else "Suggests", m)
        lic = b.get("licensed", [])
        if not isinstance(lic, list) or not all(isinstance(e, dict) for e in lic):
            raise Error("%s: build.toml licensed must be [[licensed]] tables" % gid)
        for e in lic:
            want = e.get("meta", suite == "licensed")
            if not isinstance(want, bool):
                raise Error("%s: [[licensed]] %s: meta must be true or false" % (gid, e.get("name")))
            if want:
                add("ps3-games-licensed", "Depends", [e["name"]])
    return out


def program(argv):
    """the program a launcher's Exec runs through ps3-game: past `env` and VAR=value words"""
    i = 1
    if i < len(argv) and os.path.basename(argv[i]) == "env":
        i += 1
    while i < len(argv) and ASSIGN_RE.match(argv[i]):
        i += 1
    return argv[i] if i < len(argv) else None


def launcher(gid, path):
    """a launcher's text with TryExec= its program (after the Exec line)"""
    lines = path.read_text().splitlines()
    execs = [l for l in lines if l.startswith("Exec=")]
    if len(execs) != 1:
        raise Error("%s: %s needs exactly one Exec line" % (gid, path.name))
    argv = shlex.split(execs[0][len("Exec="):])
    if not argv or argv[0] != "ps3-game":
        raise Error("%s: %s: Exec must start the game through ps3-game (game mode)" % (gid, path.name))
    prog = program(argv)
    if not prog:
        raise Error("%s: %s: no program in its Exec" % (gid, path.name))
    out = []
    for l in lines:
        if l.startswith("TryExec="):
            continue   # ours replaces any
        out.append(l)
        if l.startswith("Exec="):
            out.append("TryExec=" + prog)
    return ("\n".join(out) + "\n").encode()


def migrate_lines(gid, path):
    out = []
    for l in path.read_text().splitlines():
        l = l.strip()
        if not l or l.startswith("#"):
            continue
        w = l.split()
        if len(w) > 2 or w[0].startswith("/") or ".." in w[0].split("/") or (len(w) == 2 and not w[1].startswith("/")):
            raise Error("%s: migrate-home.txt line %r: <path in the home> [<absolute packaged default>]" % (gid, l))
        out.append(" ".join(w))
    return out


def collect(gs):
    """{member: bytes} of integration.tar's tree/ part"""
    files, owner = {}, {}
    divert, migrate = [], []

    def put(name, data, gid):
        if name in owner:
            raise Error("%s and %s both install /%s" % (owner[name], gid, name))
        owner[name] = gid
        files[name] = data

    for gid, gdir, b in gs:
        for d in strings(b, "divert", gid):
            if not re.fullmatch(r"(/[A-Za-z0-9._+-]+)+", d):
                raise Error("%s: build.toml divert %r is not an absolute path" % (gid, d))
            if d in divert:
                raise Error("%s: divert %s is listed twice" % (gid, d))
            divert.append(d)
        desk = gdir / "desktop"
        if not desk.is_dir():
            continue
        for f in sorted((desk / "launchers").glob("*")):
            if not f.is_file() or not f.name.endswith(".desktop"):
                raise Error("%s: desktop/launchers/%s is not a .desktop file" % (gid, f.name))
            put("%s/%s" % (APPS, f.name), launcher(gid, f), gid)
        fdir = desk / "files"
        for f in sorted(p for p in fdir.rglob("*") if p.is_file()) if fdir.is_dir() else []:
            put(f.relative_to(fdir).as_posix(), f.read_bytes(), gid)
        if (desk / "migrate-home.txt").is_file():
            migrate += migrate_lines(gid, desk / "migrate-home.txt")
    for d in divert:
        if d.lstrip("/") in files:
            raise Error("%s is both diverted and installed" % d)
    files[SHARE + "/divert.list"] = "".join(d + "\n" for d in divert).encode()
    files[SHARE + "/migrate-home.list"] = "".join(m + "\n" for m in migrate).encode()
    return files


def substvars(fields):
    return "".join("games:%s=%s\n" % (k, ", ".join(v)) for k, v in fields.items() if v).encode()


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


def write_atomic(path, data):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    tmp = str(path) + ".tmp"
    Path(tmp).write_bytes(data)
    os.replace(tmp, path)


def pack(root, out, licensed_out, tree=None):
    gs = games(root)
    deps = depends(gs)
    files = collect(gs)
    members = {"tree/" + k: v for k, v in files.items()}
    for p in ("ps3-games", "ps3-games-free"):
        members["control/%s.substvars" % p] = substvars(deps[p])
    when = mtime()
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for name in sorted(members):
            add(tar, name, members[name], when)
    write_atomic(out, buf.getvalue())
    if licensed_out:
        write_atomic(licensed_out, substvars(deps["ps3-games-licensed"]))
    if tree:
        for name, data in members.items():
            p = Path(tree) / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
    return len(gs), len(files), deps


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(GAMES), help=argparse.SUPPRESS)
    ap.add_argument("--out", default=str(REPO / "build" / "gamedb" / "integration.tar"))
    ap.add_argument("--licensed-out", default=str(REPO / "build" / "gamedb" / "ps3-games-licensed.substvars"),
                    help="ps3-games-licensed's substvars ('' for none)")
    ap.add_argument("--tree", help="also unpack the members into this directory (tests, menu-check.sh)")
    a = ap.parse_args(argv)
    try:
        n, f, deps = pack(a.root, a.out, a.licensed_out, a.tree)
    except Error as e:
        print("pack-integration.py: %s" % e, file=sys.stderr)
        return 2
    print("pack-integration: %d games, %d files -> %s; ps3-games %d, ps3-games-free %d+%d, ps3-games-licensed %d"
          % (n, f, a.out, len(deps["ps3-games"]["Depends"]), len(deps["ps3-games-free"]["Depends"]),
             len(deps["ps3-games-free"]["Suggests"]), len(deps["ps3-games-licensed"]["Depends"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
