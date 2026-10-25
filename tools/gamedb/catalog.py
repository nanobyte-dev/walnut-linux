#!/usr/bin/env python3
"""catalog.py [--out FILE] [--deb-index URL]: build catalog.json from gamedb/content/games, the one
file the website's templates (Hugo reads it as data) and the desktop app (later, packaged) use.

Design
- Per game: the front matter, the rating computed by score.combine() from its tests' result.toml
  (never stored: change the scoring and every medal follows), a test summary per test, the badges
  and the "patched by Walnut" facts. The result:
    {"schema": 1, "generated": "<UTC>", "games": [{"id", "title", ..., "rating": {...}, "tests": [...],
      "patched": bool, "patches": [{source, subject, file, url}], "debs": [{"package", "url"}], "badges": [...]}]}
  Scores are rounded to whole numbers here, the only place that rounds.
- Patched by Walnut: a game whose patched_by names a directory of ours (<dir>/package.conf with
  <dir>/patches/series). The patches are listed by the Subject: line of each patch file (linked to the file on the public repository's main branch), in series
  order. With --deb-index URL (the apt repository root, e.g. https://walnut.nanobyte.dev/apt) the
  stable suite's dists/stable/main/binary-ppc64/Packages.xz is read and each of the game's packages
  found there gets a direct .deb link (Filename: -> URL/<Filename>). Without the option, or without a
  network, the links are simply left out; nothing else changes.
- SOURCE_DATE_EPOCH, when set, fixes "generated" so a build can be repeated byte for byte.
- stdlib only; the rating arithmetic is score.py.
"""
import argparse
import datetime
import json
import lzma
import os
import re
import sys
import tomllib
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import check   # noqa: E402
import score   # noqa: E402

SCHEMA = 1
LOW_MEMORY_KB = 10 * 1024


def read_game(gdir):
    fm = check.front_matter(gdir / "index.md")
    tests = []
    tdir = gdir / "tests"
    for t in sorted(tdir.iterdir()) if tdir.is_dir() else []:
        r = tomllib.loads((t / "result.toml").read_text())
        r["id"] = t.name
        tests.append(r)
    return fm, tests


# where the patch files are read on the web: the public repository's main branch
PATCH_URL = "https://github.com/nanobyte-dev/walnut-linux/blob/main/%s/patches/%s"


def patch_subjects(srcdir, repo):
    """[(file name, Subject: line)] of <srcdir>/patches/series' patches, in order (wrapped lines joined,
    "[PATCH n/m]" cut)"""
    pdir = repo / srcdir / "patches"
    series = pdir / "series"
    if not series.exists():
        return []
    out = []
    for name in series.read_text().split():
        if name.startswith("#") or not (pdir / name).exists():
            continue
        subj, lines = None, (pdir / name).read_text(errors="replace").split("\n")
        for i, line in enumerate(lines):
            if line.startswith("Subject:"):
                subj = line[8:].strip()
                for nxt in lines[i + 1:]:
                    if nxt[:1] in (" ", "\t"):
                        subj += " " + nxt.strip()
                    else:
                        break
                break
        subj = re.sub(r"^\[PATCH[^\]]*\]\s*", "", subj or name)
        out.append((name, subj))
    return out


def deb_index(url):
    """{package: Filename} of the stable suite's ppc64 Packages.xz under the repository root url; {} offline"""
    index = url.rstrip("/") + "/dists/stable/main/binary-ppc64/Packages.xz"
    try:
        with urllib.request.urlopen(index, timeout=20) as f:
            text = lzma.decompress(f.read()).decode("utf-8", "replace")
    except Exception as e:   # no network, 404, bad xz: the links are optional
        print("catalog.py: no deb links (%s: %s)" % (index, e), file=sys.stderr)
        return {}
    files = {}
    for stanza in text.split("\n\n"):
        f = dict(line.split(": ", 1) for line in stanza.split("\n") if ": " in line and not line.startswith(" "))
        if "Package" in f and "Filename" in f:
            files[f["Package"]] = f["Filename"]   # one version per package in the stable suite
    return files


def rnd(v):
    return None if v is None else int(round(v))


def details(r, fps_cap):
    """the numbers behind each aspect as text, for the page"""
    n = r["numbers"]
    d = {}
    if n.get("median_fps") is not None:
        d["frame"] = "median %.0f fps, 1%% low %.0f%s" % (n["median_fps"], n["low1_fps"],
                                                           ", capped by design at %g" % fps_cap if fps_cap else "")
    if n.get("stutter_s_per_min") is not None:
        d["smooth"] = "%.1f s of stutter a minute, longest frame %.0f ms" % (n["stutter_s_per_min"], n["longest_ms"])
    if n.get("sound_dropouts_per_min") is not None:     # measured by ps3-xrunlog: what could be heard
        d["sound"] = "%.1f audible sound dropouts a minute" % n["sound_dropouts_per_min"]
    elif n.get("sound_faults_per_min") is not None:
        d["sound"] = "%.1f sound faults a minute" % n["sound_faults_per_min"]
    d["stability"] = "%d crash%s in %.1f h of play%s" % (r["crashes"], "" if r["crashes"] == 1 else "es", r["hours"],
                                                         " (provisional: under 1 h)" if r["provisional"] else "")
    if n.get("load_s") is not None:
        d["load"] = "%.0f s from launch to the game's first screen" % n["load_s"]
    return d


