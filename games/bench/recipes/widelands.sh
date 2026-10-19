# Widelands 1.3.1 (Debian ports ppc64: widelands, widelands-data; +628 MB installed, 422 MB download,
# mostly data and the fonts it drags in; libglew2.3 is new): smoke test, not benchmarked. Starts the
# first tutorial scenario ("Basic Control", campaigns/tutorial01_basic_control.wmf) straight away, fullscreen
# 1920x1080, sound off in variant nosound. The tutorial is a scripted introduction: it pauses on message
# windows that need a click (the first is an intro box). The mouse does everything (scroll with
# the edges or arrows, build from the building menu, Esc / F10 opens the menu). Variant menu is the main
# menu; skip: the same tutorial map with `--autosave=0`.
# Settings: ~/.widelands/config (restored after the run); the command line sets the video mode:
# --fullscreen alone: 1.3.1 takes no value for it and refuses --xres/--yres beside it (the screen's mode is used).
# What it exercises: SDL2 + OpenGL 2.1 with GLSL 1.20 shaders (Widelands 1.x always renders through
# OpenGL; no GL 3 needed -- a GLSL compile error on nv30 would show in the log as "shader"), lots of
# small sprites in batches, SDL2_image/ttf/mixer, Lua scripting. Savegames are explicit-byte-order.
# Risks: RAM (the map and building graphics are decoded into big textures: a 217 MB machine is the
# worry, watch dmesg.log and MemAvailable), GLSL limits, loading time, the game's own music.
COMM=widelands
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.widelands/config")
WL_MAP=/usr/share/games/widelands/data/campaigns/tutorial01_basic_control.wmf
CMD=(/usr/games/widelands --fullscreen --scenario="$WL_MAP")
variant_menu() { CMD=(/usr/games/widelands --fullscreen); }
variant_nosound() { CMD+=(--nosound); }
variant_skip() { CMD+=(--autosave=0); }
