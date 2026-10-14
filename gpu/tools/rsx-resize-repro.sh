#!/bin/sh
# Reproduce graph exception 0x106 (docs/RSX.md): xeglgears under Xorg/glamor, resized in steps.
# Run from SSH after a fresh boot (it may wedge the GPU). Push dumps (libdrm, every submit):
#   ~/pb-x.txt (Xorg/glamor), ~/pb-gears.txt (xeglgears); sizes tried in ~/resize.log
RSX_X_ENV="NOUVEAU_LIBDRM_DEBUG=1 NOUVEAU_LIBDRM_OUT=/home/tibi/pb-x.txt" \
	exec /home/tibi/bin/rsx-x.sh 300 sh -c '
	NOUVEAU_LIBDRM_DEBUG=1 NOUVEAU_LIBDRM_OUT=/home/tibi/pb-gears.txt xeglgears &
	sleep 15
	python3 - <<PY > /home/tibi/resize.log 2>&1
import ctypes, subprocess, time, re
out = subprocess.run(["xwininfo", "-name", "xeglgears"], capture_output=True, text=True).stdout
wid = int(re.search(r"Window id: (0x[0-9a-f]+)", out).group(1), 16)
x = ctypes.CDLL("libX11.so.6"); d = x.XOpenDisplay(None)
for w, h in [(400, 300), (600, 450), (800, 600), (1000, 700), (1200, 800), (1600, 900),
             (1920, 1080), (1920, 2), (2, 1080), (640, 2), (2, 2), (800, 600)]:
    print(w, h, flush=True)
    x.XResizeWindow(d, wid, w, h); x.XFlush(d)
    time.sleep(3)
print("done", flush=True)
PY
	kill %1'
