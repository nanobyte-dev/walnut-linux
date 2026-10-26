+++
id = "oom-the-memory-is-the-sound"
games = ["redeclipse"]
driver_patches = []
game_patches = []
sessions = []
batches = []
+++

# Red Eclipse: OOM-killed loading torus, and the memory is the sound (2026-10-18)

**Symptom.** The first smoke run of Debian's Red Eclipse 1.6.0 was OOM-killed while loading torus. On the
PS3 the process grew to about 200 MB during that load (55 MB resident, about 150 MB in zram).

**Evidence.** Debian's amd64 build under Xvfb and llvmpipe on the PC, after loading torus: 534 MB RSS at
44.1 kHz stereo, 381 MB with no sample loaded, 397 MB with `soundmono 1` and `soundfreq 11025`, 375 MB
with the other lean settings, 326 MB with `maxtexsize 256` and `texreduce 3`. The menu alone: 262 MB, 231
without samples.

**Cause.** `addsound` loads each sample at registration and SDL_mixer decodes it whole into PCM in the
device's format: about 190 samples at the start, and at the map load every `mapsound` of the map's .cfg
(torus: 74 ambience files, 24 MB of .ogg). Volumes don't stop the decoding, and
`SDL_AUDIODRIVER=none` makes the game quit.

**Fix.** In the recipe, not the game: `soundmono` and `soundfreq` written to `init.cfg` (they count only
before the audio device opens), the lean texture and effect settings, one bot. With them it runs on
torus with no OOM. The real fix, samples loaded when first played and cube2's memory patches carried
over, is open.
