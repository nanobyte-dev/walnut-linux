"""walnut_rate.py: walnut-rate, played sessions for anyone with a Walnut PS3 (the walnut-rate package; docs/games/
SUITE.md "Played sessions", docs/GAMEDB.md "How a game is tested"). Python stdlib (and python3-pil for the
screenshots' JPEGs), run from a terminal of the desktop as the player:

  walnut-rate list                      the games with a played test: installed or not, medal, sessions so far
  walnut-rate check [--close]           is the console clean for a session? (bench/harness/cleancheck.py)
  walnut-rate play GAME [LAUNCHER] [--close]
                                        the check, then the session exactly as session.sh runs it for us (fresh
                                        defaults, game mode, the recorder, screenshots), watched for interference
                                        (BENCH_WATCH=1), with the root reads through walnut-rate-helper
                                        (BENCH_ROOT_HELPER); then the terminal form and a summary
  walnut-rate export RUN DIR [--nick NAME] [--publish yes] [--yes]
                                        the session as the folder a submission adds, DIR/submissions/<game>/<id>/:
                                        the run directory as tools/gamedb/import-run.py reads it (shots as JPEG),
                                        the check's record, submitter.toml; scrubbed of the console's user, host
                                        and addresses, checked by submission_check.py, its contents and sizes shown
                                        before anything is written; then the steps to send it from a PC
  walnut-rate submit [RUN] [--nick NAME] [--publish yes] [--repo OWNER/NAME] [--yes] [--again]
                                        the same folder (RUN: default the newest), shown with its size, sent on your
                                        yes as a GitHub pull request to nanobyte-dev/walnut-linux (--repo or
                                        WALNUT_RATE_REPO: elsewhere, a test fork) with gh when it is installed and
                                        logged in; else the steps by hand. The link is kept with the run
  walnut-rate status                    the sessions on this console and what became of them (exported, sent)

The package's tree (/usr/lib/walnut-rate): bench/ (harness/, lib/, games/<id>/: the same files make bench-push
copies to /var/tmp/bench for our own runs), evidence.py (tools/gamedb/'s: the scrub), submission_check.py
(tools/gamedb/'s: the check the pull request's workflow runs), walnut-rate-helper, version.
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
import tempfile
import time
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
CHECK = "clean-check.txt"       # in the run directory: the check before the session (submission_check.EXTRA_FILES)


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


def _checker():
    """tools/gamedb/submission_check.py (the package's copy beside this file): the submission's names, limits and check"""
    _evidence()
    import submission_check
    return submission_check


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
    p("next: walnut-rate submit  sends it to us as a GitHub pull request (it shows what goes in first and asks), or\n"
      "      walnut-rate export %s DIR writes it to DIR (a USB stick) to send from a PC" % Path(run).name)
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
            state.append("sent: %s (%s)" % (sub[-1][1], sub[-1][0]))
        state.append(exp[-1] if exp else "not exported" if not sub else None)
        print("%-34s  %-14s  %-12s  %-6s  %-5s  %s" % (r.name, b.get("game", "?"), minutes(b.get("play_seconds")),
                                                     b.get("ended", "?"), "yes" if (r / "session.toml").exists() else "no",
                                                     "; ".join(x for x in state if x)))
    return 0


# ---- export ----------------------------------------------------------------------------------------
# A session leaves the console as one folder, submissions/<game>/<id>/ (tools/gamedb/submission_check.py: what it
# holds and what the check refuses), sent as a GitHub pull request to nanobyte-dev/walnut-linux that adds only that
# folder (the user, 2026-10-30: no receiver of ours). export writes it to a directory (a USB stick), submit opens the
# pull request with gh when gh is installed and logged in, else prints the steps by hand.

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
    read = lambda n: (Path(run) / n).read_bytes() if (Path(run) / n).is_file() else b""
    return _checker().run_words(read("bench.txt"), read("versions"))


def console_identity(run=None):
    """(users, hosts, home, left): what the scrub replaces, made from the console itself: the player's login and
    full name, the host name, every address of this machine; home the player's home directory; left the
    names left as they are (submission_check.GENERIC, or a word of the run's structure: run_words)"""
    pw = pwd.getpwuid(os.getuid())
    users = {pw.pw_name, getpass.getuser(), pw.pw_gecos.split(",")[0].strip()}
    hn = socket.gethostname()
    hosts = {hn, hn.split(".")[0]}
    try:
        hosts.add(Path("/etc/hostname").read_text().strip())
    except OSError:
        pass
    voc = run_words(run) if run else set()
    generic = _checker().GENERIC
    left = set()

    def keep(names):
        out = set()
        for x in names:
            if not x or len(x) < 3:
                continue
            if x.lower() in generic or x.lower() in voc:
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


def xwd_rgb(src):
    """(width, height, RGB bytes) of an X window dump (xwd: 24 or 32 bits per pixel TrueColor, either byte order);
    stdlib only. Raises ValueError for any other kind"""
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
    return w, ht, bytes(rgb)


def xwd_to_png(src, dst, level=6):
    """an X window dump as a PNG (stdlib only); (width, height). Raises ValueError for a kind xwd_rgb can't read"""
    w, ht, rgb = xwd_rgb(src)
    row = w * 3
    raw = b"".join(b"\0" + rgb[y * row:(y + 1) * row] for y in range(ht))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, ht, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, level)) + chunk(b"IEND", b"")
    Path(dst).write_bytes(png)
    return w, ht


