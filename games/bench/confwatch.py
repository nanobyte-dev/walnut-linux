#!/usr/bin/env python3
"""confwatch.py: did the player change the game's video settings, and does the game read its settings back?
(docs/GAMEDB.md "Rules versions": Display's "as shipped", Features' "settings stick"). Run by session.sh
before and after a played session, as the player.

  confwatch.py before OUT.json STATE.json TOKEN...     before the game starts
  confwatch.py after BEFORE.json STATE.json RUN_DIR    after it quit: RUN_DIR/config.tsv, and
                                                       config_* lines on stdout for bench.txt

READ-ONLY on the player's files, a hard rule: they are opened O_RDONLY|O_NOATIME (the read leaves their
access time alone, so the game's own read can be seen), never written, copied, backed up or restored.
What is kept is ours: hashes (the file's, and one per line), sizes and times in OUT.json and in
STATE.json (~/.cache/ps3-session/confwatch/<game>.json), never the text.

- The game's files: directories under ~, ~/.config and ~/.local/share whose name contains a TOKEN (the
  recipe's name and the game's packages, as session.sh passes them; "luanti" also finds ~/.minetest
  through ALIASES), at most 4 levels down, text files (no NUL byte in the first 4 KB) up to 512 KB, at
  most 2000 of them.
- Video settings changed: a file that existed before the session and has lines after it that it didn't
  have before, whose key (the first name on the line: `seta r_mode "4"`, `fullscreen = 1`, `<width ...`)
  looks like a video setting (VIDEO_RE, minus window positions and times). Only the key names are
  reported. A file the game created in this session doesn't count (its first start writes its defaults).
- Settings read back: the settings-like files (SETTINGS_RE) the game changed in its last session
  (STATE.json), unchanged since, whose access time moved during this one: settings_reread=yes. None of
  them read: no; nothing to tell (no last session, the home mounted noatime, or relatime can't show a
  second read within a day of the last one): no line. Then STATE.json gets this session's changed
  settings files.
"""
import hashlib
import json
import os
import re
import sys
import time

MAX_FILES, MAX_SIZE, MAX_DEPTH, MAX_LINES = 2000, 512 * 1024, 4, 20000
ALIASES = {"luanti": ["minetest"], "dsda-doom": ["dsda"], "yquake2": ["yq2"], "armagetronad": ["armagetron"],
           "supertuxkart": ["stk"]}
VIDEO_RE = re.compile(r"(^r_|^vid_|^gl_|video|screen|resolution|fullscreen|^width$|^height$|xres|yres|vsync|"
                      r"swapinterval|display|^window|gfx|graphic|shadow|texture|aniso|antialias|msaa|fsaa|quality|"
                      r"detail|^fov$|gamma|bright|renderer)", re.I)
NOT_VIDEO_RE = re.compile(r"(pos|_x$|_y$|^x$|^y$|last|time|count|recent|history|stamp)", re.I)
SETTINGS_RE = re.compile(r"(cfg|conf|config|settings|options|prefs|\.ini$|\.xml$|\.json$|\.toml$|rc$)", re.I)
KEY_RE = re.compile(r"[\s<\[\"'(]*(?:(?:seta|set|sets|setu|cvar)\s+)?[\"']?([A-Za-z_][\w.\-]*)")


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def roots(home, tokens):
    toks = set()
    for t in tokens:
        for x in [t] + ALIASES.get(t, []):
            x = re.sub(r"-(data|common|server|nonfree)$", "", x)
            if len(norm(x)) >= 3:
                toks.add(norm(x))
    out = []
    for base in (home, os.path.join(home, ".config"), os.path.join(home, ".local", "share")):
        try:
            names = sorted(os.listdir(base))
        except OSError:
            continue
        for n in names:
            p = os.path.join(base, n)
            if n in (".cache", ".config", ".local") or not os.path.isdir(p) or os.path.islink(p):
                continue
            if any(t in norm(n) for t in toks):
                out.append(p)
    return out


def read(path):
    """the bytes of a file without touching its access time (O_NOATIME needs the owner: the player)"""
    try:
        fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOATIME", 0))
    except PermissionError:
        fd = os.open(path, os.O_RDONLY)     # not the owner: the atime may move, that file's reread can't be told
    try:
        chunks = []
        while True:
            b = os.read(fd, 65536)
            if not b:
                return b"".join(chunks)
            chunks.append(b)
    finally:
        os.close(fd)


def line_hashes(data):
    return sorted({hashlib.sha1(line.strip()).hexdigest()[:12] for line in data.splitlines()[:MAX_LINES] if line.strip()})


