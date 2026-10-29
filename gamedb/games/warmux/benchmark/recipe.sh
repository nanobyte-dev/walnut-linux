# Warmux 11.04.1 (Debian ports ppc64: warmux, warmux-data; +119 MB installed, 98 MB download, most of it
# the CJK fonts Debian pulls in with it: fonts-vlgothic, fonts-wqy-microhei; SDL 1.2 stack
# libsdl-gfx1.2-5, libsdl-net1.2, libsdl-ttf2.0-0, libcurl3t64-gnutls is new): smoke test, not
# benchmarked. A Worms-like artillery game, dead upstream since 2012, SDL 1.2 through sdl12-compat over
# our SDL3. Menu only: Play > Local game (no network), pick the teams, Start (mouse; the pad does
# not work). Upstream's options are in the in-game Options menu (video: fullscreen/resolution), saved
# to ~/.warmux/config.xml (BACKUP restores it): the first run is windowed, set Fullscreen there once.
# Command line has only -v/-h/-d: nothing starts a match.
# What it exercises: sdl12-compat's SDL_SetVideoMode surface blitting at 1080p (software, the whole
# map is a big 8-bit-alpha surface repainted by region), SDL_gfx rotozoom, SDL_mixer. Same family as
# Pingus and Frozen Bubble.
# Risks: big-endian: the map/terrain format is generated at run time from PNGs (Debian built it on
# powerpc/ppc64 for years), but the SDL_gfx rotozoom code assumes pixel orders (colour channel mix-ups
# would show on the worms); RAM with the big maps.
COMM=warmux
. "$BENCH_LIB/smoke.inc"
# the config is ~/.config/wormux/config.xml (the game still calls itself wormux; it exists after the
# first run); the first run (2026-10-19) was a small window: switch full_screen on if the file is there
WM_CFG=$HOME/.config/wormux/config.xml
BACKUP=("$WM_CFG")
WM_FRESH=0
setup() {
	# fresh: no configuration, what a new player gets (our package: fullscreen at the screen's size)
	[ "$WM_FRESH" = 1 ] && { rm -f "$WM_CFG"; return 0; }
	# a set size (WM_RES=WxH): a minimal config.xml, fullscreen at that size (sdl12-compat scales it up)
	if [ -n "${WM_RES:-}" ]; then
		mkdir -p "${WM_CFG%/*}"
		printf '<?xml version="1.0" encoding="utf-8"?>\n<config><version>11.04</version><video><width>%s</width><height>%s</height><full_screen>true</full_screen></video></config>\n' \
			"${WM_RES%x*}" "${WM_RES#*x}" > "$WM_CFG"
		return 0
	fi
	[ -f "$WM_CFG" ] && sed -i 's|<full_screen>false<|<full_screen>true<|' "$WM_CFG"; return 0
}
CMD=(/usr/games/warmux)
variant_debug() { CMD+=(-d); }
variant_fresh() { WM_FRESH=1; }
# Into a local match without the user (games night 2026-10-28): clicks through XTEST (xkeys.py @X,Y at
# 1920x1080) 20 s after the window; WM_CLICKS are the steps
WM_CLICKS=()
during() { [ ${#WM_CLICKS[@]} = 0 ] && return 0; smoke_xkeys Warmux 20 "${WM_CLICKS[@]}"; shot_mark; }
SHOT_FROM=mark
variant_play() { WM_FRESH=1; WM_CLICKS=(@765,470 +1 @770,480 +6 @910,1050 +1 @916,1054 +15); }	# Play, then the green tick (two human teams, map Abyss)
# the same match at a smaller size scaled up (7 fps at 1920x1080, 2026-10-29)
variant_play540() { variant_play; WM_FRESH=0; WM_RES=960x540; }
variant_play720() { variant_play; WM_FRESH=0; WM_RES=1280x720; }