SCALES = (1.0, 0.75, 0.5, 0.35)     # the screenshots made smaller until they fit submission_check.SHOTS_MAX


def shot_jpeg(src, dst, scale=1.0):
    """a screenshot (xwd, PNG or JPEG) as a JPEG at evidence.py's quality (SHOT_QUALITY), scale times its size; no
    metadata. A JPEG at full size is copied as it is"""
    src = Path(src)
    if scale == 1.0 and src.suffix == ".jpg":
        shutil.copyfile(src, dst)
        return
    try:
        from PIL import Image
    except ImportError:
        raise ValueError("python3-pil is missing (sudo apt install python3-pil): the screenshots go as JPEG")
    if src.suffix == ".xwd":
        w, h, rgb = xwd_rgb(src)
        im = Image.frombytes("RGB", (w, h), rgb)
    else:
        with Image.open(src) as f:
            im = f.convert("RGB")
    if scale < 1.0:
        im = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)
    im.save(dst, "JPEG", quality=int(_evidence().SHOT_QUALITY), optimize=True)


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
    """[(rel, kind, source)] of what goes into the folder: kind text (scrubbed) or shot (as JPEG, shots/<name>.jpg)"""
    ev = _evidence()
    run = Path(run)
    rels = [r for r in ev.RAW_FILES + _checker().EXTRA_FILES if (run / r).is_file()]
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
                out.append((str(Path(rel).with_suffix(".jpg")), "shot", c))
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


def submitter_toml(nick, run, game, sid):
    ev = _evidence()
    return ev.toml_text({"nickname": nick, "publish": True, "walnut_rate": version(), "exported": now_stamp(),
                         "run": run, "game": game, "id": sid},
                        ["walnut-rate: who played this session and what they allow (docs/GAMEDB.md \"How a game is tested\")",
                         "nickname: the name the site credits (empty: \"a tester\"); publish: the tester agrees that",
                         "the session's numbers, answers and screenshots are published on walnut.nanobyte.dev"])


def submission_id(run, fresh=False):
    """the session's submission id (submission_check.ID_RE: its date and six random letters or digits), kept in the
    run's walnut-rate.log so an export and a submit of the same run name the same folder"""
    if not fresh:
        try:
            for line in reversed((Path(run) / LOG).read_text(errors="replace").splitlines()):
                m = re.match(r"^id (\S+)$", line)
                if m and _checker().ID_RE.match(m[1]):
                    return m[1]
        except OSError:
            pass
    m = re.search(r"(\d{4})(\d{2})(\d{2})T\d{6}Z", Path(run).name)
    day = "%s-%s-%s" % m.groups() if m else datetime.date.today().isoformat()
    import secrets
    sid = day + "-" + "".join(secrets.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(6))
    log_run(Path(run), "id " + sid)
    return sid


