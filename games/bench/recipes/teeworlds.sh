# Teeworlds 0.7.5 (Debian ports ppc64: teeworlds, teeworlds-data, and teeworlds-server for the local
# game): smoke test, not benchmarked. The SDL2 client at 1920x1080 fullscreen connects to a
# teeworlds-server that during() starts on the same machine (map dm1, no registration with the master
# server, 4 slots): there is no single-player mode and no bots, so the player runs around alone
# (arrow keys or WASD, mouse aims; the DS3 only through SDL's joystick mapping, untried). Variant
# menu: no server, the client stays in the main menu (server browser needs the network).
# What it exercises: `gfx_texture_compression 1` makes the client upload its textures as
# GL_COMPRESSED_RGBA, which Mesa turns into DXT5 (our libspeq 0.367 encodes DXT5 on the SPEs through
# Mesa patch 0032, not in a Mesa deb yet: with the current Mesa Mesa's own PPE encoder does it, ~2.6
# Mpixel/s, so the start can take seconds longer; note which it was, the log has libspeq's lines with
# SPEQ_DEBUG=1), and the tile maps are uploaded as one 3D texture (the tiles as layers), the case
# GAME-SUITE.md lists it for. Variant notc is the same without compression.
# Settings go on the command line (each argument is a console line, run before the client starts);
# the client also writes them to ~/.teeworlds/settings.cfg at exit, which BACKUP restores.
COMM=teeworlds
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.teeworlds/settings.cfg")
TW_ARGS=("gfx_screen_width 1920" "gfx_screen_height 1080" "gfx_fullscreen 1" "gfx_vsync 0" "gfx_maxfps 0"
	"gfx_fsaa_samples 0" "gfx_high_detail 0" "gfx_texture_quality 1" "gfx_texture_compression 1"
	"cl_showfps 1" "player_name smoke")
TW_CONNECT="connect 127.0.0.1:8303"
tw_cmd() { CMD=(/usr/games/teeworlds "${TW_ARGS[@]}" ${TW_CONNECT:+"$TW_CONNECT"}); }
tw_cmd
# the local server (teeworlds-server is its own package); it dies with the run
during() {
	[ -x /usr/games/teeworlds-server ] || { echo "no teeworlds-server: apt install teeworlds-server"; return 1; }
	[ -n "$TW_CONNECT" ] || { echo "menu variant: no server"; return 0; }
	/usr/games/teeworlds-server "sv_name smoke" "sv_port 8303" "sv_map dm1" "sv_register 0" \
		"sv_max_clients 4" "sv_gametype dm" &
	local srv=$!
	trap 'kill $srv 2>/dev/null' TERM EXIT
	wait $srv
}
variant_notc() { TW_ARGS=("${TW_ARGS[@]/gfx_texture_compression 1/gfx_texture_compression 0}"); tw_cmd; }
variant_menu() { TW_CONNECT=; tw_cmd; }
variant_window() { TW_ARGS=("${TW_ARGS[@]/gfx_fullscreen 1/gfx_fullscreen 0}"); tw_cmd; }
# the SPE encoders off (Mesa's own DXT path on the PPE): SPEQ_S3TC=ppe, with libspeq's counters in the log
variant_s3tcppe() { ENV+=(SPEQ_S3TC=ppe SPEQ_DEBUG=1); }
variant_s3tcspu() { ENV+=(SPEQ_S3TC=spu SPEQ_DEBUG=1); }
