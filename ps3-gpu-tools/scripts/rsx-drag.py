# drag.py N: N rapid resizes of the xeglgears window (like dragging a corner)
import ctypes, subprocess, re, sys, time, math
tree = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True, text=True).stdout
wid = int(re.search(r'(0x[0-9a-f]+) "xeglgears', tree).group(1), 16)
x = ctypes.CDLL("libX11.so.6"); d = x.XOpenDisplay(None)
for i in range(int(sys.argv[1])):
    w = int(700 + 600 * math.sin(i / 25)); h = int(500 + 400 * math.sin(i / 31))
    x.XResizeWindow(d, wid, w, h); x.XFlush(d)
    if i % 100 == 0: print(i, w, h, flush=True)
    time.sleep(0.03)
print("done", flush=True)
