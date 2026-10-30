"""walnut_rate.py: walnut-rate, played sessions for anyone with a Walnut PS3 (the walnut-rate package; docs/games/
SUITE.md "Played sessions", docs/GAMEDB.md "How a game is tested"). Python stdlib, run from a terminal of the
desktop as the player:

  walnut-rate list                      the games with a played test: installed or not, medal, sessions so far
  walnut-rate check [--close]           is the console clean for a session? (bench/harness/cleancheck.py)
  walnut-rate play GAME [LAUNCHER] [--close]
                                        the check, then the session exactly as session.sh runs it for us (fresh
                                        defaults, game mode, the recorder, screenshots), watched for interference
                                        (BENCH_WATCH=1), with the root reads through walnut-rate-helper
                                        (BENCH_ROOT_HELPER); then the terminal form and a summary
  walnut-rate export RUN DIR [--nick NAME] [--contact TEXT] [--publish yes|no] [--yes]
                                        the session as one file, DIR/<run>.tar: the run directory as
                                        tools/gamedb/import-run.py reads it (shots as PNG), the check's record,
                                        submitter.toml; scrubbed of the console's user, host and addresses
                                        first, its contents and sizes shown before anything is written
  walnut-rate status                    the sessions on this console and what became of them (exported, sent)
  walnut-rate submit [RUN] [--nick NAME] [--contact TEXT] [--publish yes|no] [--yes] [--again]
                                        the same bundle (RUN: default the newest), shown with its size, sent on
                                        your yes to walnut.nanobyte.dev (WALNUT_RATE_URL: elsewhere); the
                                        submission id it gets is kept with the run, walnut-rate status shows it

The package's tree (/usr/lib/walnut-rate): bench/ (harness/, lib/, games/<id>/: the same files make bench-push
copies to /var/tmp/bench for our own runs), evidence.py (tools/gamedb/'s: the scrub), walnut-rate-helper, version.
"""
import datetime
import getpass
import io
import json
import os
import pwd
import re
import shutil
import socket
import struct
import subprocess
import sys
import tarfile
import tempfile
import zlib
from pathlib import Path

LIB = Path(__file__).resolve().parent
BENCH = LIB / "bench"
HELPER = LIB / "walnut-rate-helper"
OUT = Path("/var/tmp/ps3-bench")
CATALOG = Path("/usr/share/walnut-games/catalog.json")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
RUN_RE = re.compile(r"^(?:ps3-bench-)?\d{8}T\d{6}Z-session-([a-z0-9][a-z0-9-]*)-([a-z0-9]+)$")   # ps3-bench.sh's names
LOG = "walnut-rate.log"         # in the run directory: what walnut-rate did with it (exports)
CHECK = "clean-check.txt"       # in the run directory: the check before the session
# besides evidence.RAW_FILES and RAW_GLOBS (what import-run.py reads) and the form: the reviewer's context
EXTRA_FILES = ("session.toml", CHECK, "watch.log", "processes-check.txt")
# never scrubbed as a name: words of the logs themselves (a user or host called so is left as it is)
GENERIC = {"root", "player", "host", "walnut", "ps3", "localhost", "debian", "user", "users", "admin", "test",
           "linux", "game", "games", "sound", "audio", "video", "input", "session", "bench"}


def _harness():
    for d in (BENCH / "harness", BENCH.parent / "tools" / "gamedb"):
        if str(d) not in sys.path:
            sys.path.insert(0, str(d))


def _evidence():
    _harness()
    if str(LIB) not in sys.path:
        sys.path.insert(0, str(LIB))
    import evidence
    return evidence


def version():
    try:
        return (LIB / "version").read_text().strip()
    except OSError:
        return "dev"


# ---- list ------------------------------------------------------------------------------------------

def catalog(path=None):
    try:
        return {g["id"]: g for g in json.loads(Path(path or CATALOG).read_text())["games"]}
    except (OSError, ValueError, KeyError, TypeError):
        return {}


