# OpenAL Soft's voice mixer on an SPE: the design

Designed 2026-10-16, not built, because M0 measured the mixer under the 6% bar: OpenAL Soft is 3.7 to 4.7% of a game's samples (OpenArena 4.7%, ClassiCube 3.7%), see [../SPU-AUDIO.md](../SPU-AUDIO.md). The design is kept in case a future game spends more on OpenAL. Everything here is read from Debian's `openal-soft` 1:1.25.2-1 (what the PS3 runs; our unpatched rebuild is `openal-soft/`) and `ioquake3` 1.36+u20260719.58839361+dfsg-1. Line numbers refer to those trees. Numbers marked "estimate" were never measured on the PS3. The runtime it would use is described in [../SPU.md](../SPU.md).

## The idea in short

- **Hook.** One per mixer update, not per mixer function. In `ProcessContexts` (`alc/alu.cpp:2193-2198`) the loop that calls `Voice::mix` for every playing voice becomes: prepare every voice on the PPE, mix all eligible voices in one libspeq job, wait, finish every voice on the PPE. `Voice::mix` (`core/voice.cpp:695-1193`) is split into those three parts so the PPE path and the SPU path share the bookkeeping. The SPU kernel and queue live in libspeq as a service (`speq_mix_v1`), as the DXT encoder does for Mesa, and OpenAL Soft `dlopen`s `libspeq.so.0`.
- **Expected gain.** If voice mixing is most of the 11% the mixer thread had in step 0's (void) profile, as the code suggests, about 8 to 10 points of the game's samples leave the PPE and 1 to 2 stay. That would be +3 to +5 fps of the 5.6 fps between stock sound (49.7) and no sound (55.3); those two fps figures come from the invalid step 0 runs, so read them as the shape of the estimate. M0 later put the whole of OpenAL under 5%.
- **Main risks.** The gain is small if libasound and parameter updates are a big part of the thread. Two copies of the voice bookkeeping would drift: the split avoids that. Source pages swapped out under memory pressure: the PPE pre-touches and a timeout falls back. Sharing the SPEs with DXT and fluidsynth (the deadline pool answers it). Rebasing a patch on OpenAL Soft's hottest function.

## 1. The mixer path in 1.25.2

**Thread and call chain.** The ALSA backend's thread `alsoft-mixer` (`AlsaPlayback::mixerProc`, `alc/backends/alsa.cpp:462`) waits in `snd_pcm_wait`, takes the mmap area and calls `DeviceBase::renderSamples(void*, n, frameStep)` (`alu.cpp:2466`), which loops over chunks of at most `BufferLineSize` = 1024 frames:

1. clear `MixBuffer`;
2. under `getWriteMixLock()`, `ProcessContexts` (`alu.cpp:2170`): `ProcessParamUpdates` (play/stop/pause requests from the API, then voices with new properties get gains, panning, pitch and resampler recomputed; ioquake3 moves sources every frame, so this runs for most voices about once per game frame), then `Voice::mix` for each voice not Stopped or Pending, then the effect slots (none: ioquake3 uses no EFX);
3. `mPostProcess`: for stereo output `AmbiDecPostProcess`, a first-order 2D ambisonic decode from 3 lines (W, Y, X) to 2, done with `MixSamples`, the same `Mix_C` the voices use (in a flat profile `Mix_C` holds this decode too);
4. limiter, distance compensation, dither: all off for float output;
5. `Write<float>` interleaves into the mmap area.

Then `snd_pcm_mmap_commit` hands the frames to libasound on the same thread. `mixerProc` renders every free whole period at once, and `renderSamples` cuts that into updates of at most 1024 frames: 512 when one period is free, 1024 when two are.

**Our device.** Nothing sets ALSA options, so OpenAL opens `default`: `plug`, then `softvol`, then `dmix:CARD=PS3,FORMAT=S16` at 48000 Hz with a 1024-frame slave period. OpenAL asks for float32 stereo, 48000 Hz, 512-frame periods times 3. So `plug` converts float to S16 inside libasound on the mixer thread (the "ALSA conversion" in profiles), and softvol and dmix's mixing run there too. An update is 10.7 or 21.3 ms. `ALSOFT_LOGLEVEL=3` prints the period OpenAL actually got. snd_ps3 offers 44100, 48000, 88200 and 96000 Hz, so a 44.1 kHz dmix is possible.

