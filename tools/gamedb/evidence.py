"""evidence.py: a played session's evidence in gamedb (gamedb/README.md "games/<id>/rating/"): what
tools/gamedb/import-run.py keeps of a harness run beside the record, how tools/gamedb/reimport.py makes a
run directory out of it again, and the public-safe scan tools/gamedb/check.py runs over it.

A session folder, rating/sessions/<run>/:
  result.toml   the record, derived from the evidence below (import-run.py, reimport.py)
  form.toml     the player's answers as given (the run's session.toml)
  review.md     the reviewer's REVIEW.md as given
  shots/        every screenshot of the session as JPEG (SHOT_SIZE, SHOT_QUALITY: the size the site and the
                app show): ps3-<p>s.jpg the timed ones, ps3-mark-<t>s.jpg the player's own (shot_name);
                result.toml [shots] ps3 lists the gallery's, as paths in the folder (shots/ps3-60s.jpg).
                shots/pc/pc-<p>s.jpg: the PC reference the run's review sheet had (build/bench-ref)
  raw/          the harness's files a re-derivation reads (RAW_FILES, RAW_GLOBS), and the game's settings as
                the session wrote them (fresh-home/, small text files only), scrubbed (scrub()) and each one
                xz-compressed: raw/<its path in the run>.xz. raw/import.toml: the importer's own inputs
                (the run's name, --launcher, --shots, --rules; frozen: the record is kept as it was).
                Private: the public copy strips gamedb/**/raw/** (tools/public-export.sh)
shots/ and raw/ are Git LFS files (.gitattributes).

Scrub (raw/ only; form.toml and review.md are kept as given and must pass the scan): /home/<user> -> ~,
MAC addresses -> xx:xx:xx:xx:xx:xx, private IPv4 addresses -> LAN-IP, this machine's and the console's
user and host names (identity()) -> player / host. The scan (folder_errors) refuses what the scrub
should have removed, in the record, form.toml, review.md and every raw/ file (decompressed): /home/,
private IPv4, MAC addresses, the identity's names, and raw/ paths that match a licensed leak glob
(tools/licensed.py). An LFS pointer (objects not fetched) is skipped, not read.
"""
import functools
import getpass
import importlib.util
import json
import lzma
import os
import re
import shutil
import socket
import subprocess
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

SHOT_SIZE, SHOT_QUALITY = "800x450", "82"
# the run's files a re-derivation reads (import-run.py, bench/report.py, bench/shots.py), and the ones a
# reader of the evidence wants beside them (the command, temperatures, memory snapshots, the X log)
RAW_FILES = ("bench.txt", "cmd", "versions", "result", "samples", "temps", "out.log", "game.log", "dmesg.log",
             "input.tsv", "input.err", "xruns.tsv", "xruns.log", "snap-pre.txt", "snap-post.txt", "Xorg.0.log",
             "shots/geometry.tsv")
RAW_GLOBS = ("hud/*",)
RAW_MAX = 32 << 20           # a raw file over this is left out (raw/import.toml skipped)
FRESH_MAX = 64 << 10         # fresh-home/: text files up to this size, up to FRESH_TOTAL in all
FRESH_TOTAL = 1 << 20
IMPORT = "import.toml"
LFS_MAGIC = b"version https://git-lfs.github.com/spec/"
FOLDER_FILES = ("result.toml", "form.toml", "review.md", "shots", "raw")

MAC_RE = re.compile(r"(?<![0-9A-Fa-f:])[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}(?![0-9A-Fa-f:])")
LAN_RE = re.compile(r"(?<![\d.])(?:10\.\d{1,3}\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}"
                    r"|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})(?![\d.])")
HOME_RE = re.compile(r"/home/([A-Za-z0-9._-]+)")


def shot_name(p):
    """the file of a shot in shots/: a timed point (int seconds) or a player's mark (float, seconds)"""
    return "ps3-%ds.jpg" % p if isinstance(p, int) else "ps3-mark-%ss.jpg" % ("%.1f" % p).replace(".", "_")


def jpeg(src, dst):
    """src as a JPEG at SHOT_SIZE (ImageMagick); None, or the error"""
    p = subprocess.run(["magick", str(src), "-resize", SHOT_SIZE, "-strip", "-quality", SHOT_QUALITY, str(dst)],
                       capture_output=True, text=True)
    return "magick %s: %s" % (Path(src).name, p.stderr.strip()[-200:]) if p.returncode else None


