# Freeciv 3.2.6 (Debian ports ppc64: freeciv-client-gtk3 + freeciv-server + freeciv-data; +52 MB
# installed with the server's 4.5 MB, 20 MB download; GTK3 and SDL2 are in the image): smoke test,
# not benchmarked. A local server (freeciv-server, a script of /set commands, port 5557) starts a game
# of five AIs at once ("novice" skill, small map, a 3 s turn timeout so the AIs play on their own) and
# the GTK3 client (freeciv-gtk3.22, cairo software drawing through X, no GL) connects to it as an
# observer ("smoke": a connection that joins a started game takes no player, it sees the whole map).
# That shows the client's map, city and report drawing under a live game with no clicks needed. A
# human game is variant play: server started, game not, the client lands in the pregame list (the
# player's slot is the one named "smoke"; "Start game" is a button) -- the player has to click it.
# The server is during() itself (exec: the background process is the server, stopped with during() at
# the end of the run; its log is fc-server.log in the run directory).
# freeciv-sdl2 (freeciv-client-sdl) is not used: its package hard-depends on fonts-noto-cjk and the
# other CJK fonts (+170 MB installed) and the SDL2 client is the least maintained. Variant sdl2 runs it
# anyway if installed.
# Fullscreen: the GTK client has the option gui_gtk3_fullscreen (its client rc,
# ~/.freeciv/freeciv-client-rc-3.2, BACKUP restores it); the key name is UNVERIFIED -- if the window
# is windowed in the first run, Alt+Return in the client toggles it (by hand).
# First run (2026-10-19): the script's `start` was refused ("Not enough human players", minplayers 1)
# and "set skill" is not an option (skilllevel), so the client sat in the pregame chat window, a white
# screen; minplayers 0 lets the game start with no human, and the observer joins the running game.
# Risks: GTK3 cairo drawing at 1080p through XRender on the PPE (the tiles are small, a redraw is
# partial), the server's RAM (a few tens of MB), "freeciv-server" not installed (Recommends only:
# install it by name), the observer joining before the game has started.
COMM=freeciv-gtk3.22
. "$here/recipes/smoke.inc"
FC_PORT=5557
FC_SCRIPT=$HOME/.freeciv/ps3-smoke.serv
FC_RC=$HOME/.freeciv/freeciv-client-rc-3.2
BACKUP=("$FC_RC" "$FC_SCRIPT")
FC_START=1
setup() {
	mkdir -p "$HOME/.freeciv"
	printf '%s\n' "set aifill 5" "set skilllevel novice" "set size 1" "set timeout 3" "set startunits cwxx" \
		"set autotoggle enabled" "set minplayers 0" "set endturn 50" "set saveturns 200" \
		"$([ "$FC_START" = 1 ] && echo start)" > "$FC_SCRIPT"
	printf '%s\n' "[client]" "gui_gtk3_fullscreen=TRUE" "gui_gtk3_enable_tabs=FALSE" "sound_enable_effects=FALSE" \
		"sound_enable_music=FALSE" > "$FC_RC"
}
CMD=(/usr/games/freeciv-gtk3.22 -a -s localhost -p "$FC_PORT" -n smoke)
during() { exec /usr/games/freeciv-server -p "$FC_PORT" -r "$FC_SCRIPT" -q 600 > "${BENCH_DIR:-$D}/fc-server.log" 2>&1; }
variant_play() { FC_START=0; }
variant_sdl2() { COMM=freeciv-sdl2; CMD[0]=/usr/games/freeciv-sdl2; }
