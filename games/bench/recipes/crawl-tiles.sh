# Dungeon Crawl Stone Soup 0.34.1 tiles build (Debian ports ppc64: crawl-tiles + crawl-common +
# crawl-tiles-data, +47 MB installed, 22 MB download): smoke test, not benchmarked. Starts a new
# character at once, no menus: a Human Berserker (the background with a fixed starting weapon: other
# backgrounds ask for one) named Smoke on a fixed seed (-seed 1, so every run has the same dungeon
# and the same first levels), in the tiles window at 1920x1080 fullscreen. The first screen is the
# Dungeon level 1 with the tiles map, inventory and message panes. Keys (arrows or vi keys hjklyubn
# move, '5' or 's' waits, 'o' auto-explores, Tab auto-fights, Ctrl-Q or 'S' saves and quits). With
# auto-explore ('o') held by xkeys the game plays itself, which is what variant explore does: it
# presses 'o' every 6 s once the window is up (it walks until something comes into view).
# Settings: a private rc, ~/.crawl/ps3-smoke.rc (BACKUP restores it; the game's saves in ~/.crawl/saves
# stay): tile_full_screen, tile_window_width/height 1920x1080 (option names from crawl's
# options_guide.txt; the log tells if one was refused). The tiles renderer is SDL2 + OpenGL (textured
# quad batches, GL 1.x/2.x level); save files are marshalled with
# explicit byte order (Debian builds it on s390x too).
# Risks: the GL context from SDL2's video (sdl2-compat over SDL3), texture atlas size (4096x4096
# atlas upload: VRAM), a Berserker's species/background pair being refused (the log says so),
# the character name already saved (the game loads that save instead: harmless).
COMM=crawl-tiles
. "$here/recipes/smoke.inc"
# "Loading maps..." (the des files, in a small splash window) was still up 60 s into the first run
# (2026-10-19, 120 s run): give it 300 s; SIGTERM at the end prints a "crash" report, it is the harness
DURATION=${SMOKE_SECS:-300}
CR_RC=$HOME/.crawl/ps3-smoke.rc
BACKUP=("$CR_RC")
setup() {
	mkdir -p "${CR_RC%/*}"
	printf '%s\n' "tile_full_screen = true" "tile_window_width = 1920" "tile_window_height = 1080" > "$CR_RC"
}
CMD=(/usr/games/crawl-tiles -rc "$CR_RC" -name Smoke -species Hu -background Be -seed 1)
CR_EXPLORE=0
during() { [ "$CR_EXPLORE" = 1 ] || return 0; smoke_xkeys Dungeon 120 o +6 o +6 o +6 o +6 o +6 o +6 o +6 o +6 o; }
variant_explore() { CR_EXPLORE=1; }
variant_sprint() { CMD+=(-sprint); }
variant_menu() { CMD=(/usr/games/crawl-tiles -rc "$CR_RC"); }
