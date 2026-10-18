# Armagetron Advanced 0.2.9.3: a local game of 4 AI players with the player spectating, which plays
# itself round after round (FIRST_USE 1 drops it straight into the game, SPECTATOR_MODE_1 and the
# SP_* settings make it AI only; setup writes them for every run). fps from the Gallium HUD.
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
AA_LIVE=1	# the raised match limits (not for playback and record: a recording replays only with the settings it was made with)
setup() {
	mkdir -p "$(dirname "$AA_CFG")"; touch "$AA_CFG"
	sed -i -E '/^ *(FIRST_USE|SPECTATOR_MODE_1|SP_NUM_AIS|SP_AUTO_AIS|SP_AUTO_IQ|SP_AI_IQ|FULLSCREEN|MAX_FPS|SP_LIMIT_ROUNDS|SP_LIMIT_TIME|SP_LIMIT_SCORE) /d' "$AA_CFG"
	printf '%s\n' "FIRST_USE 1" "SPECTATOR_MODE_1 1" "SP_NUM_AIS 4" "SP_AUTO_AIS 0" "SP_AUTO_IQ 0" \
		"SP_AI_IQ 100" "FULLSCREEN 1" "MAX_FPS 0" >> "$AA_CFG"
	[ "$AA_LIVE" = 1 ] && printf '%s\n' "SP_LIMIT_ROUNDS 100000" "SP_LIMIT_TIME 100000" >> "$AA_CFG"
	return 0
}
CMD=(/usr/games/armagetronad)
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
# Mesa ps3.282 switches (docs/GAME-PLAN.md): relocations back on (0019 off), or 0019 kept with a
# submission every N draws (0026)
variant_relocs() { ENV+=(NV30_PS3_RELOCS=1); }
variant_kick150() { ENV+=(NV30_PS3_KICK_DRAWS=150); }
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
variant_kick50() { ENV+=(NV30_PS3_KICK_DRAWS=50); }
