#!/usr/bin/env python3
"""session-form.py RUN_DIR [GAME VARIANT]: the end-of-session form of a played session
(session.sh, docs/games/SUITE.md "Played sessions"). Asks whether the sound and the music were
heard, how the controls worked and for free-text notes about the gameplay, and writes
RUN_DIR/session.toml. A Tk dialog in the Walnut palette with large type for the TV (python3-tk);
without a display or tkinter it asks on the terminal (/dev/tty, else stdin).

  session.toml   sound = "yes" | "no" | "partly"
                 music = "yes" | "no" | "none"          (none: the game has no music)
                 controls = "full" | "partial" | "keyboard-mouse"
                 notes = "..."                          (one string, \\n for line breaks)
                 saved = "2026-10-23T18:04:11Z", game, variant, form = "tk" | "terminal"

Closing the window without Save writes nothing (the run directory has no session.toml; run this
script again on it). Exit status: 0 saved, 1 not saved, 2 usage.
Test mode: WALNUT_FORM_AUTO="sound=yes,music=none,controls=full,notes=text" fills the form and
presses Save without showing a window (or, with --terminal, without asking).
"""
import datetime
import os
import re
import sys

BG, PANEL, ACCENT, TEXT = "#14304d", "#0e2238", "#d49a5c", "#ececec"
MUTED = "#9fb3c8"

QUESTIONS = [
    ("sound", "Did you hear the sound?",
     [("yes", "Yes"), ("partly", "Partly"), ("no", "No")]),
    ("music", "Did you hear the music?",
     [("yes", "Yes"), ("no", "No"), ("none", "No music in this game")]),
    ("controls", "How did the controls work?",
     [("full", "Full controller"), ("partial", "Partial"),
      ("keyboard-mouse", "Keyboard and mouse only")]),
]


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
    lines = ["# the end-of-session form (games/bench/session-form.py)"]
    for key, _, _ in QUESTIONS:
        lines.append("%s = %s" % (key, toml_str(answers[key])))
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
    """WALNUT_FORM_AUTO for tests: key=value pairs separated by commas (notes last)."""
    spec = os.environ.get("WALNUT_FORM_AUTO")
    if not spec:
        return None
    d = {}
    m = re.match(r"sound=([^,]*),music=([^,]*),controls=([^,]*)(?:,notes=(.*))?$", spec, re.S)
    if not m:
        sys.exit("WALNUT_FORM_AUTO: sound=..,music=..,controls=..,notes=..")
    d["sound"], d["music"], d["controls"] = m.group(1), m.group(2), m.group(3)
    d["notes"] = (m.group(4) or "").replace("\\n", "\n")
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
        w, h = min(1500, sw - 80), min(1000, sh - 80)
        root.geometry("%dx%d+%d+%d" % (w, h, (sw - w) // 2, (sh - h) // 2))
        # large type on the TV; scaled down on a small screen (the PC)
    scale = 1.0 if root.winfo_screenheight() >= 1000 else 0.7
    fam = "DejaVu Sans"
    f_title = tkfont.Font(family=fam, size=int(26 * scale), weight="bold")
    f_big = tkfont.Font(family=fam, size=int(20 * scale))
    f_btn = tkfont.Font(family=fam, size=int(18 * scale), weight="bold")
    f_small = tkfont.Font(family=fam, size=int(14 * scale))

    result = {"saved": False}
    vars_ = {key: tk.StringVar(value="") for key, _, _ in QUESTIONS}

    head = tk.Frame(root, bg=BG)
    head.pack(fill="x", padx=40, pady=(24, 8))
    tk.Label(head, text="How was the session?", bg=BG, fg=ACCENT, font=f_title).pack(anchor="w")
    sub = " ".join(x for x in [game, variant if variant and variant != "base" else "",
                               ("(" + play_line(run_dir) + ")") if play_line(run_dir) else ""] if x)
    if sub:
        tk.Label(head, text=sub, bg=BG, fg=MUTED, font=f_small).pack(anchor="w")

    body = tk.Frame(root, bg=PANEL)
    body.pack(fill="both", expand=True, padx=40, pady=8)
    buttons = {}
    for key, question, options in QUESTIONS:
        row = tk.Frame(body, bg=PANEL)
        row.pack(fill="x", padx=24, pady=(14, 2))
        tk.Label(row, text=question, bg=PANEL, fg=TEXT, font=f_big).pack(anchor="w")
        line = tk.Frame(row, bg=PANEL)
        line.pack(fill="x", pady=(6, 0))
        for value, label in options:
            b = tk.Radiobutton(
                line, text=label, value=value, variable=vars_[key], indicatoron=0,
                bg=BG, fg=TEXT, selectcolor=ACCENT, activebackground=ACCENT,
                activeforeground=BG, font=f_btn, relief="flat", bd=0, padx=22, pady=10,
                highlightthickness=0, command=lambda: err.set(""))
            b.pack(side="left", padx=(0, 12))
            buttons[(key, value)] = b

        # the selected button turns accent coloured with dark text
        def paint(*_a, k=key, opts=options):
            for value, _ in opts:
                on = vars_[k].get() == value
                buttons[(k, value)].configure(fg=BG if on else TEXT)
        vars_[key].trace_add("write", paint)

    tk.Label(body, text="Notes about the gameplay", bg=PANEL, fg=TEXT, font=f_big).pack(
        anchor="w", padx=24, pady=(18, 2))
    tk.Label(body, text="Glitches, a level that dragged, odd sound, awkward controls, anything the "
             "numbers will not show.", bg=PANEL, fg=MUTED, font=f_small).pack(anchor="w", padx=24)
    notes = tk.Text(body, height=7, wrap="word", bg=BG, fg=TEXT, insertbackground=ACCENT,
                    font=f_big, relief="flat", bd=0, padx=12, pady=10, highlightthickness=2,
                    highlightbackground=BG, highlightcolor=ACCENT)
    notes.pack(fill="both", expand=True, padx=24, pady=(6, 18))

    foot = tk.Frame(root, bg=BG)
    foot.pack(fill="x", padx=40, pady=(4, 24))
    err = tk.StringVar(value="")
    tk.Label(foot, textvariable=err, bg=BG, fg=ACCENT, font=f_small).pack(side="left")

    def do_save(_event=None):
        answers = {k: v.get() for k, v in vars_.items()}
        if auto:
            answers = {k: auto[k] for k in answers}
        missing = [q for k, q, _ in QUESTIONS if not answers[k]]
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
            answers = {k: auto[k] for k, _, _ in QUESTIONS}
            notes = auto["notes"]
        else:
            out.write("\nHow was the session? %s %s\n" % (game or "", play_line(run_dir)))
            for key, question, options in QUESTIONS:
                while True:
                    menu = "  ".join("%d) %s" % (i + 1, lab) for i, (_, lab) in enumerate(options))
                    a = ask("%s\n  %s\n  > " % (question, menu)).strip().lower()
                    pick = None
                    if a.isdigit() and 1 <= int(a) <= len(options):
                        pick = options[int(a) - 1][0]
                    else:
                        for value, lab in options:
                            if a and (a == value or lab.lower().startswith(a)):
                                pick = value
                                break
                    if pick:
                        answers[key] = pick
                        break
                    out.write("  one of the numbers above\n")
            out.write("Notes about the gameplay (an empty line ends them):\n")
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
