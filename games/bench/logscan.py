#!/usr/bin/env python3
"""logscan.py [options] [run dir | build/bench dir]...: errors and warnings in the logs of games/bench runs.

Scans game.log, out.log, dmesg.log, Xorg.0.log and wm.log of each run directory (default: every run
in build/bench) plus the facts bench.txt carries (rc, ended, sound, underruns), normalises the
messages (timestamps, numbers, addresses and the run's own paths stripped), and prints per game
(bench.txt game=) every distinct message with its severity class, the number of runs it appears in,
its total count, the first and last run, and a trend flag against --since:

  NEW      only in runs from --since on, and the game has runs from before (a regression candidate)
  GONE     only in runs before --since, and the game has runs from after it (fixed, or not run again)
  always   in every run of the game
  some     in some runs, before and after
  -        the game has no runs on one side of --since: no baseline

Severity classes, worst first: crash, oom, rsx (ps3rsx in dmesg: out of VRAM/GART, rejected push,
graph exceptions), nouveau (nv30/Mesa), gl (shader compile/link, fallbacks, missing GL), audio
(ALSA/OpenAL underruns, "buffer size too low"), sdl, script (Lua/Python), x (X errors), missing
(files and assets), game (anything else that looks like an error or warning).

  logscan.py                          every run in build/bench, per game
  logscan.py --since 20261019T09      trend flag against another stamp
  logscan.py -g sauerbraten -c audio  one game, one class (-g and -c repeat or take commas)
  logscan.py --by-message             one block per message across games instead of per game
  logscan.py --new                    only the NEW messages
  logscan.py --min-class gl           only gl and worse
  logscan.py -v                       list every run that has each message
  logscan.py --show-ignored           what the ignore list swallowed, with counts
  logscan.py --json                   the findings as JSON
  logscan.py DIR...                   run directories (or directories of them); stamps like
                                      ps3-bench-20261019T104123Z-openttd-base
The game.log lines also appear in out.log (with a time prefix); a message found in both is counted
once, from game.log.
"""
import argparse
import collections
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BENCH = HERE.parent.parent / "build" / "bench"
SINCE_DEFAULT = "20261019T09"     # runs from here on have Mesa ps3.544

CLASSES = ["crash", "oom", "rsx", "nouveau", "gl", "audio", "sdl", "script", "x", "missing", "game"]
FILES = ["game.log", "out.log", "dmesg.log", "Xorg.0.log", "wm.log"]

# --- what counts as a finding ---------------------------------------------------------------
# A line is a candidate when it matches CANDIDATE. dmesg.log lines are all candidates (the kernel
# said something during the run) except the ignored ones.
CANDIDATE = re.compile(
    r"error|warn|fail|segmentation|cannot|can't|couldn't|could not|unable|not found|missing|no such|underrun|xrun|"
    r"timeout|timed out|killed|oom|segfault|signal|abort|exception|reject|denied|invalid|out of |"
    r"assert|panic|\bbug\b|\(EE\)|\(WW\)|broken pipe|too (low|old)|deprecated|fallback|falling back|"
    r"draw module|tgsi|software rast|swrast|llvmpipe|no memory|not supported|unsupported|corrupt|"
    r"stall|lag peaked|took \d+ms", re.I)

# first match wins; order = severity
RULES = [
    ("crash", re.compile(r"SIGSEGV|SIGABRT|SIGBUS|SIGILL|segfault|core dumped|Segmentation|assertion|"
                         r"Traceback|panic|(?-i:BUG:)|stack smashing|double free|corrupted|_client\[\d+\]: code:|"
                         r"Received signal (?!15\b)|terminate called|fatal", re.I)),
    ("rsx", re.compile(r"ps3rsx|out of (VRAM|GART)|rejected push|validator|graph exception|GART", re.I)),
    ("oom", re.compile(r"out of memory|\boom\b|oom-kill|Killed process|page allocation failure|Cannot allocate memory|"
                       r"bad_alloc|memory allocation failed|no memory", re.I)),
    ("nouveau", re.compile(r"nouveau|nv30|nv40|nvfx|nv_|DRM_|drm\b|libdrm", re.I)),
    ("gl", re.compile(r"shader|glsl|GL_[A-Z0-9_]+ not found|OpenGL|\bGL\b|gl[A-Z]\w+\(|tgsi|out of temps|"
                      r"draw module|swrast|llvmpipe|fixed pipeline|Core context", re.I)),
    ("audio", re.compile(r"ALSA|ALSOFT|OpenAL|underrun|xrun|buffer size|pipewire|pcm|sound|audio|mixer", re.I)),
    ("sdl", re.compile(r"\bSDL", re.I)),
    ("script", re.compile(r"\blua\b|\.lua|script|python|TileDef|User Config", re.I)),
    ("x", re.compile(r"X Error|XIO|BadAlloc|BadMatch|BadValue|Xlib|X11|Xorg|xkb|\(EE\)", re.I)),
    ("missing", re.compile(r"No such file|not found|Couldn't open|Can't open|could not open|failed to load|can't load|"
                           r"Could not find|Couldn't find|missing|cannot open|unable to open", re.I)),
]
DEFAULT_CLASS = "game"

