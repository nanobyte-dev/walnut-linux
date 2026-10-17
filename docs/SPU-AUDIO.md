# SPU audio: OpenAL Soft's voice mixing on an SPE

2026-10-16. Design only, nothing built. Follows docs/SPU-OFFLOAD-SURVEY.md ("Measured: step 0":
sound costs ~10% of OpenArena's frame rate, the mixer thread is 11% of the game's samples) and
the plan's section 7. Source read: Debian's `openal-soft` 1:1.25.2-1 (the version in the
snapshot and on the PS3, `build/apt-cache/libopenal1_1%3a1.25.2-1_ppc64.deb`; .dsc sha256
`4ea0ebdadbc70fe2ecf1972e85cf6238e09e0664e59fafa6f9aaac9db71ccb14`) and Debian's `ioquake3`
1.36+u20260719.58839361+dfsg-1. Line numbers are from those trees. "Estimate" marks numbers
nobody has measured here.

2026-10-17: every cited line checked against the 1.25.2 tree (Debian applies no patches to it)
and corrected in place; sections 1, 2 and 3(f) gained what the check found (the profile split's
traps, the gain ramp's first update, the queue shared with DXT and fluidsynth). The package is
ours now: `openal-soft/` (debsrc, unpatched, `1:1.25.2-1+ps3.N`), so the debug symbols match
what the PS3 runs. Its build confirms the scalar mixer: CMake finds no `xmmintrin.h` (POWER's SSE
translation headers need POWER8) and no NEON, "CPU extensions: Default".

## Summary

- **Hook:** one per mixer update, not per mixer function. In `ProcessContexts`
  (alc/alu.cpp:2193-2198) the loop that calls `Voice::mix` for every playing voice becomes:
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
  Sharing the SPEs with the DXT queue and fluidsynth: DXT jobs run up to ~2 ms, so audio needs
  one process-wide queue with deadline jobs claimed first (3(f) 5). Rebasing a patch on OpenAL
  Soft's hottest function.
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

1. clear `MixBuffer` (alu.cpp:2400);
2. under `getWriteMixLock()` (2403): `ProcessContexts` (alu.cpp:2170):
   - `ProcessParamUpdates` (2185, defined at 2147): first `ProcessVoiceChanges` (play, stop,
     pause and restart requests queued by the API), then voices with new properties get gains,
     panning, pitch and resampler recomputed (`CalcVoiceParams` 2007, which takes the pending
     property update and calls `CalcAttnVoiceParams` 1707 for 3D mono voices like ioquake3's,
     `CalcNonAttnVoiceParams` otherwise; `CalcPanningAndFilters` 1467, `PrepareResampler`
     253). ioquake3 moves sources every frame, so this runs for most voices about once per game
     frame;
   - `Voice::mix` for each voice not Stopped/Pending (2193-2198);
   - effect slots (2200-2246): none, ioquake3 uses no EFX;
3. `mPostProcess` (2422): for our stereo device `AmbiDecPostProcess`, a first-order 2D
   ambisonic decode of 3 lines (W, Y, X) to 2 (alc/panning.cpp:1435-1437 via `InitPanning`,
   `StereoConfig` panning.cpp:548). `BFormatDec::process` (core/bformatdec.cpp:60-93) does it
   with `MixSamples`, i.e. the same `Mix_C` the voices use: in a flat profile `Mix_C` holds the
   decode too (section 2);
4. limiter, distance compensation, dither (2425-2435): all off for float output (below);
5. `Write<float>` interleaves `RealOut` into the mmap area (2476-2485).

Then `snd_pcm_mmap_commit` hands the frames to libasound on the same thread. `mixerProc` renders
every whole period that is free at once (`avail -= avail%update_size`, alsa.cpp:509, under the
backend's mutex), and `renderSamples` cuts that into updates of at most 1024 frames: with
512-frame periods an update is 512 frames or, when two periods are free, 1024.

**Our device.** Nothing sets ALSA options, so OpenAL opens ALSA's `default`. On the PS3 that is
alsa-lib's `cards/PS3.conf`: `plug` → `softvol` → `dmix:CARD=PS3,FORMAT=S16`, and dmix
defaults to 48000 Hz with a 1024-frame slave period (`pcm/dmix.conf`). OpenAL asks for its
defaults: float32, stereo, 48000 Hz, 512-frame periods × 3 (core/device.h:46-49,
alc/alc.cpp:1131). So:
- `plug` converts float to S16 in libasound, on the mixer thread. That is the "ALSA conversion"
  in the profile. softvol and dmix's mixing also run there.
- The period OpenAL gets back from dmix (512 or 1024) is unverified; `ALSOFT_LOGLEVEL=3` prints
  it ("Post-reset: Stereo, Float, 48000hz, <period> / <buffer> buffer", alc.cpp:1603; the bench
  variants put it in bench.txt as `oal_device`). An update is 10.7 or 21.3 ms. dmix's slave runs
  1024-frame periods, 16 of them (pcm/dmix.conf defaults); snd_ps3 offers 44100, 48000, 88200
  and 96000 Hz (sound/ppc/snd_ps3.c:85-88), so a dmix at 44.1 kHz is possible.
- **No HRTF.** Stereo HRTF turns on only when asked or when the device counts as headphones
  (`DeviceFlag::DirectEar`, panning.cpp:1310-1311). The ALSA backend never sets that flag; only
  the config's `stereo-mode=headphones` would (alc.cpp:1609-1616), and nobody sets it. So step
  0's `hrtf=false` changed nothing (it is the deprecated spelling of the default,
  alc.cpp:1255-1266).
- **No SIMD.** `Voice::InitMixer` (voice.cpp:129-184) takes the mixers from `SelectMixer` and
  friends (voice.cpp:74-125), and `PrepareResampler` (alu.cpp:253-282) the resampler from
  `SelectResampler`, both by NEON/SSE flags. ppc64 has neither (the build finds no
  `xmmintrin.h`), so the PPE runs `Mix_C` and `Resample_Cubic_C` (core/mixer/mixer_c.cpp:198
  and 247, plain scalar C). The default resampler is `Spline` (`ResamplerDefault`,
  core/voice.h:35), i.e. the 4-tap cubic with a 32-phase table (core/cubic_defs.h: 32 x (4
  coefficients + 4 deltas) floats, 1 KB). Debian builds with `-O2` and `_GLIBCXX_ASSERTIONS`
  ("STL hardening level: Performance"): the spans in those loops are bounds-checked.

**Why step 0's variants didn't help.**
- **B (`sample-type=int16`)** also turned on the output limiter and 16-bit dithering, because
  both default to on for integer output (dither alc.cpp:1670-1697, limiter 1704-1754). That
  is likely why B's mixer thread grew to 16.5%. A fair int16 test adds `output-limiter=false`
  and `dither=false` (variant b16).
- **B22 (`frequency=22050`)** probably still ran at 48 kHz. Without `[alsa] allow-resampler=true`
  the backend disables ALSA's resampler (alsa.cpp:736-743), so `plug` won't convert rates, and
  `set_rate_near` on a dmix fixed at 48000 returns 48000. Check the log before believing any
  rate result. `frequency` also scales the period (alc.cpp:1140-1146): 44100 asks for 470
  frames.

**Voice::mix, per voice and per update** (core/voice.cpp:695-1193):
- 706-761: read the position (`mPosition` int, `mPositionFrac` 16.16, `MixerFracBits` 16 in
  core/mixer/defs.h:23), the current/loop buffer, the step `mStep` (pitch × 65536, capped at
  `MaxPitch` 10; for 3D voices computed in `CalcAttnVoiceParams`, alu.cpp:1990-1994,
  1675-1680 is the non-3D path), drop the loop past a static loop end, handle the delayed start
  (`outPos`) and a Stopping voice that never started; `samplesToMix` = update − `outPos`.
- 762-781: the mixing lines are carved from the end of `device->mSampleData` (one per mixing
  channel: 1 for mono, 2 when `mDuplicateMono`, alu.cpp:1605, which is never for 3D voices).
- 785-978, per source channel:
  - copy the 48-float history `mPrevSamples[chan]` into `device->mResampleData`;
  - load the source window as float: `LoadBufferStatic` 487, `LoadBufferQueue` 550 or
    `LoadBufferCallback` 533, which call `LoadSamples` 261 (also mulaw/alaw, i32, f64 and the
    ADPCM blocks); negative positions load silence first, an ended voice holds its quietest
    sample;
  - resample into the mixing line with `mResampler`, or copy when step = 1.0 and frac = 0
    (936);
  - save the new history, only while Playing (943-957). The loop repeats only if pitch
    saturates the resample buffer: `mResampleData` is 1024 + decoder padding + 48 floats, of
    which a pass may read all but 24 (`SrcSizeMax`), so above a step of ~1.0 a 1024-frame
    update takes more than one pass.
- 989-1006: decoders (UHJ/super-stereo) and the ambisonic band splitter. Neither applies to us.
- 1008-1074: gains, then per channel:
  - **the first update of a voice doesn't ramp**: without `IsFading` (set at 1074 after the
    first mix) `counter` is 0 and the current gains jump to the targets (1009-1027); later
    updates ramp over `min(samplesToMix, 64)` samples;
  - `DoFilters` (biquad low/high-pass, only when `FilterActive`: HF or LF gain ≠ 1,
    alu.cpp:1620-1622, never for ioquake3: no filters, air absorption factor 0, cone HF 1);
  - then `MixSamples` (`Mix_C` → `MixLine`, mixer_c.cpp:150: the ramp, then a constant gain,
    skipped for a line whose gain is below `GainSilenceThreshold`) into each Dry line. That is
    3 lines for us; HRTF and NFC are the alternatives, both off. A silent voice (all gains
    ~0, e.g. far away) still costs its load and resample;
  - Stopping voices mix towards zero targets (`SilentTarget`);
  - then each aux send (none).
