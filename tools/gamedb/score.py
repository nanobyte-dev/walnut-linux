#!/usr/bin/env python3
"""score.py: Walnut Games' rating arithmetic (docs/GAMEDB.md "How a game is rated"). A library; run it
by itself to print the calibration table of the 13 benchmarked games.

Design
- Every aspect gets a score from 0 to 100 through a piecewise-linear curve: a list of (measured value,
  score) anchor points, linear between them, clamped outside. The anchors are the ones in
  docs/GAMEDB.md; changing a number there means changing it here, and test_gamedb.py checks that the
  calibration table still comes out the same.
- An aspect that can't be measured is None. It drops out of the weighted mean, and the others'
  weights are renormalised.
- The medal is the weighted mean's medal, capped one level above the weakest aspect (the weakest-link
  rule). Scrap floors ("won't start", "doesn't reach play", "crashes in most sessions", "too slow")
  win over the mean and come with a reason.
- Nothing here reads files: import-run.py computes the numbers, catalog.py combines the tests of a
  game through combine() and puts the result on the site. All of it is stdlib only.

Frame rate has one extra rule: a game capped by design (fps_cap in its front matter) that holds its
cap (median >= 0.95 x cap and 1% low >= 0.8 x cap) scores 100.

Stability: with crashes, the rate is crashes per hour of play (at least one hour counts, so one crash
in ten minutes isn't "six an hour"); without, the score starts at 90 and rises to 100 over the first
three hours of play (0 crashes in 3 h bounds the rate under one an hour, the rule of three). A game
that hasn't crashed isn't marked down for a short test: 90 is Platinum level, so it never caps the
medal; only the last 10 points need the hours. Under one hour of play the score is marked
provisional.
"""
import statistics
import sys

MEDALS = ["Scrap", "Rust", "Tin", "Bronze", "Silver", "Gold", "Platinum"]
THRESHOLDS = {"Platinum": 90, "Gold": 75, "Silver": 55, "Bronze": 35, "Tin": 25, "Rust": 15, "Scrap": 0}
WEIGHTS = {"frame": 25, "smooth": 20, "sound": 15, "picture": 15, "stability": 15, "display": 5, "load": 5}
ASPECT_NAMES = {"frame": "Frame rate", "smooth": "Smoothness", "sound": "Sound", "picture": "Picture",
                "stability": "Stability", "display": "Display", "load": "Load"}

# (measured value, score) anchors; the value axis rises (a lower score at the larger value, or the
# other way round for frame rate)
MEDIAN_FPS = [(20, 0), (55, 100)]
LOW1_FPS = [(10, 0), (45, 100)]
STUTTER_S_PER_MIN = [(0.1, 100), (0.5, 75), (2, 55), (5, 0)]
LONGEST_FRAME_MS = [(250, 100), (1000, 75), (3000, 55), (5000, 0)]
# sound: half for playing the right sounds (the player's form), half for how clean they are
SOUND_PRESENT = 50
SOUND_CLEAN_PER_MIN = [(0, 50), (10, 40), (30, 32), (60, 22), (120, 10), (240, 0)]
CRASHES_PER_HOUR = [(0, 100), (1 / 3, 75), (1, 55), (4, 35), (12, 0)]
LOAD_SECONDS = [(15, 100), (30, 75), (60, 55), (120, 0)]

# the Scrap floors (docs/GAMEDB.md "The medals")
TOO_SLOW_FPS = 15
TOO_SLOW_STUTTER = 10      # seconds of stutter a minute
CRASH_FREE_FULL_HOURS = 3
CRASH_FREE_START = 90       # crash-free, any play time: Platinum level, never caps the medal
PROVISIONAL_HOURS = 1


def interp(x, points):
    """the score of x on a piecewise-linear curve of (value, score) points, clamped to the ends"""
    if x <= points[0][0]:
        return float(points[0][1])
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return float(points[-1][1])


def frame_score(median, low1, fps_cap=None):
    """mean of the median's score and the 1% low's; a game holding its cap scores 100. None unmeasured"""
    if median is None or low1 is None:
        return None
    if fps_cap and median >= 0.95 * fps_cap and low1 >= 0.8 * fps_cap:
        return 100.0
    return (interp(median, MEDIAN_FPS) + interp(low1, LOW1_FPS)) / 2


def smooth_score(stutter_s_per_min, longest_ms):
    """the worse of the stutter time's score and the longest frame's"""
    if stutter_s_per_min is None or longest_ms is None:
        return None
    return min(interp(stutter_s_per_min, STUTTER_S_PER_MIN), interp(longest_ms, LONGEST_FRAME_MS))


