#!/usr/bin/python3
# xkeys.py TITLE KEY...: key presses through the X server's XTEST extension (libX11 + libXtst by
# ctypes, both in the image; no xdotool, no input device, no root). The window whose name starts
# with TITLE (case-insensitive) gets the X input focus first, then each KEY (an X keysym name:
# Return, space, Up, Escape) is pressed, held HOLD s (0.25) and released; "+N" waits N seconds;
# "@X,Y" moves the pointer to X,Y (screen pixels) and clicks the left button.
# Used by smoke.inc's smoke_xkeys. Exit 1 when the display or the window is missing.
import ctypes, os, sys, time

HOLD = float(os.environ.get("XKEYS_HOLD", "0.25"))
x = ctypes.CDLL("libX11.so.6")
tst = ctypes.CDLL("libXtst.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XOpenDisplay.argtypes = [ctypes.c_char_p]
x.XDefaultRootWindow.restype = ctypes.c_ulong
x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x.XKeysymToKeycode.restype = ctypes.c_ubyte
x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x.XStringToKeysym.restype = ctypes.c_ulong
x.XStringToKeysym.argtypes = [ctypes.c_char_p]


class XTextProperty(ctypes.Structure):
    _fields_ = [("value", ctypes.c_void_p), ("encoding", ctypes.c_ulong), ("format", ctypes.c_int),
                ("nitems", ctypes.c_ulong)]


# XGetWMName, not XFetchName: XFetchName returns only a STRING-typed WM_NAME, and sdl2-compat's SDL3
# sets it as UTF8_STRING (Chromium B.S.U., 2026-10-28: its window was never found); the bytes of an
# ASCII title are the same either way
x.XGetWMName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(XTextProperty)]
x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                         ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                         ctypes.POINTER(ctypes.c_uint)]
x.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
x.XFree.argtypes = [ctypes.c_void_p]
x.XFlush.argtypes = [ctypes.c_void_p]
x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
tst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
tst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
tst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
# the default error handler exits the process: a window that went away must not kill the helper
ERR = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
handler = ERR(lambda d, e: 0)
x.XSetErrorHandler(handler)


def find(dpy, win, title):
    root, parent = ctypes.c_ulong(), ctypes.c_ulong()
    kids, n = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
    tp = XTextProperty()
    if x.XGetWMName(dpy, win, ctypes.byref(tp)) and tp.value:
        name = ctypes.string_at(tp.value, tp.nitems)
        x.XFree(tp.value)
        if name.decode("latin1").lower().startswith(title):
            return win
    if not x.XQueryTree(dpy, win, ctypes.byref(root), ctypes.byref(parent), ctypes.byref(kids), ctypes.byref(n)):
        return None
    found = None
    for i in range(n.value):
        found = find(dpy, kids[i], title)
        if found:
            break
    if kids:
        x.XFree(kids)
    return found


def main():
    title, keys = sys.argv[1].lower(), sys.argv[2:]
    dpy = x.XOpenDisplay(None)
    if not dpy:
        print("xkeys: no display"); return 1
    win = find(dpy, x.XDefaultRootWindow(dpy), title)
    if not win:
        print("xkeys: no window named %s*" % sys.argv[1]); return 1
    for k in keys:
        if k.startswith("+"):
            time.sleep(float(k[1:])); continue
        if k.startswith("@"):	# @X,Y: the pointer to X,Y on the screen, then a left click held HOLD s
            px, py = (int(v) for v in k[1:].split(","))
            x.XSetInputFocus(dpy, win, 2, 0)
            tst.XTestFakeMotionEvent(dpy, -1, px, py, 0); x.XSync(dpy, 0)
            time.sleep(0.3)
            tst.XTestFakeButtonEvent(dpy, 1, 1, 0); x.XSync(dpy, 0)
            time.sleep(HOLD)
            tst.XTestFakeButtonEvent(dpy, 1, 0, 0); x.XSync(dpy, 0)
            print("xkeys: click at %d,%d at %.3f" % (px, py, time.time()), flush=True)
            time.sleep(0.5)
            continue
        code = x.XKeysymToKeycode(dpy, x.XStringToKeysym(k.encode()))
        if not code:
            print("xkeys: no keycode for", k); return 1
        x.XSetInputFocus(dpy, win, 2, 0)	# RevertToParent, CurrentTime
        tst.XTestFakeKeyEvent(dpy, code, 1, 0); x.XSync(dpy, 0)
        time.sleep(HOLD)
        tst.XTestFakeKeyEvent(dpy, code, 0, 0); x.XSync(dpy, 0)
        print("xkeys: %s to window 0x%x at %.3f" % (k, win, time.time()), flush=True)
        time.sleep(0.5)
    return 0


sys.exit(main())
