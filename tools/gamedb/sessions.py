#!/usr/bin/env python3
"""sessions.py: the played sessions of one game in detail, for its Test runs tab (technical.json games[].sessions,
written by tools/gamedb/technical.py; site only).

Design
- One entry per played session (rating/sessions/<id>/result.toml with kind = "session"), newest first: the
  session rated on its own (score.combine over that one session, the rules it was recorded under), every
  aspect's score and what was measured (catalog.details), the sound dropouts, the end-of-session form's
  answers with their questions (bench/harness/session-form.py QUESTIONS), the player's notes, the picture
  review, the screenshots the gallery shows, and the builds it ran on.
- Builds: the game's package as installed (with our +ps3.N), then the platform it ran on: the kernel, Mesa,
  ps3rsx, libspeq, and the patched libraries games link (OpenAL, FluidSynth, SDL3), read from the session's
  raw/versions.xz (result.toml keeps only the game, Mesa, ps3rsx and the kernel). A fixed list (STACK), not
  each game's dependency closure: the closure needs Debian's ppc64 package index, which the site build doesn't
  have; these are the libraries Walnut patches for every game, and the console had them all installed.
  The desktop (ps3-desktop), walnut's release (shown on its own) and other metapackages are left out.
- Who played it: source "ours" (no key in result.toml) or "submitted" (a tester's, tools/gamedb/submissions.py),
  with the tester's nickname (none: anonymous; the page says "a tester").
- csv(): one row per session, the aspects as columns, for the page's download link.
- stdlib only.
"""
import csv
import importlib.util
import io
import lzma
import re
import sys
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import check   # noqa: E402
import score   # noqa: E402

# (versions key, the name the page shows): the platform under a game, in this order
STACK = [("kernel", "Linux kernel"), ("mesa-libgallium", "Mesa"), ("ps3rsx-modules-ps3", "ps3rsx"),
         ("libspeq0", "libspeq"), ("libopenal1", "OpenAL Soft"), ("libfluidsynth3", "FluidSynth"),
         ("libsdl3-0", "SDL3")]
# form keys that are bookkeeping, not answers
FORM_SKIP = {"saved", "form", "game", "variant", "form_version", "notes"}
# the first form's one controls answer (before the gamepad questions)
CONTROLS = {"full": "Full controller support", "partial": "Partly", "keyboard-mouse": "Keyboard and mouse only",
            "none": "None"}
CSV_NUMBERS = ["median_fps", "low1_fps", "stutter_s_per_min", "longest_ms", "sound_dropouts_per_min",
               "sound_faults_per_min", "load_s", "min_avail_kb"]


def questions(repo=REPO):
    """{key: (question, {answer: label})} of the session form, or {} when it can't be read"""
    path = Path(repo) / "bench" / "harness" / "session-form.py"
    try:
        spec = importlib.util.spec_from_file_location("session_form", path)
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        return {k: (q, dict(opts)) for k, q, opts, _ in m.QUESTIONS}
    except Exception:   # an older checkout, a moved file: the answers show by their keys
        return {}


def raw_versions(sdir):
    """{package: version} of the session's raw/versions.xz ("<pkg>[:arch]\\t<version>", the kernel's line
    without a tab), {} without one"""
    f = Path(sdir) / "raw" / "versions.xz"
    out = {}
    if not f.is_file():
        return out
    for line in lzma.decompress(f.read_bytes()).decode("utf-8", "replace").splitlines():
        if line.startswith("data:"):
            continue
        p, _, v = line.partition("\t")
        if not v:
            if line.strip():
                out["kernel"] = line.strip()
        elif v.strip():
            out[p.split(":")[0]] = v.strip()
    return out


def builds(t, raw, packages):
    """[{name, version, ours}] the session ran on: the game's packages, then STACK; ours: a +ps3 build"""
    v = dict(raw)
    v.update(t.get("versions", {}))
    out = [{"name": p, "version": v[p]} for p in packages if p in v]
    out += [{"name": label, "version": v[k]} for k, label in STACK if k in v]
    for b in out:
        b["ours"] = "+ps3." in b["version"] or bool(re.fullmatch(r"0\.\d+(\+.*)?", b["version"]))
    return out


def form_answers(t, sdir, qs):
    """[{question, answer}] of the end-of-session form, and the player's notes"""
    form = dict(t.get("form") or {})
    f = Path(sdir) / "form.toml"
    if f.is_file():
        try:
            form = {**tomllib.loads(f.read_text()), **form}
        except tomllib.TOMLDecodeError:
            pass
    out = []
    order = list(qs) + [k for k in form if k not in qs]
    for k in order:
        if k not in form or k in FORM_SKIP:
            continue
        if k == "controls" and "pad_play" in form:
            continue   # derived from the gamepad answers (session-form.py derived_controls)
        q, labels = qs.get(k, ("Controls", CONTROLS) if k == "controls" else (k.replace("_", " ").capitalize(), {}))
        out.append({"question": q, "answer": labels.get(str(form[k]), str(form[k]))})
    return out, (form.get("notes") or "").strip() or None