def sound_score(faults_per_min, wedged=False, effects=None, music=None):
    """half for the right sounds playing, half for how clean they are.
    effects: the player's answer about the sound effects ("yes", "partly", "no"), music ("yes", "no",
    "none": the game has none); None without a form (a benchmark run: the sound device ran, counted as
    present). Effects and music both heard: 50; one missing (or effects only partly): 25; nothing heard
    or a wedged sound device: the whole score is 0. Clean: 50 at no faults (ALSA underruns, OpenAL
    xruns and waits) a minute, falling gently to 0 at 240 a minute; 50 when they weren't counted."""
    if wedged:
        return 0.0
    if faults_per_min is None and effects is None and music is None:
        return None
    fx_ok = effects in (None, "yes")
    music_ok = music in (None, "yes", "none")
    if effects == "no" and music in ("no", "none"):
        return 0.0
    present = SOUND_PRESENT if fx_ok and music_ok else SOUND_PRESENT / 2
    clean = 50.0 if faults_per_min is None else interp(faults_per_min, SOUND_CLEAN_PER_MIN)
    return present + clean


def picture_score(points, differing):
    """100 when no capture point differs from the PC, 0 when all do"""
    if not points or differing is None:
        return None
    return 100.0 * (1 - min(differing, points) / points)


def stability_score(crashes, hours):
    """(score, provisional): see the header. None without any play"""
    if not hours:
        return None, True
    provisional = hours < PROVISIONAL_HOURS
    if crashes:
        return interp(crashes / max(hours, 1.0), CRASHES_PER_HOUR), provisional
    return CRASH_FREE_START + (100 - CRASH_FREE_START) * min(hours, CRASH_FREE_FULL_HOURS) / CRASH_FREE_FULL_HOURS, provisional


def load_score(seconds):
    return None if seconds is None else interp(seconds, LOAD_SECONDS)


def test_scores(n, fps_cap=None):
    """the aspect scores {aspect: score or None} of one test's numbers n (result.toml's [numbers]).
    Stability isn't per test: combine() scores it over all the tests."""
    return {
        "frame": frame_score(n.get("median_fps"), n.get("low1_fps"), fps_cap),
        "smooth": smooth_score(n.get("stutter_s_per_min"), n.get("longest_ms")),
        "sound": sound_score(n.get("sound_faults_per_min"), bool(n.get("snd_stall")), n.get("sound_effects"),
                             n.get("music")),
        "picture": picture_score(n.get("shot_points"), n.get("shot_differing")),
        "display": n.get("display_score"),
        "load": load_score(n.get("load_s")),
    }


def weighted_mean(scores):
    """the weights-renormalised mean of the measured aspects, or None when none is"""
    got = {k: v for k, v in scores.items() if v is not None and k in WEIGHTS}
    if not got:
        return None
    return sum(WEIGHTS[k] * v for k, v in got.items()) / sum(WEIGHTS[k] for k in got)


def medal_of(score):
    for m in ("Platinum", "Gold", "Silver", "Bronze", "Tin", "Rust"):
        if score >= THRESHOLDS[m]:
            return m
    return "Scrap"


def level(score):
    """an aspect's level on the Platinum..Bronze scale as an index into MEDALS; below Bronze is Tin's"""
    return max(MEDALS.index(medal_of(score)), MEDALS.index("Tin"))


def rate(scores, floor_reason=None):
    """{score, medal, weakest, capped, reason} from the aspect scores. floor_reason: a Scrap floor"""
    mean = weighted_mean(scores)
    if mean is None:
        return {"score": None, "medal": "Untested", "weakest": None, "capped": False, "reason": None}
    got = {k: v for k, v in scores.items() if v is not None and k in WEIGHTS}
    weakest = min(got, key=lambda k: got[k])
    if floor_reason:
        return {"score": mean, "medal": "Scrap", "weakest": weakest, "capped": False, "reason": floor_reason}
    medal = medal_of(mean)
    cap = MEDALS[min(level(got[weakest]) + 1, len(MEDALS) - 1)]
    capped = MEDALS.index(medal) > MEDALS.index(cap)
    return {"score": mean, "medal": cap if capped else medal, "weakest": weakest, "capped": capped,
            "reason": ASPECT_NAMES[weakest].lower() if capped else None}


def scrap_floor(median_fps=None, stutter=None, started=True, reached_play=True, crashed_sessions=0, sessions=0):
    """the reason a game is Scrap whatever its mean, or None"""
    if not started:
        return "won't start"
    if not reached_play:
        return "doesn't reach play"
    if sessions and crashed_sessions * 2 > sessions:
        return "crashes"
    if (median_fps is not None and median_fps < TOO_SLOW_FPS) or (stutter is not None and stutter > TOO_SLOW_STUTTER):
        return "too slow"
    return None


