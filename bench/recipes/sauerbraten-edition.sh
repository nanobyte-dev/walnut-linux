# Sauerbraten, the real game (Cube 2: Sauerbraten, 2020 Edition): our `sauerbraten` launcher (fullscreen at the
# desktop's resolution, settings in ~/.sauerbraten) on our cube2 engine, with sauerbraten-data (the release's
# maps, textures, models, sounds; the licensed suite). The engine and its patches are the ones of the BananaBread
# recipe (sauerbraten.sh, Debian's cube2 + packages/patched/games/cube2/patches): CPU skinning (gpuskel 0), textures compressed at
# upload (texcompress), the cheaper GL call patterns. Only the data differs. Settings go into
# ~/.sauerbraten/autoexec.cfg (run after config.cfg, so the player's saved fullscreen or vsync can't override
# them); the files are restored after the run.
# base: -l loads map "ot" (a stock Sauerbraten map: coop edit, the player at a start of the map, the pistol in
# hand) and the player stands still; a CubeScript (-x) counts frames for 30 s from 10 s after the first frame
# and prints "ps3bench: <n> frames in <ms> ms: <fps> fps, longest <ms> ms" (fps=, maxms=), then its own
# steady window, 60 s after the first frame for 120 s (STEADY_RE), and quits. One view's rendering once
# loading is over; a map with players and bots (the played session) costs more. Written without a PS3 run
# (2026-10-25): the first base run still has to confirm the map, the window and the numbers.
# A played session (session.sh): the launcher alone, the main menu; the player picks a map and mode. The map's
# "read map" line marks the first gameplay frame (PLAY_MARK).
SUITE=0
COMM=cube2_client
# the game data GAMEDATA=ssd takes from the SSD (sauerbraten-data)
DATA_DIRS=(/usr/share/games/sauerbraten)
SB_HOME=$HOME/.sauerbraten
BACKUP=("$SB_HOME/autoexec.cfg" "$SB_HOME/config.cfg" "$SB_HOME/restore.cfg" "$SB_HOME/init.cfg")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against packages/walnut/apps/sauerbraten/files/sauerbraten: -q"$HOME/.sauerbraten"
SESSION_HOME=(.sauerbraten)
SB_MAP=ot
SB_SET=()	# extra CubeScript lines for autoexec.cfg (variants)
SB_INIT=()	# extra lines for init.cfg, which the engine reads before it starts the sound (soundfreq...)
setup() {
	mkdir -p "$SB_HOME"
	printf '%s\n' "fullscreen 1" "vsync 0" "maxfps 0" "${SB_SET[@]}" > "$SB_HOME/autoexec.cfg"
	[ ${#SB_INIT[@]} -eq 0 ] || printf '%s\n' "${SB_INIT[@]}" >> "$SB_HOME/init.cfg"
}
SB_SCRIPT='ps3f = 0; ps3max = 0
ps3count = [ps3f = (+ $ps3f 1); ps3now = (getmillis 1); ps3max = (max $ps3max (- $ps3now $ps3last)); ps3last = $ps3now; sleep 1 ps3count]
sleep 1 [echo "ps3bench: first frame"; sleep 10000 [echo "ps3bench: start"; ps3t = (getmillis 1); ps3last = $ps3t; ps3count; sleep 30000 [ps3ms = (- (getmillis 1) $ps3t); echo (format "ps3bench: %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3f $ps3ms (divf (*f $ps3f 1000) $ps3ms) $ps3max)]; sleep 50000 [ps3f0 = $ps3f; ps3t2 = (getmillis 1); ps3max = 0]; sleep 170000 [ps3ms = (- (getmillis 1) $ps3t2); ps3n = (- $ps3f $ps3f0); echo (format "ps3bench: steady %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3n $ps3ms (divf (*f $ps3n 1000) $ps3ms) $ps3max); quit]]]'
sb_cmd() {
	CMD=(/usr/games/sauerbraten -w1920 -h1080 "-l$SB_MAP" "-x$SB_SCRIPT")
}
sb_cmd
FPS_RE='^ps3bench: [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
MAXMS_RE='^ps3bench: .* longest ([0-9]+) ms'
STEADY_RE='^ps3bench: steady [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
STEADY_MAXMS_RE='^ps3bench: steady .* longest ([0-9]+) ms'
LOAD_MARK='^read map '
PLAY_MARK=$LOAD_MARK
# the launcher without the benchmark map and script: the player starts at the menu
session() {
	CMD=(/usr/games/sauerbraten)
}
# a safety net: the load, then ~190 s of counting; quits by itself
DURATION=330
during() {
	while :; do
		printf '%(%s)T %s\n' -1 "$(awk '/^(MemAvailable|SwapFree):/ { printf "%s%s ", $1, $2 }' /proc/meminfo)"
		sleep 5
	done
}
parse() {
	awk '{ for (i = 2; i <= NF; i++) if ($i ~ /^MemAvailable:/) { v = substr($i, 14) + 0; if (!n++ || v < m) m = v } }
	     END { if (n) print "min_memavailable_kb=" m }' "$D/during.log" 2>/dev/null
}
# another map: the first run on the PS3 picks which stock map is the reference (SB_MAP via the variant)
variant_complex() { SB_MAP=complex; sb_cmd; }
variant_metl3() { SB_MAP=metl3; sb_cmd; }
# the bundle meant to ship in the BananaBread tests (sauerbraten.sh, variant lean): no water passes,
# low-detail shaders, models without bump, one dynamic light, compressed 512 textures, 22 kHz sound
variant_lean() {
	SB_SET+=("waterreflect 0" "waterrefract 0" "shaderdetail 1" "bumpmodels 0" "envmapmodels 0"
		"maxdynlights 1" "texcompress 512")
	SB_INIT+=("soundfreq 22050")
}
variant_gpuskel() { SB_SET+=("gpuskel 1"); }