def _config_values(repo, names):
    """NAME = value of config.local.mk (untracked: this checkout's console), for the names given"""
    out = {}
    f = Path(repo) / "config.local.mk"
    for line in (f.read_text(errors="replace").splitlines() if f.exists() else []):
        m = re.match(r"\s*([A-Z0-9_]+)\s*[:?]?=\s*(\S+)", line)
        if m and m[1] in names:
            out[m[1]] = m[2]
    return out


@functools.lru_cache(maxsize=None)
def identity(repo=REPO):
    """(user names, host names) this checkout knows to be private: the console's user and address
    (config.local.mk PS3_USER, PS3_IP), this machine's user and host name. Empty where there is no
    config.local.mk (the public copy, CI): the generic patterns still apply there"""
    cfg = _config_values(repo, ("PS3_USER", "PS3_IP"))
    if not (Path(repo) / "config.local.mk").exists():
        return (), ()
    users = {cfg.get("PS3_USER"), getpass.getuser()}
    host = socket.gethostname()
    hosts = {host, host.split(".")[0], cfg.get("PS3_IP")}
    keep = lambda s: tuple(sorted(x for x in s if x and len(x) >= 3 and x not in ("root", "player", "host", "walnut")))
    return keep(users), keep(hosts)


def _word(names):
    return re.compile(r"(?<![A-Za-z0-9_.-])(?:%s)(?![A-Za-z0-9_-])" % "|".join(map(re.escape, names))) if names else None


def scrub(text, ident=None):
    """text without the private names: /home/<user> -> ~, MACs, private IPv4, the identity's users and hosts"""
    users, hosts = identity() if ident is None else ident
    text = HOME_RE.sub("~", text)
    text = MAC_RE.sub("xx:xx:xx:xx:xx:xx", text)
    text = LAN_RE.sub("LAN-IP", text)
    for names, repl in ((hosts, "host"), (users, "player")):
        r = _word(names)
        if r:
            text = r.sub(repl, text)
    return text


# cheap first looks (the full patterns only where these hit: the scan reads every raw/ file at each check)
MAC_PRE = re.compile(r":[0-9A-Fa-f]{2}:[0-9A-Fa-f]{2}:[0-9A-Fa-f]{2}:[0-9A-Fa-f]{2}:")
LAN_PRE = re.compile(r"(?:10|192\.168|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d")


def leftovers(text, ident=None):
    """what the public-safe scan refuses in text: [(what, the match)]"""
    users, hosts = identity() if ident is None else ident
    has = lambda names: any(n in text for n in names)
    out = []
    for what, r, pre in (("a home directory", re.compile(r"/home/"), "/home/" in text),
                         ("a MAC address", MAC_RE, MAC_PRE.search(text)), ("a LAN address", LAN_RE, LAN_PRE.search(text)),
                         ("a private user name", _word(users), has(users)), ("a private host name", _word(hosts), has(hosts))):
        m = r.search(text) if r and pre else None
        if m:
            out.append((what, m[0]))
    users_at = re.compile(r"\b(?:%s)@" % "|".join(map(re.escape, users))) if users and has(users) else None
    if users_at and users_at.search(text):
        out.append(("user@", users_at.search(text)[0]))
    return out


def is_text(data):
    return b"\0" not in data[:65536]


def pack(data):
    """xz bytes of data (deterministic: the same input gives the same bytes)"""
    return lzma.compress(data, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64, preset=6)


def scrub_bytes(data, ident=None):
    return scrub(data.decode("utf-8", "surrogateescape"), ident).encode("utf-8", "surrogateescape")


def toml_text(d, head):
    """a flat TOML table of strings, numbers, bools and lists of them"""
    def v(x):
        if isinstance(x, bool):
            return "true" if x else "false"
        if isinstance(x, (int, float)):
            return repr(x)
        if isinstance(x, str):
            return json.dumps(x, ensure_ascii=False)
        return "[" + ", ".join(v(y) for y in x) + "]"
    return "".join("# %s\n" % h for h in head) + "".join("%s = %s\n" % (k, v(x)) for k, x in d.items() if x is not None)