def build_folder(run, dest, ident, home, nick, sid, progress=None):
    """the submission's files under dest (submissions/<game>/<id>/): text scrubbed, the shots as JPEG (smaller
    until they fit submission_check.SHOTS_MAX), submitter.toml. Returns ([(rel, size)], [problems]); a problem
    (something the scrub left, a picture that can't be read) means nothing may be written or sent"""
    ev, sc = _evidence(), _checker()
    run, dest = Path(run), Path(dest)
    files, problems = [], []
    plan = plan_bundle(run)
    for rel, kind, src in plan:
        if kind != "text":
            continue
        orel = scrub_text(rel, ident, home)
        out = dest / orel
        out.parent.mkdir(parents=True, exist_ok=True)
        text = scrub_text(src.read_bytes().decode("utf-8", "surrogateescape"), ident, home)
        for what, m in ev.leftovers(text, ident):
            problems.append("%s: %s left (%r)" % (orel, what, m))
        if home and home not in ("/", "") and home in text:
            problems.append("%s: the home directory left" % orel)
        out.write_bytes(text.encode("utf-8", "surrogateescape"))
        files.append((orel, out.stat().st_size))
    shots = [(rel, src) for rel, kind, src in plan if kind == "shot"]
    for scale in SCALES:
        made, total = [], 0
        for i, (rel, src) in enumerate(shots):
            if progress:
                progress("  the screenshots as JPEG%s: %d of %d" % ("" if scale == 1.0 else " at %d%%" % (scale * 100),
                                                                  i + 1, len(shots)))
            out = dest / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            try:
                shot_jpeg(src, out, scale)
            except (ValueError, OSError) as e:
                problems.append("%s: %s" % (rel, e))
                continue
            made.append((rel, out.stat().st_size))
            total += made[-1][1]
        if total <= sc.SHOTS_MAX or problems:
            break
    files += made
    if total > sc.SHOTS_MAX:
        problems.append("the screenshots are %s even at %d%% of their size, at most %s" %
                        (human(total), SCALES[-1] * 100, human(sc.SHOTS_MAX)))
    b = kv(run / "bench.txt")
    (dest / sc.SUBMITTER).write_text(submitter_toml(nick, run.name, b.get("game", ""), sid))
    files.append((sc.SUBMITTER, (dest / sc.SUBMITTER).stat().st_size))
    for what, m in ev.leftovers((dest / sc.SUBMITTER).read_text(), ident):
        problems.append("submitter.toml: %s (%r): leave it out of the nickname" % (what, m))
    return files, problems


def bundle_args(args, npos, flags=()):
    """(positional, {--nick, --publish, --repo}, {flag: bool}) of export's and submit's arguments; None: usage"""
    opts = {"--nick": None, "--publish": None, "--repo": None}
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
    b = kv(run / "bench.txt")
    if b.get("kind") != "session":
        print("walnut-rate: %s is not a played session" % run.name)
        return None
    if not ID_RE.match(b.get("game", "")):
        print("walnut-rate: %s names no game" % run.name)
        return None
    if b.get("interfered") == "1" or b.get("unexpected"):
        print("!! this session was disturbed (%s): it can't be rated, but it can still show us a problem" %
              (b.get("interference") or "unexpected processes: " + b.get("unexpected", "")))
    return run


PUBLIC = ("A submission is public: it goes to GitHub as a pull request, where anyone can read its files (the logs,\n"
          "your answers, the screenshots), and GitHub keeps them even after we close it. Your GitHub account name\n"
          "shows on it too. Walnut publishes the session on walnut.nanobyte.dev once it is reviewed.")


