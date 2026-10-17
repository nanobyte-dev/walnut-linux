# SuperTux 0.6.3 (Debian ports ppc64: supertux, supertux-data 158 MB download; the binary is
# supertux2): smoke test, not benchmarked. Starts level 1 of the first world, "Welcome to
# Antarctica", straight away (a level file on the command line): fullscreen 1920x1080, the
# fixed-function/FBO renderer `--renderer opengl20`, the HUD fps counter on (--show-fps). No demo
# ships, so Tux stands still until played (arrows move, Space/Up jumps, Ctrl runs; Esc pauses and
# leaves). Variant worldmap: the world map of the first world instead (walk to a level with Enter);
# menu: the title menu; sdl: SuperTux's software renderer (no GL, the comparison if opengl20 is wrong).
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
variant_worldmap() { st_cmd "$ST_DATA/levels/world1/worldmap.stwm"; }
variant_menu() { st_cmd; }
variant_sdl() { ST_RENDERER=sdl; st_cmd "$ST_LEVEL"; }
variant_nomusic() { CMD+=(--disable-music); }
