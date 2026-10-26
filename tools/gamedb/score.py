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
- Nothing here reads files (but calibration(), the calibration's numbers from gamedb): import-run.py computes the numbers, catalog.py combines the tests of a
  game through combine() and puts the result on the site. All of it is stdlib only.

Frame rate has one extra rule: a game capped by design (fps_cap in its front matter) that holds its
cap (median >= 0.95 x cap and 1% low >= 0.8 x cap) scores 100.

Stability: with crashes, the rate is crashes per hour of play (at least one hour counts, so one crash
in ten minutes isn't "six an hour"); without, the score starts at 90 and rises to 100 over the first
three hours of play (0 crashes in 3 h bounds the rate under one an hour, the rule of three). A game
that hasn't crashed isn't marked down for a short test: 90 is Platinum level, so it never caps the
medal; only the last 10 points need the hours. Under one hour of play the score is marked
provisional.

Rules versions (docs/GAMEDB.md "Rules versions"). Every test records the rules it was imported under
(result.toml `rules`, absent = 1). Rules 1 (the 2026-10-24 rules) are the functions above as they were:
combine(tests) and test_scores(numbers). Rules 2 (the 2026-10-26 rules) reweigh the aspects, add
Controls and Features (checklists from the form, the input log and the review), score the picture by
fixed deductions per defect category (the reviewer's answers and the form's glitches answer), take
deductions off Display (stretched, a desktop pointer, small text: the reviewer's), count a hang on
quit like a crash in Stability, add the level load to Load, and replace the weakest link by one rule: a
broken core subsystem caps the medal (broken_v2): combine(tests, rules=2) and test_scores_v2(test).
Formula 2.2 (2026-10-26): "more than half the sessions crash" needs two sessions (a single late crash in a
game's only session is Rust). Formula 2.1 (2026-10-26) redefines the medals of data version 2 (MEDALS_V2): Platinum to Bronze from the mean,
no Tin; Rust for a playable game with a core subsystem broken (or a mean under Bronze's); Scrap only for a
game that can't be played (scrap_floor_v2: won't start, crashes consistently, under 5 fps). A rating never mixes the two: catalog.py picks a launcher's newest data version
and rates it from those tests alone.
"""
import functools
import os
import statistics
import subprocess
from pathlib import Path
import sys

MEDALS = ["Scrap", "Rust", "Tin", "Bronze", "Silver", "Gold", "Platinum"]
THRESHOLDS = {"Platinum": 90, "Gold": 75, "Silver": 55, "Bronze": 35, "Tin": 25, "Rust": 15, "Scrap": 0}
WEIGHTS = {"frame": 25, "smooth": 20, "sound": 15, "picture": 15, "stability": 15, "display": 5, "load": 5}
ASPECT_NAMES = {"frame": "Frame rate", "smooth": "Smoothness", "sound": "Sound", "picture": "Picture",
                "stability": "Stability", "display": "Display", "load": "Load"}

# Rules versions, "<data>.<formula>" (docs/GAMEDB.md "Rules versions"). The data version is what a test
# measured (result.toml `rules = "2.0"`, absent: 1.0); the formula revision is the current weights and
# thresholds for that data version, applied at catalog time to every test of it (a test stores its raw
# numbers and answers, never only scores). A new criterion that needs new data is a new data version;
# a changed weight or threshold, or a dropped aspect, is a new formula revision (bump FORMULA[data]).
FORMULA = {1: 0, 2: 2}
RULES = {1: {"date": "2026-10-24", "anchor": "rules-1"},
         2: {"date": "2026-10-26", "anchor": "rules-2"}}
CURRENT_DATA = 2


@functools.lru_cache(maxsize=None)
def package_dirs(repo):
    """{name: directory} of every source package of ours in the tree repo (tools/pkg-dir.sh --all:
    packages/<group>/[<area>/]NAME, a game's or engine's packages/ and licensed/ in gamedb/). Two of one
    name are an error there, and a RuntimeError here."""
    p = subprocess.run([str(Path(__file__).resolve().parent.parent / "pkg-dir.sh"), "--all"], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, env=dict(os.environ, PKG_DIR_ROOT=str(repo)))
    if p.returncode:
        raise RuntimeError("tools/pkg-dir.sh --all: %s" % p.stderr.strip())
    return {d.rsplit("/", 1)[-1]: d for d in p.stdout.split()}


def package_dir(repo, name):
    """Our source package NAME's directory relative to the repository (tools/pkg-dir.sh), or None.
    Shared by check.py and catalog.py."""
    return package_dirs(str(Path(repo).resolve())).get(name)


def data_version(t):
    """a test's data version: the integer part of its rules (1 without the field)"""
    try:
        return int(str(t.get("rules", "1")).split(".")[0])
    except ValueError:
        return 1


def rules_label(data):
    """"rules 2.0 (2026-10-26)": the data version with its current formula revision"""
    return "rules %d.%d (%s)" % (data, FORMULA[data], RULES[data]["date"])
WEIGHTS_V2 = {"frame": 22, "smooth": 18, "sound": 12, "picture": 12, "stability": 12, "controls": 10, "display": 6,
              "features": 5, "load": 3}
ALL_ASPECT_NAMES = dict(ASPECT_NAMES, controls="Controls", features="Features")
# rules 2: no weakest link; a broken core subsystem (broken_v2) makes the game Rust whatever its mean
# (rules 2.1; the page shows the mean next to it: "Rust · 82, picture broken"); Controls, Features, Display and
# Load only weigh in the mean
# rules 2.1's medals (the user, 2026-10-26): Platinum awesome, Gold playable but below ideal, Silver playable
# with some rough edges, Bronze at the limit of playable; Rust playable but a core part broken (no or bad
# sound, a very low frame rate, broken textures, big stutters, occasional crashes) or a mean under 35; Scrap
# completely unplayable (scrap_floor_v2). No Tin: rules 1's scale (MEDALS) keeps it
MEDALS_V2 = ["Scrap", "Rust", "Bronze", "Silver", "Gold", "Platinum"]
UNPLAYABLE_FPS = 5          # rules 2.1: a median under this is Scrap ("too slow to play")
EARLY_CRASH_S = 300         # rules 2.1: a crash within this much play is an early one (the first few minutes)
ASPECTS = {1: list(WEIGHTS), 2: list(WEIGHTS_V2)}
WEIGHTS_OF = {1: WEIGHTS, 2: WEIGHTS_V2}

# (measured value, score) anchors; the value axis rises (a lower score at the larger value, or the
# other way round for frame rate)
MEDIAN_FPS = [(20, 0), (55, 100)]
LOW1_FPS = [(10, 0), (45, 100)]
STUTTER_S_PER_MIN = [(0.1, 100), (0.5, 75), (2, 55), (5, 0)]
LONGEST_FRAME_MS = [(250, 100), (1000, 75), (3000, 55), (5000, 0)]
# sound: half for playing the right sounds (the player's form), half for how clean they are
SOUND_PRESENT = 50
SOUND_CLEAN_PER_MIN = [(0, 50), (10, 40), (30, 32), (60, 22), (120, 10), (240, 0)]
# the same half from audible dropouts a minute (ps3-xrunlog: gaps of 20 ms or more with sound on a side,
# bench/harness/xruns.py), where a run measured them: every one is a click or chop someone hears, so it
# falls much faster than the raw counts' curve, most of whose faults were never heard
SOUND_DROPOUTS_PER_MIN = [(0, 50), (1, 45), (4, 35), (10, 22), (20, 10), (40, 0)]
CRASHES_PER_HOUR = [(0, 100), (1 / 3, 75), (1, 55), (4, 35), (12, 0)]
LOAD_SECONDS = [(15, 100), (30, 75), (60, 55), (120, 0)]

# what a rating needs measured: without these a game gets no medal, only its numbers (a software
# renderer gives the frame timer nothing)
REQUIRED = ("frame", "smooth")

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


def sound_score(faults_per_min, wedged=False, effects=None, music=None, dropouts_per_min=None):
    """half for the right sounds playing, half for how clean they are.
    effects: the player's answer about the sound effects ("yes", "partly", "no"), music ("yes", "no",
    "none": the game has none); None without a form (a benchmark run: the sound device ran, counted as
    present). Effects and music both heard: 50; one missing (or effects only partly): 25; nothing heard
    or a wedged sound device: the whole score is 0. Clean: from the audible dropouts a minute when the
    run measured them (dropouts_per_min: 50 at none, 45 at one, 35 at four, 22 at ten, 10 at twenty, 0
    at forty), else from the fault counts (ALSA underruns, OpenAL xruns and waits) a minute, falling
    gently to 0 at 240 a minute; 50 when neither was counted."""
    if wedged:
        return 0.0
    if faults_per_min is None and dropouts_per_min is None and effects is None and music is None:
        return None
    fx_ok = effects in (None, "yes")
    music_ok = music in (None, "yes", "none")
    if effects == "no" and music in ("no", "none"):
        return 0.0
    present = SOUND_PRESENT if fx_ok and music_ok else SOUND_PRESENT / 2
    if dropouts_per_min is not None:
        clean = interp(dropouts_per_min, SOUND_DROPOUTS_PER_MIN)
    else:
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


COVERS = 0.95   # a window showing this much of the screen (and cropped by none) counts as filling it
CROP_MAX = 0.02  # the share of a window outside the screen above which the picture is cropped


def display_sample(x, y, w, h, fullscreen, sw, sh):
    """(score, detail) of one geometry sample (bench/harness/geom.py): the game's window at x, y, w x h on a
    screen of sw x sh. Fullscreen or a window covering the screen is 100 (bars inside the picture can't be
    told from geometry, and cost nothing); cropped or partly off-screen 0; a smaller window 30 from half the
    screen's area up, 10 below."""
    vis_w = max(0, min(x + w, sw) - max(x, 0))
    vis_h = max(0, min(y + h, sh) - max(y, 0))
    cropped = 1 - vis_w * vis_h / (w * h) > CROP_MAX
    if cropped:
        return 0.0, "a %dx%d window partly off the %dx%d screen" % (w, h, sw, sh)
    if fullscreen and vis_w >= sw and vis_h >= sh or vis_w * vis_h >= COVERS * sw * sh:
        return 100.0, "fullscreen %dx%d" % (w, h)
    return (30.0 if w * h >= 0.5 * sw * sh else 10.0), "a %dx%d window on %dx%d" % (w, h, sw, sh)


def display_score(rows):
    """(score, detail) of a session's geometry samples, rows = [(x, y, w, h, fullscreen, sw, sh) or None for a
    sample that found no window]: the median sample's score (the lower middle when even), so the score is one
    a sample really had and the detail describes it. (None, None) without any sample with a window."""
    got = sorted((display_sample(*r) for r in rows if r), key=lambda sd: sd[0])
    return got[(len(got) - 1) // 2] if got else (None, None)


def test_scores(n, fps_cap=None):
    """the aspect scores {aspect: score or None} of one test's numbers n (result.toml's [numbers]).
    Stability isn't per test: combine() scores it over all the tests."""
    return {
        "frame": frame_score(n.get("median_fps"), n.get("low1_fps"), fps_cap),
        "smooth": smooth_score(n.get("stutter_s_per_min"), n.get("longest_ms")),
        "sound": sound_score(n.get("sound_faults_per_min"), bool(n.get("snd_stall")), n.get("sound_effects"),
                             n.get("music"), n.get("sound_dropouts_per_min")),
        "picture": picture_score(n.get("shot_points"), n.get("shot_differing")),
        "display": n.get("display_score"),
        "load": load_score(n.get("load_s")),
    }


def weighted_mean(scores, weights=WEIGHTS):
    """the weights-renormalised mean of the measured aspects, or None when none is"""
    got = {k: v for k, v in scores.items() if v is not None and k in weights}
    if not got:
        return None
    return sum(weights[k] * v for k, v in got.items()) / sum(weights[k] for k in got)


def medal_of(score):
    for m in ("Platinum", "Gold", "Silver", "Bronze", "Tin", "Rust"):
        if score >= THRESHOLDS[m]:
            return m
    return "Scrap"


def medal_of_v2(score):
    """rules 2.1's medal of a weighted mean: Platinum to Bronze as rules 1, below Bronze Rust (no Tin)"""
    for m in ("Platinum", "Gold", "Silver", "Bronze"):
        if score >= THRESHOLDS[m]:
            return m
    return "Rust"


def level(score):
    """an aspect's level on the Platinum..Bronze scale as an index into MEDALS; below Bronze is Tin's"""
    return max(MEDALS.index(medal_of(score)), MEDALS.index("Tin"))


def rate(scores, floor_reason=None, weights=WEIGHTS, weakest_link=True, medal_fn=medal_of):
    """{score, medal, weakest, capped, reason} from the aspect scores. floor_reason: a Scrap floor.
    weights: the rules' (WEIGHTS for rules 1, WEIGHTS_V2). weakest_link: rules 1's cap, the medal at most one
    level above the weakest aspect (rules 2 has none: a broken subsystem makes it Rust, combine_v2).
    medal_fn: the mean's medal (medal_of_v2 for rules 2.1)"""
    mean = weighted_mean(scores, weights)
    if mean is None:
        return {"score": None, "medal": "Untested", "weakest": None, "capped": False, "reason": None}
    got = {k: v for k, v in scores.items() if v is not None and k in weights}
    weakest = min(got, key=lambda k: got[k])
    if floor_reason:
        return {"score": mean, "medal": "Scrap", "weakest": weakest, "capped": False, "reason": floor_reason}
    medal = medal_fn(mean)
    if not weakest_link:
        return {"score": mean, "medal": medal, "weakest": weakest, "capped": False, "reason": None}
    cap = MEDALS[min(level(got[weakest]) + 1, len(MEDALS) - 1)]
    capped = MEDALS.index(medal) > MEDALS.index(cap)
    return {"score": mean, "medal": cap if capped else medal, "weakest": weakest, "capped": capped,
            "reason": ALL_ASPECT_NAMES[weakest].lower() if capped else None}


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


def scrap_floor_v2(tests, median_fps=None):
    """rules 2.1: the reason a launcher's played sessions can't be played at all (Scrap), or None. Consistent
    crashes only: one at startup or before play in any session, crashes in more than half the sessions with
    play, or crashes within EARLY_CRASH_S of play in half of them or more; occasional crashes are broken_v2's
    (Rust). Too slow: a median under UNPLAYABLE_FPS"""
    if not all(t.get("started", True) for t in tests):
        return "won't start"
    if any(end_failure(t) == "crash" and not (t.get("reached_play", True) and t.get("play_seconds")) for t in tests):
        return "crashes before play"
    if not any(t.get("reached_play", True) for t in tests):
        return "doesn't reach play"
    played = [t for t in tests if t.get("play_seconds")]
    crashed = [t for t in played if end_failure(t) == "crash"]
    early = [t for t in crashed if t["play_seconds"] < EARLY_CRASH_S]
    if early and len(early) * 2 >= len(played):
        return "crashes within %d minutes of play in %d of %d sessions" % (EARLY_CRASH_S // 60, len(early), len(played))
    # more than half needs two sessions (rules 2.2): one late crash in a game's only session is Rust until
    # a second session shows whether it repeats
    if len(played) >= 2 and len(crashed) * 2 > len(played):
        return "crashes in %d of %d sessions" % (len(crashed), len(played))
    if median_fps is not None and median_fps < UNPLAYABLE_FPS:
        return "too slow (%.1f fps)" % median_fps
    return None


def med(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def combine(tests, fps_cap=None, rules=1, designed_for="pad"):
    """one game's rating from its tests (a list of dicts as import-run.py writes them: kind, numbers,
    play_seconds, crashed, started, reached_play).

    Played sessions grade the game; until it has one, benchmark ("demo") runs stand in. Capture runs
    add their picture only. Scores are medians over those tests; stability counts every test's play
    time and crashes. Returns {aspects, numbers, hours, crashes, sessions, provisional, graded_by,
    + rate()'s keys}; medal "Untested" without any test. rules: the tests' data version (all of them the
    same: catalog.py never mixes them); 2 is combine_v2's, which also takes designed_for."""
    if rules == 2:
        return combine_v2(tests, fps_cap, designed_for)
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
    for key in ("median_fps", "low1_fps", "stutter_s_per_min", "longest_ms", "sound_faults_per_min",
                "sound_dropouts_per_min", "load_s"):
        nums[key] = med(t.get("numbers", {}).get(key) for t in graded)
    nums["display_detail"] = next((t["numbers"]["display_detail"] for t in reversed(graded)
                                   if t.get("numbers", {}).get("display_detail")), None)   # the newest test's words
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
    rated = rate(scores, floor)
    missing = [ASPECT_NAMES[k].lower() for k in REQUIRED if scores.get(k) is None]
    if missing and not floor:
        # no medal from what's left: sound, picture and stability alone say nothing about how it plays
        rated = {"score": None, "medal": "Untested", "weakest": None, "capped": False,
                 "reason": " and ".join(missing) + " not measured"}
    return {"aspects": scores, "numbers": nums, "hours": hours, "crashes": crashes, "sessions": len(sessions),
            "provisional": provisional, "graded_by": "session" if sessions else "demo", **rated}


# ---- data version 2 (rules 2.x) ---------------------------------------------------------------------
# One player, one ~10 min session per game, the game on fresh defaults (session.sh sets the player's
# settings aside): what the player can't reasonably test is "not tested" and never costs anything (its
# weight goes to the rest). The rating is about how the game plays on the PS3 under Walnut, never about
# the game's design: a defect counts wherever it comes from, and is labelled "also on the PC" or
# "PS3/Walnut only"; a game designed for keyboard and mouse is never marked down for weak pad support.

# Picture: fixed deductions per defect category, each category counted once per launcher, from 100
DEFECTS = {"garbage": 60, "missing": 25, "colours": 25, "flicker": 15, "lighting": 10, "softer": 5}
DEFECT_NAMES = {"garbage": "garbage textures or geometry",
                "missing": "a missing element (HUD, models, shadows, an effect)",
                "colours": "wrong colours or blending", "flicker": "flicker, z-fighting or one-frame glitches",
                "lighting": "lighting or shading differs",
                "softer": "softer or less detailed, from Walnut's own defaults"}
DEFECT_ALIASES = {"colors": "colours", "colour": "colours", "color": "colours", "blending": "colours",
                  "garbled": "garbage", "zfight": "flicker", "shading": "lighting", "soft": "softer"}
ORIGINS = {"pc": "also on the PC", "ps3": "PS3/Walnut only"}
# the player's "Visual glitches" answer: a deduction unless a reviewer's defect already takes as much
GLITCHES = {"none": 0, "minor": 10, "broken": 25}
# Display: the geometry's scale, then these deductions (floor 0); all three judged by the reviewer
STRETCHED, POINTER, SMALL_TEXT = 30, 10, 15
SMALL_TEXT_PX = 18          # the smallest menu/HUD text height at 1080p under which text is too small
# Stability: a quit is clean when the desktop is back within this many seconds
QUIT_BACK_S = 10
# Controls and Features: checklists, each item yes / partly / no / na (not tested: never counts)
PAD_ITEMS = [("pad_play", "the gamepad works in play"), ("pad_setup", "the gamepad works without setup"),
             ("pad_menus", "the menus work with the gamepad"), ("pad_hotplug", "a gamepad turned on later is found"),
             ("pad_players", "each extra gamepad joins as the next player"),
             ("no_keyboard", "nothing needs the keyboard")]
KM_ITEMS = [("keyboard_mouse", "keyboard and mouse work")]
CONTROLS = PAD_ITEMS + KM_ITEMS
FEATURES = [("menus_modes", "every menu and mode opened works"), ("saves_load", "saves load"),
            ("online", "online play works")]
CHECK_NAMES = dict(CONTROLS + FEATURES)
ANSWER_SHARE = {"yes": 1.0, "partly": 0.5, "no": 0.0}
NO_PAD_CAP = 30             # a pad game whose pad doesn't work without setup: Controls at most this
KEYBOARD_STRAY = 2          # key presses while playing with the pad that are a stray touch, not a need
DESIGNED_FOR = ("pad", "keyboard-mouse")   # the front matter's designed_for (default pad)
# the form's answers (session-form.py) as checklist answers
FORM_PAD_MENUS = {"worked": "yes", "partly": "partly", "didnt": "no", "na": "na"}
FORM_PAD_PLAY = {"seamless": ("yes", "yes"), "setup": ("yes", "no"), "didnt": ("no", "no"), "na": ("na", "na")}
FORM_KM = {"worked": "yes", "some": "no", "na": "na"}


def defect_category(word):
    w = (word or "").strip().lower()
    w = DEFECT_ALIASES.get(w, w)
    return w if w in DEFECTS else None


def picture_v2(defects, glitches=None):
    """(score, [{category, name, points, origin, what}]) from 100: defects = [(category, origin, what)] of the
    launcher's reviews (each category deducted once; its descriptions joined), glitches = the player's worst
    answer ("none", "minor", "broken"), deducted unless a review's category takes at least as much.
    (None, []) when neither a review nor an answer exists (defects None)"""
    if defects is None and glitches is None:
        return None, []
    out = {}
    for cat, origin, what in defects or []:
        e = out.setdefault(cat, {"category": cat, "name": DEFECT_NAMES[cat], "points": DEFECTS[cat],
                                 "origins": [], "what": []})
        if origin and origin not in e["origins"]:
            e["origins"].append(origin)
        if what and what not in e["what"]:
            e["what"].append(what)
    lost = sum(e["points"] for e in out.values())
    g = GLITCHES.get(glitches or "none", 0)
    found = []
    for e in out.values():
        found.append({"category": e["category"], "name": e["name"], "points": e["points"],
                      "origin": " and ".join(ORIGINS[o] for o in e["origins"]) or None, "what": "; ".join(e["what"])})
    if g and not any(e["points"] >= g for e in out.values()):
        lost += g
        found.append({"category": "player", "name": "the player saw %s glitches" % glitches, "points": g,
                      "origin": None, "what": ""})
    return max(0.0, 100.0 - lost), found


def geometry_rows(n):
    """a test's raw geometry samples (numbers.geometry: "x y w h fullscreen sw sh" or "-") as display_sample rows"""
    rows = []
    for s in n.get("geometry") or []:
        try:
            v = [int(x) for x in s.split()]
        except ValueError:
            v = []
        rows.append(tuple(v) if len(v) == 7 and v[2] > 0 and v[3] > 0 and v[5] > 0 and v[6] > 0 else None)
    return rows


def pointer_of(n, review):
    """True / False / None: the reviewer's answer (they confirm or override the measurement), else measured: an
    X pointer visible over the game's window in half the screenshots or more (geom.py, XFixes)"""
    if review.get("pointer") in ("visible", "hidden"):
        return review["pointer"] == "visible"
    if not n.get("pointer_samples"):
        return None
    return 2 * n.get("pointer_visible", 0) >= n["pointer_samples"]


def display_v2(tests):
    """(score, words, [(words, points)]) of a launcher's graded tests: the median sample of all their geometry
    (display_score's scale), the game on its shipped defaults, minus what the reviewer saw: stretched, a desktop
    pointer, text under SMALL_TEXT_PX"""
    rows = [r for t in tests for r in geometry_rows(t.get("numbers", {}))]
    base, words = display_score(rows)
    if base is None:
        return None, None, []
    ded = []
    ns = [(t.get("numbers", {}), t.get("review", {})) for t in tests]
    if any(rv.get("stretched") == "yes" for _, rv in ns):
        ded.append(("stretched to the wrong shape", STRETCHED))
    if any(pointer_of(n, rv) for n, rv in ns):
        ded.append(("a desktop mouse pointer over the game", POINTER))
    px = [rv["text_px"] for _, rv in ns if isinstance(rv.get("text_px"), (int, float))]
    if px and min(px) < SMALL_TEXT_PX:
        ded.append(("text %d px high at 1080p" % min(px), SMALL_TEXT))
    return max(0.0, base - sum(p for _, p in ded)), words, ded


def end_failure(t):
    """"crash", "hang" or None: a crash (a signal, a segfault, out of memory), or an unclean quit (the player
    had to kill it, an error exit, or the desktop not back within QUIT_BACK_S of the game's end), which
    counts like a crash in Stability. The harness's own stops (thermal) are neither"""
    n = t.get("numbers", {})
    if t.get("crashed") or t.get("end") in ("crash", "oom"):
        return "crash"
    if t.get("end") in ("closed", "error") or n.get("desktop_stuck") or (n.get("desktop_back_s") or 0) > QUIT_BACK_S:
        return "hang"
    return None


def measured_checks(t):
    """a test's checklist answers {item: "yes" | "partly" | "no"} from the player's form and the recorder's
    numbers (bench/harness/inputlog.py); what can't be told is left out (not tested). The form wins; the input
    log fills in what the player didn't answer"""
    n, f = t.get("numbers", {}), t.get("form", {})
    out = {}
    pad_used = (n.get("pad_events_play") or 0) > 0
    if n.get("pads_present") and pad_used:     # measured: the game opened the pad the player used
        out["pad_setup"] = "yes" if n.get("pad_opened") else "no"
        out["pad_play"] = "yes" if n.get("pad_opened") else "no"
    play, setup = FORM_PAD_PLAY.get(f.get("pad_play"), ("na", "na"))
    if play != "na":
        out["pad_play"], out["pad_setup"] = play, setup
    if FORM_PAD_MENUS.get(f.get("pad_menus"), "na") != "na":
        out["pad_menus"] = FORM_PAD_MENUS[f["pad_menus"]]
    if n.get("pads_hotplugged"):
        out["pad_hotplug"] = "yes" if n.get("pads_hotplug_opened") else "no"
    # the keyboard while playing with the pad: only when the logger could read the keyboard (the input group)
    if out.get("pad_play") == "yes" and pad_used and n.get("kb_readable"):
        out["no_keyboard"] = "no" if (n.get("kb_presses") or 0) > KEYBOARD_STRAY else "yes"
    if FORM_KM.get(f.get("keyboard_mouse"), "na") != "na":
        out["keyboard_mouse"] = FORM_KM[f["keyboard_mouse"]]
    for k in ("menus_modes", "saves_load"):
        if f.get(k) in ("yes", "no"):
            out[k] = f[k]
    return out


def checks_of(tests):
    """{item: (answer, origin, what)} over a launcher's tests, oldest first, the newest answer winning: the
    measured and asked answers, then the reviewer's (REVIEW.md `check:` and `broken:` lines), which win over
    both. Only played sessions answer: a scripted run's checks are ignored"""
    out = {}
    for t in sorted((t for t in tests if t.get("kind") == "session"), key=lambda t: (t.get("date", ""), t.get("id", ""))):
        for k, v in measured_checks(t).items():
            out[k] = (v, None, "")
        rv = t.get("review", {})
        for line in rv.get("checks") or []:
            k, _, v = line.partition(" ")
            if k in CHECK_NAMES and v.strip() in ("yes", "partly", "no", "na"):
                out[k] = (v.strip(), None, "")
        for line in rv.get("broken") or []:
            head, _, what = line.partition(":")
            k, _, origin = head.strip().partition(" ")
            if k in CHECK_NAMES:
                out[k] = ("no", origin.strip() or None, what.strip())
    return out


def checklist(checks, items, cap_if_no=None, cap=None):
    """(score, [failed {item, name, answer, origin, what}], passed, applicable): the share of the tested items
    that pass (partly counts half), x 100; None when none was tested. cap_if_no: an item whose "no" caps the
    score at cap"""
    tested = [(k, checks[k]) for k, _ in items if k in checks and checks[k][0] in ANSWER_SHARE]
    if not tested:
        return None, [], 0, 0
    got = sum(ANSWER_SHARE[v] for _, (v, _, _) in tested)
    s = 100.0 * got / len(tested)
    if cap_if_no and checks.get(cap_if_no, ("na",))[0] == "no":
        s = min(s, cap)
    failed = [{"item": k, "name": CHECK_NAMES[k], "answer": v, "origin": ORIGINS.get(o) if o else None, "what": w}
              for k, (v, o, w) in tested if v != "yes"]
    return s, failed, got, len(tested)


def controller_badge(checks):
    """the controller badge from the gamepad items: full (works in play without setup, the menus too), partial,
    keyboard (the pad didn't work); None when the pad wasn't tried"""
    a = {k: v[0] for k, v in checks.items()}
    if a.get("pad_play") == "no":
        return "keyboard"
    if a.get("pad_play") == "yes" or a.get("pad_setup") in ("yes", "no"):
        full = a.get("pad_setup") == "yes" and a.get("pad_menus", "yes") == "yes" and a.get("no_keyboard") != "no"
        return "full" if full else "partial"
    return None


def test_scores_v2(t, fps_cap=None):
    """the per-test aspect scores of a data-version-2 test that don't need its launcher's other tests (frame,
    smooth, sound, load); the rest are the launcher's (combine_v2). Load is the initial load (launch to the
    first frame) plus the level load (the player's last input before the play mark to the mark), where known"""
    n = t.get("numbers", {})
    load = n.get("load_s")
    if load is not None and n.get("load_level_s") is not None:
        load += n["load_level_s"]
    return {"frame": frame_score(n.get("median_fps"), n.get("low1_fps"), fps_cap),
            "smooth": smooth_score(stutter_v2(n), n.get("longest_ms")),
            "sound": sound_score(n.get("sound_faults_per_min"), bool(n.get("snd_stall")), n.get("sound_effects"),
                                 n.get("music"), n.get("sound_dropouts_per_min")),
            "load": load_score(load)}


def stutter_v2(n):
    """rules 2's stutter: against the frames around each frame (import-run.py local_stutter), where measured"""
    return n["stutter_local_s_per_min"] if n.get("stutter_local_s_per_min") is not None else n.get("stutter_s_per_min")


def combine_v2(tests, fps_cap=None, designed_for="pad"):
    """combine() under rules 2: a launcher's tests of data version 2. The aspects as in combine() where they
    didn't change (medians of frame, smooth, sound, load over the graded tests); Picture, Display, Controls
    and Features from the launcher's sessions together (only sessions, kind "session", count at all); Stability counts crashes and unclean quits over the
    play of the sessions (scripted runs and soaks never count: nobody is playing). No weakest link:
    the mean's medal (rules 2.1's scale, MEDALS_V2), Rust when a core subsystem is broken (broken_v2), Scrap when
    it can't be played (scrap_floor_v2). designed_for: "keyboard-mouse" scores Controls on the keyboard and mouse
    alone (the gamepad only makes the badge). Adds {rules, hangs, defects, display_deductions,
    controls_failed, features_failed, checks, controller, play_seconds, details, broken}."""
    tests = [t for t in tests if t.get("kind") == "session"]   # rules 2: only real played sessions count
    sessions = graded = tests
    played = [t for t in tests if t.get("play_seconds")]
    hours = sum(t["play_seconds"] for t in played) / 3600
    fails = [end_failure(t) for t in played]
    crashes, hangs = fails.count("crash"), fails.count("hang")
    per = [test_scores_v2(t, fps_cap) for t in graded]
    scores = {k: med(p[k] for p in per) for k in ("frame", "smooth", "sound", "load")}
    reviewed = [t for t in tests if t.get("picture", {}).get("verdict") in ("ok", "wrong") or t.get("review")]
    defects = [d for t in reviewed for d in parse_defects(t.get("review", {}).get("defects") or [])] if reviewed else None
    answers = [t.get("form", {}).get("glitches") for t in tests if t.get("form", {}).get("glitches") in GLITCHES]
    worst = max(answers, key=lambda a: GLITCHES[a]) if answers else None
    scores["picture"], found = picture_v2(defects, worst)
    scores["display"], display_words, display_ded = display_v2(graded)
    checks = checks_of(tests)
    if designed_for == "keyboard-mouse":
        scores["controls"], c_failed, c_pass, c_n = checklist(checks, KM_ITEMS)
    else:
        scores["controls"], c_failed, c_pass, c_n = checklist(checks, CONTROLS, "pad_setup", NO_PAD_CAP)
    scores["features"], f_failed, f_pass, f_n = checklist(checks, FEATURES)
    scores["stability"], provisional = stability_score(crashes + hangs, hours)
    nums = {}
    for key in ("median_fps", "low1_fps", "stutter_s_per_min", "longest_ms", "sound_faults_per_min",
                "sound_dropouts_per_min", "load_s", "load_level_s"):
        nums[key] = med(t.get("numbers", {}).get(key) for t in graded)
    nums["stutter_s_per_min"] = med(stutter_v2(t.get("numbers", {})) for t in graded)
    nums["display_detail"] = display_words
    det = {}
    if scores["picture"] is not None:
        det["picture"] = "; ".join("%s -%d%s" % (d["name"], d["points"], " (%s)" % d["origin"] if d["origin"] else "")
                                   for d in found) or "nothing wrong found"
    if display_words:
        det["display"] = "; ".join([display_words] + ["%s -%d" % d for d in display_ded])
    fmt = lambda x: ("%g" % x)
    if c_n:
        det["controls"] = "%s of %d tested checks pass%s%s" % (fmt(c_pass), c_n, "; not: " + "; ".join(
            f["name"] + (" (partly)" if f["answer"] == "partly" else "") for f in c_failed) if c_failed else "",
            " (designed for keyboard and mouse: the gamepad only makes the badge)" if designed_for == "keyboard-mouse" else "")
    if f_n:
        det["features"] = "%s of %d tested checks pass%s" % (fmt(f_pass), f_n, "; broken: " + "; ".join(
            f["name"] for f in f_failed) if f_failed else "")
    if nums["load_s"] is not None:
        det["load"] = ("%.0f s to the game's first screen + %.0f s from the last input before play to play" % (
            nums["load_s"], nums["load_level_s"]) if nums["load_level_s"] is not None else
            "%.0f s to the game's first screen (the level load not measured: no play mark or no input log)" % nums["load_s"])
    play_s = sum(t.get("play_seconds") or 0 for t in graded)
    extra = {"rules": 2, "hangs": hangs, "defects": found, "display_deductions": display_ded,
             "controls_failed": c_failed, "features_failed": f_failed, "controller": controller_badge(checks),
             "checks": {k: v[0] for k, v in checks.items()}, "play_seconds": play_s, "details": det,
             "designed_for": designed_for}
    base = {"aspects": scores, "numbers": nums, "hours": hours, "crashes": crashes, "sessions": len(sessions), **extra}
    if not graded and scores["picture"] is None:
        return dict(base, provisional=True, graded_by=None, **rate({}, None, WEIGHTS_V2, False, medal_of_v2))
    if not graded:
        base["aspects"] = {k: (v if k == "picture" else None) for k, v in scores.items()}
        return dict(base, provisional=True, graded_by="capture", **rate({}, None, WEIGHTS_V2, False, medal_of_v2))
    floor = scrap_floor_v2(graded, nums["median_fps"])
    broken = broken_v2(graded, nums, found, worst, crashes, hours, fps_cap)
    rated = rate(scores, floor, WEIGHTS_V2, False, medal_of_v2)
    if not floor and broken and rated["score"] is not None:   # playable, a core part broken: Rust, the mean beside it
        rated = dict(rated, medal="Rust", reason="; ".join(broken))
    missing = [ALL_ASPECT_NAMES[k].lower() for k in REQUIRED if scores.get(k) is None]
    if missing and not floor:
        rated = {"score": None, "medal": "Untested", "weakest": None, "capped": False,
                 "reason": " and ".join(missing) + " not measured"}
    return dict(base, provisional=provisional, graded_by="session", broken=broken, **rated)


def broken_v2(graded, nums, defects, glitches, crashes, hours, fps_cap=None):
    """the core subsystems that are broken, as words for the page ([] for none): explicit flags, not score
    thresholds (garbage textures leave Picture at 40). Picture: a garbage defect or the player's "broken"
    glitches; Sound: the effects not heard or a wedged device (missing music alone isn't); Frame rate: a median
    under 20 fps, unless the game's own cap is lower and held; Smoothness: a freeze over 5 s or 5 s of stutter
    a minute; Stability: under an hour of play between crashes"""
    out = []
    if any(d["category"] == "garbage" for d in defects) or glitches == "broken":
        out.append("picture broken (%s)" % ("garbage textures or geometry" if any(d["category"] == "garbage" for d in defects)
                                     else "the player saw it broken"))
    ns = [t.get("numbers", {}) for t in graded]
    if any(n.get("snd_stall") for n in ns):
        out.append("sound broken (the sound device wedged)")
    elif any(n.get("sound_effects") == "no" for n in ns):
        out.append("sound broken (no sound effects heard)")
    fps = nums.get("median_fps")
    if fps is not None and fps < MEDIAN_FPS[0][0] and not (fps_cap and fps >= 0.95 * fps_cap):
        out.append("frame rate broken (%.0f fps)" % fps)
    if (nums.get("longest_ms") or 0) > LONGEST_FRAME_MS[-1][0]:
        out.append("smoothness broken (a %.1f s freeze)" % (nums["longest_ms"] / 1000))
    elif (nums.get("stutter_s_per_min") or 0) >= STUTTER_S_PER_MIN[-1][0]:
        out.append("smoothness broken (%.1f s of stutter a minute)" % nums["stutter_s_per_min"])
    if crashes and hours / crashes < 1:
        out.append("stability broken (%d crash%s in %.1f h)" % (crashes, "" if crashes == 1 else "es", hours))
    return out


def parse_defects(lines):
    """[(category, origin, what)] of REVIEW.md's defect lines as result.toml keeps them ("<category> <pc|ps3>:
    <what>"); unknown categories are left out (import-run.py refuses them)"""
    out = []
    for line in lines:
        head, _, what = line.partition(":")
        words = head.split()
        cat = defect_category(words[0]) if words else None
        if cat:
            origin = words[1].lower() if len(words) > 1 and words[1].lower() in ORIGINS else None
            out.append((cat, origin, what.strip()))
    return out


# The calibration: the 13 games of the benchmark suite with their known numbers, as docs/GAMEDB.md
# describes (stutter ~ the stall seconds over the 2-minute window / 2, load unmeasured, stability clean,
# picture and display right). The numbers are the summary batch CALIBRATION_BATCH in gamedb
# (gamedb/benchmarks/batches.toml; each game's benchmark/results/<batch>.toml, every value with its source);
# calibration() reads them, the one place here that reads a file. A row: (label, median fps, 1% low, stall
# seconds in 120 s, longest ms, sound faults a minute).
CALIBRATION_BATCH = "suite-summary-2026-10"
CALIBRATION_METRICS = ("fps_steady", "low1_play", "stall_play_s", "longest_play_ms", "sound_faults_per_min")


def calibration(gamedb=None):
    """[(label, median, low1, stall_s, longest, faults)] of the calibration batch, in its entries' order;
    also (game, variant) as calibration_ids() gives"""
    return [row for row, _ in _calibration(gamedb)]


def calibration_ids(gamedb=None):
    return [gv for _, gv in _calibration(gamedb)]


def _calibration(gamedb):
    import importlib.util
    from pathlib import Path
    repo = Path(__file__).resolve().parent.parent.parent
    spec = importlib.util.spec_from_file_location("benchrecords", repo / "bench" / "analysis" / "benchrecords.py")
    br = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(br)
    out = []
    for game, variant, e in br.summary(CALIBRATION_BATCH, gamedb or repo / "gamedb"):
        out.append(((e["label"],) + tuple(e[m] for m in CALIBRATION_METRICS), (game, variant)))
    return out


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
    for row in calibration():
        s, r = calibrate(row)
        note = " (capped)" if r["capped"] else (" (%s)" % r["reason"] if r["reason"] else "")
        print("%-20s %6.0f %6.0f %6.0f %6.0f  %-8s  %s%s" % (
            row[0], s["frame"], s["smooth"], s["sound"], r["score"], r["weakest"], r["medal"], note))


if __name__ == "__main__":
    sys.exit(main())