def ref_root():
    return Path(os.environ.get("BENCH_REF") or REPO / "build" / "bench-ref")


def _shots_module():
    if str(REPO / "bench") not in sys.path:
        sys.path.insert(0, str(REPO / "bench"))
    import shots
    return shots


def collect(d, sdir, opts, ident=None, ref=None):
    """write the evidence of run directory d into the empty session folder sdir: shots/ (every screenshot,
    shots/pc/ the review sheet's PC reference), raw/ (scrubbed, compressed; raw/import.toml: opts and what
    was left out), form.toml, review.md. Returns a list of problems (a shot that can't be converted)"""
    shots = _shots_module()
    d, sdir = Path(d), Path(sdir)
    errs, skipped = [], []
    (sdir / "shots").mkdir(parents=True)
    sh = shots.parse(d)
    for p, s in sorted(sh["shots"].items()) + sorted(sh["marks"].items()):
        src = shots.image(d, s)
        if src:
            e = jpeg(src, sdir / "shots" / shot_name(p))
            if e:
                errs.append(e)
    rd = shots.ref_dir(sh, ref or ref_root()) if sh.get("game") else None
    pc = []
    if rd:
        rs = shots.parse(rd)
        for p, s in sorted(rs["shots"].items()):
            src = shots.image(rd, s)
            if src:
                (sdir / "shots" / "pc").mkdir(exist_ok=True)
                e = jpeg(src, sdir / "shots" / "pc" / ("pc-%ds.jpg" % p))
                if e:
                    errs.append(e)
                else:
                    pc.append(p)
    files = [r for r in RAW_FILES if (d / r).is_file()]
    for g in RAW_GLOBS:
        files += sorted(str(f.relative_to(d)) for f in d.glob(g) if f.is_file() and str(f.relative_to(d)) not in files)
    raw = sdir / "raw"
    raw.mkdir()

    def put(rel, data):
        out = raw / (rel + ".xz")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(pack(scrub_bytes(data, ident)))

    for rel in files:
        data = (d / rel).read_bytes()
        if len(data) > RAW_MAX or not is_text(data):
            skipped.append("%s: %d bytes%s" % (rel, len(data), "" if is_text(data) else ", binary"))
            continue
        put(rel, data)
    if rd:
        for rel in ("bench.txt", "versions"):
            if (rd / rel).is_file():
                put("pc/" + rel, (rd / rel).read_bytes())
    total = 0
    fresh = d / "fresh-home"
    for f in sorted(fresh.rglob("*")) if fresh.is_dir() else []:
        if not f.is_file():
            continue
        rel = str(f.relative_to(d))
        data = f.read_bytes()
        if len(data) > FRESH_MAX or not is_text(data) or total + len(data) > FRESH_TOTAL:
            skipped.append("%s: %d bytes%s" % (scrub(rel, ident), len(data), "" if is_text(data) else ", binary"))
            continue
        total += len(data)
        put(scrub(rel, ident), data)
    for src, dst in (("session.toml", "form.toml"), ("REVIEW.md", "review.md")):
        if (d / src).is_file():
            shutil.copyfile(d / src, sdir / dst)
    info = dict(opts, pc_ref=rd.name if rd and pc else None, skipped=skipped or None)
    (raw / IMPORT).write_text(toml_text(info, ["what tools/gamedb/import-run.py was given (tools/gamedb/reimport.py "
                                               "imports the session again with the same)"]))
    return errs


def read_import(sdir):
    """raw/import.toml of a session folder, {} without one"""
    f = Path(sdir) / "raw" / IMPORT
    if not f.is_file() or f.read_bytes().startswith(LFS_MAGIC):
        return {}
    return tomllib.loads(f.read_text())


def lfs_pointers(sdir):
    """the session's shots/ and raw/ files that are Git LFS pointers (objects not fetched)"""
    out = []
    for sub in ("shots", "raw"):
        for f in sorted((Path(sdir) / sub).rglob("*")) if (Path(sdir) / sub).is_dir() else []:
            if f.is_file() and f.stat().st_size < 1024 and f.read_bytes().startswith(LFS_MAGIC):
                out.append(str(f.relative_to(sdir)))
    return out


