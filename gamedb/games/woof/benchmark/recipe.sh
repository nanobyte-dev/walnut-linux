# Woof 16.0.0 (Debian ports ppc64: woof-doom, SDL3, OpenAL, fluidsynth for MIDI) with Freedoom 2
# (the freedoom package, /usr/share/games/doom): smoke test, not benchmarked. A software renderer
# whose frame goes to the screen through SDL3's renderer (our libsdl3 patch for the big-endian
# window texture matters here), 400 lines high scaled to the 1920x1080 fullscreen window. base
# starts level 1 at once (-warp 1 -skill 3) for the player; variant demo plays the WAD's DEMO1 (the
# game leaves a message box at its end, UNVERIFIED), attract is the title screen and its demo loop,
# unattended; nomusic is -nomusic (the MIDI music goes through fluidsynth, in software on the PPE:
# dsda-doom's choppy sound came from it; our libfluidsynth3 runs its voices on the SPEs through libspeq).
# Settings: ~/.local/share/woof/woof.cfg is written (restored after the run): fullscreen, no vsync,
# the 35 Hz frame cap kept (uncapped 0), no fps limit, the render height 400 (max_video_height 600),
# the fps counter. The key names are read from the binary's strings; Woof keeps defaults for keys it
# doesn't know. Variant uncapped: uncapped 1 (interpolated frames).
# Risks: SDL3's renderer path and colours (variants sdlsw: SDL_RENDER_DRIVER=software, gles:
# opengles2), OpenAL + fluidsynth CPU and underruns (bench.txt's underruns=), Woof 16 needs SDL3 >= 3.4.
COMM=woof
. "$BENCH_LIB/smoke.inc"
DOOM_WAD=/usr/share/games/doom/freedoom2.wad
WOOF_CFG=$HOME/.local/share/woof/woof.cfg
WOOF_SET=()	# extra "key value" lines (variants)
BACKUP=("$WOOF_CFG")
setup() {
	mkdir -p "${WOOF_CFG%/*}"
	printf '%s\n' "fullscreen 1" "exclusive_fullscreen 0" "use_vsync 0" "uncapped 0" "fpslimit 0" \
		"integer_scaling 0" "current_video_height 400" "max_video_height 600" "fps_counter 1" \
		"${WOOF_SET[@]}" > "$WOOF_CFG"
}
CMD=(/usr/games/woof -iwad "$DOOM_WAD" -fullscreen -warp 1 -skill 3)
variant_demo() { CMD=(/usr/games/woof -iwad "$DOOM_WAD" -fullscreen -playdemo demo1); }
variant_attract() { CMD=(/usr/games/woof -iwad "$DOOM_WAD" -fullscreen); }
variant_nomusic() { CMD+=(-nomusic); }
variant_uncapped() { WOOF_SET=("uncapped 1"); }
variant_sdlsw() { ENV+=(SDL_RENDER_DRIVER=software); }
variant_gles() { ENV+=(SDL_RENDER_DRIVER=opengles2); }
