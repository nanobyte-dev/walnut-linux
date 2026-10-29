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

Games that need the original release's data: the engine packages go in the public repository and
Walnut Games sets up the data from the player's own copy (a disc, a disc image, a GOG or Steam
folder, an installer; "Add game files..." in the app, `walnut-data import` in a terminal). Nothing
copyrighted is ever published. That is item 5.10.

## Next up (the user, 2026-10-23)

Walnut 0.1.0, the first public alpha, is out ([release/ALPHA.md](release/ALPHA.md)). In this order:

1. A round of cleanups (disk space on the PC).
2. An Atomic Bomberman clone on the PS3, packaged.
3. Then what the release interrupted, now one stage: **Walnut Games** ([GAMEDB.md](GAMEDB.md)), a
   games database at walnut.nanobyte.dev/gamedb/ and a launcher app on Walnut, every game played,
   measured and compared with the PC, rated with medals. Steps:
   - P0 the rating, calibrated on the 13 benchmarked games (done, the user's OK);
   - P1 the catalog (one folder per game), the scoring and import tools, the Hugo site (3 games);
   - P2 the server: screenshots as their own upload kind, /gamedb/ published;
   - P3 the session recorder (the ~10-minute played session, its end-of-session form);
   - P4 the desktop app (Tk) and its install helper;
   - P5 the games one by one to about 50, new games first. Rules 2.0 (2026-10-26, [GAMEDB.md](GAMEDB.md#rules-versions))
     rate the sessions from here on: each game runs on its shipped defaults (the player's settings set
     aside and back), the recorder measures the input, the pointer, the level load and the quit, the
     importing agent answers a picture questionnaire (review.py), the form asks eight one-tap questions.
   - Scripted runs ([games/SUITE.md](games/SUITE.md#scripted-runs)) are the benchmark harness for measuring game and
     GPU-driver optimizations (A/B runs, regressions, profiling). They never rate a game: ratings come from played sessions only.
   - P6, later (the user, 2026-10-26: "keep planned"): tests submitted by other Walnut users. Their
     sessions would be recorded by the same harness and form, sent to us and reviewed before they
     count. That needs a real database and a way to trust or weigh submissions (one player's odd
     setup must not move a medal), so it waits until P5's catalog is settled.

The rest of this file predates the 0.1.0 release and gets a refresh alongside P2.

## Where we are (2026-10-19)

The whole stack is built and packaged: the kernel (6.12 LTS with our PS3 patches), the ps3rsx
driver, Mesa nv30, the SPU toolchain and libspeq, the desktop with game mode, the splash, our apt
repository. The release kernel `6.12.111+ps3.472` with ps3rsx 0.443 and Mesa `.476` is the
default of the 2026-10-18 release build ([archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md)). Releases
now go through CI to walnut.nanobyte.dev's stable suite (docs/release/ALPHA.md); the LAN repository
that build waited in was retired on 2026-10-26 (docs/PACKAGING.md).

What limits the games, in order: RAM (about 217 MB reach Linux), then the PPE (the driver stack
takes 30-55% of a game's CPU), then the disk. The RSX and the SPEs have headroom. What we learned
in the last three days:

- Fewer GL calls in the game beat driver work by far: [X-Moto](../gamedb/games/xmoto/) went from 44.5 to 166.5 fps (18,500 to
  165 GL calls a frame), against +5 to +24% from Mesa's patch rounds.
- A kernel bug caused many of the "random" stalls: an idle PPE thread lost its timer on the way
  into `lv1_pause` and slept up to 1.4 s. The fix (local/0009) is confirmed on the PS3, including a
  clean in-game repeat (batch 16), and is in every current kernel. [STALLS.md](STALLS.md)
- Memory patches in the games remove most of what stalls are left: [OpenArena](../gamedb/games/openarena/)'s preload took its
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
| D1 | Stable publish of the release (old 1). Decided 2026-10-18 as "test first, then publish": everything rebuilt official, an image built, the PS3 image session, then the stable publish with an explicit `DEBS=` list and `build/aptly` backed up. Nothing is published yet. | [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md) | 5.1 |
| D2 | The 720p console (old 8): 4 MB of RAM always, but X shares the mode, so the desktop and games run at 720p (2.25x fewer pixels) | [KERNEL-MEMORY.md](KERNEL-MEMORY.md) | 4.10 |
| D3 | dsda-doom's music default (old 8): fluidsynth on the SPEs (chops), OPL, or off | [SPU-AUDIO.md](SPU-AUDIO.md) | gamedb:dsda-doom (was 2.8) |
| D4 | SuperTuxKart's `lean` default (old 7): 21.4 smooth fps and 2.4 s of stalls against 13 fps and 13.5 s. Its package's patch 0006 now makes `lean` a fresh configuration's default; the sound check by ear is still open | [technical notes](../gamedb/games/supertuxkart/technical.md) | 2.1 |
| D5 | Fan floor and the panic-path test (old 4): a fixed floor (at least 60%, `pwm1`) and the optional panic test, each only with the user's explicit OK | [THERMAL.md](THERMAL.md) | 4.4 |
| D6 | The 7th SPE entry (old 6): a Petitboot entry with `spu_base.enum_shared=1` and a same-boot benchmark | [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md) | 4.2 |
| D7 | XMB-side backups (old 5): a second NOR dump with `cmp`, EEPROM, flags, SYSCON error log, encrypted copies off the PC | [CONSOLE.md](CONSOLE.md) | 4.1 |
| D8 | lv1 peek (old 5): a read-only probe of hypercall 182 and the `gpu-4kb-iopage` site; the RAM it could win is small | [research/FLASH-SYSCON.md](research/FLASH-SYSCON.md) | 4.3 |
| D9 | GameOS auto-mount at boot (read-only), asked on 2026-10-18, no answer yet | [GAMEOS-HDD.md](GAMEOS-HDD.md) | 4.5 |
| D10 | Upstream drafts (old 9): five drafts ([Teeworlds](../gamedb/games/teeworlds/), Yamagi, the Mesa LUT, two kernel patches), none sent | [upstream/README.md](upstream/README.md) | 5.8 |
| D11 | Recovery hardware (old 5): a NOR flasher and a SYSCON UART are hardware mods, so only on request; every flash or SYSCON write waits for them | [CONSOLE.md](CONSOLE.md) | 4.1 |
| D12 | Nexuiz install: about 800 MB on the USB disk for one smoke run | [technical notes](../gamedb/games/nexuiz/technical.md) | gamedb:nexuiz (was 1.12) |
| D13 | The default kernel: the release build makes `.472` (with the idle fix) the default; the stable suite still has the 2026-10-16 builds. Left: when the stopgap kicker goes for good | [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md) | 1.2 |
| D14 | Reimage timing: the user said on 2026-10-17 the image rebuild waits until this plan is through; it is in Stage 5 | [IMAGE-DIFF.md](IMAGE-DIFF.md) | 5.4 |

Settled since the list was made: [Luanti](../gamedb/games/luanti/)'s package default (base stays, clean runs, row 67).

## The stages

Within a stage the items are in the order to take them. Effort: S = a day or less, M = days,
L = weeks (our work, not the waiting). Needs: **PS3** = runs on the console with nobody at it
(desktop stopped, same-boot baselines), **user** = the user at the console or TV (reboots,
listening, the controller, decisions), **host** = the PC only. Item numbers are stable: other docs
and TESTS.md cite them, so finished items stay in the table with their result.

The stages hold the work that spans games or is the system's. An item about one game is in that
game's backlog in GameDB (`gamedb/games/<id>/todo.toml`, its `source` naming the item, "PLAN 2.9"),
listed under [Per-game items](#per-game-items) below; those numbers aren't reused.

### Stage 1: close what's in flight

Built or queued work, mostly PS3 runs. Done when every row has a measured verdict. Done and about
one game: 1.3 ([OpenArena](../gamedb/games/openarena/)'s opengl2 memory) and 1.4 ([BananaBread](../gamedb/games/bananabread/)'s world normals as shorts), both
measured in batch 17 (gamedb:batch/2026-10-18-batch17). Moved to their games: 1.11 (the menu-only
smoke games), 1.12 (Nexuiz).

| # | Item | Status | Needs | Where |
|---|---|---|---|---|
| 1.1 | Clean repeat of the idle fix on/off | **done** (batch 16): confirmed, no SSH poll | PS3 | [STALLS.md](STALLS.md) |
| 1.2 | The fixed kernel as the default, then retire the stopgap kicker | `.472` is the release default; the bench's default `ENVS` is `-` (no kicker); game mode's `ps3-kick` skips itself when `ps3_idle_prep` is on. Left: drop `ps3-kick` once no kernel without the fix is in use (D13) | host, PS3 | [DESKTOP.md](DESKTOP.md), [STALLS.md](STALLS.md) |
| 1.5 | Game prefetch A/B | **done** (batch 16): no gain, [Luanti](../gamedb/games/luanti/) loads slower, default off | PS3 | [STALLS.md](STALLS.md) |
| 1.6 | Internal disk speed test, read-only | **done**: SSD 100 MB/s, 0.1 ms random 4K; USB 27.8 MB/s, 20 ms | PS3 | [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 1.7 | Load and warm-up profiles of 5 games, shader caches cold/warm | batch 17 read: the disk is 34-41% of every load, CPU items 1-3 s per game ([SuperTuxKart](../gamedb/games/supertuxkart/) 5-8), the GLSL cache saves CPU but no wall time. The SSD root (ssdroot2 P1a): loads -39 to -61%, the disk 11-26% of a load, decoders and linking are what is left. Left: Mesa 0039 `NV30_SHADER_CACHE` cold/warm, a symbolised re-run | PS3 | [PROFILING.md](PROFILING.md) |
| 1.8 | local/0041's power paths: `poweroff`, power button taps during shutdown, reboot | open; upstream draft 3 waits on it | user | [THERMAL.md](THERMAL.md) |
| 1.9 | The next kernel's checks | `.457` was never booted; `.472` carries its changes and runs since batch ssd1 (2026-10-19). Left: local/0043's console-frame release checks (`ps3fb.shadow_release=1`, not scheduled yet), `dev_hdd1` with NLS, ps3rsx `shadow_fast` A/B (scheduled ssdroot2 P2) | user, PS3 | [KERNEL-MEMORY.md](KERNEL-MEMORY.md), [HOTPATHS.md](HOTPATHS.md) |
| 1.10 | Game mode at the TV: the escape keys, the automatic start, Luanti from the menu with sound, the desktop's return time, `gamemode-check.sh`, the `ps3-game-kick` unit | open; almost none of it has run on the PS3 | user | [DESKTOP.md](DESKTOP.md) |
| 1.13 | The interference detector's own checks: a key press, an SSH login, a mouse bump, the tty8 screen | open | user | [STALLS.md](STALLS.md) |
| 1.14 | The whole system on the SSD image (Phase B) | the SSD root ran batches ssdroot1 and ssdroot2 (loads -39 to -61%, BananaBread's gameplay stalls 1.6 to 0.05 s); left: USB root against SSD root in alternating boots | PS3, user (reboots) | [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 1.15 | The kernel guard (local/0045) on the PS3: a kernel deb with it, then the "On the PS3" checks | built, on in the series since 1abb2a1, not booted | user, PS3 | [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 1.16 | Smoke runs of the new games ([ScummVM](../gamedb/games/scummvm/), [Simutrans](../gamedb/games/simutrans/), Crawl, [Freeciv](../gamedb/games/freeciv/), [LinCity-NG](../gamedb/games/lincity-ng/), [Flare](../gamedb/games/flare/), [Widelands](../gamedb/games/widelands/), [Warmux](../gamedb/games/warmux/), Cataclysm) and [OpenTTD](../gamedb/games/openttd/)'s gameplay benchmark | first runs done in batch ssdroot3 (seven of nine start; OpenTTD `blit8` 34-43 fps against 11, now the package default); the re-runs and the menu-only games are each game's todo item (TESTS.md Next lists them) | PS3 | [TESTS.md](TESTS.md), [games/README.md](games/README.md) |

### Stage 2: smoothness and correctness for the suite

The suite as a player sees it: sound, game mode, VRAM, and results that hold up on a second run.
Moved to their games: 2.8 ([dsda-doom](../gamedb/games/dsda-doom/)'s chopping music, which [OpenTTD](../gamedb/games/openttd/)'s shares; decision D3) and 2.9
([Luanti](../gamedb/games/luanti/)'s `view40`).

| # | Item | Status | Needs | Where |
|---|---|---|---|---|
| 2.1 | Sound check across all games; fix what is silent ([SuperTuxKart](../gamedb/games/supertuxkart/)'s `sound=none` was a read before the game opened its device; RUNNING since the SSD root, still not heard by ear) | open; D4 waits on it | PS3, user by ear | [games/README.md](games/README.md) |
| 2.2 | A benchmark round through game mode, reconciled with the desktop-stopped baselines, same boot | open; no number was ever taken the way games are played | PS3 | [DESKTOP.md](DESKTOP.md) |
| 2.3 | VRAM measurement for [OpenArena](../gamedb/games/openarena/) opengl2 and SuperTuxKart, then a VRAM reserve game mode shrinks | open | PS3, then host | [backlog/memory.md](backlog/memory.md) |
| 2.4 | Texture compression at upload for every game (Mesa 0038, `NV30_FORCE_DXT`) | built, default off; ssdroot2 P2: [BananaBread](../gamedb/games/bananabread/) -42 MB VRAM with no fps change resolved, SuperTuxKart and OpenArena gl2min force nothing; Luanti, [ClassiCube](../gamedb/games/classicube/), `all` and `NEAREST` left | PS3 | [MEMORY.md](MEMORY.md) |
| 2.5 | The kernel's OS-area flash write | done: local/0044 (`ps3_os_area.write`, default 0); the passive check passed on 2026-10-20 (no write attempt in a 13 h boot after the NTP sync) | PS3 | [CONSOLE.md](CONSOLE.md) |
| 2.6 | Repeats of single-run results | ClassiCube `chunks1` (+4.7%) and [X-Moto](../gamedb/games/xmoto/)'s stream buffer (+1%) repeated in batch 16. Left: one benchmark item each in BananaBread, Luanti, [Extreme Tux Racer](../gamedb/games/etr/), [Teeworlds](../gamedb/games/teeworlds/) and [Armagetron Advanced](../gamedb/games/armagetronad/) ([Per-game items](#per-game-items)) | PS3 | TESTS.md Next |
| 2.7 | A 30-minute soak of a heavy game, supervised (temperatures, Extreme Tux Racer's GART growth of 4 MB a minute, Luanti's arena growth) | open | PS3, user nearby | [backlog/smoothness.md](backlog/smoothness.md#long-session-soak) |

### Stage 3: performance work

With the stalls gone, the frame rate of the CPU- and GPU-bound games. Details for every row are in
the backlog. Moved to their games: 3.7 ([OpenArena](../gamedb/games/openarena/)'s hunk trim and renderergl2 `r_keepImages`), 3.8
([Red Eclipse](../gamedb/games/redeclipse/)'s memory patches), 3.10 ([Neverball](../gamedb/games/neverball/)'s longer replay).

| # | Item | Effort | Where |
|---|---|---|---|
| 3.1 | The remaining game-side batching, perf first: the method is in the backlog, the work one item per game ([BananaBread](../gamedb/games/bananabread/)'s cube2 0007-0009 unmeasured, [SuperTuxKart](../gamedb/games/supertuxkart/)'s legacy renderer, [OpenTTD](../gamedb/games/openttd/)'s GL driver) | M per game | [backlog/graphics.md](backlog/graphics.md#game-side-batching-in-the-remaining-games) |
| 3.2 | Tiling and Z-cull (lv1 attributes 0x300/0x301): [ClassiCube](../gamedb/games/classicube/) is GPU-bound now | M-L | [backlog/graphics.md](backlog/graphics.md) |
| 3.3 | RSX/SPU probes E8b, E16, E17 and the spufs dispatch latency | S each | [backlog/graphics.md](backlog/graphics.md) |
| 3.4 | Indirect constant addressing (ARL) in nv30 vertex programs, for GPU skinning | M-L | [backlog/graphics.md](backlog/graphics.md) |
| 3.5 | Mesa built with nv30 only | S-M | [backlog/memory.md](backlog/memory.md) |
| 3.6 | Sound straight to ALSA, without the pulse plugin and server | M | [backlog/memory.md](backlog/memory.md) |
| 3.9 | Smaller ideas: cheaper Mesa 0028/0031, ADPCM sound effects in [ioquake3](../gamedb/engines/ioquake3/), half-float vertex data, shader IR dropped after linking, OpenAL Soft's b16/f44 | S-M each | [backlog/README.md](backlog/README.md) |
| 3.11 | Kernel stat options (PSI, delay accounting, schedstats) and a debugfs `ps3rsx_stats`, each with its RAM cost | S-M | [backlog/smoothness.md](backlog/smoothness.md) |
| 3.12 | Later: vertex texture fetch, derivatives, instancing, video decoding on the SPEs (parked by CLAUDE.md); SPU meshing is a [Luanti](../gamedb/games/luanti/) and a ClassiCube item | L | [backlog/spu.md](backlog/spu.md), [backlog/graphics.md](backlog/graphics.md) |

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
| 4.16 | Walnut's browser on WebKitGTK (the user, 2026-10-28): the `badwolf` shell is packaged (one web process for all tabs, hardware acceleration off, JavaScript on, the Walnut User-Agent the GameDB site reads). Debian's WebKitGTK 2.52.6 aborts on nearly every page on the PS3 (big-endian CompactVariant, WebKit bug 300442) and 2.54 has no big-endian build, so the user chose our own 2.52.6 build (option 2): `webkit2gtk 2.52.6-1+ps3.N` with the big-endian fixes, the GTK 3 flavour only, slimmed (no WebGL, WebRTC, WebAudio, speech, ...; video kept to try), built on the PC (`BUILD=xcc`); measured on the PC, 2.52 gets no upstream security fixes and we carry it. Porting Skia to big-endian is in the backlog | on the PS3 since 2026-10-29 (installed for the user to try, not the default): right but slow (~20 s a page, paging), video a slideshow; next: what memory to free while the browser is in front, the WebKit EGL fix, drop video? | [research/BROWSERS.md](research/BROWSERS.md#where-it-stands), [DESKTOP.md](DESKTOP.md#the-browser) |
| 4.15 | Dev loop: does a Petitboot-only reboot clear a wedged RSX, can the CPU read VRAM after a graph exception, kexec from our Linux | user | [backlog/platform.md](backlog/platform.md) |

### Stage 5: preparing for release

Everything meant for other users. Nothing here is published without the user's OK.

| # | Item | Status | Where |
|---|---|---|---|
| 5.1 | Stable publish of the release (D1), then a written release process | every package rebuilt official (kernel `.472` the default, Mesa `.476`, ...) and in `ps3-dev`; the stable command with its `DEBS=` list is written, not run, until the image session passes | [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md) |
| 5.2 | A repository for others: a public host, signing and a published key, mirrors; licensed data only for the owner | done: walnut.nanobyte.dev (2026-10-21; signed suites built by CI, the licensed suite password-protected, published from the PC); the LAN repository on tibi-server retired 2026-10-26; mirrors not done | [backlog/platform.md](backlog/platform.md) |
| 5.3 | The installer (GameOS app design), `make iso`, `make disk-image`, first-boot assumptions | designed, not built | [backlog/platform.md](backlog/platform.md), [GAMEOS-HDD.md](GAMEOS-HDD.md) |
| 5.4 | Image rebuild and a reimage of the USB disk (D14) | the image is built (2026-10-18, 2.9 GiB, sha256 `65dae499...`); its PS3 session is pending | [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md), [IMAGE-DIFF.md](IMAGE-DIFF.md) |
| 5.5 | End-user docs: booting through Petitboot (the fan and boot-flag steps), game mode and its keys, which games work, licensed data, OtherOS++ safety | open | [backlog/platform.md](backlog/platform.md) |
| 5.6 | Security update policy: the sid snapshot plus Debian's live ports, when we rebase, SSH exposure | open | [PACKAGING.md](PACKAGING.md) |
| 5.7 | Other PS3 models (fats, other slims, the QCFW/BadWDSD news for 25xx-40xx); everything is tested on one CECH-20xx | open (research) | [backlog/hardware.md](backlog/hardware.md) |
| 5.8 | Upstream submissions (D10), rebased; Mesa nv30 patches tested first on the Core 2 + 7600 GT box (stock x86 nouveau, piglit/deqp), that box's only job (the user, 2026-10-18) | drafts, none sent | [upstream/README.md](upstream/README.md) |
| 5.9 | Host test suites run automatically | `make test` / `make test-slow` exist (tools/run-tests.sh); not in CI | [backlog/platform.md](backlog/platform.md) |
| 5.10 | Game data import in Walnut Games: the player picks a disc in the drive, an ISO (udisks loop mount), a BIN/CUE or an Alcohol 120% MDF/MDS (data track to an ISO, audio tracks to Ogg Vorbis), an installer or a folder; our patched game-data-packager (`packages/patched/tools/game-data-packager`) runs as the player and builds the data deb; the root helper takes only its file stream (`walnut-games-helper import-data ID PKG`), checks every file against a manifest from game-data-packager's data, writes the package itself and installs it. Also the CLI `walnut-data` (list, import, remove, install-repo), the app's "Add game files..." button and chooser, "Install from Walnut licensed" for the licensed suite, `[data_setup]` per game in GameDB with editions that the ratings respect, launchers stamped `X-Walnut-Game` and shown when their data exists (`ps3-game-menu --reload`). Games: OpenRCT2 (pilot), Doom, Doom II, Final Doom, Heretic, Hexen, Quake, Quake II, Quake III Arena (with Team Arena), OpenTTD's original graphics | implemented; imports proven on the PS3 for RCT2 (ISO, CD files, MDF/MDS, GOG 2012 with the expansions, TTP, the Steam folder: TESTS rct2-sources), Doom/Doom II/Final Doom/Heretic, Hexen (GOG 1.1, Mac), Quake and Quake II (BIN/CUE with CD music), Quake's 2021 re-release with its four add-ons (GOG's installer: the base first, then each add-on it names), Quake III (CD; GOG with Team Arena), TTD (+FM music); each removed and imported again, the plan's Verification items checked (2026-10-28, TESTS data-verify). Launchers whose data is missing stay out of the menu and the app's Play (QuakeSpasm's and Quake II's too since 2026-10-28). Waiting for a disc in the PS3's drive (2026-10-29: only a data CD, CM8330, is in it): RCT2 from its physical CD and CD-audio ripping from an audio CD; optional: Hexen 1.0 via Raven's RTPatch update in DOSBox. Future, not now (the user, 2026-10-27): engines for the GameDB candidates (ECWolf, EDuke32, iortcw, ET, Shockolate, uHexen2, Doom64 EX, SeriousSamClassic) | [pc-classics.md](../gamedb/research/pc-classics.md) (the games that need it), [PACKAGING.md](PACKAGING.md#game-data), [GAMEDB.md](GAMEDB.md) |
| 5.11 | Walnut Games and the GameDB site redesigned (the user, 2026-10-28), one design for both: a game's page has tabs Overview, Test runs, Technical, Guides, each a separate generated page (no JavaScript); Test runs shows played sessions (rated) apart from scripted benchmarks (not rated); Guides holds how-tos, tips and recommended settings (`gamedb/games/<id>/guides/*.md`). The site's styles move to SCSS (Hugo extended): custom properties and mixins in the source, compiled down to CSS 2.1 that NetSurf and the app's HTML view render (modern layout only as a later declaration that old engines drop). In the app, the right-hand pane holds the actions (one main button that follows the state: Install, Add game files..., Play with its launchers; Remove and Remove data below) and a quick glance (medal and score, up to 4 screenshots, a few numbers, links); Details opens a full-screen view of the same pages in a lightweight HTML widget (Tkhtml through tkinterweb, not WebKit), its buttons as walnut:// links the app handles. Pad and TV first (1080p from the couch). Comments: backlog. The site part is built (2026-10-28): the tabs, the SCSS, `build.sh --app` for the app's copy with walnut:// links. The app's side is built (2026-10-28): the pane (main button by state, smaller ones, glance), Details in tkinterweb with the packaged `--app` pages, the d-pad, L1/R1, Square and Circle, text scaled for 1080p, walnut://game/<id> from the website into one running app, the tag filter; checked on the PS3 (2026-10-28): the pane in the Install and Play states, Details' tabs, a walnut://game link through xdg-open into the running app, its memory steady over 5 opens | built | [GAMEDB.md](GAMEDB.md#the-website), [GAMEDB.md](GAMEDB.md#the-desktop-app), [backlog/games.md](backlog/games.md#comments-on-a-games-page) |

### Per-game items

The games (and engines) whose backlog has items from this plan, and which items; each game's whole
backlog, the played-session queue's place included, is its todo.toml. The full list across games is in
[backlog/games.md](backlog/games.md). Generated from the todo.toml files (`tools/gamedb/blocks.py`).

<!-- gamedb:todo-summary ../gamedb plan -->
| Game | Open items | Kinds | From the plan |
|---|---|---|---|
| [Armagetron Advanced](../gamedb/games/armagetronad/todo.toml) | 2 | 1 benchmark, 1 session | 2.6 |
| [BananaBread](../gamedb/games/bananabread/todo.toml) | 6 | 4 benchmark, 1 package, 1 session | 2.6, 3.1 |
| [ClassiCube](../gamedb/games/classicube/todo.toml) | 4 | 1 fix, 2 package, 1 research | 3.12 |
| [dsda-doom](../gamedb/games/dsda-doom/todo.toml) | 2 | 1 research, 1 session | 2.8 |
| [Extreme Tux Racer](../gamedb/games/etr/todo.toml) | 3 | 1 benchmark, 1 research, 1 session | 2.6 |
| [Luanti](../gamedb/games/luanti/todo.toml) | 6 | 1 benchmark, 1 package, 3 research, 1 session | 2.6, 2.9, 3.12 |
| [Neverball](../gamedb/games/neverball/todo.toml) | 2 | 1 benchmark, 1 session | 3.10 |
| [Nexuiz Classic](../gamedb/games/nexuiz/todo.toml) | 1 | 1 benchmark | 1.12 |
| [OpenArena](../gamedb/games/openarena/todo.toml) | 6 | 2 benchmark, 2 fix, 1 package, 1 session | 3.7 |
| [OpenTTD](../gamedb/games/openttd/todo.toml) | 5 | 2 benchmark, 1 fix, 1 research, 1 session | 2.8, 3.1 |
| [Red Eclipse](../gamedb/games/redeclipse/todo.toml) | 1 | 1 fix | 3.8 |
| [SuperTuxKart](../gamedb/games/supertuxkart/todo.toml) | 5 | 2 fix, 1 package, 1 research, 1 session | 2.1, 3.1 |
| [Teeworlds](../gamedb/games/teeworlds/todo.toml) | 1 | 1 benchmark | 2.6 |
| [ioquake3 (engine)](../gamedb/engines/ioquake3/todo.toml) | 1 | 1 fix | 3.9 |
<!-- /gamedb:todo-summary -->

## Where the rest went

The base system and toolchain decisions and the build setup are in [PACKAGING.md](PACKAGING.md).
The console, the test loop and its gotchas are in [CONSOLE.md](CONSOLE.md). The open questions
each became a stage item above. The plan as it stood before 2026-10-19, with its dated history,
work queue and older numbers, is [archive/PLAN.md](archive/PLAN.md); the original wish list is
`backlog.txt`.
