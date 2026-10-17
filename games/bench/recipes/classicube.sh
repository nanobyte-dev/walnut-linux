# ClassiCube (ours): a timedemo through a plugin. assets/classicube-bench.so (source in
# classicube-bench/, `make classicube-bench`) flies the camera along assets/classicube-bench.path
# over the saved map assets/classicube-bench.cw: 3000 frames, the same ones every run (frame-based,
# like a Quake timedemo: a faster run only takes less time), after a warm-up that builds every chunk
# of the map, so the run builds none. Then it prints the frame rate and closes the game itself.
# load = seconds to the first measured frame (map load and warm-up). The plugin does nothing without
# CC_BENCH; the run removes it again (BACKUP: it wasn't there before). Options and plugins live next
# to the game in ~/.local/share/classicube.
SUITE=1
COMM=ClassiCube
CC_DIR=${XDG_DATA_HOME:-$HOME/.local/share}/classicube
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
ENV=(CC_BENCH="$ASSETS/classicube-bench.path")
# ccbench: 3000 frames 58.21 seconds 51.54 fps, frame ms min/avg/p99/max 12.1/19.4/31.0/45.6
FPS_RE='^ccbench: [0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='^ccbench: [0-9]+ frames .* min/avg/p99/max [0-9.]+/[0-9.]+/[0-9.]+/([0-9.]+)'
LOAD_MARK='^ccbench: start after'
# the game quits by itself; this only stops a run that hangs or crawls (under ~19 fps it can't
# finish: run.sh smoke stops everything at 180 s)
DURATION=120	# a safety cap: the plugin quits by itself (~70 s of demo after ~15 s of load)
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
variant_shots() { ENV+=(CC_BENCH_SHOTS="$D"); }
# the old run: standing still at the map's spawn for 60 s, no plugin, fps from the Gallium HUD only
# (the map loads in a few seconds and prints nothing: the HUD's first 10 s count as loading)
variant_still() {
	CC_PLUGIN=0; ENV=(); FPS_RE=; MAXMS_RE=; LOAD_MARK=; DURATION=60; HUD_SKIP=10
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
variant_nosound() { oal_after setup cc_nosound; }
cc_nosound() {
	sed -i '/^\(soundsvolume\|musicvolume\)=/d' "$CC_DIR/options.txt"
	printf 'soundsvolume=0\nmusicvolume=0\n' >> "$CC_DIR/options.txt"
}