PRE_RELEASE = "pre-0.1"   # runs from before the harness recorded the release (all before 0.1.0)
GRADED = ("session", "demo")


def tested_version(t, packages):
    """(walnut release, game package, its version) a test ran on: the release from the run's
    os-release, the game's version from its first package the run recorded"""
    v = t.get("versions", {})
    pkg = next((p for p in packages if p in v), None)
    # without our build number (+ps3.N): it changes with every rebuild, not with the game
    ver = re.sub(r"\+ps3\.\d+$", "", v[pkg]) if pkg else None
    return v.get("walnut") or PRE_RELEASE, pkg, ver


def version_groups(tests, packages):
    """the tests grouped by the version they ran on, newest first (by their latest date)"""
    groups = {}
    for t in tests:
        groups.setdefault(tested_version(t, packages), []).append(t)
    return sorted(groups.items(), key=lambda kv: max((t["date"], t["id"]) for t in kv[1]), reverse=True)


def method(t):
    """how a test was measured, for which tests can be merged: sound dropouts measured by the logger,
    or only counted (before it existed)"""
    return "dropouts" if "sound_dropouts" in t.get("numbers", {}) else "counts"


def rated_tests(ts):
    """what a rating is made from (ts: one version's tests): the graded tests (sessions, benchmark runs)
    measured the way the newest one was, merged (medians); sessions measured differently aren't (the
    user, 2026-10-25); capture runs add their picture"""
    graded = sorted((t for t in ts if t.get("kind") in GRADED), key=lambda t: (t["date"], t["id"]))
    same = [t for t in graded if method(t) == method(graded[-1])] if graded else []
    return same + [t for t in ts if t.get("kind") not in GRADED]


def history(groups, fps_cap):
    """one row per version: its own rating (from that version's sessions measured like its newest), for the website's
    comparison between versions"""
    out = []
    for (walnut, pkg, ver), ts in groups:
        r = score.combine(rated_tests(ts), fps_cap)
        out.append({"walnut": walnut, "package": pkg, "version": ver,
                    "first": min(t["date"] for t in ts), "last": max(t["date"] for t in ts), "tests": len(ts),
                    "hours": round(r["hours"], 2), "crashes": r["crashes"], "medal": r["medal"],
                    "score": rnd(r["score"]), "graded_by": r["graded_by"],
                    "aspects": {k: rnd(r["aspects"].get(k)) for k in score.ASPECT_NAMES}})
    return out


def launcher_groups(tests):
    """the tests by launcher (the menu entry's Name the test started; result.toml's launcher), in
    first-seen order. A test that names none belongs to the game's only named launcher, or with
    several to an unnamed one (None: the default). A capture run without a launcher shows the
    game's picture: it counts for every launcher."""
    named = list(dict.fromkeys(t["launcher"] for t in tests if t.get("launcher")))
    groups = {}
    for t in tests:
        if t.get("launcher") or t.get("kind") in GRADED:
            name = t.get("launcher") or (named[0] if len(named) == 1 else None)
            groups.setdefault(name, []).append(t)
    shared = [t for t in tests if not t.get("launcher") and t.get("kind") not in GRADED]
    if not groups:
        return [(named[0] if len(named) == 1 else None, list(tests))]
    return [(name, ts + shared) for name, ts in groups.items()]


def rate_launcher(tests, fm):
    """one launcher's rating, from the newest version's sessions measured like its newest one (capture
    runs of any version add their picture), and its history (every version rated the same way)"""
    fps_cap = fm.get("fps_cap")
    groups = version_groups(tests, fm["packages"])
    current = next((k for k, ts in groups if any(t.get("kind") in GRADED for t in ts)), None)
    cur = rated_tests([t for k, ts in groups for t in ts if k == current or t.get("kind") not in GRADED])
    cur.sort(key=lambda t: (t["date"], t["id"]))
    r = score.combine(cur, fps_cap)
    det = details(r, fps_cap)
    pic = [(t["numbers"]["shot_points"], t["numbers"]["shot_differing"]) for t in cur
           if "shot_points" in t.get("numbers", {})]
    if pic:
        det["picture"] = "%d of %d capture points differ from the PC" % (sum(p[1] for p in pic), sum(p[0] for p in pic))
    aspects = {}
    for k, name in score.ASPECT_NAMES.items():
        aspects[k] = {"name": name, "weight": score.WEIGHTS[k], "score": rnd(r["aspects"].get(k)),
                      "detail": det.get(k)}
    graded = [t for t in cur if t.get("kind") in GRADED]
    avail = [t["numbers"]["min_avail_kb"] for t in graded if "min_avail_kb" in t.get("numbers", {})]
    rating = {"medal": r["medal"], "score": rnd(r["score"]), "capped": r["capped"], "reason": r["reason"],
              "weakest": r["weakest"], "aspects": aspects,
              "unmeasured": [a["name"] for a in aspects.values() if a["score"] is None],
              "hours": round(r["hours"], 2), "crashes": r["crashes"], "provisional": r["provisional"],
              "graded_by": r["graded_by"],
              "silent": aspects["sound"]["score"] == 0 and r["aspects"].get("sound") is not None,
              "low_memory": bool(avail) and min(avail) < LOW_MEMORY_KB,
              "tested": None if current is None else {
                  "walnut": current[0], "package": current[1], "version": current[2],
                  "date": max(t["date"] for k, ts in groups if k == current for t in ts)}}
    return rating, history(groups, fps_cap)