def submitter_answers(opts):
    """(nickname, yes to publishing) from the options, else asked on the terminal; None: not allowed or too long"""
    interactive = sys.stdin.isatty()
    nick = opts["--nick"] if opts["--nick"] is not None else (
        ask("A nickname to credit you with on the site (Enter: none, the site says \"a tester\"): ") if interactive else "")
    if len(nick) > 40 or any(ord(c) < 32 or ord(c) == 127 for c in nick):
        print("walnut-rate: the nickname: at most 40 characters on one line")
        return None
    pub = opts["--publish"]
    if pub is None:
        print(PUBLIC)
        pub = ask("Do you agree? [y/N] ", "n") if interactive else "no"
    if pub.lower() not in ("y", "yes"):
        print("walnut-rate: a session is only sent with your yes to publishing it; nothing written")
        return None
    return nick, True


def games_exist():
    """submission_check's game_exists for the games this package has played tests of"""
    _harness()
    import games
    return lambda g: (games.root() / g / "rating" / "test.toml").is_file()


def stage_folder(run, stage, nick, sid):
    """the submission staged as stage/submissions/<game>/<id>/, checked by submission_check and listed for the
    tester; (the folder, [(rel, size)]), or None when the scrub left something or the check refuses it"""
    sc = _checker()
    users, hosts, home, left = console_identity(run)
    game = kv(run / "bench.txt").get("game", "")
    dest = Path(stage) / sc.TOP / game / sid
    dest.mkdir(parents=True)
    print("walnut-rate: preparing %s" % run.name)
    files, problems = build_folder(run, dest, (users, hosts), home, nick, sid,
                                   progress=lambda s: print(s, end="\r", flush=True))
    print()
    for x in problems:
        print("!! " + x)
    if problems:
        print("walnut-rate: the folder isn't right (above); nothing written or sent")
        return None
    try:
        sc.check_folder(dest, games_exist())
    except sc.Refused as e:
        for r in e.reasons:
            print("!! " + r)
        print("walnut-rate: the submission check refuses it (above); nothing written or sent")
        return None
    total = sum(s for _, s in files)
    print("\nThe submission %s/%s/%s/: %d files, %s" % (sc.TOP, game, sid, len(files), human(total)))
    for rel, s in sorted(files):
        print("  %9s  %s" % (human(s), rel))
    print("Scrubbed out of every text file: your user name%s, the host name, this console's addresses, MAC and "
          "LAN addresses, home directories. The submission check passes." % ("s" if len(users) > 1 else ""))
    if left:
        print("Left as they are (words of the logs themselves): %s" % ", ".join(left))
    print("submitter.toml: nickname %r, publish yes" % nick)
    return dest, files


def log_run(run, line):
    try:
        with open(Path(run) / LOG, "a") as f:
            f.write(line + "\n")
    except OSError:
        pass


def now_stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def stage_root(run):
    return run.parent if os.access(run.parent, os.W_OK) else Path(tempfile.gettempdir())


def cmd_export(args):
    a = bundle_args(args, (2,))
    if not a or a[1]["--repo"] is not None:
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
    sid = submission_id(run)
    with tempfile.TemporaryDirectory(prefix=".walnut-rate-export-", dir=stage_root(run)) as stage:
        st = stage_folder(run, stage, ans[0], sid)
        if st is None:
            return 1
        folder, files = st
        rel = folder.relative_to(stage)
        dest = dest_dir / rel
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
        dest.parent.mkdir(parents=True, exist_ok=True)
        part = dest.parent / ("." + dest.name + ".part")
        shutil.rmtree(part, ignore_errors=True)
        shutil.copytree(folder, part)
        if dest.exists():
            shutil.rmtree(dest)
        os.rename(part, dest)
    log_run(run, "exported %s to %s (%d bytes)" % (now_stamp(), dest, total))
    print("walnut-rate: wrote %s (%s, %d files)." % (dest, human(total), len(files)))
    print(manual_steps(dest_dir, rel, run.name))
    return 0


# ---- submit ----------------------------------------------------------------------------------------

SUBMITTED_RE = re.compile(r"^submitted (\S+) pr (\S+)")


def submissions(run):
    """[(when, the pull request's URL)] of the run's submissions (its walnut-rate.log)"""
    try:
        lines = (Path(run) / LOG).read_text(errors="replace").splitlines()
    except OSError:
        return []
    return [(m[1], m[2]) for m in map(SUBMITTED_RE.match, lines) if m]


