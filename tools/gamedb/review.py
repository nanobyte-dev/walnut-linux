#!/usr/bin/env python3
"""review.py RUN...: what the reviewing agent needs to judge a played session's picture under rules 2.0
(docs/GAMEDB.md "Where each criterion comes from", docs/games/SUITE.md "Played sessions"): the agent that
imports a run looks at these and answers the questions, by looking, never by pixel metrics.

Writes into each run directory:
  shots-vs-pc.png    the session's PS3 screenshots next to the PC reference where one exists (shots.py's sheet)
  review-text.png    1:1 crops (no scaling) of the four corners and the middle of up to two screenshots, with a
                     pixel ruler (ticks every 2 px, 10 / 18 / 30 px marked): the smallest text's height is read off it
  REVIEW.md          the questionnaire, every answer "?" (import-run.py refuses one left so); an existing
                     REVIEW.md is never overwritten: the template goes to REVIEW.template.md then
The template carries the measured hints: black bars on the screenshots (a hint for "stretched", never scored)
and the pointer the recorder saw (the reviewer confirms or overrides it).
"""
import argparse
import importlib.util
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path[:0] = [str(REPO / "games" / "bench"), str(HERE)]
import score   # noqa: E402
import shots   # noqa: E402

CROP_W, CROP_H = 480, 270
REGIONS = {"top left": (0, 0), "top right": (1440, 0), "middle": (720, 405), "bottom left": (0, 810),
           "bottom right": (1440, 810)}


def importer():
    spec = importlib.util.spec_from_file_location("import_run", HERE / "import-run.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def magick(*args):
    p = subprocess.run(["magick", *map(str, args)], capture_output=True, text=True)
    if p.returncode:
        raise RuntimeError("magick: %s" % p.stderr.strip()[-300:])
    return p.stdout


def ruler(path, h=CROP_H):
    """a 40 px wide pixel ruler: a tick every 2 px, longer every 10, and 10, 18 and 30 px marked from the top"""
    args = ["-size", "40x%d" % h, "xc:#000000", "-stroke", "#c0c0c0"]
    for y in range(0, h, 2):
        args += ["-draw", "line 0,%d %d,%d" % (y, 12 if y % 10 == 0 else 5, y)]
    args += ["-stroke", "#d49a5c"]
    for y, lab in ((10, "10"), (18, "18"), (30, "30")):
        args += ["-draw", "line 0,%d 20,%d" % (y, y)]
    args += ["-stroke", "none", "-fill", "#d49a5c", "-pointsize", "10"]
    for y, lab in ((10, "10"), (18, "18"), (30, "30")):
        args += ["-annotate", "+22+%d" % (y + 4), lab]
    magick(*args, path)


def text_sheet(pictures, out):
    """review-text.png: for each picture, a row of 1:1 crops with a ruler in front of each"""
    with tempfile.TemporaryDirectory(prefix="review-") as tmp:
        tmp = Path(tmp)
        r = tmp / "ruler.png"
        ruler(r)
        rows = []
        for i, (label, pic) in enumerate(pictures):
            cells = []
            for j, (name, (x, y)) in enumerate(REGIONS.items()):
                c = tmp / ("c%d-%d.png" % (i, j))
                magick(pic, "-crop", "%dx%d+%d+%d" % (CROP_W, CROP_H, x, y), "+repage", "-background", "#000000",
                       "-extent", "%dx%d" % (CROP_W, CROP_H), c)
                cells += [r, c]
            row = tmp / ("r%d.png" % i)
            head = tmp / ("h%d.png" % i)
            magick("-size", "%dx26" % (len(REGIONS) * (CROP_W + 40)), "xc:#202020", "-fill", "white", "-pointsize", "16",
                   "-annotate", "+8+18", "%s, 1:1 crops: %s" % (label, ", ".join(REGIONS)), head)
            magick(*cells, "+append", row)
            rows += [head, row]
        magick(*rows, "-background", "#000000", "-append", "+repage", out)


def template(d, hints):
    lines = [
        "# REVIEW.md for %s: answer every line (rules 2.0); a \"?\" left is unanswered and import-run.py refuses it." % d.name,
        "# Judge by looking at shots-vs-pc.png and review-text.png, never by pixel metrics. \"pc\": the PC reference",
        "# shows it too (upstream's); \"ps3\": PS3/Walnut only. \"where\": the screen or the moment, short.",
        "verdict: ?",
        "# Stretched or squashed: circles and characters out of proportion (black bars around the right shape are fine).",
        "# Hint, measured: %s" % hints["bars"],
        "stretched: ?",
        "# The smallest menu or HUD text, its height in px at 1080p (review-text.png's ruler): N: where, or none.",
        "text_px: ?",
        "# A mouse pointer over the game. Measured: %s. Confirm or override: visible or hidden." % hints["pointer_words"],
        "pointer: %s" % hints["pointer"],
        "# Each defect category: no, or pc: where, or ps3: where (one line per place is fine).",
    ]
    for cat, name in score.DEFECT_NAMES.items():
        lines += ["# %s (-%d)" % (name, score.DEFECTS[cat]), "%s: ?" % cat]
    lines += ["# Anything broken on screen or in the notes (a menu, a mode, saves, the pad): none, or one line each",
              "# broken: <item> <pc|ps3>: <what>; items: %s" % ", ".join(score.CHECK_NAMES),
              "broken: ?"]
    return "\n".join(lines) + "\n"


def hints(d):
    ir = importer()
    rows, known, visible = ir.geometry_raw(d)
    sh = shots.parse(d)
    pics = [shots.image(d, s) for _, s in sorted(sh["shots"].items()) if shots.image(d, s)]
    screen = next((tuple(int(x) for x in r.split()[5:7]) for r in rows if r != "-"), (1920, 1080))
    b = ir.bars(pics, screen) if pics else {}
    if b.get("bars"):
        bars = "%s on %d of %d lit shots, the picture %s on %dx%d" % (b["bars"], b["bars_shots"], b["lit_shots"],
                                                                     b["bars_content"], *screen)
    elif b:
        bars = "no black bars (%d lit shots)" % b["lit_shots"]
    else:
        bars = "no lit shots to look at"
    if known:
        pw = "visible over the game in %d of %d shots" % (visible, known)
        pointer = "visible" if 2 * visible >= known else "hidden"
    else:
        pw, pointer = "not measured (no cursor column)", "?"
    return {"bars": bars, "pointer_words": pw, "pointer": pointer}, pics


def review(run):
    d = Path(run)
    h, pics = hints(d)
    out = []
    sheet, _ = shots.sheet(d)
    if sheet:
        out.append(sheet)
    lit = [p for p in pics if statistics.mean(importer().gray(p) or b"\0") >= 12]
    pick = [lit[0], lit[len(lit) // 2]] if len(lit) > 1 else lit
    if pick:
        text_sheet([(Path(p).name, p) for p in dict.fromkeys(pick)], d / "review-text.png")
        out.append(d / "review-text.png")
    t = d / ("REVIEW.template.md" if (d / "REVIEW.md").exists() else "REVIEW.md")
    t.write_text(template(d, h))
    out.append(t)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("runs", nargs="+")
    for run in ap.parse_args().runs:
        for p in review(run):
            print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