# --- the ignore list: known-harmless noise, one reason each. Keep it short. ------------------
IGNORE = [
    (r"xkbcomp|XKEYBOARD keymap|Could not resolve keysym|Symbol map for key|Multiple symbols for level|"
     r"\(WW\) warning, \(EE\) error", "Xorg's keymap compiler complains about keysyms it doesn't know; not fatal"),
    (r"pw\.conf .*client\.conf", "pipewire client library: no client.conf, built-in defaults are used"),
    (r"Failed to create PipeWire event context", "OpenAL Soft tries PipeWire first, then falls back to ALSA"),
    (r"Openbox-Message: Unable to find a valid menu file", "no root menu in the bare bench session"),
    (r"VGA arbiter|falling back to /sys/devices|Platform probe for", "Xorg probing on a platform GPU with no PCI"),
    (r"Kernel command line", "the boot line, repeated in every log"),
    (r"input: Logitech", "the receiver re-enumerates; not a game message"),
    (r"kmalloc-|^\s*(unevictable|slab_reclaimable|sec_pagetables)", "the slab/mem dump that follows an allocation failure; the failure line itself is kept"),
    (r"GL_EXTENSIONS|GL extensions:|^bench:|bench: ", "extension lists and the harness's own output"),
    (r"Client Shutdown \(Received signal 15\)", "Quake 3 engines report the harness's SIGTERM at the end of the run"),
    (r"nv30-(sysmem|forcedxt)", "our driver trace lines, not errors"),
    (r"\]\s+\[\s*(pid\s*\]|\d+\])|oom_kill_process|Tasks state|Mem-Info|Node \d+ |active_anon|Hardware name|Call Trace|^\s*\S+\+0x", "the kernel's OOM report: task table and backtrace; the oom-killer lines themselves are kept"),
    (r"callbacks suppressed", "rate-limit footer; the suppressed line is kept"),
    (r"psbench: mem tick|ACTION\[", "luanti's harness mod ticks"),
    (r'\(WW\) The directory "/usr/share/fonts', "Xorg's default font path names directories this image doesn't have; no game uses X core fonts"),
    (r"\b0 timeouts, 0 errors", "SPU synth's statistics line with nothing wrong in it (a non-zero one is kept)"),
    (r"Crash caused by signal #15|submit a bug report|We crashed", "Crawl's handler reporting the harness's SIGTERM at the end of the run"),
]
IGNORE = [(re.compile(p, re.I), why) for p, why in IGNORE]

# --- normalisation ----------------------------------------------------------------------------
ANSI = re.compile(r"\x1b?\[[0-9;]*m")
CARET = re.compile(r"\^[0-9a-z]")                       # Quake colour codes
TS_LEAD = re.compile(r"^\s*(\[\s*[\d.]+\]\s*)?(\d+\.\d+\s+)?")   # "[  12.3] " kernel, "12.3 " out.log
TS_DATE = re.compile(r"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d:?")
TS_PW = re.compile(r"\[\d+:\d+:\d+\.\d+\]")
HEX = re.compile(r"0x[0-9a-f]+|\b[0-9a-f]{8,}\b", re.I)
RUNPATH = re.compile(r"/var/tmp/ps3-bench/[\w.\-]+")
NUM = re.compile(r"(?<![A-Za-z_\d])\d+(\.\d+)?")
SP = re.compile(r"\s+")


def normalise(line):
    s = ANSI.sub("", line.rstrip("\n"))
    s = CARET.sub("", s)
    s = TS_LEAD.sub("", s, count=1)
    s = TS_DATE.sub("", s)
    s = TS_PW.sub("", s)
    s = RUNPATH.sub("<run>", s)
    s = HEX.sub("ADDR", s)
    s = NUM.sub("N", s)
    s = SP.sub(" ", s).strip()
    i = s.find("ALSA lib")
    if i > 0:
        s = s[i:]                       # progress dots or other text printed on the same line
    return s.lstrip(". ")[:300]


