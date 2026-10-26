#!/usr/bin/env python3
"""shots.py: the screenshots of games/bench runs (client.sh BENCH_SHOTS, run.sh SHOTS=; docs/games/SUITE.md
"Screenshots"). The picture check: a PS3 capture next to the PC reference of the same recipe at the same
shot point, for a human or an agent to look at. Not a pixel diff: the two runs are at different moments
(loads differ, physics isn't deterministic); only a black or flat screen is flagged by itself.
  shots.py cost RUN...          per shot: the capture's wall time and the HUD frame times around it
                                (python3 only: runs on the PS3 too). The cost of a capture is the
                                longest frame it touched against the frames before it
  shots.py convert DIR...       (PC, ImageMagick) every *.xwd in DIR/shots (and DIR itself) to PNG; the
                                .xwd is removed once the PNG reads back with the same size
  shots.py sheet RUN... [--ref ROOT]
                                (PC) RUN/shots-vs-pc.png: one row per shot point, PS3 | PC (each 800x450:
                                1608 px wide), labelled with the game, variant, shot point, capture time
                                and both sides' builds; the PC side from ROOT/<game>-<variant>/ (default
                                build/bench-ref, made by tools/bench-ref.sh). A "shotsheet:" line goes into
                                RUN/result: the sheet, the reference, and BLACK/FLAT where a side is one
  shots.py post ROOT [--ref ROOT2]
                                (PC; make bench-pull runs it on build/bench) convert, then the sheet for
                                every run under ROOT whose sheet is missing or older than its shots (or
                                its reference's); the shotsheet: line also in RUN/shots-vs-pc.txt
  shots.py pending ROOT         the sheets under ROOT without a REVIEW.md next to them, oldest first:
                                what nobody has looked at yet
REVIEW.md (next to the sheet, written by the reviewer): the first line "verdict: ok|wrong|unclear
[words]", then whatever the reviewer saw; report.py shows the verdict as review=.
bench.txt lines (client.sh): shots=<points>, shot_ref=<from>:<s>, shot=<p>s:<file>:<ms>,
shot_t=<p>s:<s after t0>:<frame-time lines before>:<after>, shot_skipped=, shot_static=1, shot_err=.
The player's own shots of a played session (Alt+F12 or pad Select+Start, inputlog.py + client.sh): shot_mark=<t>s:<file>:<ms>,
<t> the seconds after t0 of the capture (parse(): "marks" {t: {"file", "ms", "t"}}); the sheet shows them first."""
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
TILE_W, TILE_H, GAP = 800, 450, 8
SKIP_PKGS = {"libspeq0", "libfluidsynth3", "libopenal1", "libsdl3-0", "ps3-gpu-tools"}


def parse(d):
    """the shot lines of run directory d's bench.txt: {"points", "ref", "ref_t", "static", "skipped",
    "err", "shots": {point: {"file", "ms", "t", "n0", "n1"}}, "window"}"""
    d = Path(d)
    out = {"points": [], "ref": None, "ref_t": None, "static": False, "skipped": [], "err": [], "shots": {},
           "window": [], "game": None, "variant": None, "marks": {}}
    f = d / "bench.txt"
    if not f.exists():
        return out
    for line in f.read_text(errors="replace").splitlines():
        k, _, v = line.partition("=")
        if k == "shots":
            out["points"] = [int(x) for x in v.split() if x.isdigit()]
        elif k == "shot_ref":
            src, _, t = v.partition(":")
            out["ref"], out["ref_t"] = src, num(t)
        elif k == "shot_static":
            out["static"] = v.strip() == "1"
        elif k == "shot_skipped":
            out["skipped"] = [int(x.rstrip("s")) for x in v.split() if x.rstrip("s").isdigit()]
        elif k == "shot_err":
            out["err"].append(v)
        elif k == "shot":
            m = re.match(r"(\d+)s:([^:]*):(\d+)$", v)
            if m:
                s = out["shots"].setdefault(int(m[1]), {})
                s.update(file=None if m[2] == "-" else m[2], ms=int(m[3]))
        elif k == "shot_mark":
            m = re.match(r"([\d.]+)s:([^:]*):(\d+)$", v)
            if m:
                out["marks"][float(m[1])] = {"file": None if m[2] == "-" else m[2], "ms": int(m[3]), "t": float(m[1])}
        elif k == "shot_t":
            m = re.match(r"(\d+)s:([\d.]+):([-\d]+):([-\d]+)$", v)
            if m:
                s = out["shots"].setdefault(int(m[1]), {})
                s.update(t=float(m[2]), n0=None if m[3] == "-" else int(m[3]), n1=None if m[4] == "-" else int(m[4]))
        elif k == "window":
            w = [float(x) for x in v.split() if num(x) is not None]
            out["window"] = list(zip(w[::2], w[1::2]))
        elif k in ("game", "variant"):
            out[k] = v
    return out


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def image(d, s):
    """the shot's picture in run directory d: the .xwd as recorded, else its PNG (after convert)"""
    if not s.get("file"):
        return None
    p = Path(d) / s["file"]
    for c in (p, p.with_suffix(".png")):
        if c.exists():
            return c
    return None