def installed(packages):
    """{package: True|False} from dpkg (None for all when dpkg can't be asked)"""
    if not packages:
        return {}
    try:
        p = subprocess.run(["dpkg-query", "-W", "-f", "${Package}\t${db:Status-Abbrev}\n"] + sorted(set(packages)),
                           capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return {k: None for k in packages}
    st = dict(line.split("\t", 1) for line in p.stdout.splitlines() if "\t" in line)
    return {k: st.get(k, "").startswith("ii") for k in packages}


def games_list(bench=None, cat=None):
    """[(id, title, installed True|False|None, medal, sessions)] of the games with a played test and a recipe"""
    _harness()
    import games
    root = games.root()
    cat = catalog() if cat is None else cat
    ids = [g for g in games.dirs() if (root / g / "rating" / "test.toml").is_file()]
    inst = installed([p for g in ids for p in cat.get(g, {}).get("packages", [])[:1]])
    out = []
    for g in ids:
        c = cat.get(g, {})
        pk = c.get("packages", [])[:1]
        out.append((g, c.get("title", g), inst.get(pk[0]) if pk else None, (c.get("rating") or {}).get("medal") or "-",
                    sum(1 for t in c.get("tests", []) if t.get("kind") == "session")))
    return out


def cmd_list(args):
    rows = games_list()
    if not rows:
        print("walnut-rate: no games with a played test (the package's bench/games is empty?)")
        return 1
    w = max(len(r[0]) for r in rows)
    t = min(32, max(len(r[1]) for r in rows))
    print("%-*s  %-*s  %-9s  %-9s  %s" % (w, "GAME", t, "TITLE", "INSTALLED", "MEDAL", "SESSIONS"))
    for g, title, inst, medal, n in rows:
        print("%-*s  %-*s  %-9s  %-9s  %d" % (w, g, t, title[:t], {True: "yes", False: "no", None: "-"}[inst], medal, n))
    print("\nwalnut-rate play GAME  plays one (install it first: Walnut Games, or sudo apt install)")
    return 0


# ---- check -----------------------------------------------------------------------------------------

def run_check(close=False, out=sys.stdout):
    _harness()
    import cleancheck
    again = lambda: cleancheck.check(os.getpid(), str(BENCH), str(HELPER), str(OUT))
    f = again()
    if close:
        f = cleancheck.close_and_check(f, again, out=out)
    buf = io.StringIO()
    ready = cleancheck.report(f, buf)
    out.write(buf.getvalue())
    return ready, buf.getvalue()


def cmd_check(args):
    close = "--close" in args
    if [a for a in args if a != "--close"]:
        return usage()
    print("walnut-rate check: looking at the console (a few seconds)")
    ready, _ = run_check(close)
    if not ready and not close:
        print("(walnut-rate check --close asks the windows and programs above to close)")
    return 0 if ready else 1


# ---- play ------------------------------------------------------------------------------------------

def session_env(run_file, ps3_bench):
    """the session's environment: the player's, without any harness switch of theirs, plus ours"""
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("BENCH_", "SESSION_", "GALLIUM_HUD", "WALNUT_FORM")) and k != "PS3_BENCH"}
    env.update(BENCH_ROOT_HELPER=str(HELPER), BENCH_WATCH="1", SESSION_FORM="0", SESSION_RUN_FILE=str(run_file),
               PS3_BENCH=ps3_bench)
    return env


def kv(path):
    out = {}
    try:
        for line in Path(path).read_text(errors="replace").splitlines():
            k, _, v = line.partition("=")
            out[k.strip()] = v.strip()
    except OSError:
        pass
    return out