- 1076-1193: a Stopping voice becomes Stopped here without moving; otherwise advance the
  position, walk the buffer queue (`buffers_done`), loop or end, store state, push
  `BufferCompleted` / `SourceStopped` events.

**Sample data.**
- A buffer's samples live in `al::Buffer::mDataStorage` (al/buffer.h:36-44), a variant of
  `al::vector<T,16>` (u8, i16, i32, f32, f64, mulaw, alaw, IMA4, MSADPCM): 16-byte aligned at
  the start, in the format given to `alBufferData`, in host byte order (big-endian i16 here, as
  the SPU reads it: no swaps).
- A voice reads `VoiceBufferItem::mSamples` (core/voice.h:93), a span into it, with `mSampleLen`,
  `mLoopStart`, `mLoopEnd` and `mNext` (voice.h:82-91). A voice is static when the source has
  `AL_STATIC` type, one buffer given with `AL_BUFFER` (al/source.cpp:660). Otherwise it is a
  queue walked item by item, wrapping to `mLoopBuffer`.

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

Through the game suite (games/bench, docs/GAME-PLAN.md), not `step0.sh` (its window never
reached the screen). Set up 2026-10-17, not run yet.

**Which games.** OpenAL Soft plays the sound of OpenArena (ioquake3 dlopens libopenal.so.1),
Yamagi Quake II (`s_openal 1`, its default), ClassiCube and Luanti in the suite, and
SuperTuxKart (a recipe outside the suite). Not the others of the plan's list: QuakeSpasm 0.96.3
plays through SDL2's audio (Debian's package links no libopenal), Sauerbraten through
SDL2_mixer, and Extreme Tux Racer through SFML 3.1, whose audio module is miniaudio. ClassiCube's
bench flight probably plays nothing (music starts after 2 to 7 minutes, the flight digs no
blocks), so its numbers are the cost of an open, idle OpenAL device.