def rebuild(sdir, dest):
    """a run directory made from session folder sdir's evidence, dest/<the run's name>/: raw/ decompressed
    (raw/pc/ and raw/import.toml left out), form.toml as session.toml, review.md as REVIEW.md, and each shot
    of shots/ where bench.txt names its picture (shots/shot-60s.xwd -> shots/shot-60s.jpg)"""
    shots = _shots_module()
    sdir = Path(sdir)
    opts = read_import(sdir)
    if not opts.get("run"):
        raise ValueError("%s: no raw/%s" % (sdir, IMPORT))
    bad = lfs_pointers(sdir)
    if bad:
        raise ValueError("%s: Git LFS objects not fetched (git lfs pull): %s" % (sdir, ", ".join(bad[:3])))
    rd = Path(dest) / opts["run"]
    rd.mkdir(parents=True)
    raw = sdir / "raw"
    for f in sorted(raw.rglob("*.xz")):
        rel = str(f.relative_to(raw))[:-3]
        if rel.startswith("pc/"):
            continue
        out = rd / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(lzma.decompress(f.read_bytes()))
    for src, dst in (("form.toml", "session.toml"), ("review.md", "REVIEW.md")):
        if (sdir / src).is_file():
            shutil.copyfile(sdir / src, rd / dst)
    sh = shots.parse(rd)
    for p, s in list(sh["shots"].items()) + list(sh["marks"].items()):
        jpg = sdir / "shots" / shot_name(p)
        if s.get("file") and jpg.is_file():
            out = (rd / s["file"]).with_suffix(".jpg")
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(jpg, out)
    return rd


@functools.lru_cache(maxsize=None)
def _licensed_globs():
    spec = importlib.util.spec_from_file_location("licensed", REPO / "tools" / "licensed.py")
    lic = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lic)
    return lic, tuple(lic.globs(REPO))


def folder_errors(t, listed, ident=None):
    """the problems of session folder t (beyond result.toml's own checks): files that don't belong, shots
    that aren't JPEG files of shots/, and the public-safe scan of the record, form.toml, review.md and raw/"""
    t = Path(t)
    errs = []
    for f in sorted(x.name for x in t.iterdir()):
        if f not in FOLDER_FILES:
            errs.append("unexpected file %s (a session folder holds %s)" % (f, ", ".join(FOLDER_FILES)))
        elif f in ("shots", "raw") and not (t / f).is_dir():
            errs.append("%s is not a folder" % f)
    for f in sorted(listed):
        if not re.fullmatch(r"shots/ps3-[A-Za-z0-9_-]+\.jpg", f):
            errs.append("[shots] lists %s: a gallery shot is shots/ps3-<point>.jpg" % f)
    sd = t / "shots"
    for f in sorted(sd.rglob("*")) if sd.is_dir() else []:
        rel = str(f.relative_to(sd))
        if f.is_file() and not re.fullmatch(r"ps3-[A-Za-z0-9_-]+\.jpg|pc/pc-[A-Za-z0-9_-]+\.jpg", rel):
            errs.append("unexpected file shots/%s (shots/ holds ps3-<point>.jpg and pc/pc-<point>.jpg)" % rel)
    texts = [(n, (t / n).read_bytes()) for n in ("result.toml", "form.toml", "review.md") if (t / n).is_file()]
    raw = t / "raw"
    lic = None
    for f in sorted(raw.rglob("*")) if raw.is_dir() else []:
        if not f.is_file():
            continue
        rel = str(f.relative_to(t))
        data = f.read_bytes()
        if data.startswith(LFS_MAGIC):
            continue                   # not fetched: nothing to read
        if f.suffix == ".xz":
            try:
                data = lzma.decompress(data)
            except lzma.LZMAError as e:
                errs.append("%s: not xz (%s)" % (rel, e))
                continue
            rel = rel[:-3]
        if lic is None:
            lic, globs = _licensed_globs()
        hit = lic.glob_hit(rel[len("raw/"):], globs)
        if hit:
            errs.append("%s: a licensed file (%s)" % (rel, hit))
        texts.append((rel, data))
    for rel, data in texts:
        for what, m in leftovers(data.decode("utf-8", "replace"), ident):
            errs.append("%s: %s (%r): not public-safe (tools/gamedb/evidence.py scrub)" % (rel, what, m))
    return errs
