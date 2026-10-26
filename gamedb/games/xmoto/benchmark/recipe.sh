# X-Moto 0.6.3: replays the "credits" replay (shipped inside xmoto.bin, which --replay doesn't read:
# assets/xmoto-credits.rpl is it, extracted, MD5 05a7200d...) with --timedemo --benchmark
# (draws every frame, prints the average and exits), fullscreen 1920x1080, no network. It sets no
# swap interval itself: vblank_mode=0.
# The replay runs in real time (physics steps by the wall clock; --timedemo only lifts the frame
# cap): ~144 s, so runs before 2026-10-17's steady state (cut at 90 s) never printed the average.
# Steady state: passes. Our patch 0003 (StateReplaying::update, at the benchmark end) with
# XMOTO_BENCH_LOOPS=N plays the replay N times (a new StatePreplayingReplay instead of closing),
# printing the lines each pass. Two passes (~300 s with the reloads): the first is the warm-up, the
# second (~144 s, starts after 0.9 x 60 s) is counted and covers the 120 s. fps= is the first pass's.
SUITE=1
COMM=xmoto
ENV=(vblank_mode=0 XMOTO_BENCH_LOOPS=2)
PASSES=1
BACKUP=("$HOME/.config/xmoto/config.dat")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against the recipe's config.dat and Replays paths
SESSION_HOME=(.config/xmoto .local/share/xmoto)
setup() {
	mkdir -p "$HOME/.local/share/xmoto/Replays"
	cp "$ASSETS/xmoto-credits.rpl" "$HOME/.local/share/xmoto/Replays/credits.rpl"
}
CMD=(/usr/games/xmoto --nowww -fs -res 1920x1080 --replay credits --timedemo --benchmark)
FPS_RE='Average framerate: ([0-9.]+) fps'
# A played session (session.sh): the game without the credits replay and the benchmark loop
session() {
	local x e=(); for x in "${ENV[@]}"; do [[ $x == XMOTO_BENCH_LOOPS=* ]] || e+=("$x"); done; ENV=("${e[@]}")
	CMD=(/usr/games/xmoto --nowww -fs -res 1920x1080)
}
DURATION=450	# a safety cap: the load, two passes of ~150 s with the reload, the quit
SOUND_AT=20
# Mesa ps3.282: a submission every 100 draws (0026), the Armagetron fix, checked here
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
# Mesa ps3.299 switches, to bisect its cost here: 0027 (adaptive submission) off, 0030 (index upload) off
variant_autooff() { ENV+=(NV30_PS3_KICK_AUTO=0); }
variant_idxoff() { ENV+=(NV30_INDEX_UPLOAD=0); }
# our xmoto (gamedb/games/xmoto/packages/xmoto/, +ps3.N): DrawLib's primitives batched and the GL state set only when
# drawing (XMOTO_GL_BATCH), the level's blocks and edges from one buffer (XMOTO_GL_BLOCKS);
# base has both. Draws, vertices and syncs per frame every 5 s on stderr (game.log):
variant_glstats() { ENV+=(XMOTO_GL_STATS=1); }
# Debian's drawing: glBegin/glEnd, a draw per block polygon (~1960 draws a frame)
variant_nobatch() { ENV+=(XMOTO_GL_BATCH=0); }
# batching only, the blocks polygon by polygon (~1000 draws a frame, ~40 with base)
variant_noblocks() { ENV+=(XMOTO_GL_BLOCKS=0 XMOTO_GL_STATS=1); }
# 0031 (fragment programs from GART after a constant change) off; 0028 (index buffer fenced) off
variant_fpvram() { ENV+=(NV30_FP_CONST_VRAM=1); }
variant_idxlate() { ENV+=(NV30_IDXBUF_LATE_REF=1); }
# our xmoto 0004 (docs/HOTPATHS.md, target 3): the batches written straight into a mapped stream
# buffer instead of client memory that Mesa copies at every draw (u_upload_data's memcpy, 5.5% of
# the CPU, the minimap's circles most of it); the same vertices and draws. streamstats counts
# its buffer renewals too
variant_stream() { ENV+=(XMOTO_GL_STREAM=1); }
variant_streamstats() { ENV+=(XMOTO_GL_STREAM=1 XMOTO_GL_STATS=1); }
