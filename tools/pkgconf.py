#!/usr/bin/env python3
"""pkgconf.py: the one reader of our source packages' package.conf files (docs/LAYOUT.md), and the package list.

  pkgconf.py dir NAME             the directory of package NAME (exit 1: none, 2: not a package name)
  pkgconf.py dir --all [--order]  every package directory, one per line
  pkgconf.py dir --order          every package name in build order: after the ones its LOCAL_DEPS names, linux first,
                                  ps3rsx last (it builds against the kernels), otherwise by name
  pkgconf.py get PKG KEY          KEY's value (empty when unset); $NAME in it is expanded like sh would
  pkgconf.py json PKG             every key as a JSON object
  pkgconf.py words PKG KEY        KEY's value as one word per line (LOCAL_DEPS, VERSION_PATHS, FLAVOURS)

A package is a directory with a package.conf where packages/<group>/<name>/, packages/<group>/<area>/<name>/,
gamedb/games/<id>/packages/<name>/, gamedb/games/<id>/licensed/<name>/ or gamedb/engines/<e>/packages/<name>/ say; its name
is the directory's. Two directories of one name are an error in every mode. PKG_DIR_ROOT=DIR reads another tree (tests).

package.conf is read as sh reads it, for the one form it uses: KEY=value lines, the value bare or in "..." or '...',
blank lines and # comments; $NAME and ${NAME} are expanded from the earlier keys, then the environment (Makefile exports
the ORIG variables), else empty. Anything else (a command, a multi-line value) is an error, never a guess."""
import glob
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(os.environ.get("PKG_DIR_ROOT") or Path(__file__).resolve().parent.parent)
PATTERNS = ["packages/*/*/package.conf", "packages/*/*/*/package.conf", "gamedb/games/*/packages/*/package.conf",
            "gamedb/games/*/licensed/*/package.conf", "gamedb/engines/*/packages/*/package.conf"]
ASSIGN = re.compile(r"""([A-Za-z_][A-Za-z0-9_]*)=("([^"]*)"|'([^']*)'|[^\s"']*)(?:\s+#.*)?$""")
REF = re.compile(r"\$(?:([A-Za-z_][A-Za-z0-9_]*)|\{([A-Za-z_][A-Za-z0-9_]*)\})")
NAME = re.compile(r"^[a-z0-9][a-z0-9.+-]*$")


class Error(Exception):
    pass


class Conf(dict):
    """parse()'s result; .bad: the keys of lines a lenient parse could not read"""
    bad = frozenset()


def parse(text, where="package.conf", env=None, strict=True):
    """{KEY: value} of a package.conf's text; strict=False skips the lines it cannot read (the publisher reads old
    commits' files, and wants only the keys it asks for)"""
    out = Conf()
    bad = set()
    out.bad = bad
    for n, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = ASSIGN.fullmatch(line)
        if not m and not strict:
            bad.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", line))  # every name the line mentions: export VERSION=.., : ${VERSION:=..}
            continue
        if not m:
            raise Error("%s:%d: not a KEY=value line: %s" % (where, n, line))
        key, raw = m.group(1), m.group(2)
        # sh reads these differently from the plain KEY=value this reader means: refuse, never guess
        unquoted = raw if not raw.startswith("'") else ""
        if re.search(r"[`\\;&|<>]|\$\(|\$\{[^}]*[^A-Za-z0-9_}]|\$(?![A-Za-z_{])", unquoted) or \
                (raw[:1] not in "\"'" and re.search(r"^~|:~", raw)):
            if not strict:
                bad.add(key)
                continue
            raise Error("%s:%d: a command, an escape or an expansion form other than $NAME: %s" % (where, n, line))
        if m.group(4) is not None:      # '...': no expansion
            out[key] = m.group(4)
            continue
        val = m.group(3) if m.group(3) is not None else raw
        out[key] = REF.sub(lambda r: out.get(r.group(1) or r.group(2), (env if env is not None else os.environ).get(r.group(1) or r.group(2), "")), val)
    return out


def read(pkg_dir):
    p = ROOT / pkg_dir / "package.conf"
    return parse(p.read_text(), str(p))


def all_dirs():
    """every package directory, in PATTERNS order, each pattern's matches sorted bytewise"""
    dirs = []
    for pat in PATTERNS:
        dirs += [os.path.relpath(os.path.dirname(f), ROOT) for f in sorted(glob.glob(str(ROOT / pat))) if os.path.isfile(f)]
    names = {}
    for d in dirs:
        names.setdefault(os.path.basename(d), []).append(d)
    dups = sorted(n for n, ds in names.items() if len(ds) > 1)
    if dups:
        raise Error("\n".join("!! two packages named %s: %s" % (n, " ".join(names[n])) for n in dups))
    return dirs


def order(dirs):
    """package names in build order"""
    dirmap = {os.path.basename(d): d for d in dirs}
    deps = {n: read(d).get("LOCAL_DEPS", "").split() for n, d in dirmap.items()}
    if "ps3rsx" in dirmap:
        deps["ps3rsx"] = deps["ps3rsx"] + ["linux"]
    for n in sorted(dirmap):
        for d in deps[n]:
            if d not in dirmap:
                raise Error("!! %s: LOCAL_DEPS names %s, which isn't a package here" % (n, d))
    names = sorted(dirmap)
    left = (["linux"] if "linux" in dirmap else []) + [n for n in names if n not in ("linux", "ps3rsx")] \
        + (["ps3rsx"] if "ps3rsx" in dirmap else [])
    out, built = [], set()
    while left:
        for n in left:
            if all(d in built for d in deps[n]):
                out.append(n)
                built.add(n)
                left.remove(n)
                break   # start over: an earlier name whose dependency just came out goes first
        else:
            raise Error("!! LOCAL_DEPS cycle among: " + " ".join(left))
    return out


def find(name):
    for d in all_dirs():
        if os.path.basename(d) == name:
            return d
    raise Error("!! no package %s (packages/**/%s/package.conf, gamedb/*/*/{packages,licensed}/%s/package.conf)" % (name, name, name))


def main(argv):
    try:
        cmd, args = (argv[0], argv[1:]) if argv else ("", [])
        if cmd == "dir":
            if args[:1] == ["--all"] or args[:1] == ["--order"]:
                dirs = all_dirs()
                ordered = "--order" in args
                if args[:1] == ["--all"] and not ordered:
                    out = dirs
                else:
                    byname = {os.path.basename(d): d for d in dirs}
                    out = order(dirs)
                    if "--all" in args:
                        out = [byname[n] for n in out]
                print("\n".join(out))
            elif len(args) == 1 and NAME.match(args[0]):
                print(find(args[0]))
            else:
                print("usage: pkg-dir.sh NAME | --all [--order] | --order", file=sys.stderr)
                return 2
        elif cmd in ("get", "words") and len(args) == 2:
            val = read(find(args[0])).get(args[1], "")
            print("\n".join(val.split()) if cmd == "words" else val)
        elif cmd == "json" and len(args) == 1:
            print(json.dumps(read(find(args[0])), indent=1, sort_keys=True))
        else:
            print(__doc__, file=sys.stderr)
            return 2
    except Error as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
