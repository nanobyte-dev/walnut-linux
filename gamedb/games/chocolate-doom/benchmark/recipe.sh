# Chocolate Doom 3.1.1 (Debian ports ppc64) with Freedoom 2 (the freedoom package, /usr/share/games/doom):
# smoke test, not benchmarked. The software renderer (vanilla 320x200 scaled up by SDL2's renderer
# to a fullscreen 1920x1080 window: SDL2 here is sdl2-compat over our SDL3), OPL music emulation
# by default (no MIDI synthesizer), SDL_mixer for the sound. base starts level 1 of Freedoom 2 at
# once (-warp 1 -skill 3) for the player; variant demo plays the WAD's own DEMO1 and exits at its
# end (Freedoom 0.13's demos desync, they only show the engine); attract is the title screen and
# its demo loop, unattended; nomusic.
# Settings: ~/.local/share/chocolate-doom/chocolate-doom.cfg is written (restored after the run):
# fullscreen at the desktop size (fullscreen_width/height 0), the scaling buffer capped at 1 M
# pixels (default 16 M: an intermediate texture of up to 3200x2000 uploaded every frame; 1 M is a
# 3x buffer, 960x600), no ENDOOM screen, no start-up delay, no disk icon.
# Risks: the SDL2 renderer path with the big-endian window texture (dsda-doom's software frame came
# out green-tinted through SDL's GL renderer until our SDL3 patch: SDL_RENDER_DRIVER=software or
# opengles2 are the variants sdlsw and gles), vsync (vblank_mode=0 here), the OPL music's CPU
# on the PPE, sound underruns (bench.txt's underruns= counts ALSA's).
COMM=chocolate-doom
. "$BENCH_LIB/smoke.inc"
DOOM_WAD=/usr/share/games/doom/freedoom2.wad
CD_CFG=$HOME/.local/share/chocolate-doom/chocolate-doom.cfg
BACKUP=("$CD_CFG")
setup() {
	mkdir -p "${CD_CFG%/*}"
	printf '%s\n' "fullscreen 1" "fullscreen_width 0" "fullscreen_height 0" "max_scaling_buffer_pixels 1000000" \
		"show_endoom 0" "startup_delay 0" "show_diskicon 0" > "$CD_CFG"
}
CMD=(/usr/games/chocolate-doom -iwad "$DOOM_WAD" -fullscreen -warp 1 -skill 3)
variant_demo() { CMD=(/usr/games/chocolate-doom -iwad "$DOOM_WAD" -fullscreen -playdemo demo1); }
variant_attract() { CMD=(/usr/games/chocolate-doom -iwad "$DOOM_WAD" -fullscreen); }
variant_nomusic() { CMD+=(-nomusic); }
variant_sdlsw() { ENV+=(SDL_RENDER_DRIVER=software); }
variant_gles() { ENV+=(SDL_RENDER_DRIVER=opengles2); }
