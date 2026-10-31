"""walnut_games.py (walnut-games package): the Walnut Games desktop app, run by /usr/bin/walnut-games.

Design
- A plain Tk window for the pad, mouse and keyboard: a top bar (search, Filters (the tags), medal, sort, "installed
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
  (ps3-game-menu --reload "$XDG_RUNTIME_DIR/ps3-game-menu", which also restarts the menu), so the new
  game starts through game mode. Play
  is off while the helper runs: game mode would freeze it with dpkg's lock held.
- At each start, in a thread, `sudo -n walnut-games-helper adopt` (adopt_games) marks the catalog games'
  automatic packages manual: the old ps3-games metapackages (gone since 2026-10-28) installed them as
  their dependencies, and autoremove would take them once a metapackage went. Failures are ignored;
  with nothing to adopt the helper changes nothing.
- Play starts the runtime copy of the game's .desktop (its Exec already goes through ps3-game),
  else `ps3-game` plus the entry in /usr/share/applications with the field codes cut out and
  split by shlex, never through a shell.
- Game data (a game's catalog data_setup, GameDB build.toml [data_setup]): the page offers "Install from
  Walnut licensed" when the apt sources have one of its present_any names (apt-cache policy, once in a
  thread at start and on Refresh: the licensed suite; checked over every present_any name, since its
  newer builds carry game-data-packager's names), then "Add game files..." (required data missing, or optional data only like OpenTTD) with a label under
  it that starts REQUIRED: or OPTIONAL: and names the original game ([data_setup] original); with only a
  free shareware or demo edition installed it stays, OPTIONAL, for the full game (whose import replaces
  the free one), and the chooser no longer offers that free download. Play stays
  off while required data is missing. The chooser is a window of big buttons (the
  pad is the pointer; arrows, Enter, Escape) listing find_sources: the disc in the drive (lsblk; not
  mounted: /dev/sr0, which walnut-data mounts), /media/$USER/* and /run/media/$USER/*, ~/GOG Games/*,
  Steam's steamapps/common/*, *.iso/*.img/*.cue/*.mdf/*.zip/*.exe/gog_*.sh in ~/Downloads and ~; Other file and
  Other folder (Tk's dialogs). The game's hint is shown above. `walnut-data import GAME SOURCE` runs
  in its own session with its output in ~/.cache/walnut-games/import.log (import.json: its pid, game,
  start, source), which a thread reads as it grows: the step, the file being read, the elapsed time,
  the last 3 log lines and a bar that only moves (no percentages). Closing the app leaves it running;
  opened again, the app finds it by walnut-data's lock (~/.cache/walnut-data/import.lock, held for
  the whole import) and shows it in place of the buttons. A failure is told by game-data-packager's
  error kind (import_error_message); unsuitable (a known release gdp refuses, e.g. Hexen 1.0) tells the release and gdp's reason;
  needs-download offers "Try again with download" (-p for the
  game's downloads packages) when the game has some. A plain import makes the game's base (its required
  packages); when the same source also holds optional ones (walnut-data's "addon available PKG" lines:
  expansions, music) the page shows "Also add: NAME" buttons, which run `walnut-data import -p PKG GAME
  SOURCE` with the same source. After a success the menu is made again
  (ps3-game-menu --reload) and the page shows Play, naming the launchers the data added. Remove names
  the installed data packages and their size and runs the helper's remove-data after remove. The
  installed data's edition (X-Walnut-Edition, dpkg-query) is shown with the rating's facts.
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
    from tkinter import filedialog, messagebox, ttk
except ImportError:   # the tests of the pure functions need no Tk
    tk = tkfont = filedialog = messagebox = ttk = None

DATA = os.environ.get("WALNUT_GAMES_DATA") or "/usr/share/walnut-games"
WEB = "https://walnut.nanobyte.dev/gamedb/"
HELPER = "/usr/lib/ps3/sbin/walnut-games-helper"
MENU_TOOL = "/usr/lib/ps3/desktop/ps3-game-menu"
APPS = "/usr/share/applications"

# the Walnut palette (the website's)
BG, PANEL, BORDER = "#14304d", "#0e2238", "#1d3e61"
ACCENT, ACCENT_DEEP, ACCENT_LIGHT = "#d49a5c", "#b5763a", "#e3ae73"
TEXT, MUTED, ENTRY = "#ececec", "#b4bec9", "#091726"     # text white, secondary text light gray
LINK, LINK_HOVER = ACCENT, ACCENT_LIGHT                   # yellow is for links only (the user, 2026-10-31)
CARD_HOVER = "#12304f"

MEDAL_ORDER = ["Platinum", "Gold", "Silver", "Bronze", "Tin", "Rust", "Scrap", "Untested"]
OLD_MEDALS = {"Tin"}    # rules 1's scale only (rules 2.1 has none): in the filter only when a game has it
MEDAL_COLORS = {"Platinum": "#e5e4e2", "Gold": "#d4af37", "Silver": "#c0c0c0", "Bronze": "#cd7f32",
                "Tin": "#9a9ea3", "Rust": "#8a4b32", "Scrap": "#5f5a55", "Untested": "#6f7a86"}
QUICK_EXIT_S = 20   # a game that quits sooner than this gets a note in the status line


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
    be of the right type, data_setup's too), and the ids and picture paths must be safe to use in a URL and a cache path"""
    def need(ok, what):
        if not ok:
            raise ValueError(what)

    need(isinstance(obj, dict) and isinstance(obj.get("games"), list) and obj["games"], "no games list")
    # the tag taxonomy (gamedb/tags.toml): optional (an older catalog has none), well formed when there
    groups, tags = obj.get("tag_groups", []), obj.get("tags", {})
    need(isinstance(groups, list) and all(isinstance(x, dict) and isinstance(x.get("id"), str) and
                                          isinstance(x.get("tags", []), list) and
                                          all(isinstance(t, str) for t in x.get("tags", [])) for x in groups),
         "bad tag_groups")
    need(isinstance(tags, dict) and all(isinstance(k, str) and isinstance(v, dict) for k, v in tags.items()),
         "bad tags")
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
        for k in ("tags", "known_issues"):
            need(all(isinstance(x, str) for x in g.get(k) or []) and isinstance(g.get(k) or [], list),
                 "%s: bad %s" % (gid, k))
        for k in ("patches", "launchers", "tests"):
            need(isinstance(g.get(k) or [], list) and all(isinstance(x, dict) for x in g.get(k) or []),
                 "%s: bad %s" % (gid, k))
        need(all(isinstance(p.get("subject"), str) for p in g.get("patches") or []), "%s: bad patches" % gid)
        ds = g.get("data_setup")
        need(ds is None or isinstance(ds, dict), "%s: bad data_setup" % gid)
        for k in ("required", "optional", "downloads", "fetch", "present_any"):
            v = (ds or {}).get(k) or []
            need(isinstance(v, list) and all(isinstance(x, str) and x for x in v), "%s: bad data_setup %s" % (gid, k))
        alt = (ds or {}).get("alternatives") or []
        need(isinstance(alt, list) and all(isinstance(grp, list) and all(isinstance(x, str) and x for x in grp)
                                           for grp in alt), "%s: bad data_setup alternatives" % gid)
        for k in ("gdp", "hint"):
            need((ds or {}).get(k) is None or isinstance(ds[k], str), "%s: bad data_setup %s" % (gid, k))
        need((ds or {}).get("original") is None or (isinstance(ds["original"], str) and ds["original"].strip()),
             "%s: bad data_setup original" % gid)
        ed = (ds or {}).get("editions") or {}
        need(isinstance(ed, dict) and all(isinstance(v, str) for v in ed.values()), "%s: bad data_setup editions" % gid)
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


def data_packages(game):
    """the data packages whose state the app reads for a game: data_setup's present_any and optional"""
    ds = game.get("data_setup") if isinstance(game.get("data_setup"), dict) else {}
    return [p for k in ("present_any", "optional") for p in ds.get(k) or [] if isinstance(p, str)]


def data_present(game, have):
    """whether the game's data is installed: any one of data_setup's present_any (its required
    game-data-packager packages, or the licensed suite's builds of the same data); None when the game
    names no such data (no data_setup, or optional data only)"""
    ds = game.get("data_setup") if isinstance(game.get("data_setup"), dict) else {}
    need = [p for p in ds.get("present_any") or [] if isinstance(p, str)]
    return any(p in have for p in need) if need else None


def tag_label(taxonomy, tid):
    """a tag id's label (catalog "tags"), the id itself when the catalog doesn't know it"""
    t = (taxonomy or {}).get(tid)
    return t.get("label") or tid if isinstance(t, dict) else tid


def genre_text(game, taxonomy):
    """the card's line: the game's genre tags' labels ("shooter / first-person"); an older catalog without
    the taxonomy: its categories"""
    if taxonomy:
        return " / ".join(tag_label(taxonomy, t) for t in game.get("tags") or []
                          if isinstance(taxonomy.get(t), dict) and taxonomy[t].get("group") == "genre")
    return " / ".join(c for c in game.get("categories") or [] if isinstance(c, str))


def tag_groups(cat):
    """the tag filter's groups: [(group id, label, [(tag id, label, games)])], only tags some game has, in
    the catalog's order"""
    taxonomy = cat.get("tags") or {}
    out = []
    for grp in cat.get("tag_groups") or []:
        tags = [(t, tag_label(taxonomy, t), taxonomy[t].get("games") or 0) for t in grp.get("tags") or []
                if isinstance(taxonomy.get(t), dict) and (taxonomy[t].get("games") or 0) > 0]
        if tags:
            out.append((grp["id"], grp.get("label") or grp["id"], tags))
    return out


def tags_match(game, chosen, taxonomy):
    """the tag filter as on the site: a game must have one of the chosen tags of each group (OR within a
    group, AND across groups); nothing chosen: every game"""
    by_group = {}
    for t in chosen:
        by_group.setdefault(((taxonomy or {}).get(t) or {}).get("group") or "", set()).add(t)
    have = set(game.get("tags") or [])
    return all(have & ts for ts in by_group.values())


def filter_games(games, have, text="", tags=(), medal="All", installed_only=False, sort="Best first",
                 taxonomy=None):
    """the cards shown: the search's words all in the title, the summary or the tags' labels (and ids), the
    tag filter (tags_match), the medal, installed only; best first or by name"""
    words = text.lower().split()
    out = []
    for g in games:
        hay = " ".join([g["title"], g.get("summary") or ""] + list(g.get("tags") or []) +
                       [tag_label(taxonomy, t) for t in g.get("tags") or []]).lower()
        if any(w not in hay for w in words):
            continue
        if tags and not tags_match(g, tags, taxonomy):
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
    """the keys (Exec, Path, Terminal, X-Walnut-Game, ...) of a .desktop file's [Desktop Entry] group; None when unreadable"""
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
    is the entry's Name. An entry stamped X-Walnut-Game=<id> (ps3-games-integration's launchers) is
    only its own game's: games sharing an engine (dsda-doom: Freedoom, Doom, Heretic) don't list each
    other's; an unstamped one counts for any game that leads to it."""
    gid = game.get("id")
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
        if not argv or name in seen or (gid and e.get("X-Walnut-Game", gid) != gid):
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


