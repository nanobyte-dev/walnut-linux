# Colobot: Gold Edition 0.2.2-alpha (Debian ports ppc64: colobot, colobot-common, -sounds, -textures):
# smoke test, not benchmarked. `-runscene missions101` (category, chapter, level) starts the first mission straight away (the
# landing on Earth), with a save folder of the run's own (-savedir: the player's saves and settings
# untouched), fullscreen at 1920x1080 from the run's colobot.ini (written here, in that folder; variant
# fresh writes none: our colobot 0001's default). The mission opens with a cutscene and a briefing; the
# shots are after the load.
# What it exercises: the gl14 device (GL 1.4 fixed function) by default; variant gl21 the GLSL device
# (GLSL 1.20); the terrain, the robots, the particle effects, OpenAL. Risks: RAM (~300 MB of data),
# the GLSL device on nv30.
COMM=colobot
. "$BENCH_LIB/smoke.inc"
CB_SAVE=/var/tmp/ps3-bench/colobot-save
CB_DEV=gl14
CB_FRESH=0
setup() {
	rm -rf -- "${CB_SAVE:?}"; mkdir -p "$CB_SAVE"
	[ "$CB_FRESH" = 1 ] && return 0	# no colobot.ini: our colobot 0001 starts fullscreen at the desktop's mode
	printf '%s\n' "[Setup]" "Fullscreen=1" "Resolution=1920x1080" > "$CB_SAVE/colobot.ini"
}
# no -resolution: with it Colobot ignores the ini's Fullscreen (m_resolutionOverride) and opens a window
cb_cmd() { CMD=(/usr/games/colobot -savedir "$CB_SAVE" -graphics "$CB_DEV" -runscene missions101); }
cb_cmd
variant_gl21() { CB_DEV=gl21; cb_cmd; }
variant_fresh() { CB_FRESH=1; }
