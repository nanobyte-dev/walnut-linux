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
# of 2026-10-18 sat on it for its whole 120 s, although a virtual keyboard (evemu-device, smoke_keys)
# had played two Returns 30 s after the window (device created, X added it, Return is MENU_SELECT in
# keyboard_config.cpp; why SDL2 did not see them is not known: the focus, or the 50 ms press landing
# in one frame, Controller::pressed compares with the last frame). So during() presses the keys
# through XTEST (smoke.inc's smoke_xkeys, xkeys.py: focus set on the window, keys held 0.25 s)
# ST_KBD_WAIT s after the window appears (the card was up at 28 s from the start then, and still at
# 40 s), then Space (Jump, also leaves the card) and Return again, 8 s apart, in case one came
# during the load (in the level itself Return does nothing: Menu-select is read only by menus, the
# world map and text arrays; Space makes Tux jump). Not for worldmap and menu, where it would pick.
# What it exercises: GL 2.1 fixed function with an FBO, SDL2_image PNG loading (big-endian
# colour order), OpenAL + Ogg music.
# Risks: the renderer choice (--renderer opengl20 forces the old GL 2.0 path; `auto` would pick
# opengl33 first and fall back), FBO use on nv30, RAM (the data is 240 MB installed).
# SuperTux saves its video settings (-f, -g) in ~/.local/share/supertux2/config at exit: BACKUP.
COMM=supertux2
. "$BENCH_LIB/smoke.inc"
ST_DATA=/usr/share/games/supertux2
ST_LEVEL=$ST_DATA/levels/world1/welcome_antarctica.stl
BACKUP=("$HOME/.local/share/supertux2/config")
ST_RENDERER=opengl20
st_cmd() { CMD=(/usr/games/supertux2 --renderer "$ST_RENDERER" -f -g 1920x1080 --show-fps "$@"); }
st_cmd "$ST_LEVEL"
ST_KBD_WAIT=30
ST_KEYS=1	# the Return keys past the title card (off for worldmap and menu)
during() { [ "$ST_KEYS" = 1 ] || return 0; smoke_xkeys SuperTux "$ST_KBD_WAIT" Return +8 space +8 Return; shot_mark; }
SHOT_FROM=mark	# the screenshots after the keys (the load's end in the variants without keys)
variant_worldmap() { ST_KEYS=0; st_cmd "$ST_DATA/levels/world1/worldmap.stwm"; }
variant_menu() { ST_KEYS=0; st_cmd; SHOT_STATIC=1; }
# the title card left alone (the run of 2026-10-18)
variant_card() { ST_KEYS=0; }
variant_sdl() { ST_RENDERER=sdl; st_cmd "$ST_LEVEL"; }
variant_nomusic() { CMD+=(--disable-music); }
variant_fresh() { CMD=(/usr/games/supertux2 "$ST_LEVEL"); }	# no video options: what a new player gets (the level card keys still pressed)
