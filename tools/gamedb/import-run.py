#!/usr/bin/env python3
"""import-run.py RUN... --game ID [options]: turn bench run directories into a game's sessions in the Walnut Games catalog (docs/GAMEDB.md).
Options: --launcher NAME, --shots POINTS, --rules R, --session NAME, --keep-record, --content DIR, --dry-run.

Design
- Input: a run directory of the harness (bench/harness/run.sh, make bench-pull: bench.txt, versions,
  hud/, game.log, dmesg.log, REVIEW.md, shots/). bench/analysis/report.py's run() supplies the numbers
  it already derives (steady fps, 1% low, the interference verdict, memory, the screenshots' review);
  this tool adds the ones the rating needs and report.py doesn't compute:
    stutter   frames longer than max(50 ms, 3 x the median frame); the time they lost is their
              length minus the median frame, summed per minute of the frames' own time. The frames
              are those after the load, without the reload segments of a looped demo
    sound     (alsa underruns + OpenAL xruns + OpenAL waits) per minute of (seconds - play start);
              snd_stall > 0 is a wedge. Runs with ps3-xrunlog (bench.txt xrunlog=on) also get the
              audible dropouts (xruns_audible + OpenAL waits) per minute, which the score uses instead
    play      seconds - the play start, for runs that reached play (menu-only runs count 0)
    end       "crash" when ended=exit with rc > 128 or the kernel log has a segfault/oom line;
              "window"/"duration" are the harness's own stops, not crashes
  The play start is the recipe's own mark where it has one (Warzone: the line the game prints when
  the match starts), else bench.txt's load_end; the load time is the same moment.
- Output: the session folder <content>/<id>/rating/sessions/<date>-<kind>/ (--session NAME: that one). First the
  evidence (tools/gamedb/evidence.py): every screenshot in shots/ as JPEG, 800x450, quality 82 (ImageMagick
  `magick`), the PC reference in shots/pc/, the player's form.toml and the reviewer's review.md as given, the
  harness's files scrubbed and compressed in raw/ with raw/import.toml (this tool's inputs). Then result.toml,
  derived from that evidence alone (derive_session: the run rebuilt from it, so tools/gamedb/reimport.py
  gets the same record from the folder without the run), written by a small hand-rolled TOML writer (stdlib
  has a reader only) under a "generated, do not edit" header; its [shots] ps3 lists the gallery (the pictures
  the site and the app show: only the PS3's, the PC reference is for reviewing) as shots/<file>. The repository
  keeps shots/ and raw/ in Git LFS (.gitattributes). index.md is never touched. The game must exist.
  --keep-record: the folder's result.toml stays as it is (a record an older importer derived, before rules
  versions), only its gallery moves into shots/; raw/import.toml frozen = true, reimport.py leaves it.
- kind: "session" (bench.txt kind=session, the recorder of phase P3), "demo" (a harness run with a
  measured window: stands in for a session until the game has one), "capture" (shots only: grades
  the picture, nothing else).
- launcher: result.toml gets `launcher = "<Name>"` (the game's menu entry the session started, each
  launcher is rated on its own) from bench.txt's launcher=, or from --launcher NAME for runs that
  recorded none; a different --launcher than the recorded one is refused. Neither: no key (the
  catalog takes the game's default launcher).
- rules (docs/GAMEDB.md "Rules versions"): result.toml gets `rules = "<data>.<formula>"`, the data version
  the run measured with the formula revision of the day. A session whose form is the second one
  (session.toml form_version = 2) is data version 2; older runs stay 1 (they lack the new data points);
  --rules 2.0 sets it by hand (a capture or a scripted run reviewed the new way). The catalog scores
  every test from its raw numbers and answers with the current formula of its data version, so a
  version-2 test keeps no [scores] table; version 1 keeps its as it always had.
- Data version 2 adds, all raw: the window samples (numbers.geometry, scored at catalog time) with the
  X pointer over the window (pointer_samples, pointer_visible: geom.py), black bars on the screenshots
  (bars, bars_content), the desktop back after the quit (desktop_back_s, desktop_stuck: session.sh), the
  input devices (pads_present, pad_opened, pad_events_play, pads_hotplugged, pads_hotplug_opened,
  kb_presses, kb_readable: inputlog.py's input.tsv), the level load (load_level_s: the player's last
  press before the play mark to the mark), the fresh defaults (fresh_home, fresh_home_new, recovered:
  freshhome.py), the form's answers ([form]) and the reviewer's answers ([review]) to review.py's
  questionnaire: verdict, stretched, text_px, pointer, each defect category (no, or pc|ps3 and where;
  pc: also on the PC reference, ps3: PS3/Walnut only) and broken (none, or `<item> <pc|ps3>: <what>`),
  plus optional `check: <item> yes|partly|no|na` lines. A question left unanswered ("?") is refused, so
  is an answer that can't be read, and `verdict: wrong` without a defect.
- Refused: runs with interfered=1 (or TEMPLIMIT / thermal stop), capture runs without REVIEW.md.
- No absolute path, host name or user name is written: the run is named by its directory name only.
Importing the same run again rewrites its session folder; another run of the same day and kind gets
the time of day in its directory name.
"""
import argparse
import json
import re
import statistics
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path[:0] = [str(REPO / "bench" / "analysis"), str(REPO / "bench" / "harness"), str(HERE)]   # python3 -I keeps the script's directory out
import check    # noqa: E402  (the catalog's layout: SESSIONS, CONTENT)
import evidence  # noqa: E402  (the session's evidence: shots/, raw/, form.toml, review.md)
import games    # noqa: E402  (bench/harness/games.py: a run's game and its recipe)
import gamedata  # noqa: E402  (bench/harness/gamedata.py: a game's played test, rating/test.toml)
import report   # noqa: E402  (bench/analysis/report.py)
import score    # noqa: E402
import shots    # noqa: E402

