+++
id = "music-on-the-spes"
games = ["dsda-doom"]
driver_patches = []
game_patches = []
sessions = ["2026-10-25-session", "2026-10-25-session-0121"]
batches = ["2026-10-17-dsda-doom-sound", "2026-10-17-fluidsynth-spu-dsp", "2026-10-17-fluidsynth-309", "2026-10-17-speq-367-deadline-pool", "2026-10-19-ssdroot2-gameplay-profiles"]
+++

# dsda-doom: the MIDI music chops, and FluidSynth's voices on the SPEs (2026-10-17 onward)

**Symptom.** With the music on, dsda-doom's sound chopped: 1095-1335 ALSA underruns in a 60 s `play` run,
the music audibly breaking up.

**Evidence.** FluidSynth took 48% of dsda-doom's CPU, about 40% of a PPE thread, interpolation alone 31%
([SPU-AUDIO.md, "Results on the PS3"](../../../../docs/SPU-AUDIO.md#results-on-the-ps3)). Earlier
sound-settings tries (gamedb:batch/2026-10-17-dsda-doom-sound, Mesa ps3.225): `tuned` 340 underruns and 46
s of CPU in one run, OPL 1312 in its first.

| Run (`play`, 60 s, GL) | CPU a minute | Underruns | Batch |
|---|---|---|---|
| no music | about 29 s | 18 | gamedb:batch/2026-10-17-fluidsynth-309 |
| stock FluidSynth on the PPE | 44.5-53 s | 1095-1335 | gamedb:batch/2026-10-17-fluidsynth-spu-dsp, gamedb:batch/2026-10-17-speq-367-deadline-pool |
| version 1 on the SPEs (libspeq 0.288) | 31.7 s | 1097 | gamedb:batch/2026-10-17-fluidsynth-spu-dsp |
| version 2 (libfluidsynth3 +ps3.309, libspeq 0.318) | 31.8 s | 588 and 709 (stock 1231 and 1331 the same session) | gamedb:batch/2026-10-17-fluidsynth-309 |
| version 2 on the shared deadline pool (libspeq 0.367) | 26.5 s | 465 (PPE 1095) | gamedb:batch/2026-10-17-speq-367-deadline-pool |

The user listened to version 2: "night and day, spu makes a huge difference"; the glitches that remain
line up with the game's own frame stalls. The gameplay profile of 2026-10-19 still puts the synthesis at
about 30% of the samples. In the played sessions of 2026-10-25 the OpenGL launcher counted 7.5 audible
dropouts a minute (from 10322 underruns), the software launcher 35.9.

**Open.** The cause of what remains, and with it the default (D3: FluidSynth on the SPEs, OPL, or no
music). The same SDL_mixer MIDI path plays OpenTTD's music.
