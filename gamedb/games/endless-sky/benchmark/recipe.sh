# Endless Sky 0.11.2 (our ppc64 build, gamedb/games/endless-sky/packages/endless-sky/: endless-sky 0.11.2-1+ps3.N plus endless-sky-data; Debian's
# source with our patches: big-endian pixel and WAV fixes, ES_LOW_MEMORY defaults, the game's OpenGL 2 path
# with GLSL 1.20 shaders in shaders-gl2 and the sprites as 2D atlases for Mesa's DXT compression): smoke
# test, not benchmarked. Needs our endless-sky (the upstream package has no GL2 shaders and its pixel code is
# little-endian only). Base: fullscreen 1920x1080, a saved pilot (assets/ann-lee.txt: "Ann Lee",
# a Sparrow, landed on New Boston, Rutilicus; written to the private config directory
# $HOME/.config/endless-sky-ps3bench, --config) and the keyboard: during() waits for the window, gives the
# load ES_LOAD_WAIT seconds (the game parses all data and decodes its images before the menu: the estimate was 45
# to 80 s on the PPE, measured 150 s or more with ps3.589, so the default is 240), then presses e (Enter Ship), d (Depart)
# and Return three times 15 s apart (the help dialogs after Depart), and holds Up for 5 s: the Sparrow is in flight in Rutilicus (planet
# New Boston, asteroids, HUD, radar, starfield). If the load is slower than ES_LOAD_WAIT the keys land on the
# loading screen and the menu waits: the user may nudge (e, d, Return). Variant menu: no keys.
# Settings (preferences.txt in that directory, written by setup()): vsync off, "Reduce large graphics" all
# (every sprite halved in each direction: DXT5 atlases of 94 MB by the PC's count when the flight starts),
# "Defer loading images" is on by default in this build. The game rewrites the file and the save at exit; setup() writes both again.
# Cheap ship outlines are on (outlineLite.frag, 8 texture lookups instead of 144): the full filter ran the nv40
# fragment program out of temporaries on the PS3 (ps3.602: "out of temps!!" x36 in game.log, flight with no HUD).
# Variants: fullout (the full outline filter, to see that again), noblur ("Render motion blur" 0), nodefer (every image loaded at start), full (no
# halving: 4x the texture memory), threads4 (--tq-threads 4: the game's own default is the core count, at least 4).
# What it exercises: GL 2.1 only (GL_VERSION "2.1 Mesa", GLSL 1.20): 22 shaders on nv30 (outline.frag with 144
# lookups is the one that might not fit; the game falls back to outlineLite by itself and logs a warning in
# errors.txt), sprites in GL_TEXTURE_2D atlases up to 4096x4096, the fog map as GL_LUMINANCE8, OpenAL Soft.
# Risks: RAM. On the PC (amd64 build of the same patches, Xvfb, llvmpipe, GL 2.1, flight in Rutilicus, anon
# memory): 581 MB with 378 MB of RGBA8 atlases inside it (llvmpipe keeps the textures in RAM; on the PS3 they are
# 94 MB of DXT5 in VRAM), so about 200 MB without textures, 160 to 180 on the PS3 (llvmpipe's context and JIT
# gone, nv30's in). Without our cuts (Debian's source, 24 loader threads, no defer) 1228 MB. Watch MemAvailable,
# swap-ins and stalls; the first start is the slowest.
COMM=endless-sky
. "$BENCH_LIB/smoke.inc"
# The package (ps3.581 and later) defaults NV30_FORCE_DXT=all in main(); the first PS3 run (ps3.580) did not,
# stored the atlases as RGBA8 (about 380 MB, VRAM 256 MB: "out of VRAM", then the kernel OOM at 125 s of loading).
# The stats line goes to game.log: textures taken, KiB before and after.
ENV+=(NV30_FORCE_DXT_STATS=100)
ES_DIR=$HOME/.config/endless-sky-ps3bench
ES_LOAD_WAIT=${ES_LOAD_WAIT:-240}
ES_KEYS=1
ES_REDUCE=2
ES_CHEAP=1
ES_BLUR=1
ES_ARGS=()
DATA_DIRS=(/usr/share/games/endless-sky)
DURATION=${SMOKE_SECS:-360}
setup() {
	[ -e /usr/share/games/endless-sky/shaders-gl2/sprite.frag ] || echo "WARNING: no shaders-gl2: this is not our endless-sky (+ps3): apt install endless-sky"
	rm -rf "$ES_DIR"
	mkdir -p "$ES_DIR/saves"
	cp "$ASSETS/ann-lee.txt" "$ES_DIR/saves/Ann  Lee .txt"
	printf '%s\n' "$ES_DIR/saves/Ann  Lee .txt" > "$ES_DIR/recent.txt"
	printf '%s\n' "vsync 0" "fullscreen 1" "window size 1920 1080" "\"Reduce large graphics\" $ES_REDUCE" \
		"\"Show CPU / GPU load\" 0" ${ES_DEFER:+"\"Defer loading images\" $ES_DEFER"} \
		"\"Cheap ship outlines\" $ES_CHEAP" "\"Render motion blur\" $ES_BLUR" > "$ES_DIR/preferences.txt"
}
es_cmd() { CMD=(/usr/games/endless-sky --config "$ES_DIR" "${ES_ARGS[@]}"); }
es_cmd
during() {
	[ "$ES_KEYS" = 1 ] || return 0
	# Return three times: on the PS3 the help dialog after Depart comes later than 10 s (2026-10-20: a
	# "Lost 1" dialog stayed up and the Up key went nowhere)
	smoke_xkeys "Endless Sky" "$ES_LOAD_WAIT" e +6 d +10 Return +15 Return +15 Return +3
	XKEYS_HOLD=5 smoke_xkeys "Endless Sky" 0 Up
	shot_mark	# the screenshots count from the flight's start (SHOT_FROM=mark)
}
SHOT_FROM=mark; SHOT_POINTS="20 50"	# the flight starts about 300 s into a PS3 run (DURATION 360)
variant_menu() { ES_KEYS=0; SHOT_STATIC=1; }
variant_fullout() { ES_CHEAP=0; }   # the full outline filter: nv40 runs out of fragment temps ("out of temps!!" x36), the flight showed no HUD
variant_noblur() { ES_BLUR=0; }
variant_nodefer() { ES_DEFER=0; }
variant_full() { ES_REDUCE=0; }
variant_threads4() { ES_ARGS=(--tq-threads 4); es_cmd; }