def target_repo(opts):
    r = opts.get("--repo") or os.environ.get("WALNUT_RATE_REPO") or _checker().REPO
    if not re.fullmatch(r"[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}", r):
        raise ValueError("%r: OWNER/NAME of a GitHub repository" % r)
    return r


def manual_steps(where, rel, run_name, repo=None):
    repo = repo or _checker().REPO
    return ("To send it from another computer (a PC with a browser):\n"
            "  1. walnut-rate export %s /media/$USER/<your stick> writes it to a USB stick%s.\n"
            "  2. On the PC, sign in to github.com, open https://github.com/%s and press Fork.\n"
            "  3. In your fork: Add file -> Upload files, and drag the stick's `submissions` folder onto the page (it\n"
            "     keeps the folders, %s). \"Create a new branch\", then Propose changes and Create pull request.\n"
            "  Or with gh on the PC, in a clone of your fork: copy the stick's submissions folder in, then\n"
            "     git switch -c submission-%s && git add %s && git commit -m 'a played session' &&\n"
            "     git push -u origin HEAD && gh pr create --repo %s --fill\n"
            "  The pull request must add only that folder; its check (validate-submission) says whether it is right."
            % (run_name, " (done: %s)" % where if where else "", repo, rel, Path(rel).name, rel, repo))


class GhError(Exception):
    pass


def gh_cmd():
    return shutil.which("gh")


