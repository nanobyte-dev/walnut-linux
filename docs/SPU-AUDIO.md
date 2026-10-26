# Sound on the SPEs

Two audio libraries were looked at for the SPEs. fluidsynth (MIDI synthesis, used by [dsda-doom](../gamedb/games/dsda-doom/) and others) now renders its voice DSP on the SPEs: it takes the game's CPU use from 44.5 to 31.8 s a minute and about halves the audio underruns, though the music still chops. OpenAL Soft's mixer was measured at 3.7 to 4.7% of a game's samples, under the 6% bar we set, so no SPU mixer was built. What stays on the PPE was then made cheaper there: OpenAL's mixer and libvorbis's `ov_read` without the integer/float round trips through memory the PPE pays about 40 cycles for, OpenAL's output as 16-bit so ALSA no longer converts it, and fluidsynth's reverb without a divide per sample plus a switch for its effects (all built, none run on the PS3 yet). This page has the fluidsynth design and results, the OpenAL measurement and the PPE-side changes; the mixer design that was never built is in [research/OPENAL-SPU-MIXER.md](research/OPENAL-SPU-MIXER.md). The runtime under both is in [SPU.md](SPU.md).

## OpenAL Soft's cost (M0): no SPU mixer

OpenAL Soft plays the sound of OpenArena (ioquake3 dlopens `libopenal.so.1`), Yamagi Quake II (`s_openal 1`, its default), ClassiCube, Luanti and [SuperTuxKart](../gamedb/games/supertuxkart/). It does not play [QuakeSpasm](../gamedb/games/quakespasm/) (SDL2's audio), [BananaBread](../gamedb/games/bananabread/) (SDL2_mixer) or [Extreme Tux Racer](../gamedb/games/etr/) (SFML, whose audio module is miniaudio).

**M0, the profile split** (2026-10-17, gamedb:batch/2026-10-17-mesa-262-perf, Mesa ps3.262, our `libopenal1` with its `-dbgsym`, perf on the game's process only): libopenal's own share was

| Game | OpenAL Soft share of the game's samples |
|---|---|
| [OpenArena](../gamedb/games/openarena/) | 4.7% (`renderSamples` 1.3, `Resample_Cubic_C` 1.2, `Mix_C` 0.8, `LoadSamples` 0.5), plus libasound 2.8% |
| [ClassiCube](../gamedb/games/classicube/) | 3.7% (music playing) |
| [Yamagi Quake II](../gamedb/games/yquake2/) | its audio thread, 11% of its samples (the SDL audio path, not OpenAL's mixer) |
| [Luanti](../gamedb/games/luanti/) | no sound played in its flight |

No game reached the 6% bar, so voice mixing stays on the PPE. The frame-rate variants (b16, f44, alnull) were not run; they would move at most part of the 3.7 to 4.7%. They are in [backlog/spu.md](backlog/spu.md). The profile is reproducible with `bench/analysis/oal-perf.sh` and the variants in `bench/lib/openal.inc` (`allog`, `b16`, `f44`, `b16f44`, `p1024`, `alnull`, `nosound`); the bar was "voice mixing (`Voice::mix`, inclusive) at least 6% of a game's samples in the configuration we would ship".

Why step 0's sound variants (OpenArena, 2026-10-16) told nothing: those game runs never reached the screen ([SPU.md](SPU.md), "Measured: step 0 and the harness"), the profile left the mixer thread as `[unknown]` because libopenal was never mapped, and variant B (`sample-type=int16`) also switched on the output limiter and 16-bit dithering, which likely explains why it cost more. B22 (`frequency=22050`) probably still ran at 48 kHz, since ALSA's dmix is fixed there and OpenAL disables ALSA's resampler.

### The mixer path in 1.25.2, as far as the measurement needs it

Debian's `openal-soft` 1:1.25.2-1 is what the PS3 ran for M0; our package `packages/patched/audio/openal-soft/` rebuilt it unpatched as `1:1.25.2-1+ps3.N` so `libopenal1-dbgsym` matched the running code (now it carries the PPE patch below). Its build confirms there is no SIMD mixer on ppc64: CMake finds no `xmmintrin.h` (POWER's SSE translation headers need POWER8) and no NEON, "CPU extensions: Default". The PPE runs the scalar `Mix_C` and `Resample_Cubic_C`, and the default resampler is the 4-tap cubic.

The ALSA backend's thread `alsoft-mixer` renders whole periods (512 frames) into the mmap area: process parameter updates, run `Voice::mix` for every playing voice, apply the stereo ambisonic post-process, write floats. Nothing sets ALSA options, so OpenAL opens `default`, which on the PS3 is `plug` to `softvol` to `dmix:CARD=PS3,FORMAT=S16` at 48 kHz. `plug` converts float to S16 inside libasound on the mixer thread, which is the "ALSA conversion" in the profile. HRTF is not on (the ALSA backend never sets the headphone flag, and nobody sets `stereo-mode=headphones`). The perf split to read is `Voice::mix` inclusive for the mixing, `DeviceBase::renderSamples` self plus `CalcPanningAndFilters` and `ProcessVoiceChanges` for parameter updates (our `-O2` build inlines the rest), and libasound for the conversion. The hook point, per-voice work and the rest of the walk-through are in [research/OPENAL-SPU-MIXER.md](research/OPENAL-SPU-MIXER.md).

What [OpenArena](../gamedb/games/openarena/) gives OpenAL: 518 mono WAV effects (409 at 44100 Hz, 85 at 22050), the rest Ogg (7 stereo 32 kHz music tracks, 14 small voices), every voice resampled to 48 kHz. Vorbis decoding on an SPU would not pay: [ioquake3](../gamedb/engines/ioquake3/) decodes Ogg itself through libvorbisfile in 4 KB buffers, and the 1.9 s of load and about 7 MB seen in step 0 are WAV effects read from deflated pk3s.

OpenArena also spends 13.6% in zlib during its timedemo (pk3 reads while playing). Our ioquake3 patches 0012 and 0016 (no crc32 of pk3 entries, libdeflate for whole entries) address that; they are built, not run.

## OpenAL Soft on the PPE: conversions and output format

The gameplay profiles on the SSD root ([HOTPATHS.md](HOTPATHS.md#gameplay-profiles-on-the-ssd-root-batch-ssdroot2-p1b))
put audio at 11.9% of a game's CPU on average; OpenAL Soft is most of it in [OpenArena](../gamedb/games/openarena/) (12.4%) and
[ClassiCube](../gamedb/games/classicube/) (with ALSA 9.9%, of which ALSA's float conversion 1.9%). What costs the PPE there is less
the arithmetic than its conversions.

**Why conversions.** The PPE has no moves between its integer, floating-point and vector registers:
an `int` to `float` (or back) is a store and a load, and the load waits for the store to drain, a
load-hit-store of about 40 cycles. OpenAL's C mixer (there is no SIMD mixer for ppc64) does that
for every output sample of every voice twice, the resampler's 16-bit fraction to a float and each
16-bit source sample to a float, and once per output sample when it writes 16 bits. ALSA's `plug`
does it once per sample when it converts OpenAL's float output to the S16 the PS3's device takes.

**Measured on the PC** (2026-10-19, qemu-ppc64 with the `ppecost` plugin: guest instructions,
load-hit-stores and the crude estimate `insns + 40 x LHS`, method in
[PROFILING.md](PROFILING.md#load-time-decoders-and-linking); a loopback-device harness, 16 looping
mono 44.1 kHz voices around the listener into 48 kHz stereo in 512-frame periods, OpenAL 1.25.2 built
`-O2 -mcpu=cell` with and without our patch; and alsa-lib's `plug` in front of an S16 null device):

| Per 5 s of audio | stock: insns / LHS / est | patched: insns / LHS / est |
|---|---|---|
| OpenAL's mixer, float output (Resample_Cubic_C, LoadSamples, Mix_C, renderSamples, Voice::mix) | 512 M / 11.5 M / 972 M | 426 M / 0.2 M / 436 M |
| of it the resampler | 282 M / 5.75 M / 512 M | 282 M / 0 / 282 M |
| of it loading 16-bit samples | 106 M / 5.62 M / 331 M | 20 M / 0.14 M / 26 M |
| OpenAL writing 16-bit output instead of float | +7 M / +0.48 M / +26 M | about +2 M est |
| ALSA's plug converting float to S16 (480000 samples) | 15.8 M / 0.51 M / 36 M | (not needed with 16-bit output) |

- **The patch** (`packages/patched/audio/openal-soft/patches/0002`, with `-mcpu=cell -mtune=cell` from the packaging
  patch): the resampler's fraction comes from two 256-entry tables (the same float, exactly), 16-bit
  samples load eight at a time in AltiVec (`vec_ctf(x, 15)` is x/32768 exactly), mono and interleaved
  stereo buffers, and 16-bit stereo output packs in AltiVec (`vec_cts`, `vec_packs`, the scalar
  clamp and truncation). The output is bit-identical to the unpatched build (hashes of whole
  renders: mono and stereo sources, 22.05 and 44.1 kHz, float and 16-bit, spline and linear), and
  the packaged `libopenal1 1:1.25.2-1+ps3.549` renders the same bytes as the `+ps3.273` the PS3 runs
  now. The mixer's estimate falls 55%, its instructions 17%.
- **16-bit output** (`ps3-system`'s `/etc/xdg/alsoft.conf`: `sample-type = int16`,
  `output-limiter = false`, `dither = false`): ALSA's plug then passes S16 straight through. With
  the patched OpenAL that removes the conversion (about 76 estimated cycles a sample in plug against
  a few in OpenAL's AltiVec write); with a stock OpenAL it still saves about a third (OpenAL's own
  scalar write, about 55). The limiter and dither, which OpenAL turns on for integer output, are off
  because its float output had neither and plug clamps the same way: the sound is unchanged to
  within the last bit. Step 0's `sample-type=int16` variant cost more because it had both on.
  The file comes before `~/.alsoftrc` and `$ALSOFT_CONF`, so either overrides it.
- **The resampler.** `resampler = linear` takes the two-tap interpolation instead of the 4-tap
  spline (stock: 440 M instructions against 532 M for the whole harness). It changes the sound
  (more aliasing on pitched-down effects), so it is a variant (`lin`) for the user's ear, not a
  default. `f44` (the device and dmix at 44.1 kHz, so 44.1 kHz sounds skip resampling) is the other
  free variant.
- **Not changed:** OpenAL's start-up constructors (`_GLOBAL__sub_I_bsinc_tables`, 0.07-0.08 s of
  every OpenAL game's load) also convert per entry, about 1 M load-hit-stores; small enough to leave.

**Expected on the PS3.** The harness weights the resampler and sample loading more than a game
does (M0's real split had them at 1.7 of OpenArena's 4.7%), and the estimate's 1 cycle an
instruction and 40 a load-hit-store are guesses, so the mixer's real saving is more likely 25-45%
than 55%: about 3-5% of OpenArena's CPU (OpenAL 12.4%), 2-3% of ClassiCube's plus its 1.9% of ALSA
conversion with 16-bit output, about 1% of [SuperTuxKart](../gamedb/games/supertuxkart/)'s. The A/B decides; the commands are in
[TESTS.md](TESTS.md) "Next".

**Which ALSA path.** The bench runs with the desktop stopped: OpenAL opens `default`, which is
`plug`, `softvol`, `dmix:CARD=PS3,FORMAT=S16` at 48 kHz (the profile's
`snd_pcm_lfloat_convert_float_integer` is plug's float conversion). Games also map ALSA's pulse
plugin (docs/MEMORY.md, "Sound path"); when no PulseAudio server answers it falls back to the same
device. Each `openal.inc` variant and `OAL=` token logs what OpenAL got (`oal_device=` in
bench.txt: format, rate, period and buffer), which shows the format in use.

**What the card does under dmix.** dmix opens the card (`snd_ps3`) as a 256-frame period, 4096-frame
buffer, S16_BE stream and keeps it running while any client is open, with the card's
`stop_threshold` at the boundary: the card never xruns, silence fills what no client wrote.
OpenAL's own buffer under dmix is 6 periods of 256 frames, 1536 frames (32 ms). snd_ps3 refills the
card's DMA from its buffer-empty interrupt, one 256-frame block at a time, and its hw pointer moves
only there. When that chain of interrupts stops, the card stays RUNNING with its pointer still and
every dmix client waits for good (OpenAL's "Wait timeout", [games/LOG-FINDINGS.md](games/LOG-FINDINGS.md)
item 1). Kernel patch local/0047 (not on the PS3 yet) adds a watchdog for that:
`snd_ps3.dma_watchdog_ms` (default 200, 0 off) restarts the DMA when no buffer-empty interrupt
came for that long while playing, and `/sys/module/snd_ps3/parameters/dma_stats` counts the
interrupts by kind and the restarts.

## Vorbis decoding on the PPE

libvorbis decodes the Ogg Vorbis of [OpenArena](../gamedb/games/openarena/) ([ioquake3](../gamedb/engines/ioquake3/)'s music and voices), [SuperTuxKart](../gamedb/games/supertuxkart/) (every
sound effect at load: the 2.2 s "audio" of its start-up profile) and every SDL2_mixer game
([BananaBread](../gamedb/games/bananabread/)'s music); all three go through libvorbisfile's `ov_read` to native-endian 16-bit.
Debian builds it `-O2 -ffast-math` (Debian's `-O2` overrides configure's `-O3`), no AltiVec; libvorbis
has no SIMD code. Tremor, the fixed-point decoder, is not in Debian, and the PPE's integer multiplier
is its slow unit, so it was not pursued.

**Measured on the PC** (qemu-ppc64, `ppecost`, 20 s of a SuperTuxKart stereo music track at
44.1 kHz decoded the way the games do, `ov_read` 4 KB at a time):

| Build | insns | LHS | est | |
|---|---|---|---|---|
| Debian's flags (`-O2`) | 334 M | 3.93 M | 505 M | `ov_read_filter` 172 M of it (34%) |
| `-O3 -mcpu=cell -mtune=cell` | 312 M | 3.86 M | 479 M | -5% |
| the same plus our patch | 273 M | 0.33 M | 300 M | -41% against Debian's |
| (float output, `ov_read_float`, Debian's flags) | 290 M | 0.40 M | 319 M | the decode without the conversion |

- **The conversion was a third of the decode.** `ov_read` rounds each sample with `floor()` and
  converts it to an integer (libvorbis's generic `vorbis_ftoi`, nothing for PowerPC): two
  load-hit-stores a sample. Our `packages/patched/audio/libvorbis/patches/0002` packs mono and stereo eight samples a step
  in AltiVec (`floor(x*32768 + 0.5)` with `vec_madd` and `vec_floor`, `vec_cts`, `vec_packs`: the
  scalar rounding and clamping), through an aligned 512-sample buffer so the caller's buffer needs
  no alignment. `oggdec` output is byte-identical to the unpatched build with the same flags for a
  stereo track and 12 mono effects. The packaging patch builds `-O3 -mcpu=cell -mtune=cell`.
- **Expected on the PS3:** about 40% of the Vorbis decode, so roughly 0.6-0.9 s of SuperTuxKart's
  2.2 s of load-time audio, and part of BananaBread's audio thread (6% of its CPU with the mixer and
  ALSA); a stereo 44.1 kHz stream drops from about 25 M to 15 M estimated cycles a second. No
  switch: the output is the same, so the A/B is the package against Debian's in the same boot.

## fluidsynth on the SPEs

Debian's `fluidsynth` 2.6.1+dfsg-1 is our package `packages/patched/audio/fluidsynth/` (`2.6.1+dfsg-1+ps3.N`), with one patch, `packages/patched/audio/fluidsynth/patches/0001-rvoice-voice-DSP-through-libspeq-on-the-PS3-s-SPEs.patch`, and libspeq's `speq_synth_*_v2` calls (`packages/walnut/tools/ps3-spu/lib/synth.c`, `lib/synth-kern.h`, `spu/synth-op.c`; the op lives in `deadline.spu`).

The reason: [dsda-doom](../gamedb/games/dsda-doom/) playing its demo at normal speed used 53 s of CPU in 60 s with fluidsynth music against 29 s without, and ALSA underruns went from 18 to 1280 a minute. A perf profile of dsda-doom (2026-10-17, our `+ps3.270` with dbgsym; `--proc-map-timeout=20000`) put libfluidsynth at 48.3% of the process's samples and its SDL audio thread at 55%. Per function: `fluid_rvoice_dsp_interpolate` 31.1%, `fluid_iir_filter_apply` 7.9%, `fluid_rvoice_buffers_mix` 4.2%, `fluid_rvoice_write` (the control part) 2.2%. So about 43 of those 48 points are per-voice DSP: block-sized and SIMD-friendly.

### The render path in 2.6.1

[dsda-doom](../gamedb/games/dsda-doom/) renders between MIDI events from SDL's audio callback, at 44.1 kHz, gain 0.5, polyphony 256, reverb and chorus off, with the SoundFont `/usr/share/sounds/sf3/default-GM.sf3`. `fluid_synth_write_float` goes through `fluid_synth_render_blocks` in blocks of `FLUID_BUFSIZE` = 64 samples (1.45 ms), and for each active voice `fluid_rvoice_write` renders a block and `fluid_rvoice_buffers_mix` adds it to the voice's buffers (left, right, reverb send, chorus send).

`fluid_rvoice_write` has two halves. Control, which stays on the PPE: sample sanity, the note-off check, the envelopes and two LFOs, the amplitude, the pitch-to-phase step, the filters' coefficients. It is table lookups and a few dozen flops per block. DSP, which is the cost: interpolation (fourth order by default) around a 32.32 fixed-point phase, with special points at the start, the end and across a loop wrap; the SoundFont's resonant low-pass (a biquad that also applies the volume envelope's gain ramp); and the mix. Debian builds fluidsynth with doubles, and compiles the filter with `-ffast-math`.

What stays on the PPE besides control: event handling, voice allocation and stealing, SoundFont lookups and modulators, and reverb and chorus (serial state of tens of KB; off in dsda-doom). fluidsynth's own multi-threaded render path (`synth.cpu-cores` above 1) hands whole voices to extra threads, control part included. That would mean porting the control code to the SPU and keeping it in step, so the hook instead keeps every line of control code fluidsynth's own, and the mixer keeps the stock path whenever cpu-cores is above 1.

### Design, version 2 (the current one)

**The split.** `fluid_rvoice_write` becomes `fluid_rvoice_write_ctl` plus `fluid_rvoice_write_dsp`; stock behaviour is the two together. A new render loop handles every block of one `fluid_synth_write_*` request (up to 16 blocks) before any DSP runs. This works because fluidsynth's control part needs nothing the DSP computes from samples: the phase, `has_looped` and the sample count after a block follow from the interpolation's loop structure (integer code), and the filter coefficients, cutoff, Q and gain from the smoothing counters. Only the filters' histories depend on the samples, and control never reads them. `speq_synth_add_v2` runs that sample-free part on the PPE (`sk_control`, `sk_filter_end` in `synth-kern.h`) and hands each voice's next state back at once. Then `speq_synth_render_v2` renders the whole round in one round trip. The PPE renders any voices libspeq refused (sinc interpolation, 24-bit samples, windows too wide) meanwhile.

**The job.** Entries sit voice-major, so a job renders whole voices block after block, carrying the filter histories; a job's 16 KB output holds up to 512 entries of 256 bytes per round, and jobs are cut so that both pool workers get work. The SPU writes only its own output lines and the descriptors it was given, never fluidsynth's memory. The PPE adds the lines to fluidsynth's buffers ([dsda-doom](../gamedb/games/dsda-doom/) has 4, up to 16 are supported).

**Samples by DMA.** SoundFont samples are int16 in main memory (SF3 is decoded at load). libspeq computes from the phase and increment the index range a block can read before the first loop wrap (window 0) and after it (window 1), at most 4096 samples each, which covers a pitch up to about 30 to 60 times the sample's rate; wider ones stay on the PPE. The special interpolation points (start, end, loop start, loop start+1, loop end-1) are read on the PPE. A sweeping filter gets the slice of the 12001-entry sin/cos table its cutoff crosses (at most 1024 entries). Each window's pages are touched on the PPE before submit, so a swapped-out SoundFont page returns there and not as a fault on the worker's thread. The SPU fetches one voice's windows while the previous voice renders.

**Single precision, vectors.** `lib/synth-kern.h` is one C file for both sides, in float with 4-wide vectors (SPU intrinsics in the hot loops, GCC's generic vectors elsewhere, AltiVec on the PPE path). The window's int16 samples are converted once into a float window with fluidsynth's end points written in; every cubic output is then one unaligned 4-float load times a coefficient row. The filter recurrence takes the older history first (one multiply-add on the dependency chain), the gain is applied afterwards in vectors, and 64-bit divisions and multiplications (libgcc calls on the SPU) are gone from the per-segment code.

**Fallback and the wait.** The SPU never writes a round's entries or input histories, so a late or failed round is redone on the PPE from them with the same DSP code, and render always delivers. The audio thread waits on the done word without spinning (`SPEQ_WAIT_NOSPIN`) for at most 2 ms plus 5 us per voice block (`SPEQ_SYNTH_TIMEOUT_US`). Three failed rounds in a row put the SPEs aside for 5 s; a pool whose workers all broke is gone for the process. Timeouts that come from queueing count as late, not as a failure of the SPU path.

**Where it runs.** The synth shares the deadline pool of 2 SPEs with skinning (design: [packages/walnut/tools/ps3-spu/doc/pool.md](../packages/walnut/tools/ps3-spu/doc/pool.md); the queue sharing is in [SPU.md](SPU.md)). `SPEQ_SYNTH_SPES` caps how many pool workers one round is split across. `SPEQ_DEBUG=1` prints blocks and voices on the SPEs, jobs, mean and max wait, refusals, timeouts, errors and back-offs.

**Accuracy.** Version 2 is no longer bit-exact to Debian's double-precision path. `tests/fs-kern.c` (200000 random voice blocks against fluidsynth's own functions): control results identical, samples at 96 dB SNR over all of them (random states include high-pass custom filters at low cutoffs, where single precision cancels). Whole renders of a generated GM piece: 129 to 131 dB SNR against unpatched fluidsynth, 127 to 128 with reverb and chorus, and the SPU path against the PPE path 132 dB (the jobs sum in another order). The SPU rounds float results toward zero and the host tests to nearest, so the PS3 will be a few dB below these; still far under 16-bit output.

### Host tests

- `make -C ps3-spu test`: queue-test (timeouts, poll and no-spin waits, the SPE budget, bulk queues), dlopen-check (the exports, `SPEQ_SYNTH=ppe|off` without spufs), and synth-test: the SPU path (the op on host threads, `SPEQ_FAKE=1`, every sample and table read checked against the DMA plan) against libspeq's PPE path on random voices (all three interpolations, loops down to 2 samples, wraps, sample ends, filter sweeps, ramps, unmapped buffers).
- `make -C ps3-spu test-fluidsynth` (`tests/fluidsynth-compare.sh`): fluidsynth with the hook and unpatched fluidsynth built for the host. A generated GM piece (`tests/make-midi.py`: 16 channels, about 134 voices a block on average, programs, bends, pans, sustain, CC74) is rendered with FluidR3_GM.sf2 through `tests/fs-render.c`: `SPEQ_SYNTH=off` identical to unpatched, `ppe` and `spu` at the SNR above, sinc identical (left to fluidsynth).
- Not covered on the host: the SPU program on hardware (its real speed and DMA); that is what the PS3 runs below are for.

### Results on the PS3

[dsda-doom](../gamedb/games/dsda-doom/) `play` (60 s, the demo at normal speed, GL renderer):

| Run | CPU a minute | ALSA underruns | Note |
|---|---|---|---|
| no music (`playnomusic`) | about 29 s | 18 | the floor |
| stock fluidsynth (`SPEQ_SYNTH=off`) | 44.5 to 53 s | 1095 to 1335 | |
| version 1 on the SPEs (double precision, one block per job, own queue; libspeq 0.288) | 31.7 s | 1097 | 731 us mean wait of a 1.45 ms block, 9.2 ms max, 55 timeouts |
| version 2 (single precision, rounds, 2 SPEs; libfluidsynth3 +ps3.309, libspeq 0.318) | 31.8 s | 588 and 709 (stock 1231 and 1331 in the same session) | the user, listening: "night and day, spu makes a huge difference" |
| version 2 on the shared deadline pool (libspeq 0.367) | | 465 (PPE 1095) | 4530 rounds on the SPEs, 0 timeouts, 0.84 ms mean wait |

The sources are the batches gamedb:batch/2026-10-17-fluidsynth-spu-dsp, gamedb:batch/2026-10-17-fluidsynth-309 and gamedb:batch/2026-10-17-speq-367-deadline-pool, and the results commit 388b461 in [TESTS.md](TESTS.md). Version 1 showed that the CPU can move off and the chopping stay: its scalar double-precision kernel took about 12 us per voice block (estimated 3 to 4), and the audio thread waited for every block in turn. Version 2 replaced that.

**dsda-doom still chops.** 465 underruns a minute on the pool, against 18 with no music. What glitches remain lines up with the game's own frame stalls. The cause is open: the pool's wait, SDL's audio buffer, or something outside the DSP. It is PLAN 2.8, and the decision on dsda-doom's music default (fluidsynth, OPL or off, PLAN D3) waits on it. [OpenTTD](../gamedb/games/openttd/)'s music (3893 underruns in its run) shares the SDL_mixer MIDI path. A sound check by the user's ear is scheduled in [TESTS.md](TESTS.md). The earlier sound-settings experiment on the same game (row "dsda-doom sound settings against the underruns", Mesa ps3.225): tuned settings (OPL, 48 kHz, bigger buffer, 60 fps cap) reached 340 underruns and 46 s of CPU in one run, the OPL variant 1312 in its first run; see the CSV for the rest.

## FluidSynth's reverb and chorus

What stays on the PPE of a fluidsynth render once the voice DSP is on the SPEs: control, the
effects and the output conversion. [OpenTTD](../gamedb/games/openttd/) leaves reverb and chorus at fluidsynth's defaults (on);
[dsda-doom](../gamedb/games/dsda-doom/)'s config turns both off, so nothing here changes dsda-doom.

**Measured on the PC** (2026-10-19, qemu-ppc64 with the `ppecost` plugin, the method in
[PROFILING.md](PROFILING.md#load-time-decoders-and-linking); a harness rendering the way OpenTTD
does, `fluid_synth_write_s16` in 512-frame chunks at 44.1 kHz, library defaults, TimGM6mb, 20 s of
OpenTTD's title song `tttheme2.mid` and of Freedoom's D_RUNNIN, `SPEQ_SYNTH=off`). Share of the
estimated PPE cycles of the whole render with effects on:

| Change | before 0003 | with 0003 |
|---|---|---|
| reverb off | -16.2% | -10.0% |
| chorus off | -6.2% | -6.8% |
| both off | -22.5% | -16.8% |
| chorus lines 3 -> 1 (`synth.chorus.nr`) | -2.3% | -2.5% |
| reverb engine fdn / free / lex instead of 2.6.1's default Dattorro | -6.7 / -6.0 / -9.3% | +0.1 / +0.8 / -2.6% |

- **The default reverb divided 14 times a sample.** 2.6.1's Dattorro reverb wrapped each of its 14
  output taps with `index %= size` every sample: 12.35 M divides per 20 s, about 40% of the reverb's
  estimated cycles. Patch 0003 wraps them with one conditional subtraction (the `%` stays as a
  fallback): the reverb's estimate falls from 1.03 G to 0.47 G per 20 s, output bit-identical (the
  host comparison below, and renders before and after).
- With 0003, of what stays on the PPE the reverb is 6.0% of the render, the chorus 4.8% and the
  16-bit output 3.4% (a float-to-int load-hit-store per sample); the voice DSP, 78%, is the SPEs'
  part on the PS3 (its own load-hit-stores sit in `fluid_rvoice_dsp_interpolate`).
- **On the PS3** (scaled to the gameplay profile, OpenTTD's FluidSynth 10.2% plus libspeq 4.2% of its
  samples, effects 29-39% of synthesis, split reverb 74% / chorus 26% by the model): 0003 saves 1.7 to
  2.3% of OpenTTD's samples with the same sound; on top of it reverb off saves 1.4 to 1.9%, chorus off
  1.1 to 1.5%, both 2.5 to 3.4% (plus the effect send buffers the PPE no longer adds SPU lines into),
  one chorus line about 0.5%. Those change the sound, so they are the user's call by ear.
- **Not done, and why.** A float build (`enable-floats`) would be ABI-safe but turns our SPU hook off
  (`fluid_rvoice_speq.h` needs doubles there). GCC vectorizes nothing in the chorus or the FDN
  reverb with `-O3 -mcpu=cell`; the Dattorro chain is a serial recurrence. Reverb on the SPEs: its
  lines are about 33k floats (130 KB), too big to stay resident on a deadline-pool worker shared with
  synth and skinning; its input is complete only after a round's SPU lines are added on the PPE, so
  it needs a second round trip or a chained serial job moving about 100 KB in and 48 KB out per
  1024-sample round. The compute fits (about 50-80 us a round), but at M-L effort for at most 2.5-3.4%
  of one game, 0003 and the switch below take the cheap part.

**The switch** (patch 0002): `new_fluid_synth()` applies setting overrides before it reads any
setting, so they win over what the program set: first `/etc/fluidsynth/settings` (lines `name value`
or `name=value`, `#` comments), then `FLUIDSYNTH_SETTINGS` (`name=value` pairs separated by commas or
spaces, e.g. `FLUIDSYNTH_SETTINGS=synth.reverb.active=0,synth.chorus.active=0`). Values take the
setting's type (integers also on/off, yes/no, true/false); unknown names and bad values are warned
and skipped; what was applied goes to stderr once (`fluidsynth: settings overridden: ...`). OpenTTD,
dsda-doom and SDL_mixer's fluidsynth backend all set their settings before `new_fluid_synth()` and
call nothing afterwards that turns the effects back on. The file is not shipped: the default stays
Debian's until the user has listened; a chosen default would go into ps3-system as that file. The
bench token `FLUIDFX=off|norev|nocho|cho1|on` sets the variable for one run.

**Tests.** `make -C ps3-spu test-fluidsynth` passes with 0002 and 0003; with reverb and chorus on,
`SPEQ_SYNTH=off` is identical to unpatched Debian (so 0003 is exact) and the ppe/spu paths sit at 130
to 133 dB SNR. Neither patch has run on the PS3.

## History

- 2026-10-16: OpenAL mixer designed (never built). 2026-10-17: M0 measured the mixer under the bar, so the design was parked. The same day fluidsynth's version 1 was built and run (CPU moved, chopping stayed), then version 2 (underruns halved), then the shared deadline pool (libspeq 0.367, 2026-10-17 late).
- 2026-10-19: the PPE side of the audio libraries costed under qemu with a load-hit-store counter; OpenAL's mixer, libvorbis's ov_read and fluidsynth's reverb patched, OpenAL's 16-bit output made the default, the effects switch added; built, not yet run on the PS3.
- Version 1 gave fluidsynth its own one-worker deadline queue, with DXT on a bulk queue of 4 and OpenAL's mixer imagined as a second one-worker queue, to keep every process at 6 contexts. The deadline pool replaced that arrangement so that every latency-bound service shares the same 2 SPEs.

Older versions and the dated history: [archive/SPU-AUDIO.md](archive/SPU-AUDIO.md).
