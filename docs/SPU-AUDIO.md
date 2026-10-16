# SPU audio: OpenAL Soft's voice mixing on an SPE

2026-10-16. Design only, nothing built. Follows docs/SPU-OFFLOAD-SURVEY.md ("Measured: step 0":
sound costs ~10% of OpenArena's frame rate, the mixer thread is 11% of the game's samples) and
the plan's section 7. Source read: Debian's `openal-soft` 1:1.25.2-1 (the version in the
snapshot and on the PS3, `build/apt-cache/libopenal1_1%3a1.25.2-1_ppc64.deb`; .dsc sha256
`4ea0ebdadbc70fe2ecf1972e85cf6238e09e0664e59fafa6f9aaac9db71ccb14`) and Debian's `ioquake3`
1.36+u20260719.58839361+dfsg-1. Line numbers are from those trees. "Estimate" marks numbers
nobody has measured here.

## Summary

- **Hook:** one per mixer update, not per mixer function. In `ProcessContexts`
  (alc/alu.cpp:2192-2198) the loop that calls `Voice::mix` for every playing voice becomes:
  prepare every voice on the PPE, mix all eligible voices in one libspeq job, wait, finish
  every voice on the PPE. `Voice::mix` (core/voice.cpp:695-1193) is split into those three
  parts, so the PPE path and the SPU path share the bookkeeping. The SPU kernel and the
  queue live in libspeq as a service (`speq_mix_v1`), as the DXT encoder does for Mesa;
  OpenAL Soft dlopens libspeq.so.0.
- **Expected gain:** an estimate until measurement M0 (below) splits the mixer thread by
  symbol. If voice mixing is most of the 11%, as the code suggests, about 8-10 points of the
  game's samples leave the PPE and 1-2 stay (the ALSA chain, parameter updates, wakeups).
  That should be +3 to +5 fps of the 5.6 fps between stock sound (49.7) and no sound (55.3).
- **Main risks:** the gain may be smaller than hoped if libasound and parameter updates are a
  big part of the 11% (M0 decides before any code). Keeping two copies of the voice
  bookkeeping consistent: avoided by the split. Source pages swapped out under the game's
  memory pressure: the PPE pre-touches the window, and a timeout falls back to the PPE.
  Sharing the SPEs with the DXT queue. Rebasing a patch on OpenAL Soft's hottest function.
- **Stage (d), Vorbis per buffer: not worth it for OpenArena.** Its effects are WAV (518
  files, all mono PCM), only the 7 music tracks are Ogg, and ioquake3 already streams those
  in 4 KB buffers through libvorbisfile. The ~7 MB is decoded *WAV* PCM. If we want that
  memory back, the lever is ADPCM effect buffers, which OpenAL Soft already decodes at mix
  time (stage d' below).

## 1. The mixer path in 1.25.2

**Thread and call chain.** The ALSA backend's thread `alsoft-mixer` (`AlsaPlayback::mixerProc`,
alc/backends/alsa.cpp:462) waits in `snd_pcm_wait`, takes the mmap area and calls
`DeviceBase::renderSamples(void*, n, frameStep)` (alc/alu.cpp:2466). That loops over
`renderSamples(n)` (alu.cpp:2395) in chunks of at most `BufferLineSize` = 1024 frames
(core/bufferline.h):

1. clear `MixBuffer` (alu.cpp:2398);
2. under `getWriteMixLock()`: `ProcessContexts` (alu.cpp:2170):
   - `ProcessParamUpdates` (2185): voices with new properties get gains, panning, pitch and
     resampler recomputed (`CalcAttnVoiceParams` 1707, `CalcPanningAndFilters` 1467,
     `PrepareResampler` 253). ioquake3 moves sources every frame, so this runs for most
     voices about once per game frame;
   - `Voice::mix` for each voice not Stopped/Pending (2192-2198);
   - effect slots (2200-2250): none, ioquake3 uses no EFX;
