#!/bin/sh
# Reproduce graph exception 0x106 (docs/RSX.md): xeglgears under Xorg/glamor, resized in steps.
# Run from SSH after a fresh boot (it may wedge the GPU). RESIZE=drag: rapid small resizes. Push dumps (libdrm, every submit):
#   ~/pb-x.txt (Xorg/glamor), ~/pb-gears.txt (xeglgears); sizes tried in ~/resize.log
RSX_X_ENV="NOUVEAU_LIBDRM_DEBUG=1 NOUVEAU_LIBDRM_OUT=/home/tibi/pb-x.txt" \
	exec /usr/lib/ps3/bin/rsx-x.sh 300 sh -c '
	NOUVEAU_LIBDRM_DEBUG=1 NOUVEAU_LIBDRM_OUT=/home/tibi/pb-gears.txt xeglgears &
	python3 - <<PY > /home/tibi/resize.log 2>&1
import ctypes, subprocess, time, re
wid = None
for _ in range(90):
    tree = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True, text=True).stdout
    m = re.search(r"(0x[0-9a-f]+) \"[^\"]*[Gg]ears[^\"]*\"", tree)
    if m:
        wid = int(m.group(1), 16); print("window", m.group(0), flush=True); break
    time.sleep(1)
if wid is None:
    print("no gears window; tree:", tree[:2000]); raise SystemExit(1)
time.sleep(5)
x = ctypes.CDLL("libX11.so.6"); d = x.XOpenDisplay(None)
import math, os
if os.environ.get("RESIZE") == "drag":
    # like dragging a corner: small steps every 30 ms, growing and shrinking
    for i in range(400):
        w = int(700 + 600 * math.sin(i / 25)); h = int(500 + 400 * math.sin(i / 31))
        if i % 20 == 0: print(i, w, h, flush=True)
        x.XResizeWindow(d, wid, w, h); x.XFlush(d)
        time.sleep(0.03)
    time.sleep(5)
else:
    for w, h in [(400, 300), (600, 450), (800, 600), (1000, 700), (1200, 800), (1600, 900),
                 (1920, 1080), (1920, 2), (2, 1080), (640, 2), (2, 2), (800, 600)]:
        print(w, h, flush=True)
        x.XResizeWindow(d, wid, w, h); x.XFlush(d)
        time.sleep(3)
print("done", flush=True)
PY
	kill %1'
