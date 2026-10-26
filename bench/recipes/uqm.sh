# The Ur-Quan Masters 0.8.0 (our uqm, 0.8.0+dfsg-4+ps3.N; the content is Debian's non-free arch:all
# uqm-content and uqm-music 0.8.0+deb-1, installed from the ps3-licensed suite, where we put them
# unchanged): smoke test, not benchmarked. SDL2; the 320x240 game screen is scaled up by the engine's
# OpenGL path (--opengl: one textured quad through SDL's GL context, fixed function) to a 1920x1080
# fullscreen window. Variants: sdl (--nogl: SDL2's own renderer instead), window, menu (no keys),
# melee (Super-Melee instead of a new game), nomusic.
# The game starts at the title/intro; during() presses Return (XTEST, smoke_xkeys) for the intro and
# the main menu's default item, "Start New Game" (restart.c: START_NEW_GAME is 0, the first item),
# then Return again now and then to get through the opening scenes. Not seen on the PS3: the key
# sequence is a first guess from the source. Look at the webcam photo, change UQM_KEYS if it stops
# on a menu. The Super-Melee keys (Down Down Return, then Return into the fleet setup) are a guess too.
# Config: ~/.uqm/uqm.cfg is rewritten by the game, restored by BACKUP.
COMM=uqm
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.uqm/uqm.cfg")
UQM_GL=(--opengl)
UQM_WIN=(-f)
UQM_KEYS=(Return +4 Return +4 Return +6 Return +6 Return +10 Return)
uqm_cmd() { CMD=(/usr/games/uqm -r 1920x1080 "${UQM_WIN[@]}" "${UQM_GL[@]}" "${UQM_EXTRA[@]}"); }
UQM_EXTRA=()
uqm_cmd
setup() { [ -e /usr/share/games/uqm/content/uqm.addons ] || [ -d /usr/share/games/uqm/content ] || echo "WARNING: no UQM content: apt install uqm-content uqm-music (ps3-licensed suite)"; }
during() { [ ${#UQM_KEYS[@]} -gt 0 ] || return 0; smoke_xkeys "The Ur-Quan" 8 "${UQM_KEYS[@]}"; shot_mark; }
SHOT_FROM=mark	# the screenshots after the keys (the load's end in the variants without keys)
variant_sdl() { UQM_GL=(--nogl); uqm_cmd; }
variant_window() { UQM_WIN=(-w); uqm_cmd; }
variant_menu() { UQM_KEYS=(); SHOT_STATIC=1; }
variant_melee() { UQM_KEYS=(Down +1 Down +1 Return +4 Return +4 Return +6 Return); }
variant_nomusic() { UQM_EXTRA=(--musicvol=0); uqm_cmd; }