def adopt_games(helper=HELPER, runner=subprocess.run):
    """`sudo -n HELPER adopt`, its output dropped: True when it ran and succeeded; never raises (the
    games stay as they are when sudo or the helper refuses)"""
    if not os.path.exists(helper):
        return False
    try:
        return runner(["sudo", "-n", helper, "adopt"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL, timeout=300).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


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


# ---- game data (walnut-data, the helper's import-data / install-repo-data / remove-data) -----------

WALNUT_DATA = "/usr/bin/walnut-data"
APT_CACHE = "/usr/bin/apt-cache"
LSBLK = "/usr/bin/lsblk"
DISC = "/dev/sr0"
EDITION_LABELS = {"shareware": "Shareware", "demo": "Demo", "full": "Full game", "expansion": "Expansion",
                  "music": "Music", "remaster": "Remaster"}
INSTALLER_RE = re.compile(r"(?i)^(.*\.(iso|img|cue|mdf|zip|exe)|gog_.*\.sh)$")
MAX_SOURCES = 40


def data_setup(game):
    ds = game.get("data_setup") if game else None
    return ds if isinstance(ds, dict) and isinstance(ds.get("gdp"), str) else None


def data_title(game):
    """the original commercial game whose files are imported ("Quake" for QuakeSpasm): [data_setup] original,
    else the game's title"""
    ds = data_setup(game) or {}
    return ds.get("original") or game["title"]


DATA_LABEL = "Add game files\u2026"
REPO_LABEL = "Install from Walnut licensed"


FREE_EDITIONS = ("shareware", "demo")


def data_note(game, required, full=False):
    """(tag, text) of the one line by the import button: REQUIRED (the game needs the files) or OPTIONAL
    (optional data; full: the full game in place of the free shareware or demo installed)"""
    name = data_title(game)
    if full:
        return "OPTIONAL:", "the full game from the original %s, in place of the free one" % name
    if required:
        return "REQUIRED:", "the files from your copy of the original %s" % name
    return "OPTIONAL:", "the original %s graphics and sound" % name


def optional_data_present(game, have):
    ds = data_setup(game) or {}
    return any(p in have for p in ds.get("optional") or [])


def data_actions(game, have, repo, importing=None):
    """the data buttons a game page shows, in order: "importing" (an import of this game runs: no button),
    "repo" (Install from Walnut licensed: the apt sources have one of its present_any names), "setup"
    (Add game files...: required data missing), "add" (the same button, optional data only, none installed)
    or "full" (the same button: only a free shareware or demo edition is installed and the game has a full
    one, whose import takes its place); [] when its data is there or it takes none"""
    ds = data_setup(game)
    if not ds:
        return []
    if importing == game["id"]:
        return ["importing"]
    if ds.get("required"):
        if data_present(game, have):
            eds = ds.get("editions") or {}
            mine = [p for p in ds.get("present_any") or ds["required"] if p in have]
            full = [p for p in ds["required"] if eds.get(p) and eds[p] not in FREE_EDITIONS]
            return ["full"] if full and all(eds.get(p) in FREE_EDITIONS for p in mine) else []
        any_of = [p for p in ds.get("present_any") or ds["required"] if isinstance(p, str)]
        return (["repo"] if any(p in repo for p in any_of) else []) + ["setup"]
    return [] if optional_data_present(game, have) else ["add"]


def parse_apt_policy(out):
    """the names `apt-cache policy NAME...` shows a candidate for that comes from a package source (an
    installed package is its own candidate, from /var/lib/dpkg/status only: not one)"""
    have, cur, cand, ver = set(), None, None, None
    for line in out.splitlines():
        m = re.match(r"(\S+):\s*$", line)
        if m:
            cur, cand, ver = m.group(1), None, None
            continue
        if cur is None:
            continue
        m = re.match(r"\s+Candidate:\s*(\S+)", line)
        if m:
            cand = m.group(1) if m.group(1) != "(none)" else None
            continue
        m = re.match(r"\s(?:\*\*\*|   )\s(\S+)\s+-?\d+\s*$", line)      # a version of the version table
        if m:
            ver = m.group(1)
            continue
        m = re.match(r"\s+-?\d+\s+(\S+)", line)                         # one of that version's sources
        if m and cand and ver == cand and m.group(1) != "/var/lib/dpkg/status":
            have.add(cur)
    return have


def repo_candidates(names, runner=None):
    """the names among names the configured apt sources have (the licensed suite's data); set() when
    apt-cache can't say"""
    if not names:
        return set()
    try:
        out = (runner or (lambda c: subprocess.run(c, capture_output=True, text=True, timeout=60).stdout))(
            [APT_CACHE, "policy"] + sorted(names))
    except (OSError, subprocess.SubprocessError):
        return set()
    return parse_apt_policy(out) & set(names)


def data_info(packages, runner=None):
    """{package: (edition, installed KB)} of the installed ones (dpkg-query; X-Walnut-Edition is empty for
    a package without it: the licensed suite's)"""
    if not packages:
        return {}
    cmd = ["dpkg-query", "-W", "-f", "${Package}\t${db:Status-Abbrev}\t${X-Walnut-Edition}\t${Installed-Size}\n"] + \
        sorted(packages)
    try:
        out = (runner or (lambda c: subprocess.run(c, capture_output=True, text=True).stdout))(cmd)
    except OSError:
        return {}
    info = {}
    for line in out.splitlines():
        f = line.split("\t")
        if len(f) == 4 and f[1][1:2] == "i":
            info[f[0]] = (f[2].strip(), int(f[3]) if f[3].strip().isdigit() else 0)
    return info


def edition_text(game, info):
    """what the installed data is: "Full game", "Shareware", "Full game, Music"; "" when the installed
    packages don't say (the licensed suite's)"""
    ds = data_setup(game) or {}
    names = [p for k in ("present_any", "required", "optional") for p in ds.get(k) or []]
    labels = []
    for p in dict.fromkeys(names):
        if p in info and info[p][0]:
            label = EDITION_LABELS.get(info[p][0], info[p][0].capitalize())
            if label not in labels:
                labels.append(label)
    if "Full game" in labels:
        labels = [l for l in labels if l not in ("Shareware", "Demo")]
    return ", ".join(labels)


def human_size(kb):
    if kb >= 1 << 20:
        return "%.1f GB" % (kb / (1 << 20))
    if kb >= 1024:
        return "%d MB" % round(kb / 1024)
    return "%d KB" % max(1, kb)


def remove_question(game, info):
    """the Remove confirmation, naming the imported data that goes with the game"""
    ds = data_setup(game) or {}
    names = [p for p in dict.fromkeys(p for k in ("required", "optional", "present_any") for p in ds.get(k) or [])
             if p in info]
    q = "Remove %s?" % game["title"]
    if names:
        kb = sum(info[p][1] for p in names)
        q += ("\n\nThis also removes your imported game data (%s: %s); you'd need the disc or your copy again "
              "to play it." % (", ".join(names), human_size(kb)))
    return q, names


def remove_data_question(game, info):
    """the Remove data confirmation (the game stays installed), naming the data packages and their size"""
    names = remove_question(game, info)[1]
    kb = sum(info[p][1] for p in names)
    return ("Remove your game data for %s (%s: %s)?\n\nThe game stays installed; you'd need the disc or your "
            "copy again to play it." % (game["title"], ", ".join(names), human_size(kb)), names)


def parse_lsblk_disc(text):
    """(label, mount point) of the disc in the drive from `lsblk -J -b -o NAME,LABEL,MOUNTPOINT,TYPE,SIZE
    /dev/sr0`, or None when there is no disc (or lsblk said nothing)"""
    try:
        devs = json.loads(text).get("blockdevices") or []
    except (ValueError, AttributeError):
        return None
    for d in devs:
        if not isinstance(d, dict) or d.get("type") != "rom":
            continue
        try:
            size = int(d.get("size") or 0)
        except (TypeError, ValueError):
            size = 0
        if size <= 0:
            return None
        return (d.get("label") or "", d.get("mountpoint") or None)
    return None


def disc_state(runner=None):
    if not os.path.exists(DISC):
        return None
    try:
        out = (runner or (lambda c: subprocess.run(c, capture_output=True, text=True, timeout=10).stdout))(
            [LSBLK, "-J", "-b", "-o", "NAME,LABEL,MOUNTPOINT,TYPE,SIZE", DISC])
    except (OSError, subprocess.SubprocessError):
        return None
    return parse_lsblk_disc(out)


def _dirs(path):
    try:
        return sorted(e.path for e in os.scandir(path) if e.is_dir() and not e.name.startswith("."))
    except OSError:
        return []


def find_sources(home, user, disc=None, media_roots=None):
    """[(label, path)]: where the player's game files may be, best first: the disc in the drive (disc:
    (label, mount point or None); not mounted: walnut-data mounts /dev/sr0), mounted media (/media/USER/*,
    /run/media/USER/*), installed copies (~/GOG Games/*, Steam's steamapps/common/*), then disc images,
    archives and installers (*.iso, *.img, *.cue of a BIN/CUE, *.mdf, *.zip, *.exe, gog_*.sh) in ~/Downloads and ~,
    newest first"""
    out, seen = [], set()

    def add(label, path):
        real = os.path.realpath(path)
        if real not in seen:
            seen.add(real)
            out.append((label, path))

    if disc is not None:
        label, mp = disc
        add("Disc in the drive" + (": " + label if label else ""), mp or DISC)
    roots = media_roots if media_roots is not None else ["/media/" + user, "/run/media/" + user]
    for r in roots:
        for d in _dirs(r):
            add("Disc or drive: " + os.path.basename(d), d)
    for d in _dirs(os.path.join(home, "GOG Games")):
        add("GOG: " + os.path.basename(d), d)
    for steam in (".steam/steam/steamapps/common", ".local/share/Steam/steamapps/common"):
        for d in _dirs(os.path.join(home, steam)):
            add("Steam: " + os.path.basename(d), d)
    files = []
    for d in (os.path.join(home, "Downloads"), home):
        try:
            for e in os.scandir(d):
                if e.is_file() and INSTALLER_RE.match(e.name):
                    files.append((e.stat().st_mtime, e.name, e.path))
        except OSError:
            pass
    for _, name, path in sorted(files, key=lambda f: (-f[0], f[1])):
        add(("Disc image: " if name.lower().endswith((".iso", ".img", ".cue", ".mdf")) else "File: ") + name, path)
    return out[:MAX_SOURCES]


def import_command(gid, source, packages=None, walnut_data=None):
    cmd = [walnut_data or WALNUT_DATA, "import"]
    for p in packages or []:
        cmd += ["-p", p]
    return cmd + [gid] + ([source] if source else [])


def parse_import_line(line):
    """one line of walnut-data's output: ('step', text) | ('file', name, bytes) | ('generated', path) |
    ('addon', package) | ('gdp-error', kind, text) | ('progress', percent, text) | ('log'|'error'|'status', text) | None"""
    line = line.rstrip("\n")
    if line.startswith("gdp "):
        kind, _, rest = line[4:].partition(" ")
        if kind == "step":
            return ("step", rest)
        if kind == "file":
            name, _, size = rest.rpartition(" ")
            try:
                return ("file", name, int(size))
            except ValueError:
                return ("file", rest, -1)
        if kind == "generated":
            return ("generated", rest)
        if kind == "error":
            k, _, text = rest.partition(" ")
            return ("gdp-error", k, text)
        return ("log", line[4:]) if line[4:].strip() else None
    if line.startswith("addon available "):
        pkg = line[len("addon available "):].strip()
        if pkg and " " not in pkg:
            return ("addon", pkg)
    if line.startswith("warning missing "):
        pkg, sep, why = line[len("warning missing "):].partition(": ")
        if pkg and " " not in pkg:
            return ("missing", pkg, why.strip() if sep else "")
    return parse_helper_line(line)


# what the imported packages are called and the fix file a download brings, for the notice after a
# partial import (any other package: its name, "its official patch files")
DATA_NAMES = {"tnt-wad": ("TNT: Evilution", "tnt31.wad"), "plutonia-wad": ("The Plutonia Experiment", "")}


def data_package_name(pkg):
    if pkg in DATA_NAMES:
        return DATA_NAMES[pkg][0]
    return re.sub(r"-(wad|data)$", "", pkg).replace("-", " ").strip().title() or pkg


def drop_alternatives(missing, alternatives, have):
    """missing [(package, reason)] without the packages of an alternatives group (data_setup: any one
    member satisfies it) of which another member is installed (have)"""
    out = []
    for p, why in missing:
        if any(isinstance(g, list) and p in g and any(m != p and m in have for m in g) for g in alternatives or []):
            continue
        out.append((p, why))
    return out


def addon_label(game, pkg):
    """the button that adds an optional package from the same source: 'Also add: Armagon (expansion)'"""
    ds = data_setup(game) or {}
    words = data_package_name(pkg).split()
    for r in ds.get("required") or []:       # drop the words it shares with the base ("Quake Rerelease Armagon")
        base = data_package_name(r).split()
        n = 0
        while n < len(base) and n < len(words) - 1 and base[n] == words[n]:
            n += 1
        if n:
            words = words[n:]
            break
    name = " ".join(words)
    ed = (ds.get("editions") or {}).get(pkg)
    return "Also add: %s%s" % (name, " (%s)" % ed if ed in ("expansion", "music") else "")


def partial_notice(download, missing):
    """the page's notice after an import that made some packages and not others: download is the packages that
    need their patch download, missing [(package, reason)] the others; "" when nothing is missing"""
    parts = []
    for pkg in download:
        fix = DATA_NAMES.get(pkg, ("", ""))[1]
        parts.append("%s needs its %s from the network: Try again with download" %
                     (data_package_name(pkg), "official fix file (%s)" % fix if fix else "official patch files"))
    if missing:
        parts.append("Not on this copy: " + ", ".join("%s (%s)" % (data_package_name(p), why or "not made")
                                                      for p, why in missing))
    return "\n".join(parts)


def import_error_kind(gdp_kind, errors):
    """the error kind of a failed import: game-data-packager's, else from walnut-data's own message
    (its free-space check, a busy lock)"""
    if gdp_kind:
        return gdp_kind
    last = (errors or [""])[-1].lower()
    if last.startswith("not enough space"):
        return "no-space"
    if last.startswith("busy"):
        return "busy"
    return "failed"


def unsuitable_parts(text):
    """(release, reason) of an unsuitable error's text 'FILE RELEASE: REASON' (the file has no spaces)"""
    head, sep, reason = (text or "").partition(": ")
    release = head.partition(" ")[2].strip()
    return (release, reason.strip()) if sep and release and reason.strip() else ("", (text or "").strip())


def import_error_message(kind, title, tail=(), errors=(), detail=""):
    """the plain English of an import's failure (kind: game-data-packager's error kinds, no-space and busy
    from walnut-data too)"""
    if kind == "not-found":
        return "No %s files found here." % title
    if kind == "unknown-version":
        return "This copy is a version Walnut doesn't know yet."
    if kind == "unsuitable":
        release, reason = unsuitable_parts(detail)
        if release:
            return "This copy is %s, which %s can't use: %s" % (release, title, reason)
        return "This copy is a release %s can't use%s" % (title, ": " + reason if reason else ".")
    if kind == "needs-download":
        return ("%s needs its official patch files: connect to the network and try again, or add the patch "
                "file." % title)
    if kind == "no-space":
        why = [e for e in errors if e.lower().startswith("not enough space")]
        return "Not enough space: " + why[-1].split(":", 1)[1].strip() + "." if why else \
            "Not enough space for %s's data: free some space and try again." % title
    if kind == "busy":
        return "Another import or install is running: try again when it has finished."
    lines = [l for l in tail if l][-3:]
    return "The import failed." + ("\n" + "\n".join(lines) if lines else "")


def import_lock_state(path):
    """(pid, game id) of the walnut-data import running now (it holds its lock), or None"""
    import fcntl
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    except OSError:
        return None
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
        except BlockingIOError:
            pid, gid = os.read(fd, 200).decode("utf-8", "replace").split()[:2]
            return int(pid), gid
        fcntl.flock(fd, fcntl.LOCK_UN)
        return None
    except (OSError, ValueError):
        return None
    finally:
        os.close(fd)


def data_cache_dir():
    return os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "walnut-data")


