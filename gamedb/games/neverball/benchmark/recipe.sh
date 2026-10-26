# Neverball (1.6 snapshot of 2018): its shipped replay gui/demo1.nbr, real-time paced. Our package
# (gamedb/games/neverball/packages/neverball/patches) prints one line with NEVERBALL_BENCH=1 when the replay ends, from the first
# replay frame (frames, seconds, fps, longest frame) and quits, so the run is exactly the replay;
# DURATION is a safety cap. Config ~/.neverball/
# neverballrc: fullscreen 1920x1080, vsync off, the fps counter on. It generates mipmaps on the GPU
# (GL_GENERATE_MIPMAP) and uses GL_LUMINANCE_ALPHA textures: both broken on nv30 until the Mesa
# fixes (docs/games/light-games.md); variant nomip turns mipmaps off.
# Steady state: passes. The replay is ~20 s; our patch 0002 (st_demo.c bench_end) with
# NEVERBALL_BENCH_LOOPS=N replays it N times (demo_replay_stop, progress_replay on a copy of
# curr_demo(), goto_state(&st_demo_play)), one line per pass, and quits after the Nth. 12 passes
# cover 60 s of warm-up + 120 s; the client stops the run once enough is measured. fps= is the
# first pass's.
SUITE=1
COMM=neverball
NB_RC=$HOME/.neverball/neverballrc
BACKUP=("$NB_RC")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against the recipe's neverballrc path
SESSION_HOME=(.neverball)
NB_MIP=1
ENV=(NEVERBALL_BENCH=1 NEVERBALL_BENCH_LOOPS=20)	# passes take 12-16 s on the PS3: 12 sometimes ended before the 60 + 120 s window (2026-10-18)
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
DURATION=400	# a safety cap: load, 20 passes of 12-16 s, margin
variant_nomip() { NB_MIP=0; }
# our neverball (gamedb/games/neverball/packages/neverball/patches/0003): fewer GL calls and draws a frame, each change behind an
# environment switch, base has them all on. gui/demo1.nbr on llvmpipe at GL 2.1: 2154 -> 844 calls,
# 137 -> 74 draws a frame (levels at their intro view: greed 6289 -> 1915, mover 8205 -> 2463 calls).
# gloff is NEVERBALL_GL=0 (the game as Debian draws it), a no* variant is base with one change off.
variant_gloff() { ENV+=(NEVERBALL_GL=0); }
variant_nomesh() { ENV+=(NEVERBALL_GL_MESH=0); }	# a buffer per mesh: buffers and pointers set for every mesh
variant_noitems() { ENV+=(NEVERBALL_GL_ITEMS=0); }	# items drawn one by one with their own state
variant_noback() { ENV+=(NEVERBALL_GL_BACK=0); }	# background billboards: matrix calls and a draw each (76 draws a frame on demo1, 13 in base)
variant_nostate() { ENV+=(NEVERBALL_GL_STATE=0); }	# depth toggles around empty passes, both texture units each time
