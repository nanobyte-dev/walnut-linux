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
# carries on and never prints its frames/fps line. So a fixed 90 s, fps from the Gallium HUD.
FPS_RE='[0-9]+ frames in [0-9.]+ seconds: ([0-9.]+) fps'
DURATION=90
HUD_SKIP=10
# make the recording (90 s), then copy /var/tmp/armagetronad-bench.aarec into assets/
variant_record() { CMD=(/usr/games/armagetronad --record /var/tmp/armagetronad-bench.aarec); DURATION=90; }
# the first-run screen and tutorial game, live (the old smoke test)
variant_live() { CMD=(/usr/games/armagetronad); DURATION=45; HUD_SKIP=15; }
