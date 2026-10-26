#!/usr/bin/env python3
"""playdef.py TEST.TOML: a game's played test (gamedb/games/<id>/rating/test.toml, gamedb/README.md
"games/<id>/rating/") as shell assignments for session.sh and client.sh (eval "$(python3 -I playdef.py
FILE)"), every value shlex-quoted; the test is data, nothing in it is run here. Exit 2 with the problems
on stderr when the file doesn't parse or breaks the schema (KEYS, a closed set: a typo fails the session
instead of vanishing); tools/gamedb/check.py runs the same validate().

  TEST_PROCESS    process    the game's process name (pgrep -x: peak RSS, the per-client VRAM, play-test.sh stop)
  TEST_HOME=()    home       its settings and data directories under ~ (relative, globs allowed): a session
                             sets them aside and runs the game on fresh defaults (freshhome.py)
  TEST_PLAY_MARK  play_mark  a regex on the game's output: the first gameplay frame (bench.txt play_from=mark)
  TEST_LOAD_MARK  load_mark  a regex on the game's output: the load's end (play_from=load without a play mark)
  TEST_COMMAND=() command    the command a player starts, absolute path first: the session's command when the
                             game has no menu launcher (a launcher's Exec wins)
  TEST_ENV=()     env        NAME=value: the game's environment in a session (instead of the recipe's ENV,
                             the benchmark's switches)
  TEST_SOUND_AT   sound_at   seconds from the start when the sound card's state is read
  TEST_HUD        hud        1: the Gallium HUD's frame times (default), 0: none
Keys left out come out empty (TEST_HUD 1). TEST_FILE is the file read.
"""
import re
import shlex
import sys
import tomllib

# key: (type, required)
KEYS = {"process": (str, True), "home": (list, True), "play_mark": (str, False), "load_mark": (str, False),
        "command": (list, False), "env": (list, False), "sound_at": (int, False), "hud": (bool, False)}
# directories a home entry may never be (setting one aside would take every game's settings with it)
SHARED = {".config", ".local", ".local/share", ".cache", ".var", ".var/app"}
ENV_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*=.*\Z", re.S)
PROCESS_RE = re.compile(r"[^\s/]+\Z")


def validate(t):
    """the problems of a parsed test.toml (a dict), [] when it is fine"""
    errs = []
    for k in t:
        if k not in KEYS:
            errs.append("unknown key %r (test.toml has %s)" % (k, ", ".join(KEYS)))
    for k, (typ, req) in KEYS.items():
        if k not in t:
            if req:
                errs.append("%s is missing" % k)
            continue
        v = t[k]
        if not isinstance(v, typ) or (typ is int and isinstance(v, bool)):
            errs.append("%s should be %s" % (k, typ.__name__))
            continue
        if typ is list and not all(isinstance(x, str) and x for x in v):
            errs.append("%s is a list of strings" % k)
            continue
        if k == "process" and not PROCESS_RE.match(v):
            errs.append("process %r: a process name (no spaces, no /)" % v)
        elif k in ("play_mark", "load_mark"):
            try:
                re.compile(v)
            except re.error as e:
                errs.append("%s %r: %s" % (k, v, e))
            if not v:
                errs.append("%s is empty (leave it out)" % k)
        elif k == "home":
            for h in v:
                parts = h.split("/")
                if h.startswith(("/", "~")) or ".." in parts or "." in parts or "" in parts:
                    errs.append("home %r: a path relative to ~ and inside it" % h)
                elif h in SHARED:
                    errs.append("home %r: a directory every game shares, not the game's own" % h)
        elif k == "command":
            if not v or not v[0].startswith("/"):
                errs.append("command: the program first, as an absolute path")
        elif k == "env":
            for e in v:
                if not ENV_RE.match(e):
                    errs.append("env %r: NAME=value" % e)
        elif k == "sound_at" and v < 0:
            errs.append("sound_at: seconds, 0 or more")
    return errs


def assignments(t, path):
    """the shell lines for a valid test"""
    q = shlex.quote
    arr = lambda xs: "(" + " ".join(q(x) for x in xs) + ")"
    return "\n".join([
        "TEST_FILE=" + q(str(path)),
        "TEST_PROCESS=" + q(t.get("process", "")),
        "TEST_HOME=" + arr(t.get("home", [])),
        "TEST_PLAY_MARK=" + q(t.get("play_mark", "")),
        "TEST_LOAD_MARK=" + q(t.get("load_mark", "")),
        "TEST_COMMAND=" + arr(t.get("command", [])),
        "TEST_ENV=" + arr(t.get("env", [])),
        "TEST_SOUND_AT=" + q(str(t["sound_at"]) if "sound_at" in t else ""),
        "TEST_HUD=" + ("0" if t.get("hud") is False else "1"),
    ]) + "\n"


def main(argv):
    if len(argv) != 1 or argv[0].startswith("-"):
        sys.exit(__doc__)
    try:
        with open(argv[0], "rb") as f:
            t = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        print("playdef: %s: %s" % (argv[0], e), file=sys.stderr)
        return 2
    errs = validate(t)
    for e in errs:
        print("playdef: %s: %s" % (argv[0], e), file=sys.stderr)
    if errs:
        return 2
    sys.stdout.write(assignments(t, argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