def classify(msg):
    for cls, rx in RULES:
        if rx.search(msg):
            return cls
    return DEFAULT_CLASS


def ignored(raw):
    for rx, why in IGNORE:
        if rx.search(raw):
            return why
    return None


# --- one run ------------------------------------------------------------------------------------
def read_kv(path):
    kv = {}
    try:
        for line in open(path, errors="replace"):
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                kv.setdefault(k, v)
    except OSError:
        pass
    return kv


def run_stamp(d):
    m = re.search(r"(\d{8}T\d{6}Z?)", d.name)
    return m.group(1) if m else d.name


def run_game(d, kv):
    g = kv.get("game")
    if g:
        return g
    m = re.search(r"\d{8}T\d{6}Z-(.+?)-[^-]+$", d.name)
    g = m.group(1) if m else d.name
    return g[6:] if g.startswith("smoke-") else g       # smoke runs of the same game count with it


def scan_run(d, ignored_out=None):
    """-> {(class, msg): count}, summary facts"""
    kv = read_kv(d / "bench.txt")
    found = collections.Counter()
    seen_game = set()      # normalised messages game.log already counted
    for fn in FILES:
        p = d / fn
        if not p.is_file():
            continue
        try:
            lines = open(p, errors="replace").read().splitlines()
        except OSError:
            continue
        for raw in lines:
            if len(raw) > 1500:
                continue
            body = TS_LEAD.sub("", raw, count=1) if fn == "out.log" else raw
            if fn != "dmesg.log" and not CANDIDATE.search(raw):
                continue
            why = ignored(body)
            if why:
                if ignored_out is not None:
                    ignored_out[(normalise(raw), why)] += 1
                continue
            msg = normalise(raw)
            if not msg or len(msg) < 4:
                continue
            if fn == "dmesg.log" and not CANDIDATE.search(raw) and "ps3rsx" not in raw:
                continue
            if fn == "game.log":
                seen_game.add(msg)
            elif fn == "out.log" and msg in seen_game:
                continue
            cls = classify(msg)
            if fn == "dmesg.log" and cls == DEFAULT_CLASS:
                cls = "game"
            found[(cls, msg)] += 1
    # facts from bench.txt
    rc = kv.get("rc", "")
    ended = kv.get("ended", "")
    if rc in ("134", "139", "132", "135", "136", "138"):
        found[("crash", "bench.txt: game died with rc=%s (%s)" % (
            rc, {"134": "SIGABRT", "139": "SIGSEGV", "132": "SIGILL", "135": "SIGBUS", "136": "SIGFPE",
                 "138": "SIGBUS"}[rc]))] += 1
    elif ended == "exit" and rc not in ("", "0"):
        found[("game", "bench.txt: game exited early with rc=%s" % rc)] += 1
    if ended == "killed":
        found[("game", "bench.txt: run ended=killed (harness killed it)")] += 1
    snd = kv.get("sound", "")
    if snd and snd not in ("RUNNING", "n/a", "none"):
        found[("audio", "bench.txt: sound card state %s (not RUNNING)" % snd)] += 1
    try:
        if int(kv.get("underruns", "0") or 0) > 0:
            found[("audio", "bench.txt: underruns>0 (ALSA xruns during the run)")] += int(kv["underruns"])
    except ValueError:
        pass
    return found, kv


# --- aggregation ----------------------------------------------------------------------------------
def collect(dirs, ignored_out=None):
    games = collections.defaultdict(lambda: {"runs": {}, "msgs": collections.defaultdict(dict)})
    for d in dirs:
        found, kv = scan_run(d, ignored_out)
        g = run_game(d, kv)
        st = run_stamp(d)
        G = games[g]
        G["runs"][st] = {"dir": d.name, "variant": kv.get("variant", ""), "underruns": kv.get("underruns", "")}
        for (cls, msg), n in found.items():
            G["msgs"][(cls, msg)][st] = n
    return games


def trend(stamps, all_stamps, since):
    first, last = min(stamps), max(stamps)
    before = [s for s in all_stamps if s < since]
    after = [s for s in all_stamps if s >= since]
    if len(stamps) == len(all_stamps):
        return "always"
    if not before or not after:
        return "-"
    if first >= since:
        return "NEW"
    if last < since:
        return "GONE"
    return "some"