3. `mPostProcess` (2417): for our stereo device `AmbiDecPostProcess`, a first-order 2D
   ambisonic decode of 3 lines (W, Y, X) to 2 (alc/panning.cpp:1435-1437 via `InitPanning`,
   `StereoConfig` panning.cpp:548);
4. limiter, distance compensation, dither (2420-2432): all off for float output (below);
5. `Write<float>` interleaves `RealOut` into the mmap area (2481-2490).

Then `snd_pcm_mmap_commit` hands the frames to libasound on the same thread.

**Our device.** Nothing sets ALSA options, so OpenAL opens ALSA's `default`. On the PS3 that is
alsa-lib's `cards/PS3.conf`: `plug` → `softvol` → `dmix:CARD=PS3,FORMAT=S16`, and dmix
defaults to 48000 Hz with a 1024-frame slave period (`pcm/dmix.conf`). OpenAL asks for its
defaults: float32, stereo, 48000 Hz, 512-frame periods × 3 (core/device.h:46-49,
alc/alc.cpp:1131). So:
- `plug` converts float to S16 in libasound, on the mixer thread. That is the "ALSA conversion"
  in the profile. softvol and dmix's mixing also run there.
- The period OpenAL gets back from dmix (512 or 1024) is unverified; `ALSOFT_LOGLEVEL=3` prints
  it. An update is 10.7 or 21.3 ms.
- **No HRTF.** Stereo HRTF turns on only when asked or when the backend reports headphones
  (`DeviceFlag::DirectEar`, panning.cpp:1310-1311), and the ALSA backend never reports them.
  So step 0's `hrtf=false` changed nothing.
- **No SIMD.** Mixer variants are picked at `Voice::InitMixer` (voice.cpp:129-184) from
  NEON/SSE flags. ppc64 has neither, so the PPE runs `Mix_C` and `Resample_Cubic_C`
  (core/mixer/mixer_c.cpp, plain scalar C). The default resampler is `Spline`
  (`ResamplerDefault`, core/voice.h:35), i.e. the 4-tap cubic with a 32-phase table
  (core/cubic_defs.h: 1 KB).

**Why step 0's variants didn't help.**
- **B (`sample-type=int16`)** also turned on the output limiter and 16-bit dithering, because
  both default to on for integer output (alc.cpp:1669-1696 and 1704-1755). That is likely why
  B's mixer thread grew to 16.5%. A fair int16 test adds `output-limiter=false` and
  `dither=false`.
- **B22 (`frequency=22050`)** probably still ran at 48 kHz. Without `[alsa] allow-resampler=true`
  the backend disables ALSA's resampler (alsa.cpp:736-743), and `set_rate_near` on a dmix
  fixed at 48000 returns 48000. Check the log before believing any rate result.

**Voice::mix, per voice and per update** (core/voice.cpp):
- 705-755: read the position (`mPosition` int, `mPositionFrac` 16.16), the current/loop buffer,
  the step `mStep` (pitch × 65536, from alu.cpp:1680), handle the delayed start (`outPos`) and
  stopping.
- 786-978, per source channel:
  - copy the 48-float history `mPrevSamples[chan]` into `device->mResampleData`;
  - load the source window as float: `LoadBufferStatic` 487, `LoadBufferQueue` 550 or
    `LoadBufferCallback` 533, which call `LoadSamples` 261 to convert u8/i16/f32/IMA4/MSADPCM;
  - resample into `mSampleData` with `mResampler` (or a copy when step = 1.0 and frac = 0);
  - save the new history. The loop repeats only if pitch saturates the 1024+padding buffer.
- 989-1006: decoders (UHJ/super-stereo) and the ambisonic band splitter. Neither applies to us.
- 1008-1074: per channel:
  - `DoFilters` (biquad low/high-pass, only when `FilterActive`: never for ioquake3, no
    filters, no air absorption);
  - then `MixSamples` (`Mix_C`: gain ramp over at most 64 samples, then a constant gain) into
    each Dry line. That is 3 lines for us; HRTF and NFC are the alternatives, both off;
  - then each aux send (none).
