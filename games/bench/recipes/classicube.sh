# ClassiCube (ours): no timedemo. Loads a saved map (assets/classicube-bench.cw, the player at the
# map's spawn, standing still) for a fixed time; fps from the Gallium HUD. A flight path needs a
# plugin (later). Its options live next to the game in ~/.local/share/classicube.
SUITE=1
COMM=ClassiCube
CC_DIR=${XDG_DATA_HOME:-$HOME/.local/share}/classicube
BACKUP=("$CC_DIR/options.txt")
CC_LIMIT=LimitNone
setup() {
	mkdir -p "$CC_DIR/maps"
	cp "$ASSETS/classicube-bench.cw" "$CC_DIR/maps/bench.cw"
	[ -e "$CC_DIR/options.txt" ] || cp /usr/lib/classicube/options.txt "$CC_DIR/"
	sed -i '/^\(fpslimit\|window-width\|window-height\)=/d' "$CC_DIR/options.txt"
	printf 'fpslimit=%s\nwindow-width=1920\nwindow-height=1080\n' "$CC_LIMIT" >> "$CC_DIR/options.txt"
}
CMD=(/usr/games/classicube "$CC_DIR/maps/bench.cw")
DURATION=60
# the map loads in a few seconds and prints nothing: the HUD's first 10 s count as loading
HUD_SKIP=10
# vsync on, as players run it
variant_vsync() { CC_LIMIT=LimitVSync; }
