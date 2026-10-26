# Cataclysm: Dark Days Ahead 0.F-3 (Debian ports ppc64: cataclysm-dda-sdl with cataclysm-dda-data, +399
# MB installed, 115 MB download, fonts-roboto-fontface, fonts-terminus, fonts-unifont): smoke test, MAYBE,
# not benchmarked. Menu only: MOTD, New Game, a world (the first creates one: world generation allocates
# hundreds of MB on a typical run, a 217 MB PS3 is likely to OOM-kill it: that is the result this test
# is for), a character, then the tiles view. The wrapper is /usr/games/cataclysm-tiles (SDL2 with the
# SDL renderer; it has a curses sibling that is not this test). Keyboard: Enter/arrows in the menus,
# '?' for the key list; the character creation takes about a minute of key presses.
# Settings: ~/.config/cataclysm-dda/config/options.json gets fullscreen and the software SDL renderer
# (RENDERER=software; variant accel: opengl). BACKUP restores it. Saves
# are left in ~/.local/share/cataclysm-dda (the world is created by the player, so not removed).
# Risks: RAM (world + mapgen; watch dmesg.log), SDL2 texture-atlas upload (a 4096x4096 tileset
# texture), the fonts, and big-endian: save files are JSON text (endian-free), the tileset PNGs go
# through SDL2_image.
COMM=cataclysm-tiles
. "$BENCH_LIB/smoke.inc"
CD_OPT=$HOME/.config/cataclysm-dda/config/options.json
BACKUP=("$CD_OPT")
CD_REND=software
setup() {
	mkdir -p "${CD_OPT%/*}"
	printf '[{"name":"FULLSCREEN","value":"fullscreen"},{"name":"RENDERER","value":"%s"},{"name":"VSYNC","value":false}]\n' "$CD_REND" > "$CD_OPT"
}
CMD=(/usr/games/cataclysm-tiles)
variant_accel() { CD_REND=opengl; }
