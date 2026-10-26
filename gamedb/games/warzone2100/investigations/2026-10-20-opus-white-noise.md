+++
id = "opus-white-noise"
games = ["warzone2100"]
driver_patches = []
game_patches = ["warzone2100/0008"]
sessions = []
batches = []
+++

# Warzone 2100: the music was white noise (2026-10-20)

**Symptom.** The second PS3 smoke run (ps3.617, stopped at 110 s) played white noise for the music.

**Evidence.** Checked under qemu with OpenAL Soft's wave writer at the main menu: a zero-crossing rate of
0.44 with ps3.617 (noise). The decode itself was right on big-endian: track19.opus through the game's
opusfile, built with its flags, matches amd64 within 8 LSB. The Vorbis effects already asked `ov_read` for
big-endian.

**Cause.** The Opus decoder (the soundtrack, warzone2100-music) wrote its samples little-endian, but
OpenAL's `AL_FORMAT_*16` expects native order.

**Fix.** Patch 0008 copies them as they are (ps3.619): a zero-crossing rate of 0.01 (the menu music). In the
third run the user heard music and effects.
