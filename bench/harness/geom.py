#!/usr/bin/env python3
"""geom.py PID AT_S [FILE]: where the game's window is on the screen, one TSV row (appended to FILE, else
printed). Run by client.sh's shot_take after each screenshot (a background job at nice 19), on the PS3
with x11-utils (xwininfo, xprop; in the image) and python3 only.
Columns: at_s x y w h fullscreen screen_w screen_h by cursor cursor_name (by: pid, or size for the no-pid
fallback; docs/GAMEDB.md "Display"). No window found: "-" for x y w h and fullscreen, the screen's size still
given. No screen (no X): nothing is written.
cursor: 1 when an X pointer is visible over the game's window (XFixes' cursor image has a pixel that isn't
transparent, at a position inside the window), 0 when it is hidden or elsewhere, "-" when that can't be told
(no libXfixes, no window); cursor_name the cursor's theme name (left_ptr) or "-". A game that draws its own
pointer in GL hides the X one; a pointer hidden with XFixesHideCursor still reads as visible (REVIEW.md
`pointer: hidden` corrects it).
The window: a viewable window of 64x64 or more whose _NET_WM_PID is PID or one of its descendants (a
launcher script's child); else the largest viewable one that names no pid, isn't a desktop/dock/
splash/tooltip-type window and isn't from another process. The largest of the matches wins.
fullscreen: 1 for _NET_WM_STATE_FULLSCREEN, or a window covering the whole screen."""
import os
import re
import subprocess
import sys

HEADER = "at_s\tx\ty\tw\th\tfullscreen\tscreen_w\tscreen_h\tby\tcursor\tcursor_name"
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


def cursor():
    """(x, y, visible, name) of the X pointer through XFixes (ctypes: libX11 and libXfixes, in the image with
    Mesa), or None"""
    import ctypes

    class Image(ctypes.Structure):
        _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short), ("width", ctypes.c_ushort),
                    ("height", ctypes.c_ushort), ("xhot", ctypes.c_ushort), ("yhot", ctypes.c_ushort),
                    ("cursor_serial", ctypes.c_ulong), ("pixels", ctypes.POINTER(ctypes.c_ulong)),
                    ("atom", ctypes.c_ulong), ("name", ctypes.c_char_p)]
    try:
        x11 = ctypes.CDLL("libX11.so.6")
        xf = ctypes.CDLL("libXfixes.so.3")
    except OSError:
        return None
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    x11.XFree.argtypes = [ctypes.c_void_p]
    xf.XFixesQueryExtension.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    xf.XFixesGetCursorImage.restype = ctypes.POINTER(Image)
    xf.XFixesGetCursorImage.argtypes = [ctypes.c_void_p]
    dpy = x11.XOpenDisplay(None)
    if not dpy:
        return None
    try:
        ev, err = ctypes.c_int(), ctypes.c_int()
        if not xf.XFixesQueryExtension(dpy, ctypes.byref(ev), ctypes.byref(err)):
            return None
        img = xf.XFixesGetCursorImage(dpy)
        if not img:
            return None
        c = img.contents
        visible = any(c.pixels[i] >> 24 & 0xFF for i in range(c.width * c.height))
        name = (c.name or b"").decode("ascii", "replace") or "-"
        out = (c.x, c.y, visible, name)
        x11.XFree(img)
        return out
    finally:
        x11.XCloseDisplay(dpy)


def cursor_cols(g, cur):
    """the cursor and cursor_name columns for window g (x, y, w, h, ...) and cursor() 's answer"""
    if not g or not cur:
        return "-", "-"
    x, y, visible, name = cur
    over = g[0] <= x < g[0] + g[2] and g[1] <= y < g[1] + g[3]
    return ("1" if visible and over else "0"), name


def main(argv):
    if len(argv) < 3 or not argv[1].isdigit():
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        return 2
    screen = screen_size()
    if not screen:
        return 1
    tree = run("xwininfo", "-root", "-tree")
    g = pick(tree, family(int(argv[1])), screen)
    try:
        cur = cursor()
    except Exception:   # an odd libXfixes: the pointer stays unknown, the geometry is still written
        cur = None
    row = "%s\t%s\t%d\t%d\t%s\t%s\t%s" % (argv[2], "\t".join(map(str, g[:5])) if g else "-\t-\t-\t-\t-", *screen,
                                     g[5] if g else "-", *cursor_cols(g, cur))
    if len(argv) > 3:
        new = not os.path.exists(argv[3])
        with open(argv[3], "a") as f:
            f.write((HEADER + "\n" if new else "") + row + "\n")
    else:
        print(row)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
