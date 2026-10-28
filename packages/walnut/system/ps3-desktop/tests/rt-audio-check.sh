#!/bin/sh
# rt-audio-check.sh: ps3-rt-audio on this host, with no PS3 and no real-time limit: which threads
# of a stand-in game (tests/rtaudio.c) it would set to SCHED_RR (a stub chrt records the calls).
# Covers: the thread named like SDL's, the unnamed one polling the "sound device" (a FIFO standing
# in for /dev/snd/timer, PS3_RT_AUDIO_FDS), not the one polling something else, not one asleep,
# never the main thread (it polls the FIFO too), each thread once over several scans, and
# PS3_GAME_RT_AUDIO=off.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d) || exit 1
trap 'kill $g 2>/dev/null; rm -rf "$W"' EXIT
pass=0 fail=0
ok() { echo "PASS: $1"; pass=$((pass + 1)); }
bad() { echo "FAIL: $1"; fail=$((fail + 1)); }
cc -O1 -pthread -o "$W/rtaudio" "$(dirname "$0")/rtaudio.c" || exit 1
mkfifo "$W/snd"
printf '#!/bin/sh\necho "$*" >> %s/chrt.log\n' "$W" > "$W/chrt"; chmod +x "$W/chrt"
export PS3_RT_AUDIO_CHRT=$W/chrt PS3_RT_AUDIO_FDS="^$W/snd\$"
RA=$here/files/libexec/ps3-rt-audio

start() {	# the stand-in game, its tids in N S O Z
	"$W/rtaudio" "$W/snd" < /dev/null > "$W/out" 2> /dev/null & g=$!
	i=0; until grep -q ready "$W/out" 2>/dev/null || [ $i -ge 50 ]; do sleep 0.1; i=$((i + 1)); done
	read -r _ N S O Z < "$W/out"
}
start
for i in 1 2 3; do sh "$RA" scan "$W/st" $g 2> "$W/log$i"; sleep 0.2; done
[ "$(grep -c "^-r -p 1 $N\$" "$W/chrt.log")" = 1 ] && ok "the SDLAudio thread, once" || bad "the SDLAudio thread: $(cat "$W/chrt.log")"
[ "$(grep -c "^-r -p 1 $S\$" "$W/chrt.log")" = 1 ] && ok "the thread polling the sound device, once" || bad "the sound thread"
grep -q " $O\$" "$W/chrt.log" && bad "the thread polling something else" || ok "not the thread polling something else"
grep -q " $Z\$" "$W/chrt.log" && bad "the sleeping thread" || ok "not the sleeping thread"
grep -q " $g\$" "$W/chrt.log" && bad "the main thread" || ok "never the main thread"
grep -q 'SDLAudioP1.*(named)' "$W/log1" && grep -q 'waits on the sound device' "$W/log1" && ok "logged" || bad "the log: $(cat "$W/log1")"
kill $g; wait $g 2>/dev/null
rm -f "$W/chrt.log"
PS3_GAME_RT_AUDIO=off start
sh "$RA" scan "$W/st" $g 2> "$W/log4"
[ ! -s "$W/chrt.log" ] && grep -q 'PS3_GAME_RT_AUDIO=off' "$W/log4" && ok "PS3_GAME_RT_AUDIO=off" || bad "PS3_GAME_RT_AUDIO=off: $(cat "$W/chrt.log" "$W/log4" 2>/dev/null)"
echo "rt-audio-check: $pass passed, $fail failed"
[ $fail = 0 ]
