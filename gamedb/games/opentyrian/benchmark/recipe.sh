# OpenTyrian 2.1 (our opentyrian, 2.1.20260912-1+ps3.N, and tyrian-data from the licensed suite):
# smoke test, not benchmarked. SDL2 renders the 320x200 8-bit VGA surface through a scaler (Scale2x
# by default, in software) to a texture that SDL draws scaled up to the 1920x1080 desktop-fullscreen
# window; no OpenGL needs from the game itself. Data: /usr/share/games/tyrian (the freeware Tyrian
# 2.1, gamedb/games/opentyrian/licensed/tyrian-data/tyrian-data.sh -> package tyrian-data), found without options.
# The game starts at its intro and title screen: during() presses Return (XTEST, smoke_xkeys) to skip
# the intro and take "New Game" (the default item), then through the episode, difficulty and ship
# screens into the first level, then Space (fire) now and then. The key sequence is a first guess
# from the source (the screens' order), not seen on the PS3: look at the webcam photo, and change
# OT_KEYS (X keysym names, +N waits N seconds) if it stops on a menu. Variant menu: no keys.
# Settings: ~/.config/opentyrian/opentyrian.cfg (fullscreen 0 = desktop fullscreen, scaler) is written
# by setup() and restored by BACKUP; the game rewrites it at exit.
COMM=opentyrian
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.config/opentyrian/opentyrian.cfg")
OT_SCALER=Scale2x
OT_KEYS=(Return +3 Return +3 Return +3 Return +3 Return +6 space +5 space +5 space)
CMD=(/usr/games/opentyrian)
setup() {
	mkdir -p "$HOME/.config/opentyrian"
	printf "section video\n\titem fullscreen 0\n\titem scaler '%s'\n\titem scaling_mode 'Fit 8:5'\n" "$OT_SCALER" > "$HOME/.config/opentyrian/opentyrian.cfg"
	[ -e /usr/share/games/tyrian/tyrian1.lvl ] || echo "WARNING: no Tyrian data: apt install tyrian-data (the licensed suite)"
}
during() { [ ${#OT_KEYS[@]} -gt 0 ] || return 0; smoke_xkeys OpenTyrian 8 "${OT_KEYS[@]}"; shot_mark; }
SHOT_FROM=mark	# the screenshots after the keys (the load's end in the variants without keys)
variant_menu() { OT_KEYS=(); SHOT_STATIC=1; }
variant_nosound() { CMD=(/usr/games/opentyrian --no-sound); }
# the cheaper (nearest-neighbour 2x in SDL) and the dearer scaler
variant_noscaler() { OT_SCALER=None; }
variant_scale3x() { OT_SCALER=Scale3x; }
