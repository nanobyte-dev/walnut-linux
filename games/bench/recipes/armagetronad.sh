# Armagetron Advanced 0.2.9.3: the smoke test starts it fullscreen to its menu for a fixed time.
# Later: a recording made on the PS3 (--record f.aarec), played back with --playback --benchmark
# (prints fps), and the spectator soak with AI players (docs/GAME-PLAN.md).
COMM=armagetronad.re	# armagetronad.real, cut to 15 characters
CMD=(/usr/games/armagetronad)	# fullscreen by default; this build refuses --fullscreen
DURATION=45
HUD_SKIP=15
