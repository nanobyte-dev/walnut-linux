#!/usr/bin/env python3
"""launcher.py [options] PACKAGE...: the menu launchers of a game, found the way the Walnut Games app
finds them (packages/walnut/apps/walnut-games/files/lib/walnut_games.py play_commands), for session.sh.

  --choose NAME      the launcher to take: a menu entry's file name (openarena-gl2.desktop, with or
                     without .desktop) or its Name (OpenArena (GL 2.0)). Without it: the first one.
  --argv-out FILE    write the chosen launcher's command there, NUL-separated (client.sh reads it)
  --runtime-dir DIR  ps3-game-menu's runtime directory (default $XDG_RUNTIME_DIR/ps3-game-menu); its
                     applications/ are looked in before --apps
  --apps DIR         the installed menu entries (default /usr/share/applications)
  --dpkg CMD         what lists a package's files, run as `CMD -L PACKAGE...` (default dpkg-query)

Launchers: the menu entries the packages ship (dpkg -L), sorted by file name, then any other shown
entry (not NoDisplay or Hidden) whose Exec runs one of the packages' programs (/usr/games, /usr/bin,
/usr/local/games), skipping ps3-game, env and VAR=value words. An entry's copy in the runtime
directory wins over the installed one.

The command is the Exec with field codes (%U) removed, split like a shell would, without ps3-game
(the harness puts game mode on itself); leading VAR=value words get an `env` in front.

Output: the first line is the chosen launcher "FILE<TAB>NAME", then every launcher, one per line,
"FILE<TAB>NAME". Exit 3 when the packages have no launcher, 2 for an unknown --choose (the list goes
to stderr, nothing to stdout)."""
import argparse
import os
import shlex
import subprocess
import sys

BIN_DIRS = ("/usr/games", "/usr/bin", "/usr/local/games")


def parse_exec(line):
    """the argv of a .desktop Exec= value: field codes (%f %U ...) cut out, %% kept as %, shlex rules"""
    args = []
    for a in shlex.split(line):
        a = a.replace("%%", "\0")
        if len(a) == 2 and a[0] == "%":
            continue
        out, i = "", 0
        while i < len(a):
            if a[i] == "%" and i + 1 < len(a):
                i += 2
                continue
            out += a[i]
            i += 1
        args.append(out.replace("\0", "%"))
    return args


def desktop_entry(path):
    """the [Desktop Entry] keys of a .desktop file (the unlocalized ones); None without an Exec"""
    keys, inside = {}, False
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for raw in f:
                line = raw.strip()
                if line.startswith("["):
                    inside = line == "[Desktop Entry]"
                elif inside and "=" in line and not line.startswith("#"):
                    k, _, v = line.partition("=")
                    keys.setdefault(k.strip(), v.strip())
    except OSError:
        return None
    return keys if "Exec" in keys else None


def package_files(packages, dpkg="dpkg-query"):
    if not packages:
        return []
    try:
        out = subprocess.run(shlex.split(dpkg) + ["-L"] + sorted(packages), capture_output=True, text=True).stdout
    except OSError:
        return []
    return [l.strip() for l in out.splitlines() if l.strip().startswith("/")]


def entry_program(argv):
    """the program an Exec runs: past ps3-game, env and its VAR=value words"""
    i = 0
    while i < len(argv) and (os.path.basename(argv[i]) in ("ps3-game", "env") or
                             (i and "=" in argv[i] and not argv[i].startswith("-"))):
        i += 1
    if i >= len(argv):
        return None
    return argv[i] if os.path.isabs(argv[i]) else os.path.basename(argv[i])   # a full path stays one


def game_argv(argv):
    """the command without ps3-game; a leading VAR=value gets `env` (stdbuf can't run an assignment)"""
    if argv and os.path.basename(argv[0]) == "ps3-game":
        argv = argv[1:]
    if argv and "=" in argv[0] and not argv[0].startswith("-") and os.path.basename(argv[0]) != "env":
        argv = ["env"] + argv
    return argv


def launchers(files, runtime_dir=None, apps="/usr/share/applications"):
    """[(file name, Name, argv with ps3-game)] in the app's order"""
    dirs = ([os.path.join(runtime_dir, "applications")] if runtime_dir else []) + [apps]
    # the programs by name (on PATH) and by full path (an Exec of /usr/lib/yamagi-quake2/quake2)
    progs = {os.path.basename(f) for f in files if os.path.dirname(f) in BIN_DIRS} | set(files)
    names = sorted({os.path.basename(f) for f in files if os.path.dirname(f) == apps and f.endswith(".desktop")})
    out, seen = [], set()

    def add(name, e):
        try:
            argv = parse_exec(e["Exec"])
        except ValueError:
            return
        if not argv or name in seen:
            return
        out.append((name, e.get("Name") or name[:-len(".desktop")], argv))
        seen.add(name)

    for name in names:
        for d in dirs:
            e = desktop_entry(os.path.join(d, name))
            if e:
                add(name, e)
                break
    if progs:
        for d in dirs:
            try:
                found = sorted(n for n in os.listdir(d) if n.endswith(".desktop"))
            except OSError:
                continue
            for name in found:
                e = None if name in seen else desktop_entry(os.path.join(d, name))
                if not e or e.get("NoDisplay") == "true" or e.get("Hidden") == "true":
                    continue
                try:
                    if entry_program(parse_exec(e["Exec"])) in progs:
                        add(name, e)
                except ValueError:
                    pass
    return out


def choose(found, want):
    """the launcher called want (file name, with or without .desktop, or Name); None when none is"""
    for f, n, a in found:
        if want in (f, n, f[:-len(".desktop")]):
            return f, n, a
    return None


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("packages", nargs="*")
    ap.add_argument("--choose")
    ap.add_argument("--argv-out")
    ap.add_argument("--runtime-dir", default=None)
    ap.add_argument("--apps", default="/usr/share/applications")
    ap.add_argument("--dpkg", default="dpkg-query")
    a = ap.parse_args(argv)
    rt = a.runtime_dir
    if rt is None and os.environ.get("XDG_RUNTIME_DIR"):
        rt = os.path.join(os.environ["XDG_RUNTIME_DIR"], "ps3-game-menu")
    found = launchers(package_files(a.packages, a.dpkg), rt, a.apps)
    if not found:
        return 3
    lines = ["%s\t%s" % (f, n) for f, n, _ in found]
    pick = choose(found, a.choose) if a.choose else found[0]
    if pick is None:
        print("no launcher %r; the choices:" % a.choose, file=sys.stderr)
        print("\n".join("  " + l.replace("\t", "  ") for l in lines), file=sys.stderr)
        return 2
    if a.argv_out:
        with open(a.argv_out, "wb") as f:
            f.write(b"".join(x.encode() + b"\0" for x in game_argv(pick[2])))
    print("%s\t%s" % pick[:2])
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
