# Backlog: RAM and VRAM

The memory cuts not made yet, the kernel memory options, the VRAM swap's reserve and texture
compression. RAM (about 217 MB for Linux) is still the limit for the heavy games, and VRAM counts
as memory here because the VRAM swap (ps3vswap) keeps swapped pages in the VRAM games don't use.
Items are in rank order; the rank is the one in [the ranked list](README.md). Terms:
[glossary](../README.md#glossary).

Effort: S = a day or less, M = days, L = weeks. "PS3" means a run with nobody at the console,
"user" the user at the console or TV.

Where the heavy games stand (batch 16-17 and ssd1, fps.csv rows 68-70): BananaBread (Debian's cube2) 117 MB of VRAM
and 16-24 MB of RAM free at the lowest; OpenArena gl2min 117 MB (97.7 with packed MD3 frames) and
9-20 MB free; SuperTuxKart `lean` 95-96 MB and 22-27 MB free; Luanti 60-66 MB and 12.6-18 MB free.

## VRAM

### Texture compression at upload for every game

Rank 3. Mesa 0038, `NV30_FORCE_DXT=off|opaque|all`: plain RGB(A)8 uploads stored as DXT1 (opaque)
or DXT5 (with alpha), compressed by libspeq on the SPEs.

**Why.** Most games upload plain RGB(A)8, which the RSX stores at 4 bytes a texel; DXT1 takes an
eighth of that, DXT5 a quarter. The heavy games are VRAM-bound (above), and every MB of textures is
a MB the VRAM swap moves to the disk. libspeq compresses RGB sources on 4 SPEs in 1.0 ms per
1024x1024 against about 175 ms on the PPE (row 59), so the load cost is gone. OpenArena gl2min and
SuperTuxKart already ask for compression themselves and BananaBread's cube2 compresses from 1024
up, so `opaque` takes only the rest there; Luanti and ClassiCube upload everything plain and should
show the most.

- **Payoff:** high for the VRAM-bound games: texture VRAM /4 to /8.
- **Effort:** M (built). **Risk:** medium: DXT is lossy (blockiness on UI, banding on skies);
  textures updated off the 4x4 grid go back to uncompressed (`fdxt_unforced`); SuperTuxKart and
  Luanti make their mipmaps on the CPU with it (`fdxt_mipgen_ms`). NPOT textures are left alone
  because nv30's NPOT DXT layout has never run.
- **Needs:** PS3, then the user looks at the pictures before a game gets it in its launcher.
- **Status:** built (Mesa .466~dev, official .476), default off; host checks pass (5105 policy
  checks, `gl-forcedxt` 128 checks per mode on softpipe in the ppc64 chroot). Measured on the PS3 in
  batch ssdroot2 P2 (`opaque` against off, x2): BananaBread peak VRAM 117 to 73-78 MB with no fps
  change resolved and a slightly softer picture; SuperTuxKart `lean` and OpenArena gl2min force
  0 textures ([MEMORY.md](../MEMORY.md#measured-on-the-ps3-batch-ssdroot2-p2)). Left, in TESTS.md
  "Next": Luanti and ClassiCube (the games that upload everything plain), `NEAREST`, SuperTuxKart
  `all`, OpenArena `gl2min_tc0`, and why OpenArena's 2238 uploads are all skipped for format.
- **Source:** PLAN 2.4, [archive/MEMORY.md, "Texture compression at upload"](../archive/MEMORY.md#texture-compression-at-upload-mesa-0038-nv30_force_dxt-2026-10-18).

**To start.** On the PS3 first the GL test against softpipe:
`for m in off opaque all; do NV30_FORCE_DXT=$m gl-forcedxt; done`, and the same with
`LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe`. Then one run with the per-decision lines, then
the A/B with the stats line in every arm:

    S=NV30_FORCE_DXT_STATS=50
    ENVS="$S+NV30_FORCE_DXT=opaque+NV30_FORCE_DXT_DEBUG=1" /var/tmp/bench/run.sh bench luanti base 1
    MEM=1 STALLS=1 ENVS="$S $S+NV30_FORCE_DXT=opaque" /var/tmp/bench/run.sh bench sauerbraten base 2

Read `vram` and `mem_gpu_vram`, `fdxt_raw_mb` against `fdxt_dxt_mb`, `fdxt_skip`,
`fdxt_unforced`, `fdxt_mipgen_ms`, `load`, `stall_play_s`, `low1`, `mem_swap`. Add `SPEQ_DEBUG=1`
in one run: no game has exercised libspeq's RGB path yet (BananaBread's demo uploads none), and
Teeworlds' DXT5 has never shown a `SPEQ` line.

### VRAM budget in game mode

Rank 4. Which path a game short of VRAM takes, then a VRAM swap reserve that game mode shrinks or
caps.

**Why.** The VRAM swap may hold up to 96 MB (`vswap_vram_mb`) of the ~245 MB of VRAM. A buffer that
finds no VRAM first makes the swap hand chunks back (`ps3rsx_vswap_shrink`), and those pages go to
the disk; only if that fails does the buffer land in GART (system RAM). So a heavy game's VRAM
costs disk swap traffic first. Which of the two OpenArena opengl2 (117 MB of VRAM) and SuperTuxKart
(96 MB, "out of VRAM" lines remain) hit has never been measured. The user's decision of 2026-10-17:
a measurement first, then the reserve.

- **Payoff:** high for the heavy games' swap stalls. **Effort:** S-M.
- **Risk:** low-medium: swap traffic moves elsewhere; measure both sides.
- **Needs:** PS3, then host.
- **Status:** never tried; not in TESTS.md.
- **Source:** PLAN 2.3; [archive/GAME-PLAN.md, "Next phase"](../archive/GAME-PLAN.md#next-phase-from-playable-to-good); [archive/IDEAS.md, "How to start the top ones"](../archive/IDEAS.md#how-to-start-the-top-ones) step 5.

**To start.** Log `ps3rsx_vswap_shrink` calls and GART fallbacks during an OpenArena gl2min and a
SuperTuxKart `lean` run (with the kernel counters item this becomes a `ps3rsx_stats` read; until
then a trace or a debug print). Then try `vswap_vram_mb` lower (a load-time parameter, 0444: a boot
argument, or a ps3rsx reload with X stopped and `swapoff /dev/ps3vswap0`), or a game-mode hook that
writes the swap's VRAM pages out before the game loads.

## RAM

### Console frame release (local/0043) on the PS3

Rank 5. ps3fb gives its console's 8 MiB of XDR back while ps3rsx owns the GPU.

**Why.** The console frame is a 9 MiB boot allocation that nobody sees while X runs; ps3fb's blit
thread is idle during games (0 CPU s over 20 s). local/0043 frees 8192 KiB of it (the first MiB is
IO page 0, the FIFO and reset program, and stays) 3 s after X takes the GPU, and takes it back in 1
MiB chunks when the console is shown again.

- **Payoff:** medium: +8 MB of `MemAvailable` during every game (`MemTotal` +8192 kB after the
  first release).
- **Effort:** S (built). **Risk:** medium: if lv1 refuses the read-only remap of the frame's IO
  pages, nothing is freed (dmesg `iomap of IO page`); the console shows ps3fb's last DDR frame until
  its memory is back; while the console is visible its chunks may come from the 64 MiB CMA area,
  lowering the GART ceiling by up to 8 MiB; fbcon then draws through 4K vmalloc mappings (cost not
  measured).
- **Needs:** PS3 for the memory steps and the game A/B (VT switches by `chvt`), the user for the
  panic step.
- **Status:** built, in the running kernel .472, switch `ps3fb.shadow_release` (off; runtime at
  `/sys/module/ps3fb/parameters/shadow_release`). The test plan has never run; not in TESTS.md.
- **Source:** PLAN 1.9; [archive/KERNEL-MEMORY.md, section 6](../archive/KERNEL-MEMORY.md#6-ps3fbs-console-shadow-while-x-owns-the-display-local0043-switch-off-by-default).

**To start.** KERNEL-MEMORY.md's test plan in one boot: meminfo (`MemTotal`, `MemFree`,
`MemAvailable`, `CmaFree`) at the text console and with X idle; `echo 1 >
/sys/module/ps3fb/parameters/shadow_release` (expect `console shadow released ... (8192 KiB)` and
no `iomap of IO page ... failed`); cycle 0/1 three times; ten VT switch rounds, also with a game
running; X exit and a pattern written through `/dev/fb0`; a process holding `/dev/fb0` open blocks
the release; a ps3rsx reload; then the 5-10 game suite with the switch on against off in the same
boot (`MemAvailable` +8 MB, fps unchanged, less swap); reboot and power off with the console
released. The panic step (`echo c > /proc/sysrq-trigger`) only with the user's OK. When it passes:
`ps3fb.shadow_release=1` in `packages/patched/kernel/linux/flavours/*.args`.

### The 720p console

Rank 24. `ps3fb=5M` with mode 131: 4 MB less RAM for good.

**Why.** The console frame needs 9 MiB at 1080p and 5 MiB at 720p. The HDMI mode is shared: ps3fb
sets it and ps3rsx's KMS follows, so X, the desktop and the games run at 720p too (2.25x fewer
pixels a frame, which is also fps). Whether the TV's scaling is acceptable is the user's call.
If local/0043 (above) works, the console frame is mostly freed while X runs anyway, so the 720p
console would save RAM mainly while the text console is visible; its other gain is the pixels
(inference, not measured).

- **Payoff:** medium (4 MB always, and fewer pixels). **Effort:** S (boot arguments).
- **Risk:** the picture at 720p. **Needs:** the user's decision, a reboot.
- **Status:** waiting on decision D2.
- **Source:** PLAN 4.10, D2; [archive/KERNEL-MEMORY.md, "ps3fb: how big must it be"](../archive/KERNEL-MEMORY.md#ps3fb-how-big-must-it-be-and-can-the-console-move-to-vram).

### OpenArena opengl2: what is left

Rank 20. The renderer's texture reloads, the hunk's high water, a malloc trim on top.

**Why.** Batch 17 took gl2min to 57-60 fps with 0.7-2.6 s of gameplay stalls (ioquake3 462's SNORM
colours, row 69), but its RAM is still the lowest of the suite (9-20 MB free at the lowest, rows 68-70). Left:

- renderergl2 has no `r_keepImages` (0025 is opengl1's), so every demo pass and map change reloads,
  decodes and mipmaps every texture: `R_MipMapsRGB` was 7.8% of a profiled window with three
  reloads, `COM_ParseExt` (the .shader scripts, at every load) 2.8%;
- after a map load, `madvise(MADV_DONTNEED)` on the page-aligned gap between `hunk_low.temp` and
  `s_hunkTotal - hunk_high.temp` in `s_hunkData` (code/qcommon/common.c, next to
  `Com_TouchMemory`), behind a cvar `com_hunkTrim` (default 0): 2.7 MB in opengl1;
- MEMORY.md's second A/B step, `MALLOC=trim` on top of gl2min and `gl2min_md3pack`, never ran.

- **Payoff:** a few MB and shorter reloads. **Effort:** S-M.
- **Risk:** low. **Needs:** host, PS3. **Status:** never tried.
- **Source:** PLAN 3.7; [archive/MEMORY.md, "Proposed, not implemented"](../archive/MEMORY.md#proposed-not-implemented).

### Sound straight to ALSA

Rank 36. The games' sound without ALSA's pulse plugin and the PulseAudio server.

**Why.** Every game maps libpulse, libpulsecommon and its codecs (libsndfile, FLAC, opus, vorbis,
mpg123) and dbus/systemd through ALSA's pulse plugin: 0.5-3.1 MB of RSS per game (mostly shared
clean pages), a "threaded-ml" thread in the OpenAL games, and a pulse server outside the game. The
desktop keeps its sound (CLAUDE.md "Memory trims"): this would be a game-mode or per-game setting.

- **Payoff:** 0.5-3 MB per game plus the server. **Effort:** M.
- **Risk:** underruns (measure them). **Needs:** PS3. **Status:** never tried.
- **Source:** PLAN 3.6; [archive/MEMORY.md, "Shared by every game"](../archive/MEMORY.md#shared-by-every-game), cut #11.

### ADPCM sound effects in ioquake3

Rank 38. OpenArena's effects stored as 4-bit ADPCM, which OpenAL Soft decodes at mix time.

**Why.** OpenArena's ~7 MB of decoded sound is WAV effects (518 files, all mono PCM; only the 7
music tracks are Ogg, already streamed in 4 KB buffers). OpenAL Soft accepts `AL_FORMAT_MONO_IMA4`
and `AL_FORMAT_MONO_MSADPCM_SOFT` (al/buffer.cpp:645-646) and decodes them when it mixes
(`LoadSamples<IMA4Data>`, voice.cpp:279). ioquake3's `S_AL_BufferLoad` would encode 16-bit effects
before `qalBufferData`: a quarter of the memory. A free step comes first and was never measured
either: `s_khz 11` halves OpenArena's decoded sounds (HARDWARE-LEVERAGE.md's per-game settings,
with ClassiCube's view distance, which decides the chunk meshes it builds on the CPU).

- **Payoff:** ~5 MB in OpenArena. **Effort:** M (an ioquake3 patch, encoding at load).
- **Risk:** 4-bit quality: the user listens. **Needs:** host, PS3, the user by ear.
- **Status:** never tried.
- **Source:** PLAN 3.9; [archive/SPU-AUDIO.md, "(d) Vorbis on an SPU: no; ADPCM effects instead (d')"](../archive/SPU-AUDIO.md#d-vorbis-on-an-spu-no-adpcm-effects-instead-d).

### Mesa built with nv30 only

Rank 39. A Mesa without the nv50 and nvc0 drivers.

**Why.** libgallium's writable segment is 1.0 MB of relocations (`.opd` 537 KiB of ELFv1 function
descriptors, `.data.rel.ro` 466 KiB, 62722 relocations), private to each process; Mesa's private
dirty is 1.4-1.5 MB per game. nv50/nvc0 code makes part of those relocations. softpipe stays: it is
the reference of the mesa-tests.

- **Payoff:** ~0.3-0.5 MB private per game (estimate, not measured), a smaller package.
- **Effort:** S-M (a Mesa meson patch). **Risk:** low.
- **Needs:** host. **Status:** never tried.
- **Source:** PLAN 3.5; [archive/MEMORY.md, "Shared by every game"](../archive/MEMORY.md#shared-by-every-game), cut #10.

### Smaller vertex data

Rank 43. Half floats or 16-bit vertex attributes, which nv30 fetches natively.

**Why.** Vertex buffers halve. Part of this is done where it removed per-draw conversions: ioquake3
0027 (SNORM colours, the default) and 0029 (packed MD3 frames, 62.2 -> 37.5 MB of VRAM on the
host, -20 MB in batch 17) and cube2 0011 (world normals as shorts). Left in BananaBread: the grass
(`grassvert.bounds`, GL_BYTE in a GART stream buffer: a per-draw conversion but no copy) and the
edit-mode prefab preview; elsewhere, float vertex data in any engine.

- **Payoff:** low-medium. **Effort:** M per engine. **Risk:** low (wrong fetches show on the webcam).
- **Needs:** host, PS3. **Status:** never tried beyond the patches named.
- **Source:** PLAN 3.9; [archive/GAME-PLAN.md, "Next phase"](../archive/GAME-PLAN.md#next-phase-from-playable-to-good); [archive/MEMORY.md, "Proposed, not implemented"](../archive/MEMORY.md#proposed-not-implemented).

### Shader IR dropped after linking

Rank 44. Mesa drops a program's compiled IR after linking and rebuilds it from the disk cache when
needed.

**Why.** Estimated at tens of MB for OpenArena opengl2 when it kept ~100-115 GLSL programs with their
source and IR; ioquake3 0001 already frees the shader objects after link, so what Mesa itself still
keeps is unknown.

- **Payoff:** low-medium now. **Effort:** M. **Risk:** medium (variant recompiles at draw time
  would be new hitches).
- **Needs:** a measurement of what Mesa keeps first (MEM=1 on OpenArena gl2min), host.
- **Status:** never tried.
- **Source:** PLAN 3.9; [archive/GAME-PLAN.md, "Next phase"](../archive/GAME-PLAN.md#next-phase-from-playable-to-good).

### Kernel config: the last few hundred KB

Rank 62. The rows of KERNEL-MEMORY.md's ranked changes that are neither done nor postponed.

**Why.** The release flavour took 8.7 MB (rows 1, 3, 4, 8, 9, 10 and 15). Left:

| Change | Saves | Cost |
|---|---:|---|
| `STACKPROTECTOR=n`, `INIT_STACK_NONE`, `RANDOMIZE_KSTACK_OFFSET=n` | 275K and a little speed | hardening lost |
| `DEBUG_FS=n` | 259K | ps3rsx's debugfs files go (check the module builds); the `ps3rsx_stats` idea needs debugfs |
| `COMPAT=n` | 197K | breaks any 32-bit ppc binary |
| `SLUB_DEBUG=n` | 99K | loses `/proc/slabinfo`: keep it for memory work |
| `LOG_BUF_SHIFT=15` | 144K more than the release flavour's | a shorter kernel log |

- **Payoff:** under 1 MB. **Effort:** S. **Risk:** as in the table.
- **Needs:** host, a reboot. **Status:** never tried.
- **Source:** [archive/KERNEL-MEMORY.md, "4. Ranked changes"](../archive/KERNEL-MEMORY.md#4-ranked-changes).

Rows 5-7 and 14 (zram off, seccomp filters, cfg80211, `dhash_entries`/`ihash_entries`) are
postponed by the user's rule; see [the ranked list](README.md#postponed-by-the-users-rules). Row 2
is the 720p console above.

## Not here

- The lv1 `gpu-4kb-iopage` patch (4 KB IO pages for the GART: a few MB at most) is
  [hardware.md, "Read-only probes of lv1 and the firmware state"](hardware.md#read-only-probes-of-lv1-and-the-firmware-state).
- Luanti's memory is [games.md, "Luanti: view40 as an option, and its memory"](games.md#luanti-view40-as-an-option-and-its-memory),
  BananaBread's `texreduce` is [games.md, "BananaBread: texreduce"](games.md#sauerbraten-texreduce).
- 64 KB IO pages for finer GART allocations are [graphics.md, "Driver limits not pushed yet"](graphics.md#driver-limits-not-pushed-yet).

Older versions and the dated history: [archive/MEMORY.md](../archive/MEMORY.md),
[archive/KERNEL-MEMORY.md](../archive/KERNEL-MEMORY.md), [archive/IDEAS.md](../archive/IDEAS.md),
[archive/PLAN.md](../archive/PLAN.md).
