# Simutrans 124.5 with pak64 (Debian ports ppc64: simutrans, simutrans-data, simutrans-pak64; +30 MB
# installed, 6 MB download): smoke test, not benchmarked. Starts a new game on the default random map
# at once (no -load, no demo.sve in pak64: simmain.cc generates a world with the default settings and
# opens straight on it), pak64 (the default pak, /usr/share/games/simutrans/pak), fullscreen
# 1920x1080 (-fullscreen -screensize), sound off (-nosound: no sound card dependency; variant sound
# on). Nothing moves until the player lays track or a road (toolbar, mouse); the world's own
# traffic, water and the month clock tick. Keyboard: arrows scroll, +/- zoom, Esc opens the menu,
# 'Q' quits. Variant fps25/fps60: -fps; hw: -use_hw (SDL2's accelerated renderer instead of its software
# texture path); small: -screensize 1280x720; scenario: the pak64 "millionaire" scenario (a
# tutorial-like map with a goal), loaded by -scenario.
# Simutrans' video is its own software blitter into an SDL2 texture at the screen size, so at
# 1080p the PPE paints 2 Mpixel per redraw (only the changed tiles: dirty rectangles); big-endian: pak
# files are little-endian and read through explicit byte-order helpers (it has shipped for PPC Macs).
# User data: ~/.simutrans (simuconf.tab, saved games, screenshots); simuconf.tab is written by the game
# at exit, so BACKUP restores it. -use_workdir is NOT used (Debian's install dir is read-only).
# Risks: RAM at 1080p (the world 128x128 plus pak64's 12 MB of images, a few tens of MB), the SDL2
# texture upload colours on big-endian, the PPE's redraw speed.
COMM=simutrans
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.simutrans/simuconf.tab")
SIM_SIZE=1920x1080
sim_cmd() { CMD=(/usr/games/simutrans -objects pak/ -fullscreen -screensize "$SIM_SIZE" -nosound "$@"); }
sim_cmd
variant_sound() { CMD=("${CMD[@]/-nosound}"); }
variant_hw() { CMD+=(-use_hw); }
variant_fps25() { CMD+=(-fps 25); }
variant_fps60() { CMD+=(-fps 60); }
variant_small() { SIM_SIZE=1280x720; sim_cmd; }
variant_scenario() { sim_cmd -scenario millionaire; }