def elapsed_text(secs):
    secs = int(max(0, secs))
    return "%d:%02d:%02d" % (secs // 3600, secs // 60 % 60, secs % 60) if secs >= 3600 else \
        "%d:%02d" % (secs // 60, secs % 60)


class ImportView:
    """what the page shows of a running import, fed walnut-data's lines (parse_import_line)"""

    def __init__(self, gid, t0=None):
        self.gid, self.t0 = gid, t0 if t0 is not None else time.time()
        self.step, self.file, self.tail, self.errors = "Starting...", "", [], []
        self.gdp_kind, self.gdp_text, self.done = None, "", False
        self.addons = []                         # optional packages the source also holds (walnut-data: addon available)
        self.download, self.missing = [], []     # packages that need their download / (package, reason) not made

    def feed(self, ev):
        if not ev:
            return
        k = ev[0]
        if k == "step":
            self.step, self.file = ev[1][:1].upper() + ev[1][1:], ""
        elif k == "file":
            self.file = os.path.basename(ev[1].replace("//", "/")) + \
                (" (%s)" % human_size(max(1, ev[2] >> 10)) if ev[2] >= 0 else "")
        elif k == "generated":
            self.step, self.file = "Made " + os.path.basename(ev[1]), ""
        elif k == "gdp-error":
            self.gdp_kind, self.gdp_text = ev[1], ev[2]
            self.tail.append(ev[2])
            pkg, sep, _ = ev[2].partition(": ")
            if ev[1] == "needs-download" and sep and pkg and " " not in pkg and pkg not in self.download:
                self.download.append(pkg)
        elif k == "addon":
            if ev[1] not in self.addons:
                self.addons.append(ev[1])
        elif k == "missing":
            self.missing.append((ev[1], ev[2]))
            self.tail.append("not made: %s" % ev[1])
        elif k == "progress":
            self.step, self.file = ev[2][:1].upper() + ev[2][1:], ""
        elif k in ("log", "error"):
            self.tail.append(ev[1])
            if k == "error":
                self.errors.append(ev[1])
        self.tail = self.tail[-3:]

    def kind(self):
        return import_error_kind(self.gdp_kind, self.errors)


def screenshots(game):
    """[(label, relative path)]: every test's pictures, as rendered on the PS3"""
    out = []
    for t in game.get("tests", []):
        for f in t.get("ps3", []):
            out.append(("Tested %s%s" % (t.get("date", ""), ", " + t["launcher"] if t.get("launcher") else ""), f))
    return out


# ---- the pane's actions and glance, the Details view, the pad (PLAN 5.11) -----------------------

def main_action(game, installed, can_install, acts, n_launchers, idle, data_ok):
    """the pane's main button, which follows the game's state: (kind, label, enabled), None without a game.
    install: not installed (off for a game the packaged catalog doesn't have: can_install False);
    add-data: installed, its required data missing (data_actions "setup"); importing: an import of this game
    runs; play: Play, a menu of the launchers when there are several, off without one or while the required
    data is missing (data_ok False). Off while the helper or an import runs (idle False)."""
    if game is None:
        return None
    if "importing" in acts:
        return ("importing", "Importing…", False)
    if not installed:
        return ("install", "Install", bool(can_install and idle))
    if "setup" in acts:
        return ("add-data", DATA_LABEL, bool(idle))
    return ("play", "Play ▾" if n_launchers > 1 else "Play", bool(idle and n_launchers and data_ok is not False))


def side_actions(installed, acts, main, data_names, retry=False):
    """the smaller buttons under the main one, in order: add-data (Add game files... when it is not the main
    button: optional data, the full game in place of a free edition, or the data before the game is
    installed), repo (Install from Walnut licensed), retry (Try again with download), remove (installed),
    remove-data (game data installed: data_names, remove_question's)"""
    out = []
    if main != "add-data" and any(a in acts for a in ("setup", "add", "full")):
        out.append("add-data")
    if "repo" in acts:
        out.append("repo")
    if retry:
        out.append("retry")
    if installed:
        out.append("remove")
    if data_names:
        out.append("remove-data")
    return out


FPS_RE = re.compile(r"median (\d+(?:\.\d+)?) fps")


def glance_numbers(rating, size_kb=None):
    """the pane's key numbers [(value, label)]: the median frame rate (from what the frame rate aspect
    measured, else its score), the smoothness score and the game's installed size, each when known"""
    out = []
    asp = rating.get("aspects") or {}
    fr = asp.get("frame") or {}
    m = FPS_RE.search(fr.get("detail") or "")
    if m:
        out.append((m.group(1) + " fps", "median frame rate"))
    elif fr.get("score") is not None:
        out.append(("%d/100" % round(fr["score"]), "frame rate"))
    sm = asp.get("smooth") or {}
    if sm.get("score") is not None:
        out.append(("%d/100" % round(sm["score"]), "smoothness"))
    if size_kb:
        out.append((human_size(size_kb), "installed size"))
    return out


def parse_apt_sizes(out):
    """{package: installed KB} from `apt-cache show --no-all-versions NAME...`"""
    sizes, name = {}, None
    for line in out.splitlines():
        if line.startswith("Package:"):
            name = line.split(":", 1)[1].strip()
        elif line.startswith("Installed-Size:") and name:
            v = line.split(":", 1)[1].strip()
            if v.isdigit():
                sizes.setdefault(name, int(v))
        elif not line.strip():
            name = None
    return sizes


def package_sizes(names, runner=None):
    """{package: installed KB} of the names apt knows (the sources' candidate, or the installed one); {}
    when apt-cache can't say"""
    if not names:
        return {}
    try:
        out = (runner or (lambda c: subprocess.run(c, capture_output=True, text=True, timeout=120).stdout))(
            [APT_CACHE, "show", "--no-all-versions"] + sorted(names))
    except (OSError, subprocess.SubprocessError):
        return {}
    return parse_apt_sizes(out)


WALNUT_ACTIONS = ("install", "add-data", "play")


def parse_walnut_link(url):
    """(action, game id, launcher name or None) of a link the app's pages carry (tools/gamedb/build.sh --app):
    walnut://install/<id>, walnut://add-data/<id>, walnut://play/<id>, walnut://play/<id>?launcher=<name>;
    None for anything else (another scheme, an unknown action, a bad id)"""
    import urllib.parse
    try:
        u = urllib.parse.urlsplit(url)
    except ValueError:
        return None
    parts = [p for p in u.path.split("/") if p]
    if u.scheme != "walnut" or u.netloc not in WALNUT_ACTIONS or len(parts) != 1 or not ID_RE.match(parts[0]):
        return None
    launcher = urllib.parse.parse_qs(u.query).get("launcher", [None])[0] if u.netloc == "play" else None
    return u.netloc, parts[0], launcher or None


TABS = ("index.html", "test-runs/index.html", "technical/index.html", "guides/index.html")


def details_tabs(pages, gid):
    """the game's tab pages in the app's copy of the site (Overview, Test runs, Technical, Guides), those
    that exist; [] when the copy doesn't have the game (a game only on the website, or no copy installed)"""
    base = os.path.join(pages, "games", gid)
    return [os.path.join(base, t) for t in TABS if os.path.isfile(os.path.join(base, t))]


def tab_step(pages, current, d):
    """the tab page d steps (-1, 1) from the page current (a path under pages/games/<id>/; a page or picture
    that isn't a tab counts as the tab whose folder holds it), or None past either end or off a game"""
    rel = os.path.relpath(current, pages).replace(os.sep, "/")
    parts = rel.split("/")
    if len(parts) < 3 or parts[0] != "games":
        return None
    tabs = details_tabs(pages, parts[1])
    inside = "/".join(parts[2:])
    best, i = -1, None
    for n, t in enumerate(tabs):
        folder = os.path.dirname(os.path.relpath(t, os.path.join(pages, "games", parts[1])))
        if (not folder or inside == folder + "/index.html" or inside.startswith(folder + "/")) and len(folder) > best:
            best, i = len(folder), n
    if i is None or not 0 <= i + d < len(tabs):
        return None
    return tabs[i + d]


def link_target(url, pages):
    """what a link clicked in Details does: ("walnut", (action, id, launcher)) for the app's own links;
    ("close", None) for the copy's home page (All games: back to the app's list); ("tag", id) for a tag's
    page (the list filtered by that tag); ("page", url) for a page
    or picture of the app's copy; ("web", url) for another site or a page of the website the copy doesn't
    carry (its address on the website); ("ignore", url) for the rest (an unknown walnut:// link)"""
    import urllib.parse
    if url.startswith("walnut:"):
        w = parse_walnut_link(url)
        return ("walnut", w) if w else ("ignore", url)
    u = urllib.parse.urlsplit(url)
    if u.scheme in ("http", "https"):
        return ("web", url)
    if u.scheme != "file":
        return ("ignore", url)
    path = os.path.normpath(urllib.parse.unquote(u.path))
    root = os.path.normpath(pages)
    if not path.startswith(root + os.sep):
        return ("ignore", url)
    rel = os.path.relpath(path, root).replace(os.sep, "/")
    if rel == "index.html":
        return ("close", None)
    m = re.match(r"^tags/([a-z0-9][a-z0-9-]{0,63})/(index\.html)?$", rel)
    if m:
        return ("tag", m.group(1))
    if os.path.exists(path):
        return ("page", url)
    web = rel[:-len("index.html")] if rel == "index.html" or rel.endswith("/index.html") else rel
    return ("web", WEB + web + ("#" + u.fragment if u.fragment else ""))


def nav_pick(cur, cands, direction):
    """the index of the rectangle (x, y, w, h) among cands nearest to cur in a direction (up, down, left,
    right), or None when none lies that way. A candidate must start past cur's middle in that direction;
    the gap along the direction counts once, the gap across it (0 when the two overlap) twice, and the
    offset of the middles breaks ties"""
    cx, cy = cur[0] + cur[2] / 2, cur[1] + cur[3] / 2
    best, pick = None, None
    for i, (x, y, w, h) in enumerate(cands):
        mx, my = x + w / 2, y + h / 2
        if direction in ("left", "right"):
            if (direction == "right" and (mx <= cx or x + 2 < cx)) or (direction == "left" and (mx >= cx or x + w - 2 > cx)):
                continue
            along = max(0, x - (cur[0] + cur[2])) if direction == "right" else max(0, cur[0] - (x + w))
            across = max(0, y - (cur[1] + cur[3]), cur[1] - (y + h))
            tie = abs(my - cy)
        else:
            if (direction == "down" and (my <= cy or y + 2 < cy)) or (direction == "up" and (my >= cy or y + h - 2 > cy)):
                continue
            along = max(0, y - (cur[1] + cur[3])) if direction == "down" else max(0, cur[1] - (y + h))
            across = max(0, x - (cur[0] + cur[2]), cur[0] - (x + w))
            tie = abs(mx - cx)
        score = (along + 2 * across, tie)
        if best is None or score < best:
            best, pick = score, i
    return pick


def ui_scale(screen_h, env=None):
    """how much bigger the text and the pane are than the app's own sizes: 1 at every resolution (the
    user, 2026-10-31: no bigger text unasked); WALNUT_GAMES_SCALE (0.75-3) makes them bigger or smaller"""
    v = env if env is not None else os.environ.get("WALNUT_GAMES_SCALE")
    if v:
        try:
            return max(0.75, min(3.0, float(v)))
        except ValueError:
            pass
    return 1.0


# the gamepad's buttons the X pointer driver leaves free (ps3-desktop's 40-joystick-pointer.conf: the
# sticks move the pointer, Cross is a left click, Circle a right click): evdev key codes and hat axes
PAD_KEYS = {310: "l1", 311: "r1", 308: "square", 544: "up", 545: "down", 546: "left", 547: "right"}
PAD_HATS = {(16, -1): "left", (16, 1): "right", (17, -1): "up", (17, 1): "down"}
PAD_STALE_S = 0.5      # a press older than this was queued while the app was frozen (game mode): dropped


def pad_devices(text):
    """the event devices (/dev/input/eventN) of the joysticks in /proc/bus/input/devices"""
    out = []
    for line in text.splitlines():
        if line.startswith("H: Handlers="):
            hs = line.split("=", 1)[1].split()
            if any(h.startswith("js") for h in hs):
                out += ["/dev/input/" + h for h in hs if re.match(r"^event\d+$", h)]
    return out


def pad_press(etype, code, value):
    """the pad action of one input event (type, code, value), or None: a key going down (EV_KEY 1, value 1)
    in PAD_KEYS, or a hat (EV_ABS 3) going to one side"""
    if etype == 1 and value == 1:
        return PAD_KEYS.get(code)
    if etype == 3:
        return PAD_HATS.get((code, value))
    return None


class PadReader:
    """a thread reading the gamepads' event devices (no grab: the X pointer driver and the games still get
    them) for the buttons the pointer leaves free (pad_press); each press is handed to put(name). Devices
    are looked for again every 3 s (a pad plugged in or paired later); one that fails is closed. Presses
    older than PAD_STALE_S are dropped (the kernel queued them while game mode froze the app)."""

    def __init__(self, put, devices="/proc/bus/input/devices"):
        self.put, self.devices, self.fds = put, devices, {}
        import struct
        self.ev = struct.Struct("@llHHi")      # struct input_event: a timeval, type, code, value

    def scan(self):
        try:
            with open(self.devices) as f:
                paths = pad_devices(f.read())
        except OSError:
            paths = []
        for p in paths:
            if p not in self.fds:
                try:
                    self.fds[p] = os.open(p, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC)
                except OSError:
                    pass

    def run(self):
        import select
        last = 0.0
        while True:
            if time.monotonic() - last > 3:
                self.scan()
                last = time.monotonic()
            if not self.fds:
                time.sleep(3)
                continue
            try:
                ready, _, _ = select.select(list(self.fds.values()), [], [], 3)
            except (OSError, ValueError):
                ready = []
            for fd in ready:
                try:
                    data = os.read(fd, self.ev.size * 64)
                except BlockingIOError:
                    continue
                except OSError:
                    data = b""
                if not data:
                    for p, f in list(self.fds.items()):
                        if f == fd:
                            del self.fds[p]
                            os.close(fd)
                    continue
                for i in range(0, len(data) - self.ev.size + 1, self.ev.size):
                    sec, usec, etype, code, value = self.ev.unpack_from(data, i)
                    name = pad_press(etype, code, value)
                    if name and abs(time.time() - (sec + usec / 1e6)) < PAD_STALE_S:
                        self.put(name)

    def start(self):
        threading.Thread(target=self.run, daemon=True).start()
        return self


def external_game(url):
    """the game id of the one link Walnut Games takes from outside (the website's "Open in Walnut Games",
    through xdg-open and the .desktop file's x-scheme-handler/walnut): walnut://game/<id>; None for
    anything else. install, add-data and play links are the Details view's own and never come in this way;
    the id must still be in the catalog (App.open_game)"""
    import urllib.parse
    try:
        u = urllib.parse.urlsplit(url)
    except ValueError:
        return None
    parts = [p for p in u.path.split("/") if p]
    if u.scheme != "walnut" or u.netloc != "game" or len(parts) != 1 or u.query or u.fragment:
        return None
    return parts[0] if ID_RE.match(parts[0]) else None


def instance_paths(runtime=None):
    """(lock, hand-off file) of the one running Walnut Games, in $XDG_RUNTIME_DIR/walnut-games/"""
    d = os.path.join(runtime or os.environ.get("XDG_RUNTIME_DIR") or cache_dir(), "walnut-games")
    return os.path.join(d, "lock"), os.path.join(d, "open")


def claim_instance(lock):
    """the lock's descriptor when no other Walnut Games runs (held, not inherited, while this one runs);
    None when one does (or the lock can't be made: then this one runs on its own)"""
    import fcntl
    try:
        os.makedirs(os.path.dirname(lock), mode=0o700, exist_ok=True)
        fd = os.open(lock, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    except OSError:
        return -1
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return fd
    except OSError:
        os.close(fd)
        return None


def hand_off(path, gid):
    """tell the running Walnut Games to come forward (on game gid, when one is given): one line in the
    hand-off file, written whole (it reads and removes it)"""
    with open(path + ".part", "w") as f:
        f.write((gid or "-") + "\n")
    os.replace(path + ".part", path)


def take_hand_off(path):
    """what another start handed over: a game id, "" (just come forward), or None (nothing); read once"""
    try:
        with open(path) as f:
            text = f.read(200).strip()
        os.remove(path)
    except OSError:
        return None
    return text if ID_RE.match(text) else ""


def rounded_points(x1, y1, x2, y2, r):
    """polygon points for create_polygon(smooth=True) of a rounded rectangle"""
    return [x1 + r, y1, x1 + r, y1, x2 - r, y1, x2 - r, y1, x2, y1, x2, y1 + r, x2, y1 + r, x2, y2 - r, x2, y2 - r,
            x2, y2, x2 - r, y2, x2 - r, y2, x1 + r, y2, x1 + r, y2, x1, y2, x1, y2 - r, x1, y2 - r, x1, y1 + r,
            x1, y1 + r, x1, y1]


# ---- the window -------------------------------------------------------------------------------

CARD_W, THUMB_W, THUMB_H, GAP = 264, 256, 144, 14
PANE_W = 420        # the pane's width at scale 1 (ui_scale)
GLANCE_SHOTS = 4    # screenshot thumbnails in the pane
# the widgets the d-pad and the arrow keys move between (Tk's class names)
FOCUS_CLASSES = {"TButton", "TCheckbutton", "TCombobox", "TEntry", "Button", "Checkbutton", "Radiobutton"}


class App:
    def __init__(self, root, data=None, runtime_dir=None, helper=HELPER, online=None, pad=False, hand_off_path=None,
                 start_game=None):
        self.root, self.data = root, data or DATA
        self.pages = os.path.join(self.data, "pages")   # the app's copy of the site (Details)
        self.runtime_dir = runtime_dir if runtime_dir is not None else \
            (os.path.join(os.environ["XDG_RUNTIME_DIR"], "ps3-game-menu") if os.environ.get("XDG_RUNTIME_DIR") else None)
        self.helper = helper
        self.online = (not os.environ.get("WALNUT_GAMES_OFFLINE")) if online is None else online
        packaged = load_catalog_obj(self.data)
        self.pkg_ids = {g["id"] for g in packaged["games"]}   # what the root helper can install
        self.pkg_data = {g["id"] for g in packaged["games"] if data_setup(g)}   # and import data for
        self.cat = choose_catalog(packaged, read_cached_catalog() if self.online else None)
        self.games = self.cat["games"]
        self.by_id = {g["id"]: g for g in self.games}
        self.refreshing = False
        self._fetching = set()
        self.have = set()
        self.shown, self.selected = [], None
        self.thumbs, self.cards = {}, {}
        self.busy = None                       # (id, "install"|"remove") while the helper runs
        self.imp = None                        # ImportView of the walnut-data import running (or just ended)
        self.imp_source = {}                   # game id -> the source of its last import (Try again)
        self.addons = {}                       # game id -> packages the last import's source also holds (Also add)
        self.partial = {}                      # game id -> (packages needing a download, the notice) of a partial import
        self.retry = {}                        # game id -> the source to import again with downloads on
        self.imp_before = set()                # the Play entries before the import (what it added)
        self.repo = set()                      # data names the apt sources have (the licensed suite)
        self.info = {}                         # installed data packages -> (edition, KB)
        self.sizes = {}                        # package -> installed KB (apt-cache show, in a thread)
        self.chooser = None
        self.menu = None                       # Play's menu of launchers while it is open
        self.dv = None                         # the Details view while it is open (open_details)
        self.main_kind = None                  # what the main button does now (main_action)
        self.play_cmds = []
        self._manifests = {}                   # (game id, installed packages) -> dpkg -L
        self.events = queue.Queue()
        self.glance_imgs = []
        self._style()
        self._build()
        self.refresh_installed()
        self.apply_filters()
        self.root.after(100, self._poll)
        if self.online:
            self.fetch_catalog(manual=False)
        self.check_repo()
        self.resume_import()
        threading.Thread(target=adopt_games, args=(self.helper,), daemon=True).start()
        if pad and os.environ.get("WALNUT_GAMES_PAD") != "off":
            PadReader(lambda name: self.events.put(("pad", name))).start()
        self.root.after(200, lambda: self.canvas.focus_set() if self.dv is None else None)
        self.hand_off_path = hand_off_path     # another start's walnut://game/<id> comes in here (main)
        self._hand_off_n = 0
        if start_game:
            self.root.after(300, lambda: self.open_game(start_game))

    def open_game(self, gid):
        """walnut://game/<id> from outside: the window comes forward on that game, its Details when the app's
        copy has it, else its card and pane; an id the catalog doesn't have only brings the window forward"""
        try:
            self.root.deiconify()
            self.root.lift()
            self.root.focus_force()
        except tk.TclError:
            pass
        if not gid or gid not in self.by_id:
            if gid:
                print("walnut-games: no game %r in the catalog" % gid, file=sys.stderr)
            return False
        self.close_details()
        if self.menu is not None or self.chooser is not None:
            self.back()
        self.select(gid)
        self._see_card(gid)
        self.open_details(gid, quiet=True)
        return True

    # -- look
    def _style(self):
        r = self.root
        r.configure(bg=BG)
        self.scale = s = ui_scale(r.winfo_screenheight())
        fams = set(tkfont.families(r))
        fam = "DejaVu Sans" if "DejaVu Sans" in fams else tkfont.nametofont("TkDefaultFont").actual("family")

        def font(size, bold=False):
            return tkfont.Font(family=fam, size=round(size * s), weight="bold" if bold else "normal")
        self.f = {"base": font(10), "small": font(9), "bold": font(10, True), "card": font(11, True),
                  "pill": font(9, True), "title": font(17, True), "big": font(13, True), "main": font(14, True),
                  "num": font(15, True)}
        for n in ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont", "TkFixedFont"):
            try:
                tkfont.nametofont(n).configure(family=fam, size=round(10 * s))
            except tk.TclError:
                pass
        # the cards' rows follow the text's size
        self.pill_h = self.f["pill"].metrics("linespace") + 8
        self.card_title_y = THUMB_H + 12 + self.pill_h + 8 + self.f["card"].metrics("linespace") // 2
        self.card_cat_y = self.card_title_y + self.f["card"].metrics("linespace") // 2 + 6 + \
            self.f["small"].metrics("linespace") // 2
        self.card_h = self.card_cat_y + self.f["small"].metrics("linespace") // 2 + 12
        pad = round(6 * s)
        st = ttk.Style(r)
        st.theme_use("clam")
        st.configure(".", background=BG, foreground=TEXT, fieldbackground=ENTRY, bordercolor=BORDER,
                     lightcolor=BORDER, darkcolor=BORDER, troughcolor=PANEL, font=self.f["base"],
                     focuscolor=ACCENT_LIGHT, insertcolor=TEXT, selectbackground=ACCENT, selectforeground="#14304d")
        st.configure("TFrame", background=BG)
        st.configure("Panel.TFrame", background=PANEL)
        st.configure("TLabel", background=BG, foreground=TEXT)
        st.configure("Panel.TLabel", background=PANEL)
        st.configure("Muted.TLabel", foreground=MUTED)
        st.configure("Title.TLabel", font=self.f["title"])
        st.configure("TEntry", fieldbackground=ENTRY, foreground=TEXT, bordercolor=BORDER, padding=pad)
        # focus shows: a light border (and the button's dashed ring) on whatever the pad or the keys are on
        st.map("TEntry", bordercolor=[("focus", ACCENT_LIGHT)], lightcolor=[("focus", ACCENT_LIGHT)],
               darkcolor=[("focus", ACCENT_LIGHT)])
        st.configure("TCombobox", fieldbackground=ENTRY, background=BORDER, foreground=TEXT, arrowcolor=MUTED,
                     bordercolor=BORDER, padding=pad - 2, arrowsize=round(12 * s))
        st.map("TCombobox", fieldbackground=[("focus", BORDER), ("readonly", ENTRY)], foreground=[("readonly", TEXT)],
               bordercolor=[("focus", ACCENT_LIGHT)], lightcolor=[("focus", ACCENT_LIGHT)],
               darkcolor=[("focus", ACCENT_LIGHT)], arrowcolor=[("active", ACCENT_LIGHT), ("focus", ACCENT_LIGHT)])
        r.option_add("*TCombobox*Listbox.background", ENTRY)
        r.option_add("*TCombobox*Listbox.foreground", TEXT)
        r.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
        r.option_add("*TCombobox*Listbox.selectForeground", "#14304d")
        r.option_add("*TCombobox*Listbox.font", self.f["base"])
        st.configure("TCheckbutton", background=BG, foreground=TEXT, indicatorbackground=ENTRY,
                     indicatorforeground=ACCENT_LIGHT, padding=pad - 2, indicatorsize=round(12 * s),
                     focusthickness=2)
        st.map("TCheckbutton", background=[("focus", BORDER), ("active", BG)], indicatorbackground=[("selected", ENTRY)],
               foreground=[("active", "#ffffff"), ("focus", "#ffffff")])
        st.configure("Filter.TCheckbutton", background=PANEL, padding=round(4 * s))
        st.map("Filter.TCheckbutton", background=[("focus", BORDER), ("active", PANEL)],
               foreground=[("active", "#ffffff"), ("focus", "#ffffff")])
        st.configure("TButton", background=BORDER, foreground=TEXT, bordercolor=BORDER, padding=(round(12 * s), pad),
                     relief="flat", focusthickness=2)
        st.map("TButton", background=[("disabled", PANEL), ("active", "#26507c"), ("focus", "#26507c")],
               foreground=[("disabled", "#6c7d90")], bordercolor=[("focus", ACCENT_LIGHT)],
               lightcolor=[("focus", ACCENT_LIGHT)], darkcolor=[("focus", ACCENT_LIGHT)])
        accent = dict(background=[("disabled", PANEL), ("pressed", ACCENT_DEEP), ("active", ACCENT_LIGHT),
                                  ("focus", ACCENT_LIGHT)],
                      foreground=[("disabled", "#6c7d90")], bordercolor=[("focus", TEXT)],
                      lightcolor=[("focus", TEXT)], darkcolor=[("focus", TEXT)])
        st.configure("Accent.TButton", background=ACCENT, foreground="#14304d", font=self.f["bold"],
                     focuscolor="#14304d")
        st.map("Accent.TButton", **accent)
        # the pane's main button: Install, Add game files..., Play
        st.configure("Main.TButton", background=ACCENT, foreground="#14304d", font=self.f["main"],
                     padding=(round(20 * s), round(10 * s)), focuscolor="#14304d")
        st.map("Main.TButton", **accent)
        st.configure("Source.TButton", background=ENTRY, foreground=TEXT, anchor="w", padding=(12, 8), justify="left")
        st.map("Source.TButton", background=[("active", BORDER), ("focus", BORDER)])
        st.configure("Link.TButton", background=PANEL, foreground=LINK, padding=(round(8 * s), round(4 * s)))
        st.map("Link.TButton", foreground=[("active", LINK_HOVER), ("focus", LINK_HOVER)],
               background=[("active", PANEL), ("focus", BORDER)], bordercolor=[("focus", ACCENT_LIGHT)])
        # a screenshot thumbnail in the pane (opens it in Details)
        st.configure("Shot.TButton", background=ENTRY, padding=2, bordercolor=PANEL, focusthickness=0)
        st.map("Shot.TButton", background=[("focus", ACCENT_LIGHT), ("active", ACCENT)],
               bordercolor=[("focus", ACCENT_LIGHT)])
        st.configure("Horizontal.TProgressbar", background=ACCENT, troughcolor=ENTRY, bordercolor=BORDER,
                     lightcolor=ACCENT, darkcolor=ACCENT)
        for o in ("Vertical", "Horizontal"):
            st.configure(o + ".TScrollbar", background=BORDER, troughcolor=PANEL, bordercolor=PANEL,
                         arrowcolor=MUTED, lightcolor=BORDER, darkcolor=BORDER, arrowsize=round(14 * s))
            st.map(o + ".TScrollbar", background=[("active", "#26507c")])

    def _build(self):
        r = self.root
        r.title("Walnut Games")
        w, h = min(1480, r.winfo_screenwidth() - 40), min(900, r.winfo_screenheight() - 80)
        if self.scale > 1:      # a TV: the whole screen
            w, h = r.winfo_screenwidth(), r.winfo_screenheight()
        r.geometry("%dx%d" % (w, h))
        r.minsize(900, 560)
        if self.scale > 1:
            try:
                r.attributes("-zoomed", True)
            except tk.TclError:
                pass
        # top bar
        bar = ttk.Frame(r, style="Panel.TFrame", padding=(12, 10))
        bar.pack(fill="x")
        self.bar = bar
        self.v_text, self.v_medal = tk.StringVar(), tk.StringVar(value="All")
        self.tag_filter = set()                # the tags ticked in Filters (tags_match)
        self.filters_win = None
        self.v_sort = tk.StringVar(value="Best first")
        self.v_inst = tk.BooleanVar()
        ttk.Label(bar, text="Walnut Games", font=self.f["big"], style="Panel.TLabel").pack(side="left", padx=(0, 16))
        self.search = ttk.Entry(bar, textvariable=self.v_text, width=22, font=self.f["base"])
        self.search.pack(side="left", padx=(0, 10))
        # the tag filter: a panel of the catalog's tag groups (open_filters)
        self.b_filters = ttk.Button(bar, text="Filters", command=self.open_filters)
        self.b_filters.pack(side="left", padx=(0, 4))
        for var, vals, wd, name in ((self.v_medal, ["All"] + medal_choices(self.games), 10, "Medal"),
                                    (self.v_sort, ["Best first", "Name"], 10, "Sort")):
            ttk.Label(bar, text=name, style="Muted.TLabel", background=PANEL).pack(side="left", padx=(8, 2))
            cb = ttk.Combobox(bar, textvariable=var, values=vals, state="readonly", width=wd, font=self.f["base"])
            cb.pack(side="left", padx=4)
            cb.bind("<<ComboboxSelected>>", lambda e: self.apply_filters())
            # up and down move between the controls (the pad's d-pad); Enter opens the list
            for key in ("Up", "Down"):
                cb.bind("<" + key + ">", lambda e, k=key: self._key_nav(e, k))
            cb.bind("<Return>", lambda e: (e.widget.tk.call("ttk::combobox::Post", e.widget), "break")[1])
        for var, text in ((self.v_inst, "Installed only"),):
            ttk.Checkbutton(bar, text=text, variable=var, command=self.apply_filters, style="TCheckbutton").pack(
                side="left", padx=8)
        self.count = ttk.Label(bar, text="", style="Muted.TLabel", background=PANEL)
        self.count.pack(side="right")
        self.b_refresh = ttk.Button(bar, text="Refresh", command=self.refresh)
        self.b_refresh.pack(side="right", padx=(0, 12))
        self.v_text.trace_add("write", lambda *a: self.apply_filters())
        # body: the cards, then the pane
        # a draggable divider between the cards and the pane (the user widens the pane)
        body = tk.PanedWindow(r, orient="horizontal", bg=BORDER, sashwidth=6, sashrelief="flat",
                              borderwidth=0, showhandle=False, opaqueresize=True)
        body.pack(fill="both", expand=True)
        self.body = body
        left = ttk.Frame(body)
        self.pane_w = round(PANE_W * self.scale)
        self.details = ttk.Frame(body, style="Panel.TFrame", padding=round(14 * self.scale))
        body.add(left, minsize=320, stretch="always")
        body.add(self.details, minsize=round(360 * self.scale), width=self.pane_w, stretch="never")
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
            self.canvas.bind("<" + key + ">", lambda e, dx=dx, dy=dy, k=key: self._card_key(e, k, dx, dy))
        self.canvas.bind("<Button-1>", lambda e: self.canvas.focus_set())
        self.canvas.bind("<FocusIn>", lambda e: self._mark_selected())
        self.canvas.bind("<FocusOut>", lambda e: self._mark_selected())
        r.bind("<Control-f>", lambda e: (self.search.focus_set(), "break")[1])
        # the pad and the keys (docs/GAMEDB.md "The desktop app"): arrows move the focus, Enter presses the
        # focused button, Escape and Circle (the pointer's right click) go back
        for key in ("Up", "Down", "Left", "Right"):
            r.bind_all("<" + key + ">", lambda e, k=key: self._key_nav(e, k))
        r.bind_all("<Prior>", lambda e: self._details_scroll(-1, "pages") if self.dv else None)
        r.bind_all("<Next>", lambda e: self._details_scroll(1, "pages") if self.dv else None)
        r.bind_all("<Return>", self._key_press)
        r.bind_all("<KP_Enter>", self._key_press)
        r.bind_all("<Escape>", lambda e: self.back())
        r.bind_all("<Button-3>", lambda e: self.back())
        r.bind_all("<ButtonPress-1>", self._click_focus, add="+")
        self._build_details()

    def _build_details(self):
        """the pane: the game's name and medal, the actions (one main button that follows the state, smaller
        ones under it, the import's and the helper's progress), then a quick glance (key numbers, the data's
        edition, screenshot thumbnails, links and Details). One column of rows (grid): a row that doesn't
        apply is taken out and comes back in its place."""
        d = self.details
        self.g = None
        wrap = self.pane_w - round(32 * self.scale)
        self.wrap = wrap
        d.columnconfigure(0, weight=1)
        rows = {}

        def row(name, widget, **kw):
            rows[name] = widget
            widget.grid(row=len(rows), column=0, sticky=kw.pop("sticky", "w"), **kw)
            return widget
        self.d_title = row("title", ttk.Label(d, text="", style="Title.TLabel", background=PANEL, wraplength=wrap))
        self.d_medal = row("medal", tk.Canvas(d, height=self.f["big"].metrics("linespace") + 14, bg=PANEL,
                                              highlightthickness=0, width=wrap), pady=(4, 0))
        self.d_note = row("medal_note", ttk.Label(d, text="", foreground=TEXT, background=PANEL,
                                                  wraplength=wrap, justify="left", font=self.f["small"]))
        self.d_sum = row("summary", ttk.Label(d, text="", style="Muted.TLabel", background=PANEL, wraplength=wrap,
                                              justify="left"), pady=(4, 10))
        # the main button, then what the data button adds (one line), the smaller buttons, the add-ons
        self.b_main = row("main", ttk.Button(d, text="Install", style="Main.TButton", command=self.main_pressed),
                          sticky="ew", pady=(0, 6))
        self.b_install = self.b_play = self.b_main      # one button now (the tests' and older names)
        self.b_note = ttk.Frame(d, style="Panel.TFrame")
        self.note_tag = ttk.Label(self.b_note, text="", font=self.f["bold"], foreground=TEXT, background=PANEL)
        self.note_text = ttk.Label(self.b_note, text="", style="Muted.TLabel", background=PANEL, wraplength=wrap - 120,
                                   justify="left", font=self.f["small"])
        self.note_tag.pack(side="left", anchor="n", padx=(0, 6))
        self.note_text.pack(side="left", fill="x")
        rows["note_main"] = None       # its place under the main button; "note_side": under the smaller ones
        self.side_row = row("side", ttk.Frame(d, style="Panel.TFrame"), sticky="ew")
        rows["note_side"] = None
        self.side_rows = [ttk.Frame(self.side_row, style="Panel.TFrame") for _ in range(3)]
        for f in self.side_rows:
            f.pack(side="top", anchor="w")
        self.b_data = ttk.Button(self.side_row, text=DATA_LABEL, command=self.open_chooser)
        self.b_repo = ttk.Button(self.side_row, text=REPO_LABEL, command=lambda: self.run_helper("install-repo-data"))
        self.b_retry = ttk.Button(self.side_row, text="Try again with download", style="Accent.TButton",
                                  command=self.retry_download)
        self.b_remove = ttk.Button(self.side_row, text="Remove", command=lambda: self.run_helper("remove"))
        self.b_remove_data = ttk.Button(self.side_row, text="Remove data", command=lambda: self.run_helper("remove-data"))
        self.side_buttons = {"add-data": self.b_data, "repo": self.b_repo, "retry": self.b_retry,
                             "remove": self.b_remove, "remove-data": self.b_remove_data}
        self.addon_row = row("addons", ttk.Frame(d, style="Panel.TFrame"), sticky="ew")
        self.addon_buttons = []
        self.partial_label = row("partial", ttk.Label(d, text="", style="Muted.TLabel", background=PANEL,
                                                      wraplength=wrap, justify="left", font=self.f["small"]),
                                 pady=(0, 6))
        self.d_reason = row("reason", ttk.Label(d, text="", style="Muted.TLabel", background=PANEL, wraplength=wrap,
                                                justify="left"))
        # the activity strip (status, progress, an import's details): only while something happens
        self.activity = row("activity", ttk.Frame(d, style="Panel.TFrame"), sticky="ew")
        self.d_status = ttk.Label(self.activity, text="", style="Muted.TLabel", background=PANEL, wraplength=wrap,
                                  justify="left")
        self.d_status.pack(side="top", anchor="w", pady=(4, 2))
        self.progress = ttk.Progressbar(self.activity, maximum=100, mode="determinate")
        self.imp_detail = ttk.Label(self.activity, text="", style="Muted.TLabel", background=PANEL, wraplength=wrap,
                                    justify="left", font=self.f["small"])
        self._activity_after = None
        # the glance
        self.d_numbers = row("numbers", ttk.Frame(d, style="Panel.TFrame"), sticky="ew", pady=(12, 4))
        self.d_info = row("info", ttk.Label(d, text="", style="Muted.TLabel", background=PANEL, wraplength=wrap,
                                            justify="left"), pady=(2, 8))
        self.d_thumbs = row("thumbs", ttk.Frame(d, style="Panel.TFrame"), sticky="w")
        self.shot_w = (wrap - 8) // 2
        self.shot_h = self.shot_w * 9 // 16
        self.links = row("links", ttk.Frame(d, style="Panel.TFrame"), sticky="ew", pady=(10, 0))
        self.b_details = ttk.Button(self.links, text="Details", command=self.open_details)
        self.b_details.pack(side="top", anchor="w", pady=(0, 4))
        weblinks = ttk.Frame(self.links, style="Panel.TFrame")
        weblinks.pack(side="top", anchor="w")
        ttk.Button(weblinks, text="Technical notes", style="Link.TButton", command=self.open_technical).pack(side="left")
        ttk.Button(weblinks, text="Open on the web", style="Link.TButton", command=self.open_web).pack(side="left")
        self.b_home = ttk.Button(weblinks, text="Home page", style="Link.TButton", command=self.open_home)
        self.rows = rows
        self.show(None)

    def _row(self, name, on, widget=None, **kw):
        """show or hide one row of the pane, in its place"""
        w = widget or self.rows[name]
        if on:
            if name in ("note_main", "note_side"):
                # its own grid row, kept free for it (row(): the rows are numbered in their order)
                w.grid(row=list(self.rows).index(name) + 1, column=0, sticky="w", pady=(0, 6), **kw)
            elif not w.winfo_manager():
                w.grid()
        else:
            if name in ("note_main", "note_side"):
                w.grid_forget()
            else:
                w.grid_remove()

    # -- cards
    def apply_filters(self, *a):
        self.shown = filter_games(self.games, self.have, self.v_text.get(), self.tag_filter, self.v_medal.get(),
                                  self.v_inst.get(), self.v_sort.get(), taxonomy=self.cat.get("tags"))
        self.b_filters.configure(text="Filters (%d)" % len(self.tag_filter) if self.tag_filter else "Filters")
        if self.filters_win is not None:
            self.filters_count.configure(text="%d of %d games" % (len(self.shown), len(self.games)))
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
                    if self.online and g.get("thumb", True):
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
            y = GAP + (i // cols) * (self.card_h + GAP)
            self.draw_card(g, x, y)
        rows = (len(self.shown) + cols - 1) // cols
        c.configure(scrollregion=(0, 0, width, GAP + rows * (self.card_h + GAP)))
        if not self.shown:
            c.create_text(width // 2, 80, text="No game matches.", fill=MUTED, font=self.f["big"])
        if reset:
            c.yview_moveto(0)

    def draw_card(self, g, x, y):
        c, tag = self.canvas, "g_" + g["id"].replace("-", "_")
        sel = g["id"] == self.selected
        c.create_polygon(rounded_points(x, y, x + CARD_W, y + self.card_h, 12), smooth=True, fill=PANEL,
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
        c.create_polygon(rounded_points(px, py, px + tw, py + self.pill_h, 10), smooth=True, fill=fill, outline="",
                         tags=(tag,))
        c.create_text(px + tw / 2, py + self.pill_h / 2, text=label, fill=pill_text_color(fill), font=self.f["pill"],
                      tags=(tag,))
        if is_installed(g, self.have):
            c.create_text(x + CARD_W - 10, py + self.pill_h / 2, text="Installed", fill=TEXT, anchor="e",
                          font=self.f["small"], tags=(tag,))
        c.create_text(x + 10, y + self.card_title_y, text=self.elide(g["title"], self.f["card"], CARD_W - 20),
                      fill=TEXT, anchor="w", font=self.f["card"], tags=(tag,))
        c.create_text(x + 10, y + self.card_cat_y, text=self.elide(genre_text(g, self.cat.get("tags")),
                                                                   self.f["small"], CARD_W - 20),
                      fill=MUTED, anchor="w", font=self.f["small"], tags=(tag,))
        self.cards[g["id"]] = (x, y)
        c.tag_bind(tag, "<Button-1>", lambda e, i=g["id"]: self.select(i))
        c.tag_bind(tag, "<Double-Button-1>", lambda e, i=g["id"]: self.open_details(i))
        c.tag_bind(tag, "<Enter>", lambda e, t=tag: c.itemconfigure(t + "_bg", fill=CARD_HOVER))
        c.tag_bind(tag, "<Leave>", lambda e, t=tag: c.itemconfigure(t + "_bg", fill=PANEL))
        if sel:
            self._mark_selected()

    @staticmethod
    def elide(text, font, width):
        """text cut to one line of width pixels, with an ellipsis when it was cut"""
        if font.measure(text) <= width:
            return text
        while text and font.measure(text + "\u2026") > width:
            text = text[:-1]
        return text.rstrip() + "\u2026"

    def _wheel(self, d):
        self.canvas.yview_scroll(d * 3, "units")

    def _card_key(self, e, key, dx, dy):
        """an arrow on the cards: the next card; past the grid's edge, the focus leaves the cards (the
        top bar above, the pane on the right)"""
        ids = [g["id"] for g in self.shown]
        i = ids.index(self.selected) if self.selected in ids else -1
        cols = max(1, getattr(self, "cols", 1))
        if i >= 0 and ((dx < 0 and i % cols == 0) or (dx > 0 and (i % cols == cols - 1 or i == len(ids) - 1)) or
                       (dy < 0 and i < cols) or (dy > 0 and i + cols >= len(ids) and
                                                 (i // cols) == (len(ids) - 1) // cols)):
            return self._key_nav(e, key)
        self._move(dx, dy)
        return "break"

    def _move(self, dx, dy):
        ids = [g["id"] for g in self.shown]
        if not ids:
            return
        i = ids.index(self.selected) if self.selected in ids else -1
        j = 0 if i < 0 else min(len(ids) - 1, max(0, i + dx + dy * self.cols))
        self.select(ids[j])
        self._see_card(ids[j])

    def _see_card(self, gid):
        if gid not in self.cards:
            return
        x, y = self.cards[gid]
        top, bot = self.canvas.canvasy(0), self.canvas.canvasy(self.canvas.winfo_height())
        total = float(self.canvas.cget("scrollregion").split()[3]) or 1
        if y < top:
            self.canvas.yview_moveto((y - GAP) / total)
        elif y + self.card_h > bot:
            self.canvas.yview_moveto((y + self.card_h + GAP - self.canvas.winfo_height()) / total)

    def _mark_selected(self):
        """the selected card's outline: bright and thick while the cards have the focus"""
        try:
            focused = self.root.focus_get() is self.canvas
        except (KeyError, tk.TclError):
            focused = False
        for i in self.cards:
            tag = "g_" + i.replace("-", "_") + "_bg"
            if i == self.selected:
                self.canvas.itemconfigure(tag, outline=ACCENT_LIGHT if focused else ACCENT, width=4 if focused else 2)
            else:
                self.canvas.itemconfigure(tag, outline=BORDER, width=1)

    def select(self, gid):
        self.selected = gid
        self._mark_selected()
        self.show(self.by_id.get(gid))

    # -- the pane
    def show(self, g):
        self.g = g
        d = self.d_medal
        d.delete("all")
        if g is None:
            self.d_title.configure(text="Walnut Games")
            self.d_sum.configure(text="Pick a game to see how it runs on the PS3.")
            for name in ("medal", "medal_note", "main", "side", "numbers", "info", "thumbs", "links"):
                self._row(name, False)
            self.update_buttons()
            return
        self.d_title.configure(text=g["title"])
        self.d_sum.configure(text=g.get("summary", ""))
        for name in ("medal", "main", "side", "links"):
            self._row(name, True)
        r = g["rating"]
        m = r["medal"]
        fill = MEDAL_COLORS.get(m, MEDAL_COLORS["Untested"])
        lh = self.f["big"].metrics("linespace")
        pw = self.f["big"].measure(m) + 28
        d.create_polygon(rounded_points(2, 4, 2 + pw, lh + 12, 14), smooth=True, fill=fill, outline="")
        d.create_text(2 + pw / 2, lh / 2 + 8, text=m, fill=pill_text_color(fill), font=self.f["big"])
        x = pw + 14
        if r.get("score") is not None:
            t = d.create_text(x, lh / 2 + 8, text=str(r["score"]), fill=TEXT, font=self.f["title"], anchor="w")
            x = d.bbox(t)[2] + 8
            t = d.create_text(x, lh / 2 + 10, text="of 100", fill=MUTED, font=self.f["small"], anchor="w")
            x = d.bbox(t)[2] + 16
        if r.get("hours"):
            d.create_text(x, lh / 2 + 10, text="%.1f h played, %d crash%s" % (
                r["hours"], r.get("crashes", 0), "" if r.get("crashes") == 1 else "es"), fill=MUTED,
                font=self.f["small"], anchor="w")
        note = g.get("medal_note") or ("Capped: " + r["reason"] if r.get("capped") and r.get("reason") else "")
        self.d_note.configure(text=note)
        self._row("medal_note", bool(note))
        self.b_home.pack(side="left") if g.get("homepage") else self.b_home.pack_forget()
        self.show_glance()
        self.update_buttons()

    def show_glance(self):
        """the key numbers, the controller and the game data's edition, and up to 4 screenshot thumbnails"""
        g = self.g
        for w in self.d_numbers.winfo_children():
            w.destroy()
        size = sum(self.sizes.get(p, 0) for p in g["packages"]) if all(p in self.sizes for p in g["packages"]) else 0
        nums = glance_numbers(g["rating"], size)
        for i, (value, label) in enumerate(nums):
            tile = ttk.Frame(self.d_numbers, style="Panel.TFrame")
            tile.grid(row=0, column=i, sticky="w", padx=(0, round(24 * self.scale)))
            ttk.Label(tile, text=value, font=self.f["num"], background=PANEL).pack(anchor="w")
            ttk.Label(tile, text=label, style="Muted.TLabel", background=PANEL, font=self.f["small"]).pack(anchor="w")
        self._row("numbers", bool(nums))
        info = ["Controller: " + (g["rating"].get("controller") or g.get("controller") or "unknown")]
        ed = edition_text(g, self.info)
        if g.get("needs_data") and data_present(g, self.have):
            info.append("Your game data: " + (ed or "installed"))
        elif data_setup(g) and optional_data_present(g, self.have):
            info.append("Original data: " + (ed or "installed"))
        self.d_info.configure(text="   ·   ".join(info))
        self._row("info", True)
        self.show_thumbs()

    def show_thumbs(self):
        for w in self.d_thumbs.winfo_children():
            w.destroy()
        self.glance_imgs = []
        g = self.g
        shots = screenshots(g)[:GLANCE_SHOTS] if g else []
        for label, rel in shots:
            path = os.path.join(self.data, "games", g["id"], rel)
            if not os.path.exists(path):
                path = self.cache_path(g["id"], rel)
                if not os.path.exists(path):
                    self.fetch(g["id"], rel, path)
                    continue
            img = self.small_picture(path, self.shot_w, self.shot_h)
            if img is None:
                continue
            self.glance_imgs.append(img)
            b = ttk.Button(self.d_thumbs, image=img, style="Shot.TButton",
                           command=lambda rel=rel: self.open_details(shot=rel))
            n = len(self.glance_imgs) - 1
            b.grid(row=n // 2, column=n % 2, padx=(0, 6), pady=(0, 6))
        self._row("thumbs", bool(self.glance_imgs))

    def small_picture(self, path, w, h):
        """a screenshot as a PhotoImage of at most w x h (JPEG decoded at a smaller size), or None"""
        try:
            from PIL import Image
        except ImportError:
            return None
        try:
            im = Image.open(path)
            im.draft("RGB", (w, h))
            im = im.convert("RGB")
            im.thumbnail((w, h))
            try:
                from PIL import ImageTk
                return ImageTk.PhotoImage(im)
            except ImportError:   # Pillow without its Tk part: hand Tk a PNG
                import base64
                import io
                buf = io.BytesIO()
                im.save(buf, "PNG")
                return tk.PhotoImage(data=base64.b64encode(buf.getvalue()))
        except Exception:         # a broken picture must not take the window down
            return None

    def cache_path(self, gid, rel):
        return os.path.join(cache_dir(), gid, rel)

    def fetch(self, gid, rel, path, kind="shot"):
        if not self.online:
            self.events.put((kind, gid, rel, None))
            return
        if (kind, gid, rel) in self._fetching:
            return
        self._fetching.add((kind, gid, rel))

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
        self.check_repo()
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
        self.cat, self.games = cat, cat["games"]
        self.by_id = {g["id"]: g for g in self.games}
        self.thumbs.clear()
        self.tag_filter &= set(self.cat.get("tags") or {})       # tags the new catalog no longer has drop out
        if self.filters_win is not None:
            self.close_filters()
        self.apply_filters_inplace()
        new = self.by_id.get(self.selected)
        if self.selected and new is None:
            self.selected = None
            self.show(None)
        elif new is not None and new != old:
            self.show(new)
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
        games = games or self.games
        self.have = dpkg_installed({p for g in games for p in g["packages"] + data_packages(g)})
        self.info = data_info({p for g in games for p in data_packages(g) if p in self.have})

    def check_repo(self):
        """which data packages the apt sources have (apt-cache policy, in a thread): "repo" event; and the
        games' installed sizes (apt-cache show), once: "sizes" event"""
        if not os.path.exists(APT_CACHE):
            return
        names = {p for g in self.games if data_setup(g) for p in data_setup(g).get("present_any") or []}
        want = {p for g in self.games for p in g["packages"]} - set(self.sizes)

        def work():
            if names:
                self.events.put(("repo", repo_candidates(names)))
            if want:
                self.events.put(("sizes", package_sizes(want)))
        threading.Thread(target=work, daemon=True).start()

    def manifest(self, g):
        """the game's packages' files (dpkg -L), cached until the installed set changes"""
        key = (g["id"], tuple(sorted(p for p in g["packages"] if p in self.have)))
        if key not in self._manifests:
            self._manifests[key] = package_files(g["packages"])
        return self._manifests[key]

    def update_buttons(self):
        """the main button and the smaller ones for the game's state (main_action, side_actions), with the
        line that says what Add game files... adds, the add-ons and a partial import's notice"""
        g = getattr(self, "g", None)
        inst = bool(g) and is_installed(g, self.have)
        idle = self.busy is None and (self.imp is None or self.imp.done)
        can = bool(g) and g["id"] in self.pkg_ids
        reason = "Install it after the next Walnut Games update." if g and not can and not inst else ""
        self.d_reason.configure(text=reason)
        self._row("reason", bool(reason))
        self.b_refresh.configure(state="normal" if idle and not self.refreshing else "disabled")
        self.play_cmds = order_launchers(play_commands(g, self.runtime_dir, files=lambda: self.manifest(g)),
                                         g.get("launchers")) if g and inst else []
        running = self.imp.gid if self.imp is not None and not self.imp.done else None
        acts = data_actions(g, self.have, self.repo, running) if g and g["id"] in self.pkg_data else []
        main = main_action(g, inst, can, acts, len(self.play_cmds), idle, data_present(g, self.have) if g else None)
        self.main_kind = main[0] if main else None
        if main:
            self.b_main.configure(text=main[1], state="normal" if main[2] else "disabled")
        retry = bool(g) and not running and (
            (g["id"] in self.retry and any(a in acts for a in ("setup", "add", "full")) and
             bool((data_setup(g) or {}).get("downloads"))) or
            bool(g["id"] in self.pkg_data and (self.partial.get(g["id"]) or [None])[0] and self.imp_source.get(g["id"])))
        names = remove_question(g, self.info)[1] if g else []
        side = side_actions(inst, acts, self.main_kind, names, retry) if g else []
        for b in self.side_buttons.values():
            b.pack_forget()
        x, r = 0, 0
        for key in side:       # the smaller buttons in rows as wide as the pane
            b = self.side_buttons[key]
            b.configure(state="normal" if idle else "disabled")
            # the data button is the accent one when nothing else offers the data
            if key == "add-data":
                b.configure(style="TButton" if "repo" in acts or "full" in acts or g["id"] in self.retry
                            else "Accent.TButton")
            bw = b.winfo_reqwidth() + 8
            if x and x + bw > self.wrap and r + 1 < len(self.side_rows):
                x, r = 0, r + 1
            b.pack(in_=self.side_rows[r], side="left", padx=(0, 8), pady=(0, 6))
            x += bw
        self._row("side", bool(side))
        # REQUIRED / OPTIONAL: one line under the button it concerns
        self._row("note_main", False, self.b_note)
        self._row("note_side", False, self.b_note)
        if any(a in acts for a in ("setup", "add", "full")):
            tag, text = data_note(g, "setup" in acts, "full" in acts)
            self.note_tag.configure(text=tag)
            self.note_text.configure(text=text)
            self._row("note_main" if self.main_kind == "add-data" else "note_side", True, self.b_note)
        self.update_data_extras(g, idle, running)

    def update_data_extras(self, g, idle, running):
        """the add-on buttons (Also add: NAME) and a partial import's notice"""
        for b in self.addon_buttons:
            b.destroy()
        self.addon_buttons = []
        adds = [p for p in self.addons.get(g["id"], []) if p not in self.have] if g and not running else []
        if adds and g["id"] in self.pkg_data and self.imp_source.get(g["id"]):
            for p in adds:
                b = ttk.Button(self.addon_row, text=addon_label(g, p), style="Accent.TButton",
                               command=lambda p=p: self.add_on(p), state="normal" if idle else "disabled")
                b.pack(side="top", anchor="w", pady=(0, 6))
                self.addon_buttons.append(b)
        self._row("addons", bool(self.addon_buttons))
        part = self.partial.get(g["id"]) if g and not running and g["id"] in self.pkg_data else None
        self.partial_label.configure(text=part[1] if part else "")
        self._row("partial", bool(part))

    def show_activity(self, text, progress=None, hide_after=None, detail=None):
        """the status line (and the progress bar when progress is a number; "busy": a bar that only
        moves, for an import, which has no percentages) under the buttons, a smaller detail text under
        them; hide_after: seconds until it goes away by itself"""
        if self._activity_after:
            self.root.after_cancel(self._activity_after)
            self._activity_after = None
        self.d_status.configure(text=text)
        if progress is None:
            self.progress.stop()
            self.progress.pack_forget()
        else:
            if progress == "busy":
                if str(self.progress["mode"]) != "indeterminate":
                    self.progress.configure(mode="indeterminate")
                    self.progress.start(40)
            else:
                self.progress.stop()
                self.progress.configure(mode="determinate", value=progress)
            if not self.progress.winfo_ismapped():
                self.progress.pack(side="top", fill="x")
        if detail:
            self.imp_detail.configure(text=detail)
            if not self.imp_detail.winfo_ismapped():
                self.imp_detail.pack(side="top", anchor="w", fill="x", pady=(4, 0))
        else:
            self.imp_detail.pack_forget()
        self._row("activity", True)
        if hide_after:
            self._activity_after = self.root.after(int(hide_after * 1000), self.hide_activity)

    def hide_activity(self):
        if self._activity_after:
            self.root.after_cancel(self._activity_after)
            self._activity_after = None
        self._row("activity", False)

    def run_helper(self, op):
        """install, remove (with remove-data after it when the game has imported data installed) or
        install-repo-data (its data from the apt sources) through the root helper, in a thread"""
        g = self.g
        if not g or self.busy or (self.imp is not None and not self.imp.done):
            return
        ops = [op]
        if op == "remove":
            question, data = remove_question(g, self.info)
            if not messagebox.askyesno("Remove", question, parent=self.root):
                return
            if data:
                ops.append("remove-data")
        elif op == "remove-data":
            question, data = remove_data_question(g, self.info)
            if not data or not messagebox.askyesno("Remove data", question, parent=self.root):
                return
        self.busy = (g["id"], op)
        self.retry.pop(g["id"], None)
        self.partial.pop(g["id"], None)
        self.show_activity({"install": "Installing %s...", "remove": "Removing %s...",
                            "remove-data": "Removing the game data of %s...",
                            "install-repo-data": "Installing the game data of %s from Walnut licensed..."}[op]
                           % g["title"], progress=0)
        self.update_buttons()

        def work():
            tail, rc = [], 0
            for one in ops:
                try:
                    p = subprocess.Popen(["sudo", "-n", self.helper, one, g["id"]], stdin=subprocess.DEVNULL,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                                         start_new_session=True)
                    for line in p.stdout:
                        ev = parse_helper_line(line)
                        if ev:
                            self.events.put(("helper",) + ev)
                            if ev[0] in ("log", "error"):
                                tail.append(ev[1])
                    rc = p.wait()
                except OSError as e:
                    rc, tail = 1, [str(e)]
                if rc != 0:
                    break
            self.regenerate_menu()
            self.events.put(("done", g["id"], op, rc, tail[-3:]))
        threading.Thread(target=work, daemon=True).start()

    def regenerate_menu(self):
        """the session's menu entries made again (a new game, a data launcher whose files now exist) and
        the menu reloaded: ps3-game-menu --reload"""
        rt = os.environ.get("XDG_RUNTIME_DIR")
        if rt and os.path.exists(MENU_TOOL):
            try:
                subprocess.run([MENU_TOOL, "--reload", os.path.join(rt, "ps3-game-menu")], stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
            except (OSError, subprocess.SubprocessError):
                pass

    # -- game data: the chooser, walnut-data import and its progress
    def open_chooser(self, g=None):
        """the sources list (a window of big buttons: the pad's pointer clicks them; arrows, Enter and
        Escape work too), with the game's hint and Other file / Other folder"""
        g = g or self.g
        ds = data_setup(g)
        if not ds or self.busy or (self.imp is not None and not self.imp.done):
            return
        if self.filters_win is not None:
            self.close_filters()
            return "break"
        if self.chooser is not None:
            try:
                self.chooser.destroy()
            except tk.TclError:
                pass
        home = os.path.expanduser("~")
        user = os.environ.get("USER") or os.path.basename(home)
        sources = find_sources(home, user, disc_state())
        title = data_title(g)
        top = tk.Toplevel(self.root, bg=PANEL)
        self.chooser = top
        top.title("Add game files for " + g["title"])
        top.transient(self.root)
        w, h = 640, 600
        try:
            x = self.root.winfo_rootx() + max(0, (self.root.winfo_width() - w) // 2)
            y = self.root.winfo_rooty() + max(0, (self.root.winfo_height() - h) // 3)
            top.geometry("%dx%d+%d+%d" % (w, h, x, y))
        except tk.TclError:
            top.geometry("%dx%d" % (w, h))
        box = ttk.Frame(top, style="Panel.TFrame", padding=16)
        box.pack(fill="both", expand=True)
        ttk.Label(box, text="Add the original %s game files" % title,
                  font=self.f["big"], background=PANEL).pack(anchor="w")
        if ds.get("hint"):
            ttk.Label(box, text=ds["hint"], style="Muted.TLabel", background=PANEL, wraplength=w - 40,
                      justify="left").pack(anchor="w", pady=(4, 0))
        ttk.Label(box, text="Where are your %s files?" % title, background=PANEL).pack(anchor="w", pady=(10, 4))
        bottom = ttk.Frame(box, style="Panel.TFrame")
        bottom.pack(side="bottom", fill="x", pady=(10, 0))
        area = ttk.Frame(box, style="Panel.TFrame")
        area.pack(fill="both", expand=True)
        canvas = tk.Canvas(area, bg=PANEL, highlightthickness=0)
        sb = ttk.Scrollbar(area, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)
        inner = ttk.Frame(canvas, style="Panel.TFrame")
        win = canvas.create_window(0, 0, window=inner, anchor="nw")
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(win, width=e.width))
        for ev, d in (("<Button-4>", -1), ("<Button-5>", 1)):
            top.bind(ev, lambda e, d=d: canvas.yview_scroll(d * 2, "units"))
        buttons = []

        def choose(path):
            top.destroy()
            self.chooser = None
            self.start_import(g, path)

        for pkg in [p for p in ds.get("fetch") or [] if p not in self.have]:     # not the one installed
            ed = (ds.get("editions") or {}).get(pkg, "free version")
            b = ttk.Button(inner, text="Download the free %s (needs the network)" % ed, style="Source.TButton",
                           command=lambda p=pkg: (top.destroy(), setattr(self, "chooser", None),
                                                  self.start_import(g, None, [p])))
            b.pack(fill="x", pady=2)
            buttons.append(b)
        for label, path in sources:
            b = ttk.Button(inner, text=label + "\n" + path, style="Source.TButton", command=lambda p=path: choose(p))
            b.pack(fill="x", pady=2)
            buttons.append(b)
        if not sources:
            ttk.Label(inner, text="Nothing found yet: put the disc in the drive, plug in the drive that has the "
                      "files, or pick a file or folder below.", style="Muted.TLabel", background=PANEL,
                      wraplength=w - 60, justify="left").pack(anchor="w", pady=8)

        def pick(folder):
            if folder:
                path = filedialog.askdirectory(parent=top, initialdir=home, title="The folder with the %s files" % title)
            else:
                path = filedialog.askopenfilename(parent=top, initialdir=home, title="A %s disc image, installer or "
                                                  "archive" % title)
            if path:
                choose(path)

        cancel = ttk.Button(bottom, text="Cancel", command=lambda: (top.destroy(), setattr(self, "chooser", None)))
        other_file = ttk.Button(bottom, text="Other file\u2026", command=lambda: pick(False))
        other_dir = ttk.Button(bottom, text="Other folder\u2026", command=lambda: pick(True))
        for b in (other_file, other_dir):
            b.pack(side="left", padx=(0, 8))
        cancel.pack(side="right")
        order = buttons + [other_file, other_dir, cancel]

        def move(d):
            f = top.focus_get()
            i = order.index(f) if f in order else -1
            j = max(0, min(len(order) - 1, i + d))
            order[j].focus_set()
            if order[j] in buttons:     # keep it in view
                total = max(1, inner.winfo_height())
                y = order[j].winfo_y()
                top_y, bot_y = canvas.canvasy(0), canvas.canvasy(canvas.winfo_height())
                if y < top_y or y + order[j].winfo_height() > bot_y:
                    canvas.yview_moveto(max(0.0, (y - 8) / total))
            return "break"

        top.bind("<Down>", lambda e: move(1))
        top.bind("<Up>", lambda e: move(-1))
        top.bind("<Return>", lambda e: (top.focus_get().invoke() if hasattr(top.focus_get(), "invoke") else None,
                                        "break")[1])
        top.bind("<Escape>", lambda e: cancel.invoke())
        top.after(50, lambda: (order[0].focus_set(), self.pad_warp() if self.pad_recent() else None)
                  if top.winfo_exists() else None)
        self.chooser_sources = sources      # what was offered (the tests look)

    def import_paths(self):
        cache = cache_dir()
        return os.path.join(cache, "import.log"), os.path.join(cache, "import.json")

    def start_import(self, g, source, packages=None):
        """walnut-data import in the background, its output to ~/.cache/walnut-games/import.log (the app
        reads it from there, so an import goes on when the app is closed, and is shown again when it
        is opened: resume_import)"""
        if self.busy or (self.imp is not None and not self.imp.done):
            return
        log, side = self.import_paths()
        cmd = import_command(g["id"], source, packages)
        try:
            os.makedirs(os.path.dirname(log), exist_ok=True)
            with open(log, "w") as out:
                p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                     start_new_session=True)
        except OSError as e:
            self.show_activity("Can't start the import: %s" % e)
            return
        t0 = time.time()
        try:
            with open(side + ".part", "w") as f:
                json.dump({"pid": p.pid, "game": g["id"], "t0": t0, "source": source}, f)
            os.replace(side + ".part", side)
        except OSError:
            pass
        self.retry.pop(g["id"], None)
        self.partial.pop(g["id"], None)
        self.imp_source[g["id"]] = source
        self.imp_before = {c[0] for c in getattr(self, "play_cmds", [])} if self.g is g else set()
        self.imp = ImportView(g["id"], t0)
        self.watch_import(g["id"], log, proc=p)
        self.show_import()
        self.update_buttons()

    def add_on(self, pkg):
        """an add-on the last import's source also holds: walnut-data import -p PKG from the same source (a
        disc or image is opened again by walnut-data)"""
        g = self.g
        if g and self.imp_source.get(g["id"]) and pkg in self.addons.get(g["id"], []):
            self.start_import(g, self.imp_source[g["id"]], [pkg])

    def retry_download(self):
        g = self.g
        if not g:
            return
        part = self.partial.get(g["id"])
        if part and part[0] and self.imp_source.get(g["id"]):
            self.start_import(g, self.imp_source[g["id"]], list(part[0]))
            return
        if g["id"] not in self.retry:
            return
        ds = data_setup(g)
        self.start_import(g, self.retry[g["id"]], [p for p in ds.get("downloads") or []])

    def resume_import(self):
        """an import that runs (walnut-data's lock is held) shown again: with its log when this app started
        it, else as "started elsewhere" (a terminal)"""
        held = import_lock_state(os.path.join(data_cache_dir(), "import.lock"))
        if not held:
            return
        pid, gid = held
        log, side = self.import_paths()
        try:
            with open(side) as f:
                meta = json.load(f)
        except (OSError, ValueError):
            meta = {}
        mine = meta.get("pid") == pid and meta.get("game") == gid
        self.imp = ImportView(gid, meta.get("t0") if mine else None)
        if mine:
            self.imp_source[gid] = meta.get("source")
        else:
            self.imp.step = "Importing (started in a terminal)"
        self.watch_import(gid, log if mine else None)
        self.show_import()

    def watch_import(self, gid, log, proc=None):
        """a thread reading the import's log as it grows ("import-line" events) until walnut-data exits
        (its process, or its lock when this app didn't start it): an "import-done" event"""
        lock = os.path.join(data_cache_dir(), "import.lock")

        def work():
            pos, buf = 0, b""
            while True:
                alive = proc.poll() is None if proc is not None else import_lock_state(lock) is not None
                if log:
                    try:
                        with open(log, "rb") as f:
                            f.seek(pos)
                            chunk = f.read()
                        pos += len(chunk)
                        buf += chunk
                    except OSError:
                        pass
                    *lines, buf = buf.split(b"\n")
                    for raw in lines:
                        ev = parse_import_line(raw.decode("utf-8", "replace"))
                        if ev:
                            self.events.put(("import-line", gid, ev))
                if not alive:
                    break
                time.sleep(0.3)
            self.events.put(("import-done", gid, proc.wait() if proc is not None else None))
        threading.Thread(target=work, daemon=True).start()
        self.root.after(1000, self._tick)

    def _tick(self):
        if self.imp is not None and not self.imp.done:
            self.show_import()
            self.root.after(1000, self._tick)

    def show_import(self):
        v = self.imp
        if v is None:
            return
        g = self.by_id.get(v.gid, {"title": v.gid, "id": v.gid})
        detail = ["Reading " + v.file] if v.file else []
        detail.append("Elapsed " + elapsed_text(time.time() - v.t0))
        detail += ["\u2022 " + l[:120] for l in v.tail]
        self.show_activity("Importing the %s data for %s: %s" % (data_title(g), g["title"], v.step), progress="busy",
                           detail="\n".join(detail))

    def import_done(self, gid, rc):
        v = self.imp
        if v is None or v.gid != gid:
            return
        v.done = True
        self.refresh_installed()
        self.check_repo()
        g = self.by_id.get(gid)
        title = data_title(g) if g else gid
        ok = rc == 0 if rc is not None else (not v.errors and g is not None and
                                             (bool(data_present(g, self.have)) or optional_data_present(g, self.have)))
        if ok:
            self.addons[gid] = list(v.addons)
            note = partial_notice(v.download, drop_alternatives(
                v.missing, (data_setup(g) or {}).get("alternatives") if g else None, self.have))
            if note:
                self.partial[gid] = (list(v.download), note)
            ed = edition_text(g, self.info) if g else ""
            self.show_activity("The %s data is installed%s. Making the menu entries..." % (title, ": " + ed if ed else ""))

            def work():
                self.regenerate_menu()
                self.events.put(("menu-done", gid, ed))
            threading.Thread(target=work, daemon=True).start()
        else:
            kind = v.kind()
            if kind == "needs-download" and g and data_setup(g).get("downloads") and self.imp_source.get(gid):
                self.retry[gid] = self.imp_source[gid]
            self.show_activity(import_error_message(kind, title, v.tail, v.errors, v.gdp_text))
        self.apply_filters_keep()

    def menu_done(self, gid, ed):
        self._manifests.clear()
        self.apply_filters_keep()
        g = self.by_id.get(gid)
        new = []
        if g is not None and self.g is g:
            new = [c[0] for c in getattr(self, "play_cmds", []) if c[0] not in self.imp_before]
        self.show_activity("The %s data is installed%s.%s" % (data_title(g) if g else gid, ": " + ed if ed else "",
                                                               (" New in Play: " + ", ".join(new) + ".") if new else "") +
                           (" Not everything was made: see the notice below." if gid in self.partial else ""),
                           hide_after=None if gid in self.partial else 12)
        self.update_buttons()

    def _poll(self):
        self._hand_off_n += 1
        if self.hand_off_path and self._hand_off_n % 5 == 0 and os.path.exists(self.hand_off_path):
            gid = take_hand_off(self.hand_off_path)
            if gid is not None:
                self.open_game(gid)
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
                    ok = {"install": "installed", "remove": "removed", "install-repo-data": "data installed",
                          "remove-data": "data removed"}.get(op, "done")
                    self.check_repo()       # what the sources offer beyond what is installed changed
                    title = self.by_id.get(gid, {"title": gid})["title"]
                    # a success fades after a few seconds; a failure stays until the next action
                    self.show_activity(title + (" " + ok + "." if rc == 0 else
                                       ": " + op + " failed" + (" (refused)" if rc == 2 else "") + "."),
                                       hide_after=6 if rc == 0 else None)
                    if rc != 0:
                        messagebox.showerror("Walnut Games", "%s %s failed:\n\n%s" % (
                            {"install": "Installing", "remove": "Removing", "remove-data": "Removing the data of"}.get(
                                op, "Installing the data of"), title,
                            "\n".join(tail) or "no details"), parent=self.root)
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
                elif ev[0] == "import-line":
                    if self.imp is not None and self.imp.gid == ev[1] and not self.imp.done:
                        self.imp.feed(ev[2])
                        self.show_import()
                elif ev[0] == "import-done":
                    self.import_done(ev[1], ev[2])
                elif ev[0] == "menu-done":
                    self.menu_done(ev[1], ev[2])
                elif ev[0] == "repo":
                    self.repo = ev[1]
                    self.update_buttons()
                elif ev[0] == "catalog":
                    self.on_catalog(*ev[1:])
                elif ev[0] == "thumb":
                    self._fetching.discard(tuple(ev[:3]))
                    if ev[3]:
                        self.thumbs.pop(ev[1], None)
                        self.layout()
                elif ev[0] == "shot":
                    self._fetching.discard(tuple(ev[:3]))
                    if self.g and self.g["id"] == ev[1] and ev[3]:
                        self.show_thumbs()
                elif ev[0] == "sizes":
                    self.sizes.update(ev[1])
                    if self.g:
                        self.show_glance()
                elif ev[0] == "pad":
                    self.on_pad(ev[1])
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
                    bd=1, relief="solid", font=self.f["big"])
        for label, argv, cwd in cmds:
            m.add_command(label=label, command=lambda a=argv, c=cwd: self.launch(g, a, c))
        b = self.b_main
        self.menu = m
        m.bind("<Unmap>", lambda e: setattr(self, "menu", None) if self.menu is m else None)
        # no grab_release after it: on X11 tk_popup returns at once, and the menu's grab is what
        # closes it on a click outside
        m.tk_popup(b.winfo_rootx(), b.winfo_rooty() + b.winfo_height())
        if self.pad_recent():       # the d-pad goes on in the menu: its first line under the pointer
            m.activate(0)
            self.root.after(50, self.pad_warp)

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
        """the game's Technical tab in Details; the website's page when the app's copy can't show it"""
        g = getattr(self, "g", None)
        tech = [t for t in (details_tabs(self.pages, g["id"]) if g else []) if t.endswith("technical/index.html")]
        if not (tech and self.open_details(page=tech[0], quiet=True)):
            webbrowser.open(web_url(g["id"] if g else None, technical=True))

    def open_home(self):
        if getattr(self, "g", None) and self.g.get("homepage"):
            webbrowser.open(self.g["homepage"])

    # -- the main button and the pages' walnut:// links: one code path
    def main_pressed(self):
        if self.main_kind and self.g:
            self.dispatch(self.main_kind, self.g["id"])

    def dispatch(self, action, gid, launcher=None):
        """what the main button and the pages' walnut:// links do: install (the helper), add-data (the
        chooser), play (the launcher named, else Play, a menu when there are several). The game is selected
        first, so the pane shows the progress. False when it can't be done now (the status line says why
        when it isn't plain from the pane)"""
        g = self.by_id.get(gid)
        if g is None or action not in WALNUT_ACTIONS:
            return False
        if self.g is not g:
            self.select(gid)
            self._see_card(gid)
        inst = is_installed(g, self.have)
        enabled = str(self.b_main["state"]) != "disabled"
        if action == "install":
            if inst:
                self.show_activity("%s is installed." % g["title"], hide_after=4)
                return False
            if self.main_kind != "install" or not enabled:
                return False
            self.run_helper("install")
            return True
        if action == "add-data":
            if gid not in self.pkg_data:
                return False
            self.open_chooser(g)
            return self.chooser is not None
        if not inst:
            self.show_activity("Install %s first." % g["title"], hide_after=6)
            return False
        if self.main_kind == "add-data":
            self.show_activity("%s needs its game files first: %s" % (g["title"], DATA_LABEL), hide_after=6)
            return False
        if self.main_kind != "play" or not enabled:
            return False
        if launcher:
            for label, argv, cwd in self.play_cmds:
                if label.split(" · ")[0] == launcher:
                    self.launch(g, argv, cwd)
                    return True
        self.play()
        return True

    # -- Details: the app's copy of the game's pages (tools/gamedb/build.sh --app) in Tkhtml (tkinterweb)
    def open_details(self, gid=None, shot=None, page=None, quiet=False):
        """the game's pages, full window, in place of the list (Back, Escape or Circle comes back): the
        Overview, or page, or a screenshot (shot: its path in the game's folder) with the Overview under it.
        tkinterweb is imported here, at the first open, so the app starts as small as before; without it,
        or without the game in the app's copy (a game only on the website), the website's page opens in the
        browser (quiet: say nothing and return False, the caller has its own fallback)"""
        g = self.by_id.get(gid) if gid else getattr(self, "g", None)
        if g is None or self.dv is not None:
            return False
        tabs = details_tabs(self.pages, g["id"])
        html, why = None, ""
        if tabs:
            try:
                import tkinterweb
            except Exception as e:      # not installed, or its Tkhtml doesn't load
                tkinterweb, why = None, str(e)
        f = None
        if tabs and tkinterweb is not None:
            self.bar.pack_forget()
            self.body.pack_forget()
            s = self.scale
            f = ttk.Frame(self.root, style="Panel.TFrame")
            f.pack(fill="both", expand=True)
            head = ttk.Frame(f, style="Panel.TFrame", padding=(round(12 * s), round(8 * s)))
            head.pack(fill="x")
            back = ttk.Button(head, text="◀  Back", command=self.back)
            back.pack(side="left")
            ttk.Label(head, text=g["title"], font=self.f["big"], background=PANEL).pack(side="left", padx=16)
            ttk.Label(head, text="L1 / R1: tabs      Circle: back", style="Muted.TLabel",
                      background=PANEL).pack(side="right")
            try:
                html = tkinterweb.HtmlFrame(f, messages_enabled=False, on_link_click=self.details_link, zoom=s,
                                            forms_enabled=False, objects_enabled=False, javascript_enabled=False,
                                            caret_browsing_enabled=False, horizontal_scrollbar=False)
                html.pack(fill="both", expand=True)
            except Exception as e:
                why = str(e)
                html = None
        if html is None:
            if f is not None:
                f.destroy()
                self.bar.pack(fill="x")
                self.body.pack(fill="both", expand=True)
            if why:
                print("walnut-games: Details in the browser: %s" % why, file=sys.stderr)
            if quiet:
                return False
            webbrowser.open(web_url(g["id"]))
            self.show_activity("The details opened on the website%s." % (
                "" if not tabs else " (python3-tkinterweb shows them here)"), hide_after=8)
            return False
        self.dv = {"g": g, "frame": f, "html": html, "back": back, "url": None, "history": []}
        self.details_load(page or tabs[0])
        if shot:
            p = os.path.join(self.pages, "games", g["id"], shot)
            for alt in (p, os.path.join(self.data, "games", g["id"], shot), self.cache_path(g["id"], shot)):
                if os.path.exists(alt):
                    self.details_load(alt, push=True)
                    break
        back.focus_set()
        return True

    def details_load(self, path_or_url, push=False):
        dv = self.dv
        url = path_or_url if "://" in path_or_url else "file://" + path_or_url
        if push and dv["url"]:
            dv["history"].append(dv["url"])
        dv["url"] = url
        dv["html"].load_url(url)

    def details_link(self, url):
        """a link clicked in Details (tkinterweb's on_link_click: nothing is navigated by itself): walnut://
        runs the action (Details closes, the pane shows its progress), the app's copy loads in place, the
        website and other sites open in the browser, an unknown walnut:// link is ignored"""
        kind, val = link_target(url, self.pages)
        if kind == "walnut":
            self.close_details()
            self.dispatch(*val)
        elif kind == "page":
            dv = self.dv
            if dv and val.split("#")[0] == (dv["url"] or "").split("#")[0]:
                dv["url"] = val
                dv["html"].load_url(val)          # an anchor on the same page
            elif dv:
                self.details_load(val, push=True)
        elif kind == "web":
            webbrowser.open(val)
        elif kind == "close":
            self.close_details()
        elif kind == "tag":         # a tag's page: the list, filtered by that tag
            self.close_details()
            self.filter_by_tag(val)
        else:
            print("walnut-games: link ignored: %s" % url, file=sys.stderr)

    def details_tab(self, d):
        """L1 / R1 (and left / right): the tab before or after; a tab replaces the page (Back leaves Details)"""
        dv = self.dv
        if not dv or not dv["url"]:
            return
        import urllib.parse
        nxt = tab_step(self.pages, urllib.parse.unquote(urllib.parse.urlsplit(dv["url"]).path), d)
        if nxt:
            dv["history"].clear()
            self.details_load(nxt)

    def _details_scroll(self, n, what):
        if self.dv:
            try:
                self.dv["html"].yview_scroll(n, what)
            except tk.TclError:
                pass
            return "break"

    def close_details(self):
        """back to the list: the HTML frame is destroyed, so its pages and pictures are freed"""
        dv, self.dv = self.dv, None
        if not dv:
            return
        for w in (dv["html"], dv["frame"]):
            try:
                w.destroy()
            except Exception:
                pass
        dv.clear()
        import gc
        gc.collect()
        self.bar.pack(fill="x")
        self.body.pack(fill="both", expand=True)
        self.root.after_idle(self._focus_after_details)

    def _focus_after_details(self):
        target = self.b_details if self.b_details.winfo_ismapped() else self.canvas
        target.focus_set()
        if self.pad_recent():
            self.root.after(50, self.pad_warp)

    def back(self):
        """Escape, Circle (the pointer's right click) and Back: Play's menu or the chooser closes; in Details,
        the page before (a link followed), else Details itself"""
        if self.menu is not None:
            try:
                self.menu.unpost()
                self.root.tk.call("grab", "release", str(self.menu))
            except tk.TclError:
                pass
            self.menu = None
            return "break"
        if self.filters_win is not None:
            self.close_filters()
            return "break"
        if self.chooser is not None:
            try:
                self.chooser.destroy()
            except tk.TclError:
                pass
            self.chooser = None
            return "break"
        if self.dv is not None:
            if self.dv["history"]:
                self.dv["url"] = self.dv["history"].pop()
                self.dv["html"].load_url(self.dv["url"])
            else:
                self.close_details()
            return "break"
        return None

    # -- the tag filter
    def filter_by_tag(self, tid):
        """only the games with tag tid (a tag link in Details), the search cleared"""
        if tid not in (self.cat.get("tags") or {}):
            return False
        self.tag_filter = {tid}
        self.v_text.set("")
        self.apply_filters()
        if self.shown:
            self.select(self.shown[0]["id"])
        self.canvas.focus_set()
        return True

    def open_filters(self):
        """the tag filter: a window of the catalog's tag groups, a box for every tag some game has (the d-pad
        moves between them, Cross ticks one; the list follows at once); a game shows when it has one of the
        ticked tags of each group, as on the site. Done, Escape or Circle closes it; Clear unticks all."""
        if self.filters_win is not None:
            return
        groups = tag_groups(self.cat)
        top = tk.Toplevel(self.root, bg=PANEL)
        self.filters_win = top
        top.title("Filters")
        top.transient(self.root)
        s = self.scale
        w, h = round(760 * s), min(round(640 * s), self.root.winfo_screenheight() - 80)
        try:
            x = self.root.winfo_rootx() + max(0, (self.root.winfo_width() - w) // 2)
            y = self.root.winfo_rooty() + max(0, (self.root.winfo_height() - h) // 3)
            top.geometry("%dx%d+%d+%d" % (w, h, x, y))
        except tk.TclError:
            top.geometry("%dx%d" % (w, h))
        box = ttk.Frame(top, style="Panel.TFrame", padding=round(16 * s))
        box.pack(fill="both", expand=True)
        ttk.Label(box, text="Filters", font=self.f["big"], background=PANEL).pack(anchor="w")
        ttk.Label(box, text="A game shows when it has one of the ticked tags in each group.", style="Muted.TLabel",
                  background=PANEL).pack(anchor="w", pady=(2, 8))
        bottom = ttk.Frame(box, style="Panel.TFrame")
        bottom.pack(side="bottom", fill="x", pady=(10, 0))
        area = ttk.Frame(box, style="Panel.TFrame")
        area.pack(fill="both", expand=True)
        canvas = tk.Canvas(area, bg=PANEL, highlightthickness=0)
        sb = ttk.Scrollbar(area, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)
        inner = ttk.Frame(canvas, style="Panel.TFrame")
        win = canvas.create_window(0, 0, window=inner, anchor="nw")
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(win, width=e.width))
        for ev, d in (("<Button-4>", -1), ("<Button-5>", 1)):
            top.bind(ev, lambda e, d=d: canvas.yview_scroll(d * 2, "units"))
        self.filter_vars = {}

        def see(wd):        # the focused box kept in view
            total = max(1, inner.winfo_height())
            y = wd.winfo_rooty() - inner.winfo_rooty()
            top_y, bot_y = canvas.canvasy(0), canvas.canvasy(canvas.winfo_height())
            if y < top_y or y + wd.winfo_height() > bot_y:
                canvas.yview_moveto(max(0.0, (y - 30) / total))

        def toggle(tid):
            if self.filter_vars[tid].get():
                self.tag_filter.add(tid)
            else:
                self.tag_filter.discard(tid)
            self.apply_filters()
        cols = 3
        first = None
        for gid, label, tags in groups:
            ttk.Label(inner, text=label, font=self.f["bold"], foreground=TEXT, background=PANEL).pack(
                anchor="w", pady=(8, 2))
            grid = ttk.Frame(inner, style="Panel.TFrame")
            grid.pack(anchor="w", fill="x")
            for n, (tid, tlabel, count) in enumerate(tags):
                v = tk.BooleanVar(value=tid in self.tag_filter)
                self.filter_vars[tid] = v
                cb = ttk.Checkbutton(grid, text="%s (%d)" % (tlabel, count), variable=v, style="Filter.TCheckbutton",
                                     command=lambda t=tid: toggle(t))
                cb.grid(row=n // cols, column=n % cols, sticky="w", padx=(0, 16))
                cb.bind("<FocusIn>", lambda e, wd=cb: see(wd))
                first = first or cb
            for c in range(cols):
                grid.columnconfigure(c, weight=1, uniform="tags")

        def clear():
            self.tag_filter.clear()
            for v in self.filter_vars.values():
                v.set(False)
            self.apply_filters()
        done = ttk.Button(bottom, text="Done", style="Accent.TButton", command=self.close_filters)
        done.pack(side="right")
        ttk.Button(bottom, text="Clear", command=clear).pack(side="right", padx=(0, 8))
        self.filters_count = ttk.Label(bottom, text="", style="Muted.TLabel", background=PANEL)
        self.filters_count.pack(side="left")
        top.protocol("WM_DELETE_WINDOW", self.close_filters)
        top.after(50, lambda: ((first or done).focus_set(), self.pad_warp() if self.pad_recent() else None)
                  if top.winfo_exists() else None)

    def close_filters(self):
        if self.filters_win is not None:
            try:
                self.filters_win.destroy()
            except tk.TclError:
                pass
        self.filters_win = None
        self.b_filters.focus_set()

    # -- moving the focus: the arrow keys and the pad's d-pad
    def _focus_path(self):
        try:
            return str(self.root.tk.call("focus"))
        except tk.TclError:
            return ""

    def _rect(self, path, card=True):
        """a widget's rectangle on the screen; the cards' is the selected card's (card: moving from it)"""
        t = self.root.tk.call
        if card and path == str(self.canvas) and self.selected in self.cards:
            x, y = self.cards[self.selected]
            return (self.canvas.winfo_rootx() + x - self.canvas.canvasx(0),
                    self.canvas.winfo_rooty() + y - self.canvas.canvasy(0), CARD_W, self.card_h)
        return (int(t("winfo", "rootx", path)), int(t("winfo", "rooty", path)),
                int(t("winfo", "width", path)), int(t("winfo", "height", path)))

    def _focusables(self, top):
        """[(path, rectangle)] of the controls in the window top the focus can go to: shown and not off"""
        t, out = self.root.tk.call, []

        def walk(p):
            for c in self.root.tk.splitlist(t("winfo", "children", p)):
                c = str(c)
                if str(t("winfo", "toplevel", c)) != top or not int(t("winfo", "viewable", c)):
                    continue
                cls = str(t("winfo", "class", c))
                if cls in FOCUS_CLASSES or c == str(self.canvas):
                    try:
                        off = int(t(c, "instate", "disabled")) if cls.startswith("T") else \
                            (str(t(c, "cget", "-state")) == "disabled" if cls != "Canvas" else False)
                    except tk.TclError:
                        off = False
                    if not off:
                        out.append((c, self._rect(c, card=False)))
                walk(c)
        walk(top)
        return out

    def _key_nav(self, e, key):
        """an arrow key (or the d-pad): in Details, up and down scroll and left and right change the tab;
        elsewhere the focus goes to the nearest control that way (nav_pick); from the cards' right edge to
        the main button. Lists, menus and a text field's left and right keep their own keys."""
        if self.dv is not None:
            if key in ("Up", "Down"):
                return self._details_scroll(-4 if key == "Up" else 4, "units")
            self.details_tab(-1 if key == "Left" else 1)
            return "break"
        path = self._focus_path()
        if not path:
            return None
        t = self.root.tk.call
        cls = str(t("winfo", "class", path))
        if cls in ("Listbox", "Menu", "Text") or (cls in ("TEntry", "Entry") and key in ("Left", "Right")):
            return None
        if path == str(self.canvas) and key == "Right" and str(self.b_main["state"]) != "disabled" and \
                self.b_main.winfo_ismapped():
            self.b_main.focus_set()
            return "break"
        top = str(t("winfo", "toplevel", path))
        others = [(p, r) for p, r in self._focusables(top) if p != path]
        i = nav_pick(self._rect(path), [r for _, r in others], key.lower())
        if i is not None:
            target = others[i][0]
            t("focus", target)
            if target == str(self.canvas) and self.selected not in self.cards and self.shown:
                self.select(self.shown[0]["id"])
            self._mark_selected()
        return "break"

    def _key_press(self, e):
        """Enter: the focused button is pressed; on the cards, the focus goes to the main button"""
        path = self._focus_path()
        if not path:
            return None
        cls = str(self.root.tk.call("winfo", "class", path))
        if cls in ("TButton", "TCheckbutton", "Button", "Checkbutton", "Radiobutton"):
            self.root.tk.call(path, "invoke")
            return "break"
        if path == str(self.canvas) and self.g is not None and self.b_main.winfo_ismapped():
            self.b_main.focus_set()
            return "break"
        return None

    def _click_focus(self, e):
        """a click (the pad's Cross) on a button gives it the focus too, so the d-pad goes on from there"""
        w = e.widget
        try:
            if w.winfo_class() in ("TButton", "TCheckbutton", "TCombobox") and w.instate(["!disabled"]):
                w.focus_set()
        except (AttributeError, tk.TclError):
            pass

    # -- the pad's free buttons (PadReader): the d-pad, L1, R1, Square
    def pad_recent(self):
        return time.monotonic() - getattr(self, "pad_t", -1e9) < 60

    def on_pad(self, name):
        """a pad button: only while the app has the focus (a game or the browser on top has it otherwise).
        The d-pad is the arrow keys, then the pointer goes onto the focus (Cross, the pointer's click, presses
        it); L1 / R1 change Details' tab; Square opens Details. Circle is the pointer's right click (back)."""
        path = self._focus_path()
        if not path:
            return
        self.pad_t = time.monotonic()
        if name in ("up", "down", "left", "right"):
            try:
                self.root.tk.call("event", "generate", path, "<KeyPress-%s>" % name.capitalize())
            except tk.TclError:
                return
            self.root.after_idle(self.pad_warp)
        elif name in ("l1", "r1"):
            self.details_tab(-1 if name == "l1" else 1)
        elif name == "square" and self.dv is None and self.chooser is None and self.menu is None and self.g:
            self.open_details()

    def pad_warp(self):
        """the pointer onto what has the focus (a list's or a menu's active line, the selected card), so
        that Cross, the pointer's left click, presses the focused control"""
        path = self._focus_path()
        if not path:
            return
        t = self.root.tk.call
        try:
            cls = str(t("winfo", "class", path))
            w, h = int(t("winfo", "width", path)), int(t("winfo", "height", path))
            x, y = w // 2, h // 2
            if cls == "Listbox":
                bb = self.root.tk.splitlist(t(path, "bbox", "active"))
                if bb:
                    bx, by, bw, bh = (int(v) for v in bb)
                    x, y = bx + bw // 2, by + bh // 2
            elif cls == "Menu":
                i = str(t(path, "index", "active"))
                if i != "none":
                    y0 = int(t(path, "yposition", i))
                    last = int(t(path, "index", "end"))
                    y1 = int(t(path, "yposition", int(i) + 1)) if int(i) < last else h
                    y = (y0 + y1) // 2
            elif path == str(self.canvas) and self.selected in self.cards:
                cx, cy = self.cards[self.selected]
                x = int(cx + CARD_W / 2 - self.canvas.canvasx(0))
                y = int(cy + self.card_h / 2 - self.canvas.canvasy(0))
            t("event", "generate", path, "<Motion>", "-warp", 1, "-x", x, "-y", y)
        except (tk.TclError, ValueError):
            pass



def main(argv):
    """walnut-games [walnut://game/<id>]: the app, on that game when a link is given (the website's "Open in
    Walnut Games"); with Walnut Games already running, the link is handed to it and this start ends"""
    gid = None
    if len(argv) > 1:
        gid = external_game(argv[1])
        if gid is None:
            print("walnut-games: not a link Walnut Games opens: %r (walnut://game/<id> only)" % argv[1][:200],
                  file=sys.stderr)
            return 2
    lock, hand = instance_paths()
    if claim_instance(lock) is None:
        try:
            hand_off(hand, gid)
            return 0
        except OSError as e:
            print("walnut-games: can't reach the running Walnut Games: %s" % e, file=sys.stderr)
            return 1
    if tk is None:
        print("walnut-games: python3-tk is not installed", file=sys.stderr)
        return 1
    try:
        root = tk.Tk()
    except tk.TclError as e:
        print("walnut-games: no display: %s" % e, file=sys.stderr)
        return 1
    try:
        os.remove(hand)       # a hand-off left from before this start
    except OSError:
        pass
    try:
        App(root, pad=True, hand_off_path=hand, start_game=gid)
    except (OSError, ValueError, KeyError) as e:
        print("walnut-games: the catalog can't be read: %s" % e, file=sys.stderr)
        return 1
    root.mainloop()
    return 0
