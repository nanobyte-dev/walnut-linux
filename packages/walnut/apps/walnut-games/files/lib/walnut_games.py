"""walnut_games.py (walnut-games package): the Walnut Games desktop app, run by /usr/bin/walnut-games.

Design
- A plain Tk window for mouse and keyboard: a top bar (search, category, medal, sort, "installed
  only"), the games as cards on a tk.Canvas, and a details pane on the right.
  Stdlib and tkinter; PIL only to show the JPEG screenshots (Tk itself reads PNG, the thumbnails).
- The catalog is the one packaged in /usr/share/walnut-games/ (catalog.json, games/<id>/thumb.png
  and the screenshots): the same list the root helper installs from, so the app works offline.
  WALNUT_GAMES_DATA points the app at another catalog directory.
- Fresh ratings: the window opens at once with the packaged catalog, or the last web copy cached in
  ~/.cache/walnut-games/catalog.json when that one is newer (choose_catalog), and a background thread
  fetches https://walnut.nanobyte.dev/gamedb/catalog.json (8 s timeout). A download is used only if
  validate_catalog accepts it (a bad one is dropped, what is shown stays) and it is newer (is_newer:
  the catalog's content_date, its newest test date, then its revision, a hash of the content); it is
  cached and the cards and details are redrawn in place (filters, search, selection, scroll and the
  screenshot shown are kept). Offline or failing: a line on stderr, nothing else. The Refresh button
  does the same fetch and also re-reads the installed state (dpkg-query) and the launchers; it is off
  while a refresh or an install runs. Thumbnails and screenshots missing from the package are fetched
  from the website once and cached (the card shows a placeholder meanwhile). WALNUT_GAMES_OFFLINE=1
  turns the network off.
- Install maps ids to packages in the root helper from the packaged, root-owned catalog only, so a
  game that exists only in the web catalog shows Install off, with the reason, until the next package
  update. Play and Remove work for any installed game.
- Installed state comes from dpkg-query (no root). Install and Remove run
  `sudo -n /usr/lib/ps3/sbin/walnut-games-helper install|remove ID` (it takes game ids only), its
  "progress N text" lines drive the progress bar, and afterwards the menu entries are made again
  (ps3-game-menu "$XDG_RUNTIME_DIR/ps3-game-menu"), so the new game starts through game mode. Play
  is off while the helper runs: game mode would freeze it with dpkg's lock held.
- Play starts the runtime copy of the game's .desktop (its Exec already goes through ps3-game),
  else `ps3-game` plus the entry in /usr/share/applications with the field codes cut out and
  split by shlex, never through a shell.
- The pure parts (catalog, filters, Exec parsing, colours, helper output) are plain functions, tested
  on a PC without a display by packages/walnut/apps/walnut-games/tests/test_walnut_games.py; the window is the App class.
"""
import json
import os
import re
import queue
import shlex
import subprocess
import sys
import threading
import time
import urllib.request
import webbrowser

try:
    import tkinter as tk
    from tkinter import font as tkfont
    from tkinter import messagebox, ttk
except ImportError:   # the tests of the pure functions need no Tk
    tk = tkfont = messagebox = ttk = None

DATA = os.environ.get("WALNUT_GAMES_DATA") or "/usr/share/walnut-games"
WEB = "https://walnut.nanobyte.dev/gamedb/"
HELPER = "/usr/lib/ps3/sbin/walnut-games-helper"
MENU_TOOL = "/usr/lib/ps3/desktop/ps3-game-menu"
APPS = "/usr/share/applications"

# the Walnut palette (the website's)
BG, PANEL, BORDER = "#14304d", "#0e2238", "#1d3e61"
ACCENT, ACCENT_DEEP, ACCENT_LIGHT = "#d49a5c", "#b5763a", "#e3ae73"
TEXT, MUTED, ENTRY = "#ececec", "#c9a87c", "#091726"
CARD_HOVER = "#12304f"

MEDAL_ORDER = ["Platinum", "Gold", "Silver", "Bronze", "Tin", "Rust", "Scrap", "Untested"]
OLD_MEDALS = {"Tin"}    # rules 1's scale only (rules 2.1 has none): in the filter only when a game has it
MEDAL_COLORS = {"Platinum": "#e5e4e2", "Gold": "#d4af37", "Silver": "#c0c0c0", "Bronze": "#cd7f32",
                "Tin": "#9a9ea3", "Rust": "#8a4b32", "Scrap": "#5f5a55", "Untested": "#6f7a86"}
DETAILS_W = 372     # the details pane's content width
QUICK_EXIT_S = 20   # a game that quits sooner than this gets a note in the status line
ASPECT_TOP = 22     # the caption line above the aspect rows
ASPECT_GAP = 8      # space between two aspects' blocks
ASPECT_ORDER = ["frame", "smooth", "sound", "picture", "stability", "display", "load"]


# ---- pure functions ---------------------------------------------------------------------------

def web_url(gid=None, technical=False):
    """the site's page of game gid (its technical page with technical), the technical index or the
    home page without a game"""
    if not gid:
        return WEB + ("technical/" if technical else "")
    return WEB + "games/%s/%s" % (gid, "technical/" if technical else "")


def load_catalog_obj(data=None):
    with open(os.path.join(data or DATA, "catalog.json"), encoding="utf-8") as f:
        return json.load(f)


def load_catalog(data=None):
    return load_catalog_obj(data)["games"]


ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
SHOT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*(/[A-Za-z0-9][A-Za-z0-9._-]*)*$")
MAX_CATALOG = 16 << 20


def validate_catalog(obj):
    """a downloaded catalog as a dict, or ValueError: it must have the games list and the fields the
    app reads (id, title, packages, a rating with a medal; the lists and dicts it iterates over must
    be of the right type), and the ids and picture paths must be safe to use in a URL and a cache path"""
    def need(ok, what):
        if not ok:
            raise ValueError(what)

    need(isinstance(obj, dict) and isinstance(obj.get("games"), list) and obj["games"], "no games list")
    seen = set()
    for g in obj["games"]:
        need(isinstance(g, dict), "a game is not an object")
        gid = g.get("id")
        need(isinstance(gid, str) and ID_RE.match(gid) and gid not in seen, "bad or repeated id %r" % (gid,))
        seen.add(gid)
        need(isinstance(g.get("title"), str) and g["title"], "%s: no title" % gid)
        need(isinstance(g.get("packages"), list) and g["packages"] and
             all(isinstance(p, str) and p for p in g["packages"]), "%s: bad packages" % gid)
        need(isinstance(g.get("rating"), dict) and isinstance(g["rating"].get("medal"), str),
             "%s: no rating" % gid)
        need(isinstance(g["rating"].get("aspects", {}), dict), "%s: bad aspects" % gid)
        need(all(isinstance(k, str) for k in g["rating"].get("order") or []) and
             isinstance(g["rating"].get("order") or [], list), "%s: bad aspect order" % gid)
        for k in ("summary", "controller", "medal_note"):
            need(g.get(k) is None or isinstance(g[k], str), "%s: bad %s" % (gid, k))
        for k in ("categories", "tags", "known_issues"):
            need(all(isinstance(x, str) for x in g.get(k) or []) and isinstance(g.get(k) or [], list),
                 "%s: bad %s" % (gid, k))
        for k in ("patches", "launchers", "tests"):
            need(isinstance(g.get(k) or [], list) and all(isinstance(x, dict) for x in g.get(k) or []),
                 "%s: bad %s" % (gid, k))
        need(all(isinstance(p.get("subject"), str) for p in g.get("patches") or []), "%s: bad patches" % gid)
        for l in g.get("launchers") or []:
            need(isinstance(l.get("rating"), dict) and isinstance(l["rating"].get("medal"), str),
                 "%s: bad launcher" % gid)
        for t in g.get("tests") or []:
            shots = t.get("ps3") or []
            need(isinstance(shots, list) and all(isinstance(f, str) and SHOT_RE.match(f) and ".." not in f.split("/")
                                                 for f in shots), "%s: bad picture path" % gid)
    return obj


