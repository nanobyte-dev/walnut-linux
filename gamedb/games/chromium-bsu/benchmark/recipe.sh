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
CB_LEVEL=1
CB_FRESH=0
setup() {
	# fresh: no configuration at all, the package's defaults (ours: fullscreen at the screen's size)
	[ "$CB_FRESH" = 1 ] && { rm -f "$CB_CFG"; return 0; }
	printf '%s\n' "full_screen 1" "screenWidth 1920" "screenHeight 1080" "gfxLevel $CB_LEVEL" "show_fps 1" \
		"true_color 1" "audioType 0" > "$CB_CFG"
}
CMD=(/usr/games/chromium-bsu --fullscreen)
variant_full() { CB_LEVEL=2; }
variant_min() { CB_LEVEL=0; }
variant_noblend() { CMD+=(--noblend); }
variant_nosound() { CMD+=(--noaudio); }
# Into play without the user: Return on "Start Game" through XTEST 8 s after the window, then a few
# shots (Space) and moves; the screenshots count from there (SHOT_FROM=mark)
CB_KEYS=1
during() { [ "$CB_KEYS" = 1 ] || return 0; smoke_xkeys Chromium 8 Return +4 space space Left Left space Right Right space; shot_mark; }
SHOT_FROM=mark
variant_fresh() { CB_FRESH=1; CMD=(/usr/games/chromium-bsu); }
variant_menu() { CB_KEYS=0; SHOT_STATIC=1; }