def rows_for(games, since, classes, min_rank, new_only):
    out = {}
    for g, G in sorted(games.items()):
        allst = sorted(G["runs"])
        rows = []
        for (cls, msg), per in G["msgs"].items():
            if classes and cls not in classes:
                continue
            if CLASSES.index(cls) > min_rank:
                continue
            tr = trend(list(per), allst, since)
            if new_only and tr != "NEW":
                continue
            rows.append({"class": cls, "msg": msg, "runs": len(per), "total": sum(per.values()),
                         "first": min(per), "last": max(per), "trend": tr, "stamps": sorted(per)})
        rows.sort(key=lambda r: (CLASSES.index(r["class"]), -r["runs"], -r["total"], r["msg"]))
        out[g] = (allst, rows)
    return out


def short(st):
    return st.rstrip("Z")[4:] if len(st) > 10 else st      # 1018T201750


def print_games(res, verbose, games):
    for g, (allst, rows) in res.items():
        if not allst:
            continue
        print("\n== %s: %d run(s), %s .. %s, %d distinct message(s)" % (
            g, len(allst), short(allst[0]), short(allst[-1]), len(rows)))
        for r in rows:
            print("  %-8s runs %3d/%-3d total %7d  %s..%s  %-6s %s" % (
                r["class"], r["runs"], len(allst), r["total"], short(r["first"]), short(r["last"]),
                r["trend"], r["msg"][:200]))
            if verbose:
                print("           in: " + " ".join(short(s) for s in r["stamps"]))


def print_by_message(res, verbose):
    agg = collections.defaultdict(list)
    for g, (allst, rows) in res.items():
        for r in rows:
            agg[(r["class"], r["msg"])].append((g, len(allst), r))
    order = sorted(agg.items(), key=lambda kv: (CLASSES.index(kv[0][0]), -len(kv[1]),
                                                -sum(x[2]["runs"] for x in kv[1]), kv[0][1]))
    for (cls, msg), lst in order:
        tot_runs = sum(x[2]["runs"] for x in lst)
        print("\n%-8s %d game(s), %d run(s): %s" % (cls, len(lst), tot_runs, msg[:220]))
        for g, n, r in lst:
            print("           %-14s runs %3d/%-3d total %7d  %s..%s  %s" % (
                g, r["runs"], n, r["total"], short(r["first"]), short(r["last"]), r["trend"]))
            if verbose:
                print("             in: " + " ".join(short(s) for s in r["stamps"]))


def find_dirs(args):
    roots = [Path(a) for a in args] or [BENCH]
    dirs = []
    for r in roots:
        if (r / "bench.txt").is_file() or any((r / f).is_file() for f in FILES):
            dirs.append(r)
        elif r.is_dir():
            dirs += sorted(x for x in r.iterdir()
                           if x.is_dir() and ((x / "bench.txt").is_file() or (x / "game.log").is_file()))
    return dirs


