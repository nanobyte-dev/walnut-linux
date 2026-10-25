#!/usr/bin/env python3
"""session-form.py RUN_DIR [GAME VARIANT]: the end-of-session form of a played session
(session.sh, docs/games/SUITE.md "Played sessions"). It asks only what the recorder can't measure, the
things the player saw anyway in normal play: one tap each, most with a default, then the notes, under a
minute. A Tk dialog in the Walnut palette with large type for the TV (python3-tk); without a display or
tkinter it asks on the terminal (/dev/tty, else stdin).

  session.toml   sound = "yes" | "partly" | "no"                  (no default)
                 music = "yes" | "no" | "none"                    (none: the game has no music; no default)
                 glitches = "none" | "minor" | "broken"           (default none: didn't notice any)
                 played_with = "pad" | "keyboard" | "both"        (default pad)
                 pad_menus = "yes" | "no" | "na"                  (the menus with the pad; default yes)
                 menus_modes = "yes" | "no"                       (the menus and modes opened worked; default yes)
                 saves_load = "yes" | "no" | "na"                 (default na: didn't try)
                 controls = "full" | "partial" | "keyboard-mouse" (derived, for the old badge and old readers)
                 notes = "..."                                    (one string, \\n for line breaks)
                 form_version = 2, saved = "2026-10-26T18:04:11Z", game, variant, form = "tk" | "terminal"

form_version 2 makes the importer file the session under rules 2.x (tools/gamedb/import-run.py); the
first form (sound, music, controls, notes) had none. Closing the window without Save writes nothing (the
run directory has no session.toml; run this script again on it). Exit status: 0 saved, 1 not saved, 2 usage.
Test mode: WALNUT_FORM_AUTO="sound=yes,music=none,glitches=minor,notes=text" (key=value pairs, notes last;
unset keys keep their defaults) fills the form and presses Save without showing a window (or, with
--terminal, without asking).
"""
import datetime
import os
import re
import sys

BG, PANEL, ACCENT, TEXT = "#14304d", "#0e2238", "#d49a5c", "#ececec"
MUTED = "#9fb3c8"
FORM_VERSION = 2

# (key, question, [(value, label)], default or None: must be answered)
QUESTIONS = [
    ("sound", "Did you hear the sound effects?",
     [("yes", "Yes"), ("partly", "Partly"), ("no", "No")], None),
    ("music", "Did you hear the music?",
     [("yes", "Yes"), ("no", "No"), ("none", "No music in this game")], None),
    ("glitches", "Visual glitches?",
     [("none", "Didn't notice any"), ("minor", "Minor"), ("broken", "Broken")], "none"),
    ("played_with", "You played with",
     [("pad", "The pad"), ("keyboard", "Keyboard/mouse"), ("both", "Both")], "pad"),
    ("pad_menus", "The menus with the pad",
     [("yes", "Worked"), ("no", "Didn't"), ("na", "Didn't try")], "yes"),
    ("menus_modes", "Menus and modes you opened",
     [("yes", "Worked"), ("no", "Something didn't")], "yes"),
    ("saves_load", "Saving and loading",
     [("yes", "Worked"), ("no", "Didn't"), ("na", "Didn't try")], "na"),
]


def derived_controls(a):
    """the first form's controls answer from the new ones (the old controller badge)"""
    if a.get("played_with") == "keyboard":
        return "keyboard-mouse"
    return "full" if a.get("pad_menus") == "yes" else "partial"


def toml_str(s):
    """A TOML basic string: backslash, quote and control characters escaped."""
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\u%04x" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def save(run_dir, answers, notes, game, variant, form):
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines = ["# the end-of-session form (games/bench/session-form.py)", "form_version = %d" % FORM_VERSION]
    for key, _, _, _ in QUESTIONS:
        lines.append("%s = %s" % (key, toml_str(answers[key])))
    lines.append("controls = %s" % toml_str(derived_controls(answers)))
    lines.append("notes = %s" % toml_str(notes.strip()))
    lines.append("saved = %s" % toml_str(now))
    lines.append("form = %s" % toml_str(form))
    if game:
        lines.append("game = %s" % toml_str(game))
    if variant:
        lines.append("variant = %s" % toml_str(variant))
    path = os.path.join(run_dir, "session.toml")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, path)
    return path


def play_line(run_dir):
    """'12 min of play, ended: quit' from the run's bench.txt, or ''."""
    kv = {}
    try:
        with open(os.path.join(run_dir, "bench.txt"), encoding="utf-8", errors="replace") as f:
            for line in f:
                k, _, v = line.rstrip("\n").partition("=")
                kv[k] = v
    except OSError:
        return ""
    try:
        mins = float(kv.get("play_seconds") or kv.get("seconds") or "") / 60
    except ValueError:
        return ""
    s = "%.0f min played" % mins
    if kv.get("ended"):
        s += ", ended: " + kv["ended"]
    return s


