# SuperTux 0.6.3 (Debian ports ppc64: supertux, supertux-data 158 MB download; the binary is
# supertux2): smoke test, not benchmarked. Starts level 1 of the first world, "Welcome to
# Antarctica", straight away (a level file on the command line): fullscreen 1920x1080, the
# fixed-function/FBO renderer `--renderer opengl20`, the HUD fps counter on (--show-fps). No demo
# ships, so Tux stands still until played (arrows move, Space/Up jumps, Ctrl runs; Esc pauses and
# leaves). Variant worldmap: the world map of the first world instead (walk to a level with Enter);
# menu: the title menu; sdl: SuperTux's software renderer (no GL, the comparison if opengl20 is wrong).
# The level's title card ("Welcome to Antarctica" and a jumping Tux) waits for a key: LevelIntro::update
# (src/supertux/levelintro.cpp) leaves only on Jump, Action, Menu-select, Start or Esc, with no
# timeout, and GameSession::setup shows it for every level with coins, badguys or secrets. The run
# of 2026-10-18 sat on it for its whole 120 s. So during() types Return into a virtual keyboard
# (smoke.inc's smoke_keys) ST_KBD_WAIT s after the window appears (the card was up at 28 s from the
# start then, and still at 40 s), and once more 8 s later in case the first came during the load
# (Return does nothing in the level itself: Menu-select is read only by menus, the world map and
# text arrays, none in this level). Not for worldmap and menu, where Return would pick something.
# What it exercises: GL 2.1 fixed function with an FBO, SDL2_image PNG loading (big-endian
# colour order), OpenAL + Ogg music.
# Risks: the renderer choice (--renderer opengl20 forces the old GL 2.0 path; `auto` would pick
# opengl33 first and fall back), FBO use on nv30, RAM (the data is 240 MB installed).
# SuperTux saves its video settings (-f, -g) in ~/.local/share/supertux2/config at exit: BACKUP.
COMM=supertux2
. "$here/recipes/smoke.inc"
ST_DATA=/usr/share/games/supertux2
ST_LEVEL=$ST_DATA/levels/world1/welcome_antarctica.stl
BACKUP=("$HOME/.local/share/supertux2/config")
ST_RENDERER=opengl20
st_cmd() { CMD=(/usr/games/supertux2 --renderer "$ST_RENDERER" -f -g 1920x1080 --show-fps "$@"); }
st_cmd "$ST_LEVEL"
ST_KBD_WAIT=30
ST_KEYS=1	# the Return keys past the title card (off for worldmap and menu)
during() { [ "$ST_KEYS" = 1 ] || return 0; smoke_keys SuperTux "$ST_KBD_WAIT" 001c +8 001c; }
variant_worldmap() { ST_KEYS=0; st_cmd "$ST_DATA/levels/world1/worldmap.stwm"; }
variant_menu() { ST_KEYS=0; st_cmd; }
# the title card left alone (the run of 2026-10-18)
variant_card() { ST_KEYS=0; }
variant_sdl() { ST_RENDERER=sdl; st_cmd "$ST_LEVEL"; }
variant_nomusic() { CMD+=(--disable-music); }