def med(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def combine(tests, fps_cap=None):
    """one game's rating from its tests (a list of dicts as import-run.py writes them: kind, numbers,
    play_seconds, crashed, started, reached_play).

    Played sessions grade the game; until it has one, benchmark ("demo") runs stand in. Capture runs
    add their picture only. Scores are medians over those tests; stability counts every test's play
    time and crashes. Returns {aspects, numbers, hours, crashes, sessions, provisional, graded_by,
    + rate()'s keys}; medal "Untested" without any test."""
    sessions = [t for t in tests if t.get("kind") == "session"]
    demos = [t for t in tests if t.get("kind") == "demo"]
    graded = sessions or demos
    played = [t for t in tests if t.get("play_seconds")]
    hours = sum(t["play_seconds"] for t in played) / 3600
    crashes = sum(1 for t in played if t.get("crashed"))
    per = [test_scores(t.get("numbers", {}), fps_cap) for t in graded]
    scores = {k: med(p[k] for p in per) for k in ("frame", "smooth", "sound", "display", "load")}
    scores["picture"] = med(test_scores(t.get("numbers", {}))["picture"] for t in tests)
    scores["stability"], provisional = stability_score(crashes, hours)
    nums = {}
    for key in ("median_fps", "low1_fps", "stutter_s_per_min", "longest_ms", "sound_faults_per_min", "load_s"):
        nums[key] = med(t.get("numbers", {}).get(key) for t in graded)
    if not graded and scores["picture"] is None:
        return {"aspects": scores, "numbers": nums, "hours": hours, "crashes": crashes, "sessions": len(sessions),
                "provisional": True, "graded_by": None, **rate({})}
    if not graded:   # captures only: the picture alone doesn't make a medal
        scores = {k: (v if k == "picture" else None) for k, v in scores.items()}
        return {"aspects": scores, "numbers": nums, "hours": hours, "crashes": crashes, "sessions": 0,
                "provisional": True, "graded_by": "capture", **rate({})}
    floor = scrap_floor(
        nums["median_fps"], nums["stutter_s_per_min"],
        started=all(t.get("started", True) for t in graded),
        reached_play=any(t.get("reached_play", True) for t in graded),
        crashed_sessions=sum(1 for t in played if t.get("crashed")), sessions=len(played))
    return {"aspects": scores, "numbers": nums, "hours": hours, "crashes": crashes, "sessions": len(sessions),
            "provisional": provisional, "graded_by": "session" if sessions else "demo", **rate(scores, floor)}


# The calibration: the 13 games of docs/games/README.md with their known numbers, as docs/GAMEDB.md
# describes (stutter ~ the stall seconds over the 2-minute window / 2, load unmeasured, stability clean,
# picture and display right). (name, median fps, 1% low, stall seconds in 120 s, longest ms, sound
# faults a minute). Sound faults are readings of the table's sound column where the log has no number.
CALIBRATION = [
    ("X-Moto", 160.7, 106.7, 0.0, 43, 0),
    ("OpenArena, opengl1", 70.6, 34.6, 0.0, 56, 0),
    ("Yamagi Quake II", 68.7, 31.1, 0.1, 91, 0),
    ("OpenArena, opengl2", 60.4, 25.6, 0.2, 199, 0),
    ("QuakeSpasm", 91.0, 48.6, 2.5, 393, 0),
    ("Armagetron", 83.0, 24.1, 0.1, 127, 0),
    ("ClassiCube", 47.0, 30.7, 0.0, 65, 0),
    ("Extreme Tux Racer", 59.0, 27.3, 2.9, 483, 0),
    ("Luanti", 46.0, 19.1, 3.5, 724, 0),
    ("dsda-doom", 55.9, 40.3, 0.0, 34, 465),
    ("Sauerbraten", 39.1, 25.6, 0.05, 103, 40),
    ("Neverball", 66.5, 41.3, 2.0, 3900, 50),
    ("OpenTTD, 8bpp", 38.3, 13.4, 0.4, 210, 3500),
    ("SuperTuxKart lean", 21.0, 11.8, 1.8, 513, 3),
    ("OpenTTD, 32bpp", 11.0, 5.7, 52.0, 311, 3500),
]


def calibrate(row):
    """(scores, rating) of one CALIBRATION row"""
    _, median, low, stall_s, longest, faults = row
    scores = test_scores({"median_fps": median, "low1_fps": low, "stutter_s_per_min": stall_s / 2,
                          "longest_ms": longest, "sound_faults_per_min": faults})
    scores["picture"] = scores["display"] = 100.0
    scores["stability"] = 100.0
    return scores, rate(scores, scrap_floor(median, stall_s / 2))


def main():
    print("%-20s %6s %6s %6s %6s  %-8s  %s" % ("game", "frame", "smooth", "sound", "mean", "weakest", "medal"))
    for row in CALIBRATION:
        s, r = calibrate(row)
        note = " (capped)" if r["capped"] else (" (%s)" % r["reason"] if r["reason"] else "")
        print("%-20s %6.0f %6.0f %6.0f %6.0f  %-8s  %s%s" % (
            row[0], s["frame"], s["smooth"], s["sound"], r["score"], r["weakest"], r["medal"], note))


if __name__ == "__main__":
    sys.exit(main())
