# Flare 1.15 (Debian ports ppc64: flare-engine, flare-game; the game's data is 620 MB installed and
# 570 MB to download, the engine 4.5 MB; fonts-liberation and fonts-unifont come with it): smoke test,
# not benchmarked. Menu only: the title screen of "Empyrean Campaign" (Play Game, then New Game and
# the character creation; keyboard works in Flare's menus: arrows, Return, Esc). A hands-off run
# shows the title's animation only; the player starts a character. The Debian wrapper is
# /usr/games/flare (the mods are in /usr/share/games/flare/mods).
# Settings: ~/.config/flare/settings.txt is written (restored after the run): fullscreen, 1920x1080,
# vsync off, hardware surface on (SDL2's accelerated renderer; variant swsurf = off), the frame cap
# 60. Key names from the Flare engine's settings docs (resolution_w, resolution_h, fullscreen, vsync,
# hwsurface, max_fps); an unknown key is ignored by the engine.
# What it exercises: SDL2 + SDL2_image PNG tilesets, SDL2_ttf text, SDL2_mixer Ogg; an isometric
# renderer drawing many 64x32 sprites with alpha through SDL_Renderer (our SDL3 under sdl2-compat),
# so the same big-endian texture colour path as Chocolate Doom.
# Risks: 620 MB on the root file system, loading the campaign's maps on a 217 MB machine (the
# tileset PNGs decoded whole), the SDL2 renderer's colours.
COMM=flare
. "$BENCH_LIB/smoke.inc"
FL_CFG=$HOME/.config/flare/settings.txt
BACKUP=("$FL_CFG")
FL_HW=1
setup() {
	mkdir -p "${FL_CFG%/*}"
	printf '%s\n' "fullscreen=1" "resolution_w=1920" "resolution_h=1080" "vsync=0" "hwsurface=$FL_HW" \
		"max_fps=60" "music_volume=96" "sound_volume=128" > "$FL_CFG"
}
CMD=(/usr/games/flare)
variant_swsurf() { FL_HW=0; }
variant_nosound() { CMD+=(--no-audio); }
