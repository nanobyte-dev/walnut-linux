#!/usr/bin/env python3
"""xloop.py [ITERATIONS]: X/glamor desktop latency, timed (the A/B's desktop case). On the running X
($DISPLAY): one window resized through a cycle of sizes and, at each step, an offscreen pixmap of the
window's size made, filled, copied to the window and freed, then XSync: the buffer churn and blits
glamor does when windows open, close and resize. Prints per-step milliseconds (median, p99, max)
and the total as "rsxab:" lines, and exits 1 if X can't be opened. libX11 through ctypes only (the
image has no xdotool or python3-xlib)."""
import ctypes
import ctypes.util
import statistics
import sys
import time

N = int(sys.argv[1]) if len(sys.argv) > 1 else 600
SIZES = [(640, 480), (1280, 720), (800, 600), (1600, 900), (320, 240), (1024, 768)]

x = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XOpenDisplay.argtypes = [ctypes.c_char_p]
x.XDefaultScreen.argtypes = [ctypes.c_void_p]
x.XRootWindow.restype = ctypes.c_ulong
x.XRootWindow.argtypes = [ctypes.c_void_p, ctypes.c_int]
x.XDefaultDepth.argtypes = [ctypes.c_void_p, ctypes.c_int]
x.XCreateSimpleWindow.restype = ctypes.c_ulong
x.XCreateSimpleWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint,
                                  ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_ulong]
x.XMapWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
x.XCreatePixmap.restype = ctypes.c_ulong
x.XCreatePixmap.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
x.XFreePixmap.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x.XCreateGC.restype = ctypes.c_void_p
x.XCreateGC.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
x.XFreeGC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
x.XSetForeground.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
x.XFillRectangle.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                             ctypes.c_uint, ctypes.c_uint]
x.XCopyArea.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_int,
                        ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_int]
x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
x.XDestroyWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x.XCloseDisplay.argtypes = [ctypes.c_void_p]

dpy = x.XOpenDisplay(None)
if not dpy:
    print("rsxab: xloop error=no_display")
    sys.exit(1)
scr = x.XDefaultScreen(dpy)
root = x.XRootWindow(dpy, scr)
depth = x.XDefaultDepth(dpy, scr)
win = x.XCreateSimpleWindow(dpy, root, 40, 40, 640, 480, 0, 0, 0x202020)
x.XMapWindow(dpy, win)
x.XSync(dpy, 0)
time.sleep(1)
gc = x.XCreateGC(dpy, win, 0, None)
times = []
t_all = time.monotonic()
for i in range(N):
    w, h = SIZES[i % len(SIZES)]
    t = time.monotonic()
    x.XResizeWindow(dpy, win, w, h)
    pm = x.XCreatePixmap(dpy, win, w, h, depth)
    x.XSetForeground(dpy, gc, 0x102030 + (i * 0x050301 & 0xffffff))
    x.XFillRectangle(dpy, pm, gc, 0, 0, w, h)
    x.XSetForeground(dpy, gc, 0xc0c0c0)
    x.XFillRectangle(dpy, pm, gc, w // 4, h // 4, w // 2, h // 2)
    x.XCopyArea(dpy, pm, win, gc, 0, 0, w, h, 0, 0)
    x.XFreePixmap(dpy, pm)
    x.XSync(dpy, 0)
    times.append((time.monotonic() - t) * 1000)
total = time.monotonic() - t_all
x.XFreeGC(dpy, gc)
x.XDestroyWindow(dpy, win)
x.XCloseDisplay(dpy)
times.sort()
print("rsxab: xloop steps=%d total_s=%.2f median_ms=%.2f p99_ms=%.2f max_ms=%.2f" % (
    N, total, statistics.median(times), times[int(0.99 * (N - 1))], times[-1]))
