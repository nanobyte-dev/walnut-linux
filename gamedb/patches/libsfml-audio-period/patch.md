+++
component = "libsfml"
patches = ["patches/0001-Audio-playback-period-of-1024-frames-SFML_AUDIO_PERIO.patch"]
aliases = ["libsfml 0001"]
category = "correctness"
switch = "SFML_AUDIO_PERIOD_FRAMES"
default = "1024 (upstream: 64)"
forwarded = "no"
tests = []
batches = []
+++
# SFML's audio playback period of 1024 frames

SFML 3.1 opens its miniaudio playback device with a 64-frame period and three periods: a 4 ms buffer. On the
PS3 (ALSA through dmix, no real-time scheduling for the game's audio thread) the PPE misses that deadline
whenever the game does anything heavy, and every miss is an xrun: Extreme Tux Racer logged 608 in a
2-minute played session ([gamedb:etr/sound-buffer](../../games/etr/investigations/2026-10-24-sound-buffer.md)).
The period is now 1024 frames, about 64 ms of buffer at 48 kHz (a sound starts at most 23 ms late in the
mix). `SFML_AUDIO_PERIOD_FRAMES` (32 to 16384) overrides it, and 64 gives upstream's value back. The game
can't change it itself: SFML doesn't expose the setting.

Not yet measured on the PS3: the next played session of Extreme Tux Racer should show the xrun count.
