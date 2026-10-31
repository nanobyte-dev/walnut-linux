#!/usr/bin/python3 -I
# grabmenu.py: a stand-in menu for autogm-check.sh (never shipped). Copied as "jgmenu" (or any
# name: its comm is the file's name), it maps an override-redirect window (a menu) and grabs the
# keyboard and the pointer, as jgmenu does while open, and keeps them until it is killed.
#   grabmenu.py [--hide-on-startup] [--state FILE] [--spawn COMMAND...]
#     --hide-on-startup  no window, no grab: a hidden jgmenu (what the guard starts again)
#     --state FILE       "grabbed" written to FILE once it holds both grabs
#     --spawn COMMAND    started (a session of its own, as jgmenu's spawn) once it holds them:
#                        the menu still open while the game starts
import ctypes
import subprocess
import sys
import time

args = sys.argv[1:]
hide = state = None
spawn = []
while args:
    a = args.pop(0)
    if a == "--hide-on-startup":
        hide = True
    elif a == "--state":
        state = args.pop(0)
    elif a == "--spawn":
        spawn, args = args, []
if hide:
    while True:
        time.sleep(3600)
x = ctypes.CDLL("libX11.so.6")
vp, ul = ctypes.c_void_p, ctypes.c_ulong
x.XOpenDisplay.restype = vp
x.XOpenDisplay.argtypes = [ctypes.c_char_p]
d = x.XOpenDisplay(None)
if not d:
    sys.exit("grabmenu: no display")
x.XDefaultRootWindow.argtypes = [vp]
x.XDefaultRootWindow.restype = ul
root = x.XDefaultRootWindow(d)


class SetAttr(ctypes.Structure):
    _fields_ = [("background_pixmap", ul), ("background_pixel", ul), ("border_pixmap", ul),
                ("border_pixel", ul), ("bit_gravity", ctypes.c_int), ("win_gravity", ctypes.c_int),
                ("backing_store", ctypes.c_int), ("backing_planes", ul), ("backing_pixel", ul),
                ("save_under", ctypes.c_int), ("event_mask", ctypes.c_long),
                ("do_not_propagate_mask", ctypes.c_long), ("override_redirect", ctypes.c_int),
                ("colormap", ul), ("cursor", ul)]


x.XCreateWindow.argtypes = [vp, ul, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint,
                            ctypes.c_uint, ctypes.c_int, ctypes.c_uint, vp, ul, ctypes.POINTER(SetAttr)]
x.XCreateWindow.restype = ul
a = SetAttr()
a.override_redirect = 1
a.background_pixel = 0x808080
w = x.XCreateWindow(d, root, 10, 10, 200, 300, 0, 0, 1, None, (1 << 9) | (1 << 1), ctypes.byref(a))
x.XMapRaised.argtypes = [vp, ul]
x.XMapRaised(d, w)
x.XGrabKeyboard.argtypes = [vp, ul, ctypes.c_int, ctypes.c_int, ctypes.c_int, ul]
x.XGrabPointer.argtypes = [vp, ul, ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_int, ul, ul, ul]
x.XSync.argtypes = [vp, ctypes.c_int]
x.XSync(d, 0)
for i in range(200):
    if x.XGrabKeyboard(d, root, 1, 1, 1, 0) == 0:
        break
    time.sleep(0.01)
for i in range(200):
    if x.XGrabPointer(d, root, 0, 4 | 8, 1, 1, 0, 0, 0) == 0:
        break
    time.sleep(0.01)
x.XSync(d, 0)
if state:
    with open(state, "w") as f:
        f.write("grabbed\n")
if spawn:
    subprocess.Popen(spawn, start_new_session=True, stdin=subprocess.DEVNULL)
while True:
    time.sleep(3600)
