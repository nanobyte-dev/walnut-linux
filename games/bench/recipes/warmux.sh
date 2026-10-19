# Warmux 11.04.1 (Debian ports ppc64: warmux, warmux-data; +119 MB installed, 98 MB download, most of it
# the CJK fonts Debian pulls in with it: fonts-vlgothic, fonts-wqy-microhei; SDL 1.2 stack
# libsdl-gfx1.2-5, libsdl-net1.2, libsdl-ttf2.0-0, libcurl3t64-gnutls is new): smoke test, not
# benchmarked. A Worms-like artillery game, dead upstream since 2012, SDL 1.2 through sdl12-compat over
# our SDL3. Menu only: Play > Local game (no network), pick the teams, Start (mouse; the pad does
# not work). Upstream's options are in the in-game Options menu (video: fullscreen/resolution), saved
# to ~/.warmux/config.xml (BACKUP restores it): the first run is windowed, set Fullscreen there once.
# Command line has only -v/-h/-d: nothing starts a match.
# What it exercises: sdl12-compat's SDL_SetVideoMode surface blitting at 1080p (software, the whole
# map is a big 8-bit-alpha surface repainted by region), SDL_gfx rotozoom, SDL_mixer. Same family as
# Pingus and Frozen Bubble.
# Risks: big-endian: the map/terrain format is generated at run time from PNGs (Debian built it on
# powerpc/ppc64 for years), but the SDL_gfx rotozoom code assumes pixel orders (colour channel mix-ups
# would show on the worms); RAM with the big maps.
COMM=warmux
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.warmux/config.xml")
CMD=(/usr/games/warmux)
variant_debug() { CMD+=(-d); }
