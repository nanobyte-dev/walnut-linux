#!/usr/bin/env python3
"""geom.py PID AT_S [FILE]: where the game's window is on the screen, one TSV row (appended to FILE, else
printed). Run by client.sh's shot_take after each screenshot (a background job at nice 19), on the PS3
with x11-utils (xwininfo, xprop; in the image) and python3 only.
Columns: at_s x y w h fullscreen screen_w screen_h by (by: pid, or size for the no-pid fallback; docs/GAMEDB.md "Display"). No window found: "-" for
x y w h and fullscreen, the screen's size still given. No screen (no X): nothing is written.
The window: a viewable window of 64x64 or more whose _NET_WM_PID is PID or one of its descendants (a
launcher script's child); else the largest viewable one that names no pid, isn't a desktop/dock/
splash/tooltip-type window and isn't from another process. The largest of the matches wins.
fullscreen: 1 for _NET_WM_STATE_FULLSCREEN, or a window covering the whole screen."""
import os
import re
import subprocess
import sys

HEADER = "at_s\tx\ty\tw\th\tfullscreen\tscreen_w\tscreen_h\tby"
LINE = re.compile(r"^(\s+)(0x[0-9a-f]+) .*?\s(\d+)x(\d+)\+(-?\d+)\+(-?\d+)\s+\+(-?\d+)\+(-?\d+)\s*$")
SKIP_TYPES = ("_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_SPLASH",
              "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_WM_WINDOW_TYPE_MENU",
              "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_COMBO")
MIN_SIDE = 64


def run(*cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def family(pid):
    """pid and all its descendants (from /proc/*/stat)"""
    kids = {}
    for p in os.listdir("/proc"):
        if p.isdigit():
            try:
                with open("/proc/%s/stat" % p) as f:
                    ppid = int(f.read().rsplit(")", 1)[1].split()[1])
            except (OSError, ValueError, IndexError):
                continue
            kids.setdefault(ppid, []).append(int(p))
    out, todo = set(), [pid]
    while todo:
        p = todo.pop()
        if p not in out:
            out.add(p)
            todo += kids.get(p, [])
    return out


def screen_size():
    m = re.search(r"Width:\s*(\d+).*?Height:\s*(\d+)", run("xwininfo", "-root"), re.S)
    return (int(m[1]), int(m[2])) if m else None


def pick(tree, pids, screen):
    """tree: the text of xwininfo -root -tree; the (x, y, w, h, fullscreen) of the game's window or None"""
    best = {}   # rank -> (area, geometry): 0 the pid matches, 1 no pid at all
    for line in tree.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        wid = m[2]
        w, h, x, y = int(m[3]), int(m[4]), int(m[7]), int(m[8])
        if w < MIN_SIDE or h < MIN_SIDE:
            continue
        props = run("xprop", "-id", wid, "_NET_WM_PID", "_NET_WM_STATE", "_NET_WM_WINDOW_TYPE")
        pm = re.search(r"_NET_WM_PID\(CARDINAL\) = (\d+)", props)
        if pm and int(pm[1]) not in pids:
            continue
        if any(t in props for t in SKIP_TYPES):
            continue
        if "IsViewable" not in run("xwininfo", "-id", wid):
            continue
        fs = "_NET_WM_STATE_FULLSCREEN" in props or (x <= 0 and y <= 0 and x + w >= screen[0] and y + h >= screen[1])
        rank = 0 if pm else 1
        if rank not in best or w * h > best[rank][0]:
            best[rank] = (w * h, (x, y, w, h, 1 if fs else 0, "pid" if pm else "size"))
    for rank in (0, 1):
        if rank in best:
            return best[rank][1]
    return None


def main(argv):
    if len(argv) < 3 or not argv[1].isdigit():
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        return 2
    screen = screen_size()
    if not screen:
        return 1
    tree = run("xwininfo", "-root", "-tree")
    g = pick(tree, family(int(argv[1])), screen)
    row = "%s\t%s\t%d\t%d\t%s" % (argv[2], "\t".join(map(str, g[:5])) if g else "-\t-\t-\t-\t-", *screen, g[5] if g else "-")
    if len(argv) > 3:
        new = not os.path.exists(argv[3])
        with open(argv[3], "a") as f:
            f.write((HEADER + "\n" if new else "") + row + "\n")
    else:
        print(row)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