**Variants**, `variant_<name>` in those four recipes, the same names everywhere
(games/bench/recipes/openal.inc, which also writes the run's `alsoft.conf`, OpenAL's log
`alsoft.log` and `oal_backend`/`oal_device` in bench.txt):

| Variant | What | Reads as |
|---|---|---|
| `base` | the recipe as it is | the shipped state |
| `allog` | stock settings, OpenAL's log only | the reference: backend, format, rate, period |
| `b16` | `sample-type=int16`, `output-limiter=false`, `dither=false` | plug's float→S16 moves into OpenAL's `Write` without the limiter's and dither's work |
| `f44` | dmix at 44100 (`defaults.pcm.dmix.rate 44100` in a temporary `~/.asoundrc`) and `frequency=44100` | 409 of OpenArena's 518 effects take the copy path (voice.cpp:936) instead of the cubic resampler |
| `b16f44` | both | the best free configuration, if both help |
| `p1024` | `period_size=1024` (dmix's own period) | the fixed cost per update (wakeups, `ProcessParamUpdates`) |
| `alnull` | `drivers=null`: OpenAL's null backend mixes in real time, writes nothing | base − alnull = libasound's share, alnull − nosound = OpenAL's own |
| `nosound` | the game's own switch (OpenArena and Yamagi `s_initsound 0`, ClassiCube volumes 0, Luanti `ALSOFT_DRIVERS=none`: its device fails to open) | the bound (step 0's Z) |

