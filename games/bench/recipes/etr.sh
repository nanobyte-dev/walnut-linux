# Extreme Tux Racer 0.8.4: no demo mode, so a race played once by a person is recorded as keyboard
# events (evemu-record) and replayed into a virtual keyboard (evemu-device + evemu-play). The same
# events at the same times from when ETR's window appears: ETR's physics follow the frame time,
# so at another frame rate Tux's line can drift a little (docs/GAME-PLAN.md). A marker event
# written into the keyboard when the window appears makes the recording start there.
#   variant record: you play (menus too, then quit from the menu); the events land in the run
#                   directory as etr-race.evemu, to copy to assets/etr-race.evemu, cut after the
#                   last input that matters (assets/etr-race.evemu: Practice, one course, 69 s)
#   base:           the recorded race, replayed; fps from the Gallium HUD
SUITE=1
COMM=etr
ETR_KBD=/dev/input/event0	# the Logitech receiver's keyboard on the PS3
ETR_REC=$ASSETS/etr-race.evemu
CMD=(/usr/games/etr)
DURATION=100	# the replay closes ETR at ~83 s
HUD_SKIP=15
ETR_MODE=replay
etr_window() {	# waits up to 60 s for ETR's window to be mapped
	for i in $(seq 600); do
		xwininfo -root -tree 2>/dev/null | grep -qiE '"(Extreme Tux Racer|etr)"' && return 0; sleep 0.1
	done; return 1
}
marker() {	# an MSC_SCAN + SYN_REPORT written into the keyboard: the recording's time zero (a lone
		# SYN_REPORT is dropped by the input core); games ignore scan codes
	python3 -c 'import os,struct; f=os.open("'"$ETR_KBD"'",os.O_WRONLY)
os.write(f,struct.pack("@llHHi",0,0,4,4,0)+struct.pack("@llHHi",0,0,0,0,0)); os.close(f)'
}
during() {
	if [ "$ETR_MODE" = record ]; then
		etr_window || { echo "no ETR window"; return 1; }
		evemu-record "$ETR_KBD" > "$BENCH_DIR/etr-race.evemu" & r=$!
		sleep 0.3; marker; echo "recording from $EPOCHREALTIME"
		wait $r
	else
		[ -s "$ETR_REC" ] || { echo "no recording $ETR_REC"; return 1; }
		# a virtual keyboard like the recorded one (root: /dev/uinput), gone by itself after the run
		sudo -n timeout $((DURATION + 30)) evemu-device "$ETR_REC" > "$BENCH_DIR/.evdev" 2>&1 &
		for i in $(seq 50); do node=$(awk '{print $NF}' "$BENCH_DIR/.evdev" | grep -m1 /dev/input/); [ -n "$node" ] && break; sleep 0.1; done
		[ -n "$node" ] || { echo "no virtual keyboard"; cat "$BENCH_DIR/.evdev"; return 1; }
		etr_window || { echo "no ETR window"; return 1; }
		echo "replaying into $node from $EPOCHREALTIME"
		evemu-play "$node" < "$ETR_REC"
		echo "replay done at $EPOCHREALTIME"
		# the recording ends with the last steering input (69 s; the results screen and the quit
		# that followed were cut): 5 s more of the race, then ETR is closed
		sleep 5; pkill -x etr
	fi
}
variant_record() { ETR_MODE=record; DURATION=600; }
# Our extremetuxracer (etr/): fewer, larger GL submissions, each with an environment variable
# (src/glfast.h in the package). base has them all on, gloff is ETR_GL=0 (the game as Debian
# draws it), a no* variant is base with one change off.
variant_gloff() { ENV+=(ETR_GL=0); }
variant_noterrain() { ENV+=(ETR_GL_TERRAIN=0); }
variant_notux() { ENV+=(ETR_GL_TUX=0); }
variant_noshadow() { ENV+=(ETR_GL_SHADOW=0); }
variant_notracks() { ENV+=(ETR_GL_TRACKS=0); }
variant_notrees() { ENV+=(ETR_GL_TREES=0); }
variant_treesorder() { ENV+=(ETR_GL_TREES_GROUP=0); }	# trees and items by type only where the course's list has them so
variant_noparticles() { ENV+=(ETR_GL_PARTICLES=0); }
variant_nosnow() { ENV+=(ETR_GL_SNOW=0); }
variant_nohud() { ENV+=(ETR_GL_HUD=0); }
variant_nobind() { ENV+=(ETR_GL_BIND=0); }
