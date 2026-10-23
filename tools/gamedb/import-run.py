#!/usr/bin/env python3
"""import-run.py RUN... --game ID [--launcher NAME] [--ref DIR] [--content DIR] [--dry-run]: turn games/bench run
directories into a game's tests in the Walnut Games catalog (docs/GAMEDB.md).

Design
- Input: a run directory of the harness (games/bench/run.sh, make bench-pull: bench.txt, versions,
  hud/, game.log, dmesg.log, REVIEW.md, shots/). games/bench/report.py's run() supplies the numbers
  it already derives (steady fps, 1% low, the interference verdict, memory, the screenshots' review);
  this tool adds the ones the rating needs and report.py doesn't compute:
    stutter   frames longer than max(50 ms, 3 x the median frame); the time they lost is their
              length minus the median frame, summed per minute of the frames' own time. The frames
              are those after the load, without the reload segments of a looped demo
    sound     (alsa underruns + OpenAL xruns + OpenAL waits) per minute of (seconds - play start);
              snd_stall > 0 is a wedge
    play      seconds - the play start, for runs that reached play (menu-only runs count 0)
    end       "crash" when ended=exit with rc > 128 or the kernel log has a segfault/oom line;
              "window"/"duration" are the harness's own stops, not crashes
  The play start is the recipe's own mark where it has one (Warzone: the line the game prints when
  the match starts), else bench.txt's load_end; the load time is the same moment.
- Output: <content>/<id>/tests/<date>-<kind>/result.toml, written by a small hand-rolled TOML writer
  (stdlib has a reader only) under a "generated, do not edit" header, and the screenshots next to it:
  ps3-<point>.jpg and pc-<point>.jpg at 800x450, JPEG quality 82 (ImageMagick `magick`), which the
  repository keeps in Git LFS (.gitattributes). index.md is never touched. The game must exist.
- kind: "session" (bench.txt kind=session, the recorder of phase P3), "demo" (a harness run with a
  measured window: stands in for a session until the game has one), "capture" (shots only: grades
  the picture, nothing else).
- launcher: result.toml gets `launcher = "<Name>"` (the game's menu entry the session started, each
  launcher is rated on its own) from bench.txt's launcher=, or from --launcher NAME for runs that
  recorded none; a different --launcher than the recorded one is refused. Neither: no key (the
  catalog takes the game's default launcher).
- Refused: runs with interfered=1 (or TEMPLIMIT / thermal stop), capture runs without REVIEW.md.
- No absolute path, host name or user name is written: the run is named by its directory name only.
Importing the same run again rewrites its test directory; another run of the same day and kind gets
the time of day in its directory name.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path[:0] = [str(REPO / "games" / "bench"), str(HERE)]   # python3 -I keeps the script's directory out
import report   # noqa: E402  (games/bench/report.py)
import score    # noqa: E402
import shots    # noqa: E402

CRASH_RE = re.compile(r"segfault|general protection|out of memory|oom-kill|killed process", re.I)
SKIP_PKGS = shots.SKIP_PKGS
JPEG_SIZE, JPEG_QUALITY = "800x450", "82"


class Refused(Exception):
    pass


def front_matter(index):
    """the TOML front matter (between +++ lines) of a game's index.md"""
    text = index.read_text()
    m = re.match(r"\+\+\+\n(.*?)\n\+\+\+", text, re.S)
    return tomllib.loads(m[1]) if m else {}


def play_start(d, b):
    """seconds from the client's start to the first gameplay frame: the recipe's own mark where it has
    one (SHOT_RE / shot_mark: the line the game prints when the match starts, shot_ref=re|mark:<s>),
    else the end of the load (bench.txt load_end). None when neither is known"""
    sh = shots.parse(d)
    if sh["ref"] in ("re", "mark") and sh["ref_t"] is not None:
        return sh["ref_t"]
    return report.num(b.get("load_end"))


def frames(d, b):
    """(frame times in ms of the play, the seconds they span) or ([], 0): after the load, the reload
    segments of a looped demo left out"""
    path = d / "hud" / "frametime_(ms)"
    if not path.exists():
        return [], 0.0
    ft = report.floats(path)
    if not ft:
        return [], 0.0
    ends = report.frame_ends(ft, b)
    start = play_start(d, b) or 0.0
    rel = report.reload_segments(d, b) or []
    secs = report.num(b.get("seconds")) or float("inf")   # frames past the run's end: the clock drifted
    keep = [v for e, v in zip(ends, ft)
            if e - v / 1000 >= start - 0.05 and e <= secs and not any(s < e <= t for s, t in rel)]
    return keep, sum(keep) / 1000


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
    n["snd_stall"] = int(report.num(b.get("snd_stall")) or 0)
    # the player's answers (session.sh's form): what they heard counts for half the sound score
    form = d / "session.toml"
    if form.exists():
        f = tomllib.loads(form.read_text(errors="replace"))
        if f.get("sound") in ("yes", "partly", "no"):
            n["sound_effects"] = f["sound"]
        if f.get("music") in ("yes", "no", "none"):
            n["music"] = f["music"]
    if load_end is not None:
        n["load_s"] = round(load_end, 1)
    if r.get("avail") is not None:
        n["min_avail_kb"] = int(r["avail"])
    if r.get("rss") is not None:
        n["rss_mib"] = r["rss"]
    return n


