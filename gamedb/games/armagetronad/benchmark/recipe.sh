# Armagetron Advanced 0.2.9.3: a local game of 4 AI players with the player spectating, which plays
# itself round after round (SPECTATOR_MODE_1 and the SP_* settings make it AI only; setup writes
# them for every run). fps from the Gallium HUD.
# Menus (0.2.9: FIRST_USE is true on a first start, so FIRST_USE 1 in the config means the first
# start's language menu, player setup and welcome screen, which wait for a key: the run of
# 2026-10-18 sat at the language menu for its whole window). The live variants write FIRST_USE 0
# and the language (LANGUAGE_FIRST/_SECOND), and `during` types Return, Down, Return into a
# virtual keyboard (as etr's recipe does): main menu "Play Game", then "Local Game", which starts the
# game at once. Playback and record keep FIRST_USE 1: their recording holds the first start's keys.
# Steady state: the HUD window (WINDOW_STOP=1: 60 s from the first frame, then 120 s, then the game
# is stopped). The rounds are alike but not identical from run to run (the AIs decide live).
# The match limits are raised (10 rounds or 30 minutes by default: the champion screen and a new
# match every few minutes otherwise).
# Until 2026-10-18 base played back assets/armagetronad-bench.aarec (--playback --benchmark), a 90 s
# recording cut by the record run's stop, not ended with a quit: after its end the game draws
# nothing more (a black screen with the cursor, no frames, no CPU: the HUD stopped 66-77 s after
# its first frame in every run), so the 60 + 120 s window held ~6 s of frames and perf had samples
# only at its start (the suite run 20261018T004040Z). The recording is variant playback now, with a
# window that fits in it; a longer one (variant record, ended with Quit from the menu before
# DURATION) would make a playback window of the full length.
SUITE=1
COMM=armagetronad.re	# armagetronad.real, cut to 15 characters
AA_CFG=$HOME/.armagetronad/var/user.cfg
BACKUP=("$AA_CFG")
AA_REC=$ASSETS/armagetronad-bench.aarec
AA_KBD_WAIT=25	# seconds from the window's mapping to the keys (load, splash screen of up to 6 s, then the menu waits)
AA_LIVE=1	# the raised match limits (not for playback and record: a recording replays only with the settings it was made with)
setup() {
	mkdir -p "$(dirname "$AA_CFG")"; touch "$AA_CFG"
	sed -i -E '/^ *(FIRST_USE|LANGUAGE_FIRST|LANGUAGE_SECOND|SPECTATOR_MODE_1|SP_NUM_AIS|SP_AUTO_AIS|SP_AUTO_IQ|SP_AI_IQ|FULLSCREEN|MAX_FPS|SP_LIMIT_ROUNDS|SP_LIMIT_TIME|SP_LIMIT_SCORE) /d' "$AA_CFG"
	printf '%s\n' "FIRST_USE $((1 - AA_LIVE))" "LANGUAGE_FIRST American English" "LANGUAGE_SECOND American English" "SPECTATOR_MODE_1 1" "SP_NUM_AIS 4" "SP_AUTO_AIS 0" "SP_AUTO_IQ 0" \
		"SP_AI_IQ 100" "FULLSCREEN 1" "MAX_FPS 0" >> "$AA_CFG"
	[ "$AA_LIVE" = 1 ] && printf '%s\n' "SP_LIMIT_ROUNDS 100000" "SP_LIMIT_TIME 100000" >> "$AA_CFG"
	return 0
}
CMD=(/usr/games/armagetronad)
# virtual keyboard (the description from etr's recording) and the keys to the local game
during() {
	[ "$AA_LIVE" = 1 ] || return 0
	local i node kbd=${BENCH_DIR:-$D}/.aa-kbd
	cp "$BENCH_LIB/keyboard.evemu" "$kbd.desc"
	sudo -n timeout $((DURATION + 30)) evemu-device "$kbd.desc" > "$kbd.log" 2>&1 &
	for i in $(seq 50); do node=$(awk '{print $NF}' "$kbd.log" | grep -m1 /dev/input/); [ -n "$node" ] && break; sleep 0.1; done
	[ -n "$node" ] || { echo "no virtual keyboard"; cat "$kbd.log"; return 1; }
	for i in $(seq 600); do
		xwininfo -root -tree 2>/dev/null | grep -qi '"Armagetron Advanced"' && break; sleep 0.1
	done
	sleep "$AA_KBD_WAIT"
	local k
	for k in 001c 006c 001c; do	# Return (Play Game), Down (Local Game), Return
		printf 'E: 0.000000 0001 %s 0001\nE: 0.000000 0000 0000 0000\nE: 0.050000 0001 %s 0000\nE: 0.050000 0000 0000 0000\n' "$k" "$k" > "$kbd.keys"
		evemu-play "$node" < "$kbd.keys"; echo "key $k at $EPOCHREALTIME"; sleep 2
	done
	rm -f "$kbd.desc" "$kbd.keys"
}
DURATION=270
WINDOW_STOP=1
HUD_SKIP=10
# the recording played back (--benchmark: renders every recorded frame; its "<N> frames in <T>
# seconds: <X> fps." line never comes, the recording has no end). Frames for 54-77 s from the first
# one, so a short window: 10 s warm-up, 40 s measured. Identical every run
FPS_RE='[0-9]+ frames in [0-9.]+ seconds: ([0-9.]+) fps'
variant_playback() { AA_LIVE=0; CMD=(/usr/games/armagetronad --playback "$AA_REC" --benchmark); WARMUP=10; MEASURE=40; DURATION=120; }
# make a recording (quit from the menu before DURATION, so its playback ends there), then copy
# /var/tmp/armagetronad-bench.aarec into assets/ (the asset of 2026-10-17 is a 90 s one, cut)
variant_record() { AA_LIVE=0; CMD=(/usr/games/armagetronad --record /var/tmp/armagetronad-bench.aarec); DURATION=240; WINDOW_STOP=0; }
# the live game for 45 s, no window (the old smoke test)
variant_live() { DURATION=45; HUD_SKIP=15; WINDOW_STOP=0; }
# Mesa 0020 off (texture caches invalidated on every draw, as before ps3.262): bisects its cost
variant_texinv() { ENV+=(NV30_TEX_INVALIDATE_ALWAYS=1); }
# Mesa ps3.282 switches (docs/archive/GAME-PLAN.md): relocations back on (0019 off), or 0019 kept with a
# submission every N draws (0026)
variant_relocs() { ENV+=(NV30_PS3_RELOCS=1); }
variant_kick150() { ENV+=(NV30_PS3_KICK_DRAWS=150); }
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
variant_kick50() { ENV+=(NV30_PS3_KICK_DRAWS=50); }

# vsync off, as in every other recipe (2026-10-18): Mesa ignores the swap interval the game asks for,
# so the numbers are uncapped and comparable (ETR's 59 fps looked like a 60 Hz cap)
ENV+=(vblank_mode=0)
