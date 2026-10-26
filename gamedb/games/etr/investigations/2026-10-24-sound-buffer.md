+++
id = "sound-buffer"
games = ["etr"]
driver_patches = ["libsfml/0001"]
game_patches = []
sessions = ["2026-10-24-session"]
batches = []
+++

# Extreme Tux Racer: a 4 ms sound buffer (2026-10-24)

**Symptom.** The played session of 2026-10-24 logged 608 xruns, 7.5 audible-dropout marks a minute, which
the player did not hear.

**Cause.** ETR plays through SFML 3.1 (miniaudio), which opens the ALSA device with a 64-frame period and 3
periods: a 4 ms buffer, which the PPE misses whenever a frame runs long. ETR cannot change it: SFML does not
expose the setting.

**Fix.** Our `libsfml` (packages/patched/audio/libsfml/, 0001) opens it with a 1024-frame period: about 64
ms of buffer at 48 kHz, 24 KB, a sound effect or the music starting at most 23 ms late in the mix.
`SFML_AUDIO_PERIOD_FRAMES=<32..16384>` overrides it (64 is upstream's). It serves every SFML game. Not yet
tried on the PS3: the next played session should show the xrun count.