- 1076-1193: advance the position, walk the buffer queue (`buffers_done`), loop or end,
  store state, push `BufferCompleted` / `SourceStopped` events.

**Sample data.**
- A buffer's samples live in `ALbuffer::mDataStorage` (al/buffer.h:36), an `al::vector<T,16>`:
  16-byte aligned at the start, in the format given to `alBufferData` (u8, i16, f32, IMA4,
  MSADPCM, ...).
- A voice reads `VoiceBufferItem::mSamples`, a span into it. A voice is static when the source
  was given one buffer with `AL_BUFFER` (al/source.cpp:660). Otherwise it is a queue walked
  item by item, wrapping to `mLoopBuffer`.

**What ioquake3 gives it** (code/client/snd_openal.c):
- **Effects:** `S_AL_BufferLoad` (358) decodes the whole file with `S_CodecLoad` and uploads it
  with `qalBufferData` at the file's own rate (411, 428). These become static voices,
  `AL_FORMAT_MONO16` or `MONO8`.
- **Music:** `S_AL_MusicProcess` (2023) refills 4 queued buffers of 4096 bytes. That is
  stereo 16-bit at the track's rate, decoded on the game thread by libvorbisfile (`ov_read`,
  snd_codec_ogg.c:391).
- **Sources:** up to `s_alSources` 96, 3D, `AL_INVERSE_DISTANCE_CLAMPED`, Doppler 1.0. No
  EFX, no filters, no HRTF request.
- **OpenArena's data** (all pk3s in the Debian packages):
  - 518 WAV, all mono: 409 at 44100 Hz, 85 at 22050 Hz, 24 at other rates; 507 are 16-bit.
  - 21 Ogg: the 7 stereo 32 kHz music tracks (7 MB) and 14 small mono voices.
  - Every voice is resampled to 48 kHz: 44100/48000 is a step of 0.919, 22050 one of 0.459.

## 2. Measurements before any code (M0, free)

All of these go through `games/openarena/step0.sh` and `oa-profile.sh`, with
`ALSOFT_LOGLEVEL=3` logged.
1. **Profile split of the mixer thread.** In `oa-profile.sh`, run
   `perf report --comm alsoft-mixer --sort dso,sym` and record these shares of all game samples:
   - `Voice::mix`, `Resample_*_C`, `Mix_C`, `LoadSamples` (what moves to the SPU);
   - `CalcAttnVoiceParams`/`CalcPanningAndFilters` (stays);
   - `BFormatDec::process` and `Write<>` (stays, or moves later);
   - libasound (stays);
   - kernel (wakeups).

   **Go/no-go:** build the SPU path only if the first group is ≥ 6% of the game's samples.
   Step 0's profile A can't answer it (checked 2026-10-16): all 730 samples of the mixer thread
   (tid 20798) resolve to `[unknown]`, perf never mapped libopenal in that recording. Re-profile
   with `libopenal1-dbgsym` installed and perf started before the game (or `perf record -p` after
   OpenAL has opened its device, checking `perf report --sort dso` shows libopenal).
2. **Variants to add to `step0.sh audio`:**
   - **B16:** `sample-type=int16`, `output-limiter=false`, `dither=false`. This moves plug's
     conversion into OpenAL without adding work.
   - **F44:** `frequency=44100` with dmix at 44100 (`defaults.pcm.dmix.rate 44100` in
     `/etc/asound.conf`; snd_ps3 takes 44.1 kHz, to be confirmed). 409 of 518 effects then
     take the copy path instead of the cubic resampler (voice.cpp, `increment == MixerFracOne
     && fracPos == 0`).

   Either one may be shippable on its own, and the SPU path's gain should be measured
   against the best of them.
3. **Voices per update:** this needs the patch's debug counter, or a count in the profile of
   `Voice::mix` calls per update. It sizes the job.

## 3. Design

### (a) What runs on the SPU

**One job per update:** load, convert, resample and mix every eligible voice into the 3 Dry
lines, plus each voice's new history and gains. This is voice.cpp:786-1074 for the
configuration we have.

