# Neverball (1.6 snapshot of 2018): its shipped replay gui/demo1.nbr, real-time paced. Our package
# (neverball/patches) prints one line with NEVERBALL_BENCH=1 when the replay ends, from the first
# replay frame (frames, seconds, fps, longest frame) and quits, so the run is exactly the replay;
# DURATION is a safety cap. Config ~/.neverball/
# neverballrc: fullscreen 1920x1080, vsync off, the fps counter on. It generates mipmaps on the GPU
# (GL_GENERATE_MIPMAP) and uses GL_LUMINANCE_ALPHA textures: both broken on nv30 until the Mesa
# fixes (docs/GAME-PLAN.md 1.0); variant nomip turns mipmaps off.
SUITE=1
COMM=neverball
NB_RC=$HOME/.neverball/neverballrc
BACKUP=("$NB_RC")
NB_MIP=1
ENV=(NEVERBALL_BENCH=1)
FPS_RE='^neverball: [0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='^neverball: .* frame ms max ([0-9.]+)'
setup() {
	mkdir -p "$HOME/.neverball"; touch "$NB_RC"
	sed -i -E '/^(fullscreen|width|height|vsync|fps|mipmap|multisample) /d' "$NB_RC"
	printf '%s\n' "fullscreen 1" "width 1920" "height 1080" "vsync 0" "fps 1" "mipmap $NB_MIP" \
		"multisample 0" >> "$NB_RC"
}
CMD=(/usr/games/neverball --replay /usr/share/games/neverball/gui/demo1.nbr)
DURATION=90
variant_nomip() { NB_MIP=0; }
