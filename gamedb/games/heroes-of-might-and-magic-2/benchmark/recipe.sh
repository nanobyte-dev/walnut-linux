# Heroes of Might and Magic II on fheroes2 1.1.17 (Debian contrib's source built for ppc64 by us: fheroes2
# 1.1.17+dfsg-1+ps3.N) with the player's own data: the original's DATA/ and MAPS/ in ~/.local/share/fheroes2/
# (copied there for the test, never committed: hashes in technical.md): smoke test, not benchmarked.
# Variant fresh: no configuration (what a new player gets); base writes ~/.config/fheroes2/fheroes2.cfg
# with fullscreen at 1920x1080 (restored after the run). The menus are mouse-driven; FH_KEYS are xkeys.py
# steps (keys and @X,Y clicks) after the window.
# What it exercises: SDL2 through sdl2-compat, the game's own 8-bit palette renderer scaled by SDL, the AGG
# and ICN formats (little-endian, read byte by byte upstream), SDL2_mixer for MIDI/WAV.
COMM=fheroes2
. "$BENCH_LIB/smoke.inc"
FH_CFG=$HOME/.config/fheroes2/fheroes2.cfg
BACKUP=("$FH_CFG")
FH_FRESH=0
setup() {
	rm -f "$FH_CFG"; [ "$FH_FRESH" = 1 ] && return 0
	mkdir -p "${FH_CFG%/*}"
	# the first-run dialogs (language, "select a game resolution") and the intro skipped
	printf '%s\n' "videomode = 1920x1080:1920x1080" "fullscreen = on" "first time game run = off" \
		"show game intro = off" "lang = en" > "$FH_CFG"
}
CMD=(/usr/games/fheroes2)
FH_KEYS=(@1410,520 +4 @1566,74 +5 @834,716 +10 @956,618 +2)	# NEW GAME, STANDARD GAME, OKAY (the first map, "4 Dimensions"), OKAY on its story (1920x1080, the 4:3 picture in the middle)
during() { [ ${#FH_KEYS[@]} = 0 ] && return 0; smoke_xkeys fheroes2 10 "${FH_KEYS[@]}"; shot_mark; }
SHOT_FROM=mark
variant_fresh() { FH_FRESH=1; }