The PPE keeps:
- `ProcessParamUpdates` (the job needs its output);
- the voice bookkeeping (positions, queue walk, events);
- voices the SPU path doesn't take;
- the post-process, `Write`, and libasound.

**Eligible voice:**
- PCM u8/i16/f32, mono or stereo;
- static or queued (not a callback buffer);
- no decoder, not ambisonic, no HRTF, no NFC, no active filter, no aux send;
- resampler point, linear or cubic (spline/gaussian share the cubic code, they only differ in
  table);
- a source window ≤ 4096 frames (pitch ≤ ~3.9 for a 1024-frame update).

Every ioquake3 voice qualifies. The rest are mixed by the PPE after the job, into the same
lines.

**Per-voice descriptor** (`struct speq_mix_voice`, 256 bytes, built by the PPE in prepare):
- format, channels, frame step, resampler, step (16.16), start frac, `samplesToMix`, `outPos`,
  play state (Stopping mixes towards silent targets);
- the source window as up to 4 segments `{ea, frames}`, plus leading silence (negative
  position) and a "hold last sample" fill. The PPE resolves queue items and loop wraps here,
  so the SPU never walks OpenAL's lists;
- per channel: the EA of `mPrevSamples[chan]` (48 floats, 16-byte aligned), current and target
  gains for 3 lines, the fade counter;
- where in the job's output slot to put the new history and current gains.

**Job** (`struct speq_job`, 128 bytes):
- `op` = mix;
- `arg[]`: the descriptor array's EA and count, the output slot's EA, samples to do, the line
  count (3).

**SPU side.** Double-buffered: DMA-list in descriptor n+1 and its window while mixing n.
- Convert to float in local store.
- Resample 4 outputs at a time: the cubic phase table is 1 KB, resident.
- Mix with the 64-sample ramp into 3 lines accumulated in LS (12 KB).
- At the end, put the lines and the per-voice results into the slot.

LS budget (estimate): code ~20 KB, the lines 12 KB, two windows of ≤16 KB plus descriptors,
history 0.4 KB per voice. About 70 KB.

**Size against libspeq's costs.** All estimates: SPU cycles from instruction counts, PPE time
from the profile share.

| | per voice (mono, 1024 frames) | 25 voices |
|---|---|---|
| SPU: window in (≤1 KB at 44.1 kHz i16), convert | ~0.2 µs | |
| SPU: cubic resample (~8 cycles/output) | ~2.6 µs | |
| SPU: mix into 3 lines with ramp | ~0.4 µs | |
| SPU job | ~3.5 µs (music, stereo: ~7) | **~100 µs** (+ ~5 µs output put) |
| PPE today (11-12% of a thread ≈ 120 ms/s) | | **~1.3 ms** (512 frames) to **~2.5 ms** (1024 frames) per update, all of the mixer thread |
| PPE on the SPU path: prepare ~1 µs + finish ~0.5 µs per voice, submit 3-10 µs, futex sleep/wake ~10 µs, 0x2104 wake on the worker's thread ~8 µs, add 3 lines ~5 µs | | **~60-70 µs** |

A ~100 µs job is above the 80 µs spin and well above dispatch (submit p50 3-10 µs). One SPE
is enough. Split across two only if M0 shows over ~50 voices.

### (b) The hook

**Not at the mixer-function level.**
- `MixSamplesOut`, `MixSamplesOne` (core/mixer.h) and `mResampler` are called once per
  channel per voice per update, each with ~1024 samples, i.e. 1-3 µs of SPU work. That is
  below submit plus completion.
- The data flow forbids batching them. `Voice::mix` resamples into `device->mSampleData` and
  mixes it at once, and the next voice reuses the same scratch.
- An SPU variant there would mean one synchronous round trip per call: ~50 per update, each
  costing more than the work.

The variant slot is still worth one thing: an **AltiVec** `Mix_VMX`/`Resample_Cubic_VMX`. The
PPE has VMX, and Debian's ppc64 build uses none. That makes the PPE fallback (and every
non-Cell ppc64 with AltiVec) faster, and it is the bar the SPU path must beat. It is optional,
after the SPU path.