def in_window(sh):
    """the shots whose capture started inside the measured window"""
    return [p for p, s in sh["shots"].items()
            if "t" in s and any(a <= s["t"] <= b for a, b in sh["window"])]


def frametimes(d):
    f = Path(d) / "hud" / "frametime_(ms)"
    out = []
    if f.exists():
        for x in f.read_text(errors="replace").split():
            if (v := num(x)) is not None:
                out.append(v)
    return out


def costs(d):
    """per shot taken: point, t, ms, frames (the frame-time lines written while it ran, +1 for the
    frame in flight), longest (ms, the longest of them), before (median and max of the 120 frames
    before), inwin"""
    sh = parse(d)
    ft = frametimes(d)
    win = set(in_window(sh))
    out = []
    for p in sorted(sh["shots"]):
        s = sh["shots"][p]
        if "ms" not in s:
            continue
        c = {"point": p, "t": s.get("t"), "ms": s["ms"], "inwin": p in win, "ok": bool(s.get("file"))}
        n0, n1 = s.get("n0"), s.get("n1")
        if ft and n0 is not None and n1 is not None:
            touched = ft[n0:n1 + 1]
            before = ft[max(0, n0 - 120):n0]
            c["frames"] = len(touched)
            c["longest"] = max(touched) if touched else None
            if before:
                c["before_med"] = statistics.median(before)
                c["before_max"] = max(before)
        out.append(c)
    return out


def summary(d):
    """report.py's columns: shot_n (shots taken), shot_ms (the slowest capture), shot_win (shots in the
    measured window), shot_frame (the longest frame a capture touched, ms), review (REVIEW.md's verdict)"""
    r = {}
    cs = costs(d)
    if cs:
        r["shot_n"] = sum(1 for c in cs if c["ok"])
        r["shot_ms"] = max(c["ms"] for c in cs)
        r["shot_win"] = sum(1 for c in cs if c["inwin"])
        lf = [c["longest"] for c in cs if c.get("longest") is not None]
        if lf:
            r["shot_frame"] = round(max(lf), 1)
    if (v := review(d)) is not None:
        r["review"] = v
    return r


def review(d):
    f = Path(d) / "REVIEW.md"
    if not f.exists():
        return None
    for line in f.read_text(errors="replace").splitlines():
        m = re.match(r"\s*verdict:\s*(\S+)", line, re.I)
        if m:
            return m[1].lower()
    return "?"


