# Crispy Doom 7.1 (the snapshot; live debian-ports has 7.2) with Freedoom 2 (the freedoom package,
# /usr/share/games/doom): smoke test, not benchmarked. Chocolate Doom's sibling with extras, the same
# engine path: the software renderer scaled up by SDL2's renderer (sdl2-compat over our SDL3) to a
# fullscreen 1920x1080 window, OPL music emulation by default, SDL_mixer. base starts level 1 of
# Freedoom 2 at once (-warp 1 -skill 3) for the player; variant demo plays the WAD's own DEMO1 and
# exits at its end (Freedoom 0.13's demos desync, they only show the engine); attract is the title
# screen and its demo loop, unattended; nomusic.
# Settings: ~/.local/share/crispy-doom/crispy-doom.cfg is written (restored after the run):
# fullscreen at the desktop size (fullscreen_width/height 0), the scaling buffer capped at 1 M
# pixels (default 16 M: an intermediate texture of up to 3200x2000 uploaded every frame), no
# ENDOOM screen, no start-up delay, no disk icon, and Crispy's own: vanilla resolution
# (crispy_hires 0; its default 640x400 quadruples the software renderer's work), 8-bit colour
# (crispy_truecolor 0), the 35 Hz frame cap kept (crispy_uncapped 0), no vsync of its own, no
# widescreen. Variants: hires (640x400), uncapped (Crispy's interpolated frame rate).
# Risks: the SDL2 renderer path with the big-endian window texture (dsda-doom's software frame came
# out green-tinted through SDL's GL renderer until our SDL3 patch: SDL_RENDER_DRIVER=software or
# opengles2 are the variants sdlsw and gles), the OPL music's CPU on the PPE, sound underruns
# (bench.txt's underruns= counts ALSA's).
COMM=crispy-doom
. "$BENCH_LIB/smoke.inc"
DOOM_WAD=/usr/share/games/doom/freedoom2.wad
CD_CFG=$HOME/.local/share/crispy-doom/crispy-doom.cfg
CD_SET=()	# extra "key value" lines (variants)
BACKUP=("$CD_CFG")
setup() {
	mkdir -p "${CD_CFG%/*}"
	printf '%s\n' "fullscreen 1" "fullscreen_width 0" "fullscreen_height 0" "max_scaling_buffer_pixels 1000000" \
		"show_endoom 0" "startup_delay 0" "show_diskicon 0" "crispy_hires 0" "crispy_truecolor 0" \
		"crispy_uncapped 0" "crispy_vsync 0" "crispy_widescreen 0" "crispy_fpslimit 0" "${CD_SET[@]}" > "$CD_CFG"
}
CMD=(/usr/games/crispy-doom -iwad "$DOOM_WAD" -fullscreen -warp 1 -skill 3)
variant_demo() { CMD=(/usr/games/crispy-doom -iwad "$DOOM_WAD" -fullscreen -playdemo demo1); }
variant_attract() { CMD=(/usr/games/crispy-doom -iwad "$DOOM_WAD" -fullscreen); }
variant_nomusic() { CMD+=(-nomusic); }
variant_hires() { CD_SET=("crispy_hires 1"); }
variant_uncapped() { CD_SET=("crispy_uncapped 1"); }
variant_sdlsw() { ENV+=(SDL_RENDER_DRIVER=software); }
variant_gles() { ENV+=(SDL_RENDER_DRIVER=opengles2); }