**At the update level** (alu.cpp:2192-2198). The patch:
1. splits `Voice::mix` into
   - `prepareMix()`: 705-760, the eligibility test, the source window resolved into segments;
   - `mixCore()`: 786-1074, unchanged C;
   - `finishMix()`: 1076-1193, unchanged.

   The PPE path is `prepareMix` + `mixCore` + `finishMix`, so the existing behaviour is the
   reference.
2. in `ProcessContexts`:
   - if libspeq is loaded and the SPU path is enabled, prepare all voices and submit one job
     for the eligible ones;
   - mix the others on the PPE into the lines;
   - wait;
   - on success, add the slot's lines into `Dry.Buffer` and copy history and gains back;
   - `finishMix` every voice;
   - on any error, run `mixCore` for the eligible voices instead (their state was not
     touched).
3. loads libspeq.so.0 with `dlopen` once (the Mesa 0011 pattern), resolving
   `speq_mix_v1(const struct speq_mix_update *)`. Without it the code is stock.

All policy lives in libspeq:
- `SPEQ_MIX=off|spu`, the timeout, the slots and page touching;
- disabling itself after repeated failures.

OpenAL's side is about 300 lines: the split, the descriptor fill, and the dlopen.

**DMA constraints.**
- Source data can be anywhere in the game's heap. The SPU gets from the window start rounded
  down to 16 bytes and skips the head in LS. Gets are 16-byte multiples ≤ 16 KB each, with a
  DMA list for longer windows. A 4096-frame stereo f32 window is 32 KB, which is 2 elements.
- History lines are 16-byte aligned (`al::vector<HistoryLine,16>`, voice.h:247).
- The output slot is ours: 128-byte aligned, allocated once, touched, reused.
- **Page faults.** A DMA to a page that isn't resident costs 40-87 µs on the worker's thread
  (measured), and much more if the page is in swap on the USB disk: the game runs with ~128 MB
  swapped. prepare touches one byte per 4 KB page of every window, the load the PPE path does
  anyway. A miss still costs a fault, on the PPE, where it would have happened today.
- The SPU never writes the game's memory, only the slot. So a wrong or stale EA can only fail
  its own job (EFAULT, worker recreated in ~3.4 ms, measured).
- **Buffer lifetime:** the job finishes inside the mix lock, before `finishMix`, so OpenAL's
  existing rules hold. A buffer attached to a playing voice can't be deleted, and API calls
  that need the mixer idle wait for the lock.

### (c) Latency and synchronisation

- **Wait without spinning.** `speq_wait` spins 80 µs before its futex sleep. That spin runs on
  the mixer thread, which shares the PPE core with the game thread through SMT, so it burns
  the very cycles we are trying to free. The audio wait should sleep at once. The extra ~62 µs
  wake latency is irrelevant against a 10.7-21.3 ms period. This needs a libspeq option (see
  the plan).
- **Deadline.** The mixer thread starts an update when a period is free, with 1-2 more
  periods still queued in ALSA, so it has milliseconds of slack. Timeout: 2 ms (the job is
  ~0.1 ms). On timeout or error:
  - abandon the slot: it is not reused until its done word completes;
  - mix the eligible voices on the PPE (1-2.5 ms);
  - carry on. The update is late but inside the buffered periods, so there is no underrun.

  Slots: a ring of 3, ~45 KB each (3 lines + 64 voices of results), touched once. libspeq has
  no timed wait yet (needed, see the plan). After 3 consecutive failures the process stays on
  the PPE, as `s3tc.c`'s `queue_broke` does.
- **Mix lock.** `getWriteMixLock` (alu.cpp:2403) is held across the job. API calls that
  `waitForMix()` (al/source.cpp:311 offsets, 697 `SendVoiceChanges` for play/stop, 1693
  `AL_LOOPING`) spin on the game thread meanwhile. The hold shrinks from ~1.3-2.5 ms of PPE
  mixing to ~0.2 ms; only a timeout makes it longer.
