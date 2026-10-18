# Open ideas, ranked (2026-10-17)

The ideas from the research docs (RESEARCH, HARDWARE-LEVERAGE, RSX-SPU-FEASIBILITY,
SPU-OFFLOAD-SURVEY, SPU-OFFLOAD, KERNEL-MEMORY, PERF, RSX, RSX-PROGRAMS, DESKTOP-PLAN, DRIVER-PLAN)
that are not done yet, plus today's leads, ranked by expected payoff against effort and risk. The goal
they are measured against: an open, Sony-independent PS3 running open-source games well (~60 fps at
1080p for light games, no memory stalls) and a good desktop.

What today's measurements say (GAME-PLAN.md, games/bench/results/):
- **Fewer GL calls in the game beat driver work by far.** X-Moto 44.5 -> 166.5 fps with our
  batching (18,500 -> 165 calls a frame), Extreme Tux Racer 52 -> 59 at 37% less CPU, against
  +0 to +24% for three Mesa rounds (0015-0031).
- **Memory is the main limit for the big games.** OpenArena opengl2 14.8 -> 26-32 fps from
  memory patches alone (the longest stall 24.7 -> 6 s); SuperTuxKart ps3.306 still logs "out of VRAM"
  at 132 MB (98 MB with nohd or lean), HUD 13-19 fps; Luanti 177 MB swapped out; Sauerbraten
  OOM-killed without the VRAM swap.
- **SPU audio works but waits on latency.** fluidsynth on the SPEs cut dsda-doom's CPU from 49 to
  32 s a minute, but the chopping stayed (1097 underruns against 18 without music); OpenAL Soft is
  only 3.7-4.7% of a game.