def auto_answers():
    """WALNUT_FORM_AUTO for tests: key=value pairs separated by commas (notes last); the defaults for the rest"""
    spec = os.environ.get("WALNUT_FORM_AUTO")
    if not spec:
        return None
    notes = ""
    m = re.search(r"(?:^|,)notes=(.*)$", spec, re.S)
    if m:
        notes, spec = m.group(1), spec[:m.start()]
    d = {k: dflt for k, _, _, dflt in QUESTIONS}
    for part in filter(None, spec.split(",")):
        k, eq, v = part.partition("=")
        if not eq:
            sys.exit("WALNUT_FORM_AUTO: key=value pairs, notes last")
        if k in d:
            d[k] = v
    d["notes"] = notes.replace("\\n", "\n")
    return d


def form_tk(run_dir, game, variant):
    import tkinter as tk
    from tkinter import font as tkfont

    auto = auto_answers()
    root = tk.Tk()
    root.title("Walnut: how was the session?")
    root.configure(bg=BG)
    if auto:
        root.withdraw()
    else:
        sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
        w, h = min(1500, sw - 80), min(980, sh - 80)
        root.geometry("%dx%d+%d+%d" % (w, h, (sw - w) // 2, (sh - h) // 2))
    # large type on the TV; scaled down on a small screen (the PC)
    scale = 1.0 if root.winfo_screenheight() >= 1000 else 0.7
    fam = "DejaVu Sans"
    f_title = tkfont.Font(family=fam, size=int(26 * scale), weight="bold")
    f_big = tkfont.Font(family=fam, size=int(19 * scale))
    f_btn = tkfont.Font(family=fam, size=int(16 * scale), weight="bold")
    f_small = tkfont.Font(family=fam, size=int(14 * scale))

    result = {"saved": False}
    vars_ = {key: tk.StringVar(value=dflt or "") for key, _, _, dflt in QUESTIONS}

    head = tk.Frame(root, bg=BG)
    head.pack(fill="x", padx=40, pady=(20, 6))
    tk.Label(head, text="How was the session?", bg=BG, fg=ACCENT, font=f_title).pack(anchor="w")
    sub = " ".join(x for x in [game, variant if variant and variant != "base" else "",
                               ("(" + play_line(run_dir) + ")") if play_line(run_dir) else ""] if x)
    tk.Label(head, text=(sub + "  " if sub else "") + "The rest is measured: just what you noticed.",
             bg=BG, fg=MUTED, font=f_small).pack(anchor="w")

    body = tk.Frame(root, bg=PANEL)
    body.pack(fill="both", expand=True, padx=40, pady=6)
    body.columnconfigure(1, weight=1)
    buttons = {}
    for i, (key, question, options, _) in enumerate(QUESTIONS):
        tk.Label(body, text=question, bg=PANEL, fg=TEXT, font=f_big, anchor="w").grid(
            row=i, column=0, sticky="w", padx=(24, 18), pady=(12 if i == 0 else 6, 0))
        line = tk.Frame(body, bg=PANEL)
        line.grid(row=i, column=1, sticky="w", pady=(12 if i == 0 else 6, 0))
        for value, label in options:
            b = tk.Radiobutton(
                line, text=label, value=value, variable=vars_[key], indicatoron=0,
                bg=BG, fg=TEXT, selectcolor=ACCENT, activebackground=ACCENT,
                activeforeground=BG, font=f_btn, relief="flat", bd=0, padx=18, pady=7,
                highlightthickness=0, command=lambda: err.set(""))
            b.pack(side="left", padx=(0, 10))
            buttons[(key, value)] = b

        # the selected button turns accent coloured with dark text
        def paint(*_a, k=key, opts=options):
            for value, _ in opts:
                on = vars_[k].get() == value
                buttons[(k, value)].configure(fg=BG if on else TEXT)
        vars_[key].trace_add("write", paint)
        paint()

    n = len(QUESTIONS)
    tk.Label(body, text="Notes", bg=PANEL, fg=TEXT, font=f_big).grid(row=n, column=0, sticky="nw", padx=(24, 18),
                                                                     pady=(16, 0))
    tk.Label(body, text="What didn't work, a glitch and where, odd sound, anything the numbers won't show.",
             bg=PANEL, fg=MUTED, font=f_small).grid(row=n, column=1, sticky="w", pady=(16, 0))
    notes = tk.Text(body, height=4, wrap="word", bg=BG, fg=TEXT, insertbackground=ACCENT,
                    font=f_big, relief="flat", bd=0, padx=12, pady=8, highlightthickness=2,
                    highlightbackground=BG, highlightcolor=ACCENT)
    notes.grid(row=n + 1, column=0, columnspan=2, sticky="nsew", padx=24, pady=(6, 16))
    body.rowconfigure(n + 1, weight=1)

    foot = tk.Frame(root, bg=BG)
    foot.pack(fill="x", padx=40, pady=(4, 20))
    err = tk.StringVar(value="")
    tk.Label(foot, textvariable=err, bg=BG, fg=ACCENT, font=f_small).pack(side="left")

    def do_save(_event=None):
        answers = {k: v.get() for k, v in vars_.items()}
        if auto:
            answers = {k: auto[k] for k in answers}
        missing = [q for k, q, _, _ in QUESTIONS if not answers[k]]
        if missing:
            err.set("Please answer: " + "; ".join(missing))
            return
        text = auto["notes"] if auto else notes.get("1.0", "end")
        result["path"] = save(run_dir, answers, text, game, variant, "tk")
        result["saved"] = True
        root.destroy()

    tk.Button(foot, text="Save", command=do_save, bg=ACCENT, fg=BG, activebackground=TEXT,
              activeforeground=BG, font=f_btn, relief="flat", bd=0, padx=48, pady=10,
              highlightthickness=0).pack(side="right")
    tk.Button(foot, text="Skip", command=root.destroy, bg=PANEL, fg=MUTED, activebackground=BG,
              activeforeground=TEXT, font=f_small, relief="flat", bd=0, padx=24, pady=10,
              highlightthickness=0).pack(side="right", padx=16)
    if auto:
        root.update()
        do_save()
    else:
        root.lift()
        root.attributes("-topmost", True)
        root.after(500, lambda: root.attributes("-topmost", False))
        root.after(100, notes.focus_set)
        root.mainloop()
    return result.get("path")


def form_terminal(run_dir, game, variant):
    auto = auto_answers()
    tty = None
    if not auto:
        try:
            tty = open("/dev/tty", "r+")
        except OSError:
            tty = None
    inp = tty if tty else sys.stdin
    out = tty if tty else sys.stderr

    def ask(prompt):
        out.write(prompt)
        out.flush()
        line = inp.readline()
        if line == "":
            raise EOFError
        return line.rstrip("\n")

    answers = {}
    try:
        if auto:
            answers = {k: auto[k] for k, _, _, _ in QUESTIONS}
            notes = auto["notes"]
        else:
            out.write("\nHow was the session? %s %s (Enter takes the [default])\n" % (game or "", play_line(run_dir)))
            for key, question, options, dflt in QUESTIONS:
                while True:
                    menu = "  ".join(("[%d) %s]" if v == dflt else "%d) %s") % (i + 1, lab)
                                     for i, (v, lab) in enumerate(options))
                    a = ask("%s\n  %s\n  > " % (question, menu)).strip().lower()
                    pick = dflt if not a else None
                    if a.isdigit() and 1 <= int(a) <= len(options):
                        pick = options[int(a) - 1][0]
                    elif a:
                        for value, lab in options:
                            if a == value or lab.lower().startswith(a):
                                pick = value
                                break
                    if pick:
                        answers[key] = pick
                        break
                    out.write("  one of the numbers above\n")
            out.write("Notes (an empty line ends them):\n")
            lines = []
            while True:
                s = ask("  ")
                if s == "":
                    break
                lines.append(s)
            notes = "\n".join(lines)
    except (EOFError, KeyboardInterrupt):
        return None
    return save(run_dir, answers, notes, game, variant, "terminal")


def main(argv):
    args = [a for a in argv[1:] if a != "--terminal"]
    force_terminal = "--terminal" in argv[1:]
    if not args or not os.path.isdir(args[0]):
        sys.stderr.write(__doc__.split("\n\n")[0] + "\n")
        return 2
    run_dir = args[0]
    game = args[1] if len(args) > 1 else ""
    variant = args[2] if len(args) > 2 else ""
    path = None
    if not force_terminal and os.environ.get("DISPLAY"):
        try:
            path = form_tk(run_dir, game, variant)
            if path is None:
                return 1  # the window was closed or Skip: nothing to fall back to
        except Exception as e:  # no tkinter, no X: ask on the terminal
            sys.stderr.write("session-form: no Tk dialog (%s: %s); asking here\n" % (type(e).__name__, e))
            path = None
            if isinstance(e, KeyboardInterrupt):
                return 1
            path = form_terminal(run_dir, game, variant)
    else:
        path = form_terminal(run_dir, game, variant)
    if path:
        print("session-form: saved " + path)
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
