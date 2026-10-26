# Battle for Wesnoth 1.18.8 (our ppc64 build, gamedb/games/wesnoth/packages/wesnoth/: wesnoth-1.18 1:1.18.8-1+ps3.N; Debian has
# none, Boost.Context, which only wesnothd uses, is our boost-context; data and campaigns are
# Debian's arch-all packages, wesnoth-1.18-data is 190 MB installed): smoke test, not benchmarked. base starts the tutorial at once (-c Tutorial
# --campaign-difficulty 1 --campaign-skip-story): the map with Konrad's keep and the "Select
# Character" dialog, which waits for a click; then the tutorial's message boxes (mouse, or Return
# for their default button). Variant httt is a normal campaign scenario (Heir to the Throne 2,
# Blackwater Port, difficulty 2: about 80 hexes wide, a dozen units on turn 1); the same dialogs.
# Variant ai is hands-off: a multiplayer game of two AIs on The Freelands (4p map) with the default
# era, --nodelay (no pauses between AI moves, animations still drawn), 20 turns, --rng-seed 1 so a
# rerun plays the same opening (the AI is not fully deterministic over a whole game); it ends in a
# victory/defeat box. Variant menu is the title screen, nosound the tutorial with --nosound, window the tutorial windowed at 1920x1080.
# Settings: ~/.config/wesnoth-1.18/preferences is written (restored after the run): fullscreen at
# 1920x1080, no map, water, idle or standing animations, no haloes, vsync off, scroll_to_action off,
# the frame cap 60 (--max-fps; the default is the display's refresh rate). Key names from 1.18's
# src/preferences/*.cpp; an unknown key is ignored.
# What it exercises: SDL2 (sdl2-compat over our SDL3) with the SDL_Renderer OpenGL backend: every
# image is a texture, the map is drawn into a 1920x1080 render target (FBO) each frame; nothing
# needs GL 3 (SDL's GL renderer is GL 2.1 plus ARB shader objects). Lua for the AI and the
# campaigns' scripts, pango/cairo for text, SDL2_mixer Ogg music.
# Risks: RAM. On the PC (amd64, llvmpipe, these settings) its heap peaks at 241 MiB in the tutorial,
# 296 MiB in Blackwater Port and 368 MiB in the AI game after 12 turns, of which 165-220 MiB is
# llvmpipe's (textures and render targets, VRAM here: 33-68 MiB of textures); the game's own heap,
# 76 / 126 / 148 MiB, stays in RAM: about 130 / 190 / 215 MiB of RSS expected here with the code.
# Biggest consumers: the image surface caches (SDL surfaces kept next to their textures, 16-63
# MiB), the WML config tree (~30), the terrain builder's rules (17). Watch dmesg.log,
# MemAvailable and swap-ins. Also the start-up: the game config (all eras and units) is parsed
# and cached on the first run (in ~/.config/wesnoth-1.18/cache; slow on the PPE), the second
# run reads the cache. The AI's turns are CPU work on the PPE.
# Benchmark ideas (not wired up): variant ai with the HUD window (the AI moving and fighting, the
# same opening each run with --rng-seed 1); and the AI alone without drawing:
#   wesnoth --nogui -m --controller 1:ai --controller 2:ai --scenario multiplayer_The_Freelands
#           --turns 10 --rng-seed 1 --exit-at-end
# timed with /usr/bin/time (wall time and max RSS), which is the AI's CPU cost per turn on the PPE.
COMM=wesnoth-1.18
. "$here/recipes/smoke.inc"
WN_PREFS=$HOME/.config/wesnoth-1.18/preferences
BACKUP=("$WN_PREFS")
# played sessions' fresh defaults (session.sh, freshhome.py): the game's real directories (checked against its XDG/legacy paths)
SESSION_HOME=(.config/wesnoth-1.18 .local/share/wesnoth .wesnoth1.18)
setup() {
	mkdir -p "${WN_PREFS%/*}"
	printf '%s\n' "fullscreen=yes" "maximized=no" "xresolution=1920" "yresolution=1080" "animate_map=no" \
		"animate_water=no" "idle_anim=no" "unit_standing_animations=no" "show_haloes=no" "vsync=no" \
		"scroll_to_action=no" "sound=yes" "music=yes" > "$WN_PREFS"
}
# --no-log-to-file (and WESNOTH_NO_LOG_FILE): without it wesnoth.cpp freopen()s stderr and stdout
# to ~/.config/wesnoth-1.18/logs/ on every start, which closes the pipe client.sh reads (EOF = "the
# game is gone": it stopped the game after 1.7 s, rc=143, empty game.log, empty log file; a start
# from a terminal or a desktop launcher had a tty and kept running). Now the log goes to game.log.
ENV+=(WESNOTH_NO_LOG_FILE=1)
WN=(/usr/games/wesnoth-1.18 --no-log-to-file --fullscreen --max-fps 60)
CMD=("${WN[@]}" -c Tutorial --campaign-difficulty 1 --campaign-skip-story)
variant_httt() { CMD=("${WN[@]}" -c Heir_To_The_Throne --campaign-difficulty 2 --campaign-skip-story --campaign-scenario 02_Blackwater_Port); }
variant_ai() { CMD=("${WN[@]}" -m --controller 1:ai --controller 2:ai --nodelay --turns 20 --rng-seed 1 --scenario multiplayer_The_Freelands --exit-at-end); }
variant_menu() { CMD=("${WN[@]}"); SHOT_STATIC=1; }
variant_nosound() { CMD+=(--nosound); }
variant_window() { WN=(/usr/games/wesnoth-1.18 --no-log-to-file --windowed --resolution 1920x1080 --max-fps 60); CMD=("${WN[@]}" -c Tutorial --campaign-difficulty 1 --campaign-skip-story); }
