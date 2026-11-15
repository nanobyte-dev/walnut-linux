# OpenTyrian 2.1 (our opentyrian, 2.1.20260912-1+ps3.N, and tyrian-data from the licensed suite):
# smoke test, not benchmarked. SDL2 renders the 320x200 8-bit VGA surface through a scaler (Scale2x
# by default, in software) to a texture that SDL draws scaled up to the 1920x1080 desktop-fullscreen
# window; no OpenGL needs from the game itself. Data: /usr/share/games/tyrian (the freeware Tyrian
# 2.1, gamedb/games/opentyrian/licensed/tyrian-data/tyrian-data.sh -> package tyrian-data), found without options.
# Input: a session played once by a person, recorded as keyboard events (evemu-record) and replayed
# into a virtual keyboard (evemu-device + evemu-play), as gamedb:etr does. The events replay at the
# same times from when OpenTyrian's window appears (a marker event written into the keyboard then is
# the recording's time zero). Scripted keys (smoke_xkeys) never reached a level: 2026-11-15 they
# stopped in the full game's Data screen three times.
#   variant record: you play from the title screen (New Game, episode, difficulty, Play Next Level,
#                   a minute or two of the level), then quit from the menu; the events land in the
#                   run directory as ot-session.evemu, to copy to assets/ot-session.evemu
#   base:           the recording, replayed; the screenshots count from the replay's start
#   menu:           no input: the title screen, then the attract demo (2026-11-15: drawn right)
# Settings: ~/.config/opentyrian/opentyrian.cfg (fullscreen 0 = desktop fullscreen, scaler) is written
# by setup() and restored by BACKUP; the game rewrites it at exit.
COMM=opentyrian
. "$BENCH_LIB/smoke.inc"
BACKUP=("$HOME/.config/opentyrian/opentyrian.cfg")
OT_SCALER=Scale2x
OT_KBD=/dev/input/event0	# the Logitech receiver's keyboard on the PS3
OT_REC=$ASSETS/ot-session.evemu
OT_MODE=replay
CMD=(/usr/games/opentyrian)
NEEDS=(evemu-device evemu-play)
setup() {
	mkdir -p "$HOME/.config/opentyrian"
	printf "section video\n\titem fullscreen 0\n\titem scaler '%s'\n\titem scaling_mode 'Fit 8:5'\n" "$OT_SCALER" > "$HOME/.config/opentyrian/opentyrian.cfg"
	[ -e /usr/share/games/tyrian/tyrian1.lvl ] || echo "WARNING: no Tyrian data: apt install tyrian-data (the licensed suite)"
}
ot_window() {	# waits up to 60 s for OpenTyrian's window to be mapped
	local i; for i in $(seq 600); do xwininfo -root -tree 2>/dev/null | grep -qi '"OpenTyrian' && return 0; sleep 0.1; done; return 1
}
ot_marker() {	# an MSC_SCAN + SYN_REPORT written into the keyboard: the recording's time zero (games ignore scan codes)
	python3 -c 'import os,struct; f=os.open("'"$OT_KBD"'",os.O_WRONLY)
os.write(f,struct.pack("@llHHi",0,0,4,4,0)+struct.pack("@llHHi",0,0,0,0,0)); os.close(f)'
}
during() {
	local i node
	case $OT_MODE in
	none) return 0 ;;
	record)
		ot_window || { echo "no OpenTyrian window"; return 1; }
		evemu-record "$OT_KBD" > "$BENCH_DIR/ot-session.evemu" & local r=$!
		sleep 0.3; ot_marker; echo "recording from $EPOCHREALTIME"
		wait $r ;;
	replay)
		[ -s "$OT_REC" ] || { echo "no recording $OT_REC: run the record variant first"; return 1; }
		# a virtual keyboard like the recorded one (root: /dev/uinput), gone by itself after the run
		sudo -n timeout $((DURATION + 30)) evemu-device "$OT_REC" > "$BENCH_DIR/.evdev" 2>&1 &
		for i in $(seq 50); do node=$(awk '{print $NF}' "$BENCH_DIR/.evdev" | grep -m1 /dev/input/); [ -n "$node" ] && break; sleep 0.1; done
		[ -n "$node" ] || { echo "no virtual keyboard"; cat "$BENCH_DIR/.evdev"; return 1; }
		ot_window || { echo "no OpenTyrian window"; return 1; }
		shot_mark; echo "replaying into $node from $EPOCHREALTIME"
		evemu-play "$node" < "$OT_REC"
		echo "replay done at $EPOCHREALTIME" ;;
	esac
}
SHOT_FROM=mark	# the screenshots count from the replay's start (the load's end in the variants without input)
variant_record() { OT_MODE=record; DURATION=900; NEEDS=(evemu-record); }	# ends when you quit (15 min cap; run.sh smoke needs BENCH_TIMEOUT=960); no shots (no mark)
variant_menu() { OT_MODE=none; SHOT_STATIC=1; }
variant_nosound() { CMD=(/usr/games/opentyrian --no-sound); }
# the cheaper (nearest-neighbour 2x in SDL) and the dearer scaler
variant_noscaler() { OT_SCALER=None; }
variant_scale3x() { OT_SCALER=Scale3x; }
