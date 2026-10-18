#!/usr/bin/python3 -I
# xkey.py KEY+KEY... (autogm-check.sh; never shipped): presses the keys in order and lets go of
# them in reverse through XTEST, as the keyboard would (Alt_L+Tab, Super_L+d).
import ctypes
import sys
import time

x = ctypes.CDLL("libX11.so.6")
t = ctypes.CDLL("libXtst.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XOpenDisplay.argtypes = [ctypes.c_char_p]
x.XStringToKeysym.restype = ctypes.c_ulong
x.XStringToKeysym.argtypes = [ctypes.c_char_p]
x.XKeysymToKeycode.restype = ctypes.c_ubyte
x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x.XFlush.argtypes = x.XCloseDisplay.argtypes = [ctypes.c_void_p]
t.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
d = x.XOpenDisplay(None)
if not d:
    sys.exit("xkey: no display")
codes = []
for k in sys.argv[1].split("+"):
    c = x.XKeysymToKeycode(d, x.XStringToKeysym(k.encode()))
    if not c:
        sys.exit(f"xkey: no key {k}")
    codes.append(c)
for c in codes:
    t.XTestFakeKeyEvent(d, c, 1, 0)
    x.XFlush(d)
    time.sleep(0.05)
for c in reversed(codes):
    t.XTestFakeKeyEvent(d, c, 0, 0)
    x.XFlush(d)
    time.sleep(0.05)
x.XCloseDisplay(d)