def minutes(s):
    try:
        s = float(s)
    except (TypeError, ValueError):
        return "?"
    return "%d min %02d s" % (s // 60, s % 60)


def summary(run, out=sys.stdout):
    b = kv(Path(run) / "bench.txt")
    shots = [line for line in (Path(run) / "bench.txt").read_text(errors="replace").splitlines()
             if line.startswith(("shot=", "shot_mark="))] if (Path(run) / "bench.txt").exists() else []
    marks = sum(1 for s in shots if s.startswith("shot_mark="))
    p = lambda s="": print(s, file=out)
    p("== the session: %s, %s of play, ended: %s%s" % (b.get("game", "?"), minutes(b.get("play_seconds")),
                                                      b.get("ended", "?"), " (%s)" % b["ended_why"] if b.get("ended_why") else ""))
    rec = ["frame times" if (Path(run) / "hud").is_dir() else "no frame times (not a GL game?)",
           "%d screenshots (%d of yours)" % (len(shots), marks),
           "sound: %s audible dropouts" % b.get("xruns_audible", b.get("underruns", "?")),
           "the input devices" if (Path(run) / "input.tsv").exists() else "no input log"]
    p("   recorded: " + ", ".join(rec))
    p("   the form: " + ("saved" if (Path(run) / "session.toml").exists() else
                         "not saved (answer it later: python3 -I %s --terminal %s %s base)" %
                         (BENCH / "harness" / "session-form.py", run, b.get("game", "GAME"))))
    if b.get("interfered") == "1" or b.get("unexpected"):
        p("!! the session was disturbed and can't be rated: %s" %
          "; ".join(x for x in (b.get("interference"), b.get("unexpected") and "unexpected processes: " + b["unexpected"]) if x))
    if b.get("temp_stop") == "1" or (Path(run) / "THERMAL").exists():
        p("!! stopped by the temperature limit: let the PS3 cool down")
    p("   the run: %s" % run)
    p("next: walnut-rate submit  sends it to us (it shows what goes in first and asks), or walnut-rate export %s DIR\n"
      "      writes it to DIR (a USB stick, for example)" % Path(run).name)
    return b


def cmd_play(args):
    close = "--close" in args
    args = [a for a in args if a != "--close"]
    if not 1 <= len(args) <= 2:
        return usage()
    game = args[0]
    if not ID_RE.match(game):
        print("walnut-rate: not a game id: %r (walnut-rate list)" % game[:64])
        return 2
    if game not in [r[0] for r in games_list(cat={})]:
        print("walnut-rate: %s has no played test (walnut-rate list)" % game)
        return 2
    _harness()
    import cleancheck
    print("walnut-rate play %s: the check first (a few seconds)" % game)
    ready, record = run_check(close)
    if not ready:
        print("walnut-rate: not starting; fix the above, then walnut-rate play %s again%s" %
              (game, "" if close else " (--close asks them to close)"))
        return 1
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    record = "# walnut-rate %s check before the session, %s\n%s" % (version(), stamp, record)
    print("\nThe game starts now. Play it normally and quit it from its own menu when you are done: there is no time\n"
          "limit (ten minutes or more is good). To show something (a glitch): Alt+F12 on the keyboard, or Select+Start\n"
          "on the gamepad, takes a screenshot. Don't use the console for anything else meanwhile, and no SSH: that\n"
          "spoils the session. The form follows here when the game is over.\n")
    with tempfile.TemporaryDirectory(prefix="walnut-rate-") as tmp:
        rf = Path(tmp) / "run"
        cmd = [str(BENCH / "harness" / "session.sh"), game] + args[1:]
        rc = subprocess.call(cmd, env=session_env(rf, cleancheck.ps3_bench() or "ps3-bench.sh"))
        run = rf.read_text().strip() if rf.exists() else ""
    if not run or not Path(run).is_dir():
        print("walnut-rate: the session made no run (session.sh exit %d; its log: ~/.cache/ps3-session/)" % rc)
        return 1
    Path(run, CHECK).write_text(record)
    print()
    subprocess.call([sys.executable, "-I", str(BENCH / "harness" / "session-form.py"), "--terminal", run, game, "base"])
    print()
    summary(run)
    return 0


# ---- status ----------------------------------------------------------------------------------------

def runs(out_dir=None):
    d = Path(out_dir or OUT)
    return sorted((p for p in d.glob("*-session-*") if p.is_dir() and RUN_RE.match(p.name)), key=lambda p: p.name) \
        if d.is_dir() else []


def cmd_status(args):
    if args:
        return usage()
    rr = runs()
    if not rr:
        print("walnut-rate: no sessions on this console yet (walnut-rate play GAME)")
        return 0
    print("%-34s  %-14s  %-12s  %-6s  %-5s  %s" % ("RUN", "GAME", "PLAY", "ENDED", "FORM", "STATE"))
    for r in rr:
        b = kv(r / "bench.txt")
        state = []
        if b.get("interfered") == "1" or b.get("unexpected"):
            state.append("disturbed: not rated")
        exp = [line for line in (r / LOG).read_text(errors="replace").splitlines() if line.startswith("exported ")] \
            if (r / LOG).exists() else []
        sub = submissions(r)
        if sub:
            state.append("submitted as %s (%s)" % (sub[-1][1], sub[-1][0]))
        state.append(exp[-1] if exp else "not exported" if not sub else None)
        print("%-34s  %-14s  %-12s  %-6s  %-5s  %s" % (r.name, b.get("game", "?"), minutes(b.get("play_seconds")),
                                                     b.get("ended", "?"), "yes" if (r / "session.toml").exists() else "no",
                                                     "; ".join(x for x in state if x)))
    return 0


# ---- export ----------------------------------------------------------------------------------------

def resolve_run(name, out_dir=None):
    if name == "last":
        rr = runs(out_dir)
        return rr[-1] if rr else None
    p = Path(name)
    if not p.is_dir():
        p = Path(out_dir or OUT) / name
    return p if p.is_dir() and (p / "bench.txt").is_file() else None


def addresses():
    """the console's own IP addresses (iproute2), loopback left out"""
    try:
        p = subprocess.run(["ip", "-o", "addr", "show"], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return set()
    out = set()
    for m in re.finditer(r"\binet6?\s+([0-9a-fA-F.:]+)(?:/\d+)?", p.stdout):
        a = m[1]
        if a not in ("127.0.0.1", "::1") and not a.startswith("127."):
            out.add(a)
    return out


def run_words(run):
    """the words the run's structure is made of: bench.txt's keys and the versions file's package names. A user
    or host named like one of them is left as it is (scrubbing it would break the record)"""
    words = set()
    b = Path(run) / "bench.txt"
    if b.is_file():
        words |= {line.partition("=")[0].strip() for line in b.read_text(errors="replace").splitlines() if "=" in line}
    v = Path(run) / "versions"
    if v.is_file():
        for line in v.read_text(errors="replace").splitlines():
            if line.split():
                words.add(line.split()[0].removeprefix("data:"))
    return {w.lower() for w in words if w}


def console_identity(run=None):
    """(users, hosts, home, left): what the scrub replaces, made from the console itself: the player's login and
    full name, the host name, every address of this machine; home the player's home directory; left the
    names left as they are (GENERIC, or a word of the run's structure: run_words)"""
    pw = pwd.getpwuid(os.getuid())
    users = {pw.pw_name, getpass.getuser(), pw.pw_gecos.split(",")[0].strip()}
    hn = socket.gethostname()
    hosts = {hn, hn.split(".")[0]}
    try:
        hosts.add(Path("/etc/hostname").read_text().strip())
    except OSError:
        pass
    voc = run_words(run) if run else set()
    left = set()

    def keep(names):
        out = set()
        for x in names:
            if not x or len(x) < 3:
                continue
            if x.lower() in GENERIC or x.lower() in voc:
                left.add(x)
                continue
            out.add(x)
        return out
    users, hosts = keep(users), keep(hosts) | addresses()
    return tuple(sorted(users)), tuple(sorted(hosts)), str(Path.home()), sorted(left)


def scrub_text(text, ident, home=None):
    ev = _evidence()
    if home and not home.startswith("/home/") and home not in ("/", ""):
        text = text.replace(home.rstrip("/") + "/", "~/").replace(home.rstrip("/"), "~")
    return ev.scrub(text, ident)


def xwd_to_png(src, dst, level=6):
    """an X window dump (xwd: 24 or 32 bits per pixel TrueColor, either byte order) as a PNG; stdlib only.
    Raises ValueError for any other kind"""
    data = Path(src).read_bytes()
    if len(data) < 100:
        raise ValueError("%s: too short for an xwd" % src)
    h = struct.unpack(">25I", data[:100])
    hsize, version, fmt, w, ht = h[0], h[1], h[2], h[4], h[5]
    byte_order, bpp, bpl, vclass = h[7], h[11], h[12], h[13]
    masks, ncolors = h[14:17], h[19]
    if version != 7 or fmt != 2 or bpp not in (24, 32) or vclass not in (4, 5) or not w or not ht:
        raise ValueError("%s: an xwd this can't read (version %d, format %d, %d bpp, visual class %d)" %
                         (src, version, fmt, bpp, vclass))
    off = hsize + 12 * ncolors
    n = bpp // 8
    if len(data) < off + bpl * ht or bpl < w * n:
        raise ValueError("%s: truncated" % src)
    idx = []
    for m in masks:
        sh = (m & -m).bit_length() - 1
        if m >> sh != 0xff or sh % 8:
            raise ValueError("%s: colour masks %s aren't whole bytes" % (src, [hex(x) for x in masks]))
        idx.append(sh // 8 if byte_order == 0 else n - 1 - sh // 8)
    img = data[off:off + bpl * ht]
    if bpl != w * n:
        img = b"".join(img[y * bpl:y * bpl + w * n] for y in range(ht))
    rgb = bytearray(w * ht * 3)
    for c in range(3):
        rgb[c::3] = img[idx[c]::n]
    row = w * 3
    raw = b"".join(b"\0" + bytes(rgb[y * row:(y + 1) * row]) for y in range(ht))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, ht, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, level)) + chunk(b"IEND", b"")
    Path(dst).write_bytes(png)
    return w, ht


def shot_files(run):
    """the run's screenshots as bench.txt lists them (shot= and shot_mark=): [the file as recorded, relative]"""
    out = []
    for line in (Path(run) / "bench.txt").read_text(errors="replace").splitlines():
        if line.startswith(("shot=", "shot_mark=")):
            parts = line.split("=", 1)[1].split(":")
            if len(parts) >= 2 and parts[1] and ".." not in parts[1].split("/") and not parts[1].startswith("/"):
                out.append(parts[1])
    return list(dict.fromkeys(out))


def plan_bundle(run):
    """[(rel, kind, source)] of what goes into the bundle: kind text (scrubbed), image (copied), xwd (to PNG)"""
    ev = _evidence()
    run = Path(run)
    rels = [r for r in ev.RAW_FILES + EXTRA_FILES if (run / r).is_file()]
    for g in ev.RAW_GLOBS:
        rels += sorted(str(f.relative_to(run)) for f in run.glob(g) if f.is_file())
    out, total = [], 0
    for r in dict.fromkeys(rels):
        data = (run / r).read_bytes()
        if len(data) <= ev.RAW_MAX and ev.is_text(data):
            out.append((r, "text", run / r))
    fresh = run / "fresh-home"
    for f in sorted(fresh.rglob("*")) if fresh.is_dir() else []:
        if f.is_file() and not f.is_symlink():
            data = f.read_bytes()
            if len(data) <= ev.FRESH_MAX and ev.is_text(data) and total + len(data) <= ev.FRESH_TOTAL:
                total += len(data)
                out.append((str(f.relative_to(run)), "text", f))
    for rel in shot_files(run):
        p = run / rel
        for c in (p, p.with_suffix(".png"), p.with_suffix(".jpg")):
            if c.is_file() and not c.is_symlink():
                out.append((str(c.relative_to(run)), "xwd" if c.suffix == ".xwd" else "image", c))
                break
    return out


def ask(prompt, default=""):
    try:
        a = input(prompt).strip()
    except EOFError:
        return default
    return a or default


def human(n):
    for u in ("B", "KB", "MB"):
        if n < 1024 or u == "MB":
            return ("%d %s" % (n, u)) if u == "B" else "%.1f %s" % (n, u)
        n /= 1024


def submitter_toml(nick, contact, publish, run, game):
    ev = _evidence()
    return ev.toml_text({"nickname": nick, "contact": contact, "publish": publish, "walnut_rate": version(),
                         "exported": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                         "run": run, "game": game},
                        ["walnut-rate: who played this session and what they allow (docs/GAMEDB.md \"How a game is tested\")",
                         "nickname: the name the site credits (empty: anonymous); contact: how to reach the tester about",
                         "this session, never published; publish: the tester agrees that its numbers, answers and",
                         "screenshots are published on walnut.nanobyte.dev"])


def build_bundle(run, stage, ident, home, sub, progress=None):
    """the bundle's files under stage/<run name>/: text scrubbed, xwd shots as PNG, submitter.toml.
    Returns ([(rel, size)], [problems]); a problem (something the scrub left) means nothing may be written"""
    ev = _evidence()
    run = Path(run)
    top = Path(stage) / run.name
    files, problems = [], []
    plan = plan_bundle(run)
    nshots = sum(1 for _, k, _ in plan if k == "xwd")
    done = 0
    for rel, kind, src in plan:
        orel = scrub_text(rel, ident, home)
        dst = top / orel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if kind == "text":
            text = scrub_text(src.read_bytes().decode("utf-8", "surrogateescape"), ident, home)
            for what, m in ev.leftovers(text, ident):
                problems.append("%s: %s left (%r)" % (orel, what, m))
            if home and home not in ("/", "") and home in text:
                problems.append("%s: the home directory left" % orel)
            dst.write_bytes(text.encode("utf-8", "surrogateescape"))
        elif kind == "xwd":
            done += 1
            if progress:
                progress("  converting the screenshots to PNG: %d of %d" % (done, nshots))
            try:
                dst = dst.with_suffix(".png")
                xwd_to_png(src, dst)
                orel = str(Path(orel).with_suffix(".png"))
            except ValueError as e:      # a kind of dump this can't read: the PC converts it
                dst = top / orel
                shutil.copyfile(src, dst)
                problems.append("note: %s" % e)
        else:
            shutil.copyfile(src, dst)
        files.append((orel, dst.stat().st_size))
    b = kv(run / "bench.txt")
    (top / "submitter.toml").write_text(sub(run.name, b.get("game", "")))
    files.append(("submitter.toml", (top / "submitter.toml").stat().st_size))
    for what, m in ev.leftovers((top / "submitter.toml").read_text(), ident):
        problems.append("submitter.toml: %s (%r): leave it out of the nickname and contact" % (what, m))
    return files, problems


def write_tar(stage_top, dest):
    """stage_top as dest (a tar of <run>/...; root-owned, no user or group names), written to a .part and renamed"""
    part = Path(str(dest) + ".part")
    with tarfile.open(part, "w", format=tarfile.PAX_FORMAT) as t:
        def clean(ti):
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = ""
            ti.mode = 0o755 if ti.isdir() else 0o644
            return ti
        t.add(stage_top, arcname=Path(stage_top).name, filter=clean)
    os.replace(part, dest)


def bundle_args(args, npos, flags=()):
    """(positional, {--nick, --contact, --publish}, {flag: bool}) of export's and submit's arguments; None: usage"""
    opts = {"--nick": None, "--contact": None, "--publish": None}
    on = {f: f in args for f in ("--yes",) + tuple(flags)}
    args = [a for a in args if a not in on]
    pos = []
    while args:
        a = args.pop(0)
        if a in opts:
            if not args:
                return None
            opts[a] = args.pop(0)
        elif a.startswith("--"):
            return None
        else:
            pos.append(a)
    return (pos, opts, on) if len(pos) in npos else None


def session_run(name):
    """the run directory of a played session, or None after saying why"""
    run = resolve_run(name)
    if not run:
        print("walnut-rate: no run %r (walnut-rate status lists them; `last` is the newest)" % name)
        return None
    if kv(run / "bench.txt").get("kind") != "session":
        print("walnut-rate: %s is not a played session" % run.name)
        return None
    b = kv(run / "bench.txt")
    if b.get("interfered") == "1" or b.get("unexpected"):
        print("!! this session was disturbed (%s): it can't be rated, but it can still show us a problem" %
              (b.get("interference") or "unexpected processes: " + b.get("unexpected", "")))
    return run


def submitter_answers(opts):
    """(nickname, contact, publish) from the options, else asked on the terminal; None when one is too long"""
    interactive = sys.stdin.isatty()
    nick = opts["--nick"] if opts["--nick"] is not None else (
        ask("A nickname to credit you with on the site (Enter: anonymous): ") if interactive else "")
    contact = opts["--contact"] if opts["--contact"] is not None else (
        ask("How to reach you about this session, optional, never published (Enter: none): ") if interactive else "")
    pub = opts["--publish"]
    if pub is None:
        pub = ask("May Walnut publish this session (its numbers, your answers, the screenshots)? [y/N] ", "n") \
            if interactive else "no"
    for name, v, n in (("nickname", nick, 40), ("contact", contact, 120)):
        if len(v) > n or any(ord(c) < 32 or ord(c) == 127 for c in v):
            print("walnut-rate: the %s: at most %d characters on one line" % (name, n))
            return None
    return nick, contact, pub.lower() in ("y", "yes")


def stage_bundle(run, stage, nick, contact, publish, dest_name):
    """the bundle's files staged under stage/<run>/ and listed for the tester; None when the scrub left something"""
    users, hosts, home, left = console_identity(run)
    print("walnut-rate: preparing %s" % run.name)
    files, problems = build_bundle(run, stage, (users, hosts), home,
                                   lambda r, g: submitter_toml(nick, contact, publish, r, g),
                                   progress=lambda s: print(s, end="\r", flush=True))
    print()
    hard = [x for x in problems if not x.startswith("note: ")]
    for x in problems:
        print(("  " if x.startswith("note: ") else "!! ") + x)
    if hard:
        print("walnut-rate: the scrub left something private; nothing written or sent")
        return None
    total = sum(s for _, s in files)
    print("\nThe bundle %s: %d files, %s" % (dest_name, len(files), human(total)))
    for rel, s in files:
        print("  %9s  %s" % (human(s), rel))
    print("Scrubbed out of every text file: your user name%s, the host name, this console's addresses, MAC and "
          "LAN addresses, home directories." % ("s" if len(users) > 1 else ""))
    if left:
        print("Left as they are (words of the logs themselves): %s" % ", ".join(left))
    print("submitter.toml: nickname %r, contact %s, publish %s" % (nick, "given" if contact else "none",
                                                                 "yes" if publish else "no"))
    return files


def log_run(run, line):
    try:
        with open(run / LOG, "a") as f:
            f.write(line + "\n")
    except OSError:
        pass


def now_stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def stage_root(run):
    return run.parent if os.access(run.parent, os.W_OK) else Path(tempfile.gettempdir())


def cmd_export(args):
    a = bundle_args(args, (2,))
    if not a:
        return usage()
    (name, dest_dir), opts, on = a
    run = session_run(name)
    if not run:
        return 2
    dest_dir = Path(dest_dir)
    if not dest_dir.is_dir():
        print("walnut-rate: %s is not a directory (a USB stick's, for example)" % dest_dir)
        return 2
    ans = submitter_answers(opts)
    if not ans:
        return 2
    dest = dest_dir / (run.name + ".tar")
    with tempfile.TemporaryDirectory(prefix=".walnut-rate-export-", dir=stage_root(run)) as stage:
        files = stage_bundle(run, stage, *ans, dest.name)
        if files is None:
            return 1
        total = sum(s for _, s in files)
        free = shutil.disk_usage(dest_dir).free
        if free < total + (1 << 20):
            print("walnut-rate: only %s free in %s" % (human(free), dest_dir))
            return 1
        if dest.exists():
            print("(%s exists: it will be replaced)" % dest)
        if not on["--yes"] and not (sys.stdin.isatty() and ask("Write it to %s? [y/N] " % dest_dir, "n").lower()
                                    in ("y", "yes")):
            print("walnut-rate: nothing written")
            return 1
        write_tar(Path(stage) / run.name, dest)
    log_run(run, "exported %s to %s (%d bytes)" % (now_stamp(), dest, dest.stat().st_size))
    print("walnut-rate: wrote %s (%s). On the PC: tar -xf %s -C build/bench, then the import as for any session"
          % (dest, human(dest.stat().st_size), dest.name))
    return 0


# ---- submit ----------------------------------------------------------------------------------------
# Provisional (PLAN 5.12, the user hasn't answered yet): our own receiver on walnut.nanobyte.dev
# (servers/walnut.nanobyte.dev/rate/), no account; WALNUT_RATE_URL points it elsewhere (a test receiver).

SUBMIT_URL = "https://walnut.nanobyte.dev/rate/submit"
MAX_SUBMIT = 32 << 20           # the receiver's MAX_BUNDLE
SUBMITTED_RE = re.compile(r"^submitted (\S+) id (\S+)")


def submit_url():
    """the receiver's URL; plain http only to this machine (a test receiver), never over the network"""
    import urllib.parse
    url = os.environ.get("WALNUT_RATE_URL") or SUBMIT_URL
    u = urllib.parse.urlsplit(url)
    if u.scheme == "https" and u.hostname:
        return url, u.hostname
    if u.scheme == "http" and u.hostname in ("127.0.0.1", "localhost", "::1"):
        return url, u.hostname
    raise ValueError("WALNUT_RATE_URL=%s: https://, or http:// to this machine only" % url)


def submissions(run):
    """[(when, id)] of the run's submissions (its walnut-rate.log)"""
    try:
        lines = (Path(run) / LOG).read_text(errors="replace").splitlines()
    except OSError:
        return []
    return [(m[1], m[2]) for m in map(SUBMITTED_RE.match, lines) if m]


def post_bundle(url, path, timeout=300):
    """POSTs the tar at path; (HTTP status, the reply's JSON or {"error": text})"""
    import urllib.error
    import urllib.request
    size = Path(path).stat().st_size
    with open(path, "rb") as f:
        req = urllib.request.Request(url, data=f, method="POST", headers={
            "Content-Type": "application/x-tar", "Content-Length": str(size),
            "User-Agent": "walnut-rate/%s" % version()})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                code, body = r.status, r.read(65536)
        except urllib.error.HTTPError as e:
            code, body = e.code, e.read(65536)
    try:
        reply = json.loads(body.decode("utf-8", "replace"))
        if not isinstance(reply, dict):
            raise ValueError
    except ValueError:
        reply = {"error": body.decode("utf-8", "replace").strip()[:200] or "no answer"}
    return code, reply


def cmd_submit(args):
    a = bundle_args(args, (0, 1), ("--again",))
    if not a:
        return usage()
    pos, opts, on = a
    try:
        url, host = submit_url()
    except ValueError as e:
        print("walnut-rate: %s" % e)
        return 2
    run = session_run(pos[0] if pos else "last")
    if not run:
        return 2
    past = submissions(run)
    if past and not on["--again"]:
        print("walnut-rate: %s was sent already, as %s (%s); --again sends it once more" % (run.name, past[-1][1],
                                                                                           past[-1][0]))
        return 1
    ans = submitter_answers(opts)
    if not ans:
        return 2
    if not ans[2]:
        print("(You said no to publishing: we can look at it to find problems, but it won't be rated or shown.)")
    with tempfile.TemporaryDirectory(prefix=".walnut-rate-submit-", dir=stage_root(run)) as stage:
        files = stage_bundle(run, stage, *ans, run.name + ".tar")
        if files is None:
            return 1
        tar = Path(stage) / (run.name + ".tar")
        write_tar(Path(stage) / run.name, tar)
        size = tar.stat().st_size
        print("To send: %s, %s, to %s" % (tar.name, human(size), host))
        if size > MAX_SUBMIT:
            print("walnut-rate: that is over the %s a submission may be; walnut-rate export %s DIR writes it to a USB "
                  "stick instead" % (human(MAX_SUBMIT), run.name))
            return 1
        if not on["--yes"] and not (sys.stdin.isatty() and ask("Send it to %s? [y/N] " % host, "n").lower()
                                    in ("y", "yes")):
            print("walnut-rate: nothing sent")
            return 1
        print("walnut-rate: sending (%s)" % human(size))
        try:
            code, reply = post_bundle(url, tar)
        except OSError as e:
            print("walnut-rate: couldn't reach %s (%s); nothing was sent. Try again later, or walnut-rate export %s DIR"
                  % (host, getattr(e, "reason", e), run.name))
            return 1
    sid = reply.get("id")
    if code not in (200, 201) or not isinstance(sid, str) or not re.fullmatch(r"[0-9A-Za-z-]{1,64}", sid):
        print("walnut-rate: %s refused it (%d): %s" % (host, code, str(reply.get("error", "no id in the answer"))[:300]))
        return 1
    log_run(run, "submitted %s id %s to %s (%d bytes)" % (now_stamp(), sid, host, size))
    print("walnut-rate: sent. Its submission id: %s%s. We look at every session before it counts; "
          "walnut-rate status shows the id again." % (sid, " (it was there already)" if reply.get("duplicate") else ""))
    return 0


def usage():
    print(__doc__.split("\n\n")[1].rstrip(), file=sys.stderr)
    return 2


COMMANDS = {"list": cmd_list, "check": cmd_check, "play": cmd_play, "export": cmd_export, "status": cmd_status,
            "submit": cmd_submit}


def main(argv):
    if not argv or argv[0] not in COMMANDS:
        return usage()
    if argv[0] != "list" and os.geteuid() == 0:
        print("walnut-rate: run it as the player, not as root")
        return 2
    try:
        return COMMANDS[argv[0]](argv[1:])
    except KeyboardInterrupt:
        print("\nwalnut-rate: stopped")
        return 130
