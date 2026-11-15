# Backlog: the SPEs

SPU work not done yet. What runs on the SPEs today (libspeq's DXT1/DXT5 encoders, fluidsynth's voice
DSP on the shared 2-SPE deadline pool, [BananaBread](../../gamedb/games/bananabread/)'s optional skinning) is in [SPU.md](../SPU.md)
and [SPU-AUDIO.md](../SPU-AUDIO.md). The 7th SPE is on by default ([SPU.md](../SPU.md#the-7th-spe)). What is left:
the audio libraries, and the work that waits on probe E16. Items are in rank order; the rank is the one in [the ranked list](README.md).
Terms: [glossary](../README.md#glossary).

Two items are one game's each and live in its GameDB backlog (`gamedb/games/<id>/todo.toml`): why
[dsda-doom](../../gamedb/games/dsda-doom/)'s music still chops with fluidsynth's voices on the SPEs (rank 10; [OpenTTD](../../gamedb/games/openttd/)'s MIDI shares the
path, decision D3 waits on it), and SPU meshing for [Luanti](../../gamedb/games/luanti/) and for [ClassiCube](../../gamedb/games/classicube/) (rank 49; it needs E16).

Effort: S = a day or less, M = days, L = weeks.

### Audio libraries on the PPE

Rank 45. What stays on the PPE of the games' sound, made cheaper there (built, not run): OpenAL
Soft's mixer and libvorbis's `ov_read` without integer/float round trips through memory, OpenAL's
output as 16-bit, fluidsynth's Dattorro reverb without a divide per sample, and switches for the
choices that change the sound (`OAL=lin`, `OAL=f44`, `FLUIDFX=...`).

**Why.** Audio is 11.9% of a game's CPU on average in the SSD-root profiles (5 of 7 games). The
PPE pays about 40 cycles for every int/float conversion (a load-hit-store); under qemu with a
counter for them, they were half of OpenAL's mixer, a third of a Vorbis decode, and the reverb's
divides 40% of [OpenTTD](../../gamedb/games/openttd/)'s reverb ([SPU-AUDIO.md](../SPU-AUDIO.md#openal-soft-on-the-ppe-conversions-and-output-format)).

- **Payoff:** low to medium: [OpenArena](../../gamedb/games/openarena/) 3-5% of its CPU, [ClassiCube](../../gamedb/games/classicube/) 2-3% plus ALSA's 1.9% float
  conversion, OpenTTD 1.7-2.3%, [SuperTuxKart](../../gamedb/games/supertuxkart/) 0.6-0.9 s of load; all estimates from qemu counts.
- **Effort:** S (built). **Risk:** low: each patch is bit-identical to the scalar code it replaces;
  the 16-bit default sounds the same (no limiter, no dither, as the float output had).
- **Needs:** PS3 (same-boot, the stock libraries through `LDPATH=`), the user for the variants that
  change the sound.
- **Status:** measured (ssdroot5: OpenAL +2% on OpenArena, FluidSynth's reverb +3%, SuperTuxKart's
  load -0.7 s from libvorbis) and the defaults since 2026-10-20: libopenal1, libvorbis0a/libvorbisfile3
  and libfluidsynth3 +ps3.549 replace Debian's on the PS3 and in the image. Left: the stable publish.
- **Source:** PLAN 3.9; [SPU-AUDIO.md](../SPU-AUDIO.md).

### libspeq leftovers and SPU work after E16

Rank 64. Small runtime leftovers, and the uses that wait for SPU DMA into VRAM.

- From the queue work: the stop 0x2100/0x2101 printf server and `spurun` on libspeq are not done
  (the timeout, the no-spin wait, `speq_tune` and the bulk and deadline queues are). Per-worker
  doorbells are not needed with one or two workers per deadline queue.
- Checks never run on the PS3: the deadline pool's and skinning's own PS3 steps (`packages/walnut/tools/ps3-spu/doc/pool.md`
  section 5), skinning on a map with bots (the HUD gun alone shows little), skinning's early submit
  (begin and adds at `endmodelbatches`, end at the draw; the API allows it, cube2's hook doesn't do
  it), RGB DXT compression in a real game, and a [Teeworlds](../../gamedb/games/teeworlds/) log line showing its DXT5 path.
- Audio side work: reverb and chorus on a resident worker (their state is serial), and AltiVec
  `Mix_VMX`/`Resample_Cubic_VMX` for OpenAL Soft's PPE mixer (SPU-AUDIO's design notes; the mixer is
  3.7-4.7% of a game, so small).
- The toolchain: the SPU backend still uses GCC's old reload pass; converting it to LRA is about
  4-8 weeks and only forced when GCC deletes reload ([SPU-GCC.md](../SPU-GCC.md)).
- If E16 shows GB/s: SPU decompression and generation straight into VRAM (assets stay compressed in
  RAM and on disk; estimated 2-4x less RAM and USB I/O for them), procedural textures and meshes
  from seeds, [ClassiCube](../../gamedb/games/classicube/)'s chunk meshing into VBOs.
- Later still (HARDWARE-LEVERAGE.md, deferred): SPU culling and vertex work, SPU post-processing
  (MLAA, about 10 ms of SPU time per 720p frame in the PS3 games that did it). The readback for
  post-processing must go through M2MF.

- **Effort:** M-L. **Needs:** E16 first. **Status:** never tried.
- **Source:** [archive/SPU-OFFLOAD-PLAN.md, "Parked"](../archive/SPU-OFFLOAD-PLAN.md#parked-in-the-order-they-would-come-back) 4-6; [archive/HARDWARE-LEVERAGE.md, "3. SPU + RSX together"](../archive/HARDWARE-LEVERAGE.md#3-spu--rsx-together-as-ps3-games-did).

### The reserved SPE's busy percentage, and a shared SPE scheduler

Not ranked yet (the README's "Added 2026-11-15" table).

**Why.** Two asks from 2026-11-14. First, the user asked how much of the time the reserved SPE is
busy. The answer was qualitative (the other five SPEs are mostly idle, nothing needs a sixth); no
measured busy percentage exists. Second, the user asked for "a serious spe scheduler so that both
kernel and user programs share them". It gains nothing today. On 2026-11-15 the user moved SPE
utilisation to the backlog until games use the SPUs near 100%.

- **The measurement:** read each SPE's busy time (the SPU context's run time over a window) during a
  game on the 7-SPE kernel, 6.12.115+ps3.1554 or later, and put the figure in [SPU.md](../SPU.md#the-7th-spe).
  **Effort:** S. **Needs:** PS3.
- **The scheduler:** one scheduler for the kernel's and userspace's SPE jobs. **Trigger:** worth doing
  once something keeps the shared SPEs busy. Until then the shared 2-SPE deadline pool is enough.
  **Effort:** L. **Risk:** high (kernel code, security review).
- **Status:** not started; the scheduler waits on its trigger.

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
- **[BananaBread](../../gamedb/games/bananabread/)'s skinning on the SPEs** (measured): libspeq's PPE path 28.1 fps with no stalls, the
  SPU path 23.6 with 21 stalls (4.3 s). cube2 keeps CPU skinning; `SPEQ_SKIN` selects.
- **Vorbis or zlib on the SPEs**: [OpenArena](../../gamedb/games/openarena/)'s effects are WAV and its music is already decoded per
  4 KB buffer; inflate is serial and branchy (libdeflate on the PPE instead).
- **[OpenTTD](../../gamedb/games/openttd/)'s blitter on the SPEs**: its OpenGL driver is built and runs.
- **The validator, zram or VRAM swap copies on the SPEs**: the kernel can't drive SPEs, and
  validation must stay in the kernel to be trusted.
- **Mesa's per-draw paths on the SPEs**: microsecond-sized, branchy, needed at once.
- **Compressed swap through a userspace block device** (ublk with SPU LZ4): userspace swap servers
  deadlock under reclaim unless fully mlocked; ps3vswap gives the space without that risk.
- **Software rendering on the SPEs**: the RSX exists.

Older versions and the dated history: [archive/SPU-OFFLOAD-PLAN.md](../archive/SPU-OFFLOAD-PLAN.md),
[archive/SPU-AUDIO.md](../archive/SPU-AUDIO.md), [archive/SPU-OFFLOAD-SURVEY.md](../archive/SPU-OFFLOAD-SURVEY.md),
[archive/SPU-OFFLOAD.md](../archive/SPU-OFFLOAD.md), [archive/IDEAS.md](../archive/IDEAS.md).