def rank(rating):
    """a rating's place, best first: a medal from tests beats none; a rating with its frame rate
    measured beats one without (a software renderer gives the frame timer nothing, and its score
    from sound, picture and stability alone would outrank the launchers that were measured); then
    the medal, then the score"""
    graded = rating["graded_by"] not in (None, "capture")
    framed = rating["aspects"]["frame"]["score"] is not None
    return (graded, framed, score.MEDALS.index(rating["medal"]) if rating["medal"] in score.MEDALS else -1,
            rating["score"] or 0)


def build_game(gdir, repo, debs):
    fm, all_tests = read_game(gdir)
    # one rating per launcher (a game with two renderers has two menu entries); the game's medal is
    # its best launcher's, named with it
    launchers = []
    for name, ts in launcher_groups(all_tests):
        rating, hist = rate_launcher(ts, fm)
        launchers.append({"name": name, "rating": rating, "history": hist})
    launchers.sort(key=lambda l: rank(l["rating"]), reverse=True)
    best = launchers[0]
    patches, patched_dirs = [], []
    for src in fm.get("patched_by", []):
        subjects = patch_subjects(src, repo)
        if subjects:
            patched_dirs.append(src)
            patches += [{"source": src, "subject": s, "file": f, "url": PATCH_URL % (src, f)} for f, s in subjects]
    links = [{"package": p, "url": debs[p]}
             for p in dict.fromkeys(fm["packages"] + patched_dirs) if p in debs]
    badges = []
    if fm["needs_data"]:
        badges.append("needs-data")
    if fm["controller"] != "unknown":
        badges.append("controller-" + fm["controller"])
    if best["rating"]["silent"]:
        badges.append("no-sound")
    if best["rating"]["low_memory"]:
        badges.append("low-memory")
    out_tests = []
    for t in sorted(all_tests, key=lambda t: (t["date"], t["id"]), reverse=True):
        walnut, pkg, ver = tested_version(t, fm["packages"])
        out_tests.append({
            "walnut": walnut, "package": pkg, "version": ver, "launcher": t.get("launcher"),
            "id": t["id"], "kind": t["kind"], "date": t["date"], "variant": t.get("variant"),
            "play_seconds": t.get("play_seconds", 0), "end": t.get("end"), "crashed": t.get("crashed", False),
            "verdict": t.get("picture", {}).get("verdict"), "note": t.get("picture", {}).get("note"),
            "numbers": t.get("numbers", {}), "versions": t.get("versions", {}),
            "ps3": ["tests/%s/%s" % (t["id"], f) for f in t.get("shots", {}).get("ps3", [])]})
    g = {k: fm[k] for k in ("id", "title", "summary", "categories", "tags", "license", "homepage", "packages",
                            "desktop", "needs_data", "controller") if k in fm}
    g.update({k: fm.get(k, v) for k, v in (("recipes", []), ("fps_cap", None), ("data_guide", None),
                                           ("medal_note", None), ("known_issues", []))})
    g.update(thumb="games/%s/thumb.png" % fm["id"], patched=bool(patches), patches=patches, debs=links, badges=badges,
             rating=dict(best["rating"], launcher=best["name"]), launchers=launchers, tests=out_tests)
    return g


def build(content, repo=REPO, deb_url=None):
    debs = deb_index(deb_url) if deb_url else {}
    debs = {p: deb_url.rstrip("/") + "/" + f for p, f in debs.items()}
    games = [build_game(d, repo, debs) for d in sorted(Path(content).iterdir()) if d.is_dir()]
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    when = datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc) if epoch \
        else datetime.datetime.now(datetime.timezone.utc)
    return {"schema": SCHEMA, "generated": when.strftime("%Y-%m-%dT%H:%M:%SZ"), "games": games}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--content", default=str(REPO / "gamedb" / "content" / "games"))
    ap.add_argument("--out", default=str(REPO / "gamedb" / "data" / "catalog.json"))
    ap.add_argument("--deb-index", metavar="URL", help="apt repository root, e.g. https://walnut.nanobyte.dev/apt")
    a = ap.parse_args()
    cat = build(a.content, REPO, a.deb_index)
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(json.dumps(cat, indent=1, ensure_ascii=False) + "\n")
    for g in cat["games"]:
        r = g["rating"]
        print("%-14s %-9s %s" % (g["id"], r["medal"], r["score"] if r["score"] is not None else "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
