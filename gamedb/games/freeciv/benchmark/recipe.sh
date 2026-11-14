# Freeciv 3.2.6 (Debian ports ppc64: freeciv-client-gtk3 + freeciv-server + freeciv-data; +52 MB
# installed with the server's 4.5 MB, 20 MB download; GTK3 and SDL2 are in the image): smoke test,
# not benchmarked. A local server (freeciv-server, a script of /set commands, port 5557) starts a game
# of five AIs ("novice" skill, small map, a 3 s turn timeout so the AIs play on their own); the GTK3
# client (freeciv-gtk3.22, cairo software drawing through X, no GL) joins it in pregame as "smoke", then
# during() makes that player an AI and starts the game (see during() below).
# That shows the client's map, city and report drawing under a live game with no clicks needed. A
# human game is variant play: server started, game not, the client lands in the pregame list (the
# player's slot is the one named "smoke"; "Start game" is a button) -- the player has to click it.
# The server runs under during() (its log is fc-server.log in the run directory). client.sh's SIGTERM
# at the end of the run did not stop it when it was exec'd as during() itself (ssdroot14: it outlived
# the run with 56 MB in swap and every later run refused to start), so during() waits on it and on
# TERM stops it, with SIGKILL 3 s later.
# freeciv-sdl2 (freeciv-client-sdl) is not used: its package hard-depends on fonts-noto-cjk and the
# other CJK fonts (+170 MB installed) and the SDL2 client is the least maintained. Variant sdl2 runs it
# anyway if installed.
# Fullscreen: the GTK client has the option gui_gtk3_fullscreen (its client rc,
# ~/.freeciv/freeciv-client-rc-3.2, BACKUP restores it); the key name is UNVERIFIED -- if the window
# is windowed in the first run, Alt+Return in the client toggles it (by hand).
# First run (2026-10-19): the script's `start` was refused ("Not enough human players", minplayers 1)
# and "set skill" is not an option (skilllevel), so the client sat in the pregame chat window, a white
# screen; minplayers 0 lets the game start with no human.
# Risks: GTK3 cairo drawing at 1080p through XRender on the PPE (the tiles are small, a redraw is
# partial), the server's RAM (a few tens of MB), "freeciv-server" not installed (Recommends only:
# install it by name).
COMM=freeciv-gtk3.22
. "$BENCH_LIB/smoke.inc"
FC_PORT=5557
FC_SCRIPT=$HOME/.freeciv/ps3-smoke.serv
FC_RC=$HOME/.freeciv/freeciv-client-rc-3.2
BACKUP=("$FC_RC" "$FC_SCRIPT")
FC_START=1
setup() {
	mkdir -p "$HOME/.freeciv"
	printf '%s\n' "set aifill 5" "set skilllevel novice" "set size 1" "set timeout 3" "set startunits cwxx" \
		"set autotoggle enabled" "set minplayers 0" "set endturn 50" "set saveturns 200" \
		> "$FC_SCRIPT"
	printf '%s\n' "[client]" "gui_gtk3_fullscreen=TRUE" "gui_gtk3_enable_tabs=FALSE" "sound_enable_effects=FALSE" \
		"sound_enable_music=FALSE" > "$FC_RC"
}
# The client joins in pregame and the game starts after it: a connection that joins a running game takes no
# player (2026-11-14, twice: the pregame list for all 32 turns, then "connected to no player" on the main menu).
# The server reads its console from a fifo (with stdin at EOF it ignores input); once fc-server.log shows the
# client connected (at most FC_WAIT_S s), during() sends "aitoggle smoke" (the AI plays the client's player, so
# its map, cities and units change with no clicks) and "start". The client starts FC_JOIN_S s after the server.
FC_JOIN_S=5
FC_WAIT_S=90
CMD=(sh -c 'sleep "$1"; shift; exec "$@"' sh "$FC_JOIN_S" /usr/games/freeciv-gtk3.22 -a -s localhost -p "$FC_PORT" -n smoke)
SHOT_FROM=start	# the GTK client draws through cairo, so the HUD marks no load end: the shots count from the start (2026-11-14: both were skipped)
during() {
	local d=${BENCH_DIR:-$D} f s i
	f=$d/fc-console; rm -f "$f"; mkfifo "$f" || return 1
	exec 3<>"$f"	# held open: the server never sees EOF on its console
	/usr/games/freeciv-server -p "$FC_PORT" -r "$FC_SCRIPT" -q 600 < "$f" > "$d/fc-server.log" 2>&1 &
	s=$!
	trap 'kill $s 2>/dev/null; sleep 3; kill -9 $s 2>/dev/null; exec 3>&-; rm -f "$f"; exit 0' TERM
	if [ "$FC_START" = 1 ]; then
		for ((i = 0; i < FC_WAIT_S; i++)); do grep -q 'smoke has connected' "$d/fc-server.log" 2>/dev/null && break; sleep 1; done
		sleep 3
		printf '%s\n' "aitoggle smoke" "start" >&3
	fi
	wait $s
}
variant_play() { FC_START=0; }
variant_sdl2() { COMM=freeciv-sdl2; CMD[5]=/usr/games/freeciv-sdl2; }
