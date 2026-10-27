#!/usr/bin/env python3
"""pack-integration.py [--out FILE] [--tree DIR]: what the ps3-games source package
(packages/walnut/system/ps3-games/, its one binary ps3-games-integration) takes from gamedb.

Design
- `make gamedb-integration` runs this; ps3-games' package.conf COPY_IN takes build/gamedb/integration.tar
  into the source (like walnut-games' catalog.tar), and debian/rules reads it at build time.
- Read: every game's build.toml (gamedb/README.md "build.toml": divert) and desktop/ (launchers/*.desktop,
  files/<absolute path>, migrate-home.txt), drafts included. Engines have no desktop/.
- integration.tar:
    tree/usr/share/applications/<name>.desktop  each game's launchers, with TryExec= the program their
                                    Exec runs through ps3-game (so a launcher of a game that isn't installed
                                    stays out of the menu) and X-Walnut-Game=<id> (the Walnut Games app and
                                    bench/harness/launcher.py keep a game's own: games sharing an engine
                                    don't see each other's); every launcher starts its game through ps3-game
    tree/<path>                     desktop/files/<path>: config files installed as they are
    tree/usr/share/ps3-games/divert.list        the divert paths, one per line (ps3-games-integration's
                                    preinst diverts each one; ps3-desktop's tests/menu-check.sh reads it)
    tree/usr/share/ps3-games/migrate-home.list  the migrate-home.txt lines (ps3-desktop's ps3-migrate-home)
- No metapackage Depends any more: ps3-games, ps3-games-free and ps3-games-licensed went on 2026-10-28
  (no game is installed by default; players install them from Walnut Games; docs/PACKAGING.md).
- Fails closed (exit 2): a build.toml that doesn't parse, a key of the wrong type, a launcher that
  doesn't start through ps3-game, two games installing the same file.
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


def program(argv):
    """the program a launcher's Exec runs through ps3-game: past `env` and VAR=value words"""
    i = 1
    if i < len(argv) and os.path.basename(argv[i]) == "env":
        i += 1
    while i < len(argv) and ASSIGN_RE.match(argv[i]):
        i += 1
    if i + 2 < len(argv) and argv[i] == "sh" and argv[i + 1] == "-c":
        # a shell only to expand ~ in an argument: the program is the one the script execs
        m = re.match(r"exec\s+(\S+)", argv[i + 2])
        return m.group(1) if m else argv[i]
    return argv[i] if i < len(argv) else None


def launcher(gid, path):
    """a launcher's text with TryExec= its program and X-Walnut-Game= the game's id (after the Exec line);
    an X-Walnut-Requires= (the data file the entry needs, ps3-game-menu shows it once that exists) must be
    an absolute path"""
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
    for l in lines:
        if l.startswith("X-Walnut-Requires=") and not re.fullmatch(r"(/[A-Za-z0-9._+-]+)+", l.split("=", 1)[1]):
            raise Error("%s: %s: X-Walnut-Requires must be an absolute path" % (gid, path.name))
    out = []
    for l in lines:
        if l.startswith("TryExec=") or l.startswith("X-Walnut-Game="):
            continue   # ours replace any
        out.append(l)
        if l.startswith("Exec="):
            out += ["TryExec=" + prog, "X-Walnut-Game=" + gid]
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


def pack(root, out, tree=None):
    gs = games(root)
    files = collect(gs)
    members = {"tree/" + k: v for k, v in files.items()}
    when = mtime()
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for name in sorted(members):
            add(tar, name, members[name], when)
    write_atomic(out, buf.getvalue())
    if tree:
        for name, data in members.items():
            p = Path(tree) / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
    return len(gs), len(files)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(GAMES), help=argparse.SUPPRESS)
    ap.add_argument("--out", default=str(REPO / "build" / "gamedb" / "integration.tar"))
    ap.add_argument("--tree", help="also unpack the members into this directory (tests, menu-check.sh)")
    a = ap.parse_args(argv)
    try:
        n, f = pack(a.root, a.out, a.tree)
    except Error as e:
        print("pack-integration.py: %s" % e, file=sys.stderr)
        return 2
    print("pack-integration: %d games, %d files -> %s" % (n, f, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
