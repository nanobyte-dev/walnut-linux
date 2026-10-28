# Scorched 3D 44 (Debian ports ppc64: scorched3d, scorched3d-data; with wxWidgets for its launcher): smoke
# test, not benchmarked. The menu entry starts the wxWidgets launcher (scorched3d); the game itself is the
# client scorched3dc, which `-startcustom` starts straight into a local custom game from
# ~/.scorched3d/singlecustom.xml (the launcher's "Custom" writes it; setup() writes one, below). The display
# settings go in ~/.scorched3d/display.xml (written here, restored after the run): fullscreen
# 1920x1080. Artillery turns: the arrow keys aim, +/- the power, Space fires.
# What it exercises: GL 1.x/2.x with optional GLSL (land, water, sky, shadows), GLEW, OpenAL + ALUT,
# the land generation on the PPE. Risks: the water and land shaders (nv30's GLSL), RAM, its own network
# code on big-endian (a local game still runs a server in-process).
COMM=scorched3dc
. "$BENCH_LIB/smoke.inc"
S3_DIR=$HOME/.scorched3d
BACKUP=("$S3_DIR")
setup() {
	mkdir -p "$S3_DIR"
	# the game's own "fastest" display preset (S3_DISPLAY) plus fullscreen 1920x1080; the first run
	# (2026-10-28) on the defaults was OOM-killed 34 s in, the swap filling while the land was made
	{ if [ "$S3_DISPLAY" = none ]; then echo "<options source='Scorched3D'>"
	  else sed '/<\/options>/d' "/usr/share/games/scorched3d/data/${S3_DISPLAY}display.xml"; fi
	  printf '%s\n' "<option><name>FullScreen</name><value>on</value></option>" \
		"<option><name>ScreenWidth</name><value>1920</value></option>" \
		"<option><name>ScreenHeight</name><value>1080</value></option>" "</options>"; } > "$S3_DIR/display.xml"
	# the game: the tutorial's settings file with four computer tanks (Random: a random AI each) and
	# nobody else, so the match plays itself; the client spectates
	python3 -I - "$S3_GAME" /usr/share/games/scorched3d/data/singletutorial.xml "$S3_DIR/singlecustom.xml" <<'PY'
import re, sys
human, src, dst = sys.argv[1] == "human", sys.argv[2], sys.argv[3]
s = open(src).read()
s = re.sub(r"\s*<option>\s*<name>Tutorial</name>.*?</option>", "", s, flags=re.S)
def player(m):
    n = int(m.group(1))
    v = "Human" if (human and n == 1) else ("Random" if n <= 4 else "Target")
    return m.group(0).replace("<value>%s</value>" % m.group(2), "<value>%s</value>" % v)
s = re.sub(r"<name>PlayerType(\d)</name>\s*<value>(\w+)</value>", player, s)
s = re.sub(r"(<name>NumberOf(?:Min)?Players</name>\s*<value>)8(</value>)", r"\g<1>4\2", s)
open(dst, "w").write(s)
PY
}
S3_DISPLAY=fastest	# fastest, safe, medium, ultra (data/<x>display.xml)
S3_GAME=ai	# ai: four computer tanks; human: player 1 is the player
CMD=(/usr/games/scorched3dc -startcustom)
variant_human() { S3_GAME=human; }
variant_medium() { S3_DISPLAY=medium; }
variant_default() { S3_DISPLAY=none; }	# the game's own display defaults, fullscreen 1920x1080 only
during() { sleep 30; xwininfo -root -tree | grep -m3 -i 'scorch' | sed 's/^ */window: /'; }
