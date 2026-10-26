# LinCity-NG 2.15.0 (Debian ports ppc64, +53 MB installed, 40 MB download: lincity-ng, lincity-ng-data
# and the SDL3 satellites libsdl3-image0, libsdl3-mixer0, libsdl3-ttf0, libfmt11, libxml++-5.0-1; the
# game itself is on SDL3, which is the image's libsdl3): smoke test, not benchmarked. Menu only: the
# main menu has no command-line route into a city (the options are video, size, mute and
# directories). The player clicks "Load scenario" (or "New game") and a map, then builds with the
# mouse (the toolbar at the right, the minimap at the top left; the city runs by itself once started:
# traffic, smoke, the day cycle). Esc opens the in-game menu.
# Settings: -f fullscreen, -S 1920x1080, the SDL renderer (video mode "sdl", the default; -g is
# the GL mode: variant gl), sound muted by variant mute (-m). The game rewrites its config file
# ~/.config/lincity-ng.conf (BACKUP restores it) when the in-game options change.
# What it exercises: SDL3's 2D renderer (our SDL3, the big-endian texture path), SDL3_image PNGs,
# SDL3_mixer, a large number of small isometric sprites each frame. LinCity-NG's savegames are
# text/binary written through explicit-width helpers; it has no big-endian bug reports.
# Risks: the SDL renderer's texture count and upload at 1080p, sprite batching on the PPE, the
# mouse as the only control (the DS3 pad cannot drive the menus).
COMM=lincity-ng
. "$here/recipes/smoke.inc"
BACKUP=("$HOME/.config/lincity-ng.conf")
# played sessions' fresh defaults (session.sh, freshhome.py): the game's real directories (checked against its XDG/legacy paths)
# (the derived default would have been all of ~/.config: the config is a file there)
SESSION_HOME=(.config/lincity-ng.conf .lincity-ng .local/share/lincity-ng)
CMD=(/usr/games/lincity-ng -f -S 1920x1080 -s)
variant_gl() { CMD=(/usr/games/lincity-ng -f -S 1920x1080 -g); }
variant_mute() { CMD+=(-m); }
variant_window() { CMD=(/usr/games/lincity-ng -w -S 1280x720 -s); }
