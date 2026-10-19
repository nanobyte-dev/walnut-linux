# Backlog: the SPEs

SPU work not done yet. What runs on the SPEs today (libspeq's DXT1/DXT5 encoders, fluidsynth's voice
DSP on the shared 2-SPE deadline pool, Sauerbraten's optional skinning) is in [SPU.md](../SPU.md)
and [SPU-AUDIO.md](../SPU-AUDIO.md). What is left: the cause of dsda-doom's chopping music, a 7th
SPE, meshing, and the work that waits on probe E16. Items are in rank order; the rank is the one in
[the ranked list](README.md). Terms: [glossary](../README.md#glossary).

Effort: S = a day or less, M = days, L = weeks.

### dsda-doom's chopping music

Rank 10. Why the music still chops with fluidsynth's voices on the SPEs.

**Why.** fluidsynth's voice DSP on the SPEs cut dsda-doom's CPU from 44.5 to 31.8 s a minute and
about halved the underruns; the user, listening: "night and day". On libspeq 0.367's shared pool
it is 465 underruns a minute against 1095 on the PPE, 0 timeouts, 0.84 ms mean wait (row 52). The
music still chops audibly. Whether the rest is the pool's wait, SDL's audio buffer or the game's own
frame stalls is not established. OpenTTD's music (3893 underruns, choppy) and every SDL_mixer MIDI
user share the path.

- **Payoff:** medium: two games' music, and decision D3 (dsda-doom's music default: fluidsynth on
  the SPEs, OPL, or off) waits on it.
- **Effort:** S-M. **Risk:** low (the PPE fallback per round stays).
- **Needs:** PS3, the user by ear.
- **Status:** never tried beyond the underrun counts. The listening part is scheduled: TESTS.md
  "Next" (sound by ear, with the user).
- **Source:** PLAN 2.8; [archive/SPU-AUDIO.md, "(e) Second version"](../archive/SPU-AUDIO.md#e-second-version-rounds-single-precision-two-workers).

**To start.** The recipe's variants `playspu`, `playppe` and `playoff` (`SPEQ_SYNTH=spu|ppe|off`)
interleaved with `playnomusic`, `SPEQ_DEBUG=1` in the log (rounds, waits, timeouts), and
`perf record -p` on the dsda-doom process with `libfluidsynth3-dbgsym` to see what is left on the
PPE. Then the same with a larger SDL audio buffer, and the stall recorder (`STALLS=1`) to line up
underruns with frame stalls.

### The 7th SPE

Rank 21. One more SPE for Linux by a boot parameter, `spu_base.enum_shared=1`, in a second Petitboot
entry.

**Why.** Linux gets 6 SPEs (`spu_base.enum_shared=0`): 4 for libspeq's bulk DXT queue, 2 for the
deadline pool (audio, skinning). A 7th would go to the pool or to DXT. Nothing is written anywhere:
the off switch is the other boot entry. It is the only hardware unlock on FLASH-SYSCON.md's list
that costs nothing.

- **Payoff:** medium, free. **Effort:** S. **Risk:** low.
- **Needs:** the user (a reboot into the new entry), then PS3.
- **Status:** waiting on decision D6; scheduled: TESTS.md "Next" (with the user).
- **Source:** PLAN 4.2; [archive/FLASH-SYSCON.md, "6. Recommendation, ranked"](../archive/FLASH-SYSCON.md#6-recommendation-ranked) 1.

**To start.** A Petitboot entry with the extra argument (an `/etc/kboot.d/*.conf` file, which
`ps3-update-kboot` keeps); after the boot `ls /sys/devices/system/spu` shows 7. In that boot,
`speq-bench` and the DXT tests with the default 6-way split against a split that uses the 7th, and
`ps3-gameos-mount` to see nothing we use breaks.

### OpenAL Soft's free variants

Rank 45. OpenAL Soft configured cheaper, no code: `b16` (int16 output, no limiter, no dither),
`f44` (44.1 kHz device and dmix), `p1024`, and `alnull` to split off libasound's share.

**Why.** OpenAL Soft is 3.7-4.7% of a game's samples (OpenArena 4.7% plus libasound 2.8%, ClassiCube
3.7%), under the 6% bar for an SPU mixer. The variants could take part of that for free: 409 of
OpenArena's 518 effects would take the copy path at 44.1 kHz instead of the cubic resampler. The
variants exist in the recipes (`games/bench/recipes/openal.inc`) and never ran for frame rates.

- **Payoff:** low (at most part of 3.7-4.7%). **Effort:** S. **Risk:** none for the PPE; `b16`
  changes the output, so the user listens.
- **Needs:** PS3, the user by ear. **Status:** never tried.
- **Source:** PLAN 3.9; [archive/SPU-AUDIO.md, "2. Measurements before any code"](../archive/SPU-AUDIO.md#2-measurements-before-any-code-m0-free).

**To start.** `/var/tmp/bench/run.sh suite "base b16 f44 b16f44 p1024 alnull nosound" 3`; the profile
split per game with `OAL_PERF=20 /var/tmp/bench/run.sh bench <game> allog 1` and
`/var/tmp/bench/oal-perf.sh`.

### SPU meshing for Luanti and ClassiCube

Rank 49. Block meshes and chunk builds as SPU jobs.

**Why.** Luanti's map generation burns 8.4 s on its emerge thread during the load and its mesh
thread 8.9 s in the warm-up (batch 17); ClassiCube's `Builder_MakeChunk` is its biggest load item.
The work is data-parallel and asynchronous already, the shape SPU jobs suit. Writing the meshes
straight into VBOs needs E16 ([graphics.md, "RSX and SPU probes: E8b, E16, E17"](graphics.md#rsx-and-spu-probes-e8b-e16-e17)).

- **Payoff:** medium (Luanti's load stalls, ClassiCube's chunk builds). **Effort:** L.
- **Risk:** medium (engine patches). **Needs:** host, PS3. **Status:** never tried.
- **Source:** PLAN 3.12; [archive/SPU-OFFLOAD-PLAN.md, "Parked"](../archive/SPU-OFFLOAD-PLAN.md#parked-in-the-order-they-would-come-back) 6.

### libspeq leftovers and SPU work after E16

Rank 64. Small runtime leftovers, and the uses that wait for SPU DMA into VRAM.

- From the queue work: the stop 0x2100/0x2101 printf server and `spurun` on libspeq are not done
  (the timeout, the no-spin wait, `speq_tune` and the bulk and deadline queues are). Per-worker
  doorbells are not needed with one or two workers per deadline queue.
- Checks never run on the PS3: the deadline pool's and skinning's own PS3 steps (`ps3-spu/doc/pool.md`
  section 5), skinning on a map with bots (the HUD gun alone shows little), skinning's early submit
  (begin and adds at `endmodelbatches`, end at the draw; the API allows it, cube2's hook doesn't do
  it), RGB DXT compression in a real game, and a Teeworlds log line showing its DXT5 path.
- Audio side work: reverb and chorus on a resident worker (their state is serial), and AltiVec
  `Mix_VMX`/`Resample_Cubic_VMX` for OpenAL Soft's PPE mixer (SPU-AUDIO's design notes; the mixer is
  3.7-4.7% of a game, so small).
- The toolchain: the SPU backend still uses GCC's old reload pass; converting it to LRA is about
  4-8 weeks and only forced when GCC deletes reload ([SPU-GCC.md](../SPU-GCC.md)).
- If E16 shows GB/s: SPU decompression and generation straight into VRAM (assets stay compressed in
  RAM and on disk; estimated 2-4x less RAM and USB I/O for them), procedural textures and meshes
  from seeds, ClassiCube's chunk meshing into VBOs.
- Later still (HARDWARE-LEVERAGE.md, deferred): SPU culling and vertex work, SPU post-processing
  (MLAA, about 10 ms of SPU time per 720p frame in the PS3 games that did it). The readback for
  post-processing must go through M2MF.

- **Effort:** M-L. **Needs:** E16 first. **Status:** never tried.
- **Source:** [archive/SPU-OFFLOAD-PLAN.md, "Parked"](../archive/SPU-OFFLOAD-PLAN.md#parked-in-the-order-they-would-come-back) 4-6; [archive/HARDWARE-LEVERAGE.md, "3. SPU + RSX together"](../archive/HARDWARE-LEVERAGE.md#3-spu--rsx-together-as-ps3-games-did).

## Parked

### Video decoding on the SPEs

Not ranked: parked by CLAUDE.md ("Park side projects (... HEVC decoder ...) unless asked").
MPEG-2 first (IDCT, motion compensation; 4-8 weeks), then H.264 (months); an 8-bit 1080p30 HEVC
decoder looks feasible but is months of work. The RSX has no usable video decode block; GameOS
decodes on the SPEs through Sony's VDEC. Prior art: spu-medialib (2007), an SPU MPEG-2 IDCT whose
author claimed 720p with near-zero PPE load. High value for the desktop's video, high cost.
Source: [archive/SPU-OFFLOAD.md, "Ranked candidates"](../archive/SPU-OFFLOAD.md#ranked-candidates) 2,
[archive/RESEARCH.md, "Jellyfin / video (parked)"](../archive/RESEARCH.md#jellyfin--video-parked).

## Not now, and why

- **OpenAL Soft's mixer on an SPE.** M0 measured OpenAL Soft at 3.7-4.7% of a game's samples, under
  the 6% bar, so no SPU mixer. It comes back if voice mixing (`Voice::mix` inclusive) reaches 6% of
  a game's samples in the configuration we ship. The design (one libspeq job per mixer update,
  `speq_mix_v1` on the shared deadline pool) is in
  [research/OPENAL-SPU-MIXER.md](../research/OPENAL-SPU-MIXER.md).
- **Sauerbraten's skinning on the SPEs** (measured): libspeq's PPE path 28.1 fps with no stalls, the
  SPU path 23.6 with 21 stalls (4.3 s). cube2 keeps CPU skinning; `SPEQ_SKIN` selects.
- **Vorbis or zlib on the SPEs**: OpenArena's effects are WAV and its music is already decoded per
  4 KB buffer; inflate is serial and branchy (libdeflate on the PPE instead).
- **OpenTTD's blitter on the SPEs**: its OpenGL driver is built and runs.
- **The validator, zram or VRAM swap copies on the SPEs**: the kernel can't drive SPEs, and
  validation must stay in the kernel to be trusted.
- **Mesa's per-draw paths on the SPEs**: microsecond-sized, branchy, needed at once.
- **Compressed swap through a userspace block device** (ublk with SPU LZ4): userspace swap servers
  deadlock under reclaim unless fully mlocked; ps3vswap gives the space without that risk.
- **Software rendering on the SPEs**: the RSX exists.

Older versions and the dated history: [archive/SPU-OFFLOAD-PLAN.md](../archive/SPU-OFFLOAD-PLAN.md),
[archive/SPU-AUDIO.md](../archive/SPU-AUDIO.md), [archive/SPU-OFFLOAD-SURVEY.md](../archive/SPU-OFFLOAD-SURVEY.md),
[archive/SPU-OFFLOAD.md](../archive/SPU-OFFLOAD.md), [archive/IDEAS.md](../archive/IDEAS.md).
