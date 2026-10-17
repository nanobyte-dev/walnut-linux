# Armagetron Advanced 0.2.9.3: a recorded round of AI players with the player spectating, played
# back with --playback --benchmark (renders every recorded frame, prints "<N> frames in <T> seconds:
# <X> fps."). The recording (assets/armagetronad-bench.aarec) is made on the PS3 with variant
# record: FIRST_USE 1 drops it straight into a local game, spectator mode and 4 AI players make it
# play itself. Playback needs the same settings, so setup writes them for every run.
SUITE=1
COMM=armagetronad.re	# armagetronad.real, cut to 15 characters
AA_CFG=$HOME/.armagetronad/var/user.cfg
BACKUP=("$AA_CFG")
AA_REC=$ASSETS/armagetronad-bench.aarec
setup() {
	mkdir -p "$(dirname "$AA_CFG")"; touch "$AA_CFG"
	sed -i -E '/^ *(FIRST_USE|SPECTATOR_MODE_1|SP_NUM_AIS|SP_AUTO_AIS|SP_AUTO_IQ|SP_AI_IQ|FULLSCREEN|MAX_FPS) /d' "$AA_CFG"
	printf '%s\n' "FIRST_USE 1" "SPECTATOR_MODE_1 1" "SP_NUM_AIS 4" "SP_AUTO_AIS 0" "SP_AUTO_IQ 0" \
		"SP_AI_IQ 100" "FULLSCREEN 1" "MAX_FPS 0" >> "$AA_CFG"
}
CMD=(/usr/games/armagetronad --playback "$AA_REC" --benchmark)
# The recording ends where the record run was stopped (90 s), not with a quit: playback then
# carries on and never prints its frames/fps line. fps from the Gallium HUD.
# Steady state: the HUD window (WINDOW_STOP=1: 60 s from the first frame, then 120 s, then the game
# is stopped). The recording covers the first 90 s only; after it the game goes on live, the same
# 4 AI players with the player spectating, so the window's last ~2 minutes are AI rounds that are
# alike but not identical from run to run. A recording of ~4 minutes (variant record, 240 s) that
# the recorder ends with Quit from the menu would make the whole window the same every run.
FPS_RE='[0-9]+ frames in [0-9.]+ seconds: ([0-9.]+) fps'
DURATION=270
WINDOW_STOP=1
HUD_SKIP=10
# make the recording (240 s: quit from the menu before the end, so playback ends there), then copy
# /var/tmp/armagetronad-bench.aarec into assets/ (the asset of 2026-10-17 is a 90 s one, cut)
variant_record() { CMD=(/usr/games/armagetronad --record /var/tmp/armagetronad-bench.aarec); DURATION=240; WINDOW_STOP=0; }
# the first-run screen and tutorial game, live (the old smoke test)
variant_live() { CMD=(/usr/games/armagetronad); DURATION=45; HUD_SKIP=15; WINDOW_STOP=0; }
# Mesa 0020 off (texture caches invalidated on every draw, as before ps3.262): bisects its cost
variant_texinv() { ENV+=(NV30_TEX_INVALIDATE_ALWAYS=1); }
# Mesa ps3.282 switches (docs/GAME-PLAN.md): relocations back on (0019 off), or 0019 kept with a
# submission every N draws (0026)
variant_relocs() { ENV+=(NV30_PS3_RELOCS=1); }
variant_kick150() { ENV+=(NV30_PS3_KICK_DRAWS=150); }
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
variant_kick50() { ENV+=(NV30_PS3_KICK_DRAWS=50); }
