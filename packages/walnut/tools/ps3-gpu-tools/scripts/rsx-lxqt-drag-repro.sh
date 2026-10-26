#!/bin/sh
# Reproduce graph exception 0x106 (docs/RSX.md): LXQt on an Xorg started as root with libdrm
# dumping X's pushes (~/pb-x.txt), xeglgears dragged 400 times (~/drag.py). Wedges the GPU.
RSX_X_ENV="NOUVEAU_LIBDRM_DEBUG=1 NOUVEAU_LIBDRM_OUT=/home/tibi/pb-x.txt HOME=/home/tibi" \
	exec /usr/lib/ps3/bin/rsx-x.sh 900 sh -c 'unset NOUVEAU_LIBDRM_DEBUG NOUVEAU_LIBDRM_OUT
	/usr/local/bin/ps3-session > /home/tibi/lxqt-root.log 2>&1 &
	until xwininfo -name pcmanfm-desktop0 >/dev/null 2>&1; do sleep 2; done
	xeglgears > /home/tibi/xg-root.out 2>&1 &
	sleep 15
	python3 /home/tibi/drag.py 400 > /home/tibi/drag.log 2>&1
	sleep 30'
