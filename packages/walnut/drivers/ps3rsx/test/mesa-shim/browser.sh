#!/bin/sh
# browser.sh NAME URL LOADWAIT [MODE]: Badwolf on our WebKitGTK with GPU compositing (acceleration
# ALWAYS, the DMA-BUF renderer on) in the ppc64 chroot under qemu, run through run.sh's sh mode
# (run.sh DIR sh './browser.sh ...'): the chroot needs badwolf, our webkit2gtk (with patch 0009, or
# nothing composites), xdotool, gcc and libdrm-dev, and our Mesa with mesa-drm-shim
# (docs/research/BROWSERS.md, "GPU compositing"). MODE shim (default): Mesa nv30 on the nouveau
# drm-shim with wrap.c, the pushes of each process in /tmp/cap/NAME/push.bin.<pid> (capture-stats.py,
# validate-capture); MODE llvmpipe: no shim, Mesa's software driver (softpipe in our ppc64 build), to
# check the colours on big-endian; MODE never: acceleration NEVER. XDISP=:N uses an X server outside
# the chroot whose socket is in /tmp/.X11-unix-amd64 (an amd64 Xvfb in wk-amd64:run, the chroot's
# /tmp/.X11-unix-amd64 mounted as its /tmp/.X11-unix): the chroot's Xvfb on our Mesa has no GLX visual
# GTK 3 accepts ("No available configurations"), and GTK must create a GL context or WebKit turns
# compositing off. After 30 s the window is sized to 1280x720, after LOADWAIT seconds a screenshot,
# then 20 wheel steps down and 5 up, 1 s apart, W2 seconds (default 60) and another screenshot.
N=$1; URL=$2; W=$3; MODE=${4:-shim}
cd /tmp; O=/tmp/cap/$N; rm -rf $O; mkdir -p $O
[ -e wrap.so ] || gcc -O1 -shared -fPIC -I/usr/include/libdrm -o wrap.so wrap.c -ldl
if [ -n "${XDISP:-}" ]; then	# an X server outside (the amd64 Xvfb, its socket in /tmp/.X11-unix-amd64)
	mkdir -p /tmp/.X11-unix; ln -sf /tmp/.X11-unix-amd64/X${XDISP#:} /tmp/.X11-unix/X${XDISP#:}; XP=; export DISPLAY=$XDISP
else
	rm -f /tmp/.X97-lock /tmp/.X11-unix/X97
	Xvfb :97 -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
	sleep 3; export DISPLAY=:97
fi
export HOME=/tmp/home; rm -rf $HOME; mkdir -p $HOME
eval $(dbus-launch --sh-syntax)
export WALNUT_BROWSER_ACCEL=always WEBKIT_DISABLE_DMABUF_RENDERER=0
case $MODE in
shim) export PUSHDUMP=$O/push.bin PUSHDUMP_PERPID=1 NOUVEAU_CHIPSET=47 MESA_LOADER_DRIVER_OVERRIDE=nouveau
	PRE=/tmp/wrap.so:/usr/lib/powerpc64-linux-gnu/libnouveau_noop_drm_shim.so ;;
never) export WALNUT_BROWSER_ACCEL=never WEBKIT_DISABLE_DMABUF_RENDERER=1; PRE= ;;
*) PRE= ;;
esac
LD_PRELOAD=$PRE badwolf "$URL" > $O/badwolf.log 2>&1 &
sleep 30
for w in $(xdotool search --class badwolf 2>/dev/null); do xdotool windowsize $w 1280 720 2>/dev/null; done
sleep $W
ps -eo pid,comm,rss,time | grep -E 'badwolf|WebKit' > $O/ps.txt
for p in $(ps -eo pid=,comm= | awk '$2 ~ /^WebKitWebProces/ {print $1}'); do grep -E 'gallium|dri|EGL|gbm|drm' /proc/$p/maps | awk '{print $6}' | sort -u > $O/web-maps.txt; done
xwd -root -silent | convert xwd:- $O/loaded.png
xdotool mousemove 640 400
for i in $(seq 20); do xdotool click 5; sleep 1; done
for i in $(seq 5); do xdotool click 4; sleep 1; done
sleep ${W2:-60}
xwd -root -silent | convert xwd:- $O/scrolled.png
for p in $(ps -eo pid=,comm= | awk '$2=="badwolf"||$2 ~ /^WebKit/ {print $1}'); do kill $p; done
sleep 5; [ -z "$XP" ] || kill $XP; kill $DBUS_SESSION_BUS_PID 2>/dev/null
ls -la $O