Either b16 or f44 may be shippable on its own, and the SPU path's gain should be measured
against the best of them.

**Frame rates** (on the PS3, `make bench-push` first, the protocol as for every suite run):

    /var/tmp/bench/run.sh suite "base b16 f44 b16f44 p1024 alnull nosound" 3

**1. Profile split.** Install our build of OpenAL Soft with its debug symbols: `libopenal1`,
`libopenal-data` and `libopenal1-dbgsym`, 1:1.25.2-1+ps3.N (without dbgsym libopenal's
internal functions are `[unknown]`, as in step 0). Then one profiled run per game, from
`SOUND_AT` seconds after the start (OpenArena 25, the others 15) for 20 s, with perf on the
game's process only (`-p`, never `-a`):

    for g in openarena yquake2 luanti classicube; do
        OAL_PERF=20 /var/tmp/bench/run.sh bench $g allog 1; done
    /var/tmp/bench/oal-perf.sh /var/tmp/ps3-bench/*-allog

(b16 or f44 in place of allog profiles those.) ClassiCube's run probably has no voice playing:
it shows the idle device's cost, not the mixing.

`OAL_PERF` makes the openal.inc variants run, in the background of the run,
`sudo -n timeout 40 perf record -q -F 999 -g -p <pid> -o $BENCH_DIR/perf.data -- sleep 20`.
oal-perf.sh prints for each run the shares of all the game's samples by DSO, by thread
(`alsoft-mixer` is OpenAL's), libopenal by symbol, the groups below, and the verdict. By hand,
for a game already running:

    p=$(pgrep -n -x ioquake3)    # quake2, ClassiCube, luanti
    sudo timeout 40 perf record -q -F 999 -g -p $p -o /dev/shm/oal.data -- sleep 20
    sudo chown tibi: /dev/shm/oal.data
    R="perf report -i /dev/shm/oal.data --stdio -g none --percentage absolute"
    $R --no-children --sort dso | head -15
    $R --no-children --sort comm | head
    $R --no-children --sort sym --dsos libopenal.so.1.25.2 | head -40
    $R --children --sort sym --dsos libopenal.so.1.25.2 | grep -E 'Voice::mix\(|BFormatDec|renderSamples'

(`perf report` as the owner of the file, not under sudo.) Read off, as shares of **all** the
game's samples:
- **voice mixing**: `Voice::mix` *inclusive* (the `--children` column: with the libc copies and
  fills it calls), and as a cross-check the flat sum of `Voice::mix`, `Resample_*_C`, `Mix_C`,
  `LoadSamples`/`LoadBuffer*` if not inlined. The flat `Mix_C` also holds the ambisonic decode
  (`BFormatDec::process` calls it, section 1): subtract `BFormatDec::process`'s inclusive share;
- parameter updates (stay on the PPE): our -O2 build inlines `ProcessContexts`,
  `ProcessParamUpdates` and `Calc*VoiceParams` into `DeviceBase::renderSamples(unsigned int)`
  (checked in the dbgsym's symbol table), so it is that function's *self* share plus
  `CalcPanningAndFilters` and `ProcessVoiceChanges`, which stay functions; `LoadSamples`,
  `Resample_*_C`, `Mix_C` and `BFormatDec::process` are functions too;
- `BFormatDec::process`, `Write<>` (stay, or move later);
- libasound (stays), the kernel (wakeups).

**Go/no-go:** build the SPU path only if voice mixing (`Voice::mix` inclusive) is ≥ 6% of a
game's samples, in the configuration we would ship (base, or b16/f44 if one wins). Step 0's
profile A can't answer it (checked 2026-10-16): all 730 samples of the mixer thread (tid 20798)
resolve to `[unknown]`, perf never mapped libopenal in that recording.

**2. Voices per update** (sizes the job; optional): two uprobes on line probes, counted with
perf stat while a game plays, then removed:

    L=/usr/lib/powerpc64-linux-gnu/libopenal.so.1.25.2
    sudo perf probe -x $L -a 'upd=alu.cpp:2406' -a 'vmix=alu.cpp:2197'
    sudo perf stat -e 'probe_libopenal:*' -p $(pgrep -n -x ioquake3) -- sleep 20
    sudo perf probe -d 'probe_libopenal:*'

vmix / upd is the voices mixed per update. (2406 is `ProcessContexts`' call in
`renderSamples`, 2197 the `voice->mix` call; if perf can't place a line, `perf probe -x $L -L
ProcessContexts` lists the lines it can, with the source from `apt source openal-soft`.)

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
| SPU: window in (~1.9 KB: 941 frames of mono i16 at 44.1 kHz for 1024 out), convert | ~0.3 µs | |
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
   - `prepareMix()`: 706-761, the eligibility test, the source window resolved into segments;
   - `mixCore()`: 762-1074, unchanged C (it ends by setting `IsFading`, and `MixLine` updates
     the current gains in place: both are part of what the SPU hands back);
   - `finishMix()`: 1076-1193, unchanged (the Stopping exit at 1077-1082 included).

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

  Slots: a ring of 3, ~45 KB each (3 lines + 64 voices of results), touched once. A timed
  `speq_wait` and a no-spin mode are being added to libspeq (2026-10-17). After 3 consecutive
  failures the process stays on
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
5. **SPE sharing with DXT and fluidsynth** (checked against ps3-spu/lib 2026-10-17):
   - A queue runs one SPU program (`speq_open(prog)`, jobs dispatched by `op` into that
     program's `speq_ops[]`), one worker per SPE unless asked for fewer. Mesa's S3TC queue
     opens one per SPE (lib/s3tc.c:114, `nworkers` 0). A second queue in the same process (audio,
     or fluidsynth's synth) makes more running contexts than the 6 SPEs while both are busy, and
     spufs then time-slices them in 80 ms slices: far past any audio deadline. Parked workers
     (SPU stopped, PPE thread asleep) don't compete; only running ones do.
   - **DXT jobs are not short.** `spu_encode` cuts one image into one job per worker (rows /
     nworkers, at least `JOB_MIN_PX` 16384 pixels, s3tc.c:170-174): at 379 Mpixel/s per SPE a 512×512 RGBA texture is ~0.12 ms per
     job, 1024×1024 ~0.46 ms, 2048×2048 ~1.8 ms. All six workers take the same image at once,
     so an audio job queued behind waits about one DXT job, not "~45-100 µs" (the earlier
     figure here), and during a level load it waits that long for every upload. Against the
     2 ms timeout a 2048² texture already means a PPE fallback, and the "3 consecutive
     failures, PPE for good" rule would switch the SPU path off at the first level load.
   - **fluidsynth** (the other audio service, its own agent's design): a synth job per audio
     period, maybe split across SPEs, on its own deadline. A process can have both (a game
     with OpenAL effects and fluidsynth MIDI through SDL_mixer: Woof is one), and with the
     plan's "voices split across SPEs" a synth block briefly takes several workers at once.
   - What follows for the design:
     - **one process-wide queue for all services** (one SPU program with the dxt, mix and synth
       ops; their LS buffers can overlay, a worker runs one job at a time; code ~20 KB each),
       not a queue per service: never more contexts than SPEs;
     - **two job classes**: deadline jobs (mix, synth) are claimed before bulk ones (dxt), e.g.
       a second ring the workers check first; without that, bulk jobs cut to a bounded length
       (≤ ~200 µs) whenever an audio service is open;
     - **a timeout caused by queueing is not a failure**: only EFAULT/EIO/EPROTO count towards
       "PPE for good"; a timed-out update falls back for that update only;
     - **submit never blocks the mixer thread**: a full ring (64 jobs) returns -EAGAIN instead
       of waiting for room;
     - **keep one worker resident while an audio service is active** (the per-queue idle
       threshold becomes per class, ≥ 3 periods);
     - SPEQ_DEBUG per op: queue wait, run time and fallbacks, so a late update says which
       service it waited for.
     The two-queue alternative (audio's own one-worker queue, DXT on `nspe - 1`) keeps the LS
     budgets apart but needs DXT to know about audio; the main session decides, with the
     fluidsynth design in hand.
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
- **Game:** `run.sh suite` with a variant `spu` (to add to openal.inc: `SPEQ_MIX=spu`) interleaved with
  base, the best of b16/f44, and nosound, 5 rounds, in every OpenAL game of the suite;
  `OAL_PERF` and oal-perf.sh for the per-thread split. Success:
  - fps ≥ A + 3;
  - `alsoft-mixer` ≤ 3% of samples;
  - no ALSA underruns in the log and zero fallbacks in a bot match;
  - the user hears no difference.

## 4. Implementation plan

1. **M0** (no code): the profile split and the variants of section 2 across the OpenAL games,
   recorded in SPU-OFFLOAD-SURVEY.md. Stop here if voice mixing is under 6% in every game.
2. **libspeq (ps3-spu), coordinated by the main session:**
   - `speq_wait` with a timeout and a no-spin flag (or the service uses internal ones);
   - a per-queue idle threshold;
   - the op: `spu/mix-op.c` with a host build;
   - `lib/mix.c` for `speq_mix_v1` (policy, slots, touch, fallback counting, `SPEQ_MIX`);
   - `speq-mix.h` (descriptor ABI) in libspeq-dev;
   - the queue-sharing decision (risk 5);
   - host tests in `make -C ps3-spu test`.
3. **Package `openal-soft`**: done 2026-10-17, unpatched (`openal-soft/package.conf`:
   `TYPE=debsrc`, `VERSION=1:1.25.2-1`, the `DSC_SHA256` above, `BUILD=native`; in the
   Makefile's `SRC_PACKAGES`), so the PS3 can run our build with matching debug symbols for M0.
   Still to do:
   - `make openal-soft-work` / `-patches`;
   - **one patch** "voice mixing through libspeq on the Cell's SPEs": the `Voice::mix` split,
     the `ProcessContexts` hook and `core/mixer/mixer_speq.cpp` (dlopen, descriptor fill),
     built only on ppc64;
   - no Debian packaging change (`dh-sequence-dlopenlibdeps` already handles dlopen notes;
     libspeq stays a soft dependency like Mesa's);
   - the version is `1:1.25.2-1+ps3.N`, and the PS3 gets it from our repository instead of
     Debian's.
4. **PS3:** the loopback comparison, then the game runs above. Ship if they pass.
5. **Later, only if needed:**
   - post-process and S16 conversion in the job, with int16 output;
   - AltiVec mixers for the fallback;
   - (d') ADPCM effects in ioquake3.