Before any new idea, the built-but-unmeasured work needs one PS3 session (GAME-PLAN.md "Built
2026-10-17"): dsda-doom and ClassiCube batching, ioquake3 0011-0018, fluidsynth v2, SuperTuxKart's
picture, OpenTTD's GL driver, Sauerbraten's memory patches and SPU skinning, Luanti's variants and
game mode's memory work.

## Ranked

Effort: S = a day or less, M = days, L = weeks. Risk: what can go wrong, including a GPU wedge
(only a console reboot clears it).

| # | Idea | From | Expected payoff (evidence) | Effort | Risk | Depends on |
|---|---|---|---|---|---|---|
| 1 | **Game-side batching in the remaining CPU-bound games**: measure dsda-doom GL and ClassiCube (built), then OpenArena opengl1 (renderergl1; done on the host, ioquake3 ps3.362: 198 -> 79 draws, 3022 -> 377 calls), Luanti, Sauerbraten, Neverball (done on the host, neverball ps3.370: 137 -> 74 draws, 2154 -> 844 calls), SuperTuxKart's legacy renderer | today (X-Moto, ETR, dsda-doom, ClassiCube) | high: 3.7x and +13% where done; dsda-doom ~205 -> ~44 draws, ClassiCube 1107 -> 294 draws a frame on the host; the driver stack is 30-55% of these games' CPU | M per game | low: each change behind a switch, pixels checked on llvmpipe | perf with `--proc-map-timeout=20000` to pick the targets |
| 2 | **Luanti's package defaults** from its 20 memory variants (view range, block unloading, mesh delay, heap) | GAME-PLAN | high for Luanti: stalls to 0 fps while chunks load, 177 MB swapped, 32 MB free | S once measured | low | the variants' PS3 run |
| 3 | **Texture compression at upload for every game**: an opt-in switch in libspeq/Mesa's hook (driconf or `SPEQ_S3TC_FORCE`) that stores plain RGB(A)8 uploads as DXT1 when opaque | HARDWARE-LEVERAGE 1, SPU-OFFLOAD-SURVEY #2, today (SuperTuxKart) | high: VRAM for textures /4-8; OpenArena -14 MB with no load cost on the SPEs; SuperTuxKart, Sauerbraten and OpenArena opengl2 are VRAM-bound | M | medium: lossy, opaque detection per upload, sub-image updates of compressed levels, must skip render targets | none for DXT1; alpha textures need #5 |
| 4 | **VRAM budget in game mode**: the VRAM swap's 96 MB reserve against a game's VRAM (shrink or cap it while a game runs) | GAME-PLAN, ps3rsx mem.c | high for the heavy games: a game short of VRAM makes the swap write its chunks to the USB disk first | S-M | low-medium: swap traffic moves elsewhere; measure | a measurement of which path OpenArena opengl2 and SuperTuxKart hit |
| 5 | **DXT5 in libspeq** (SPU and PPE): the alpha block next to DXT1 | SPU-OFFLOAD-PLAN parked 2, GAME-SUITE | medium: alpha textures compressed too (#3), Teeworlds and TORCS (GL_COMPRESSED_RGBA) | M | low (host tests as for DXT1) | none |
| 6 | **fluidsynth: v2 on the PS3, then the chopping's cause** (the wait, SDL's audio buffer, or outside the DSP) | SPU-AUDIO 5(e) | medium: dsda-doom's music right; OpenTTD's music and SDL_mixer MIDI share it | S-M | low: PPE fallback per round | v2 run |
| 7 | **The `dcbz` memcpy trap on X and games**: count alignment faults (`perf stat -e alignment-faults,emulation-faults`), replace memcpys into VRAM mappings with a store loop | RSX-SPU-FEASIBILITY 1, DESKTOP-PLAN "Later" | medium for the desktop: glibc's Cell memcpy into VRAM runs at 19 MB/s; antialiased drawing took glxgears from 58 to ~12 fps | S to check, M to fix | low | none |
| 8 | **Cheaper Mesa 0028 and 0031**: fence the index buffer and run FP constant changes from GART only where needed | today (X-Moto A/B) | low-medium: X-Moto -5% on ps3.299, about a frame each from 0028 and 0031 | S-M | low: switches exist (NV30_IDXBUF_LATE_REF, NV30_FP_CONST_VRAM); 0028 is a race fix, keep it correct | none |
| 9 | **Indirect constant addressing in nv30 vertex programs** (ARL, already encoded by nvfx) | GAME-PLAN driver work, GAME-SUITE | medium: GPU skinning becomes usable (Sauerbraten 11 fps GPU against 28 CPU), fewer compile-time if/else trees and less shader RAM in OpenArena opengl2 | M-L | medium: a wrong program can wedge the RSX; the 512-instruction limit | none |
| 10 | **Probe E16: SPU DMA into a VRAM buffer** (rate, correctness, I\|G against I-only) | RSX-SPU-FEASIBILITY 2, HARDWARE-LEVERAGE 3 | medium, enabling: SPU decompression and generation straight into VRAM, meshes into VBOs | S | low: only the test's buffer is written, no GPU involved | none |
| 11 | SPU meshing: Luanti's block meshes, ClassiCube's chunks | SPU-OFFLOAD-SURVEY #4, SPU-OFFLOAD-PLAN 5-6 | medium: Luanti's load stalls, ClassiCube's chunk builds | L | medium (engine patches) | #10 if the meshes go straight to VRAM |
| 12 | Console frame only while visible (released while X owns the screen), or the 720p console | KERNEL-MEMORY 3 | medium: up to 8.5 MB of RAM during games (720p: 4 MB always, and X at 720p) | M (720p: S) | medium: ps3fb's GPU iomap and vsync blit must cope; 720p is the user's call | none |
| 13 | ADPCM sound effects in ioquake3 (OpenAL decodes IMA4 and MSADPCM at mix time) | SPU-AUDIO 3(d') | low-medium: ~7 MB of decoded WAV in OpenArena down to a quarter | M | low (4-bit quality: the user listens) | none |
| 14 | Half-float or 16-bit vertex data, fetched natively by nv30 | GAME-PLAN "data for compute" | low-medium: vertex buffers /2 | M per engine | low | none |
| 15 | Mesa: drop shader IR after linking, rebuild from the disk cache when needed | GAME-PLAN "data for compute" | low-medium now: ioquake3 0001 already frees OpenArena's shader objects | M | medium (variant recompiles) | a measurement of what Mesa keeps |
| 16 | Tiling and Z-cull (lv1 attributes 0x300/0x301) | HARDWARE-LEVERAGE 2, DRIVER-PLAN "Later" | unknown: +10-30% in fill-bound scenes (estimate); matters now that batched games stop being CPU-bound | M-L | medium-high: undocumented lv1 attributes | none |
| 17 | Validator checks for the vertex texture units (0x900-0x97c) | PLAN item 6, RSX-SPU-FEASIBILITY 4 | safety: today any client can send unchecked data there and wedge the RSX | S | low | none; required before #18 |
| 18 | Vertex texture fetch, fragment derivatives (GLES2), instancing | HARDWARE-LEVERAGE 2/4, RSX-SPU-FEASIBILITY 4 | low for the current games (none uses them) | M each | medium (exceptions) | #17 for VTF |
| 19 | OpenAL Soft's free variants (b16, f44) | SPU-AUDIO 2 | low: at most part of 3.7-4.7% | S | none | none |
| 20 | Dev loop: does a Petitboot-only reboot clear a wedged RSX; kexec from our Linux; Remote Start by Wake-on-LAN | RSX, DRIVER-PLAN, PLAN open questions | low for games, saves XMB trips | S each, needs the user | low (kexec crashed the console once) | the user at the console |
| 21 | Video decoding on the SPEs (MPEG-2, then H.264; HEVC) | SPU-OFFLOAD #2, RESEARCH | high value, months of work | L | low | parked (CLAUDE.md) |

Not worth it, measured or reasoned: an SPU mixer for OpenAL Soft (3.7-4.7%, under the 6% bar),
Vorbis or zlib on the SPEs (WAV effects; libdeflate on the PPE instead), OpenTTD's blitter on the
SPEs (its GL driver is built), the validator, zram or VRAM-swap copies on the SPEs (the kernel can't
drive them), Mesa's per-draw paths on the SPEs, compressed swap through a userspace block device
(reclaim deadlocks), software rendering on the SPEs.

Postponed by the user's rule (CLAUDE.md "Memory trims"): system trims that disable things for good
(zram off, seccomp filters, cfg80211, `dhash_entries`); the same memory is to come from game mode.

Out of scope under the safety rules: fan control (needs syscon writes; researched in THERMAL.md), anything that writes flash
(the OtherOS area write seen on 2026-10-15 is to be found and disabled, not used), hardware mods (the
power-control rig). Not recommended: a cacheable PPE mapping of VRAM (E8c: if lv1 refuses the
entry the kernel BUG()s); ask before trying it.

## How to start the top ones

1. **Batching.** Run dsda-doom's and ClassiCube's recipe variants (`gloff`, `glstats`) on one boot
   against the old drawing. For the next game, count GL calls and draws per frame on llvmpipe
   forced to GL 2.1 (as for X-Moto), then perf the PS3 run with `--proc-map-timeout=20000`. Each
   change gets a switch and a recipe variant; same pixels on llvmpipe before the PS3.
2. **Luanti.** Run the 20 variants with the memory log from the ps3bench mod; ship as package
   defaults the ones that remove the stalls without shrinking the view below what the user accepts.
3. **Compression for every game.** In libspeq's s3tc path, an opt-in that takes RGB8 and RGBA8
   uploads whose alpha is all 255 as DXT1 (one scan on the PPE or the SPU); never for textures
   bound as render targets; log per texture with `SPEQ_DEBUG`. Try SuperTuxKart, Sauerbraten and
   OpenArena opengl2; compare VRAM (vram.csv) and look at the picture.
4. **VRAM budget.** Log `ps3rsx_vswap_shrink` calls and GART fallbacks during an OpenArena
   opengl2 and a SuperTuxKart run; then try `vswap_vram_mb` lower, or a game-mode hook that writes
   the swap's VRAM pages out before the game loads.
5. **DXT5.** The alpha block (8-entry palette, 48-bit indices) in the PPE encoder first, then
   dxt.spu, against Mesa's encoder with `make -C ps3-spu test-mesa`.
6. **fluidsynth.** Run the playspu/playppe/playoff variants with `SPEQ_DEBUG=1`; if the underruns
   stay with the SPU wait near zero, look at SDL's audio buffer size and dsda-doom's mixer rate.
7. **dcbz.** `perf stat -e alignment-faults,emulation-faults` on Xorg with the desktop busy and on
   one game; thousands per second mean the trap.

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
