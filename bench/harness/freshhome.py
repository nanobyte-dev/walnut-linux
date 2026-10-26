#!/usr/bin/env python3
"""freshhome.py: a played session tests the game as shipped, on fresh defaults (docs/GAMEDB.md "How a game is
tested"). session.sh moves the game's settings and data directories aside before the game starts and moves the
player's own back afterwards. The user asked for it (2026-10-26: "Test harness should reset game settings
then on start (restore after test)").

  freshhome.py aside STATE_DIR GAME PATH...   before the game: first restores what an earlier session left
                                              set aside (a crash, a kill, a reboot), with a warning; then
                                              renames each PATH that exists to PATH.walnut-aside-<stamp>.
                                              PATH is relative to $HOME (globs allowed: .local/share/warzone2100*).
                                              Prints the plan file, then bench.txt lines
  freshhome.py restore PLAN RUN_DIR           after the game: the session's fresh copy of each PATH goes to
                                              RUN_DIR/fresh-home/ (up to EVIDENCE_MAX in all, else renamed to
                                              PATH.walnut-fresh-<stamp>), the player's own renamed back
  freshhome.py recover STATE_DIR              the first step of aside, by itself

Lossless by construction:
- Only renames on the same filesystem (a sibling: instant, even for a big world), never a delete of the
  player's files, never a write over anything: a rename whose target exists is not done (that PATH is left
  as it is, and the session runs on the player's copy of it, with a warning).
- The plan (each PATH and its aside name) is written to STATE_DIR/<stamp>-<game>.json before the first
  rename, so a session cut short at any point is put back at the next session start (or by `recover`).
- The session's own fresh files are evidence, not the player's: a small set is moved into the run directory,
  a bigger one is renamed next to the original (PATH.walnut-fresh-<stamp>, kept, never deleted).
- PATH must be under $HOME and not $HOME itself; a symlink is moved as a link, never followed.
Lines on stdout are for bench.txt: fresh_home=<n set aside>, fresh_home_new=<n the game created>,
fresh_home_warn=<what went wrong>, recovered=<n>."""
import glob
import json
import os
import shutil
import sys
import time

EVIDENCE_MAX = 5 * 1024 * 1024


def stamp():
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def warn(msg):
    print("freshhome: " + msg, file=sys.stderr)


def size(path, limit):
    """the bytes under path, stopping early past limit"""
    if os.path.islink(path) or os.path.isfile(path):
        return os.lstat(path).st_size
    total = 0
    for d, _, files in os.walk(path):
        for f in files:
            try:
                total += os.lstat(os.path.join(d, f)).st_size
            except OSError:
                pass
            if total > limit:
                return total
    return total


def free_name(base):
    """base, or base.2, base.3...: a name nothing has"""
    name, i = base, 1
    while os.path.lexists(name):
        i += 1
        name = "%s.%d" % (base, i)
    return name


def write_plan(path, plan):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(plan, f, indent=1)
    os.replace(tmp, path)


def put_back(plan, run_dir=None):
    """the player's copies back where they were; the session's fresh ones to run_dir (small) or renamed next to
    them. Returns (fresh ones found, warnings)"""
    fresh, warns, used = 0, [], 0
    for m in plan["moves"]:
        path, aside = m["path"], m.get("aside")
        if os.path.lexists(path):          # the game made a fresh one
            fresh += 1
            rel = os.path.relpath(path, plan["home"])
            n = size(path, EVIDENCE_MAX)
            if run_dir and used + n <= EVIDENCE_MAX:
                used += n
                dst = free_name(os.path.join(run_dir, "fresh-home", rel.replace("/", "%")))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.move(path, dst)      # the session's files, not the player's: a copy across filesystems
            else:
                os.rename(path, free_name("%s.walnut-fresh-%s" % (path, plan["stamp"])))
        if aside and os.path.lexists(aside):
            if os.path.lexists(path):
                warns.append("%s is in the way of its own copy %s: both kept" % (path, aside))
                continue
            os.rename(aside, path)
    return fresh, warns


def recover(state_dir):
    """put back what earlier sessions left set aside; the number of plans recovered"""
    n = 0
    if not os.path.isdir(state_dir):
        return 0
    for name in sorted(os.listdir(state_dir)):
        if not name.endswith(".json"):
            continue
        p = os.path.join(state_dir, name)
        try:
            with open(p) as f:
                plan = json.load(f)
        except (OSError, ValueError):
            warn("unreadable plan %s: left for a person to look at" % p)
            continue
        pending = [m for m in plan["moves"] if m.get("aside") and os.path.lexists(m["aside"])]
        if pending:
            warn("a session of %s (%s) didn't restore its game's directories: putting back %s" % (
                plan.get("game"), plan.get("stamp"), ", ".join(m["path"] for m in pending)))
            n += 1
        _, warns = put_back(plan)
        for w in warns:
            warn(w)
        if not warns:
            os.rename(p, p + ".done")       # our own file: kept as a record, out of the way
    return n


def aside(state_dir, game, paths, home=None):
    home = os.path.realpath(home or os.path.expanduser("~"))
    os.makedirs(state_dir, exist_ok=True)
    out = ["recovered=%d" % recover(state_dir)]
    st = stamp()
    plan = {"game": game, "stamp": st, "home": home, "moves": []}
    warns = []
    expanded = []
    for p in paths:     # after the recovery: a glob mustn't catch an earlier session's leftovers
        full = os.path.join(home, p)
        if glob.has_magic(p):
            expanded += [x for x in sorted(glob.glob(full)) if ".walnut-aside-" not in x and ".walnut-fresh-" not in x]
        else:
            expanded.append(full)
    for p in dict.fromkeys(expanded):
        ap = os.path.abspath(p)
        if os.path.commonpath([ap, home]) != home or ap == home:
            warns.append("%s is not under the home directory: left alone" % p)
            continue
        m = {"path": ap}
        if os.path.lexists(ap):
            m["aside"] = "%s.walnut-aside-%s" % (ap, st)
            if os.path.lexists(m["aside"]):
                warns.append("%s exists already: %s left as it is" % (m["aside"], ap))
                continue
        plan["moves"].append(m)
    pfile = os.path.join(state_dir, "%s-%s.json" % (st, game))
    write_plan(pfile, plan)                 # the plan first: a kill from here on is recovered
    moved = 0
    for m in plan["moves"]:
        if "aside" in m:
            os.rename(m["path"], m["aside"])
            moved += 1
    out.append("fresh_home=%d" % moved)
    if warns:
        out.append("fresh_home_warn=%s" % "; ".join(warns))
        for w in warns:
            warn(w)
    return [pfile] + out


def restore(plan_file, run_dir):
    with open(plan_file) as f:
        plan = json.load(f)
    fresh, warns = put_back(plan, run_dir)
    out = ["fresh_home_new=%d" % fresh]
    if warns:
        out.append("fresh_home_warn=%s" % "; ".join(warns))
    else:
        os.rename(plan_file, plan_file + ".done")
    return out


def main(argv):
    if len(argv) >= 4 and argv[1] == "aside":
        print("\n".join(aside(argv[2], argv[3], argv[4:])))
        return 0
    if len(argv) == 4 and argv[1] == "restore":
        print("\n".join(restore(argv[2], None if argv[3] == "-" else argv[3])))
        return 0
    if len(argv) == 3 and argv[1] == "recover":
        print("recovered=%d" % recover(argv[2]))
        return 0
    print(__doc__.split("\n\n")[0], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
