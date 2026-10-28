# Neverputt (our neverball source package, 1.6.0+git20180603-3+ps3.N: neverputt, neverputt-data): smoke
# test, not benchmarked. Neverball's golf game: no replay to start from the command line, so the menus
# are walked by XTEST (Return on Play, the first course, the first hole). Fresh settings: the run starts
# without ~/.neverball/neverputtrc (restored after the run), so what is checked is the package's own
# default (0004: fullscreen at the screen's mode).
# What it exercises: the same GL path as Neverball (fixed function, VBOs, point sprites), SDL2 audio
# (Ogg), the ball physics. Risks: few; the course's water and shadows.
COMM=neverputt
. "$BENCH_LIB/smoke.inc"
NP_RC=$HOME/.neverball/neverputtrc
BACKUP=("$NP_RC")
setup() { rm -f "$NP_RC"; }
CMD=(/usr/games/neverputt)
NP_KEYS=(Return +4 Return +4 Return +6)
during() { [ ${#NP_KEYS[@]} = 0 ] && return 0; smoke_xkeys Neverputt 10 "${NP_KEYS[@]}"; shot_mark; }
SHOT_FROM=mark
variant_menu() { NP_KEYS=(); SHOT_STATIC=1; }