CRASH_RE = re.compile(r"segfault|general protection|out of memory|oom-kill|killed process", re.I)
SKIP_PKGS = shots.SKIP_PKGS
JPEG_SIZE, JPEG_QUALITY = evidence.SHOT_SIZE, evidence.SHOT_QUALITY


class Refused(Exception):
    pass


def front_matter(index):
    """the TOML front matter (between +++ lines) of a game's index.md"""
    text = index.read_text()
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+", text, re.S)
    return tomllib.loads(m[1]) if m else {}


LOAD_FRAME_MS = 1000    # a frame this long within LOAD_LOOKBACK_S before a play mark is part of that level's load
LOAD_LOOKBACK_S = 30


def play_marks(d, b):
    """every time the game logged its play mark (a race or level starting), seconds after the start:
    bench.txt play_marks= (client.sh), else the game's play_mark (rating/test.toml) over out.log's
    timestamped lines (runs recorded before play_marks=), else just play_start. [] without a mark"""
    if b.get("play_from") != "mark":
        return []
    if b.get("play_marks"):
        return sorted(t for t in (report.num(x) for x in b["play_marks"].split()) if t is not None)
    first = report.num(b.get("play_start"))
    game = games.run_game(b) if b.get("game") or b.get("recipe") else \
        games.resolve(d.name.split("-session-")[-1].rsplit("-", 1)[0], old=True)
    rx, out, t0 = test_play_mark(game), \
        d / "out.log", report.num(b.get("t0"))
    marks = []
    if rx and out.exists() and t0 is not None:
        try:
            r = re.compile(rx)
        except re.error:
            r = None
        for line in out.read_text(errors="replace").splitlines() if r else []:
            ts, _, text = line.partition(" ")
            if report.num(ts) is not None and r.search(text):
                marks.append(round(report.num(ts) - t0, 2))
    if first is not None and not any(abs(m - first) < 1 for m in marks):
        marks.append(first)
    return sorted(marks)


def test_play_mark(game):
    """the game's play_mark regex (its rating/test.toml, bench/harness/gamedata.py; a run from before the recipes
    moved into gamedb names its game by the old recipe, which games.run_game has read already), or None"""
    v = gamedata.test(game).get("play_mark") if game else None
    return v if isinstance(v, str) and v else None


def level_loads(d, b):
    """[(load start, mark)] for every play mark: a level's load runs from the earlier of the player's last
    keyboard press before the mark and the first long frame (over LOAD_FRAME_MS) in the LOAD_LOOKBACK_S
    before it (the end of the previous race and its return to the menus: SuperTuxKart, 2026-10-26), to the
    mark. The first mark's load is the level load of Load; later ones (the next race, the next level)
    are left out of the play like the first (with WARMUP_S after them) and count in Load too"""
    marks = play_marks(d, b)
    if not marks:
        return []
    keys = key_times(d)
    path = d / "hud" / "frametime_(ms)"
    ft = report.floats(path) if path.exists() else []
    ends = report.frame_ends(ft, b) if ft else []
    out, prev = [], 0.0
    for m in marks:
        lo = max(prev, m - LOAD_LOOKBACK_S)
        k = [t for t in keys if lo <= t < m]
        longs = [e - v / 1000 for e, v in zip(ends, ft) if v > LOAD_FRAME_MS and lo <= e - v / 1000 <= m]
        cands = ([max(k)] if k else []) + ([min(longs)] if longs else [])
        start = min(cands) if cands else m
        out.append((start, m))
        prev = m
    return out


def load_gaps(d, b):
    """the spans left out of the play after the first mark: each later level's load and its warm-up"""
    return [(s, m + WARMUP_S) for s, m in level_loads(d, b)[1:]]


def play_start(d, b):
    """seconds from the client's start to the first gameplay frame: the recipe's own mark where it has
    one (SHOT_RE / shot_mark: the line the game prints when the match starts, shot_ref=re|mark:<s>),
    else the end of the load (bench.txt load_end). A played session's own mark (bench.txt play_from=mark,
    play_start=: the played test's play_mark, the match starting after the menus) comes first: without it the
    menus and the setup screens counted as play (Warzone, 2026-10-25). None when nothing is known"""
    if b.get("play_from") == "mark" and report.num(b.get("play_start")) is not None:
        return report.num(b.get("play_start"))
    sh = shots.parse(d)
    if sh["ref"] in ("re", "mark") and sh["ref_t"] is not None:
        return sh["ref_t"]
    return report.num(b.get("load_end"))


WARMUP_S = 30   # seconds of play after the load that settle (the first level's start, caches): not measured


CRASH_FREEZE_MS = 1000   # the run of frames this long at the end of a crashed run: the freeze leading into the crash


def crash_freeze(d, b):
    """seconds after the start when the freeze leading into a crash began (the trailing frames over
    CRASH_FREEZE_MS of a run that ended in a crash), or None. That freeze is the crash: Stability counts it,
    Smoothness and the input checks don't (the player pressing keys to get out of a frozen game is not the
    game needing a keyboard)"""
    if b.get("ended") not in ("crash", "oom") and not (b.get("ended") == "exit" and (report.num(b.get("rc")) or 0) > 128):
        return None
    path = d / "hud" / "frametime_(ms)"
    ft = report.floats(path) if path.exists() else []
    if not ft:
        return None
    ends = report.frame_ends(ft, b)
    i = len(ft)
    while i > 0 and ft[i - 1] > CRASH_FREEZE_MS:
        i -= 1
    return ends[i] - ft[i] / 1000 if i < len(ft) else None


LOCAL_FRAMES = 60   # frames on each side of a frame whose median is its reference (about 1-2 s)