def ended(d, b):
    """(end state, crashed): the harness's own stops aren't crashes"""
    end, rc = b.get("ended") or "?", report.num(b.get("rc"))
    crashed = end == "exit" and rc is not None and rc > 128
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
    p = subprocess.run(["magick", str(src), "-resize", JPEG_SIZE, "-strip", "-quality", JPEG_QUALITY, str(dst)],
                       capture_output=True, text=True)
    if p.returncode:
        raise Refused("magick %s: %s" % (src.name, p.stderr.strip()[-200:]))


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


def import_run(run, game, content, ref_root, dry=False, launcher=None):
    d = Path(run)
    b = report.kv(d / "bench.txt")
    if not b:
        raise Refused("%s: no bench.txt" % d.name)
    r = report.run(d)
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
    launcher = recorded or (launcher or "").strip() or None
    sh = shots.parse(d)
    has_shots = any(shots.image(d, s) for s in sh["shots"].values())
    verdict = (shots.review(d) or "").rstrip(":;,.") or None   # shots.review keeps a colon: "ok:"
    windowed = bool(report.window(b))
    kind = "session" if b.get("kind") == "session" else "demo" if windowed else "capture"
    if has_shots and verdict is None:
        raise Refused("%s: has screenshots but no REVIEW.md (nobody looked at the sheet yet)" % d.name)
    m = re.search(r"(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})Z", d.name)
    if not m:
        raise Refused("%s: no timestamp in the directory name" % d.name)
    date, clock = "%s-%s-%s" % m.groups()[:3], "".join(m.groups()[3:5])
    gdir = Path(content) / game
    if not (gdir / "index.md").exists():
        raise Refused("%s: no %s/index.md (write the catalog entry first)" % (game, gdir))
    fm = front_matter(gdir / "index.md")
    tdir = gdir / "tests" / ("%s-%s" % (date, kind))
    old = tdir / "result.toml"
    if old.exists() and tomllib.loads(old.read_text()).get("run") != d.name:
        tdir = gdir / "tests" / ("%s-%s-%s" % (date, kind, clock))
    n = numbers(d, b, r)
    end, crashed = ended(d, b)
    play = n.pop("play_seconds")
    ps3 = [(p, shots.image(d, s)) for p, s in sorted(sh["shots"].items()) if shots.image(d, s)]
    rd = shots.ref_dir(sh, ref_root) if ref_root else None
    rsh = shots.parse(rd) if rd else None
    pcs = [(p, shots.image(rd, s)) for p, s in sorted(rsh["shots"].items()) if shots.image(rd, s)] if rsh else []
    if verdict in ("ok", "wrong") and ps3:
        n["shot_points"] = len(ps3)
        n["shot_differing"] = 0 if verdict == "ok" else len(ps3)
    scores = score.test_scores(n, fm.get("fps_cap"))
    top = {"schema": 1, "kind": kind, "date": date, "run": d.name, "game": game, "variant": b.get("variant"),
           "launcher": launcher, "play_seconds": play, "end": end, "crashed": crashed, "started": True, "reached_play": play > 0,
           "render": "1920x1080"}
    note = ""
    if (d / "REVIEW.md").exists():
        note = " ".join((d / "REVIEW.md").read_text(errors="replace").split("\n")[0].split())
        note = re.sub(r"^verdict:\s*", "", note)
    tables = {"numbers": n, "scores": {k: round(v, 1) for k, v in scores.items() if v is not None},
              "versions": versions(d),
              "picture": {"verdict": verdict, "note": note or None},
              "shots": {"ps3": ["ps3-%ds.jpg" % p for p, _ in ps3], "pc": ["pc-%ds.jpg" % p for p, _ in pcs]}}
    print("%s -> %s  (%s, %.0f s of play, %s)" % (d.name, tdir.relative_to(REPO) if tdir.is_relative_to(REPO)
                                                    else tdir.name, kind, play, end))
    if dry:
        return
    tdir.mkdir(parents=True, exist_ok=True)
    for p, src in ps3:
        jpeg(src, tdir / ("ps3-%ds.jpg" % p))
    for p, src in pcs:
        jpeg(src, tdir / ("pc-%ds.jpg" % p))
    (tdir / "result.toml").write_text(toml_dump(top, tables))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--game", required=True, help="the game's id (its folder under the content directory)")
    ap.add_argument("--ref", default=str(REPO / "build" / "bench-ref"), help="PC references (tools/bench-ref.sh)")
    ap.add_argument("--content", default=str(REPO / "gamedb" / "content" / "games"))
    ap.add_argument("--launcher", help="the menu launcher's Name the run started (for runs that recorded none)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    bad = 0
    for run in a.runs:
        try:
            import_run(run, a.game, a.content, a.ref if Path(a.ref).is_dir() else None, a.dry_run, a.launcher)
        except Refused as e:
            print("!! %s" % e, file=sys.stderr)
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
