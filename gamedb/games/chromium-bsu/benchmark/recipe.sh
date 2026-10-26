# Chromium B.S.U. 0.9.16.1 (Debian ports ppc64: chromium-bsu, chromium-bsu-data 1.2 MB): smoke
# test, not benchmarked. Menu only: no command-line way into the game; Enter or a click on "Start
# Game" (arrow keys or the mouse move the ship, Space fires, P pauses, Esc leaves). Its own fps
# counter is on (show_fps), at the top right during play.
# Settings: ~/.chromium-bsu is written (restored after the run): fullscreen 1920x1080, medium graphics
# (gfxLevel 1: of 0 minimal, 1 medium, 2 full), OpenAL (audioType 0), music at its default volume.
# What it exercises: old-style OpenGL (immediate mode, GLC for text, SDL2 window), OpenAL + ALUT
# sound. Risks: immediate-mode vertex counts on the PPE (many sprites), blending (variant noblend:
# -nb), the text backend (libglc0 pulls GLEW MX; a font problem would show as missing text).
COMM=chromium-bsu
. "$BENCH_LIB/smoke.inc"
CB_CFG=$HOME/.chromium-bsu
BACKUP=("$CB_CFG")
# played sessions' fresh defaults (session.sh, freshhome.py): the game's real directories (checked against its XDG/legacy paths)
SESSION_HOME=(.chromium-bsu)
CB_LEVEL=1
setup() {
	printf '%s\n' "full_screen 1" "screenWidth 1920" "screenHigh 1080" "gfxLevel $CB_LEVEL" "show_fps 1" \
		"true_color 1" "audioType 0" > "$CB_CFG"
}
CMD=(/usr/games/chromium-bsu --fullscreen)
variant_full() { CB_LEVEL=2; }
variant_min() { CB_LEVEL=0; }
variant_noblend() { CMD+=(--noblend); }
variant_nosound() { CMD+=(--noaudio); }