def local_stutter(ft):
    """ms of stutter in play-ordered frame times: a frame over max(50 ms, 3x the median of the LOCAL_FRAMES
    frames on each side) stutters by what it exceeds that median"""
    import bisect
    win = sorted(ft[:LOCAL_FRAMES + 1])
    lo, hi, total = 0, LOCAL_FRAMES + 1, 0.0     # win holds ft[lo:hi]
    for i, v in enumerate(ft):
        want_lo, want_hi = max(0, i - LOCAL_FRAMES), min(len(ft), i + LOCAL_FRAMES + 1)
        while hi < want_hi:
            bisect.insort(win, ft[hi]); hi += 1
        while lo < want_lo:
            win.pop(bisect.bisect_left(win, ft[lo])); lo += 1
        m = win[len(win) // 2] if len(win) % 2 else (win[len(win) // 2 - 1] + win[len(win) // 2]) / 2
        if v > max(50.0, 3 * m):
            total += v - m
    return total


def frames(d, b):
    """(frame times in ms of the play, the seconds they span) or ([], 0): from WARMUP_S after the load,
    the reload segments of a looped demo left out"""
    path = d / "hud" / "frametime_(ms)"
    if not path.exists():
        return [], 0.0
    ft = report.floats(path)
    if not ft:
        return [], 0.0
    ends = report.frame_ends(ft, b)
    start = (play_start(d, b) or 0.0) + WARMUP_S
    rel = report.reload_segments(d, b) or []
    secs = report.num(b.get("seconds")) or float("inf")   # frames past the run's end: the clock drifted
    cf = crash_freeze(d, b)
    if cf is not None:
        secs = min(secs, cf)
    gaps = load_gaps(d, b)
    keep = [v for e, v in zip(ends, ft)
            if e - v / 1000 >= start - 0.05 and e <= secs and not any(s < e <= t for s, t in rel)
            and not any(e > g0 and e - v / 1000 < g1 for g0, g1 in gaps)]
    return keep, sum(keep) / 1000


SHOT_WINDOW = (-0.5, 2.0)   # seconds around a screenshot whose dropouts are the recorder's, not the game's


def shot_times(d):
    """when the run's screenshots were taken, seconds after its start (bench.txt shot_t= lines)"""
    out = []
    f = d / "bench.txt"
    for line in (f.read_text(errors="replace").splitlines() if f.exists() else []):
        if line.startswith("shot_t="):
            parts = line[7:].split(":")
            if len(parts) > 1 and report.num(parts[1]) is not None:
                out.append(report.num(parts[1]))
        elif line.startswith("shot_mark="):      # the player's own shots: a capture costs a stall as well
            t = report.num(line[10:].split(":")[0].rstrip("s"))
            if t is not None:
                out.append(t)
    return out


def recorder_dropouts(d, audible, after=None, gaps=()):
    """(audible dropouts the game caused, those at a screenshot): capturing the screen reads 8 MB of
    video memory and starves the sound for a moment (X-Moto, 2026-10-24: 14 of 25 audible dropouts
    within a second or two of a capture), so dropouts in SHOT_WINDOW of one are the recorder's.
    after: dropouts before it (seconds after the run's start: the load and the warm-up) don't count.
    Without xruns.tsv, the harness's count as it is."""
    tsv, shots = d / "xruns.tsv", shot_times(d)
    if not tsv.exists() or (not shots and after is None):
        return audible, 0
    lines = tsv.read_text(errors="replace").splitlines()
    head = lines[0].split("\t") if lines else []
    if "at_s" not in head or "audible" not in head:
        return audible, 0
    at, aud = head.index("at_s"), head.index("audible")
    kept = by_shots = 0
    for line in lines[1:]:
        c = line.split("\t")
        if len(c) <= max(at, aud) or c[aud] != "1":
            continue
        t = report.num(c[at])
        if t is not None and after is not None and t < after:
            continue
        if t is not None and any(g0 <= t <= g1 for g0, g1 in gaps):    # a later level's load and warm-up
            continue
        if t is not None and any(s + SHOT_WINDOW[0] <= t <= s + SHOT_WINDOW[1] for s in shots):
            by_shots += 1
        else:
            kept += 1
    return kept, by_shots


def numbers(d, b, r):
    """the measured numbers of the run (result.toml [numbers]), None for what can't be told"""
    n = {}
    seconds, load_end = report.num(b.get("seconds")), play_start(d, b)
    ft, span = frames(d, b)
    if len(ft) > 100:
        ms = sorted(ft)
        med = statistics.median(ms)
        thr = max(50.0, 3 * med)
        n["median_fps"] = round(1000 / med, 1)
        n["low1_fps"] = round(1000 / ms[max(0, -(-len(ms) * 99 // 100) - 1)], 1)
        n["stutter_s_per_min"] = round(sum(v - med for v in ms if v > thr) / 1000 / (span / 60), 2)
        # rules 2: a frame stutters against the frames around it (LOCAL_FRAMES each side), not the session's
        # median: a heavy level running steadily slower is the frame rate's matter, not a stutter
        n["stutter_local_s_per_min"] = round(local_stutter(ft) / 1000 / (span / 60), 2)
        n["longest_ms"] = round(ms[-1], 1)
        n["frames"] = len(ms)
    elif r.get("steady") is not None and r.get("low1") is not None:   # per-second data only
        n["median_fps"], n["low1_fps"] = float(r["steady"]), float(r["low1"])
    play = 0.0
    if seconds is not None and (load_end is not None or "window" in b):
        play = max(0.0, seconds - (load_end or 0.0))
    n["play_seconds"] = round(play, 1)
    faults = sum(int(report.num(b.get(k)) or 0) for k in ("underruns", "oal_xrun", "oal_wait"))
    if play >= 30:
        n["sound_faults_per_min"] = round(faults / (play / 60), 2)
    n["sound_faults"] = faults
    # the dropouts ps3-xrunlog measured (bench/harness/xruns.py): the audible ones, plus OpenAL's waits
    # (each a second without sound), replace the fault counts in the score where the run has them
    if b.get("xrunlog") == "on" and report.num(b.get("xruns_audible")) is not None:
        start = play_start(d, b)
        audible, by_shots = recorder_dropouts(d, int(report.num(b["xruns_audible"])),
                                              None if start is None else start + WARMUP_S, load_gaps(d, b))
        dropouts = audible + int(report.num(b.get("oal_wait")) or 0)
        n["sound_dropouts"] = dropouts
        if by_shots:
            n["sound_dropouts_at_shots"] = by_shots
        if play - WARMUP_S >= 30:
            n["sound_dropouts_per_min"] = round(dropouts / ((play - WARMUP_S) / 60), 2)
        n["xruns"] = int(report.num(b.get("xruns_logged")) or 0)
        n["xrun_gap_ms"] = round(report.num(b.get("xrun_gap_ms_total")) or 0.0, 1)
    display_numbers(d, b, n)
    n["snd_stall"] = int(report.num(b.get("snd_stall")) or 0)
    # the player's answers (session.sh's form): what they heard counts for half the sound score
    form = d / "session.toml"
    if form.exists():
        f = tomllib.loads(form.read_text(errors="replace"))
        if f.get("sound") in ("yes", "partly", "no"):
            n["sound_effects"] = f["sound"]
        if f.get("music") in ("yes", "no", "none"):
            n["music"] = f["music"]
    # the load is the start-up (launch to the game's first screen: bench.txt load_end), never the time
    # the player spends in the menus before a match (a played session's play mark comes after those)
    # (in a played session load_end is the play mark; hud_t1, the game's first frame, is its start-up)
    startup = report.num(b.get("hud_t1")) if b.get("play_from") == "mark" else report.num(b.get("load_end"))
    if startup is None and b.get("play_from") != "mark":
        startup = load_end
    if startup is not None:
        n["load_s"] = round(startup, 1)
    if r.get("avail") is not None:
        n["min_avail_kb"] = int(r["avail"])
    if r.get("rss") is not None:
        n["rss_mib"] = r["rss"]
    return n


def geometry(d):
    """the session's window samples (shots/geometry.tsv: at_s x y w h fullscreen screen_w screen_h, "-" where
    no window was found): [(x, y, w, h, fullscreen, sw, sh) or None], [] without the file"""
    f = d / "shots" / "geometry.tsv"
    if not f.exists():
        return []
    lines = f.read_text(errors="replace").splitlines()
    if not lines or lines[0].split("\t")[:1] != ["at_s"]:
        return []
    rows = []
    for line in lines[1:]:
        c = line.split("\t")
        if len(c) < 8:
            continue
        v = [report.num(x) for x in c[1:8]]
        if None not in v and v[2] > 0 and v[3] > 0 and v[5] > 0 and v[6] > 0:
            rows.append(tuple(int(x) for x in v))
        else:
            rows.append(None)   # "-": the shot found no game window
    return rows


def display_numbers(d, b, n):
    """Display: the score from the window samples (score.display_score); the game's own render resolution
    (bench.txt render_res=WxH, where a recipe logs it) is recorded, never scored"""
    score_, detail = score.display_score(geometry(d))
    if score_ is not None:
        n["display_score"], n["display_detail"] = score_, detail
        n["display_samples"] = sum(1 for r in geometry(d) if r)
    if re.fullmatch(r"\d+x\d+", b.get("render_res") or ""):
        n["render_res"] = b["render_res"]


def geometry_raw(d):
    """(samples as "x y w h fullscreen sw sh" or "-", pointer samples, pointer visible) of shots/geometry.tsv:
    the raw rows result.toml keeps (data version 2), and the X pointer over the window (geom.py's cursor column:
    1 visible over the window, 0 not, "-" unknown)"""
    f = d / "shots" / "geometry.tsv"
    if not f.exists():
        return [], 0, 0
    lines = f.read_text(errors="replace").splitlines()
    if not lines or lines[0].split("\t")[:1] != ["at_s"]:
        return [], 0, 0
    head = lines[0].split("\t")
    ci = head.index("cursor") if "cursor" in head else None
    out, known, visible = [], 0, 0
    for row, line in zip(geometry(d), lines[1:]):
        out.append(" ".join(map(str, row)) if row else "-")
        c = line.split("\t")
        if row and ci is not None and len(c) > ci and c[ci] in ("0", "1"):
            known += 1
            visible += c[ci] == "1"
    return out, known, visible


BAR_DARK = 20       # a column or row whose brightest pixel (0-255, on a 192x108 grey copy) is under this is black
BAR_MIN = 0.02      # bars count from this share of the width (height) on each side


def gray(path, w=192, h=108):
    p = subprocess.run(["magick", str(path), "-resize", "%dx%d!" % (w, h), "-colorspace", "Gray", "-depth", "8",
                        "gray:-"], capture_output=True)
    return p.stdout if not p.returncode and len(p.stdout) == w * h else None


def bars_of(px, w=192, h=108):
    """("pillarbox" | "letterbox" | None, content w/h share) of one picture's grey bytes: black columns (rows) on
    both sides, about as wide each, a lit picture between"""
    col = lambda x: max(px[y * w + x] for y in range(h))
    row = lambda y: max(px[y * w:(y + 1) * w])
    def run(n, f):
        k = 0
        while k < n and f(k) < BAR_DARK:
            k += 1
        return k
    if sum(px) / len(px) < 12:
        return None, None       # a black frame
    left, right = run(w, col), run(w, lambda x: col(w - 1 - x))
    if left >= BAR_MIN * w and right >= BAR_MIN * w and abs(left - right) <= 0.03 * w and left + right < w:
        return "pillarbox", ((w - left - right) / w, 1.0)
    top, bottom = run(h, row), run(h, lambda y: row(h - 1 - y))
    if top >= BAR_MIN * h and bottom >= BAR_MIN * h and abs(top - bottom) <= 0.03 * h and top + bottom < h:
        return "letterbox", (1.0, (h - top - bottom) / h)
    return None, None


def bars(pictures, screen, look=gray):
    """{bars, bars_content, bars_shots, lit_shots} over a session's screenshots: bars when at least half the
    lit pictures have the same kind; the content's size on the screen (the median picture's)"""
    seen = []
    for p in pictures:
        px = look(p)
        if px is None or sum(px) / len(px) < 12:
            continue
        seen.append(bars_of(px))
    if not seen:
        return {}
    kinds = [k for k, _ in seen if k]
    out = {"lit_shots": len(seen), "bars_shots": len(kinds)}
    if kinds and 2 * len(kinds) >= len(seen):
        kind = max(set(kinds), key=kinds.count)
        shares = sorted(c for k, c in seen if k == kind)
        fw, fh = shares[(len(shares) - 1) // 2]
        out.update(bars=kind, bars_content="%dx%d" % (round(screen[0] * fw), round(screen[1] * fh)))
    return out


def key_times(d):
    """the times (s after the start) of every press inputlog.py recorded (its key rows)"""
    f = d / "input.tsv"
    out = []
    for line in (f.read_text(errors="replace").splitlines() if f.exists() else []):
        c = line.split("\t")
        if len(c) >= 4 and c[1] == "key" and report.num(c[0]) is not None:
            out.append(report.num(c[0]))
    return out


def input_numbers(d, play_from, until=None):
    """the Controls measurements of inputlog.py's input.tsv (what the game opened, pad and keyboard use):
    {} without the file"""
    f = d / "input.tsv"
    if not f.exists():
        return {}
    lines = f.read_text(errors="replace").splitlines()
    m = re.match(r"# inputlog readable=(\S*) unreadable=(\S*)", lines[0] if lines else "")
    readable = set(m[1].split(",")) if m else set()
    n = {"input_log": True, "kb_readable": "keyboard" in readable}
    pads, hot, opened, pad_ev, kb = set(), set(), set(), 0, 0
    for line in lines[1:]:
        c = line.split("\t")
        if len(c) < 5 or c[0].startswith("#") or c[0] == "t_s":
            continue
        t = report.num(c[0])
        what, dev, cls = c[1], c[2], c[3]
        if cls == "pad" and what in ("present", "added"):
            pads.add(dev)
            if what == "added" and t is not None and t > 5:
                hot.add(dev)
        elif what == "opened":
            opened.add(dev)
        elif what in ("press", "move") and t is not None and (until is None or t < until):
            k = int(report.num(c[4]) or 0)
            if cls == "pad" and t + 10 > (play_from or 0):
                pad_ev += k
            elif cls == "keyboard" and what == "press":
                kb += k
    n.update(pads_present=len(pads), pad_opened=bool(pads & opened), pad_events_play=pad_ev, kb_presses=kb,
             pads_hotplugged=len(hot), pads_hotplug_opened=len(hot & opened))
    return n


REVIEW_RE = re.compile(r"^\s*[-*]?\s*([a-z_]+):\s*(.*?)\s*$", re.I)
# the questionnaire of tools/gamedb/review.py's template: every one answered under rules 2
REVIEW_REQUIRED = ["verdict", "stretched", "text_px", "pointer"] + list(score.DEFECTS) + ["broken"]


def answer_where(v):
    """("answer", "where") of "answer: where" or "answer" """
    a, _, w = v.partition(":")
    return a.strip().lower(), w.strip()


def review_items(d):
    """the rules-2 answers of REVIEW.md ({defects, text_px, pointer, stretched, stretched_where, checks,
    broken}); Refused for an unanswered question (a missing line or "?") or one that can't be read. The
    template (review.py) has one line per question; `defect: <category> <pc|ps3>: <what>` and repeated
    category or broken lines add more"""
    f = d / "REVIEW.md"
    if not f.exists():
        raise Refused("%s: no REVIEW.md (tools/gamedb/review.py %s writes the questions)" % (d.name, d.name))
    out, seen = {}, set()
    bad = lambda line, want: Refused("REVIEW.md: %r: want %s" % (line.strip(), want))
    for line in f.read_text(errors="replace").splitlines():
        if line.lstrip().startswith("#"):
            continue
        m = REVIEW_RE.match(line)
        if not m:
            continue
        k, v = m[1].lower(), m[2]
        if v.strip() in ("", "?"):
            continue                      # unanswered: reported below
        if k == "verdict":
            seen.add(k)
        elif k == "defect":
            head, _, what = v.partition(":")
            words = head.split()
            if len(words) != 2 or not score.defect_category(words[0]) or words[1].lower() not in score.ORIGINS:
                raise bad(line, "`defect: <%s> <pc|ps3>: <what>`" % "|".join(score.DEFECTS))
            out.setdefault("defects", []).append("%s %s: %s" % (score.defect_category(words[0]), words[1].lower(), what.strip()))
        elif score.defect_category(k) and k in score.DEFECTS:
            seen.add(k)
            a, where = answer_where(v)
            if a == "no":
                continue
            if a not in score.ORIGINS:
                raise bad(line, "`%s: no`, or `%s: pc|ps3: where`" % (k, k))
            out.setdefault("defects", []).append("%s %s: %s" % (k, a, where))
        elif k == "text_px":
            seen.add(k)
            a, where = answer_where(v)
            if a == "none":
                out["text_px_none"] = True
            elif a.isdigit():
                out["text_px"] = int(a)
                if where:
                    out["text_where"] = where
            else:
                raise bad(line, "`text_px: N: where` (pixels at 1080p) or `text_px: none`")
        elif k in ("pointer", "stretched"):
            seen.add(k)
            a, where = answer_where(v)
            ok = ("visible", "hidden") if k == "pointer" else ("yes", "no")
            if a not in ok:
                raise bad(line, "%s: %s" % (k, " or ".join(ok)))
            out[k] = a
            if k == "stretched" and where:
                out["stretched_where"] = where
        elif k == "check":
            item, _, ans = v.partition(" ")
            if item not in score.CHECK_NAMES or ans.strip() not in ("yes", "partly", "no", "na"):
                raise bad(line, "`check: <%s> yes|partly|no|na`" % "|".join(score.CHECK_NAMES))
            out.setdefault("checks", []).append("%s %s" % (item, ans.strip()))
        elif k == "broken":
            seen.add(k)
            if v.strip().lower() == "none":
                continue
            head, _, what = v.partition(":")
            words = head.split()
            if len(words) != 2 or words[0] not in score.CHECK_NAMES or words[1].lower() not in score.ORIGINS:
                raise bad(line, "`broken: none`, or `broken: <item> <pc|ps3>: <what>` (items: %s)" % ", ".join(score.CHECK_NAMES))
            out.setdefault("broken", []).append("%s %s: %s" % (words[0], words[1].lower(), what.strip()))
    missing = [q for q in REVIEW_REQUIRED if q not in seen]
    if missing:
        raise Refused("%s: REVIEW.md leaves unanswered: %s (tools/gamedb/review.py makes the template)" % (
            d.name, ", ".join(missing)))
    return out


FORM_KEYS = {"glitches": ("none", "minor", "broken"), "pad_menus": tuple(score.FORM_PAD_MENUS),
             "pad_play": tuple(score.FORM_PAD_PLAY), "keyboard_mouse": tuple(score.FORM_KM),
             "menus_modes": ("yes", "no"), "saves_load": ("yes", "no", "na")}


def form_v2(d):
    """(form_version, {the second form's answers}) of session.toml; (0, {}) without one"""
    f = d / "session.toml"
    if not f.exists():
        return 0, {}
    t = tomllib.loads(f.read_text(errors="replace"))
    return int(t.get("form_version", 1)), {k: t[k] for k, ok in FORM_KEYS.items() if t.get(k) in ok}


def v2_numbers(d, b, n, sh):
    """the raw data points of data version 2, added to n"""
    rows, known, visible = geometry_raw(d)
    if rows:
        n["geometry"] = rows
    if known:
        n["pointer_samples"], n["pointer_visible"] = known, visible
    screen = next((tuple(int(x) for x in r.split()[5:7]) for r in rows if r != "-"), (1920, 1080))
    pics = [shots.image(d, s) for _, s in sorted(sh["marks"].items()) if shots.image(d, s)]
    pics += [shots.image(d, s) for _, s in sorted(sh["shots"].items()) if shots.image(d, s)]
    n.update(bars(pics, screen))
    if report.num(b.get("desktop_back_s")) is not None:
        n["desktop_back_s"] = report.num(b["desktop_back_s"])
    if b.get("desktop_stuck") == "1":
        n["desktop_stuck"] = True
    n.update(input_numbers(d, play_start(d, b), crash_freeze(d, b)))
    for k in ("fresh_home", "fresh_home_new", "recovered"):    # the fresh defaults (freshhome.py)
        if report.num(b.get(k)) is not None:
            n[k] = int(report.num(b[k]))
    # the level load: from the player's last press (else the long frames) before each play mark to the mark,
    # the median over the session's levels (level_loads)
    ll = [m - s for s, m in level_loads(d, b) if m > s]
    if ll:
        n["load_level_s"] = round(statistics.median(ll), 1)
        n["level_loads"] = len(ll)


def ended(d, b):
    """(end state, crashed): the harness's own stops aren't crashes"""
    end, rc = b.get("ended") or "?", report.num(b.get("rc"))
    crashed = (end == "exit" and rc is not None and rc > 128) or end in ("crash", "oom")
    dm = d / "dmesg.log"
    if dm.exists() and CRASH_RE.search(dm.read_text(errors="replace")):
        crashed = True
    return ("crash" if crashed else end), crashed


def versions(d):
    out = {}
    f = d / "versions"
    if f.exists():
        for line in f.read_text(errors="replace").splitlines():
            p, _, v = line.partition("\t")
            if not v:
                if line.strip():
                    out["kernel"] = line.strip()
            elif p.split(":")[0] not in SKIP_PKGS and v.strip():
                out[p.split(":")[0]] = v.strip()
    return out


def jpeg(src, dst):
    e = evidence.jpeg(src, dst)
    if e:
        raise Refused(e)


SHOTS_MAX = 4      # pictures published per test
SHOT_SAME = 6.0    # mean difference (0-255) of two 32x18 greyscale versions under which they're one picture
SHOT_DARK = 12     # mean brightness under which a frame is a black (or fading) screen


def tiny(path):
    """a picture as 32x18 greyscale bytes, for telling pictures apart"""
    p = subprocess.run(["magick", str(path), "-resize", "32x18!", "-colorspace", "Gray", "-depth", "8", "gray:-"],
                       capture_output=True)
    if p.returncode or len(p.stdout) != 32 * 18:
        raise Refused("magick %s: can't read it" % Path(path).name)
    return p.stdout


def pick_shots(items, keep=None, n=SHOTS_MAX, look=tiny):
    """the pictures to publish of [(point, path)], in time order: the points in keep when given (a
    reviewer's choice), else up to n that differ: black frames and near-duplicates dropped (the same
    menu or intro screen caught several times), then the most varied first, each next one the most
    unlike those already taken"""
    if keep:
        return [(p, f) for p, f in items if p in keep]
    vec = {p: look(f) for p, f in items}
    diff = lambda a, b: sum(abs(x - y) for x, y in zip(vec[a], vec[b])) / len(vec[a])
    lit = [(p, f) for p, f in items if sum(vec[p]) / len(vec[p]) >= SHOT_DARK]
    distinct = []
    for p, f in lit:
        if all(diff(p, q) >= SHOT_SAME for q, _ in distinct):
            distinct.append((p, f))
    if len(distinct) <= n:
        return distinct
    spread = lambda p: statistics.pstdev(vec[p])
    chosen = [max(distinct, key=lambda x: spread(x[0]))]
    while len(chosen) < n:
        rest = [x for x in distinct if x not in chosen]
        chosen.append(max(rest, key=lambda x: min(diff(x[0], q) for q, _ in chosen)))
    return sorted(chosen)


MARKS_MAX = 6      # the player's own shots (Alt+F12, Select+Start): all of them, up to this many


shot_name = evidence.shot_name    # a shot's file in the session's shots/


def pick_all(d, sh, keep=None, look=None):
    """[(point, picture)] to publish: the player's marked shots first (always, up to MARKS_MAX: the player's
    evidence, e.g. a glitch), then the representative timed ones to fill SHOTS_MAX in all"""
    marks = [(t, shots.image(d, s)) for t, s in sorted(sh["marks"].items()) if shots.image(d, s)][:MARKS_MAX]
    timed = [(p, shots.image(d, s)) for p, s in sorted(sh["shots"].items()) if shots.image(d, s)]
    kw = {"look": look} if look else {}
    if not marks:
        return pick_shots(timed, keep, **kw)
    room = SHOTS_MAX - len(marks)
    return marks + (pick_shots(timed, keep, n=room, **kw) if room > 0 else [])


def toml_value(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, list):
        return "[" + ", ".join(toml_value(x) for x in v) + "]"
    raise TypeError(type(v))


def toml_dump(top, tables):
    """TOML text: the scalars of top, then one [table] per entry of tables; None values are left out"""
    lines = ["# generated by tools/gamedb/import-run.py from a harness run: do not edit (import again)"]
    lines += ["%s = %s" % (k, toml_value(v)) for k, v in top.items() if v is not None]
    for name, t in tables.items():
        t = {k: v for k, v in t.items() if v is not None}
        if t:
            lines += ["", "[%s]" % name]
            lines += ["%s = %s" % (json.dumps(k) if not re.fullmatch(r"[A-Za-z0-9_-]+", k) else k, toml_value(v))
                      for k, v in t.items()]
    return "\n".join(lines) + "\n"


def precheck(d, b, game, content, launcher, r=None):
    """the refusals a run directory meets before anything is written; (kind, date, clock). r: report.run(d)"""
    r = report.run(d) if r is None else r
    why = []
    if r.get("intf") == 1:
        why.append(r.get("intf_why", "interfered"))
    blob = b.get("interference", "") + " " + (d / "result").read_text(errors="replace") if (d / "result").exists() else ""
    if b.get("temp_stop") == "1" or re.search(r"TEMPLIMIT|THERMAL", blob):
        why.append("stopped by the temperature limit")
    if why:
        raise Refused("%s: refused, %s" % (d.name, "; ".join(why)))
    recorded = (b.get("launcher") or "").strip() or None
    if launcher and recorded and launcher != recorded:
        raise Refused("%s: recorded the launcher %r, not %r (--launcher)" % (d.name, recorded, launcher))
    sh = shots.parse(d)
    has_shots = any(shots.image(d, s) for s in list(sh["shots"].values()) + list(sh["marks"].values()))
    if has_shots and shots.review(d) is None:
        raise Refused("%s: has screenshots but no REVIEW.md (nobody looked at the sheet yet)" % d.name)
    kind = "session" if b.get("kind") == "session" else "demo" if report.window(b) else "capture"
    m = re.search(r"(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})Z", d.name)
    if not m:
        raise Refused("%s: no timestamp in the directory name" % d.name)
    if not (Path(content) / game / "index.md").exists():
        raise Refused("%s: no %s/index.md (write the catalog entry first)" % (game, Path(content) / game))
    return kind, "%s-%s-%s" % m.groups()[:3], "".join(m.groups()[3:5])


def derive(d, game, content, launcher=None, keep=None, rules=None):
    """(top, tables) of result.toml from run directory d (a run rebuilt from a session's evidence:
    evidence.rebuild), and the line import_run prints"""
    b = report.kv(d / "bench.txt")
    if not b:
        raise Refused("%s: no bench.txt" % d.name)
    r = report.run(d)
    kind, date, _ = precheck(d, b, game, content, launcher, r)
    recorded = (b.get("launcher") or "").strip() or None
    launcher = recorded or (launcher or "").strip() or None
    sh = shots.parse(d)
    has_shots = any(shots.image(d, s) for s in list(sh["shots"].values()) + list(sh["marks"].values()))
    verdict = (shots.review(d) or "").rstrip(":;,.") or None   # shots.review keeps a colon: "ok:"
    fm = front_matter(Path(content) / game / "index.md")
    n = numbers(d, b, r)
    fv, form = form_v2(d)
    data = int(str(rules).split(".")[0]) if rules else (2 if fv >= 2 else 1)
    if data not in score.FORMULA:
        raise Refused("--rules %s: the data versions are %s" % (rules, ", ".join(map(str, score.FORMULA))))
    review = review_items(d) if data >= 2 and (has_shots or (d / "REVIEW.md").exists()) else {}
    if data >= 2:
        if verdict == "wrong" and not review.get("defects"):
            raise Refused("%s: verdict: wrong needs a `defect: <category> <pc|ps3>: <what>` line (rules 2)" % d.name)
        v2_numbers(d, b, n, sh)
        n.pop("display_score", None), n.pop("display_detail", None)   # scored at catalog time from geometry
    end, crashed = ended(d, b)
    play = n.pop("play_seconds")
    ps3 = pick_all(d, sh, keep)
    if verdict in ("ok", "wrong") and ps3:
        n["shot_points"] = len(ps3)
        n["shot_differing"] = 0 if verdict == "ok" else len(ps3)
    scores = score.test_scores(n, fm.get("fps_cap"))
    # the record keeps its data version only ("2.0"): the formula revision is applied at catalog time (a
    # formula change re-scores every test of the data version without a re-import, docs/GAMEDB.md "Rules versions")
    top = {"schema": 1, "rules": "%d.0" % data, "kind": kind, "date": date, "run": d.name,
           "game": game, "variant": b.get("variant"),
           "launcher": launcher, "play_seconds": play, "end": end, "crashed": crashed, "started": True, "reached_play": play > 0,
           "render": "1920x1080"}
    note = ""
    if (d / "REVIEW.md").exists():
        lines = (d / "REVIEW.md").read_text(errors="replace").split("\n")
        first = next((x for x in lines if re.match(r"\s*verdict:", x, re.I)), lines[0])   # the template opens with notes
        note = re.sub(r"^verdict:\s*", "", " ".join(first.split()))
    tables = {"numbers": n, "scores": {k: round(v, 1) for k, v in scores.items() if v is not None},
              "versions": versions(d),
              "picture": {"verdict": verdict, "note": note or None},
              "shots": {"ps3": ["shots/" + shot_name(p) for p, _ in ps3]}}
    if data >= 2:   # raw numbers and answers only: the catalog scores them with the current formula
        del tables["scores"]
        tables["form"], tables["review"] = form, review
    return top, tables, "%s (%s, %.0f s of play, %s)" % (d.name, kind, play, end)


def derive_session(sdir, game, content):
    """(result.toml's text, the summary line) derived from session folder sdir's evidence alone: its run
    rebuilt from raw/, shots/, form.toml and review.md (evidence.rebuild), imported with raw/import.toml's
    inputs. The one derivation: import_run and reimport.py both call it"""
    opts = evidence.read_import(sdir)
    with tempfile.TemporaryDirectory(prefix="gamedb-rebuild-") as tmp:
        try:
            rd = evidence.rebuild(sdir, tmp)
        except ValueError as e:
            raise Refused(str(e))
        top, tables, line = derive(rd, game, content, opts.get("launcher"), opts.get("shots"), opts.get("rules"))
    return toml_dump(top, tables), line


def kept_record(old_text, sdir):
    """a frozen record (--keep-record) with its gallery in shots/: the [shots] ps3 names get the shots/ folder"""
    t = tomllib.loads(old_text)
    names = t.get("shots", {}).get("ps3", [])
    new = [x if x.startswith("shots/") else "shots/" + x for x in names]
    if names and new != names:
        out, n = re.subn(r"(?m)^ps3 = \[.*\]$", "ps3 = " + toml_value(new), old_text, count=1)
        if not n:
            raise Refused("%s: result.toml has no `ps3 = [...]` line to move into shots/" % sdir.name)
        return out
    return old_text


def import_run(run, game, content, dry=False, launcher=None, keep=None, rules=None, session=None, keep_record=False):
    """import run directory run as a session of game: its evidence into the session folder (evidence.collect),
    then the record derived from that evidence alone (derive_session). session: the folder's name (default
    <date>-<kind>, -<hhmm> for another run that day). keep_record: keep the folder's result.toml as it is
    (its gallery moved into shots/): a record derived by an older importer, whose derivation is gone
    (raw/import.toml frozen = true; reimport.py leaves it)"""
    d = Path(run)
    b = report.kv(d / "bench.txt")
    if not b:
        raise Refused("%s: no bench.txt" % d.name)
    kind, date, clock = precheck(d, b, game, content, launcher)
    sessions = Path(content) / game / check.SESSIONS
    tdir = sessions / (session or "%s-%s" % (date, kind))
    old = tdir / "result.toml"
    old_run = tomllib.loads(old.read_text()).get("run") if old.exists() else None
    if old_run and old_run != d.name:
        if session:
            raise Refused("%s: %s holds the run %s" % (d.name, tdir.name, old_run))
        tdir = sessions / ("%s-%s-%s" % (date, kind, clock))
        old = tdir / "result.toml"
    if keep_record and not old.exists():
        raise Refused("%s: --keep-record: %s has no result.toml to keep" % (d.name, tdir.name))
    opts = {"run": d.name, "game": game, "launcher": launcher, "shots": list(keep) if keep else None,
            "rules": str(rules) if rules else None, "frozen": True if keep_record else None}
    with tempfile.TemporaryDirectory(prefix="gamedb-import-") as tmp:
        stage = Path(tmp) / tdir.name
        errs = evidence.collect(d, stage, opts)
        if errs:
            raise Refused("; ".join(errs))
        if keep_record:
            text = kept_record(old.read_text(), tdir)
            for f in tomllib.loads(text).get("shots", {}).get("ps3", []):
                if not (stage / f).exists():      # the run lost that picture: the published one stays
                    src = tdir / f if (tdir / f).exists() else tdir / f.removeprefix("shots/")
                    if not src.exists():
                        raise Refused("%s: the gallery's %s is neither in the run nor in %s" % (d.name, f, tdir.name))
                    shutil.copyfile(src, stage / f)
            line = "%s (kept: %s)" % (d.name, tdir.name)
        else:
            text, line = derive_session(stage, game, content)
        (stage / "result.toml").write_text(text)
        print("%s -> %s" % (line.split(" ", 1)[0], tdir.relative_to(REPO) if tdir.is_relative_to(REPO) else tdir.name)
              + "  " + line.split(" ", 1)[1])
        if dry:
            return
        if tdir.exists():
            shutil.rmtree(tdir)
        tdir.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(stage, tdir)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--game", required=True, help="the game's id (its folder under the content directory)")
    ap.add_argument("--content", default=str(check.CONTENT))
    ap.add_argument("--launcher", help="the menu launcher's Name the run started (for runs that recorded none)")
    ap.add_argument("--shots", help="the screenshot points to publish, e.g. 60,240 (default: up to %d that differ)"
                    % SHOTS_MAX)
    ap.add_argument("--rules", help="the data version to file the run under, e.g. 2.0 (default: 2 for a session "
                    "with the second form, else 1)")
    ap.add_argument("--session", help="the session folder's name (default <date>-<kind>, -<hhmm> for another run "
                    "that day); refused when it holds another run")
    ap.add_argument("--keep-record", action="store_true", help="keep the session's result.toml as it is (a record "
                    "an older importer derived): collect the evidence only, the gallery moves into shots/")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    bad = 0
    for run in a.runs:
        try:
            import_run(run, a.game, a.content, a.dry_run, a.launcher,
                       [int(x) for x in a.shots.split(",")] if a.shots else None, a.rules, a.session, a.keep_record)
        except Refused as e:
            print("!! %s" % e, file=sys.stderr)
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