There is no HRTF (stereo HRTF turns on only when asked or when the device counts as headphones, which the ALSA backend never sets) and no SIMD: `Voice::InitMixer` and `PrepareResampler` select by NEON/SSE flags, ppc64 has neither, so the PPE runs `Mix_C` and `Resample_Cubic_C` (plain scalar C, `core/mixer/mixer_c.cpp`). The default resampler is `Spline`, the 4-tap cubic with a 32-phase table (1 KB). Debian builds with `-O2` and `_GLIBCXX_ASSERTIONS`, so the spans in those loops are bounds-checked.

**Voice::mix, per voice and update** (`core/voice.cpp:695-1193`):

- 706-761: read the position (integer plus 16.16 fraction), the current or loop buffer, the step (pitch times 65536, capped at `MaxPitch` 10), handle a delayed start and a Stopping voice that never started; `samplesToMix` = update minus `outPos`.
- 762-781: carve the mixing lines from the end of `device->mSampleData` (one per mixing channel).
- 785-978, per source channel: copy the 48-float history into `mResampleData`; load the source window as float (`LoadSamples` also does mulaw/alaw, i32, f64 and ADPCM blocks); resample into the mixing line, or copy when step is 1.0 and frac 0; save the new history while Playing.
- 989-1006: decoders and the ambisonic band splitter, which do not apply to us.
- 1008-1074: gains. The first update of a voice does not ramp (no `IsFading` yet, so the gains jump to their targets); later updates ramp over `min(samplesToMix, 64)`. `DoFilters` runs only when a filter is active (never for ioquake3). Then `MixSamples` into each Dry line, 3 for us. A silent voice still costs its load and resample.
- 1076-1193: a Stopping voice becomes Stopped; otherwise advance the position, walk the buffer queue, loop or end, store state, push events.

**Sample data.** A buffer's samples live in `al::Buffer::mDataStorage`, 16-byte aligned at the start, in the format given to `alBufferData` and in host byte order (big-endian i16 here, which the SPU reads without swaps). A voice reads `VoiceBufferItem::mSamples`, a span into it. A voice is static when its source has `AL_STATIC` type with one buffer; otherwise it walks a queue, wrapping to `mLoopBuffer`.

**What ioquake3 gives it.** Effects are decoded whole and uploaded with `qalBufferData` at the file's rate as static mono 16- or 8-bit voices. Music is four queued buffers of 4096 bytes (stereo 16-bit at the track's rate) refilled on the game thread through libvorbisfile. Up to 96 3D sources, `AL_INVERSE_DISTANCE_CLAMPED`, Doppler 1.0, no EFX, no filters. OpenArena's data: 518 WAV effects (all mono; 409 at 44100 Hz, 85 at 22050 Hz, 24 at other rates; 507 are 16-bit), 21 Ogg files (7 stereo 32 kHz music tracks, 14 small voices).

## 2. The measurement that decided against it (M0)

The go/no-go was: build the SPU path only if voice mixing (`Voice::mix` inclusive) is at least 6% of a game's samples in the configuration we would ship. M0 ran on 2026-10-17 across OpenArena, ClassiCube, Yamagi Quake II and Luanti; no game reached 6%. The numbers and how to rerun them are in [../SPU-AUDIO.md](../SPU-AUDIO.md). The variants worth running first, because they are free, are `b16` (int16 output without the limiter and dither) and `f44` (dmix and OpenAL at 44.1 kHz, so 409 of OpenArena's 518 effects take the copy path instead of the cubic resampler); either may be shippable alone, and the SPU path's gain would be judged against the best of them.

## 3. Design

### (a) What runs on the SPU

**One job per update:** load, convert, resample and mix every eligible voice into the 3 Dry lines, plus each voice's new history and gains. That is `voice.cpp:786-1074` for the configuration we have. The PPE keeps `ProcessParamUpdates` (the job needs its output), the voice bookkeeping (positions, queue walk, events), voices the SPU path does not take, the post-process, `Write`, and libasound.

**Eligible voice:** PCM u8/i16/f32, mono or stereo; static or queued (not a callback buffer); no decoder, not ambisonic, no HRTF, no NFC, no active filter, no aux send; resampler point, linear or cubic (spline and gaussian share the cubic code and differ only in table); a source window of at most 4096 frames (pitch up to about 3.9 for a 1024-frame update). Every ioquake3 voice qualifies; the rest are mixed by the PPE after the job into the same lines.

**Per-voice descriptor** (`struct speq_mix_voice`, 256 bytes, built by the PPE in prepare):

