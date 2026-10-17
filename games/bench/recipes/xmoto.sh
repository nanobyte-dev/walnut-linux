# X-Moto 0.6.3: replays the "credits" replay (shipped inside xmoto.bin, which --replay doesn't read:
# assets/xmoto-credits.rpl is it, extracted, MD5 05a7200d...) with --timedemo --benchmark
# (draws every frame, prints the average and exits), fullscreen 1920x1080, no network. It sets no
# swap interval itself: vblank_mode=0.
# The replay runs in real time (physics steps by the wall clock; --timedemo only lifts the frame
# cap): ~144 s, so runs before 2026-10-17's steady state (cut at 90 s) never printed the average.
# Steady state: the HUD window, 60 s from the first frame to the replay's end (~80 s, less than the
# 120 s wanted; report.py's win says how much). The game's average (fps=) covers the whole replay.
# Repeating the replay needs a small patch to our xmoto (StateReplaying::update, at the benchmark
# end: XMOTO_BENCH_LOOPS=N, the lines printed with the pass, replaceState with a new
# StatePreplayingReplay instead of closing); then PASSES=1 and two passes (~300 s).
SUITE=1
COMM=xmoto
ENV=(vblank_mode=0)
BACKUP=("$HOME/.config/xmoto/config.dat")
setup() {
	mkdir -p "$HOME/.local/share/xmoto/Replays"
	cp "$ASSETS/xmoto-credits.rpl" "$HOME/.local/share/xmoto/Replays/credits.rpl"
}
CMD=(/usr/games/xmoto --nowww -fs -res 1920x1080 --replay credits --timedemo --benchmark)
FPS_RE='Average framerate: ([0-9.]+) fps'
DURATION=270	# a safety cap: the game exits at the replay's end (~150 s with the load)
SOUND_AT=20
# Mesa ps3.282: a submission every 100 draws (0026), the Armagetron fix, checked here
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
# Mesa ps3.299 switches, to bisect its cost here: 0027 (adaptive submission) off, 0030 (index upload) off
variant_autooff() { ENV+=(NV30_PS3_KICK_AUTO=0); }
variant_idxoff() { ENV+=(NV30_INDEX_UPLOAD=0); }
# our xmoto (xmoto/, +ps3.N): DrawLib's primitives batched and the GL state set only when
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
