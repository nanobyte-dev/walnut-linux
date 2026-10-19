# Sound on the SPEs

Two audio libraries were looked at for the SPEs. fluidsynth (MIDI synthesis, used by dsda-doom and others) now renders its voice DSP on the SPEs: it takes the game's CPU use from 44.5 to 31.8 s a minute and about halves the audio underruns, though the music still chops. OpenAL Soft's mixer was measured at 3.7 to 4.7% of a game's samples, under the 6% bar we set, so no SPU mixer was built. This page has the fluidsynth design and results and the OpenAL measurement; the mixer design that was never built is in [research/OPENAL-SPU-MIXER.md](research/OPENAL-SPU-MIXER.md). The runtime under both is in [SPU.md](SPU.md).

## OpenAL Soft's cost (M0): no SPU mixer

OpenAL Soft plays the sound of OpenArena (ioquake3 dlopens `libopenal.so.1`), Yamagi Quake II (`s_openal 1`, its default), ClassiCube, Luanti and SuperTuxKart. It does not play QuakeSpasm (SDL2's audio), Sauerbraten (SDL2_mixer) or Extreme Tux Racer (SFML, whose audio module is miniaudio).

**M0, the profile split** (2026-10-17, Mesa ps3.262, our `libopenal1` with its `-dbgsym`, perf on the game's process only): libopenal's own share was

| Game | OpenAL Soft share of the game's samples |
|---|---|
| OpenArena | 4.7% (`renderSamples` 1.3, `Resample_Cubic_C` 1.2, `Mix_C` 0.8, `LoadSamples` 0.5), plus libasound 2.8% |
| ClassiCube | 3.7% (music playing) |
| Yamagi Quake II | its audio thread, 11% of its samples (the SDL audio path, not OpenAL's mixer) |
| Luanti | no sound played in its flight |

No game reached the 6% bar, so voice mixing stays on the PPE. The frame-rate variants (b16, f44, alnull) were not run; they would move at most part of the 3.7 to 4.7%. They are in [backlog/spu.md](backlog/spu.md). The profile is reproducible with `games/bench/oal-perf.sh` and the variants in `games/bench/recipes/openal.inc` (`allog`, `b16`, `f44`, `b16f44`, `p1024`, `alnull`, `nosound`); the bar was "voice mixing (`Voice::mix`, inclusive) at least 6% of a game's samples in the configuration we would ship".

Why step 0's sound variants (OpenArena, 2026-10-16) told nothing: those game runs never reached the screen ([SPU.md](SPU.md), "Measured: step 0 and the harness"), the profile left the mixer thread as `[unknown]` because libopenal was never mapped, and variant B (`sample-type=int16`) also switched on the output limiter and 16-bit dithering, which likely explains why it cost more. B22 (`frequency=22050`) probably still ran at 48 kHz, since ALSA's dmix is fixed there and OpenAL disables ALSA's resampler.

### The mixer path in 1.25.2, as far as the measurement needs it

Debian's `openal-soft` 1:1.25.2-1 is what the PS3 runs; our package `openal-soft/` rebuilds it unpatched as `1:1.25.2-1+ps3.N` so `libopenal1-dbgsym` matches the running code. Its build confirms there is no SIMD mixer on ppc64: CMake finds no `xmmintrin.h` (POWER's SSE translation headers need POWER8) and no NEON, "CPU extensions: Default". The PPE runs the scalar `Mix_C` and `Resample_Cubic_C`, and the default resampler is the 4-tap cubic.

The ALSA backend's thread `alsoft-mixer` renders whole periods (512 frames) into the mmap area: process parameter updates, run `Voice::mix` for every playing voice, apply the stereo ambisonic post-process, write floats. Nothing sets ALSA options, so OpenAL opens `default`, which on the PS3 is `plug` to `softvol` to `dmix:CARD=PS3,FORMAT=S16` at 48 kHz. `plug` converts float to S16 inside libasound on the mixer thread, which is the "ALSA conversion" in the profile. HRTF is not on (the ALSA backend never sets the headphone flag, and nobody sets `stereo-mode=headphones`). The perf split to read is `Voice::mix` inclusive for the mixing, `DeviceBase::renderSamples` self plus `CalcPanningAndFilters` and `ProcessVoiceChanges` for parameter updates (our `-O2` build inlines the rest), and libasound for the conversion. The hook point, per-voice work and the rest of the walk-through are in [research/OPENAL-SPU-MIXER.md](research/OPENAL-SPU-MIXER.md).

What OpenArena gives OpenAL: 518 mono WAV effects (409 at 44100 Hz, 85 at 22050), the rest Ogg (7 stereo 32 kHz music tracks, 14 small voices), every voice resampled to 48 kHz. Vorbis decoding on an SPU would not pay: ioquake3 decodes Ogg itself through libvorbisfile in 4 KB buffers, and the 1.9 s of load and about 7 MB seen in step 0 are WAV effects read from deflated pk3s.

OpenArena also spends 13.6% in zlib during its timedemo (pk3 reads while playing). Our ioquake3 patches 0012 and 0016 (no crc32 of pk3 entries, libdeflate for whole entries) address that; they are built, not run.

## fluidsynth on the SPEs

Debian's `fluidsynth` 2.6.1+dfsg-1 is our package `fluidsynth/` (`2.6.1+dfsg-1+ps3.N`), with one patch, `fluidsynth/patches/0001-rvoice-voice-DSP-through-libspeq-on-the-PS3-s-SPEs.patch`, and libspeq's `speq_synth_*_v2` calls (`ps3-spu/lib/synth.c`, `lib/synth-kern.h`, `spu/synth-op.c`; the op lives in `deadline.spu`).

The reason: dsda-doom playing its demo at normal speed used 53 s of CPU in 60 s with fluidsynth music against 29 s without, and ALSA underruns went from 18 to 1280 a minute. A perf profile of dsda-doom (2026-10-17, our `+ps3.270` with dbgsym; `--proc-map-timeout=20000`) put libfluidsynth at 48.3% of the process's samples and its SDL audio thread at 55%. Per function: `fluid_rvoice_dsp_interpolate` 31.1%, `fluid_iir_filter_apply` 7.9%, `fluid_rvoice_buffers_mix` 4.2%, `fluid_rvoice_write` (the control part) 2.2%. So about 43 of those 48 points are per-voice DSP: block-sized and SIMD-friendly.

### The render path in 2.6.1

dsda-doom renders between MIDI events from SDL's audio callback, at 44.1 kHz, gain 0.5, polyphony 256, reverb and chorus off, with the SoundFont `/usr/share/sounds/sf3/default-GM.sf3`. `fluid_synth_write_float` goes through `fluid_synth_render_blocks` in blocks of `FLUID_BUFSIZE` = 64 samples (1.45 ms), and for each active voice `fluid_rvoice_write` renders a block and `fluid_rvoice_buffers_mix` adds it to the voice's buffers (left, right, reverb send, chorus send).

`fluid_rvoice_write` has two halves. Control, which stays on the PPE: sample sanity, the note-off check, the envelopes and two LFOs, the amplitude, the pitch-to-phase step, the filters' coefficients. It is table lookups and a few dozen flops per block. DSP, which is the cost: interpolation (fourth order by default) around a 32.32 fixed-point phase, with special points at the start, the end and across a loop wrap; the SoundFont's resonant low-pass (a biquad that also applies the volume envelope's gain ramp); and the mix. Debian builds fluidsynth with doubles, and compiles the filter with `-ffast-math`.

What stays on the PPE besides control: event handling, voice allocation and stealing, SoundFont lookups and modulators, and reverb and chorus (serial state of tens of KB; off in dsda-doom). fluidsynth's own multi-threaded render path (`synth.cpu-cores` above 1) hands whole voices to extra threads, control part included. That would mean porting the control code to the SPU and keeping it in step, so the hook instead keeps every line of control code fluidsynth's own, and the mixer keeps the stock path whenever cpu-cores is above 1.

### Design, version 2 (the current one)

**The split.** `fluid_rvoice_write` becomes `fluid_rvoice_write_ctl` plus `fluid_rvoice_write_dsp`; stock behaviour is the two together. A new render loop handles every block of one `fluid_synth_write_*` request (up to 16 blocks) before any DSP runs. This works because fluidsynth's control part needs nothing the DSP computes from samples: the phase, `has_looped` and the sample count after a block follow from the interpolation's loop structure (integer code), and the filter coefficients, cutoff, Q and gain from the smoothing counters. Only the filters' histories depend on the samples, and control never reads them. `speq_synth_add_v2` runs that sample-free part on the PPE (`sk_control`, `sk_filter_end` in `synth-kern.h`) and hands each voice's next state back at once. Then `speq_synth_render_v2` renders the whole round in one round trip. The PPE renders any voices libspeq refused (sinc interpolation, 24-bit samples, windows too wide) meanwhile.

**The job.** Entries sit voice-major, so a job renders whole voices block after block, carrying the filter histories; a job's 16 KB output holds up to 512 entries of 256 bytes per round, and jobs are cut so that both pool workers get work. The SPU writes only its own output lines and the descriptors it was given, never fluidsynth's memory. The PPE adds the lines to fluidsynth's buffers (dsda-doom has 4, up to 16 are supported).

**Samples by DMA.** SoundFont samples are int16 in main memory (SF3 is decoded at load). libspeq computes from the phase and increment the index range a block can read before the first loop wrap (window 0) and after it (window 1), at most 4096 samples each, which covers a pitch up to about 30 to 60 times the sample's rate; wider ones stay on the PPE. The special interpolation points (start, end, loop start, loop start+1, loop end-1) are read on the PPE. A sweeping filter gets the slice of the 12001-entry sin/cos table its cutoff crosses (at most 1024 entries). Each window's pages are touched on the PPE before submit, so a swapped-out SoundFont page returns there and not as a fault on the worker's thread. The SPU fetches one voice's windows while the previous voice renders.

**Single precision, vectors.** `lib/synth-kern.h` is one C file for both sides, in float with 4-wide vectors (SPU intrinsics in the hot loops, GCC's generic vectors elsewhere, AltiVec on the PPE path). The window's int16 samples are converted once into a float window with fluidsynth's end points written in; every cubic output is then one unaligned 4-float load times a coefficient row. The filter recurrence takes the older history first (one multiply-add on the dependency chain), the gain is applied afterwards in vectors, and 64-bit divisions and multiplications (libgcc calls on the SPU) are gone from the per-segment code.

**Fallback and the wait.** The SPU never writes a round's entries or input histories, so a late or failed round is redone on the PPE from them with the same DSP code, and render always delivers. The audio thread waits on the done word without spinning (`SPEQ_WAIT_NOSPIN`) for at most 2 ms plus 5 us per voice block (`SPEQ_SYNTH_TIMEOUT_US`). Three failed rounds in a row put the SPEs aside for 5 s; a pool whose workers all broke is gone for the process. Timeouts that come from queueing count as late, not as a failure of the SPU path.

**Where it runs.** The synth shares the deadline pool of 2 SPEs with skinning (design: [ps3-spu/doc/pool.md](../ps3-spu/doc/pool.md); the queue sharing is in [SPU.md](SPU.md)). `SPEQ_SYNTH_SPES` caps how many pool workers one round is split across. `SPEQ_DEBUG=1` prints blocks and voices on the SPEs, jobs, mean and max wait, refusals, timeouts, errors and back-offs.

**Accuracy.** Version 2 is no longer bit-exact to Debian's double-precision path. `tests/fs-kern.c` (200000 random voice blocks against fluidsynth's own functions): control results identical, samples at 96 dB SNR over all of them (random states include high-pass custom filters at low cutoffs, where single precision cancels). Whole renders of a generated GM piece: 129 to 131 dB SNR against unpatched fluidsynth, 127 to 128 with reverb and chorus, and the SPU path against the PPE path 132 dB (the jobs sum in another order). The SPU rounds float results toward zero and the host tests to nearest, so the PS3 will be a few dB below these; still far under 16-bit output.

### Host tests

- `make -C ps3-spu test`: queue-test (timeouts, poll and no-spin waits, the SPE budget, bulk queues), dlopen-check (the exports, `SPEQ_SYNTH=ppe|off` without spufs), and synth-test: the SPU path (the op on host threads, `SPEQ_FAKE=1`, every sample and table read checked against the DMA plan) against libspeq's PPE path on random voices (all three interpolations, loops down to 2 samples, wraps, sample ends, filter sweeps, ramps, unmapped buffers).
- `make -C ps3-spu test-fluidsynth` (`tests/fluidsynth-compare.sh`): fluidsynth with the hook and unpatched fluidsynth built for the host. A generated GM piece (`tests/make-midi.py`: 16 channels, about 134 voices a block on average, programs, bends, pans, sustain, CC74) is rendered with FluidR3_GM.sf2 through `tests/fs-render.c`: `SPEQ_SYNTH=off` identical to unpatched, `ppe` and `spu` at the SNR above, sinc identical (left to fluidsynth).
- Not covered on the host: the SPU program on hardware (its real speed and DMA); that is what the PS3 runs below are for.

### Results on the PS3

dsda-doom `play` (60 s, the demo at normal speed, GL renderer):

| Run | CPU a minute | ALSA underruns | Note |
|---|---|---|---|
| no music (`playnomusic`) | about 29 s | 18 | the floor |
| stock fluidsynth (`SPEQ_SYNTH=off`) | 44.5 to 53 s | 1095 to 1335 | |
| version 1 on the SPEs (double precision, one block per job, own queue; libspeq 0.288) | 31.7 s | 1097 | 731 us mean wait of a 1.45 ms block, 9.2 ms max, 55 timeouts |
| version 2 (single precision, rounds, 2 SPEs; libfluidsynth3 +ps3.309, libspeq 0.318) | 31.8 s | 588 and 709 (stock 1231 and 1331 in the same session) | the user, listening: "night and day, spu makes a huge difference" |
| version 2 on the shared deadline pool (libspeq 0.367) | | 465 (PPE 1095) | 4530 rounds on the SPEs, 0 timeouts, 0.84 ms mean wait |

The sources are fps.csv rows "fluidsynth voice DSP on the SPEs (libspeq 0.288; Mesa ps3.282)", "fluidsynth ps3.309 (SPU synth v2) + libspeq 0.318 + Mesa ps3.299" and "libspeq 0.367 (shared deadline pool), fluidsynth ps3.309, dsda-doom ps3.361", and the results commit 388b461 in [TESTS.md](TESTS.md). Version 1 showed that the CPU can move off and the chopping stay: its scalar double-precision kernel took about 12 us per voice block (estimated 3 to 4), and the audio thread waited for every block in turn. Version 2 replaced that.

**dsda-doom still chops.** 465 underruns a minute on the pool, against 18 with no music. What glitches remain lines up with the game's own frame stalls. The cause is open: the pool's wait, SDL's audio buffer, or something outside the DSP. It is PLAN 2.8, and the decision on dsda-doom's music default (fluidsynth, OPL or off, PLAN D3) waits on it. OpenTTD's music (3893 underruns in its run) shares the SDL_mixer MIDI path. A sound check by the user's ear is scheduled in [TESTS.md](TESTS.md). The earlier sound-settings experiment on the same game (row "dsda-doom sound settings against the underruns", Mesa ps3.225): tuned settings (OPL, 48 kHz, bigger buffer, 60 fps cap) reached 340 underruns and 46 s of CPU in one run, the OPL variant 1312 in its first run; see the CSV for the rest.

## History

- 2026-10-16: OpenAL mixer designed (never built). 2026-10-17: M0 measured the mixer under the bar, so the design was parked. The same day fluidsynth's version 1 was built and run (CPU moved, chopping stayed), then version 2 (underruns halved), then the shared deadline pool (libspeq 0.367, 2026-10-17 late).
- Version 1 gave fluidsynth its own one-worker deadline queue, with DXT on a bulk queue of 4 and OpenAL's mixer imagined as a second one-worker queue, to keep every process at 6 contexts. The deadline pool replaced that arrangement so that every latency-bound service shares the same 2 SPEs.

Older versions and the dated history: [archive/SPU-AUDIO.md](archive/SPU-AUDIO.md).