def content_stamp(cat):
    """(content date, revision) of a catalog: tools/gamedb/catalog.py's content_date (the newest test
    date) and revision (a hash of the games); a catalog from before those fields gets the newest
    test date and no revision"""
    date = cat.get("content_date")
    if not isinstance(date, str):
        date = max((str(t.get("date") or "") for g in cat.get("games", []) for t in g.get("tests") or []),
                   default="")
    rev = cat.get("revision")
    return date, rev if isinstance(rev, str) else ""


def is_newer(new, cur):
    """is catalog `new` newer than `cur`? By the newest test date; on the same date a different
    revision counts as newer (the website is the current one), the same revision does not"""
    (dn, rn), (dc, rc) = content_stamp(new), content_stamp(cur)
    if dn != dc:
        return dn > dc
    return bool(rn) and rn != rc


def choose_catalog(packaged, cached):
    """the catalog to start with: the cached web copy if it is valid and newer than the packaged one"""
    if cached is not None:
        try:
            if is_newer(validate_catalog(cached), packaged):
                return cached
        except ValueError:
            pass
    return packaged


def cache_dir():
    return os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "walnut-games")


def read_cached_catalog(path=None):
    """the cached web catalog, or None (missing, unreadable or invalid)"""
    try:
        with open(path or os.path.join(cache_dir(), "catalog.json"), encoding="utf-8") as f:
            return validate_catalog(json.load(f))
    except (OSError, ValueError):
        return None


def medal_of(game):
    return game.get("rating", {}).get("medal") or "Untested"


def score_of(game):
    return game.get("rating", {}).get("score")


def pill_text_color(fill):
    """dark text on light fills, light on dark ones"""
    r, g, b = (int(fill[i:i + 2], 16) for i in (1, 3, 5))
    return "#14304d" if 0.299 * r + 0.587 * g + 0.114 * b > 110 else "#f4f4f4"


def dpkg_installed(packages, runner=None):
    """the subset of packages dpkg says is installed (ii), from one dpkg-query call"""
    if not packages:
        return set()
    cmd = ["dpkg-query", "-W", "-f", "${Package}\t${db:Status-Abbrev}\n"] + sorted(packages)
    try:
        out = (runner or (lambda c: subprocess.run(c, capture_output=True, text=True).stdout))(cmd)
    except OSError:
        return set()
    have = set()
    for line in out.splitlines():
        name, _, st = line.partition("\t")
        if st[1:2] == "i":
            have.add(name)
    return have


def is_installed(game, have):
    return all(p in have for p in game["packages"])


def filter_games(games, have, text="", category="All", medal="All", installed_only=False,
                 sort="Best first"):
    words = text.lower().split()
    out = []
    for g in games:
        hay = " ".join([g["title"], g.get("summary", ""), " ".join(g.get("categories", [])),
                        " ".join(g.get("tags", []))]).lower()
        if any(w not in hay for w in words):
            continue
        if category != "All" and category not in g.get("categories", []):
            continue
        if medal != "All" and medal_of(g) != medal:
            continue
        if installed_only and not is_installed(g, have):
            continue
        out.append(g)
    if sort == "Name":
        out.sort(key=lambda g: g["title"].lower())
    else:
        out.sort(key=lambda g: (score_of(g) is None, -(score_of(g) or 0), g["title"].lower()))
    return out


def basis_text(rating):
    """what a rating rests on and the rules it was rated under: "1 session, 9 min of play; rules 2.0 (2026-10-26)";
    "" for an older catalog"""
    b, parts = rating.get("basis") or {}, []
    if b.get("tests") and rating.get("graded_by") in ("session", "demo"):
        kind = "session" if rating["graded_by"] == "session" else "run"
        parts.append("%d %s%s, %d min of play" % (b["tests"], kind, "" if b["tests"] == 1 else "s", b.get("minutes", 0)))
    if rating.get("rules_label"):
        parts.append("rated under " + rating["rules_label"])
    return "; ".join(parts)


def step_parts(step):
    """a data step split at its backticks: [(text, is_code), ...]"""
    return [(t, i % 2 == 1) for i, t in enumerate(step.split("`")) if t]


def medal_choices(games):
    """the medal filter's entries: MEDAL_ORDER, without a medal of an old rules scale (Tin) no game has"""
    have = {medal_of(g) for g in games}
    return [m for m in MEDAL_ORDER if m not in OLD_MEDALS or m in have]


def problem_lines(rating):
    """why a game is Scrap (rules 2.1: it can't be played), rules 2's broken core subsystems (they make a playable
    game Rust whatever its mean), then its defects and failed checks, each with where it comes from ("also on the
    PC", "PS3/Walnut only")"""
    out = ["Scrap: " + rating["reason"]] if rating.get("medal") == "Scrap" and rating.get("reason") else []
    if rating.get("broken"):
        out.append(("Rust: " if rating.get("medal") == "Rust" else "Broken: ") + "; ".join(rating["broken"]))
    for d in rating.get("defects") or []:
        out.append("Picture: %s%s, -%d%s" % (d.get("name", ""), " (%s)" % d["what"] if d.get("what") else "",
                                             d.get("points", 0), " [%s]" % d["origin"] if d.get("origin") else ""))
    for key, word in (("controls_failed", "Controls: not "), ("features_failed", "Broken: ")):
        for f in rating.get(key) or []:
            out.append("%s%s%s%s" % (word, f.get("name", ""), " (%s)" % f["what"] if f.get("what") else "",
                                     " [%s]" % f["origin"] if f.get("origin") else ""))
    return out


def launcher_label(launcher):
    """a launcher of the catalog ({name, rating}) as the app names it: "OpenArena (GL 1.2) · Gold 90" """
    r = launcher["rating"]
    name = launcher.get("name") or "Default launcher"
    if r.get("graded_by") in (None, "capture"):
        return name + " \u00b7 not played yet"
    return "%s \u00b7 %s%s" % (name, r["medal"], " %d" % r["score"] if r.get("score") is not None else "")


def order_launchers(cmds, launchers):
    """Play's launchers (play_commands) best rated first, in the catalog's order (best first), the
    ones it hasn't rated after them as found; with each one's medal in its label when rated"""
    rated = {l["name"]: (i, l) for i, l in enumerate(launchers or []) if l.get("name")}
    out = []
    for n, (label, argv, cwd) in enumerate(cmds):
        hit = rated.get(label)
        out.append((hit[0] if hit else len(rated) + n, launcher_label(hit[1]) if hit else label, argv, cwd))
    return [(label, argv, cwd) for _, label, argv, cwd in sorted(out, key=lambda x: x[0])]


def tested_text(rating):
    """which version the rating was measured on: "Tested on Walnut 0.1 with openarena 0.8.8 (2026-10-20)";
    "" for an untested game (or a catalog from before the versions were recorded)"""
    t = rating.get("tested")
    if not t:
        return ""
    w = "a build before Walnut 0.1" if t["walnut"] == "pre-0.1" else "Walnut " + t["walnut"]
    game = " with %s %s" % (t["package"], t["version"]) if t.get("version") else ""
    return "Tested on %s%s (%s)" % (w, game, t["date"])


def parse_exec(line):
    """the argv of a .desktop Exec= value: field codes (%f %U ...) cut out, %% kept as %, shlex rules"""
    args = []
    for a in shlex.split(line):
        a = a.replace("%%", "\0")
        if len(a) == 2 and a[0] == "%":
            continue                      # a whole argument that is a field code
        out, i = "", 0
        while i < len(a):
            if a[i] == "%" and i + 1 < len(a):
                i += 2                    # a code inside an argument (--file=%f)
                continue
            out += a[i]
            i += 1
        args.append(out.replace("\0", "%"))
    return args


def desktop_entry(path):
    """{Exec, Path, Terminal} of a .desktop file's [Desktop Entry] group; None when unreadable"""
    keys, inside = {}, False
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for raw in f:
                line = raw.strip()
                if line.startswith("["):
                    inside = line == "[Desktop Entry]"
                elif inside and "=" in line and not line.startswith("#"):
                    k, _, v = line.partition("=")
                    keys.setdefault(k.strip(), v.strip())
    except OSError:
        return None
    return keys if "Exec" in keys else None


def package_files(packages, runner=None):
    """the paths the installed packages ship (dpkg -L, their manifest); [] when dpkg can't say"""
    if not packages:
        return []
    cmd = ["dpkg-query", "-L"] + sorted(packages)
    try:
        out = (runner or (lambda c: subprocess.run(c, capture_output=True, text=True).stdout))(cmd)
    except OSError:
        return []
    return [l.strip() for l in out.splitlines() if l.strip().startswith("/")]


