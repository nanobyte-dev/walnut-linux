# Plan

This is the plan of record: the goal, where we stand, the decisions waiting for the user, and the
work in five stages in priority order. The infrastructure is done and the work is now making games
play well; the per-game state is in [games/README.md](games/README.md), the PS3 test queue in
[TESTS.md](TESTS.md), and everything not yet tried in [backlog/README.md](backlog/README.md).
The hard safety rules are in [CLAUDE.md](../CLAUDE.md); nothing here overrides them.

## Goal

An open, Sony-independent PS3: Linux with a working GPU driver, a desktop, and open-source games
that play well. "Well" means about 60 fps at 1080p for the light games, and smoothness ranks above
average fps (the user, 2026-10-18): a lower frame rate that never stalls beats a higher one with
hitches. So every game is judged on its steady fps together with its stalls (frames over 100 ms),
its 1% low and its longest frame. AAA titles are not the goal.

The long-term form (stated 2026-10-14): a live system that boots from USB or NFS through
Petitboot, with the RSX driver, the desktop and the games working out of the box, plus an optional
installer that puts it on the console's internal disk without touching GameOS's data. That is
Stage 5.

Games that need the original release's data (the user, 2026-10-20): the engine packages go in the
public repository and are smart about their data. On first run, a game without its assets shows a
"First time setup" dialog ("This game requires assets from the original game release"; "Insert the CD
into the optical drive, or provide a source") and copies what it needs from the disc, a disc image, or
an existing install (a GOG or Steam folder, an old Windows install). Nothing copyrighted is ever
published; the user's own copy is the source. That is item 5.10.

## Where we are (2026-10-19)

The whole stack is built and packaged: the kernel (6.12 LTS with our PS3 patches), the ps3rsx
driver, Mesa nv30, the SPU toolchain and libspeq, the desktop with game mode, the splash, our apt
repository. The release kernel `6.12.111+ps3.472` with ps3rsx 0.443 and Mesa `.476` is the
default of the 2026-10-18 release build, which sits in `ps3-dev` waiting for its PS3 session
before anything goes to the stable suite ([RELEASE-CANDIDATES.md](RELEASE-CANDIDATES.md)).

What limits the games, in order: RAM (about 217 MB reach Linux), then the PPE (the driver stack
takes 30-55% of a game's CPU), then the disk. The RSX and the SPEs have headroom. What we learned
in the last three days:

- Fewer GL calls in the game beat driver work by far: X-Moto went from 44.5 to 166.5 fps (18,500 to
  165 GL calls a frame), against +5 to +24% from Mesa's patch rounds.
- A kernel bug caused many of the "random" stalls: an idle PPE thread lost its timer on the way
  into `lv1_pause` and slept up to 1.4 s. The fix (local/0009) is confirmed on the PS3, including a
  clean in-game repeat (batch 16), and is in every current kernel. [STALLS.md](STALLS.md)
- Memory patches in the games remove most of what stalls are left: OpenArena's preload took its
  gameplay stalls from 9-10 s to 0; vertex formats nv30 can fetch took OpenArena's opengl2
  renderer to 57-60 fps with 1-2 s of stalls (9-11 s before, batch 17). [MEMORY.md](MEMORY.md)
- The internal SSD reads at 100 MB/s with 0.1 ms random reads, against the USB disk's 27.8 MB/s
  and 20 ms. Since 2026-10-19 the whole system can boot from a disk image on it (Phase B), and
  tonight's batches run on that SSD root. [GAMEOS-HDD.md](GAMEOS-HDD.md)

Six of the 13 suite games reach 60 fps without stalls worth the name; the heavy ones are limited by
memory. The table is in [games/README.md](games/README.md).

One gap in how we measure: every benchmark runs with the desktop stopped, so game mode, which is
how players start games, has never been benchmarked (item 2.2).

## Open decisions for the user

Other docs cite these numbers; "old N" was the number before the 2026-10-18 restructure.

| # | Decision | Rests on | Item |
|---|---|---|---|
| D1 | Stable publish of the release (old 1). Decided 2026-10-18 as "test first, then publish": everything rebuilt official, an image built, the PS3 image session, then the stable publish with an explicit `DEBS=` list and `build/aptly` backed up. Nothing is published yet. | [RELEASE-CANDIDATES.md](RELEASE-CANDIDATES.md) | 5.1 |
| D2 | The 720p console (old 8): 4 MB of RAM always, but X shares the mode, so the desktop and games run at 720p (2.25x fewer pixels) | [KERNEL-MEMORY.md](KERNEL-MEMORY.md) | 4.10 |
| D3 | dsda-doom's music default (old 8): fluidsynth on the SPEs (chops), OPL, or off | [SPU-AUDIO.md](SPU-AUDIO.md) | 2.8 |
| D4 | SuperTuxKart's `lean` default (old 7): 21.4 smooth fps and 2.4 s of stalls against 13 fps and 13.5 s; only after the sound check | [games/supertuxkart.md](games/supertuxkart.md) | 2.1 |
| D5 | Fan floor and the panic-path test (old 4): a fixed floor (at least 60%, `pwm1`) and the optional panic test, each only with the user's explicit OK | [THERMAL.md](THERMAL.md) | 4.4 |
| D6 | The 7th SPE entry (old 6): a Petitboot entry with `spu_base.enum_shared=1` and a same-boot benchmark | [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md) | 4.2 |
| D7 | XMB-side backups (old 5): a second NOR dump with `cmp`, EEPROM, flags, SYSCON error log, encrypted copies off the PC | [CONSOLE.md](CONSOLE.md) | 4.1 |
| D8 | lv1 peek (old 5): a read-only probe of hypercall 182 and the `gpu-4kb-iopage` site; the RAM it could win is small | [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md) | 4.3 |
| D9 | GameOS auto-mount at boot (read-only), asked on 2026-10-18, no answer yet | [GAMEOS-HDD.md](GAMEOS-HDD.md) | 4.5 |
| D10 | Upstream drafts (old 9): five drafts (Teeworlds, Yamagi, the Mesa LUT, two kernel patches), none sent | [upstream/README.md](upstream/README.md) | 5.8 |
| D11 | Recovery hardware (old 5): a NOR flasher and a SYSCON UART are hardware mods, so only on request; every flash or SYSCON write waits for them | [CONSOLE.md](CONSOLE.md) | 4.1 |
| D12 | Nexuiz install: about 800 MB on the USB disk for one smoke run | [games/smoke-tested.md](games/smoke-tested.md) | 1.12 |
| D13 | The default kernel: the release build makes `.472` (with the idle fix) the default; the stable suite still has the 2026-10-16 builds. Left: when the stopgap kicker goes for good | [RELEASE-CANDIDATES.md](RELEASE-CANDIDATES.md) | 1.2 |
| D14 | Reimage timing: the user said on 2026-10-17 the image rebuild waits until this plan is through; it is in Stage 5 | [IMAGE-DIFF.md](IMAGE-DIFF.md) | 5.4 |

Settled since the list was made: Luanti's package default (base stays, clean runs, row 67).

## The stages

Within a stage the items are in the order to take them. Effort: S = a day or less, M = days,
L = weeks (our work, not the waiting). Needs: **PS3** = runs on the console with nobody at it
(desktop stopped, same-boot baselines), **user** = the user at the console or TV (reboots,
listening, the controller, decisions), **host** = the PC only. Item numbers are stable: other docs
and TESTS.md cite them, so finished items stay in the table with their result.

### Stage 1: close what's in flight

Built or queued work, mostly PS3 runs. Done when every row has a measured verdict.

| # | Item | Status | Needs | Where |
|---|---|---|---|---|
| 1.1 | Clean repeat of the idle fix on/off | **done** (batch 16): confirmed, no SSH poll | PS3 | [STALLS.md](STALLS.md) |
| 1.2 | The fixed kernel as the default, then retire the stopgap kicker | `.472` is the release default; the bench's default `ENVS` is `-` (no kicker); game mode's `ps3-kick` skips itself when `ps3_idle_prep` is on. Left: drop `ps3-kick` once no kernel without the fix is in use (D13) | host, PS3 | [DESKTOP.md](DESKTOP.md), [STALLS.md](STALLS.md) |
| 1.3 | OpenArena opengl2 memory A/B (ioquake3 0027-0029, Mesa 0037) | **done** (batch 17): gl2min 57-60 fps, stalls 1-2 s (9-11 s before); Mesa 0037 cut color16's stalls 12 to 1.5 s | PS3 | [MEMORY.md](MEMORY.md), [games/openarena.md](games/openarena.md) |
| 1.4 | Sauerbraten world normals as shorts (cube2 0011) | **done** (batch 17): +17% | PS3 | [MEMORY.md](MEMORY.md), [games/sauerbraten.md](games/sauerbraten.md) |
| 1.5 | Game prefetch A/B | **done** (batch 16): no gain, Luanti loads slower, default off | PS3 | [STALLS.md](STALLS.md) |
| 1.6 | Internal disk speed test, read-only | **done**: SSD 100 MB/s, 0.1 ms random 4K; USB 27.8 MB/s, 20 ms | PS3 | [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 1.7 | Load and warm-up profiles of 5 games, shader caches cold/warm | batch 17 read: the disk is 34-41% of every load, CPU items 1-3 s per game (SuperTuxKart 5-8), the GLSL cache saves CPU but no wall time. The SSD root (ssdroot2 P1a): loads -39 to -61%, the disk 11-26% of a load, decoders and linking are what is left. Left: Mesa 0039 `NV30_SHADER_CACHE` cold/warm, a symbolised re-run | PS3 | [PROFILING.md](PROFILING.md) |
| 1.8 | local/0041's power paths: `poweroff`, power button taps during shutdown, reboot | open; upstream draft 3 waits on it | user | [THERMAL.md](THERMAL.md) |
| 1.9 | The next kernel's checks | `.457` was never booted; `.472` carries its changes and runs since batch ssd1 (2026-10-19). Left: local/0043's console-frame release checks (`ps3fb.shadow_release=1`, not scheduled yet), `dev_hdd1` with NLS, ps3rsx `shadow_fast` A/B (scheduled ssdroot2 P2) | user, PS3 | [KERNEL-MEMORY.md](KERNEL-MEMORY.md), [HOTPATHS.md](HOTPATHS.md) |
| 1.10 | Game mode at the TV: the escape keys, the automatic start, Luanti from the menu with sound, the desktop's return time, `gamemode-check.sh`, the `ps3-game-kick` unit | open; almost none of it has run on the PS3 | user | [DESKTOP.md](DESKTOP.md) |
| 1.11 | Menu-only smoke games: TORCS, Trigger Rally, Chromium B.S.U. (installed) | open | user | [games/smoke-tested.md](games/smoke-tested.md) |
| 1.12 | Nexuiz Classic smoke on `darkplaces-gl2` | open, needs D12 | user | [games/smoke-tested.md](games/smoke-tested.md) |
| 1.13 | The interference detector's own checks: a key press, an SSH login, a mouse bump, the tty8 screen | open | user | [STALLS.md](STALLS.md) |
| 1.14 | The whole system on the SSD image (Phase B) | the SSD root ran batches ssdroot1 and ssdroot2 (loads -39 to -61%, Sauerbraten's gameplay stalls 1.6 to 0.05 s); left: USB root against SSD root in alternating boots | PS3, user (reboots) | [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 1.15 | The kernel guard (local/0045) on the PS3: a kernel deb with it, then the "On the PS3" checks | built, on in the series since 1abb2a1, not booted | user, PS3 | [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 1.16 | Smoke runs of the new games (ScummVM, Simutrans, Crawl, Freeciv, LinCity-NG, Flare, Widelands, Warmux, Cataclysm) and OpenTTD's gameplay benchmark | first runs done in batch ssdroot3 (seven of nine start; OpenTTD `blit8` 34-43 fps against 11, now the package default); re-runs and the menu-only games are in TESTS.md Next | PS3 | [TESTS.md](TESTS.md), [games/README.md](games/README.md) |

### Stage 2: smoothness and correctness for the suite

The suite as a player sees it: sound, game mode, VRAM, and results that hold up on a second run.

| # | Item | Status | Needs | Where |
|---|---|---|---|---|
| 2.1 | Sound check across all games; fix what is silent (SuperTuxKart's `sound=none` was a read before the game opened its device; RUNNING since the SSD root, still not heard by ear) | open; D4 waits on it | PS3, user by ear | [games/README.md](games/README.md) |
| 2.2 | A benchmark round through game mode, reconciled with the desktop-stopped baselines, same boot | open; no number was ever taken the way games are played | PS3 | [DESKTOP.md](DESKTOP.md) |
| 2.3 | VRAM measurement for OpenArena opengl2 and SuperTuxKart, then a VRAM reserve game mode shrinks | open | PS3, then host | [backlog/memory.md](backlog/memory.md) |
| 2.4 | Texture compression at upload for every game (Mesa 0038, `NV30_FORCE_DXT`) | built, default off; ssdroot2 P2: Sauerbraten -42 MB VRAM with no fps change resolved, SuperTuxKart and OpenArena gl2min force nothing; Luanti, ClassiCube, `all` and `NEAREST` left | PS3 | [MEMORY.md](MEMORY.md) |
| 2.5 | The kernel's OS-area flash write | done: local/0044 (`ps3_os_area.write`, default 0); the passive check passed on 2026-10-20 (no write attempt in a 13 h boot after the NTP sync) | PS3 | [CONSOLE.md](CONSOLE.md) |
| 2.6 | Repeats of single-run results | ClassiCube `chunks1` (+4.7%) and X-Moto's stream buffer (+1%) repeated in batch 16. Left: Sauerbraten `oqlag`, Luanti `view40`, Extreme Tux Racer steady state with stalls, Teeworlds, Armagetron live | PS3 | [backlog/games.md](backlog/games.md) |
| 2.7 | A 30-minute soak of a heavy game, supervised (temperatures, Extreme Tux Racer's GART growth of 4 MB a minute, Luanti's arena growth) | open | PS3, user nearby | [backlog/games.md](backlog/games.md) |
| 2.8 | dsda-doom's chopping music (465 underruns a minute on the SPE pool, 1095 on the PPE); OpenTTD's music shares the path | open; D3 waits on it | PS3, user by ear | [SPU-AUDIO.md](SPU-AUDIO.md) |
| 2.9 | Luanti: which part of `lean` empties the world; `view40` as a menu option | `chunk3` empties the world (batch 16); `view40` open | PS3 | [games/luanti.md](games/luanti.md) |

### Stage 3: performance work

With the stalls gone, the frame rate of the CPU- and GPU-bound games. Details for every row are in
the backlog.

| # | Item | Effort | Where |
|---|---|---|---|
| 3.1 | The remaining game-side batching: Sauerbraten (cube2 0007-0009 exist), SuperTuxKart's legacy renderer, OpenTTD's GL driver; perf first | M per game | [backlog/graphics.md](backlog/graphics.md) |
| 3.2 | Tiling and Z-cull (lv1 attributes 0x300/0x301): ClassiCube is GPU-bound now | M-L | [backlog/graphics.md](backlog/graphics.md) |
| 3.3 | RSX/SPU probes E8b, E16, E17 and the spufs dispatch latency | S each | [backlog/graphics.md](backlog/graphics.md) |
| 3.4 | Indirect constant addressing (ARL) in nv30 vertex programs, for GPU skinning | M-L | [backlog/graphics.md](backlog/graphics.md) |
| 3.5 | Mesa built with nv30 only | S-M | [backlog/memory.md](backlog/memory.md) |
| 3.6 | Sound straight to ALSA, without the pulse plugin and server | M | [backlog/memory.md](backlog/memory.md) |
| 3.7 | OpenArena: trim the hunk after a map load; `r_keepImages` for renderergl2 | S-M | [backlog/games.md](backlog/games.md) |
| 3.8 | Red Eclipse: cube2's memory patches carried over | M | [backlog/games.md](backlog/games.md) |
| 3.9 | Smaller ideas: cheaper Mesa 0028/0031, ADPCM sound effects in ioquake3, half-float vertex data, shader IR dropped after linking, OpenAL Soft's b16/f44 | S-M each | [backlog/README.md](backlog/README.md) |
| 3.10 | Neverball: a longer replay (the user plays about 60 s) | S | [backlog/games.md](backlog/games.md) |
| 3.11 | Kernel stat options (PSI, delay accounting, schedstats) and a debugfs `ps3rsx_stats`, each with its RAM cost | S-M | [backlog/smoothness.md](backlog/smoothness.md) |
| 3.12 | Later: SPU meshing for Luanti and ClassiCube, vertex texture fetch, derivatives, instancing, video decoding on the SPEs (parked by CLAUDE.md) | L | [backlog/spu.md](backlog/spu.md), [backlog/graphics.md](backlog/graphics.md) |

### Stage 4: hardware, safety and platform

The console itself, its safety margin, and the parts of the system a desktop user touches.

| # | Item | Needs | Where |
|---|---|---|---|
| 4.1 | XMB-side backups with xai_plugin: a second NOR dump compared with the first, EEPROM, flags, the SYSCON error log, encrypted copies off the PC (D7; recovery hardware is D11) | user (XMB) | [backlog/hardware.md](backlog/hardware.md) |
| 4.2 | The 7th SPE by boot parameter (D6) | user (reboot), PS3 | [backlog/hardware.md](backlog/hardware.md) |
| 4.3 | lv1 peek probe, read-only (D8) | user OK, PS3 | [backlog/hardware.md](backlog/hardware.md) |
| 4.4 | Fan floor and the panic-path test (D5) | user | [THERMAL.md](THERMAL.md) |
| 4.5 | GameOS auto-mount at boot, or keep it on demand (D9) | user decision | [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 4.6 | Validator checks for the vertex texture methods 0x900-0x97c (a client can wedge the RSX there today; required before vertex texture fetch) | host, PS3 | [backlog/graphics.md](backlog/graphics.md) |
| 4.7 | The DualShock 3 in play: SDL mappings, rumble, LEDs, a couch session with only the controller | user | [backlog/platform.md](backlog/platform.md) |
| 4.8 | Internal Wi-Fi (the hypervisor keeps the chip under OtherOS++) | user | [backlog/platform.md](backlog/platform.md) |
| 4.9 | Power: the power button and shutdown on the default kernel (after 1.8), Remote Start by Wake-on-LAN, an idle auto-off | user | [backlog/platform.md](backlog/platform.md) |
| 4.10 | Display variety: 720p (D2), overscan on TVs, 50 Hz modes | user, a TV | [backlog/platform.md](backlog/platform.md) |
| 4.11 | The hardware cursor (its init froze the PS3 once) and tearing | user | [backlog/platform.md](backlog/platform.md) |
| 4.12 | The USB disk: a SMART watch and its write load. The internal-disk question is answered by the SSD image (1.14); a reformatted Linux region stays research | PS3 | [GAMEOS-HDD.md](GAMEOS-HDD.md), [research/HDD-REGION.md](research/HDD-REGION.md) |
| 4.13 | Note for the user docs: the escape keys kill the game, unsaved progress is lost (goes into 5.5) | host | [DESKTOP.md](DESKTOP.md) |
| 4.14 | Desktop application quality: the browser, video in mpv, responsiveness, the `dcbz` memcpy trap | PS3 | [backlog/platform.md](backlog/platform.md) |
| 4.15 | Dev loop: does a Petitboot-only reboot clear a wedged RSX, can the CPU read VRAM after a graph exception, kexec from our Linux | user | [backlog/platform.md](backlog/platform.md) |

### Stage 5: preparing for release

Everything meant for other users. Nothing here is published without the user's OK.

| # | Item | Status | Where |
|---|---|---|---|
| 5.1 | Stable publish of the release (D1), then a written release process | every package rebuilt official (kernel `.472` the default, Mesa `.476`, ...) and in `ps3-dev`; the stable command with its `DEBS=` list is written, not run, until the image session passes | [RELEASE-CANDIDATES.md](RELEASE-CANDIDATES.md) |
| 5.2 | A repository for others: a public host, signing and a published key, mirrors; `ps3-licensed` stays LAN-only | open (today: tibi-server on the LAN, unsigned) | [backlog/platform.md](backlog/platform.md) |
| 5.3 | The installer (GameOS app design), `make iso`, `make disk-image`, first-boot assumptions | designed, not built | [backlog/platform.md](backlog/platform.md), [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 5.4 | Image rebuild and a reimage of the USB disk (D14) | the image is built (2026-10-18, 2.9 GiB, sha256 `65dae499...`); its PS3 session is pending | [RELEASE-CANDIDATES.md](RELEASE-CANDIDATES.md), [IMAGE-DIFF.md](IMAGE-DIFF.md) |
| 5.5 | End-user docs: booting through Petitboot (the fan and boot-flag steps), game mode and its keys, which games work, licensed data, OtherOS++ safety | open | [backlog/platform.md](backlog/platform.md) |
| 5.6 | Security update policy: the sid snapshot plus Debian's live ports, when we rebase, SSH exposure | open | [PACKAGING.md](PACKAGING.md) |
| 5.7 | Other PS3 models (fats, other slims, the QCFW/BadWDSD news for 25xx-40xx); everything is tested on one CECH-20xx | open (research) | [backlog/hardware.md](backlog/hardware.md) |
| 5.8 | Upstream submissions (D10), rebased; Mesa nv30 patches tested first on the Core 2 + 7600 GT box (stock x86 nouveau, piglit/deqp), that box's only job (the user, 2026-10-18) | drafts, none sent | [upstream/README.md](upstream/README.md) |
| 5.9 | Host test suites run automatically | `make test` / `make test-slow` exist (tools/run-tests.sh); not in CI | [backlog/platform.md](backlog/platform.md) |
| 5.10 | First-run game data setup (the goal above): one shared setup tool the engines' launchers call when assets are missing; sources: the PS3's optical drive, an image file (ISO, BIN/CUE), an install folder (GOG, Steam, Windows), a GOG installer; per-game extract recipes (game-data-packager where it knows the game, else ours, e.g. `tools/rct2-data.sh`'s unshield steps for RCT2); the files go to the game's data directory. Replaces the LAN-only data packages for end users | goal set, not designed | [PC-CLASSICS.md](research/PC-CLASSICS.md) (the games that need it), [PACKAGING.md](PACKAGING.md#game-data) |

## Where the rest went

The base system and toolchain decisions and the build setup are in [PACKAGING.md](PACKAGING.md).
The console, the test loop and its gotchas are in [CONSOLE.md](CONSOLE.md). The open questions
each became a stage item above. The plan as it stood before 2026-10-19, with its dated history,
work queue and older numbers, is [archive/PLAN.md](archive/PLAN.md); the original wish list is
`backlog.txt`.
