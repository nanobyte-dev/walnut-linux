# The docs

Everything we know about running Linux on our PS3, sorted by how sure it is: measured facts at the
top level, research (read or reasoned, not measured on our console) in `research/`, untried ideas
in `backlog/`, games in `games/`, and the superseded originals in `archive/`. Start with
[PLAN.md](PLAN.md) for where things stand, then [games/README.md](games/README.md) for how each
game plays. The project's rules, safety rules first, are in [CLAUDE.md](../CLAUDE.md); the repo
layout and the build commands are in [LAYOUT.md](LAYOUT.md).

## The plan and the test log

| Doc | What it is for |
|---|---|
| [PLAN.md](PLAN.md) | The goal, where we are, the user's open decisions (D1-D14) and the five stages of work with their item numbers |
| [TESTS.md](TESTS.md) | The PS3 test queue and the log of every batch that ran, with its results commit |

## Measured facts (top level)

| Doc | What it is for |
|---|---|
| [CONSOLE.md](CONSOLE.md) | Our console (model, firmware, disks, webcam), how a session runs and its gotchas, the backups we hold, recovery, the kernel's OS-area write |
| [RSX.md](RSX.md) | What the GPU does when driven from Linux: contexts, fences, copy speeds, what wedges it |
| [DRIVER.md](DRIVER.md) | How ps3rsx and our Mesa patches are built: validator, context switch, KMS, GART, VRAM swap |
| [MESA-METHODS.md](MESA-METHODS.md) | Audit of every method Mesa nv30 sends against what the RSX knows; the validator's allowlist came from it |
| [RSX-PROGRAMS.md](RSX-PROGRAMS.md) | The FIFO words of the first triangle and textured quad, with their derivations |
| [STALLS.md](STALLS.md) | Why frames stall: the stall recorder, the lost-decrementer bug and its fix, game prefetch, the interference detector |
| [PROFILING.md](PROFILING.md) | CPU and memory profiles of the games (`PERF=1`, `MEM=1`, `PERF_LOAD=1`), start-up profiles, shader caches, load-time decoders and linking (zlib-ng, libpng, libjpeg-turbo, ld.so) |
| [HOTPATHS.md](HOTPATHS.md) | The graphics stack's CPU hot paths ranked across the games, and the Mesa/ps3rsx patches that came from them |
| [MEMORY.md](MEMORY.md) | The console's RAM budget, where each game's memory goes, the cuts made and measured |
| [KERNEL-MEMORY.md](KERNEL-MEMORY.md) | What the kernel's RAM is, the release kernel's config cuts, ps3fb's console frame (local/0043) |
| [SPU.md](SPU.md) | libspeq and what runs on the SPEs: texture compression, fluidsynth, skinning |
| [SPU-AUDIO.md](SPU-AUDIO.md) | Sound on the SPEs: fluidsynth's voice DSP, why OpenAL Soft's mixer stayed on the PPE, and the PPE side made cheaper (OpenAL, Vorbis, fluidsynth's effects) |
| [SPU-GCC.md](SPU-GCC.md) | Our port of GCC's SPU backend to GCC 16.2 |
| [THERMAL.md](THERMAL.md) | Temperatures through the System Manager, the fan policy driver, the PS3 checks still to run |
| [DESKTOP.md](DESKTOP.md) | The Openbox desktop and game mode |
| [GAMEOS-HDD.md](GAMEOS-HDD.md) | GameOS's disk read-only from Linux, the SSD disk image (Phase A and B), the kernel write guard (local/0045) |
| [OTHEROS-PATCHES.md](OTHEROS-PATCHES.md) | Survey of PS3 kernel patches outside our tree: what we carry, what we ported, the user's decisions |
| [PACKAGING.md](PACKAGING.md) | The base system and toolchain, how our software becomes Debian packages, the apt repository, deploys |
| [GAMEDB.md](GAMEDB.md) | Walnut Games: the games database (walnut.nanobyte.dev/gamedb/) and the launcher app, how games are tested, scored and given medals |
| [WALNUT-SERVER.md](WALNUT-SERVER.md) | walnut.nanobyte.dev's containers, and why concurrent uploads, publishes and ccache writes can't break anything |
| [release/ALPHA.md](release/ALPHA.md) | Walnut 0.1-alpha, the first public release: plan, milestones M0-M9, the review's blockers, cuts, verification |
| [release/TESTING-0.1.md](release/TESTING-0.1.md) | Testing the nightlies before 0.1.0: feedback from the PS3 and what was done about each |
| [RELEASE-CANDIDATES.md](RELEASE-CANDIDATES.md) | The 2026-10-18 release: what goes from ps3-dev to stable, its test plan, the publish command |
| [IMAGE-DIFF.md](IMAGE-DIFF.md) | A fresh image compared with the live PS3 (2026-10-16): what a reimage would lose |

## Games

| Doc | What it is for |
|---|---|
| [games/README.md](games/README.md) | Every game at a glance: runs?, fps, smoothness, memory, picture and sound, what's next; how to read the numbers; the driver bugs games found |
| [games/SUITE.md](games/SUITE.md) | How the benchmark suite runs: `games/bench/`, recipes, the protocol, the result matrices |
| [games/LOG-FINDINGS.md](games/LOG-FINDINGS.md) | What the game logs say: errors and warnings by game, ranked, with causes and fixes; `games/bench/logscan.py` |
| [games/openarena.md](games/openarena.md) | OpenArena on our ioquake3, both renderers |
| [games/quake.md](games/quake.md) | QuakeSpasm and Yamagi Quake II |
| [games/sauerbraten.md](games/sauerbraten.md) | Sauerbraten (cube2) and Red Eclipse |
| [games/luanti.md](games/luanti.md) | Luanti and our LuaJIT |
| [games/supertuxkart.md](games/supertuxkart.md) | SuperTuxKart |
| [games/classicube.md](games/classicube.md) | ClassiCube |
| [games/doom.md](games/doom.md) | dsda-doom, and Chocolate, Crispy and Woof Doom |
| [games/openttd.md](games/openttd.md) | OpenTTD and its GL driver |
| [games/openrct2.md](games/openrct2.md) | OpenRCT2's big-endian port: patches, BE-vs-LE checks, RAM, the PS3 steps |
| [games/light-games.md](games/light-games.md) | X-Moto, Extreme Tux Racer, Neverball, Armagetron |
| [games/smoke-tested.md](games/smoke-tested.md) | Games tried once (Teeworlds, SuperTux, Nexuiz, ...) and the new games waiting for a first run |

## Backlog: what we haven't tried

| Doc | What it is for |
|---|---|
| [backlog/README.md](backlog/README.md) | Everything untried or built but unmeasured, ranked in one list; what was rejected and why |
| [backlog/graphics.md](backlog/graphics.md) | Mesa, ps3rsx, GPU features, game-side batching, RSX probes |
| [backlog/memory.md](backlog/memory.md) | RAM and VRAM cuts not made yet |
| [backlog/smoothness.md](backlog/smoothness.md) | Stalls, loads, the disk, measurement tools |
| [backlog/spu.md](backlog/spu.md) | SPU work not done |
| [backlog/games.md](backlog/games.md) | Per-game items: repeats, defaults to decide, engine patches |
| [backlog/platform.md](backlog/platform.md) | Desktop, input, network, power, display, storage, release work |
| [backlog/hardware.md](backlog/hardware.md) | Backups not yet taken, the 7th SPE, lv1 probes, recovery hardware; what is out of scope under the safety rules |

## Research (read or reasoned, not measured here)

| Doc | What it is for |
|---|---|
| [research/RESEARCH.md](research/RESEARCH.md) | The first background notes (2026-10-13): Linux on the PS3, the RSX, prior work |
| [research/HARDWARE-LEVERAGE.md](research/HARDWARE-LEVERAGE.md) | Which techniques could relieve the PS3's limits, and what became of each |
| [research/RSX-SPU-FEASIBILITY.md](research/RSX-SPU-FEASIBILITY.md) | CPU writes to VRAM, SPU DMA into VRAM, kernel M2MF copies, NV40 features, from the handbooks and source |
| [research/SPU-OFFLOAD.md](research/SPU-OFFLOAD.md) | What in our stack could run on the SPEs, prior art, what was rejected |
| [research/OPENAL-SPU-MIXER.md](research/OPENAL-SPU-MIXER.md) | The design of an SPU mixer for OpenAL Soft, never built |
| [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md) | What writing flash, the SYSCON or lv1 would unlock, the risks, how we'd recover; nothing tried |
| [research/HDD-REGION.md](research/HDD-REGION.md) | A reformatted Linux region on the internal disk: researched, not taken |
| [research/GAME-CANDIDATES.md](research/GAME-CANDIDATES.md) | How the suite's games were picked (2026-10-16) |
| [research/GAME-REEVAL.md](research/GAME-REEVAL.md) | Games first ruled out for non-GL3 reasons, measured and costed again (2026-10-19): DDNet, OpenTyrian, UQM, Hedgewars, OpenRCT2, 0 A.D., Java/.NET games and the Reddit list |
| [research/ENDLESS-SKY.md](research/ENDLESS-SKY.md) | Endless Sky's memory explained and its GL 2.1 route costed (2026-10-19): go, effort M |
| [research/EMULATORS.md](research/EMULATORS.md) | MAME, RetroArch cores and standalone emulators on ppc64 (2026-10-19); parked behind games new on the PS3 |
| [research/DARLING-PPC.md](research/DARLING-PPC.md) | A PowerPC port of Darling for Mac OS X games (2026-10-19): not worth it, the game-facing frameworks are stubs even on x86 |
| [research/CLASSIC-MACOS.md](research/CLASSIC-MACOS.md) | SheepShaver's native PowerPC mode and Basilisk II on the PS3 (2026-10-19); waits for the user's ROM and Mac OS CD |
| [research/PC-CLASSICS.md](research/PC-CLASSICS.md) | Windows PC games of 1990-2010 with open engines, decompilations or source releases (2026-10-20): state, renderer, big-endian prior art, console ports, PS3 effort; the shortlist; every alternative project per game with its popularity |

## Elsewhere in docs/

- [upstream/](upstream/README.md): drafts of upstream bug reports and patches (Teeworlds, Yamagi, Mesa, two kernel patches), none sent.
- [archive/](archive/): the originals these docs were rewritten from on 2026-10-19, unchanged except for a first line naming their successor. Every dated number and decision of 2026-10-13 to 10-19 is there.
- `glxinfo-nv47.txt`: glxinfo of Mesa nv30 on the RSX. `dumps/`: GPU exception dumps from the LXQt days.

## Where the old docs went

Some code comments and package files still cite the names from before 2026-10-19 (we leave them
until those packages change anyway, since any commit there bumps the package's `+ps3.N`). Each old
file is in `archive/` under its old name, and its first line names its successor.

| Old name | Now |
|---|---|
| `GAME-PLAN.md` | [games/README.md](games/README.md) and the per-game files |
| `GAME-SUITE.md` | [games/SUITE.md](games/SUITE.md), [research/GAME-CANDIDATES.md](research/GAME-CANDIDATES.md) |
| `IDEAS.md` | [backlog/README.md](backlog/README.md) (its "#N" numbers are mapped there) |
| `DRIVER-PLAN.md` | [DRIVER.md](DRIVER.md) |
| `DESKTOP-PLAN.md`, `PERF.md` | [DESKTOP.md](DESKTOP.md) |
| `SPU-OFFLOAD-PLAN.md` | [SPU.md](SPU.md) |
| `SPU-OFFLOAD.md`, `SPU-OFFLOAD-SURVEY.md` | [research/SPU-OFFLOAD.md](research/SPU-OFFLOAD.md); the measured parts in [SPU.md](SPU.md) |
| `HARDWARE-LEVERAGE.md`, `RSX-SPU-FEASIBILITY.md`, `RESEARCH.md` | the same names in [research/](research/) |
| `FLASH-SYSCON.md` | [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md); this console's facts, backups and the OS-area write in [CONSOLE.md](CONSOLE.md) |
| The OpenAL mixer design in `SPU-AUDIO.md` | [research/OPENAL-SPU-MIXER.md](research/OPENAL-SPU-MIXER.md) |
| The Linux-region research in `GAMEOS-HDD.md` | [research/HDD-REGION.md](research/HDD-REGION.md) |

## Glossary

The console and its firmware

- **GameOS, XMB**: Sony's own operating system and its menu. It stays the default boot OS (CLAUDE.md).
- **OtherOS++**: the custom-firmware feature that boots a second OS with GameOS-level access to the hardware, including the RSX. We run it on Evilnat 4.92 CFW.
- **Petitboot**: the boot loader OtherOS++ starts. It reads `kboot.conf` and kexecs our kernel. The OtherOS boot flag, set from the XMB, is the only way into it.
- **webMAN**: webMAN MOD, a GameOS plugin with a web interface (http://10.0.0.76/), FTP and fan control. Its fan control dies when Linux boots.
- **lv1**: the PS3's hypervisor. Linux runs as a guest (an **LPAR**, logical partition) and reaches the hardware through lv1 hypercalls.
- **SYSCON**: the system controller chip (power, fan, thermal sensors). Writes to it are forbidden apart from the fan policy (CLAUDE.md).
- **System Manager**: the lv1 service Linux talks to for power, temperatures and the fan policy (`ps3-sys-manager.c`).
- **NOR**: the 16 MB NOR flash of our CECH-20xx Slim, which holds the firmware. A dump of it is in `backups/` (private).
- **dev_hdd0, dev_hdd1**: GameOS's names for its disk regions: dev_hdd0 is the big UFS2 file system (games, saves), dev_hdd1 a 2 GB FAT32 cache.
- **ps3da, ps3db, ps3dc, ps3dd**: the internal disk's regions as Linux block devices: ps3da the whole disk, ps3db dev_hdd0, ps3dc dev_hdd1, ps3dd an OtherOS region if one is created. Our internal disk is an upgraded SSD.

The processors and memory

- **Cell, PPE, SPE/SPU**: the Cell processor has one PowerPC core (the **PPE**, two hardware threads, in-order) and SPEs, vector cores with 256 KB of local store each. Linux sees 6 SPEs. **SPU** is the SPE's processor; we use the words interchangeably.
- **XDR**: the 256 MB of main RAM. About 217-222 MB reach Linux.
- **RSX**: the GPU, an NVIDIA G70/NV47 derivative (GeForce 7 family). **NV47** is the chip id Mesa sees.
- **VRAM**: the RSX's 256 MB of GDDR3. The PPE writes it at about 124 MB/s and reads it at 10 MB/s, so data comes back only through the GPU's copy engine.
- **decrementer**: the PowerPC timer interrupt. An idle PPE thread lost its decrementer on the way into `lv1_pause` and could sleep up to 1.4 s (fixed by local/0009, docs/STALLS.md).

The graphics stack

- **ps3rsx**: our DRM kernel driver for the RSX. It registers as "nouveau" so stock Mesa and libdrm work.
- **nv30**: Mesa's Gallium driver for NV30/NV40-class GPUs, which drives the RSX. We carry patches on it (`mesa/patches/`).
- **ps3fb**: the kernel's PS3 framebuffer driver; it owns the console and allocates the GPU context ps3rsx uses.
- **FIFO, push buffer**: the command stream the CPU writes and the RSX reads.
- **validator**: the part of ps3rsx that checks every command a client submits, so a client cannot wedge the GPU or reach memory it doesn't own.
- **wedge, graph exception**: a GPU error that stops the RSX until the console is rebooted.
- **M2MF**: "memory to memory format", the RSX's copy engine (VRAM to XDR about 600 MB/s, XDR to VRAM about 2 GB/s).
- **GART**: XDR mapped for the GPU. Ours grows on demand in 1 MiB chunks from **CMA** (the kernel's contiguous memory allocator; `ps3rsx_cma=64M`).
- **KMS**: kernel mode setting; ps3rsx has a minimal KMS so Xorg's modesetting driver runs on it.
- **glamor**: Xorg's 2D acceleration through OpenGL.
- **u_vbuf**: Mesa's helper that converts vertex formats the GPU can't fetch, on the CPU, per draw.
- **DXT1, DXT5 (S3TC)**: compressed texture formats the RSX samples natively, 4-8x smaller than RGBA8.
- **vswap, ps3vswap**: our swap device that keeps swapped pages in spare VRAM (up to about 96 MB) with the USB swap partition behind it.

Our software

- **libspeq**: our library for running jobs on the SPEs (DXT encoding, fluidsynth's voice DSP, skinning). docs/SPU.md.
- **game mode**: what the desktop does while a game runs: the game and X stay resident, the desktop helpers freeze and their memory goes out, everything comes back afterwards. It starts by itself for fullscreen 3D programs. docs/DESKTOP.md.
- **the kicker**: a stopgap that woke PPE thread 1 every 10 ms (`ps3-kick`, the bench's `WAKER=10`, rows tagged `wkkick10`) so the lost decrementer couldn't stall it. Not needed with local/0009.
- **interference detector**: `ps3-intfwatch`, which runs beside every bench run and records anything that could disturb it (input, SSH logins, VT switches).
- **Phase A, Phase B**: the SSD disk-image test drive. Phase A puts game data and the swap spill in an image file on dev_hdd0; Phase B boots the whole system from it. docs/GAMEOS-HDD.md.

Measuring

- **stall**: a frame over 100 ms. "Gameplay stalls" leave out a demo's reload.
- **steady fps**: the median frame rate over the measured window.
- **warm-up, window**: every bench run skips the first 60 s after loading (warm-up) and measures at least 120 s after it (the window).
- **1% low**: 1000 divided by the 99th-percentile frame time, in fps.
- **same-boot A/B**: a change measured against its off-switch in the same PS3 session. Numbers from different boots are not compared.
- **fps.csv row N**: the Nth data row of the result matrices in `games/bench/results/`; every CSV there has the same rows.

Packages and versions

- **ps3, ps3-dev, ps3-licensed**: the suites of our apt repository. `ps3` holds stable releases (kept forever), `ps3-dev` the dev builds, `ps3-licensed` game data we may not redistribute (LAN only).
- **`+ps3.N`, `0.N`, `~dev<stamp>`**: version suffixes. A Debian package we rebuild gets `<debver>+ps3.N`, our own packages `0.N`; a dev build adds `~dev<stamp>`. "Mesa .476" or "kernel .472" means `+ps3.476`, `+ps3.472`.
- **local/NNNN**: our kernel patches in `linux/patches/local/` (for example local/0009, the idle fix).