BIN_DIRS = ("/usr/games", "/usr/bin", "/usr/local/games")


def entry_program(argv):
    """the program an Exec runs: past ps3-game, env and its VAR=value words"""
    i = 0
    while i < len(argv) and (os.path.basename(argv[i]) in ("ps3-game", "env") or
                             (i and "=" in argv[i] and not argv[i].startswith("-"))):
        i += 1
    if i >= len(argv):
        return None
    return argv[i] if os.path.isabs(argv[i]) else os.path.basename(argv[i])   # a full path stays one


def play_commands(game, runtime_dir=None, apps=APPS, files=None):
    """[(label, argv, cwd)]: every way to start the game, found from its packages' manifest (files:
    a callable giving the paths they ship): the menu entries they ship (a diverted one isn't there
    and drops out), then any shown menu entry that runs one of their programs (Walnut's own entries
    for a game, like OpenArena's two renderers). The catalog's optional `desktop` list comes first
    (an order, or an entry the manifest can't lead to). Each entry's runtime copy (ps3-game-menu's,
    Exec through ps3-game) wins over the installed one, which gets ps3-game put in front. The label
    is the entry's Name."""
    dirs = ([os.path.join(runtime_dir, "applications")] if runtime_dir else []) + [apps]
    shipped = files() if files else []
    # the programs by name (on PATH) and by full path (an Exec of /usr/lib/yamagi-quake2/quake2)
    progs = {os.path.basename(f) for f in shipped if os.path.dirname(f) in BIN_DIRS} | set(shipped)
    names = list(game.get("desktop", []))
    names += sorted(os.path.basename(f) for f in shipped
                    if os.path.dirname(f) == apps and f.endswith(".desktop") and os.path.basename(f) not in names)
    out, seen = [], set()

    def add(name, e):
        argv = parse_exec(e["Exec"])
        if not argv or name in seen:
            return
        if os.path.basename(argv[0]) != "ps3-game":
            argv = ["ps3-game"] + argv
        out.append((e.get("Name") or name[:-len(".desktop")], argv, e.get("Path") or None))
        seen.add(name)

    for name in names:
        for d in dirs:
            e = desktop_entry(os.path.join(d, name))
            if e:
                add(name, e)
                break
    if progs:
        for d in dirs:
            try:
                found = sorted(n for n in os.listdir(d) if n.endswith(".desktop"))
            except OSError:
                continue
            for name in found:
                e = None if name in seen else desktop_entry(os.path.join(d, name))
                if e and e.get("NoDisplay") != "true" and e.get("Hidden") != "true" and \
                        entry_program(parse_exec(e["Exec"])) in progs:
                    add(name, e)
    return out


def parse_helper_line(line):
    """('progress', percent, text) | ('log', text) | ('error', text) | ('status', text) | None"""
    kind, _, rest = line.rstrip("\n").partition(" ")
    if kind == "progress":
        pct, _, text = rest.partition(" ")
        try:
            return ("progress", max(0, min(100, int(pct))), text)
        except ValueError:
            return None
    if kind in ("log", "error", "status"):
        return (kind, rest)
    return ("log", line.strip()) if line.strip() else None   # sudo's own messages


def screenshots(game):
    """[(label, relative path)]: every test's pictures, as rendered on the PS3"""
    out = []
    for t in game.get("tests", []):
        for f in t.get("ps3", []):
            out.append(("Tested %s%s" % (t.get("date", ""), ", " + t["launcher"] if t.get("launcher") else ""), f))
    return out


def rounded_points(x1, y1, x2, y2, r):
    """polygon points for create_polygon(smooth=True) of a rounded rectangle"""
    return [x1 + r, y1, x1 + r, y1, x2 - r, y1, x2 - r, y1, x2, y1, x2, y1 + r, x2, y1 + r, x2, y2 - r, x2, y2 - r,
            x2, y2, x2 - r, y2, x2 - r, y2, x1 + r, y2, x1 + r, y2, x1, y2, x1, y2 - r, x1, y2 - r, x1, y1 + r,
            x1, y1 + r, x1, y1]


# ---- the window -------------------------------------------------------------------------------

CARD_W, THUMB_W, THUMB_H, GAP = 264, 256, 144, 14
CARD_H = THUMB_H + 96
SHOT_W = 360


