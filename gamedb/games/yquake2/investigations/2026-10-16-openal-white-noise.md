+++
id = "openal-white-noise"
games = ["yquake2"]
driver_patches = []
game_patches = ["yquake2/0001"]
sessions = []
batches = ["2026-10-16-yquake2-white-noise", "2026-10-16-yquake2-byteswap"]
+++

# Yamagi Quake II: OpenAL played white noise (2026-10-16)

**Symptom.** The first runs of our ppc64 build (+ps3.229) drew right, but every sound effect was loud
white noise (gamedb:batch/2026-10-16-yquake2-white-noise).

**Cause.** The game loads its 16-bit WAV samples as they are in the file, little-endian, and hands them
to OpenAL as `AL_FORMAT_*16`, which means native byte order: on a big-endian host every sample had its
bytes swapped.

**Fix.** Patch 0001 swaps the 16-bit samples to host byte order at load (+ps3.240,
gamedb:batch/2026-10-16-yquake2-byteswap). The user confirmed the sound by ear. The SDL audio path
(`s_openal 0`, variant `sdlsound`) is a second way to the card, kept as a variant.