- **Worker parking.** A worker parks after 10 ms idle (`SPEQ_IDLE_US`), the same as one
  period. It would park between most updates and pay ~90 µs (measured) to resume, mostly as
  PPE time in spu_run. The audio service should keep its worker resident while sound plays:
  an idle threshold of ≥ 3 periods for this queue.
- **Pipelining** (submit update N+1, then output N) would hide the wait entirely, at one
  period of extra latency and a much harder patch (voice changes arrive in between). The
  synchronous design above doesn't need it.

### (d) Vorbis on an SPU: no; ADPCM effects instead (d')

- **Who decodes.** ioquake3 decodes Ogg itself, with the system libvorbisfile
  (`-DUSE_CODEC_VORBIS=ON -DUSE_INTERNAL_LIBS=OFF`; `S_OGG_CodecReadStream`,
  snd_codec_ogg.c:356/391). OpenAL Soft only ever gets PCM and has no Vorbis extension. A
  Vorbis offload would hook ioquake3's `snd_codec_t` layer (snd_codec.c), not OpenAL.
- **Why it doesn't pay.**
  - Music is already per buffer (4 × 4 KB in flight).
  - Effects are WAV, so there is nothing compressed to keep.
  - The 1.9 s of load and ~7 MB measured in step 0 are WAV effects read from deflated pk3s
    and copied into OpenAL's buffers.
  - The Vorbis decoding that happens is ~1 `ov_read` of 4 KB every frame or two. ClassiCube's
    `Vorbis_OutputFrame` is 0.8%, and it has its own decoder.
