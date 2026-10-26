# ClassiCube (ours): a timedemo through a plugin. assets/classicube-bench.so (source in
# classicube-bench/, `make classicube-bench`) flies the camera along assets/classicube-bench.path
# over the saved map assets/classicube-bench.cw: 3000 frames, the same ones every run (frame-based,
# like a Quake timedemo: a faster run only takes less time), after a warm-up that builds every chunk
# of the map, so the run builds none. Then it prints the frame rate and closes the game itself.
# load = seconds to the first measured frame (map load and warm-up). The plugin does nothing without
# CC_BENCH; the run removes it again (BACKUP: it wasn't there before). Options and plugins live next
# to the game in ~/.local/share/classicube.
# Steady state: the plugin flies the path up to 6 times (CC_BENCH_PASSES, assets/classicube-bench.so
# of 2026-10-17: a report per pass, after "ccbench: pass K of N"); PASSES=1: client.sh counts the
# passes that start 54 s or more after the first measured frame (a pass is ~60 s at 50 fps, so the
# first is the warm-up) and stops the game once they cover ~120 s (2 passes). fps=, maxms= are the
# first pass's (the run as before); steady is the median of the counted passes. An older plugin
# ignores CC_BENCH_PASSES: one pass, as before.
SUITE=1
COMM=ClassiCube
CC_DIR=${XDG_DATA_HOME:-$HOME/.local/share}/classicube
# the game data GAMEDATA=ssd takes from the SSD: the per-user copies of the textures and sounds the game
# reads (and rewrites: the binds are read-write); the map is copied in by setup() every run
DATA_DIRS=("$CC_DIR/texpacks" "$CC_DIR/audio")
BACKUP=("$CC_DIR/options.txt" "$CC_DIR/plugins/ccbench.so")
CC_LIMIT=LimitNone
CC_PLUGIN=1
setup() {
	mkdir -p "$CC_DIR/maps" "$CC_DIR/plugins"
	cp "$ASSETS/classicube-bench.cw" "$CC_DIR/maps/bench.cw"
	rm -f "$CC_DIR/plugins/ccbench.so"
	[ "$CC_PLUGIN" = 1 ] && cp "$ASSETS/classicube-bench.so" "$CC_DIR/plugins/ccbench.so"
	[ -e "$CC_DIR/options.txt" ] || cp /usr/lib/classicube/options.txt "$CC_DIR/"
	sed -i '/^\(fpslimit\|window-width\|window-height\)=/d' "$CC_DIR/options.txt"
	printf 'fpslimit=%s\nwindow-width=1920\nwindow-height=1080\n' "$CC_LIMIT" >> "$CC_DIR/options.txt"
}
CMD=(/usr/games/classicube "$CC_DIR/maps/bench.cw")
ENV=(CC_BENCH="$ASSETS/classicube-bench.path" CC_BENCH_PASSES=6)
PASSES=1
# ccbench: 3000 frames 58.21 seconds 51.54 fps, frame ms min/avg/p99/max 12.1/19.4/31.0/45.6
FPS_RE='^ccbench: [0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='^ccbench: [0-9]+ frames .* min/avg/p99/max [0-9.]+/[0-9.]+/[0-9.]+/([0-9.]+)'
LOAD_MARK='^ccbench: start after'
# client.sh stops the game after the passes it needs; this only stops a run that hangs or crawls
# (a pass is 3000 frames: under ~25 fps the second pass doesn't end within the cap)
DURATION=330	# a safety cap: ~15 s of load, passes of ~60 s, stopped after the third (or the fourth)
parse() {	# frame time spread and chunk builds during the run (0: the frames were the same)
	local l
	[ -e "$D/game.log" ] || return 0
	while IFS= read -r l; do
		case $l in
		"ccbench: chunks built:"*) l=${l%% during*}; echo "cc_chunks_run=${l##* }" ;;
		"ccbench: "*" min/avg/p99/max "*) echo "cc_frame_ms=${l##* }" ;;
		esac
	done < "$D/game.log"
}
# vsync on, as players run it
variant_vsync() { CC_LIMIT=LimitVSync; }
# the path with a screenshot of every keyframe into the run directory (frame-NNNNN.ppm, 1920x1080,
# 6 MB each; their time is left out of the numbers): check the PS3's frames against another build
variant_shots() { ENV+=(CC_BENCH_SHOTS="$D" CC_BENCH_PASSES=1); }
# the old run: standing still at the map's spawn, no plugin, fps from the Gallium HUD only (the map
# loads in a few seconds and prints nothing: the HUD's first 10 s count as loading); 60 s before the
# steady state, now the HUD window (60 s from the first frame, 120 s, then stopped)
variant_still() {
	CC_PLUGIN=0; ENV=(); FPS_RE=; MAXMS_RE=; LOAD_MARK=; DURATION=240; HUD_SKIP=10; PASSES=0; WINDOW_STOP=1
	parse() { :; }
}
# OpenAL Soft measurements (docs/SPU-AUDIO.md, M0): the shared variants of openal.inc, and no sound
# at all (volumes 0: ClassiCube then never opens OpenAL)
. "$here/recipes/openal.inc"
variant_allog() { oal_variant allog; }
variant_b16() { oal_variant b16; }
variant_f44() { oal_variant f44; }
variant_b16f44() { oal_variant b16f44; }
variant_p1024() { oal_variant p1024; }
variant_alnull() { oal_variant alnull; }
variant_f32() { oal_variant f32; }
variant_lin() { oal_variant lin; }
variant_nosound() { oal_after setup cc_nosound; }
cc_nosound() {
	sed -i '/^\(soundsvolume\|musicvolume\)=/d' "$CC_DIR/options.txt"
	printf 'soundsvolume=0\nmusicvolume=0\n' >> "$CC_DIR/options.txt"
}
# Mesa ps3.282: a submission every 100 draws (0026), the Armagetron fix, checked here
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
# Mesa ps3.299 switches, to bisect its cost here: 0027 (adaptive submission) off, 0030 (index upload) off
variant_autooff() { ENV+=(NV30_PS3_KICK_AUTO=0); }
variant_idxoff() { ENV+=(NV30_INDEX_UPLOAD=0); }
# Our classicube build's GL submission switches (packages/patched/games/classicube/patches, src/Graphics_GL1.c): CC_GL=0 is
# the game as upstream draws it, the base run has them all on. CC_GL_STATS=1 prints draws, buffer
# binds and pointer sets per frame once a second into game.log (not for pictures: it adds clock reads).
variant_gloff() { ENV+=(CC_GL=0); }
variant_glstats() { ENV+=(CC_GL_STATS=1); }
variant_noptr() { ENV+=(CC_GL_PTR=0); }	# vertex pointers set for every draw again
variant_nostate() { ENV+=(CC_GL_STATE=0); }	# buffer binds and face culling set every time again
variant_chunks0() { ENV+=(CC_GL_CHUNKS=0); }	# a draw per face direction and pair, as upstream
variant_chunks1() { ENV+=(CC_GL_CHUNKS=1); }	# the same faces, neighbouring ranges in one draw (~770 draws a frame)
variant_chunks2() { ENV+=(CC_GL_CHUNKS=2); }	# all faces of a chunk part in one draw (the default: ~290 draws, 75% more indices)
# the game's own render distance (option viewdist, blocks): the fewer chunks in view, the fewer draws
cc_viewdist() { sed -i '/^viewdist=/d' "$CC_DIR/options.txt"; printf 'viewdist=%s\n' "$1" >> "$CC_DIR/options.txt"; }
cc_view64() { cc_viewdist 64; }
cc_view32() { cc_viewdist 32; }
variant_view64() { oal_after setup cc_view64; }
variant_view32() { oal_after setup cc_view32; }
