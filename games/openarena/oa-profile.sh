#!/bin/bash
# oa-profile.sh [cvars...]: X client for ps3-bench.sh: one bot match, perf profile of the game
# (no -a: system-wide call graphs hung the PS3, docs/DESKTOP.md "Early performance fixes") into $BENCH_DIR, then quits.
# Timeline: 30 s in, 8 s of samples; the match ends at 60 s.
{
OA=/usr/games/openarena
D=${BENCH_DIR:-/dev/shm}
$OA "$@" +set bot_minplayers 5 +map oa_dm4 &
sleep 3; p=$(pgrep -n -x ioquake3)
[ -n "$p" ] || { echo "no ioquake3"; wait; exit 1; }
sleep 27
sudo timeout 30 perf record -q -F 999 -g -o "$D/perf.data" -p "$p" -- sleep 8 2>&1
grep -E 'VmRSS|VmSwap' /proc/$p/status
sleep 22
kill "$p"; for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$p" 2>/dev/null || break; sleep 1; done
kill -9 "$p" 2>/dev/null
sudo chown "$(id -u)" "$D/perf.data"
# audio share: OpenAL Soft, ALSA, Vorbis, ioquake3's own mixer, SDL audio
perf report -i "$D/perf.data" --no-children --sort dso --stdio 2>/dev/null | grep -E '%' | head -15
perf report -i "$D/perf.data" --no-children --sort sym --stdio 2>/dev/null |
	grep -E '%' | grep -iE 'S_Paint|S_Transfer|S_Update|alsoft|Mix|vorbis|snd_|SDL_.*Audio' | head -15
exit
}