def main(argv=None):
    ap = argparse.ArgumentParser(description="errors and warnings in games/bench run logs")
    ap.add_argument("dirs", nargs="*")
    ap.add_argument("--since", default=SINCE_DEFAULT, help="stamp prefix splitting before/after (default %(default)s)")
    ap.add_argument("-g", "--game", action="append", default=[])
    ap.add_argument("-c", "--class", dest="classes", action="append", default=[])
    ap.add_argument("--min-class", choices=CLASSES, default=CLASSES[-1])
    ap.add_argument("--new", action="store_true", help="only NEW messages")
    ap.add_argument("--by-message", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--show-ignored", action="store_true")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    since = a.since
    dirs = find_dirs(a.dirs)
    ign = collections.Counter() if a.show_ignored else None
    games = collect(dirs, ign)
    want_g = {x for s in a.game for x in s.split(",")}
    if want_g:
        games = {g: G for g, G in games.items() if g in want_g}
    classes = {x for s in a.classes for x in s.split(",")}
    res = rows_for(games, since, classes, CLASSES.index(a.min_class), a.new)
    if a.json:
        print(json.dumps({g: {"runs": allst, "findings": rows} for g, (allst, rows) in res.items()}, indent=1))
    elif a.by_message:
        print_by_message(res, a.verbose)
    else:
        print("%d run(s) in %d game(s); trend flags against --since %s" % (len(dirs), len(res), since))
        print_games(res, a.verbose, games)
    if ign is not None:
        print("\nignored (count, reason, first example):")
        by = collections.defaultdict(lambda: [0, ""])
        for (msg, why), n in ign.items():
            by[why][0] += n
            by[why][1] = by[why][1] or msg
        for why, (n, ex) in sorted(by.items(), key=lambda x: -x[1][0]):
            print("  %7d  %s  [e.g. %s]" % (n, why, ex[:100]))
    return 0


# --- selftest (called by selftest.py) -----------------------------------------------------------------
def selftest(tmp, check):
    print("\nlog scanner (logscan.py)")
    root = Path(tmp) / "logscan"

    def mk(stamp, game, game_log="", dmesg="", rc="137", ended="window", underruns="0"):
        d = root / ("ps3-bench-%s-%s-base" % (stamp, game))
        d.mkdir(parents=True)
        (d / "bench.txt").write_text("game=%s\nvariant=base\nrc=%s\nended=%s\nunderruns=%s\nsound=RUNNING\n" % (
            game, rc, ended, underruns))
        (d / "game.log").write_text(game_log)
        (d / "out.log").write_text("".join("12.5 " + l + "\n" for l in game_log.splitlines())
                                   + "1.0 > Warning: Could not resolve keysym XF86Foo\n")
        (d / "dmesg.log").write_text(dmesg)
        (d / "Xorg.0.log").write_text("[  1.0] (WW) VGA arbiter: cannot open kernel arbiter\n")
        (d / "wm.log").write_text("")
    old = "[ALSOFT] (EE) Wait timeout... buffer size too low?\n"
    mk("20261019T080000Z", "gA", old)
    mk("20261019T081000Z", "gA", old + "ERROR: Shader 17 failed to link at 0x7fff1234\n")
    mk("20261019T091000Z", "gA", old + "ERROR: Shader 99 failed to link at 0x7fff9999\n"
       "\x1b[31m[error  ] Can't open file /tmp/x77.png\x1b[0m\n",
       dmesg="[ 77.1] ps3rsx: out of VRAM for a 4096 KiB BO\n[ 78.2] ps3rsx: out of VRAM for a 128 KiB BO\n")
    mk("20261019T092000Z", "gA", "Segmentation fault\n", rc="139", ended="exit", underruns="12")
    mk("20261019T093000Z", "gB", "all fine\n")
    games = collect(sorted(root.iterdir()))
    res = rows_for(games, "20261019T09", set(), len(CLASSES) - 1, False)
    rows = {(r["class"], r["msg"]): r for r in res["gA"][1]}
    shader = rows.get(("gl", "ERROR: Shader N failed to link at ADDR"))
    check("logscan: numbers and addresses normalise, the same shader error groups across runs",
          shader is not None and shader["runs"] == 2 and shader["total"] == 2, list(rows))
    check("logscan: a message in runs on both sides of --since is flagged 'some', one only after it 'NEW'",
          shader and shader["trend"] == "some" and
          rows[("rsx", "ps3rsx: out of VRAM for a N KiB BO")]["trend"] == "NEW",
          {k: v["trend"] for k, v in rows.items()})
    aud = rows.get(("audio", "[ALSOFT] (EE) Wait timeout... buffer size too low?"))
    check("logscan: game.log and out.log copies counted once; audio class", aud and aud["total"] == 3 and aud["runs"] == 3,
          aud)
    check("logscan: ignore list drops keysym/VGA arbiter noise",
          not any("keysym" in m or "VGA" in m for _, m in rows), list(rows))
    check("logscan: crash class from the log and from rc=139; underruns from bench.txt",
          ("crash", "Segmentation fault") in rows and ("crash", "bench.txt: game died with rc=N (SIGSEGV)".replace("N", "139")) in rows
          and ("audio", "bench.txt: underruns>0 (ALSA xruns during the run)") in rows, list(rows))
    check("logscan: a clean game has no findings", res["gB"][1] == [], res["gB"][1])
    check("logscan: colour codes and paths of numbers stripped",
          any(c == "missing" and m == "[error ] Can't open file /tmp/x77.png" for c, m in rows), list(rows))
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        main([str(root), "--since", "20261019T09", "-v"])
        main([str(root), "--by-message", "--json"])
    check("logscan: the command line runs (per game, verbose, by message, json)", "== gA" in buf.getvalue())


if __name__ == "__main__":
    sys.exit(main())
