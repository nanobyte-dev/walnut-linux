#!/bin/sh
# oa-bench.sh <demo> [cvars...]: timedemo without vsync; OpenArena's result plus per-second fps
# (median and 10th percentile, from the Gallium HUD)
pgrep -x ioquake3 && { echo "already running"; exit 1; }
demo=$1; shift
[ -e ~/.openarena/baseoa/demos/"$demo".dm_71 ] || { echo "no demo ~/.openarena/baseoa/demos/$demo.dm_71 (games/openarena/)"; exit 1; }
cd ~; rm -rf /tmp/hud; mkdir /tmp/hud
DISPLAY=:0 GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_DUMP_DIR=/tmp/hud GALLIUM_HUD_PERIOD=1 \
	timeout -s KILL 150 ps3-game openarena "$@" +set r_swapInterval 0 +set com_maxfps 0 \
	+set timedemo 1 +set nextdemo quit +demo "$demo" > /tmp/oa-bench.log 2>&1
r=$(grep -a "frames.*seconds" /tmp/oa-bench.log | tail -1)
s=$(awk 'NR>12{print int($1)}' /tmp/hud/fps | sort -n | awk '{a[NR]=$1} END{printf "median %d p10 %d (%d s)", a[int(NR/2)+1], a[int(NR/10)+1], NR}')
echo "$r | $s"
p=$(pgrep -x ioquake3); [ -n "$p" ] && kill -9 $p; true
