# Neverball (1.6 snapshot of 2018): its shipped replay gui/demo1.nbr, real-time paced. Our package
# (neverball/patches) prints one line with NEVERBALL_BENCH=1 when the replay ends, from the first
# replay frame (frames, seconds, fps, longest frame) and quits, so the run is exactly the replay;
# DURATION is a safety cap. Config ~/.neverball/
# neverballrc: fullscreen 1920x1080, vsync off, the fps counter on. It generates mipmaps on the GPU
# (GL_GENERATE_MIPMAP) and uses GL_LUMINANCE_ALPHA textures: both broken on nv30 until the Mesa
# fixes (docs/GAME-PLAN.md 1.0); variant nomip turns mipmaps off.
# Steady state: passes. The replay is ~20 s; our patch 0002 (st_demo.c bench_end) with
# NEVERBALL_BENCH_LOOPS=N replays it N times (demo_replay_stop, progress_replay on a copy of
# curr_demo(), goto_state(&st_demo_play)), one line per pass, and quits after the Nth. 12 passes
# cover 60 s of warm-up + 120 s; the client stops the run once enough is measured. fps= is the
# first pass's.
SUITE=1
COMM=neverball
NB_RC=$HOME/.neverball/neverballrc
BACKUP=("$NB_RC")
NB_MIP=1
ENV=(NEVERBALL_BENCH=1 NEVERBALL_BENCH_LOOPS=12)
PASSES=1
FPS_RE='^neverball: [0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='^neverball: .* frame ms max ([0-9.]+)'
setup() {
	mkdir -p "$HOME/.neverball"; touch "$NB_RC"
	sed -i -E '/^(fullscreen|width|height|vsync|fps|mipmap|multisample) /d' "$NB_RC"
	printf '%s\n' "fullscreen 1" "width 1920" "height 1080" "vsync 0" "fps 1" "mipmap $NB_MIP" \
		"multisample 0" >> "$NB_RC"
}
CMD=(/usr/games/neverball --replay /usr/share/games/neverball/gui/demo1.nbr)
DURATION=270	# a safety cap: load, 12 passes of ~20 s, margin
variant_nomip() { NB_MIP=0; }