def gh(args, body=None, timeout=300):
    """gh's output (JSON decoded when it is JSON); raises GhError"""
    cmd = [gh_cmd() or "gh"] + list(args)
    try:
        p = subprocess.run(cmd, input=json.dumps(body) if body is not None else None, capture_output=True,
                           text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise GhError(str(e))
    if p.returncode:
        err = (p.stderr or p.stdout).strip()
        raise GhError(err[-400:] or "gh exit %d" % p.returncode)
    try:
        return json.loads(p.stdout) if p.stdout.strip() else None
    except ValueError:
        return p.stdout


def api(method, path, body=None):
    args = ["api", "-X", method, "-H", "Accept: application/vnd.github+json", path]
    return gh(args + (["--input", "-"] if body is not None else []), body)


def gh_ready():
    """the GitHub login gh works as, or None (no gh, or not logged in)"""
    if not gh_cmd():
        return None
    try:
        gh(["auth", "status", "--hostname", "github.com"], timeout=60)
        u = api("GET", "user")
    except GhError:
        return None
    return u.get("login") if isinstance(u, dict) else None


def pr_body(run, game, sid, nick, files):
    b = kv(Path(run) / "bench.txt")
    total = sum(s for _, s in files)
    return ("A played session of `%s` from walnut-rate %s (docs/games/SUITE.md \"Played sessions\").\n\n"
            "| | |\n|---|---|\n| Game | `%s` |\n| Play | %s, ended: %s |\n| Run | `%s` |\n| Credited as | %s |\n"
            "| Folder | `submissions/%s/%s/`, %d files, %s |\n\n"
            "The tester agrees that this session (its numbers, answers and screenshots) is published on "
            "walnut.nanobyte.dev (`submitter.toml`: `publish = true`). Walnut reviews it before it counts; then this "
            "pull request is closed with a comment, not merged.\n"
            % (game, version(), game, minutes(b.get("play_seconds")), b.get("ended", "?"), Path(run).name,
               nick or "a tester", game, sid, len(files), human(total)))


def open_pr(repo, login, folder, rel, title, body, branch, progress=print):
    """the pull request to repo adding only folder's files at rel (submissions/<game>/<id>), through GitHub's API
    (gh api): a fork of repo (or repo itself when it is the login's), a commit on top of repo's default branch
    with just those files, a branch, the pull request. Nothing is cloned. Returns its URL"""
    import base64
    up = api("GET", "repos/" + repo)
    owner, main = up["owner"]["login"], up["default_branch"]
    base = api("GET", "repos/%s/git/ref/heads/%s" % (repo, main))["object"]["sha"]
    base_tree = api("GET", "repos/%s/git/commits/%s" % (repo, base))["tree"]["sha"]
    if owner.lower() == login.lower():
        target = repo
    else:
        progress("  your fork of %s" % repo)
        target = api("POST", "repos/%s/forks" % repo, {"default_branch_only": True})["full_name"]
        for _ in range(20):            # a new fork takes a moment
            try:
                api("GET", "repos/%s/git/ref/heads/%s" % (target, main))
                break
            except GhError:
                time.sleep(3)
        try:
            api("POST", "repos/%s/merge-upstream" % target, {"branch": main})
        except GhError:
            pass                       # an old fork that can't follow: the commit sits on the upstream's tip anyway
    tree = []
    names = sorted(str(p.relative_to(folder)) for p in Path(folder).rglob("*") if p.is_file())
    for i, n in enumerate(names):
        progress("  uploading %d of %d: %s" % (i + 1, len(names), n))
        data = (Path(folder) / n).read_bytes()
        sha = api("POST", "repos/%s/git/blobs" % target,
                  {"content": base64.b64encode(data).decode(), "encoding": "base64"})["sha"]
        tree.append({"path": "%s/%s" % (rel, n), "mode": "100644", "type": "blob", "sha": sha})
    t = api("POST", "repos/%s/git/trees" % target, {"base_tree": base_tree, "tree": tree})["sha"]
    c = api("POST", "repos/%s/git/commits" % target, {"message": title, "tree": t, "parents": [base]})["sha"]
    api("POST", "repos/%s/git/refs" % target, {"ref": "refs/heads/" + branch, "sha": c})
    head = branch if target == repo else "%s:%s" % (target.split("/")[0], branch)
    pr = api("POST", "repos/%s/pulls" % repo, {"title": title, "head": head, "base": main, "body": body,
                                                "maintainer_can_modify": False})
    return pr["html_url"]


def cmd_submit(args):
    a = bundle_args(args, (0, 1), ("--again",))
    if not a:
        return usage()
    pos, opts, on = a
    try:
        repo = target_repo(opts)
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
    sid = submission_id(run)
    game = kv(run / "bench.txt").get("game", "")
    with tempfile.TemporaryDirectory(prefix=".walnut-rate-submit-", dir=stage_root(run)) as stage:
        st = stage_folder(run, stage, ans[0], sid)
        if st is None:
            return 1
        folder, files = st
        rel = str(folder.relative_to(stage))
        login = gh_ready()
        if not login:
            print("\nwalnut-rate: %s, so it can't open the pull request from here." %
                  ("gh isn't logged in to GitHub (gh auth login)" if gh_cmd() else
                   "gh (GitHub's command line) isn't installed"))
            print(manual_steps(None, rel, run.name, repo))
            return 1
        size = sum(s for _, s in files)
        print("To send: a pull request to %s as %s, adding %s/ (%s)" % (repo, login, rel, human(size)))
        if not on["--yes"] and not (sys.stdin.isatty() and ask("Open it? [y/N] ", "n").lower() in ("y", "yes")):
            print("walnut-rate: nothing sent")
            return 1
        title = "submissions/%s/%s: a played session (walnut-rate %s)" % (game, sid, version())
        branch = "submission-%s-%s" % (game, sid) + ("-%d" % int(time.time()) if past else "")
        try:
            url = open_pr(repo, login, folder, rel, title, pr_body(run, game, sid, ans[0], files), branch,
                          progress=lambda s: print(s.ljust(78)[:78], end="\r", flush=True))
        except (GhError, KeyError, TypeError) as e:
            print("\nwalnut-rate: GitHub refused it: %s" % str(e)[:400])
            print("(walnut-rate submit --again tries once more; or by hand:)\n" + manual_steps(None, rel, run.name, repo))
            return 1
    print()
    log_run(run, "submitted %s pr %s (%d bytes)" % (now_stamp(), url, size))
    print("walnut-rate: sent: %s\nIts check runs there (validate-submission); we look at every session before it "
          "counts and say so on the pull request. walnut-rate status shows the link again." % url)
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