class App:
    def __init__(self, root, data=None, runtime_dir=None, helper=HELPER, online=None):
        self.root, self.data = root, data or DATA
        self.runtime_dir = runtime_dir if runtime_dir is not None else \
            (os.path.join(os.environ["XDG_RUNTIME_DIR"], "ps3-game-menu") if os.environ.get("XDG_RUNTIME_DIR") else None)
        self.helper = helper
        self.online = (not os.environ.get("WALNUT_GAMES_OFFLINE")) if online is None else online
        packaged = load_catalog_obj(self.data)
        self.pkg_ids = {g["id"] for g in packaged["games"]}   # what the root helper can install
        self.cat = choose_catalog(packaged, read_cached_catalog() if self.online else None)
        self.games = self.cat["games"]
        self.by_id = {g["id"]: g for g in self.games}
        self.refreshing = False
        self._fetching = set()
        self.have = set()
        self.shown, self.selected = [], None
        self.thumbs, self.cards = {}, {}
        self.busy = None                       # (id, "install"|"remove") while the helper runs
        self._manifests = {}                   # (game id, installed packages) -> dpkg -L
        self.events = queue.Queue()
        self.shot_i, self.shot_img = 0, None
        self.hover = None
        self.pil_ok = None
        self._style()
        self._build()
        self.refresh_installed()
        self.apply_filters()
        self.root.after(100, self._poll)
        if self.online:
            self.fetch_catalog(manual=False)

    # -- look
    def _style(self):
        r = self.root
        r.configure(bg=BG)
        fams = set(tkfont.families(r))
        fam = "DejaVu Sans" if "DejaVu Sans" in fams else tkfont.nametofont("TkDefaultFont").actual("family")
        self.f = {"base": tkfont.Font(family=fam, size=10), "small": tkfont.Font(family=fam, size=9),
                  "bold": tkfont.Font(family=fam, size=10, weight="bold"),
                  "card": tkfont.Font(family=fam, size=11, weight="bold"),
                  "pill": tkfont.Font(family=fam, size=9, weight="bold"),
                  "title": tkfont.Font(family=fam, size=17, weight="bold"),
                  "big": tkfont.Font(family=fam, size=13, weight="bold")}
        for n in ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont", "TkFixedFont"):
            try:
                tkfont.nametofont(n).configure(family=fam, size=10)
            except tk.TclError:
                pass
        s = ttk.Style(r)
        s.theme_use("clam")
        s.configure(".", background=BG, foreground=TEXT, fieldbackground=ENTRY, bordercolor=BORDER,
                    lightcolor=BORDER, darkcolor=BORDER, troughcolor=PANEL, font=self.f["base"],
                    focuscolor=ACCENT, insertcolor=TEXT, selectbackground=ACCENT, selectforeground="#14304d")
        s.configure("TFrame", background=BG)
        s.configure("Panel.TFrame", background=PANEL)
        s.configure("TLabel", background=BG, foreground=TEXT)
        s.configure("Panel.TLabel", background=PANEL)
        s.configure("Muted.TLabel", foreground=MUTED)
        s.configure("Title.TLabel", font=self.f["title"])
        s.configure("TEntry", fieldbackground=ENTRY, foreground=TEXT, bordercolor=BORDER, padding=5)
        s.map("TEntry", bordercolor=[("focus", ACCENT)], lightcolor=[("focus", ACCENT)], darkcolor=[("focus", ACCENT)])
        s.configure("TCombobox", fieldbackground=ENTRY, background=BORDER, foreground=TEXT, arrowcolor=MUTED,
                    bordercolor=BORDER, padding=4)
        s.map("TCombobox", fieldbackground=[("readonly", ENTRY)], foreground=[("readonly", TEXT)],
              bordercolor=[("focus", ACCENT)], arrowcolor=[("active", ACCENT_LIGHT)])
        r.option_add("*TCombobox*Listbox.background", ENTRY)
        r.option_add("*TCombobox*Listbox.foreground", TEXT)
        r.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
        r.option_add("*TCombobox*Listbox.selectForeground", "#14304d")
        r.option_add("*TCombobox*Listbox.font", self.f["base"])
        s.configure("TCheckbutton", background=BG, foreground=TEXT, indicatorbackground=ENTRY,
                    indicatorforeground=ACCENT_LIGHT, padding=3)
        s.map("TCheckbutton", background=[("active", BG)], indicatorbackground=[("selected", ENTRY)],
              foreground=[("active", ACCENT_LIGHT)])
        s.configure("TButton", background=BORDER, foreground=TEXT, bordercolor=BORDER, padding=(12, 6), relief="flat")
        s.map("TButton", background=[("active", "#26507c"), ("disabled", PANEL)], foreground=[("disabled", "#6c7d90")])
        s.configure("Accent.TButton", background=ACCENT, foreground="#14304d", font=self.f["bold"])
        s.map("Accent.TButton", background=[("active", ACCENT_LIGHT), ("pressed", ACCENT_DEEP), ("disabled", PANEL)],
              foreground=[("disabled", "#6c7d90")])
        s.configure("Link.TButton", background=PANEL, foreground=MUTED, padding=(4, 2))
        s.map("Link.TButton", foreground=[("active", ACCENT_LIGHT)], background=[("active", PANEL)])
        s.configure("Horizontal.TProgressbar", background=ACCENT, troughcolor=ENTRY, bordercolor=BORDER,
                    lightcolor=ACCENT, darkcolor=ACCENT)
        for o in ("Vertical", "Horizontal"):
            s.configure(o + ".TScrollbar", background=BORDER, troughcolor=PANEL, bordercolor=PANEL,
                        arrowcolor=MUTED, lightcolor=BORDER, darkcolor=BORDER)
            s.map(o + ".TScrollbar", background=[("active", "#26507c")])

    def _build(self):
        r = self.root
        r.title("Walnut Games")
        w, h = min(1480, r.winfo_screenwidth() - 40), min(900, r.winfo_screenheight() - 80)
        r.geometry("%dx%d" % (w, h))
        r.minsize(900, 560)
        # top bar
        bar = ttk.Frame(r, style="Panel.TFrame", padding=(12, 10))
        bar.pack(fill="x")
        self.v_text, self.v_cat, self.v_medal = tk.StringVar(), tk.StringVar(value="All"), tk.StringVar(value="All")
        self.v_sort = tk.StringVar(value="Best first")
        self.v_inst = tk.BooleanVar()
        ttk.Label(bar, text="Walnut Games", font=self.f["big"], style="Panel.TLabel").pack(side="left", padx=(0, 16))
        self.search = ttk.Entry(bar, textvariable=self.v_text, width=26)
        self.search.pack(side="left", padx=(0, 10))
        cats = sorted({c for g in self.games for c in g.get("categories", [])})
        for var, vals, wd, name in ((self.v_cat, ["All"] + cats, 12, "Category"),
                                    (self.v_medal, ["All"] + medal_choices(self.games), 10, "Medal"),
                                    (self.v_sort, ["Best first", "Name"], 10, "Sort")):
            ttk.Label(bar, text=name, style="Muted.TLabel", background=PANEL).pack(side="left", padx=(8, 2))
            cb = ttk.Combobox(bar, textvariable=var, values=vals, state="readonly", width=wd)
            cb.pack(side="left", padx=4)
            if name == "Category":
                self.cat_box = cb
            cb.bind("<<ComboboxSelected>>", lambda e: self.apply_filters())
        for var, text in ((self.v_inst, "Installed only"),):
            ttk.Checkbutton(bar, text=text, variable=var, command=self.apply_filters, style="TCheckbutton").pack(
                side="left", padx=8)
        self.count = ttk.Label(bar, text="", style="Muted.TLabel", background=PANEL)
        self.count.pack(side="right")
        self.b_refresh = ttk.Button(bar, text="Refresh", command=self.refresh)
        self.b_refresh.pack(side="right", padx=(0, 12))
        self.v_text.trace_add("write", lambda *a: self.apply_filters())
        # body: the cards, then the details
        # a draggable divider between the cards and the details (the user widens the details pane)
        body = tk.PanedWindow(r, orient="horizontal", bg=BORDER, sashwidth=6, sashrelief="flat",
                              borderwidth=0, showhandle=False, opaqueresize=True)
        body.pack(fill="both", expand=True)
        left = ttk.Frame(body)
        self.details = ttk.Frame(body, style="Panel.TFrame", padding=14)
        body.add(left, minsize=320, stretch="always")
        body.add(self.details, minsize=360, width=420, stretch="never")
        self.canvas = tk.Canvas(left, bg=BG, highlightthickness=0, takefocus=1)
        sb = ttk.Scrollbar(left, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda e: self.layout())
        for ev, d in (("<Button-4>", -1), ("<Button-5>", 1)):
            self.canvas.bind(ev, lambda e, d=d: self._wheel(d))
        self.canvas.bind("<MouseWheel>", lambda e: self._wheel(-1 if e.delta > 0 else 1))
        for key, dx, dy in (("Left", -1, 0), ("Right", 1, 0), ("Up", 0, -1), ("Down", 0, 1)):
            self.canvas.bind("<" + key + ">", lambda e, dx=dx, dy=dy: self._move(dx, dy))
        self.canvas.bind("<Button-1>", lambda e: self.canvas.focus_set())
        r.bind("<Control-f>", lambda e: (self.search.focus_set(), "break")[1])
        self._build_details()

    def _build_details(self):
        """the details pane: the actions (status, progress, Install/Remove/Play) pinned at the
        bottom, everything else in a scrolling area above them, so any amount of content fits"""
        d = self.details
        self.g = None
        # pinned at the bottom
        btns = ttk.Frame(d, style="Panel.TFrame")
        btns.pack(side="bottom", fill="x", pady=(8, 0))
        # the activity strip above the buttons (status, progress): only shown while something happens
        self.activity = ttk.Frame(d, style="Panel.TFrame")
        self.d_status = ttk.Label(self.activity, text="", style="Muted.TLabel", background=PANEL, wraplength=370,
                                  justify="left")
        self.d_status.pack(side="top", anchor="w", pady=(8, 2))
        self.d_reason = ttk.Label(d, text="", style="Muted.TLabel", background=PANEL, wraplength=370, justify="left")
        self.progress = ttk.Progressbar(self.activity, maximum=100, mode="determinate")
        self._activity_after = None
        self.b_install = ttk.Button(btns, text="Install", style="Accent.TButton", command=lambda: self.run_helper("install"))
        self.b_remove = ttk.Button(btns, text="Remove", command=lambda: self.run_helper("remove"))
        self.b_play = ttk.Button(btns, text="Play", style="Accent.TButton", command=self.play)
        for b in (self.b_install, self.b_remove, self.b_play):
            b.pack(side="left", padx=(0, 8))
        self.btns = btns
        # the scrolling area: a canvas holding one frame, its scroll region kept to the frame's size
        area = ttk.Frame(d, style="Panel.TFrame")
        area.pack(side="top", fill="both", expand=True)
        self.d_area = area
        self.d_scroll = tk.Canvas(area, bg=PANEL, highlightthickness=0, width=DETAILS_W)
        dsb = ttk.Scrollbar(area, orient="vertical", command=self.d_scroll.yview)
        self.d_scroll.configure(yscrollcommand=dsb.set)
        dsb.pack(side="right", fill="y")
        self.d_scroll.pack(side="left", fill="both", expand=True)
        inner = ttk.Frame(self.d_scroll, style="Panel.TFrame")
        self.d_win = self.d_scroll.create_window(0, 0, window=inner, anchor="nw", width=DETAILS_W)
        inner.bind("<Configure>", lambda e: self.d_scroll.configure(scrollregion=self.d_scroll.bbox("all")))
        self.d_scroll.bind("<Configure>", lambda e: self._details_width(e.width))
        self.d_width = DETAILS_W
        # the wheel scrolls this pane while the pointer is over it (the cards keep their own)
        area.bind("<Enter>", lambda e: self._details_wheel(True))
        area.bind("<Leave>", lambda e: self._details_wheel(False))
        head = ttk.Frame(inner, style="Panel.TFrame")
        head.pack(fill="x")
        links = ttk.Frame(head, style="Panel.TFrame")
        links.pack(side="right", anchor="n")
        ttk.Button(links, text="Open on the web", style="Link.TButton", command=self.open_web).pack(anchor="e")
        # the game's technical page on the site: its build, patches, played test and benchmark tables
        ttk.Button(links, text="Technical notes", style="Link.TButton", command=self.open_technical).pack(anchor="e")
        self.d_title = ttk.Label(head, text="", style="Title.TLabel", background=PANEL, wraplength=230)
        self.d_title.pack(side="left", anchor="w")
        self.d_sum = ttk.Label(inner, text="", style="Muted.TLabel", background=PANEL, wraplength=DETAILS_W - 8,
                               justify="left")
        self.d_sum.pack(anchor="w", pady=(2, 8))
        # a game with several launchers (two renderers): which one's rating is shown
        self.d_launch = ttk.Frame(inner, style="Panel.TFrame")
        self.launch_var = tk.IntVar(value=0)
        self.d_medal = tk.Canvas(inner, width=DETAILS_W, height=44, bg=PANEL, highlightthickness=0)
        self.d_medal.pack(anchor="w")
        self.d_aspects = tk.Canvas(inner, width=DETAILS_W, height=ASPECT_TOP + 7 * 40, bg=PANEL,
                                   highlightthickness=0)
        self.d_aspects.pack(anchor="w", pady=(6, 0))
        self.d_stats = ttk.Label(inner, text="", style="Muted.TLabel", background=PANEL, wraplength=DETAILS_W - 8,
                                 justify="left")
        self.d_stats.pack(anchor="w", pady=(4, 8))
        self.d_shots = ttk.Frame(inner, style="Panel.TFrame")
        self.d_shots.pack(fill="x")
        nav = ttk.Frame(self.d_shots, style="Panel.TFrame")
        nav.pack(fill="x")
        ttk.Button(nav, text="<", width=3, command=lambda: self.step_shot(-1)).pack(side="left")
        self.d_cap = ttk.Label(nav, text="", style="Muted.TLabel", background=PANEL, anchor="center")
        self.d_cap.pack(side="left", fill="x", expand=True)
        ttk.Button(nav, text=">", width=3, command=lambda: self.step_shot(1)).pack(side="right")
        box = tk.Frame(self.d_shots, width=SHOT_W + 4, height=SHOT_W * 9 // 16 + 6, bg=ENTRY)
        box.pack(pady=(4, 8))
        box.pack_propagate(False)
        self.d_shot = tk.Label(box, bg=ENTRY, fg=MUTED, text="", font=self.f["small"], wraplength=330)
        self.d_shot.pack(fill="both", expand=True)
        # the notes grow to their text (no second scrollbar inside the scrolling pane)
        self.d_notes = tk.Text(inner, wrap="word", height=1, width=1, bg=ENTRY, fg=TEXT, relief="flat",
                               font=self.f["small"], padx=8, pady=6, highlightthickness=1,
                               highlightbackground=BORDER, state="disabled", insertbackground=TEXT,
                               selectbackground=ACCENT, spacing3=3, cursor="arrow")
        self.d_notes.pack(fill="x", pady=(0, 6))
        self.d_notes.tag_configure("h", foreground=MUTED, font=self.f["bold"], spacing1=6)
        self.d_notes.tag_configure("warn", foreground=ACCENT_LIGHT)
        self.d_notes.tag_configure("code", font=tkfont.Font(family="monospace", size=9), background=BORDER)
        self.show(None)

    # -- cards
    def apply_filters(self, *a):
        self.shown = filter_games(self.games, self.have, self.v_text.get(), self.v_cat.get(), self.v_medal.get(),
                                  self.v_inst.get(), self.v_sort.get())
        self.count.configure(text="%d of %d games" % (len(self.shown), len(self.games)))
        self.layout(reset=True)

    def _thumb(self, g):
        """the card's picture: the packaged one, else the cached web one (fetched once); None is the placeholder"""
        gid = g["id"]
        if gid not in self.thumbs:
            path = os.path.join(self.data, "games", gid, "thumb.png")
            if not os.path.exists(path):
                path = self.cache_path(gid, "thumb.png")
                if not os.path.exists(path):
                    if self.online and ("thumb", gid) not in self._fetching:
                        self.fetch(gid, "thumb.png", path, "thumb")
                    return None
            try:
                self.thumbs[gid] = tk.PhotoImage(file=path)
            except tk.TclError:
                self.thumbs[gid] = None
                if path.startswith(cache_dir()):
                    try:
                        os.remove(path)     # a broken download: fetched again next time
                    except OSError:
                        pass
        return self.thumbs[gid]

    def layout(self, reset=False):
        c = self.canvas
        c.delete("all")
        self.cards = {}
        width = max(c.winfo_width(), CARD_W + 2 * GAP)
        cols = max(1, (width - GAP) // (CARD_W + GAP))
        self.cols = cols
        for i, g in enumerate(self.shown):
            x = GAP + (i % cols) * (CARD_W + GAP)
            y = GAP + (i // cols) * (CARD_H + GAP)
            self.draw_card(g, x, y)
        rows = (len(self.shown) + cols - 1) // cols
        c.configure(scrollregion=(0, 0, width, GAP + rows * (CARD_H + GAP)))
        if not self.shown:
            c.create_text(width // 2, 80, text="No game matches.", fill=MUTED, font=self.f["big"])
        if reset:
            c.yview_moveto(0)

    def draw_card(self, g, x, y):
        c, tag = self.canvas, "g_" + g["id"].replace("-", "_")
        sel = g["id"] == self.selected
        c.create_polygon(rounded_points(x, y, x + CARD_W, y + CARD_H, 12), smooth=True, fill=PANEL,
                         outline=ACCENT if sel else BORDER, width=2 if sel else 1, tags=(tag, tag + "_bg"))
        img = self._thumb(g)
        if img:
            c.create_image(x + 4, y + 4, image=img, anchor="nw", tags=(tag,))
        else:
            c.create_rectangle(x + 4, y + 4, x + 4 + THUMB_W, y + 4 + THUMB_H, fill=ENTRY, outline="", tags=(tag,))
        m = medal_of(g)
        sc = score_of(g)
        label = m if sc is None else "%s  %d" % (m, sc)
        px, py = x + 10, y + THUMB_H + 12
        tw = self.f["pill"].measure(label) + 20
        fill = MEDAL_COLORS.get(m, MEDAL_COLORS["Untested"])
        c.create_polygon(rounded_points(px, py, px + tw, py + 22, 10), smooth=True, fill=fill, outline="", tags=(tag,))
        c.create_text(px + tw / 2, py + 11, text=label, fill=pill_text_color(fill), font=self.f["pill"], tags=(tag,))
        flags = []
        if is_installed(g, self.have):
            flags.append("Installed")
        if flags:
            c.create_text(x + CARD_W - 10, py + 11, text="  ".join(flags), fill=ACCENT_LIGHT, anchor="e",
                          font=self.f["small"], tags=(tag,))
        c.create_text(x + 10, y + THUMB_H + 48, text=g["title"], fill=TEXT, anchor="w", font=self.f["card"], tags=(tag,),
                      width=CARD_W - 20)
        c.create_text(x + 10, y + THUMB_H + 72, text=" / ".join(g.get("categories", [])), fill=MUTED, anchor="w",
                      font=self.f["small"], tags=(tag,), width=CARD_W - 20)
        self.cards[g["id"]] = (x, y)
        c.tag_bind(tag, "<Button-1>", lambda e, i=g["id"]: self.select(i))
        c.tag_bind(tag, "<Enter>", lambda e, t=tag: c.itemconfigure(t + "_bg", fill=CARD_HOVER))
        c.tag_bind(tag, "<Leave>", lambda e, t=tag: c.itemconfigure(t + "_bg", fill=PANEL))

    def _wheel(self, d):
        self.canvas.yview_scroll(d * 3, "units")

    def _move(self, dx, dy):
        ids = [g["id"] for g in self.shown]
        if not ids:
            return
        i = ids.index(self.selected) if self.selected in ids else -1
        j = 0 if i < 0 else min(len(ids) - 1, max(0, i + dx + dy * self.cols))
        self.select(ids[j])
        x, y = self.cards[ids[j]]
        top, bot = self.canvas.canvasy(0), self.canvas.canvasy(self.canvas.winfo_height())
        total = float(self.canvas.cget("scrollregion").split()[3]) or 1
        if y < top:
            self.canvas.yview_moveto((y - GAP) / total)
        elif y + CARD_H > bot:
            self.canvas.yview_moveto((y + CARD_H + GAP - self.canvas.winfo_height()) / total)

    def select(self, gid):
        self.selected = gid
        for i, (x, y) in self.cards.items():
            tag = "g_" + i.replace("-", "_") + "_bg"
            self.canvas.itemconfigure(tag, outline=ACCENT if i == gid else BORDER, width=2 if i == gid else 1)
        self.show(self.by_id.get(gid))

    # -- details
    def show(self, g):
        self.shot_i = 0
        self.g = g
        d = self.d_medal
        d.delete("all")
        self.d_aspects.delete("all")
        notes = self.d_notes
        notes.configure(state="normal")
        notes.delete("1.0", "end")
        if g is None:
            self.d_title.configure(text="Walnut Games")
            self.d_sum.configure(text="Pick a game to see how it runs on the PS3.")
            self.d_stats.configure(text="")
            self.d_cap.configure(text="")
            self.d_shot.configure(image="", text="")
            self.d_shot.image = None
            notes.configure(state="disabled")
            self.d_shots.pack_forget()
            self.d_launch.pack_forget()
            self._fit_notes()
            self.update_buttons()
            return
        self.d_title.configure(text=g["title"])
        self.d_sum.configure(text=g.get("summary", ""))
        for w in self.d_launch.winfo_children():
            w.destroy()
        launchers = g.get("launchers") or []
        self.launch_var.set(0)
        if len(launchers) > 1:
            ttk.Label(self.d_launch, text="Launchers, best first:", style="Muted.TLabel",
                      background=PANEL).pack(anchor="w")
            row = ttk.Frame(self.d_launch, style="Panel.TFrame")
            row.pack(anchor="w", fill="x")
            for i, l in enumerate(launchers):
                tk.Radiobutton(row, text=launcher_label(l), variable=self.launch_var, value=i, indicatoron=0,
                               command=self.show_rating, bg=ENTRY, fg=TEXT, selectcolor=ACCENT_DEEP,
                               activebackground=BORDER, activeforeground=TEXT, bd=0, highlightthickness=0,
                               padx=8, pady=3, font=self.f["small"]).pack(anchor="w", pady=(2, 0))
            if not self.d_launch.winfo_ismapped():
                self.d_launch.pack(anchor="w", fill="x", pady=(0, 6), before=self.d_medal)
        else:
            self.d_launch.pack_forget()
        if not self.d_shots.winfo_ismapped():
            if self.d_notes.winfo_ismapped():
                self.d_shots.pack(fill="x", before=self.d_notes)
            else:
                self.d_shots.pack(fill="x")
        self.show_rating()
        self.show_shot()
        if g.get("medal_note"):
            notes.insert("end", g["medal_note"] + "\n", "warn")
        if g.get("needs_data") and g.get("data_steps"):
            notes.insert("end", "Setting up the game data\n", "h")
            for n, step in enumerate(g["data_steps"], 1):
                notes.insert("end", "%d. " % n)
                for text, code in step_parts(step):
                    notes.insert("end", text, "code" if code else "")
                notes.insert("end", "\n")
            if g.get("data_guide"):
                notes.insert("end", "More: " + g["data_guide"] + "\n")
        if g.get("known_issues"):
            notes.insert("end", "Known issues\n", "h")
            for k in g["known_issues"]:
                notes.insert("end", "- " + k + "\n")
        if g.get("patches"):
            notes.insert("end", "Walnut's changes (%d)\n" % len(g["patches"]), "h")
            for p in g["patches"]:
                notes.insert("end", "- " + p["subject"] + "\n")
        notes.configure(state="disabled")
        self._fit_notes()
        self.d_scroll.yview_moveto(0)
        self.update_buttons()

    def _details_width(self, w):
        """the pane was dragged or the window resized: the content follows its width"""
        w = max(DETAILS_W, w)
        if w == self.d_width:
            return
        self.d_width = w
        self.d_scroll.itemconfigure(self.d_win, width=w)
        self.d_sum.configure(wraplength=w - 8)
        self.d_stats.configure(wraplength=w - 8)
        self.d_title.configure(wraplength=max(200, w - 150))
        self.d_medal.configure(width=w)
        self.d_aspects.configure(width=w)
        if self.g is not None:
            self.d_aspects.delete("all")
            self.draw_aspects(self.g["rating"])
            self._fit_notes()

    def _details_wheel(self, on):
        if on:
            self.root.bind_all("<Button-4>", lambda e: self.d_scroll.yview_scroll(-2, "units"))
            self.root.bind_all("<Button-5>", lambda e: self.d_scroll.yview_scroll(2, "units"))
            self.root.bind_all("<MouseWheel>", lambda e: self.d_scroll.yview_scroll(-2 if e.delta > 0 else 2, "units"))
        else:
            for ev in ("<Button-4>", "<Button-5>", "<MouseWheel>"):
                self.root.unbind_all(ev)

    def _fit_notes(self):
        """the notes box as tall as its wrapped text (hidden when empty)"""
        t = self.d_notes
        if not t.get("1.0", "end-1c").strip():
            t.pack_forget()
            return
        if not t.winfo_ismapped():
            t.pack(fill="x", pady=(0, 6))
        t.update_idletasks()
        lines = t.count("1.0", "end", "displaylines")
        t.configure(height=max(1, (lines[0] if isinstance(lines, tuple) else lines or 1)))

    def show_rating(self):
        """the medal, the scores and what they were measured on: the selected launcher's (the best by
        default; a game with one launcher has only that)"""
        g = self.g
        if g is None:
            return
        launchers = g.get("launchers") or []
        r = launchers[self.launch_var.get()]["rating"] if len(launchers) > 1 else g["rating"]
        d = self.d_medal
        d.delete("all")
        m = r["medal"]
        fill = MEDAL_COLORS.get(m, MEDAL_COLORS["Untested"])
        d.create_polygon(rounded_points(2, 4, 118, 38, 16), smooth=True, fill=fill, outline="")
        d.create_text(60, 21, text=m, fill=pill_text_color(fill), font=self.f["big"])
        if r.get("score") is not None:
            d.create_text(134, 21, text=str(r["score"]), fill=TEXT, font=self.f["title"], anchor="w")
            d.create_text(178, 24, text="of 100" + ("  (cap: " + r["reason"] + ")" if r.get("capped") else ""),
                          fill=MUTED, font=self.f["small"], anchor="w")
        self.draw_aspects(r)
        stats = []
        if r.get("hours"):
            stats.append("%.1f h played, %d crash%s%s" % (r["hours"], r["crashes"], "" if r["crashes"] == 1 else "es",
                                                         " (provisional)" if r.get("provisional") else ""))
        stats.append("Controller: " + (r.get("controller") or g.get("controller") or "unknown"))
        if g.get("needs_data"):
            stats.append("Needs your own game data")
        tested = tested_text(r)
        lines = [x for x in (tested, basis_text(r)) if x] + problem_lines(r)
        self.d_stats.configure(text="".join(x + "\n" for x in lines) + "  |  ".join(stats))

    def draw_aspects(self, r):
        """one block per aspect: its name, a bar and the score out of 100, and under them what was
        measured (a bare "2" next to "Frame rate" read as 2 fps). Each block is as tall as its text
        (the measured line wraps on a narrow pane), and the canvas as tall as all of them."""
        c = self.d_aspects
        c.delete("all")
        w = getattr(self, "d_width", DETAILS_W)
        c.create_text(0, 8, text="Scores out of 100, with what was measured", fill=MUTED, anchor="w",
                      font=self.f["small"])
        y = ASPECT_TOP
        for k in r.get("order") or ASPECT_ORDER:
            a = r["aspects"].get(k)
            if not a:
                continue
            c.create_text(0, y + 9, text=a["name"], fill=TEXT, anchor="w", font=self.f["small"])
            c.create_rectangle(110, y + 4, 270, y + 14, fill=ENTRY, outline=BORDER)
            bottom = y + 18
            sc = a["score"]
            if sc is None:
                c.create_text(278, y + 9, text="not tested" if k in ("controls", "features") else "not measured",
                              fill=MUTED, anchor="w", font=self.f["small"])
            else:
                if sc:
                    c.create_rectangle(110, y + 4, 110 + 1.6 * sc, y + 14, fill=ACCENT, outline="")
                c.create_text(278, y + 9, text="%d/100" % round(sc), fill=TEXT, anchor="w", font=self.f["bold"])
                if a.get("detail"):
                    t = c.create_text(110, y + 18, text=a["detail"], fill=MUTED, anchor="nw", font=self.f["small"],
                                      width=max(160, w - 112))
                    bottom = c.bbox(t)[3]
            y = bottom + ASPECT_GAP
        c.configure(height=y + 2, width=w)

    # -- screenshots
    def step_shot(self, d):
        if self.g and screenshots(self.g):
            self.shot_i = (self.shot_i + d) % len(screenshots(self.g))
            self.show_shot()

    def show_shot(self):
        self.shot_img = None
        shots = screenshots(self.g) if self.g else []
        if not shots:
            self.d_cap.configure(text="no screenshots yet")
            self.d_shot.configure(image="", text="No screenshots yet.")
            return
        label, rel = shots[self.shot_i % len(shots)]
        self.d_cap.configure(text="%s  %d/%d" % (label, self.shot_i % len(shots) + 1, len(shots)))
        path = os.path.join(self.data, "games", self.g["id"], rel)
        if not os.path.exists(path):
            path = self.cache_path(self.g["id"], rel)
            if not os.path.exists(path):
                self.d_shot.configure(image="", text="Fetching the screenshot...")
                self.fetch(self.g["id"], rel, path)
                return
        self.set_shot(path)

    def cache_path(self, gid, rel):
        return os.path.join(cache_dir(), gid, rel)

    def fetch(self, gid, rel, path, kind="shot"):
        if not self.online:
            self.events.put((kind, gid, rel, None))
            return
        self._fetching.add((kind, gid))

        def work():
            try:
                req = urllib.request.Request(WEB + "games/%s/%s" % (gid, rel), headers={"User-Agent": "walnut-games"})
                with urllib.request.urlopen(req, timeout=15) as f:
                    data = f.read(8 << 20)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path + ".part", "wb") as o:
                    o.write(data)
                os.replace(path + ".part", path)
                self.events.put((kind, gid, rel, path))
            except Exception as e:
                print("walnut-games: can't fetch %s/%s: %s" % (gid, rel, e), file=sys.stderr)
                self.events.put((kind, gid, rel, None))
        threading.Thread(target=work, daemon=True).start()

    def set_shot(self, path):
        try:
            from PIL import Image
        except ImportError:
            self.d_shot.configure(image="", text="Screenshots need python3-pil.imagetk")
            return
        try:
            im = Image.open(path).convert("RGB")
            im.thumbnail((SHOT_W, SHOT_W * 9 // 16 + 1))
            try:
                from PIL import ImageTk
                img = ImageTk.PhotoImage(im)
            except ImportError:   # Pillow without its Tk part: hand Tk a PNG
                import base64
                import io
                buf = io.BytesIO()
                im.save(buf, "PNG")
                img = tk.PhotoImage(data=base64.b64encode(buf.getvalue()))
        except Exception:         # a broken picture must not take the window down
            self.d_shot.configure(image="", text="This screenshot can't be shown.")
            return
        self.shot_img = img
        self.d_shot.configure(image=img, text="")

    # -- buttons and the helper
    def fetch_catalog(self, manual):
        """download the web catalog in a thread; the result comes back as a "catalog" event"""
        def work():
            try:
                req = urllib.request.Request(WEB + "catalog.json", headers={"User-Agent": "walnut-games"})
                with urllib.request.urlopen(req, timeout=8) as f:
                    raw = f.read(MAX_CATALOG + 1)
                if len(raw) > MAX_CATALOG:
                    raise ValueError("catalog too large")
                cat = validate_catalog(json.loads(raw.decode("utf-8")))
                self.events.put(("catalog", cat, raw, manual, None))
            except Exception as e:
                print("walnut-games: catalog refresh failed: %s" % e, file=sys.stderr)
                self.events.put(("catalog", None, None, manual, e))
        threading.Thread(target=work, daemon=True).start()

    def refresh(self):
        """the Refresh button: the web catalog, the installed state and the launchers, redrawn in place"""
        if self.refreshing or self.busy or not self.online:
            return
        self.refreshing = True
        self.show_activity("Refreshing...")
        self.update_buttons()
        self.fetch_catalog(manual=True)

    def on_catalog(self, cat, raw, manual, err):
        """a fetch finished (on the Tk thread): cache and show a newer catalog, re-read what is installed"""
        self.refreshing = False
        changed = False
        if cat is not None and is_newer(cat, self.cat):
            try:
                os.makedirs(cache_dir(), exist_ok=True)
                path = os.path.join(cache_dir(), "catalog.json")
                with open(path + ".part", "wb") as o:
                    o.write(raw)
                os.replace(path + ".part", path)
            except OSError as e:
                print("walnut-games: can't cache the catalog: %s" % e, file=sys.stderr)
            changed = True
        self._manifests.clear()
        if changed or manual:
            self.refresh_installed(cat["games"] if changed else None)
        if changed:
            self.apply_catalog(cat)
        elif manual:
            self.apply_filters_inplace()
        if manual:
            if err is not None:
                self.show_activity("Can't reach the website, showing what is saved.", hide_after=6)
            elif changed:
                self.show_activity("Catalog updated: %d games." % len(self.games), hide_after=6)
            else:
                self.show_activity("Up to date.", hide_after=4)
        self.update_buttons()

    def apply_catalog(self, cat):
        """show another catalog without losing the filters, search, selection or scroll"""
        old = self.by_id.get(self.selected)
        shot_i, launch_i = self.shot_i, self.launch_var.get()
        dy = self.d_scroll.yview()[0]
        self.cat, self.games = cat, cat["games"]
        self.by_id = {g["id"]: g for g in self.games}
        self.thumbs.clear()
        cats = ["All"] + sorted({c for g in self.games for c in g.get("categories", [])})
        self.cat_box.configure(values=cats)
        if self.v_cat.get() not in cats:
            self.v_cat.set("All")
        self.apply_filters_inplace()
        new = self.by_id.get(self.selected)
        if self.selected and new is None:
            self.selected = None
            self.show(None)
        elif new is not None and new != old:
            self.show(new)
            self.shot_i = shot_i % max(1, len(screenshots(new)))
            if len(new.get("launchers") or []) > 1:
                self.launch_var.set(launch_i if launch_i < len(new["launchers"]) else 0)
                self.show_rating()
            self.show_shot()
            self.d_scroll.update_idletasks()
            self.d_scroll.yview_moveto(dy)
        else:
            if new is not None:
                self.g = new
            self.update_buttons()

    def apply_filters_inplace(self):
        """apply_filters, keeping the cards' scroll position"""
        region = self.canvas.cget("scrollregion").split()
        old_h = float(region[3]) if len(region) == 4 else 0.0
        top = self.canvas.yview()[0] * old_h
        self.apply_filters()
        region = self.canvas.cget("scrollregion").split()
        new_h = float(region[3]) if len(region) == 4 else 0.0
        self.canvas.yview_moveto(min(top / new_h, 1.0) if new_h else 0)

    def refresh_installed(self, games=None):
        self.have = dpkg_installed({p for g in (games or self.games) for p in g["packages"]})

    def manifest(self, g):
        """the game's packages' files (dpkg -L), cached until the installed set changes"""
        key = (g["id"], tuple(sorted(p for p in g["packages"] if p in self.have)))
        if key not in self._manifests:
            self._manifests[key] = package_files(g["packages"])
        return self._manifests[key]

    def update_buttons(self):
        g = getattr(self, "g", None)
        inst = bool(g) and is_installed(g, self.have)
        idle = self.busy is None
        can = bool(g) and g["id"] in self.pkg_ids
        self.b_install.configure(state="normal" if can and idle and not inst else "disabled")
        reason = "Install it after the next Walnut Games update." if g and not can and not inst else ""
        self.d_reason.configure(text=reason)
        if reason:
            self.d_reason.pack(side="bottom", fill="x", after=self.btns)
        else:
            self.d_reason.pack_forget()
        self.b_refresh.configure(state="normal" if idle and not self.refreshing else "disabled")
        self.b_remove.configure(state="normal" if g and idle and inst else "disabled")
        self.play_cmds = order_launchers(play_commands(g, self.runtime_dir, files=lambda: self.manifest(g)),
                                         g.get("launchers")) if g and inst else []
        self.b_play.configure(state="normal" if idle and self.play_cmds else "disabled",
                              text="Play \u25be" if len(self.play_cmds) > 1 else "Play")

    def show_activity(self, text, progress=None, hide_after=None):
        """the status line (and the progress bar when progress is a number) above the buttons;
        hide_after: seconds until it goes away by itself"""
        if self._activity_after:
            self.root.after_cancel(self._activity_after)
            self._activity_after = None
        self.d_status.configure(text=text)
        if progress is None:
            self.progress.pack_forget()
        else:
            self.progress.configure(value=progress)
            if not self.progress.winfo_ismapped():
                self.progress.pack(side="top", fill="x")
        if not self.activity.winfo_ismapped():
            self.activity.pack(side="bottom", fill="x", before=self.d_area)
        if hide_after:
            self._activity_after = self.root.after(int(hide_after * 1000), self.hide_activity)

    def hide_activity(self):
        if self._activity_after:
            self.root.after_cancel(self._activity_after)
            self._activity_after = None
        self.activity.pack_forget()

    def run_helper(self, op):
        g = self.g
        if not g or self.busy:
            return
        if op == "remove" and not messagebox.askyesno("Remove", "Remove %s?" % g["title"], parent=self.root):
            return
        self.busy = (g["id"], op)
        self.show_activity(("Installing %s..." if op == "install" else "Removing %s...") % g["title"], progress=0)
        self.update_buttons()
        cmd = ["sudo", "-n", self.helper, op, g["id"]]

        def work():
            tail = []
            try:
                p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True, errors="replace", start_new_session=True)
                for line in p.stdout:
                    ev = parse_helper_line(line)
                    if ev:
                        self.events.put(("helper",) + ev)
                        if ev[0] in ("log", "error"):
                            tail.append(ev[1])
                rc = p.wait()
            except OSError as e:
                rc, tail = 1, [str(e)]
            self.regenerate_menu()
            self.events.put(("done", g["id"], op, rc, tail[-3:]))
        threading.Thread(target=work, daemon=True).start()

    def regenerate_menu(self):
        rt = os.environ.get("XDG_RUNTIME_DIR")
        if rt and os.path.exists(MENU_TOOL):
            try:
                subprocess.run([MENU_TOOL, os.path.join(rt, "ps3-game-menu")], stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
            except (OSError, subprocess.SubprocessError):
                pass

    def _poll(self):
        try:
            while True:
                ev = self.events.get_nowait()
                if ev[0] == "helper":
                    if ev[1] == "progress":
                        self.show_activity(ev[3], progress=ev[2])
                    elif ev[1] == "error":
                        self.show_activity(ev[2], progress=self.progress["value"])
                elif ev[0] == "done":
                    _, gid, op, rc, tail = ev
                    self.busy = None
                    self.refresh_installed()
                    ok = "installed" if op == "install" else "removed"
                    title = self.by_id.get(gid, {"title": gid})["title"]
                    # a success fades after a few seconds; a failure stays until the next action
                    self.show_activity(title + (" " + ok + "." if rc == 0 else
                                       ": " + op + " failed" + (" (refused)" if rc == 2 else "") + "."),
                                       hide_after=6 if rc == 0 else None)
                    if rc != 0:
                        messagebox.showerror("Walnut Games", "%s of %s failed:\n\n%s" % (
                            op, title, "\n".join(tail) or "no details"), parent=self.root)
                    self.apply_filters_keep()
                elif ev[0] == "played":
                    _, gid, rc, secs, log = ev
                    title = self.by_id[gid]["title"] if gid in self.by_id else gid
                    if secs < QUICK_EXIT_S:
                        # it quit almost at once: say so, with where its output went
                        self.show_activity("%s closed after %d s%s. Its output: %s" % (
                            title, secs, "" if rc == 0 else " (exit code %d)" % rc, log))
                    else:
                        self.hide_activity()
                elif ev[0] == "catalog":
                    self.on_catalog(*ev[1:])
                elif ev[0] == "thumb":
                    self._fetching.discard(("thumb", ev[1]))
                    if ev[3]:
                        self.thumbs.pop(ev[1], None)
                        self.layout()
                elif ev[0] == "shot":
                    if self.g and self.g["id"] == ev[1] and ev[3]:
                        shots = screenshots(self.g)
                        if shots and shots[self.shot_i % len(shots)][1] == ev[2]:
                            self.set_shot(ev[3])
                    elif self.g and self.g["id"] == ev[1]:
                        self.d_shot.configure(image="", text="Screenshot not available offline.")
        except queue.Empty:
            pass
        self.root.after(100, self._poll)

    def apply_filters_keep(self):
        sel = self.selected
        self.apply_filters()
        self.selected = sel
        if sel in self.cards:
            self.select(sel)
        else:
            self.update_buttons()

    def play(self):
        """start the game; with several launchers (two renderers), a menu of them under the button"""
        g, cmds = self.g, getattr(self, "play_cmds", [])
        if not g or not cmds or self.busy:
            return
        if len(cmds) == 1:
            self.launch(g, cmds[0][1], cmds[0][2])
            return
        m = tk.Menu(self.root, tearoff=0, bg=PANEL, fg=TEXT, activebackground=ACCENT, activeforeground=PANEL,
                    bd=1, relief="solid", font=self.f["base"])
        for label, argv, cwd in cmds:
            m.add_command(label=label, command=lambda a=argv, c=cwd: self.launch(g, a, c))
        b = self.b_play
        # no grab_release after it: on X11 tk_popup returns at once, and the menu's grab is what
        # closes it on a click outside
        m.tk_popup(b.winfo_rootx(), b.winfo_rooty() + b.winfo_height())

    def launch(self, g, argv, cwd):
        """start the game (through ps3-game: game mode); its output goes to a log under
        ~/.cache/walnut-games; the status says "Starting" until the game has had time to come up,
        and a game that quits within QUICK_EXIT_S says so, with the log's path"""
        logdir = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "walnut-games")
        log = os.path.join(logdir, g["id"] + ".log")
        try:
            os.makedirs(logdir, exist_ok=True)
            out = open(log, "w")
            env = dict(os.environ)
            if "/usr/games" not in env.get("PATH", "").split(":"):
                # the user manager's PATH has no /usr/games, where Debian puts most games
                env["PATH"] = env.get("PATH", "/usr/bin:/bin") + ":/usr/local/games:/usr/games"
            p = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                 start_new_session=True, env=env)
            out.close()
        except OSError as e:
            self.show_activity("Can't start %s: %s" % (g["title"], e))
            return
        self.show_activity("Starting %s..." % g["title"], hide_after=QUICK_EXIT_S)
        t0 = time.monotonic()

        def wait():
            rc = p.wait()
            self.events.put(("played", g["id"], rc, int(time.monotonic() - t0), log))
        threading.Thread(target=wait, daemon=True).start()

    def open_web(self):
        webbrowser.open(web_url(self.g["id"] if getattr(self, "g", None) else None))

    def open_technical(self):
        webbrowser.open(web_url(self.g["id"] if getattr(self, "g", None) else None, technical=True))


def main(argv):
    if tk is None:
        print("walnut-games: python3-tk is not installed", file=sys.stderr)
        return 1
    try:
        root = tk.Tk()
    except tk.TclError as e:
        print("walnut-games: no display: %s" % e, file=sys.stderr)
        return 1
    try:
        App(root)
    except (OSError, ValueError, KeyError) as e:
        print("walnut-games: the catalog can't be read: %s" % e, file=sys.stderr)
        return 1
    root.mainloop()
    return 0
