# Open ideas, ranked (2026-10-18)

The ideas from the research docs (RESEARCH, HARDWARE-LEVERAGE, RSX-SPU-FEASIBILITY,
SPU-OFFLOAD-SURVEY, SPU-OFFLOAD, KERNEL-MEMORY, PERF, RSX, RSX-PROGRAMS, DESKTOP-PLAN, DRIVER-PLAN,
STALLS, HOTPATHS, MEMORY, FLASH-SYSCON, THERMAL) that are not done yet, plus the leads of the last two
days, ranked by expected payoff against effort and risk. The plan with its progress is docs/PLAN.md.
The goal they are measured against: an open, Sony-independent PS3 running open-source games well
(~60 fps at 1080p for light games) and a good desktop. **Since 2026-10-18 smoothness ranks above average
fps** (the user): an idea that removes stalls outranks one that adds a few fps. Every row is judged on
steady fps together with stall time, 1% low and longest frame.

What the measurements say (GAME-PLAN.md, games/bench/results/, same-boot rows):
- **Stalls came from three places, in this order of size.** (1) A kernel bug: the decrementer of an idle
  PPE thread is lost into `lv1_pause` (an idle thread's timer fired up to 1439 ms late; none over 19.4 ms
  with a 10 ms kicker). OpenArena's gameplay stalls 16.1 and 13.3 -> 6.5 and 6.2 s with the kicker alone.
  (2) Loads during play: OpenArena's preload took gameplay stalls from 9.9 and 9.0 s to 0. (3) Memory:
  Yamagi's PRNG table (22.6 -> 0.05 s), Luanti's `cliunload` (6.5 -> 0.8 s).
