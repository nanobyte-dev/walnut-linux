# DDNet 20.1.1 (our ddnet, ddnet-data and ddnet-server, 20.1.1+ds-1+ps3.N: Debian's ppc64 build is 16.0.2):
# smoke test, not benchmarked. A Teeworlds 0.6 descendant with its own protocol. The SDL2 client at
# 1920x1080 fullscreen connects to a DDNet-Server that during() starts on the same machine (map
# Sunny Side Up, the default; no registration with the master servers): there is no single-player mode, so the
# player runs around alone (arrow keys or A/D, space jumps, mouse aims and hooks).
# Graphics: DDNet's default on a non-x86 build is the Vulkan backend, falling back to OpenGL 3.0 and
# then down the versions one failed context at a time (graphics_threaded.cpp), each try costing
# seconds. Everything below OpenGL 3.3 is the legacy renderer (fixed function, no 3D/array textures),
# which is what the nv30 gets. The base therefore asks for it directly: gfx_backend OpenGL with a
# 2.1 context. Variants: gl15 and gl11 (older legacy contexts), default (no backend settings: the
# Vulkan try and the fall-back chain, with the time they take), menu (no server: the client stays in
# the main menu), window.
# Needs our ddnet and ddnet-server packages: Debian's ppc64 ones are older and, like Teeworlds 0.7.5,
# have the unsigned long FIONBIO bug (packages/patched/games/ddnet/ patch 0001: the sockets stayed blocking on big-endian
# 64-bit, the client sat in recvfrom() at the loading screen). `dpkg -l ddnet` must show +ps3.
# Settings go on the command line (each argument is a console line run before the client starts);
# the client writes them to ~/.local/share/ddnet/settings_ddnet.cfg at exit, which BACKUP restores.
COMM=DDNet
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.local/share/ddnet/settings_ddnet.cfg")
DD_GFX=("gfx_backend OpenGL" "gfx_gl_major 2" "gfx_gl_minor 1")
DD_ARGS=("gfx_fullscreen 1" "gfx_vsync 0" "gfx_fsaa_samples 0" "gfx_screen_width 1920" "gfx_screen_height 1080"
	"cl_showfps 1" "player_name smoke" "cl_skip_start_menu 1")
DD_CONNECT="connect 127.0.0.1:8303"
dd_cmd() { CMD=(/usr/games/DDNet "${DD_GFX[@]}" "${DD_ARGS[@]}" ${DD_CONNECT:+"$DD_CONNECT"}); }
dd_cmd
# the local server (ddnet-server is its own package); it dies with the run
during() {
	[ -x /usr/games/DDNet-Server ] || { echo "no ddnet-server: apt install ddnet-server"; return 1; }
	[ -n "$DD_CONNECT" ] || { echo "menu variant: no server"; return 0; }
	local v; for v in ddnet ddnet-server; do
		case $(dpkg-query -W -f '${Version}' $v 2>/dev/null) in *+ps3*) ;; *) echo "WARNING: $v is not ours (+ps3): the sockets stay blocking on big-endian";; esac
	done
	/usr/games/DDNet-Server "sv_name smoke" "sv_port 8303" "sv_register 0" \
		"sv_max_clients 4" &
	local srv=$!
	trap 'kill $srv 2>/dev/null' TERM EXIT
	wait $srv
}
variant_gl15() { DD_GFX=("gfx_backend OpenGL" "gfx_gl_major 1" "gfx_gl_minor 5"); dd_cmd; }
variant_gl11() { DD_GFX=("gfx_backend OpenGL" "gfx_gl_major 1" "gfx_gl_minor 1"); dd_cmd; }
variant_default() { DD_GFX=(); dd_cmd; }
variant_menu() { DD_CONNECT=; dd_cmd; SHOT_STATIC=1; }
variant_window() { DD_ARGS=("${DD_ARGS[@]/gfx_fullscreen 1/gfx_fullscreen 0}"); dd_cmd; }
