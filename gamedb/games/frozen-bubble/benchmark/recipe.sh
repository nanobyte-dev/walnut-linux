# Frozen Bubble 2.212 (Debian ports ppc64: frozen-bubble, frozen-bubble-data 21 MB download; Perl with
# SDL_perl on SDL 1.2, which here is sdl12-compat over our SDL3): smoke test, not benchmarked.
# Starts a one-player game at once (--solo: level 1 of the default level set; Left/Right aim, Up
# fires, Esc leaves). The game's video mode is fixed at 640x480 (24 bit), so fullscreen is that
# picture scaled by sdl12-compat to the screen (or a mode switch to 640x480, if X offers one:
# note which; a wrong aspect or a black border is the game's, not ours). Variant menu: the title
# menu instead (--fullscreen only); nosound: --no-sound; slow: --slow-machine.
# What it exercises: SDL 1.2 software surfaces and blits (no GL in the game itself, sdl12-compat
# uploads the frame to a texture: the big-endian colour path of our SDL3 patch), SDL_mixer sound
# and Ogg music, Perl's start-up time (loading the SDL_perl modules).
# Risks: sdl12-compat's fullscreen scaling, colours, the 640x480 surface being copied to the GPU
# every frame at 1080p, Perl module load time. The run records ~/.frozenbubble (rc, high scores):
# BACKUP restores rc.
COMM=frozen-bubble
. "$BENCH_LIB/smoke.inc"
BACKUP=("$HOME/.frozenbubble/rc")
CMD=(/usr/games/frozen-bubble --fullscreen --solo)
variant_menu() { CMD=(/usr/games/frozen-bubble --fullscreen); SHOT_STATIC=1; }
variant_nosound() { CMD+=(--no-sound); }
variant_slow() { CMD+=(--slow-machine); }
variant_window() { CMD=(/usr/games/frozen-bubble --solo); }