- **Fewer GL calls in the game beat driver work by far.** X-Moto 44.5 -> 166.5 fps, Extreme Tux Racer
  52 -> 59. Where the game is already GPU-bound or lightly loaded the gain is small: dsda-doom +3%,
  OpenArena opengl1 neutral, ClassiCube -4% steady with better lows (docs/HOTPATHS.md "ClassiCube's
  batching measured slower: GPU-bound").
- **One Mesa hot path paid for itself:** `NV30_TRANSLATE_LUT` (0033), Sauerbraten 25.9 -> 39.6 fps (+53%),
  now the default. Query wait (0035) gave no gain in the repeat (26.0/37.9 against 23.0/33.4).
- **Memory is still the limit for the big games:** Sauerbraten 117-120 MB of VRAM with 15-19 MB free,
  SuperTuxKart 96 MB even at `lean`, OpenArena opengl2 115 MB with stalls of 54.7 s and a 16 s frame in
  the profiled run, Luanti 13-21 MB free.
- **SPU audio works but waits on latency.** fluidsynth on the SPEs: CPU 44.5 -> 31.8 s a minute and
  underruns about halved (the user by ear: "night and day"); dsda-doom still chops (465 underruns a
  minute on the SPE pool against 1095 on the PPE).

Built and waiting for one PS3 session (the user's reboot session): the test kernels (lost-decrementer
fix local/0009, hwmon temperatures 0040, fan policy 0042, shutdown poll 0041, console frame release
0043, the OtherOS++ survey set 0020-0032, read-only GameOS mount), game mode's automatic start and
`ps3-kick`, the game prefetch, ps3rsx `shadow_fast`, Mesa 0036 (ClassiCube uploads), cube2 0006 SPU
skinning (measured: no gain on the demo).

## Done since 2026-10-17 (moved out of the ranking)

- Lost decrementer: found (stall recorder, A/B with wakers), confirmed on the idle PS3, stopgap kicker
  (`ps3-kick` in game mode), fix in the test kernels.
- DXT5 and RGB sources in libspeq (old #5), Mesa 0032 routes DXT5 to it; Teeworlds runs, its DXT5 path
  is not yet seen in a log.
- Luanti's variants run (old #2); the default is a user decision (`cliunload`).
- Batching measured in dsda-doom, ClassiCube, OpenArena opengl1, Luanti, Neverball (see #1).
- fluidsynth v2 on the shared deadline pool (old #6, first half).
- Temperatures reachable (the System Manager answers); fan policy allowed and built (the old "fan
  control is out of scope" is withdrawn, CLAUDE.md 2026-10-18).
- Smoke tests of 11 more games (8 run), the `ps3-licensed` suite, GameOS partition readable read-only,
  OtherOS++ patch survey, flash and SYSCON research.

## Ranked

Effort: S = a day or less, M = days, L = weeks. Risk: what can go wrong, including a GPU wedge
(only a console reboot clears it).

| # | Idea | From | Expected payoff (evidence) | Effort | Risk | Depends on, status |
|---|---|---|---|---|---|---|
| 1 | **Boot a test kernel with the lost-decrementer fix** (local/0009) and A/B it with `ps3_idle_prep=0/1` in one boot | STALLS.md, OTHEROS-PATCHES | high, and for every game: an idle thread's timer fired up to 1439 ms late; the 10 ms kicker alone took OpenArena's gameplay stalls 16.1 and 13.3 -> 6.5 and 6.2 s. The fix would retire the kicker and its 100 wakeups a second | S (a reboot session) | low-medium: skipping the pause makes idle threads spin, so watch the fan and `uptime`; the first draft was reviewed and changed | the user's reboot; fan on SYSCON; test-kernel batch (.433/.435) in one session |
| 2 | **Luanti's package default**: `cliunload` (or another of the 20 variants) | GAME-PLAN | high for Luanti: gameplay stalls 6.5 -> 0.8 s, longest frame 1943 -> 259 ms, same picture; `lean` 0.2 s but renders less terrain; `view40` 59 fps with closer fog | S | low | a user decision (PLAN.md decision 2) |
| 3 | **Game prefetch and game mode's kicker on the PS3**: the prefetch A/B (24 runs) and `ps3-game-kick` through game mode | STALLS.md "Game prefetch", DESKTOP-PLAN | medium-high: the disk is about a third of Sauerbraten's 16.5 s load and most of its hitches; USB 2 reads cost 10-20 ms each | S (about 2 h of runs) | low: reads only; watch MemAvailable (the prefetch stops on memory) | built; never run on the PS3 |
| 4 | **Texture compression at upload for every game**: an opt-in switch in libspeq/Mesa's hook (driconf or `SPEQ_S3TC_FORCE`) that stores plain RGB(A)8 uploads as DXT1 when opaque | HARDWARE-LEVERAGE 1, SPU-OFFLOAD-SURVEY #2 | high: VRAM for textures /4-8; OpenArena -14 MB; Sauerbraten (117-120 MB), SuperTuxKart (96 MB at lean) and OpenArena opengl2 (115 MB) are VRAM-bound. RGB DXT on the SPEs is 1.0 ms per 1024x1024 against about 175 ms on the PPE, so the load cost is gone; Sauerbraten's demo uploads no RGB DXT1, so it does not test it | M | medium: lossy, opaque detection per upload, sub-image updates of compressed levels, must skip render targets | DXT5 is done (libspeq, Mesa 0032); the switch is not |
| 5 | **VRAM budget in game mode**: the VRAM swap's 96 MB reserve against a game's VRAM (shrink or cap it while a game runs) | GAME-PLAN, ps3rsx mem.c | high for the heavy games: a game short of VRAM makes the swap write its chunks to the USB disk first | S-M | low-medium: swap traffic moves elsewhere; measure | a measurement of which path OpenArena opengl2 and SuperTuxKart hit (not yet taken) |
| 6 | **Game-side batching in the remaining games**: Sauerbraten (cube2 0007-0009 exist), SuperTuxKart's legacy renderer, OpenTTD's GL driver; keep the ones that help, leave the rest off | today | medium now: 3.7x in X-Moto, +13% in ETR, but +3% in dsda-doom, neutral in OpenArena, -4% in ClassiCube (GPU-bound); pick targets with `perf` first (`--proc-map-timeout=20000`) | M per game | low: each change behind a switch, pixels checked on llvmpipe | docs/HOTPATHS.md for which games are CPU-bound |
| 7 | **The 7th SPE by boot parameter** (`spu_base.enum_shared=1` in a second Petitboot entry) | FLASH-SYSCON 6.1 | medium, free: one more SPE for the deadline pool or DXT (6 now, 2 in the pool, 4 bulk); no write of any kind | S | low: off-switch is the other boot entry | the reboot session; speq benchmarks in the same boot as a 6-SPE baseline |
| 8 | **dsda-doom's chopping music: the cause** (the pool wait, SDL's audio buffer, or outside the DSP) | SPU-AUDIO 5(e) | medium: 465 underruns a minute on the SPE pool (1095 on the PPE); OpenTTD's music (3893 underruns) and SDL_mixer MIDI share it | S-M | low: PPE fallback per round | fluidsynth v2 measured; the playspu/playppe/playoff variants |
| 9 | **The `dcbz` memcpy trap on X and games**: count alignment faults (`perf stat -e alignment-faults,emulation-faults`), replace memcpys into VRAM mappings with a store loop | RSX-SPU-FEASIBILITY 1, DESKTOP-PLAN "Later" | medium for the desktop: glibc's Cell memcpy into VRAM runs at 19 MB/s; antialiased drawing took glxgears from 58 to ~12 fps | S to check, M to fix | low | none |
| 10 | **Cheaper Mesa 0028 and 0031**: fence the index buffer and run FP constant changes from GART only where needed | today (X-Moto A/B) | low-medium: X-Moto -5% on ps3.299, about a frame each from 0028 and 0031 | S-M | low: switches exist (NV30_IDXBUF_LATE_REF, NV30_FP_CONST_VRAM); 0028 is a race fix, keep it correct | none |
| 11 | **Indirect constant addressing in nv30 vertex programs** (ARL, already encoded by nvfx) | GAME-PLAN driver work, GAME-SUITE | medium: GPU skinning becomes usable (Sauerbraten 11 fps GPU against 28 CPU), fewer compile-time if/else trees and less shader RAM in OpenArena opengl2 | M-L | medium: a wrong program can wedge the RSX; the 512-instruction limit | none |
| 12 | **Probe E16: SPU DMA into a VRAM buffer** (rate, correctness, I\|G against I-only) | RSX-SPU-FEASIBILITY 2, HARDWARE-LEVERAGE 3 | medium, enabling: SPU decompression and generation straight into VRAM, meshes into VBOs | S | low: only the test's buffer is written, no GPU involved | none |
| 13 | SPU meshing: Luanti's block meshes, ClassiCube's chunks | SPU-OFFLOAD-SURVEY #4, SPU-OFFLOAD-PLAN 5-6 | medium: Luanti's load stalls, ClassiCube's chunk builds | L | medium (engine patches) | #10 if the meshes go straight to VRAM |
| 14 | Console frame only while visible (local/0043, built, off by default: 8 of the 9 MB while ps3rsx owns the GPU), or the 720p console | KERNEL-MEMORY 3 | medium: up to 8.5 MB of RAM during games (720p: 4 MB always, and X at 720p) | M (720p: S) | medium: ps3fb's GPU iomap and vsync blit must cope; 720p is the user's call | 0043 needs the test-kernel boot and a RAM A/B; 720p is the user's call |
| 15 | **Read-only probe of lv1 from Linux** (peek, hypercall 182): does Evilnat's lv1 carry the `gpu-4kb-iopage` patch, could ps3rsx shrink the 64 MB GART reservation | FLASH-SYSCON 6.3 | potentially the largest RAM win on the list; unknown until peeked | S to probe | medium: a wrong poke hangs the console; peek only, ask the user before any poke | the user's OK; the reboot session |
| 16 | ADPCM sound effects in ioquake3 (OpenAL decodes IMA4 and MSADPCM at mix time) | SPU-AUDIO 3(d') | low-medium: ~7 MB of decoded WAV in OpenArena down to a quarter | M | low (4-bit quality: the user listens) | none |
| 17 | Half-float or 16-bit vertex data, fetched natively by nv30 | GAME-PLAN "data for compute" | low-medium: vertex buffers /2 | M per engine | low | none |
| 18 | Mesa: drop shader IR after linking, rebuild from the disk cache when needed | GAME-PLAN "data for compute" | low-medium now: ioquake3 0001 already frees OpenArena's shader objects | M | medium (variant recompiles) | a measurement of what Mesa keeps |
| 19 | Tiling and Z-cull (lv1 attributes 0x300/0x301) | HARDWARE-LEVERAGE 2, DRIVER-PLAN "Later" | unknown: +10-30% in fill-bound scenes (estimate); matters now that batched games stop being CPU-bound | M-L | medium-high: undocumented lv1 attributes | none |
| 20 | Validator checks for the vertex texture units (0x900-0x97c) | PLAN item 6, RSX-SPU-FEASIBILITY 4 | safety: today any client can send unchecked data there and wedge the RSX | S | low | none; required before #18 |
| 21 | Vertex texture fetch, fragment derivatives (GLES2), instancing | HARDWARE-LEVERAGE 2/4, RSX-SPU-FEASIBILITY 4 | low for the current games (none uses them) | M each | medium (exceptions) | #17 for VTF |
| 22 | OpenAL Soft's free variants (b16, f44) | SPU-AUDIO 2 | low: at most part of 3.7-4.7% | S | none | none |
| 23 | Dev loop: does a Petitboot-only reboot clear a wedged RSX; kexec from our Linux; Remote Start by Wake-on-LAN | RSX, DRIVER-PLAN, PLAN open questions | low for games, saves XMB trips | S each, needs the user | low (kexec crashed the console once) | the user at the console |
| 24 | Video decoding on the SPEs (MPEG-2, then H.264; HEVC) | SPU-OFFLOAD #2, RESEARCH | high value, months of work | L | low | parked (CLAUDE.md) |

Not worth it, measured or reasoned: an SPU mixer for OpenAL Soft (3.7-4.7%, under the 6% bar),
Vorbis or zlib on the SPEs (WAV effects; libdeflate on the PPE instead), OpenTTD's blitter on the
SPEs (its GL driver is built), the validator, zram or VRAM-swap copies on the SPEs (the kernel can't
drive them), Mesa's per-draw paths on the SPEs, compressed swap through a userspace block device
(reclaim deadlocks), software rendering on the SPEs.

Postponed by the user's rule (CLAUDE.md "Memory trims"): system trims that disable things for good
(zram off, seccomp filters, cfg80211, `dhash_entries`); the same memory is to come from game mode.

Out of scope under the safety rules: writes to flash, the SYSCON (apart from the fan policy through the
System Manager, allowed by the user on 2026-10-18 and built as local/0042), lv1 or the hypervisor
repository (docs/FLASH-SYSCON.md ranks them and says what recovery would take; the OtherOS area write
seen on 2026-10-15 is to be found and disabled, not used), and hardware mods (the power-control rig, a
NOR flasher, a SYSCON UART). The OtherOS++ items that could reach persistent state were dropped or built
off on 2026-10-18 (OTHEROS-PATCHES.md). Not recommended: a cacheable PPE mapping of VRAM (E8c: if lv1
refuses the entry the kernel BUG()s); ask before trying it. The 8th SPE (a flash write) waits for
recovery hardware and a measured need.

## How to start the top ones

1. **Test kernel.** In the reboot session boot the kernel with local/0009 (`.433` or `.435`), run
   STALLS.md's probe test 1 (no late wakeups expected), then OpenArena `base` with `-`, `STALLS=1` and the
   kick against `echo 0 > /sys/module/kernel/parameters/ps3_idle_prep`, same boot. Watch the fan and
   `uptime`. First boot with `ps3_sys_manager.fan_policy=keep`, THERMAL.md's fan steps after.
2. **Luanti.** Decide the default from the numbers above; if `cliunload`, make it the package default and
   re-run once for the picture on the webcam.
3. **Prefetch.** Run STALLS.md "The A/B on the PS3" step 0 to 1 (Sauerbraten, OpenArena gl2min, Luanti,
   SuperTuxKart lean); then check that the `ps3-game-kick` unit starts and stops with a game.
4. **Compression for every game.** In libspeq's s3tc path, an opt-in that takes RGB8 and RGBA8 uploads
   whose alpha is all 255 as DXT1; never for render targets; log per texture with `SPEQ_DEBUG`. Try
   SuperTuxKart, Sauerbraten and OpenArena opengl2; compare VRAM (vram.csv) and look at the picture.
5. **VRAM budget.** Log `ps3rsx_vswap_shrink` calls and GART fallbacks during an OpenArena opengl2 and a
   SuperTuxKart run; then try `vswap_vram_mb` lower, or a game-mode hook that writes the swap's VRAM
   pages out before the game loads.
6. **Batching.** Count GL calls and draws per frame on llvmpipe forced to GL 2.1 (`tools/glcount`), then
   perf the PS3 run with `--proc-map-timeout=20000`. Each change gets a switch and a recipe variant.
7. **7th SPE.** A Petitboot entry with `spu_base.enum_shared=1`; `ls /sys/devices/system/spu` shows 7;
   `speq-bench` and the DXT tests in that boot against a 6-SPE run.

## Decisions (the user, 2026-10-17)

1. **System trims:** dropped. PLAN.md item 5's permanent trims go; memory comes from game mode
   (CLAUDE.md "Memory trims"), which already unloads the wallpaper and freezes the desktop.
2. **VRAM swap and game VRAM:** a measurement, not a decision (#4): which path the heavy games
   take (the swap giving VRAM back, or GART spill) is measured before the reserve is resized.
3. **SPU queues:** one shared deadline pool of 2 SPEs for every latency-bound service (audio,
   skinning: jobs from all of them in one queue, by deadline), DXT keeps its 4 bulk SPEs, so a
   process never runs more contexts than SPEs.
4. **Same-boot baselines:** yes, the rule. Every measured change runs against its off-switch (or
   the previous build) in the same PS3 session; numbers from different boots are not compared.
5. **E8's write-combining result:** a measurement (probe E8b) before either doc is relied on.

## Decisions (the user, 2026-10-18)

1. **Smoothness above average fps.** Stalls, 1% low and longest frame decide; a lower steady fps is
   fine when it plays smoothly.
2. **Fan policy from Linux is allowed**, only through the System Manager's fan-policy service, SYSCON
   automatic at boot and on every exit path, a fixed floor of at least 60% only on request. Other SYSCON
   writes stay forbidden (CLAUDE.md).
3. **OtherOS++ patches:** dropped `/dev/ps3sysmngr` (0060), the BD vendor commands (0090), the Jupiter
   Wi-Fi driver with the eurus repository write (0170, 0160, 0316); `PS3_DISP_MANAGER` off.
4. **Open for the user:** the stable publish of the READY set (RELEASE-CANDIDATES.md), Luanti's default,
   the test-kernel boot session, the fan floor, the 7th SPE entry, SuperTuxKart's `lean` default, the
   720p console.
