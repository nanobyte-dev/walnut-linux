# Dink Smallwood on GNU FreeDink 109.6 (Debian ports ppc64: freedink, freedink-engine, freedink-data
# 1.08.20190120; +95 MB installed): smoke test, not benchmarked. SDL2; the 640x480 8-bit game screen
# is drawn in software and shown through OpenGL (FreeDink's IOGfxDisplayGL, the default; -S is SDL2's
# software renderer instead: variant sw), stretched to the 1920x1080 fullscreen window (no letterbox).
# base: the title screen and its menu (Start, Continue, Quit; a hand pointer the mouse moves), no keys.
# No unattended way into a game yet: on the PC (tools/bench-ref.sh, 2026-11-14) neither an XTEST click
# on Start (the pointer jumped elsewhere: the game reads relative motion) nor arrow keys and Ctrl moved
# the title's pointer onto Start; a new game needs the player's mouse (or a pad). FreeDink's command
# line: -w windowed, -s no sound, -t truecolour (variant tc), -i no dinksmallwood.ini written (always here).
# Settings: none written (FreeDink keeps its saves in ~/.dink; the title screen writes nothing).
# Risks: the 8-bit palette path on big-endian (SDL2's texture formats), the MIDI music (SDL2_mixer
# through timidity's freepats on the PPE), the sound effects' WAV byte order.
COMM=freedink
. "$BENCH_LIB/smoke.inc"
REF_PKGS="freedink freedink-data"	# the PC reference (tools/bench-ref.sh): Debian's freedink
CMD=(/usr/games/freedink -i)
variant_tc() { CMD+=(-t); }
variant_window() { CMD+=(-w); }
variant_sw() { CMD+=(-S); }