def scan(home, rootdirs):
    """{path relative to home: {size, mtime, atime, sha, lines}} of the game's text files"""
    out = {}
    for r in rootdirs:
        top = r.rstrip("/").count("/")
        for d, dirs, files in os.walk(r):
            if d.count("/") - top >= MAX_DEPTH:
                dirs[:] = []
            dirs[:] = [x for x in dirs if not os.path.islink(os.path.join(d, x))]
            for f in files:
                p = os.path.join(d, f)
                try:
                    st = os.lstat(p)
                    if not os.path.isfile(p) or os.path.islink(p) or st.st_size > MAX_SIZE:
                        continue
                    data = read(p)
                except OSError:
                    continue
                if b"\0" in data[:4096]:
                    continue
                out[os.path.relpath(p, home)] = {"size": st.st_size, "mtime": st.st_mtime_ns, "atime": st.st_atime_ns,
                                                 "sha": hashlib.sha256(data).hexdigest(), "lines": line_hashes(data)}
                if len(out) >= MAX_FILES:
                    return out
    return out


def video_keys(data, before_lines):
    """the video-looking key names of the lines of data that weren't among before_lines (hashes)"""
    old, keys = set(before_lines), set()
    for line in data.splitlines()[:MAX_LINES]:
        s = line.strip()
        if not s or hashlib.sha1(s).hexdigest()[:12] in old:
            continue
        m = KEY_RE.match(s.decode("utf-8", "replace"))
        if m and VIDEO_RE.search(m[1]) and not NOT_VIDEO_RE.search(m[1]):
            keys.add(m[1][:40])
    return keys


def noatime(home):
    """True when the filesystem holding home is mounted noatime"""
    best, opts = "", ""
    try:
        with open("/proc/mounts") as f:
            mounts = f.read().splitlines()
        for line in mounts:
            c = line.split()
            if len(c) > 3 and (home == c[1] or home.startswith(c[1].rstrip("/") + "/")) and len(c[1]) > len(best):
                best, opts = c[1], c[3]
    except OSError:
        return False
    return "noatime" in opts.split(",")


DAY_NS = 86400 * 10 ** 9


def load(path):
    with open(path) as f:
        return json.load(f)


def dump(obj, path):
    with open(path, "w") as f:
        json.dump(obj, f)


def before(out, state, tokens, home=None):
    home = home or os.path.expanduser("~")
    rd = roots(home, tokens)
    files = scan(home, rd)
    prev = {}
    try:
        prev = load(state).get("written", {})
    except (OSError, ValueError):
        pass
    # last session's settings files, unchanged since: their access time now, to see the game read them
    watch = {}
    for p, sha in prev.items():
        f = files.get(p)
        if f and f["sha"] == sha:
            readable = f["atime"] <= f["mtime"] or time.time_ns() - f["atime"] > DAY_NS   # relatime would update it
            watch[p] = {"atime": f["atime"], "conclusive": readable}
    dump({"home": home, "tokens": list(tokens), "files": files, "watch": watch,
               "at": time.time(), "noatime": noatime(home)}, out)


def after(bfile, state, run_dir, home=None):
    b = load(bfile)
    home = home or b["home"]
    now = scan(home, roots(home, b["tokens"]))     # again: the first start may have made the directory
    rows, keys, changed, new = [], set(), [], []
    for p, f in sorted(now.items()):
        old = b["files"].get(p)
        if old and old["sha"] == f["sha"]:
            continue
        if old:
            changed.append(p)
            try:
                k = video_keys(read(os.path.join(home, p)), old["lines"])
            except OSError:
                k = set()
            keys |= k
            rows.append((p, "changed", ",".join(sorted(k)) or "-"))
        else:
            new.append(p)
            rows.append((p, "new", "-"))
    reread = None
    if b["watch"] and not b.get("noatime"):
        seen = []
        for p, w in b["watch"].items():
            try:
                at = os.stat(os.path.join(home, p)).st_atime_ns
            except OSError:
                continue
            if at > w["atime"]:
                seen.append(True)
                rows.append((p, "reread", "-"))
            elif w["conclusive"]:
                seen.append(False)
                rows.append((p, "unread", "-"))
        if seen:
            reread = "yes" if any(seen) else "no"
    written = {p: now[p]["sha"] for p in changed + new if SETTINGS_RE.search(os.path.basename(p))}
    if written:
        os.makedirs(os.path.dirname(state) or ".", exist_ok=True)
        tmp = state + ".tmp"
        dump({"written": written, "at": time.time()}, tmp)
        os.replace(tmp, state)
    with open(os.path.join(run_dir, "config.tsv"), "w") as f:
        f.write("path\tstatus\tvideo_keys\n" + "".join("%s\t%s\t%s\n" % r for r in rows))
    lines = ["config_files=%d" % len(now), "config_changed=%d" % len(changed), "config_new=%d" % len(new),
             "config_video_keys=%s" % ",".join(sorted(keys))]
    if reread:
        lines.append("settings_reread=%s" % reread)
    return lines


def main(argv):
    if len(argv) >= 4 and argv[1] == "before":
        before(argv[2], argv[3], argv[4:])
        return 0
    if len(argv) == 5 and argv[1] == "after":
        print("\n".join(after(argv[2], argv[3], argv[4])))
        return 0
    print(__doc__.split("\n\n")[0], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