def review_facts(rv):
    """[{what, answer}] of the picture review (result.toml [review], rules 2): the reviewer's answers and the
    defects and broken things found, each with where it shows ("pc": also on the PC, "ps3": PS3/Walnut only)"""
    out = []
    if rv.get("stretched"):
        out.append({"what": "Stretched or squashed", "answer": rv["stretched"]})
    if rv.get("text_px") is not None:
        out.append({"what": "Smallest text", "answer": "%s px at 1080p%s" % (
            rv["text_px"], ": " + rv["text_where"] if rv.get("text_where") else "")})
    if rv.get("pointer"):
        out.append({"what": "Mouse pointer", "answer": rv["pointer"]})
    for kind, label in (("defects", "Picture defect"), ("broken", "Broken")):
        for d in rv.get(kind, []):
            name, _, rest = str(d).partition(" ")
            origin, _, what = rest.partition(": ")
            where = score.ORIGINS.get(origin)
            text = "%s%s%s" % (name.replace("_", " "), " (%s)" % where if where else "", ": " + what if what else "")
            out.append({"what": label, "answer": text if where else str(d)})
    return out


def session(gdir, t, fm, qs):
    sdir = Path(gdir) / check.SESSIONS / t["id"]
    data = score.data_version(t)
    fps_cap, designed_for = fm.get("fps_cap"), fm.get("designed_for", "pad")
    import catalog   # noqa: E402 (catalog imports technical, which imports this)
    r = score.combine([t], fps_cap, data, designed_for)
    det = catalog.details(r, fps_cap)
    det.update(r.get("details") or {})
    n = t.get("numbers", {})
    if "shot_points" in n and data == 1:
        det["picture"] = "%d of %d capture points differ from the PC" % (n["shot_differing"], n["shot_points"])
    aspects = [{"key": k, "name": score.ALL_ASPECT_NAMES[k], "weight": score.WEIGHTS_OF[data][k],
                "score": catalog.rnd(r["aspects"].get(k)), "detail": det.get(k)} for k in score.ASPECTS[data]]
    walnut, pkg, ver = catalog.tested_version(t, catalog.recorded_packages(fm))
    answers, notes = form_answers(t, sdir, qs)
    sound = {k: n[k] for k in ("sound_dropouts", "sound_dropouts_per_min", "sound_dropouts_at_shots", "xruns",
                                "sound_faults", "sound_faults_per_min", "snd_stall") if k in n}
    rv = t.get("review") or {}
    return {
        "id": t["id"], "date": t["date"], "launcher": t.get("launcher"), "variant": t.get("variant"),
        "source": t.get("source", "ours"), "tester": t.get("tester"),
        "walnut": walnut, "package": pkg, "version": ver,
        "rules": "%d.%d" % (data, score.FORMULA[data]), "rules_label": score.rules_label(data),
        "rules_anchor": score.RULES[data]["anchor"],
        "minutes": round((t.get("play_seconds") or 0) / 60, 1), "end": t.get("end"), "crashed": t.get("crashed", False),
        "render": t.get("render"), "medal": r["medal"], "score": catalog.rnd(r["score"]), "reason": r.get("reason"),
        "aspects": aspects, "sound": sound, "answers": answers, "notes": notes,
        "picture": (t.get("picture") or {}).get("note"), "review": review_facts(rv),
        "shots": ["%s/%s/%s" % (check.SESSIONS, t["id"], f) for f in t.get("shots", {}).get("ps3", [])],
        "builds": builds(t, raw_versions(sdir), catalog.recorded_packages(fm)),
        "numbers": {k: n[k] for k in CSV_NUMBERS if k in n},
    }


def game_sessions(gdir, repo=REPO):
    """the game's played sessions in detail, newest first"""
    gdir = Path(gdir)
    fm = check.game_facts(gdir)
    qs = questions(repo)
    tdir = gdir / check.SESSIONS
    out = []
    for d in sorted(tdir.iterdir()) if tdir.is_dir() else []:
        if not (d / "result.toml").is_file():
            continue
        t = tomllib.loads((d / "result.toml").read_text())
        if t.get("kind") != "session":
            continue
        t["id"] = d.name
        out.append(session(gdir, t, fm, qs))
    out.sort(key=lambda s: (s["date"], s["id"]), reverse=True)
    return out


def to_csv(sessions):
    """the sessions as CSV text: one row per session, every aspect a column (empty: not tested)"""
    keys = list(dict.fromkeys(a["key"] for s in sessions for a in s["aspects"]))
    names = {a["key"]: a["name"] for s in sessions for a in s["aspects"]}
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["session", "date", "walnut", "package", "version", "launcher", "rules", "play_min", "end",
                "crashed", "medal", "score"] + [names[k] for k in keys] + CSV_NUMBERS)
    for s in sessions:
        sc = {a["key"]: a["score"] for a in s["aspects"]}
        w.writerow([s["id"], s["date"], s["walnut"], s["package"] or "", s["version"] or "", s["launcher"] or "",
                    s["rules"], s["minutes"], s["end"] or "", "yes" if s["crashed"] else "no", s["medal"],
                    "" if s["score"] is None else s["score"]]
                   + ["" if sc.get(k) is None else sc[k] for k in keys]
                   + [s["numbers"].get(k, "") for k in CSV_NUMBERS])
    return buf.getvalue()