def show_cost(d):
    sh = parse(d)
    print("== %s: shots %s from %s at %s s%s" % (Path(d).name, " ".join(map(str, sh["points"])) or "-", sh["ref"] or "-",
                                                sh["ref_t"] if sh["ref_t"] is not None else "-",
                                                ", skipped " + " ".join("%ds" % p for p in sh["skipped"]) if sh["skipped"] else ""))
    for e in sh["err"]:
        print("   error: " + e)
    for c in costs(d):
        line = "   %4ds at %s s: capture %d ms" % (c["point"], c["t"] if c["t"] is not None else "?", c["ms"])
        if "frames" in c:
            line += ", %d frame(s) touched, longest %s ms" % (c["frames"], "%.1f" % c["longest"] if c["longest"] is not None else "-")
            if "before_med" in c:
                line += " (the 120 before: median %.1f, max %.1f)" % (c["before_med"], c["before_max"])
        line += ", in the window" if c["inwin"] else ", outside the window"
        if not c["ok"]:
            line += ", FAILED"
        print(line)


# --- the PC side: ImageMagick ----------------------------------------------------------------
def magick(*args):
    p = subprocess.run(["magick", *map(str, args)], capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError("magick %s: %s" % (" ".join(map(str, args[:3])), p.stderr.strip()[-300:]))
    return p.stdout


def size(path):
    return magick("identify", "-format", "%w %h\n", path).split("\n")[0].strip()


def convert(d):
    """every .xwd in d/shots and d to PNG; the .xwd removed once the PNG has the same size. Returns the PNGs"""
    d = Path(d)
    done = []
    for x in sorted(list((d / "shots").glob("*.xwd")) + list(d.glob("*.xwd"))):
        png = x.with_suffix(".png")
        tmp = x.with_suffix(".png.tmp")
        try:
            magick(x, "PNG:%s" % tmp)
            if size(tmp) != size(x):
                raise RuntimeError("size %s against %s" % (size(tmp), size(x)))
        except RuntimeError as e:
            print("!! %s: %s (the .xwd kept)" % (x, e))
            tmp.unlink(missing_ok=True)
            continue
        os.replace(tmp, png)
        x.unlink()
        done.append(png)
    return done


def luma(path):
    """mean and standard deviation of the brightness (0..1), on a 64x36 thumbnail"""
    v = magick(path, "-colorspace", "Gray", "-resize", "64x36!", "-format", "%[fx:mean] %[fx:standard_deviation]", "info:",
               ).split()
    return float(v[0]), float(v[1])


def flag(path):
    """BLACK (almost no light), FLAT (one colour all over), or empty"""
    try:
        m, sd = luma(path)
    except (RuntimeError, ValueError, IndexError):
        return "UNREADABLE"
    if m < 0.03:
        return "BLACK"
    if sd < 0.015:
        return "FLAT"
    return ""


def text(t):
    """a string for -annotate: % escaped, never a leading @ (a file)"""
    t = t.replace("%", "%%")
    return " " + t if t.startswith("@") else t


def strip(path, w, lines, bg="#202020", pt=17):
    h = 10 + 24 * len(lines)
    args = ["-size", "%dx%d" % (w, h), "xc:" + bg, "-fill", "white", "-pointsize", str(pt), "-gravity", "northwest"]
    for i, ln in enumerate(lines):
        args += ["-annotate", "+8+%d" % (6 + 24 * i), text(ln)]
    magick(*args, path)


def tile(path, src, note):
    if src is None:
        magick("-size", "%dx%d" % (TILE_W, TILE_H), "xc:#404040", "-fill", "#d0d0d0", "-pointsize", "26",
               "-gravity", "center", "-annotate", "+0+0", text(note), path)
    else:
        magick(src, "-resize", "%dx%d" % (TILE_W, TILE_H), "-background", "#404040", "-gravity", "center",
               "-extent", "%dx%d" % (TILE_W, TILE_H), path)


def versions(d):
    """the run's builds from its versions file (run.sh, tools/bench-ref.sh): 'pkg ver, ..., kernel K'"""
    f = Path(d) / "versions"
    if not f.exists():
        return "versions unknown"
    out, kern = [], None
    for line in f.read_text(errors="replace").splitlines():
        p, _, v = line.partition("\t")
        if not v:
            kern = line.strip() or kern
        elif p.split(":")[0] not in SKIP_PKGS and v.strip():
            out.append("%s %s" % (p.split(":")[0], v.strip()))
    if kern:
        out.append("kernel " + kern)
    return ", ".join(out)


REFS = {"load": "the load's end", "load-nomark": "the load's end (during() set no mark)", "re": "the recipe's SHOT_RE line",
        "mark": "the recipe's shot_mark (after its keys)", "start": "the client's start"}


def wrap(line, n=150):
    """a long label line in pieces at ", " (the sheet is 1608 px: about 150 characters at 18 pt)"""
    out = []
    while len(line) > n and ", " in line[:n]:
        i = line[:n].rindex(", ") + 1
        out.append(line[:i])
        line = "    " + line[i + 1:]
    return out + [line]


def ref_dir(sh, root):
    r = Path(root) / ("%s-%s" % (sh["game"], sh["variant"]))
    return r if (r / "bench.txt").exists() else None


def sheet(d, root=None):
    """d/shots-vs-pc.png and the result's shotsheet: line; returns (sheet path, the line)"""
    d = Path(d)
    root = Path(root) if root else REPO / "build" / "bench-ref"
    sh = parse(d)
    if not sh["points"] and not sh["shots"] and not sh["marks"]:
        return None, None
    convert(d)
    rd = ref_dir(sh, root)
    rs = parse(rd) if rd else None
    pts = sorted(set(sh["points"]) | set(sh["shots"]) | (set(rs["points"]) | set(rs["shots"]) if rs else set()))
    out = d / "shots-vs-pc.png"
    flags = []
    w = 2 * TILE_W + GAP
    with tempfile.TemporaryDirectory(prefix="shotsheet-") as tmp:
        tmp = Path(tmp)
        parts = [tmp / "head.png"]
        head = ["%s %s: PS3 (left) | PC reference (right, llvmpipe, GL 2.1 forced)%s" % (
                    sh["game"], sh["variant"], ", a static screen: a close comparison is fair" if sh["static"] else ""),
                "PS3 %s: %s" % (d.name, versions(d)),
                ("PC  %s: %s" % (rd.name, versions(rd))) if rd else
                "PC  no reference in %s: tools/bench-ref.sh %s %s" % (root, sh["game"], sh["variant"]),
                "shot points: seconds after %s (PS3 at %s s%s)" % (
                    REFS.get(sh["ref"], sh["ref"] or "?"), sh["ref_t"] if sh["ref_t"] is not None else "-",
                    ", PC at %s s" % rs["ref_t"] if rs and rs["ref_t"] is not None else "")]
        strip(parts[0], w, [x for ln in head for x in wrap(ln)], bg="#000000", pt=18)
        rows = [("mark", t) for t in sorted(sh["marks"])] + [("pt", p) for p in pts]   # the player's shots first
        for i, (kind, p) in enumerate(rows):
            if kind == "mark":
                a, b = sh["marks"][p], {}
            else:
                a = sh["shots"].get(p, {})
                b = rs["shots"].get(p, {}) if rs else {}
            ia, ib = image(d, a), (image(rd, b) if rd else None)
            fa, fb = (flag(ia) if ia else ""), (flag(ib) if ib else "")
            if fa:
                flags.append("PS3 %s at %ss" % (fa, p))
            if fb:
                flags.append("PC %s at %ss" % (fb, p))
            if kind == "mark":
                la = "Player's shot at %s s, capture %s ms%s" % (p, a.get("ms", "-"), "  " + fa if fa else "")
                lb = "PC: no reference (the player marked this one)"
            else:
                la = "PS3 %d s: at %s s, capture %s ms%s" % (p, a.get("t", "-"), a.get("ms", "-"), "  " + fa if fa else "")
                lb = "PC %d s: at %s s%s" % (p, b.get("t", "-"), "  " + fb if fb else "")
            lab = tmp / ("l%d.png" % i)
            magick("-size", "%dx34" % w, "xc:#202020", "-fill", "white", "-pointsize", "18", "-gravity", "northwest",
                   "-annotate", "+8+7", text(la), "-annotate", "+%d+7" % (TILE_W + GAP + 8), text(lb), lab)
            ta, tb = tmp / ("a%d.png" % i), tmp / ("b%d.png" % i)
            why = "not reached (the run ended)" if kind == "pt" and p in sh["skipped"] else "no shot"
            tile(ta, ia, "PS3: " + why)
            tile(tb, ib, "PC: " + ("the player's own shot: no PC shot" if kind == "mark" else
                                   "no reference run" if not rd else
                                   "not reached (the run ended)" if p in rs["skipped"] else "no shot"))
            row = tmp / ("r%d.png" % i)
            magick(ta, "-size", "%dx%d" % (GAP, TILE_H), "xc:#000000", tb, "+append", row)
            parts += [lab, row]
        new = tmp / "sheet.png"
        magick(*parts, "-background", "#000000", "-append", "+repage", "-depth", "8", new)
        part = out.with_name(out.name + ".tmp")
        shutil.copyfile(new, part)
        os.replace(part, out)
    line = "shotsheet: %s vs %s" % (out, rd if rd else "no PC reference (tools/bench-ref.sh %s %s)" % (sh["game"], sh["variant"]))
    if rd:
        line += " (PC: %s)" % versions(rd)
    if flags:
        line += "; " + ", ".join(flags)
    (d / "shots-vs-pc.txt").write_text(line + "\n")
    result_line(d, line)
    return out, line


def result_line(d, line):
    """the shotsheet: line into d/result, replacing an older one (a pull brings the PS3's result back)"""
    res = Path(d) / "result"
    old = res.read_text(errors="replace").splitlines() if res.exists() else []
    if line in old:
        return
    res.write_text("\n".join([x for x in old if not x.startswith("shotsheet: ")] + [line]) + "\n")


def runs(root):
    return sorted(p for p in Path(root).iterdir() if p.is_dir() and not p.name.startswith(".") and (p / "bench.txt").exists())


def post(root, ref=None):
    made = []
    refroot = Path(ref) if ref else REPO / "build" / "bench-ref"
    for d in runs(root):
        sh = parse(d)
        if not sh["shots"]:
            continue
        convert(d)
        out = d / "shots-vs-pc.png"
        src = list((d / "shots").glob("*")) if (d / "shots").is_dir() else []
        rd = ref_dir(sh, refroot)
        if rd:
            src += list((rd / "shots").glob("*"))
        if out.exists() and all(s.stat().st_mtime <= out.stat().st_mtime for s in src):
            if (d / "shots-vs-pc.txt").exists():   # a new pull brought the PS3's result back: the line again
                result_line(d, (d / "shots-vs-pc.txt").read_text().strip())
            continue
        _, line = sheet(d, refroot)
        made.append(line)
        print(line)
    return made


def pending(root):
    out = []
    for d in runs(root):
        s = d / "shots-vs-pc.png"
        if s.exists() and not (d / "REVIEW.md").exists():
            out.append((s.stat().st_mtime, s))
    for _, s in sorted(out):
        print(s)
    return [s for _, s in sorted(out)]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    ref = None
    if "--ref" in args:
        i = args.index("--ref")
        ref = args[i + 1]
        args = args[:i] + args[i + 2:]
    if cmd == "cost":
        for d in args:
            show_cost(d)
    elif cmd == "convert":
        for d in args:
            for p in convert(d):
                print(p)
    elif cmd == "sheet":
        for d in args:
            out, line = sheet(d, ref)
            print(line or "%s: no shots" % d)
    elif cmd == "post":
        made = post(args[0] if args else REPO / "build" / "bench", ref)
        print(">> %d new sheet(s); not reviewed yet: games/bench/shots.py pending %s" % (len(made), args[0] if args else "build/bench"))
    elif cmd == "pending":
        pending(args[0] if args else REPO / "build" / "bench")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