- format, channels, frame step, resampler, step (16.16), start frac, `samplesToMix`, `outPos`, play state (Stopping mixes towards silent targets);
- the source window as up to 4 segments `{ea, frames}`, plus leading silence (negative position) and a "hold last sample" fill, so the SPU never walks OpenAL's lists;
- per channel: the address of `mPrevSamples[chan]` (48 floats, 16-byte aligned), current and target gains for 3 lines, the fade counter;
- where in the job's output slot to put the new history and current gains.

**Job** (`struct speq_job`, 128 bytes): `op` = mix; `arg[]` holds the descriptor array's address and count, the output slot's address, samples to do, line count (3).

**SPU side.** Double-buffered: DMA-list in descriptor n+1 and its window while mixing n. Convert to float in local store, resample 4 outputs at a time (the cubic phase table is 1 KB, resident), mix with the 64-sample ramp into 3 lines accumulated in local store (12 KB), then put the lines and per-voice results into the slot. Local store budget (estimate): code about 20 KB, lines 12 KB, two windows of at most 16 KB plus descriptors, history 0.4 KB per voice, about 70 KB in all.

**Size against libspeq's costs** (all estimates: SPU cycles from instruction counts, PPE time from the profile share):

| | per voice (mono, 1024 frames) | 25 voices |
|---|---|---|
| SPU: window in (about 1.9 KB), convert | about 0.3 us | |
| SPU: cubic resample (about 8 cycles per output) | about 2.6 us | |
| SPU: mix into 3 lines with ramp | about 0.4 us | |
| SPU job | about 3.5 us (music, stereo: about 7) | about 100 us, plus about 5 us for the output put |
| PPE today (11 to 12% of a thread, about 120 ms/s) | | about 1.3 ms (512 frames) to 2.5 ms (1024 frames) per update, all of the mixer thread |
| PPE on the SPU path: prepare about 1 us and finish about 0.5 us per voice, submit 3 to 10 us, futex sleep and wake about 10 us, 0x2104 wake on the worker's thread about 8 us, add 3 lines about 5 us | | about 60 to 70 us |

A job of about 100 us is above the 80 us spin and well above dispatch (submit p50 3 to 10 us). One SPE is enough; split across two only if M0 shows over about 50 voices.

### (b) The hook

**Not at the mixer-function level.** `MixSamplesOut`, `MixSamplesOne` and `mResampler` are called once per channel per voice per update with about 1024 samples, which is 1 to 3 us of SPU work, below submit plus completion. The data flow forbids batching them: `Voice::mix` resamples into `device->mSampleData` and mixes it at once, and the next voice reuses the same scratch. An SPU variant there would be one synchronous round trip per call, about 50 per update.

The variant slot is still worth one thing: an AltiVec `Mix_VMX` and `Resample_Cubic_VMX`. The PPE has VMX and Debian's ppc64 build uses none. That would speed up the PPE fallback and is the bar the SPU path must beat. Optional, after the SPU path.

**At the update level** (`alu.cpp:2192-2198`), the patch:

1. splits `Voice::mix` into `prepareMix()` (706-761: the eligibility test, the source window resolved into segments), `mixCore()` (762-1074, unchanged C; it ends by setting `IsFading`, and `MixLine` updates the current gains in place, both of which the SPU hands back) and `finishMix()` (1076-1193, unchanged). The PPE path is the three in order, so existing behaviour is the reference;
2. in `ProcessContexts`, if libspeq is loaded and enabled: prepare all voices, submit one job for the eligible ones, mix the others on the PPE into the lines, wait; on success add the slot's lines into `Dry.Buffer` and copy history and gains back, then `finishMix` every voice; on any error run `mixCore` for the eligible voices instead (their state was not touched);
3. loads `libspeq.so.0` once with `dlopen` (Mesa 0011's pattern), resolving `speq_mix_v1(const struct speq_mix_update *)`. Without it the code is stock.

All policy lives in libspeq: `SPEQ_MIX=off|spu`, the timeout, the slots and page touching, and disabling itself after repeated failures. OpenAL's side is about 300 lines.

**DMA constraints.**

- Source data can be anywhere in the game's heap. The SPU gets from the window start rounded down to 16 bytes and skips the head in local store. Gets are 16-byte multiples of at most 16 KB each, with a DMA list for longer windows (a 4096-frame stereo f32 window is 32 KB, two elements).
- History lines are 16-byte aligned. The output slot is ours: 128-byte aligned, allocated once, touched, reused.
- **Page faults.** A DMA to a non-resident page costs 40 to 87 us on the worker's thread, much more if the page is in swap on the USB disk (the game runs with about 128 MB swapped). Prepare touches one byte per 4 KB page of every window, the load the PPE path does anyway.
- The SPU never writes the game's memory, only the slot, so a wrong or stale address can only fail its own job (EFAULT, worker recreated in about 3.4 ms).
- **Buffer lifetime.** The job finishes inside the mix lock, before `finishMix`, so OpenAL's rules hold: a buffer attached to a playing voice cannot be deleted, and API calls that need the mixer idle wait for the lock.

### (c) Latency and synchronisation

- **Wait without spinning.** `speq_wait` spins 80 us before sleeping. On the mixer thread that burns cycles the game thread shares through SMT, so the audio wait sleeps at once (`SPEQ_WAIT_NOSPIN`). The extra 62 us wake latency is nothing against a 10.7 to 21.3 ms period.
- **Deadline.** The mixer thread starts an update when a period is free, with one or two more periods queued in ALSA, so it has milliseconds of slack. Timeout 2 ms (the job is about 0.1 ms). On timeout or error: abandon the slot (not reused until its done word completes), mix the eligible voices on the PPE (1 to 2.5 ms), carry on. The update is late but inside the buffered periods, so no underrun. Slots: a ring of 3, about 45 KB each (3 lines plus 64 voices of results), touched once. After 3 consecutive failures the process stays on the PPE, as `s3tc.c` does.
- **Mix lock.** `getWriteMixLock` is held across the job. API calls that `waitForMix()` (play/stop, offsets, `AL_LOOPING`) spin on the game thread meanwhile. The hold shrinks from 1.3 to 2.5 ms of PPE mixing to about 0.2 ms; only a timeout makes it longer.
- **Worker parking.** A worker parks after 10 ms idle, the length of one period, so it would park between most updates and pay about 90 us to resume. The audio service should keep its worker resident while sound plays: an idle threshold of at least 3 periods. The deadline pool does this (200 ms).
- **Pipelining** (submit update N+1, then output N) would hide the wait entirely, at one period of extra latency and a much harder patch (voice changes arrive in between). The synchronous design does not need it.

### (d) Vorbis: no. ADPCM effects instead (d')

ioquake3 decodes Ogg itself with the system libvorbisfile (`S_OGG_CodecReadStream`). OpenAL Soft only ever gets PCM and has no Vorbis extension, so a Vorbis offload would hook ioquake3's `snd_codec_t` layer, not OpenAL. It would not pay: music is already per buffer (4 x 4 KB in flight), effects are WAV so there is nothing compressed to keep, and the 1.9 s of load and about 7 MB seen in step 0 are WAV effects read from deflated pk3s and copied into OpenAL's buffers. ClassiCube's `Vorbis_OutputFrame` is 0.8% and it has its own decoder.

If the 7 MB matters: OpenAL Soft accepts `AL_FORMAT_MONO_IMA4` and `AL_FORMAT_MONO_MSADPCM_SOFT` and decodes them at mix time. ioquake3's `S_AL_BufferLoad` would encode 16-bit effects to ADPCM before `qalBufferData` (a quarter of the RAM), and the SPU kernel would decode the blocks of its window, which is cheap from the block start (the PPE path already does). Costs: an ioquake3 patch, 4-bit quality (the user listens), encoding time at load. Only after the mixer works, and only if memory is still short. See [../backlog/spu.md](../backlog/spu.md).

### (e) Expected gain

| | today | SPU path (estimate) |
|---|---|---|
| voice mixing (`Voice::mix`, resample, `Mix_C`) | most of the mixer thread | 0, plus about 60 to 70 us per update of prepare, finish, submit, wait |
| `ProcessParamUpdates` | part of the thread | same |
| AmbiDec 3 to 2, `Write<float>` | small | same (could move into the job with int16 output) |
| libasound: plug float to S16, softvol, dmix | part of the thread | same; b16 moves the conversion into OpenAL, the job could do it |
| thread wakeups (`snd_pcm_wait`, futex) | small | one more futex sleep and wake per update |

If voice mixing is about 80% of the thread (likely for scalar cubic resampling of about 25 voices into 3 lines), about 9 of the 11 points move. The fps effect is less direct, since the mixer thread costs the game thread through the shared PPE core and L2, and the game thread keeps its own AL calls. The guess was +3 to +5 fps, the bound being sound off.

### (f) Risks

1. **Gain below threshold.** M0 decided this (it was below), and b16 or f44 may take part of it for free.
2. **Behaviour drift.** The split keeps one copy of the bookkeeping; the SPU duplicates only `mixCore`'s arithmetic.
3. **SPU float.** Single precision truncates instead of rounding and flushes denormals (OpenAL flushes them too). Output differs from the C path in the last bits, so tests compare by SNR.
4. **Faults and swap.** A window in swap is a USB swap-in on the worker's thread. Handled by pre-touch, the 2 ms timeout and the PPE fallback; watch the fallback counter in games that swap.
5. **Sharing the SPEs.** A queue runs one SPU program, one worker per SPE unless asked for fewer. A second queue in a process (audio next to Mesa's DXT queue) makes more running contexts than the 6 SPEs, and spufs time-slices past that in 80 ms slices, far beyond any audio deadline. DXT jobs are not short either: `spu_encode` cuts one image into one job per worker (at least `JOB_MIN_PX` 16384 pixels) and at 379 Mpixel/s per SPE a 512x512 image is about 0.12 ms per job, 1024x1024 about 0.46 ms, 2048x2048 about 1.8 ms, so an audio job queued behind one would wait about that, and a 2048x2048 texture alone means a PPE fallback against a 2 ms timeout. Fluidsynth is a second audio service with its own deadline in the same process (a game with OpenAL effects and fluidsynth MIDI through SDL_mixer: Woof is one). What follows: one process-wide set of workers, deadline jobs claimed before bulk ones, a timeout caused by queueing counted as late rather than as a failure (only EFAULT/EIO/EPROTO count towards "PPE for good"), submit never blocking the mixer thread (a full ring of 64 jobs returns -EAGAIN), one worker kept resident while an audio service is active, and `SPEQ_DEBUG` counters per op. The deadline pool of [../SPU.md](../SPU.md) is the answer that was built for the synth and skinning; this mixer would have been its third op (its 70 KB fits the shared arena, estimate).
6. **Rebase cost.** `Voice::mix` changes in most OpenAL Soft releases. One patch, kept to `voice.cpp`/`voice.h`, `alu.cpp` and one new file.
7. **Latency spikes.** A resumed or recreated worker (90 us or 3.2 ms after 5 s parked, such as the first sound after silence) can hit the timeout once. That is a fallback, not a glitch.

**Measuring success.**

- **Host:** the SPU kernel built for the host (the `dxt-emu.h` approach) against OpenAL's C `mixCore` on synthetic voices: every format, rates 6000 to 48000, pitch 0.5 to 3, loops, queue boundaries, delayed start, stopping, fades; SNR of at least 100 dB on the lines; identical positions, history and events.
- **PS3, tool:** a scripted scene rendered through `ALC_SOFT_loopback` with `SPEQ_MIX=off` and `spu`: the same SNR test, plus us per update and jobs/fallbacks from `SPEQ_DEBUG=1`.
- **Game:** `run.sh suite` with a variant `spu` (to add to `openal.inc`: `SPEQ_MIX=spu`) interleaved with base, the best of b16/f44 and nosound, 5 rounds, in every OpenAL game; `OAL_PERF` and `oal-perf.sh` for the per-thread split. Success: fps at least stock + 3, `alsoft-mixer` at most 3% of samples, no ALSA underruns in the log, zero fallbacks in a bot match, and the user hears no difference.

## 4. Implementation plan

1. **M0** (no code): done, stopped here.
2. **libspeq (`ps3-spu`):** `speq_wait` with a timeout and a no-spin flag (done); a per-queue idle threshold (done); the op `spu/mix-op.c` with a host build; `lib/mix.c` for `speq_mix_v1` (policy, slots, touch, fallback counting, `SPEQ_MIX`); `speq-mix.h` (descriptor ABI) in `libspeq-dev`; host tests in `make -C ps3-spu test`. The queue-sharing decision became the deadline pool.
3. **Package `openal-soft`:** the unpatched debsrc exists (`openal-soft/package.conf`, `TYPE=debsrc`, `VERSION=1:1.25.2-1`, `DSC_SHA256` `4ea0ebdadbc70fe2ecf1972e85cf6238e09e0664e59fafa6f9aaac9db71ccb14`, `BUILD=native`). Still to do: one patch "voice mixing through libspeq on the Cell's SPEs" (the `Voice::mix` split, the `ProcessContexts` hook and `core/mixer/mixer_speq.cpp`, built only on ppc64). No Debian packaging change (`dh-sequence-dlopenlibdeps` already handles dlopen notes; libspeq stays a soft dependency like Mesa's).
4. **PS3:** the loopback comparison, then the game runs above. Ship if they pass.
5. **Later, only if needed:** post-process and S16 conversion in the job with int16 output; AltiVec mixers for the fallback; ADPCM effects in ioquake3.

Older versions and the dated history: [../archive/SPU-AUDIO.md](../archive/SPU-AUDIO.md).
