"""gamedb: tools/gamedb/check.py (the whole catalog) when anything it reads changed, plus these rules on
each game whose folder changed:
  - a published entry (no draft = true) has page text under its front matter, known_issues (a list; empty
    when none are known), license_source (where its licence was read), a rating/test.toml, and no
    status or status_note (those describe drafts);
  - a game going from draft (or new) to published has thumb.png, a 256x144 PNG;
  - a draft has no played sessions (rating/sessions/*): a game that was played is published, broken or
    not, with its low medal;
  - a draft with only a rating/test.toml is fine."""
import re
import struct
import subprocess
import sys
import tomllib
from pathlib import PurePath

import core

NAME = "gamedb"
TARGETS = ("gamedb/games/*/**",)
SHARED_INPUTS = ()
# what tools/gamedb/check.py reads: a change to any of these runs it (it checks the whole catalog)
CHECKPY_INPUTS = ("gamedb/**", "tools/gamedb/*.py", "tools/gamedb/*.json", "bench/harness/*.py", "bench/analysis/*.py",
                  "tools/licensed.py", "tools/licensed-files.txt", "tools/pkg-dir.sh", "**/package.conf",
                  "**/debian/control", "**/dfsg-exclude.txt")
THUMB = (256, 144)


def select(ctx):
    paths = [p for c in ctx.changes for p in c.paths()]
    whole = ctx.mode == "all" or any(core.matches(p, CHECKPY_INPUTS) for p in paths)
    full = ctx.mode == "all" or any(core.matches(p, core.own_inputs(NAME)) for p in paths)
    games = {PurePath(p).parts[2] for p in paths if core.matches(p, TARGETS)}
    if full:
        games |= {PurePath(p).parts[2] for p in ctx.tracked() if core.matches(p, TARGETS)}
    return whole or bool(games) or full, (whole or full, sorted(games))


def front_matter(text):
    """(front matter dict, body text); ValueError without one"""
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+\n?(.*)", text, re.S)
    if not m:
        raise ValueError("no +++ TOML front matter")
    return tomllib.loads(m[1]), m[2]


def png_size(b):
    if not b or b[:8] != b"\x89PNG\r\n\x1a\n" or b[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", b[16:24])


def game_errors(ctx, gid):
    d = "gamedb/games/%s" % gid
    text = ctx.read(d + "/index.md")
    if text is None:
        return
    where = d + "/index.md"
    try:
        fm, body = front_matter(text)
    except (ValueError, tomllib.TOMLDecodeError) as e:
        ctx.error(where, str(e))
        return
    draft = fm.get("draft") is True
    sessions = [p for p in ctx.tracked() if p.startswith(d + "/rating/sessions/")]
    if draft:
        if sessions:
            ctx.error(d, "a draft with played sessions (rating/sessions/): a game that was played is published, "
                      "broken or not, with its low medal")
        return
    if not body.strip():
        ctx.error(where, "a published game needs its page text under the front matter")
    if not isinstance(fm.get("known_issues"), list):
        ctx.error(where, "a published game needs known_issues (a list of sentences; [] when none are known)")
    ls = fm.get("license_source")
    if not isinstance(ls, str) or not ls.strip():
        ctx.error(where, "a published game needs license_source: where its licence was read (e.g. the package's "
                  "debian/copyright, or the upstream LICENSE file's URL)")
    for k in ("status", "status_note"):
        if k in fm:
            ctx.error(where, "%s is a draft's field: a published game has none" % k)
    if not ctx.exists(d + "/rating/test.toml"):
        ctx.error(d, "a published game needs rating/test.toml (its played test)")
    if ctx.mode == "all":
        size = png_size(ctx.read_bytes(d + "/thumb.png"))
        if size != THUMB:
            ctx.warn(d + "/thumb.png", "a published game's thumbnail is a %dx%d PNG (%s)" % (THUMB + ("missing" if size is None else "%dx%d" % size,)))
        return
    try:
        before, _ = front_matter(ctx.base_text(where) or "")
        was_published = before.get("draft") is not True
    except (ValueError, tomllib.TOMLDecodeError):
        was_published = False
    if not was_published:
        size = png_size(ctx.read_bytes(d + "/thumb.png"))
        if size != THUMB:
            ctx.error(d + "/thumb.png", "a game going from draft to published needs thumb.png, a %dx%d PNG (%s)" % (
                THUMB + ("missing or not a PNG" if size is None else "it is %dx%d" % size,)))


def run(ctx, files):
    whole, games = files
    if whole:
        r = subprocess.run([sys.executable, "-I", "tools/gamedb/check.py"], cwd=ctx.root, capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            lines = [ln for ln in r.stderr.splitlines() if ln.strip() and ": warning: " not in ln]
            for ln in lines[:200] or [r.stdout.strip() or "exit %d" % r.returncode]:
                ctx.error(None, "check.py: " + ln)
        for ln in r.stderr.splitlines():
            if ": warning: " in ln:
                ctx.warn(None, "check.py: " + ln)
    for gid in games:
        game_errors(ctx, gid)
