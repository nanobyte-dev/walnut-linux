#!/bin/sh
# oa-run.sh <seconds> [perf-at-seconds]: one OpenArena bot match, fps to /tmp/hud/fps, stopped for sure
pgrep -x ioquake3 && { echo "already running"; exit 1; }
rm -rf /tmp/hud; mkdir /tmp/hud; cd ~
(DISPLAY=:0 GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_DUMP_DIR=/tmp/hud GALLIUM_HUD_PERIOD=1 \
	ps3-game openarena $OA_ARGS +set bot_minplayers 5 +map oa_dm4 >/tmp/oa.log 2>&1 &)
sleep 3; p=$(pgrep -x ioquake3); r0=$(cat /sys/class/net/eth0/statistics/rx_bytes)
vmstat 1 "$1" > /tmp/vm.log &
if [ -n "$2" ]; then sleep "$2"; sudo perf record -q -g -o /tmp/perf.data -p "$p" -- sleep 8 2>/dev/null; sleep $(($1 - $2 - 8)); else sleep "$1"; fi
grep -E "VmRSS|VmSwap" /proc/$p/status; r1=$(cat /sys/class/net/eth0/statistics/rx_bytes); echo "nfs rx $(( (r1-r0)/1048576 )) MiB"; kill "$p"; for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$p" 2>/dev/null || break; sleep 1; done
kill -9 "$p" 2>/dev/null; sleep 1; pgrep -x ioquake3 && echo "STILL RUNNING"
echo "fps: $(awk 'NR>15{printf "%d ", $1}' /tmp/hud/fps)"
echo "vmstat si/so us/sy/id:"; awk 'NR>2{printf "%s/%s %s/%s/%s  ", $7,$8,$13,$14,$15}' /tmp/vm.log; echo