- **(d') ADPCM effects.** If the ~7 MB matters: OpenAL Soft accepts `AL_FORMAT_MONO_IMA4` and
  `AL_FORMAT_MONO_MSADPCM_SOFT` (al/buffer.cpp:645-646) and decodes them at mix time
  (`LoadSamples<IMA4Data>` voice.cpp:279, `<MSADPCMData>` 370).
  - ioquake3's `S_AL_BufferLoad` would encode 16-bit effects to ADPCM before `qalBufferData`:
    ÷4 RAM.
  - The SPU kernel would decode the blocks of the window, which is cheap from the block
    start. The PPE path already does.
  - Costs: an ioquake3 patch, 4-bit quality (the user listens), and encoding time at load.
  - Only after the mixer works, and only if memory is still short.

### (e) Expected gain

| | today | SPU path (estimate) |
|---|---|---|
| voice mixing (Voice::mix, resample, Mix_C) | most of the 11% (M0 gives the number) | 0, plus ~60-70 µs/update of prepare/finish/submit/wait |
| `ProcessParamUpdates` | part of the 11% | same |
| AmbiDec 3→2, `Write<float>` | small | same (could move into the job later, with int16 output) |
| libasound: plug float→S16, softvol, dmix | part of the 11% | same; B16 moves the conversion into OpenAL, the job could do it |
| thread wakeups (`snd_pcm_wait`, futex) | small | +1 futex sleep/wake per update |

If voice mixing is ~80% of the thread (the likely case for scalar C cubic resampling of
~25 voices into 3 lines), ~9 of the 11 points move. The fps effect is less direct. The mixer
thread costs the game thread through the shared PPE core (SMT) and the shared L2, and the
game thread keeps its own AL calls (~2 per one-shot and 4-5 per loop per frame, S_AL_Update).
So expect most of the 5.6 fps between A and Z back, not all of it: **+3 to +5 fps**. Z (55.3)
is the bound.

### (f) Risks

1. **Gain below threshold:** decided by M0 before code; B16/F44 may take part of it for free.
2. **Behaviour drift:** two implementations of the voice bookkeeping would diverge. The split
   keeps one (prepare/finish shared), and the SPU duplicates only `mixCore`'s arithmetic.
3. **SPU float:** single precision truncates instead of rounding and flushes denormals (OpenAL
   flushes them too, `FPUCtl`). The output differs in the last bits from the C path, so tests
   compare by SNR, not bytes.
4. **Faults and swap:** a window in swap means a USB swap-in on the worker's thread. Handled by
   pre-touch, the 2 ms timeout and the PPE fallback. Watch the fallback counter in games that
   swap.
5. **SPE sharing with DXT:**
   - Mesa's S3TC queue opens one worker per SPE in the same process (lib/s3tc.c:110). A second
     queue for audio would make 7 contexts for 6 SPEs while both are active, and spufs would
     time-slice them.
   - Either run both ops from one SPU program on one process-wide queue (an audio job then
     waits at most one DXT job, ~45-100 µs, behind the FIFO), or give audio its own one-worker
     queue and DXT `nspe - 1`. The main session decides; the first is simpler for the SPEs,
     the second for the LS budget.
6. **Rebase cost:** `Voice::mix` changes in most OpenAL Soft releases. One patch, kept to
   voice.cpp/voice.h, alu.cpp and one new file.
7. **Latency spikes:** a resumed or recreated worker (90 µs / 3.2 ms after 5 s parked, e.g.
   the first sound after silence) can hit the timeout once. That is a fallback, not a glitch.

**Measuring success.**
- **Host:**
  - the SPU kernel built for the host (the `dxt-emu.h` approach) against OpenAL's C
    `mixCore` on synthetic voices: every format, rates 6000-48000, pitch 0.5-3, loops, queue
    boundaries, delayed start, stopping, fades;
  - SNR ≥ 100 dB on the lines;
  - identical positions, history and events.
- **PS3, tool:** a scripted scene rendered through `ALC_SOFT_loopback` with `SPEQ_MIX=off` and
  `spu`. Same SNR test, plus µs per update and jobs/fallbacks from `SPEQ_DEBUG=1`.
- **Game:** `step0.sh audio` with a variant S (A + `SPEQ_MIX=spu`) interleaved with A, the
  best of B16/F44, and Z, 5 runs each. `oa-profile.sh` with the per-thread split. Success:
  - fps ≥ A + 3;
  - `alsoft-mixer` ≤ 3% of samples;
  - no ALSA underruns in the log and zero fallbacks in a bot match;
  - the user hears no difference.

## 4. Implementation plan

1. **M0** (no code): the profile split and variants B16/F44 above, recorded in
   SPU-OFFLOAD-SURVEY.md. Stop here if the voice-mixing group is under 6%.
2. **libspeq (ps3-spu), coordinated by the main session:**
   - `speq_wait` with a timeout and a no-spin flag (or the service uses internal ones);
   - a per-queue idle threshold;
   - the op: `spu/mix-op.c` with a host build;
   - `lib/mix.c` for `speq_mix_v1` (policy, slots, touch, fallback counting, `SPEQ_MIX`);
   - `speq-mix.h` (descriptor ABI) in libspeq-dev;
   - the queue-sharing decision (risk 5);
   - host tests in `make -C ps3-spu test`.
3. **Package `openal-soft`** (new top-level dir):
   - `package.conf` with `TYPE=debsrc`, `SOURCE=openal-soft`, `VERSION=1:1.25.2-1`, the
     `DSC_SHA256` above, `BUILD=native`;
   - `make openal-soft-work` / `-patches`;
   - **one patch** "voice mixing through libspeq on the Cell's SPEs": the `Voice::mix` split,
     the `ProcessContexts` hook and `core/mixer/mixer_speq.cpp` (dlopen, descriptor fill),
     built only on ppc64;
   - no Debian packaging change (`dh-sequence-dlopenlibdeps` already handles dlopen notes;
     libspeq stays a soft dependency like Mesa's);
   - the version becomes `1:1.25.2-1+ps3.N`, and the PS3 gets it from our repository instead
     of Debian's.
4. **PS3:** the loopback comparison, then the game runs above. Ship if they pass.
5. **Later, only if needed:**
   - post-process and S16 conversion in the job, with int16 output;
   - AltiVec mixers for the fallback;
   - (d') ADPCM effects in ioquake3.
