# Plan: modern Linux with a working GPU on the PS3

Hard constraints are in CLAUDE.md (no permanent changes beyond installing Linux, GameOS stays the
default boot OS, no syscon writes beyond the fan policy of 2026-10-18, supervised sessions). The
original wish list is `backlog.txt`. This file is the plan of record with its progress, brought up
to date on 2026-10-18 (afternoon, then again in the evening after the 419/433 boots and the clean
Luanti re-runs); the open ideas, ranked, are in docs/IDEAS.md, the per-game detail in
docs/GAME-PLAN.md.

## Goal

An open, Sony-independent PS3: Linux with a working GPU driver, a desktop, and open-source games that
play well. Played well means about 60 fps at 1080p for the light games, and **smoothness ranks above
average fps** (the user's rule, 2026-10-18): a lower frame rate that never stalls beats a higher one
with hitches, so every game is judged on its steady fps together with its stalls (frames over
100 ms), its 1% low and its longest frame. AAA titles are not the goal. The long-term form is a live
system that boots from USB or NFS through Petitboot, with the RSX driver, desktop and games working
out of the box, and an optional installer for the OtherOS++ Linux region (see "Long-term goal").

## Summary (2026-10-18)

**Where we are.** The infrastructure is finished and published to our own repository (kernel, ps3rsx,
Mesa nv30, SPU toolchain, desktop, splash, packaging). The work is in the phase "games from playable
to good". RAM (217 MB for Linux) is the binding constraint, then the PPE (the driver stack takes
30-55% of a game's CPU), not the RSX or the SPEs. The last two days found and measured most of what
decides smoothness:

- **Fewer GL calls in the game beats driver work by far.** X-Moto 44.5 -> 166.5 fps (18,500 -> 165 GL
  calls a frame), Extreme Tux Racer 52 -> 59, against +5 to +24% from Mesa rounds 0015-0031.
- **A kernel bug caused many of the "random" stalls.** The decrementer of an idle PPE thread is lost
  on the way into `lv1_pause`: a probe on the idle PS3 saw wakeups up to 1439 ms late on thread 1, and
  none over 19.4 ms with a 10 ms kicker (docs/STALLS.md). OpenArena's gameplay stalls fell from 16.1 and
  13.3 s to 6.5 and 6.2 s with the kicker alone. The fix (local/0009) is **confirmed on the PS3**
  (evening, kernel .419): worst wakeup of an idle thread 4 ms with it, 1103 ms with its switch off, same
  boot; OpenArena's gameplay stalls 0.5 and 0.1 s with it against 8.2 and 5.6 s without (row 66, an SSH
  poll ran through those four runs: repeat clean). Kernel .433 booted after it with the fix, the
  temperatures, the fan policy, gelic and spufs all working. Game mode's stopgap kicker (`ps3-kick`)
  stays for kernels without the fix and skips itself when `ps3_idle_prep` is on.
- **Memory patches in the games** remove the stalls that are left: OpenArena's preload (ioquake3
  0025-0026) took gameplay stalls from 9.9 and 9.0 s to 0, its longest frame from 1057 and 807 ms to
  64 and 47 ms, and steady fps from 64.6 to 70.4; Yamagi Quake II's small PRNG table saves 70 MB
  (stalls 22.6 -> 0.05 s). Luanti's `cliunload` looked like 6.5 -> 0.8 s, but that base run was
  interfered with; the clean re-runs (row 67) show it **worse** (3.8 and 5.2 s, 2 s reload hitches)
  against base's 1.5 and 2.1 s, so base stays.
- **Mesa's translate LUT** (patch 0033, now the default) gave Sauerbraten +53% (25.9 -> 39.6 fps) on
  the earlier spectator demo (row 57). The first-person demo used since row 64 is heavier and swings
  a lot from run to run (base 23-38 steady); the LUT was not A/B'd again on it.
- **Tools to see why**: the stall recorder (`STALLS=1`), the perf and memory profiles (`PERF=1`,
  `MEM=1`), a webcam photo at 30 and 60 s of every run, same-boot A/B rows in `games/bench/results/`.

**Games.** Of the 13 suite games, six meet 60 fps with no gameplay stalls worth the name (QuakeSpasm,
Yamagi Quake II, X-Moto, Armagetron, Neverball, OpenArena opengl1) and Extreme Tux Racer is at 59.
ClassiCube (48), dsda-doom (56) and Luanti (45-46) are CPU-bound or still stall a little. The heavy ones are
limited by memory and are smooth only after more work (Sauerbraten, SuperTuxKart, OpenArena opengl2),
and OpenTTD is far under. Of the 11 smoke-tested new games, all 8 that start without someone at the
controller run (Teeworlds, SuperTux, Red Eclipse, Frozen Bubble, Chocolate, Crispy and Woof Doom,
Pingus); the 3 menu-only ones (TORCS, Trigger Rally, Chromium B.S.U.) are installed and wait for the
user. Nexuiz Classic on our `darkplaces-gl2` is built, not run. Table below.

**Seen on the PS3 since the afternoon** (boots of .419 and .433): the lost-decrementer fix, the hwmon
temperatures, the fan policy (SYSCON automatic at boot, full speed and back), the OtherOS++ survey set
(gelic, spufs), the read-only GameOS mount of `dev_hdd0`, the interference detector (every run since
batch 15). **Installed, not yet tried at the TV:** game mode 0.458 (the menu-freeze fix and the
escape keys). **Built, not run:** game mode's automatic start and game prefetch A/B (learn runs only),
kernel .457 for the next boot (console frame freed while X runs, local/0043, 8 MB; NLS for
`dev_hdd1`; ps3rsx `shadow_fast`), ioquake3 0027-0029, Mesa 0037 and cube2 0011 (the memory A/Bs of
docs/MEMORY.md).

### Open decisions for the user

1. **Stable publish of the READY set** (docs/RELEASE-CANDIDATES.md): the kernel `.178` with ps3rsx
   0.249, Mesa, libspeq/ps3-spu, ps3-gpu-tools, libsdl3, luajit, ioquake3, yamagi-quake2, dsda-doom,
   fluidsynth, extremetuxracer, neverball, luanti, cube2, openttd. Needs the rebuilds and an explicit
   `DEBS=` list; `build/aptly` is backed up first. Seven more items need one check each (xmoto,
   classicube, supertuxkart, teeworlds, ps3-system/desktop/games, ps3-boot, splash). Nothing is
   published.
2. ~~**Luanti's package default**~~ (settled by measurement, 2026-10-18 evening): base stays. The
   earlier `cliunload` numbers (stalls 6.5 -> 0.8 s) compared it with an interfered base run. Clean
   re-runs on kernel .433 (row 67, x2, interfered=0): base 1.5 and 2.1 s of stalls, longest 167 and
   203 ms, 45-46 fps; `cliunload` **worse**, 3.8 and 5.2 s, longest 2.05 and 2.08 s (blocks unloaded,
   then reloaded with a hitch); `lean` 56-57 fps with 0.4 and 0.9 s, but the webcam shows a **black
   void** at 150 s in both runs (no sky, no terrain): it renders nothing, so not a win. Open: which
   part of `lean` (srvunload, active2, chunk3) empties the world. `view40` (59 fps, closer fog,
   row 63, clean as far as its files show) is the one variant left worth offering as an option.
3. **A test-kernel boot session**: half done. `.419` booted (idle fix confirmed, GameOS `dev_hdd0`
   mounted read-only), then `.433` (idle fix, temperatures, fan SYSCON automatic and full speed, gelic,
   spufs all pass). Still to do on `.433` with the user present: the power paths of the shutdown poll
   fix (poweroff, power button taps during shutdown, reboot). Next boot: `.457` (pin `.433` as the
   default first; steps in build/tmp/nextkernel-STATUS.md): `dev_hdd1` with NLS, the console frame
   release (local/0043) A/B, ps3rsx `shadow_fast` A/B.
4. **Fan floor**: the fan policy is SYSCON automatic at boot and on every exit path, full speed on
   request. A fixed floor (>= 60%, `pwm1`) only with the user's explicit OK during the fan test
   (docs/THERMAL.md "PS3 test plan for the fan").
5. **Flash and SYSCON** (docs/FLASH-SYSCON.md, nothing tried): the recommendation is the 7th SPE by
   `spu_base.enum_shared=1` (no write), then the missing read-only backups (second flash dump, EEPROM,
   error log), then a read-only probe of lv1 peek. Everything that writes stays off the table until
   recovery hardware exists, and that is a hardware mod, so only on request.
6. **7th SPE test**: add a Petitboot entry with `spu_base.enum_shared=1` and run the speq benchmarks in
   the same boot against the 6-SPE baseline (part of the reboot session).
7. **SuperTuxKart default**: ship `lean` (21.4 smooth fps and 2.4 s of stalls against 13 fps and 13.5 s)?
   Open question first: every SuperTuxKart run so far logged `sound=none` (the ALSA device never
   started), so whether it plays sound at all on the PS3 is unknown (profile mode turning audio off,
   or OpenAL failing); a normal start with the webcam microphone settles it.
8. **Still open from before**: the 720p console (-4 MB; the desktop and games would run at 720p) and
   dsda-doom's music default (OPL or off).
9. **Upstream submissions** (docs/upstream/README.md): five drafts (Teeworlds, Yamagi, Mesa LUT, the two
   kernel patches), none sent. The idle patch now has its with-fix numbers (419: 4 ms against 1103 ms),
   so it can drop RFC once rebased.

### Per game status

Steady fps is the median over the 120 s window after a 60 s warm-up, 1920x1080, picture checked on the
webcam, same-boot rows with the 10 ms kicker (`wkkick10`) from `games/bench/results/` (row = fps.csv
row). "Stalls" is gameplay stall time in the window (frames over 100 ms), "longest" the longest frame.
Detail and history per game in docs/GAME-PLAN.md.

| Game | Steady fps | Smoothness | Memory | Picture, sound | Next step |
|---|---|---|---|---|---|
| QuakeSpasm | 91.0 (row 58) | 1% low 48.6, stalls 2.5 s | 45 MB free at the lowest | right, right | none |
| Yamagi Quake II | 68.7 (row 57, kiss4k) | stalls 0.1 s, longest 91 ms | 46 MB free (12 before); RSS 125 -> 56 MB | right, right | none |
| X-Moto | 160.4, 163.1 with the stream buffer (row 64) | 1% low 100, stalls 0.4 s | 53 MB free | right, right (engine quiet) | none |
| Armagetron | 83 live AI rounds (row 60) | one 127 ms frame in 2 min | 58 MB free | right, right | none |
| Neverball | 66.5 (row 54) | 51 frames over 100 ms in 20 s of replay | 58 MB free | right, 118 underruns | a longer replay; batching gives CPU -19% |
| OpenArena (opengl1) | 70.6 (row 59) | stalls 0.0 s, longest 64 ms | 26 MB free | right, right | none |
| Extreme Tux Racer | 59 HUD (row 49, batching) | not re-measured | 53 MB free | right, right | steady-state row with stalls |
| ClassiCube | 48.4, 50.9 with chunks1 (row 64) | stalls 0.0 s, 1% low 31.6 | 57 MB free | right, right | decide chunks1 and the -4% of batching |
| dsda-doom (GL) | 55.9 (row 61) | stalls 0.0 s, 1% low 40.3 | 43 MB free | right, **music chops** (465 underruns a minute on the SPE pool against 1095) | the chopping's cause |
| Luanti | 45-46 base (row 67, clean) | stalls 1.5/2.1 s, longest 167/203 ms | 13-21 MB free (row 63) | right, right | base stays (decision 2); `cliunload` worse, `lean` renders a black void |
| Sauerbraten | 39.6 on the old spectator demo (row 57); 23-38 on the first-person demo (rows 64, 65) | stalls 2.8 s, 1% low 13.9 | 117-120 MB VRAM, 19 MB free | right, 130 underruns | memory: Mesa 0037 + cube2 0011 A/B (built); Mesa 0035 query wait measured no gain |
| SuperTuxKart | lean 21.4 smooth (row 61) | stalls 2.4 s (13.5 default) | 96 MB VRAM, "out of VRAM" lines remain | track draws; **`sound=none` in every run** | sound first (decision 7), then `lean` default |
| OpenArena (opengl2) | 53.4 profiled (row 61) | stalls 16.0 s, longest 6.3 s (54.7 s and 16 s before report.py stopped counting the reload's last frame as play) | 115 MB VRAM, 9.7 MB free | right, right | memory: ioquake3 0027-0029 + Mesa 0037 A/B (built; docs/MEMORY.md) |
| OpenTTD | 21 with the GL driver (software 8-12) | stalls 6.3 s, 3893 underruns | 17 MB free | right, **choppy** | sound, then fps |

Smoke-tested only (no benchmark, `SUITE=0`): Teeworlds 183 fps (our BE fix), SuperTux 63-66, Red Eclipse
86 HUD peak with lean settings and no OOM, Frozen Bubble 45, Chocolate and Crispy Doom 35 (the vanilla
cap), Woof 34, Pingus 39. TORCS, Trigger Rally and Chromium B.S.U. start in a menu: installed on the
PS3, they wait for the user. Nexuiz Classic (`darkplaces-gl2`, about 800 MB with its data) is built
and waits for a smoke run with the user present. Sound was not confirmed in any smoke run.

## Progress by area (2026-10-18)

### Games and the graphics stack
- **Done:** game-side GL batching in X-Moto (44.5 -> 166.5 fps), Extreme Tux Racer (52 -> 59), dsda-doom
  (+3%, 1% low 36.4 -> 40.3), Neverball (CPU -19%), Luanti (stalls 9.1 -> 6.25 s), ClassiCube (-4%
  steady, better lows; chunks1 +5%), OpenArena opengl1 (neutral; the lightmap atlas is off by default
  because any atlas page adds about 10 s of hitches). Mesa 0015-0035 (switches per patch). ps3rsx 0.249
  and `shadow_fast` (built, default off). Mesa `NV30_TRANSLATE_LUT` default: Sauerbraten +53%.
- **Memory patches in the games:** OpenArena opengl2 14.8 -> 26-32 fps and longest stall 24.7 -> 6 s
  (ioquake3 0001-0010); OpenArena preload as above; Yamagi -70 MB; Sauerbraten no longer OOM-killed;
  Red Eclipse loads without OOM with lean sound. Luanti: none of its memory variants is a win on clean
  runs (trim16 rejected, row 57; cliunload worse and lean empty, row 67). docs/MEMORY.md has the per-game
  accounting (Luanti 102 + 47 MB swapped, OpenArena 71 + 20, Sauerbraten 78 + 12) and the ranked cuts.
- **Vertex formats nv30 can't fetch** (2026-10-18 evening, docs/MEMORY.md): u_vbuf converts them per
  draw and nouveau keeps a malloc copy of every VRAM buffer it reads, then of every new buffer once a
  screen-wide hint is set (OpenArena opengl2: ~62 MB of copies of its model VBOs). Built, A/B pending:
  ioquake3 0027 (colours as SNORM shorts), 0029 (packed MD3 frames, 62 -> 37.5 MB of VRAM), Mesa 0037
  (switches on the copies and the hint), cube2 0011 (Sauerbraten's world normals as shorts).
- **Bugs fixed in games:** big-endian bugs in dsda-doom, Yamagi (OpenAL noise), SDL3, LuaJIT, SuperTuxKart
  (.spm meshes), Teeworlds (`FIONBIO` takes an int: the client hung in `recvfrom`), Luanti (Ogg Vorbis byte
  order); OpenTTD's GL driver runs (21 fps); the SuperTuxKart track draws.
- **In progress:** the heavy games' memory (Sauerbraten, SuperTuxKart, OpenArena opengl2); ClassiCube's
  cost (the -4%; `chunks1` +5% in one run, row 64, to repeat) and the GPU-bound finding in
  docs/HOTPATHS.md; the dsda-doom chopping; SuperTuxKart's missing sound.
- **Measured neutral, kept off or as they were:** Mesa 0034 fence pool (row 58), 0035 query wait (row
  65), 0036 staging pool (same fps, CPU -4%, row 64), the LUT in QuakeSpasm (row 58),
  `vm.watermark_boost_factor=0` (row 62), the malloc presets (noise between rounds, row 61).
- **Next:** the remaining batching (IDEAS #6), compression for every game (#4), DXT5 users, the three
  menu-only smoke games and Nexuiz with the user.

### Stalls and measurement
- **Done:** the stall recorder (`ps3-stallrec`, causes per 10 ms sample), `PERF=1` and `MEM=1`, gameplay-only
  stall columns, steady-state windows, the webcam check on every run, same-boot A/B rows
  (`fps.csv` has 65 rows). Finding: the recorder itself removed stalls (OpenArena gameplay stalls 28.9 and
  20.7 s off, 7.1 and 7.3 s on), which led to the lost-decrementer bug (confirmed on the idle PS3).
  Stopgap in game mode: a 10 ms kicker for thread 1 (`ps3-kick`, built, not yet run through game mode).
  Game prefetch (a game's own files read at its start) is built.
- **Done 2026-10-18 evening:** the kernel fix local/0009 booted (.419, .433) and confirmed: probe 4 ms
  with it against 1103 ms off, same boot; OpenArena gameplay stalls 0.1-0.5 s against 5.6-8.2 s (row
  66, SSH-polled, repeat clean). The interference detector (`ps3-intfwatch`, ps3-gpu-tools 0.453) runs
  with every bench run and shows a BENCHMARK RUNNING screen on tty8; a retro-scan threw out 10 runs
  (games/bench/results/interfered.txt). report.py counts a reload's last frame as the reload's.
- **In progress:** the prefetch A/B (docs/STALLS.md "The A/B on the PS3", 24 runs; the learn runs of
  Sauerbraten, OpenArena gl2min and Luanti are done, SuperTuxKart's was interrupted); the clean
  repeat of the idle fix on/off on .433.
- **Next:** drop the stopgap kicker once the fixed kernel is the default; the detector's own checks
  (STALLS.md "Checking it on the PS3": a key press, an SSH login, a mouse bump).

### Memory and game mode
- **Done:** release kernel (+8.7 MB), VRAM swap with GPU copies (swap-ins ~10x faster), automatic game
  mode for fullscreen 3D programs (`ps3-game-detect`) with the wallpaper unloaded, services' heaps
  pushed to swap and the desktop's files prefetched on leaving. System trims stay dropped (CLAUDE.md
  "Memory trims").
- **Built, not booted:** local/0043 frees 8 of the console's 9 MB of XDR while ps3rsx owns the GPU
  (docs/KERNEL-MEMORY.md 6), in kernel .457.
- **Game mode on the PS3 (2026-10-18):** Luanti from the Games menu froze X (jgmenu frozen holding its
  grab); fixed in ps3-desktop 0.456, and 0.458 adds the ways out (Ctrl+Alt+Esc, the PS button held 3 s,
  Alt+Tab/Win+D thaw the desktop). 0.458 is installed on the PS3, not yet tried at the TV.
- **Open:** the 720p console, the VRAM reserve against a game's VRAM (a measurement: which path the
  heavy games take), game mode's numbers on the PS3 (`gamemode-check.sh` never ran there).

### SPUs
- **Done:** libspeq (queue, DXT1/DXT5, RGB sources: 1024x1024 RGB DXT in 1.0 ms on 4 SPEs against about
  175 ms on the PPE), the shared 2-SPE deadline pool, fluidsynth's voice DSP on the SPEs (CPU 44.5 ->
  31.8 s a minute, underruns about halved, the user by ear: "night and day"), skinning for Sauerbraten
  (selectable, no gain on the demo). OpenAL Soft's mixer stays on the PPE (3.7-4.7%).
- **Next:** probe E16 (SPU DMA into VRAM), DXT for every game, the 7th SPE.

### Hardware, safety and the kernel
- **Temperatures and fan:** the System Manager answers the Linux LPAR on our OtherOS++ (Cell 65.8 C, RSX
  60.0 C, through the raw device of kernel .178). The hwmon driver (local/0040) and the fan policy
  (local/0042: SYSCON automatic at boot and on every exit, full speed or a fixed floor on request)
  passed on kernel .433 (2026-10-18 evening: temperatures through hwmon, SYSCON automatic at boot,
  full speed and back). Not tried yet: the floor (needs the user's OK), the power-off and reboot
  paths. CLAUDE.md allows the fan policy from Linux; every other SYSCON write stays forbidden.
- **OtherOS++ patch survey** (docs/OTHEROS-PATCHES.md): 13 safe fixes ported as local/0020-0032 (gelic RX,
  ten spufs fixes); the user dropped `/dev/ps3sysmngr`, the BD vendor commands, the Jupiter Wi-Fi
  driver with its repository write, and switched the dispatcher manager off. Booted in .419 and .433:
  gelic (a 200 MB transfer) and the spufs checks pass.
- **GameOS disk readable read-only** (docs/GAMEOS-HDD.md): `dev_hdd0` mounted read-only on the 419 boot
  (864 GB file system, 22 games listed); `dev_hdd1` (vfat) failed on a missing NLS codepage, fixed in
  kernel .457 and by a retry in `ps3-gameos-mount`. UFS write support is not built.
- **Flash and SYSCON research** (docs/FLASH-SYSCON.md): nothing written; ranked recommendation in
  decision 5.

### Packages and repository
- Suites `ps3` (kept forever), `ps3-dev`, and since 2026-10-17 `ps3-licensed` for licensed game content
  (`sauerbraten-nonfree-data`, `classicube-data`, Quake data; `ps3-games` holds only free packages).
- The stable suite still holds the 2026-10-16 morning builds (kernel .152, Mesa .152). The READY set
  (docs/RELEASE-CANDIDATES.md) is the step that makes the games reachable from stable. Nothing was
  published to stable on 2026-10-18; ps3-dev got the day's builds (ps3-desktop 0.458, ps3-gpu-tools
  0.453 among them).

## Base and toolchain (decided 2026-10-15)

- **Base:** a pinned Debian sid snapshot (`DEBIAN_SNAPSHOT`, now 2026-10-13) is our release.
  Debian has no stable release for ppc64 (big-endian), only debian-ports unstable, so a frozen
  snapshot is the closest thing to an LTS. It moves deliberately, at the latest when Debian 14
  is released, and never piecemeal.
- **One compiler version everywhere: GCC 16.2,** the snapshot's system compiler.
  - **PPE** (kernel, Mesa, ps3rsx, games): Debian's `gcc-16-powerpc64-linux-gnu`
    16.2.0-3cross1, from the amd64 sid snapshot of the same date.
  - **SPU:** our port of the SPU backend to GCC 16.2 (docs/SPU-GCC.md, `spu-toolchain/gcc/`).
  - **binutils:** 2.47, for the SPU with one gas patch (`spu-toolchain/binutils/`, the `hbrr` regression:
    gas took `nop 127` for the `.nop` directive). **newlib** for the SPU: 4.6.0 (seven
    patches, `spu-toolchain/newlib-4.6/`). On the SPEs (2026-10-15): 1629 of 1677 execute tests pass,
    the same tests one by one as with binutils 2.33.1 and newlib 1.20.
- **Kernel:** 6.12 LTS (supported to Dec 2028), from Debian's `linux` source (trixie, 6.12.111-1)
  with our patches, two flavours: the release kernel `6.12.111+ps3.N` (task #14: +8.7 MB `MemTotal`
  measured on the PS3, 222704 kB against 213992 kB; vmlinux 11.9 MB against 21.2 MB) and debug
  kernels `6.12.111+ps3.N-debug<stamp>` (each debug build its own release, side by side; names
  decided 2026-10-16). **Mesa:** Debian's source package of the
  snapshot's release, rebuilt with our patches (`mesa/`, version `26.2.4-1+ps3.N`).
- **Packaging (2026-10-16, plan: Debian source packages):** components Debian ships are built
  from Debian's source package plus our patch series (`<pkg>/patches`, `<pkg>/packaging`), version
  `<debver>+ps3.N`; native in a ppc64 chroot under qemu (`make <pkg>-deb`), the kernel cross-built.
  The repository is laid out by output package (`<pkg>/`).
- **apt repository** on tibi-server (`tools/repo-server/`), published with aptly from
  `build/aptly`: suite `ps3` (`http://10.0.0.10:8070/ps3`, stable releases, kept forever) and
  suite `ps3-dev` (`.../ps3-dev`, dev builds `~dev<stamp>` with debug symbols, the newest 5 of
  each; `make deploy` goes through it). Decided 2026-10-16.
- **apt on the PS3 (2026-10-16):** Debian live (debian-ports unstable) *and* ours, so anything we
  don't build installs and upgrades from Debian; `ps3-system`'s pins put our stable suite at 990
  (wins over Debian, never downgrades an installed dev build), the dev suite at 1 (only on
  request: `apt install -t ps3-dev`) and block Debian's kernels and Mesa (-1: never a
  candidate). `apt upgrade` is safe: a Debian package that needs a newer Mesa than ours is kept
  back. New kernels of ours arrive through meta packages. Source:
  git.tibich.com/chibicitiberiu/ps3-linux.

- **Distribution name and look (2026-10-16): Walnut Linux.** Stand-in logo after the owner's
  reference (`ps3-splash/art/make-logos.py`; a better one before release), the walnut as the
  kernel's boot logo, a Plymouth splash (`ps3-splash`: the logo and a progress bar with the walnut
  rolling along it, on the desktop's blue), wallpapers from the owner's photos of the walnut tree
  (CC BY-NC 4.0).

## Long-term goal (stated 2026-10-14)

A live environment for the PS3 that boots from USB or over NFS through Petitboot, with the RSX
driver, desktop and games working out of the box, plus an optional installer that installs it
permanently (to the OtherOS++ Linux region of the HDD, within the safety rules).

## Earlier status (2026-10-17, kept for the reasoning; numbers superseded by the summary above)

**Phase: the games, from playable to good.** The game stage's baseline is done (docs/GAME-PLAN.md,
status table): every game of the suite has run on the PS3 at 1920x1080 with the picture checked on
the webcam, after six big-endian bugs in games and libraries and five driver bugs. Light games aim
at ~60 fps; QuakeSpasm, Yamagi Quake II, Neverball, Armagetron and now X-Moto reach it. The heavy
ones (Luanti, Sauerbraten, OpenArena opengl2, SuperTuxKart) are limited by memory. Three levers, in
this order (GAME-PLAN.md "Next phase"):

1. **Memory**, in the games and through game mode (CLAUDE.md "Memory trims": nothing desktop users
   expect is disabled for good; it gets out of the way while a game runs). Our ioquake3 0001-0010
   took OpenArena opengl2 from 14.8 to 26-32 fps and its longest stall from 24.7 to 6 s; game mode
   now unloads the PCManFM desktop and wallpaper, pushes the services' heaps to swap, drops the page
   cache and compacts (built, not measured on the PS3 yet); Sauerbraten, Luanti and SuperTuxKart have
   memory patches or variants waiting for a run (2026-10-18: run since, see the summary: Sauerbraten no
   longer OOMs, Luanti cliunload, SuperTuxKart lean).
2. **Stored data for compute** where the SPEs and the RSX have headroom: compressed textures
   (DXT1 on the SPEs, done for OpenArena; every game next), mip chains on the GPU, compressed
   sound, smaller vertex formats.
3. **CPU**: Mesa nv30 rounds 0015-0031 (ps3.246 to ps3.299: OpenArena +23%, Yamagi +24%, X-Moto +21%
   on ps3.262; Armagetron's loss on ps3.262 fixed by 0027's adaptive submission, 50 -> 61.5 fps),
   ps3rsx 0.249 (cheaper validation and shadow). **Game-side batching beat the driver work by far**:
   our X-Moto went from 44.5 to 166.5 fps (18,500 -> 165 GL calls a frame), Extreme Tux Racer from
   52 to 59. dsda-doom's GL renderer and ClassiCube are next. SPU offload (docs/SPU-OFFLOAD-PLAN.md):
   fluidsynth's voice DSP moved to the SPEs (dsda-doom's CPU 49 -> 32 s a minute, but the music still
   chops; a second version is built); OpenAL Soft's mixer is 3.7-4.7% of a game, under the 6% bar,
   so no SPU mixer; skinning on the SPEs is built for Sauerbraten.

Where things are: GAME-PLAN.md (per game, the measured rounds, what is built and waiting),
SPU-OFFLOAD-PLAN.md (SPU work), games/bench/results/ (every measured configuration, one row each),
DESKTOP-PLAN.md (game mode), IDEAS.md (what to do next, ranked).

Lessons (2026-10-17):
- Every run counts only with the picture checked (tools/bench-cam.sh photographs each run at 30 and
  60 s). The first watcher opened an SSH connection every 5 s, which loaded the PPE (load average 5
  at idle) and slowed the runs it watched: those runs (13:05-14:50 UTC) were discarded; it now keeps
  one connection per batch.
- `perf` on the PS3 needs `--proc-map-timeout=20000`: with the default it starts sampling before it
  has read a big process's maps and files most samples as `[unknown]` (77% of OpenArena's). Earlier
  profiles with a large unknown share were hit by it.
- Compare on the same boot: ClassiCube gave 50.1 fps on one boot and 46.5-46.9 on another with
  Mesa ps3.282 and ps3.299 alike. A/B rows reinstall the old build on the same boot.
- OpenArena keeps only 32 `+` commands on its command line (Debian's wrapper passes ~10): recipe
  cvars go into autoexec.cfg.

## Earlier status (2026-10-16)

- **Done 2026-10-16:** the splash (docs/PACKAGING.md, "The boot splash"): our Plymouth
  (Debian's 26.134.222-1 plus one patch: the script plugin crashed on
  `ply_console_viewer_hide(NULL)` without text rendering or with `plymouth.prefer-fbcon`), the
  `walnut` script theme in `ps3-splash` with Plymouth's DRM renderer diverted (it fails on ps3rsx
  at boot), `DeviceTimeout=1`, no XKB keymap compile (3 s), `quit --retain-splash`, the getty on
  tty1 that leaves the splash alone and the splash's last frame on X's root window until the
  desktop draws. ps3rsx (f30b505): DPMS off now blanks with a ps3av video mute instead of
  flipping to ps3fb's console, and the hardware cursor comes back when the CRTC is enabled again.
  The legacy pipeline is retired (`make kernel`, `make debs`, `build-kernel.sh`, `build-debs.sh`,
  `stage-package.sh`, the `ps3-kernel-*` packages, the transitional `ps3-mesa`, `config.mk`'s
  `KERNEL_*`); everything builds as Debian source packages into `build/debs`.
- **Next (agreed 2026-10-16):** a test boot of the release kernel `6.12.111+ps3.178` with
  ps3rsx 0.181 (the owner reboots): splash timing from the USB disk and the DPMS fix, neither
  tested on hardware yet. For the timing its arguments file gets
  `plymouth.debug=file:/var/log/plymouth-debug.log plymouth.debug=stream:/dev/null`
  (PACKAGING.md, "The boot splash"). Then clean the old kernels off the PS3 (the
  owner removes them; Petitboot has no default or timer: the owner picks from the menu), build a
  fresh image and compare a fresh install with the live system, and reimage the USB disk with it.
  No publishing to the stable suite until the owner says so.
- **Lessons (2026-10-16):** a deploy must not let apt solve against Debian's full lists on the PS3
  (twice it stalled the machine for good: swap disabled by a packaging bug the first time, apt's
  memory with ~60000 Debian packages the second); kernel and Mesa installs stop the desktop
  first. A Debian package can't ship its own config into another package's conffile without
  ordering care (Plymouth). A deploy picks up whatever is in `build/debs`: kernels only with
  `KERNELS=1` or by name since an unfinished kernel build went in without its ps3rsx modules.
  From the USB disk every library a boot-time program loads costs seconds (Pango for Plymouth's
  labels: 10 to 11 s; an XKB keymap: 3 s): measure with `plymouth.debug=file:` (plus
  `plymouth.debug=stream:/dev/null`, or the debug text lands on tty1).

- **Packages (2026-10-16, docs/PACKAGING.md):** everything we ship is a Debian package built from
  a source package: Mesa is Debian's `mesa` rebuilt with our patches (replaces Debian's; LLVM,
  z3 and sensors are gone from the PS3, 196 MB of disk), plus `ps3-gpu-tools`, `ps3-spu`,
  `ps3rsx-modules-*`, `ps3-boot`, `ps3-system`, `ps3-desktop`, `ps3-splash` (with our
  `plymouth`) and `classicube`. The kernels are Debian's `linux` rebuilt: release
  `6.12.111+ps3.N` (`linux-image-ps3`), debug `6.12.111+ps3.N-debug<stamp>`
  (`linux-image-ps3-debug`). They come from
  our apt repository on tibi-server; Debian's live ports archive covers the rest, with pins that
  keep Debian's kernels and Mesa out. The image installs ours with apt; `customize.sh` keeps only
  per-installation settings.

- **Boot:** the image lives on a USB disk; Petitboot boots it on its own from `/etc/kboot.conf`
  (written by `ps3-update-kboot`, one entry per installed kernel, newest the default, the older
  ones as fallbacks; the image boots the release kernel by default). NFS netboot still works
  for development (`make netboot-deb`, a kernel from `build/debs/linux`).
- **GPU:** `ps3rsx` (registers as "nouveau") runs Mesa nv30 from our fork: Xorg with glamor,
  GL games, KMS with real vsync. The GART is dynamic (CMA, 1 MB chunks on demand). Details:
  DRIVER-PLAN.md, RSX.md.
- **Desktop:** Openbox + tint2 + PCManFM, NetworkManager (never on eth0), pnmixer, dunst; Shut
  down / Reboot in the menu. The user D-Bus runs under systemd (`dbus-user-session`).
- **Game mode:** keeps the game and X resident (cgroup `memory.low`), freezes the desktop
  helpers, gvfs, udisks2 and polkit, and pushes their memory out; all of it thaws when the game
  exits. It starts by itself for any fullscreen 3D program (`ps3-game-detect`), and the menu's
  games start through `ps3-game` (before they load); `PS3_GAME_MODE=off` turns it off
  (DESKTOP-PLAN.md).
- **Swap:** `ps3vswap0`, a ps3rsx block device that keeps swapped pages in spare VRAM (up to
  96 MB), with the USB swap partition behind it. Recently swapped pages stay in VRAM, cold ones
  are demoted to the disk, and games get VRAM back on demand.
- **Games:** ClassiCube locked at 60 fps (1080p, vsync); OpenArena timedemo median 50–60 fps,
  with stalls of up to a second or more still in live play. (2026-10-17: these were measured with
  nothing on screen and are void; the valid numbers are in GAME-PLAN.md.)

## Memory: the central problem

Measured mid-game (OpenArena, 2026-10-15). Reports: KERNEL-MEMORY.md (kernel side, measured),
HARDWARE-LEVERAGE.md (techniques), RSX-SPU-FEASIBILITY.md (VRAM and SPU access paths),
SPU-OFFLOAD-SURVEY.md.

| Of 256 MB XDR | MB | Note |
|---|---:|---|
| Hypervisor (never reaches Linux) | 16 | |
| Kernel image | 21.1 | 6.2 of it alignment padding (`STRICT_KERNEL_RWX`) |
| ps3fb console frame | 9.2 | 1080p; 720p needs 5 |
| `struct page`, hashes, stacks | ~4.7 | |
| Slab, page tables, vmalloc | ~29 | zram table 1.5, seccomp filters 1.0, `cfg80211` 0.5 |
| Kept free (reserve, watermarks) | ~20 | `min_free_kbytes` 8 MB: two hangs without it |
| GART (CMA, on demand) | 6–14 | the rest of the 64 MB CMA area serves normal pages |
| OpenArena | 74 (+10 swapped) | engine data: map, models, decoded sounds, hunk |
| X | 6 (+27 swapped) | 8 MB wallpaper segment, 16 MB heap (glamor/Mesa) |
| Services, desktop helpers, file cache | the rest | helpers frozen in game mode |

Why it hurts so much: whatever doesn't fit comes back from a USB 2 disk whose bridge (JMS567)
speaks only Bulk-Only Transport, one command at a time. A game's page fault queues behind swap
writes; the kernel stacks of blocked threads showed the game faulting its own code back in, and
with the game pinned, X swapping in (X is on every frame's path).

Facts that decide the design (RSX.md, RSX-SPU-FEASIBILITY.md):
- **The PPE must not read VRAM:** 10 MB/s measured. Writes reach 124 MB/s with 8-byte stores;
  uncached stores leave the chip as single transactions of at most 16 bytes, so ~250 MB/s is the
  ceiling. Only 128-byte transfers (SPU DMA, the RSX's own copies) reach GB/s.
- **glibc's memcpy into a VRAM mapping is a trap:** `__memcpy_cell` uses `dcbz`, which faults on
  cache-inhibited memory and is emulated by the kernel per 128-byte line (19 MB/s).
- **The RSX copies fast:** M2MF VRAM→XDR ~600 MB/s, XDR→VRAM ~2 GB/s.
- **VRAM is mostly idle:** X ~19 MB, a game ~70 MB, of ~250 MB.
- **Textures already cost VRAM, not RAM:** Mesa keeps no CPU copy, ioquake3 frees decoded
  images after upload.

Goal (stated 2026-10-15): **a game can use 2/3 of the RAM (~160 MB) without stalls, without
removing features.** The levers, in the order of the queue below: fast swap in VRAM (GPU
copies), a leaner kernel (−8.7 MB measured, −4 MB more at 720p), userspace trims (zram,
wallpaper, unused modules), then per-game settings and, with the SPUs, compute instead of
memory (compressed textures, streamed audio).

## Work queue

Task numbers refer to the session task list. "Reboot" marks steps that need one.

### Now

1. ~~VRAM swap, GPU copies~~ (task #13): done (`ps3rsx/src/kcopy.c`). Swap-ins and chunk
   write-backs are copied by the GPU into a pinned 1 MiB GART bounce when it can be had without
   waiting; any failure falls back to PPE reads of the same slot. Measured: a working set that
   fits the VRAM tier swaps in ~10× faster (passes 0.2–1.2 s against 4.4–17.7 s); the GL suite
   passes 3× in a row while ~92k kernel copies interleave with its clients; 0 fallbacks so far.
   **OpenArena didn't improve** (timedemo median 47–54, longest frames 1.4–7.6 s): its threads
   no longer block on swap-ins at all; what's left is file I/O on the USB disk: code and data
   page faults (often waiting for the disk's single command slot), file reads, and **file
   opens** (ext4 inode lookups: the lazy asset loads and ioquake3's search through directories
   before its pk3s). Levers: more RAM for the page cache (items 3–5) and fewer file accesses in
   the game (item 8).
2. ~~SPU toolchain versions~~ (tasks #6, #7): done. binutils 2.47 (one gas patch) and newlib
   4.6 (seven patches); the 1677 execute tests on the SPEs give the same result per test as
   the old toolchain (1629 pass).

### Next: memory

3. ~~**Release kernel config**~~ (reboot): done 2026-10-16, the `ps3-release` flavour, `MemTotal`
   +8.7 MB measured (KERNEL-MEMORY.md section 4). From KERNEL-MEMORY.md, measured: `STRICT_KERNEL_RWX=n`
   (−6.0 MB of padding), `KALLSYMS_ALL=n` (−1.0 MB, −0.6 MB in modules), drop IPv6, io_uring,
   quota, kexec, hugetlbfs, NFSv4 as a module (−1.0 MB), smaller log buffer, no dynamic debug,
   IKCONFIG or debugfs (−1.1 MB). Total −8.7 MB. Keep `SLUB_DEBUG` (99 KB, gives slabinfo) and
   built-ins (a module costs 1.5–3× its size). Check ps3rsx builds without debugfs. As a new
   kernel package next to the debug one, so Petitboot offers both.
4. **720p console** (`ps3fb=5M`, mode 131; −4 MB). The user decides: X shares the mode, so the
   desktop and games run at 720p too (also 2.25× fewer pixels per frame). Still open (2026-10-18).
   Meanwhile local/0043 frees 8 of the console's 9 MB while ps3rsx owns the GPU (built, not booted).
5. **Userspace trims (dropped 2026-10-17, the user: memory comes from game mode, see CLAUDE.md "Memory trims"):** zram off now that VRAM swap works (1.5 MB table plus its pages);
   wallpaper out of the 8 MB shared segment (in VRAM, or a solid colour); unload `cfg80211`
   when Wi-Fi is unused; `systemd-networkd` duplicates NetworkManager here; journal size limit;
   the seccomp filters of services that don't need them (1 MB). Re-measure the budget after.
   (2026-10-17, the user's rule in CLAUDE.md "Memory trims": nothing a desktop user expects is
   disabled for good; the memory comes from game mode instead. The wallpaper now leaves in game
   mode, and the services' memory is pushed to swap there. The system trims of this item wait.)
6. **Validator safety:** the vertex-texture methods 0x900–0x97c are allowed with unchecked data;
   a bad setup could wedge the GPU. Check them like the fragment texture units.

### Next: games and CPU

7. **Free per-game experiments:** OpenArena done (2026-10-16, SPU-OFFLOAD-SURVEY.md "Measured:
   step 0"): compressed textures save 11-14 MB of VRAM but Mesa's PPE encoder adds 1.1-1.5 s of
   load and 150-300 ms hitches during play (not shipped; libspeq's encoder next); sound costs
   ~10% of the frame rate and no OpenAL or game setting changes that. Left: `s_khz 11`,
   ClassiCube view distance. (2026-10-17: step 0's game numbers are void, nothing reached the
   screen; OpenAL Soft measured since at 3.7-4.7% of a game's samples, SPU-OFFLOAD-PLAN.md.)
8. **OpenArena file I/O:** build ioquake3 ourselves (`-mcpu=cell`, pinned, like ClassiCube);
   load player models at map start (`cg_deferPlayers` made no difference: the loads happen when
   bots appear); stop the per-file search through the home and base directories before the
   pk3s (~1300 failed opens per 10 s of play, now the main source of disk stalls with code
   faults); listen on the TV; DS3 bindings in play. (2026-10-17: ioquake3 is our package now;
   0011-0018 cut the engine's and renderer_opengl1's CPU time: no shader-script rescan, no crc32 of
   pk3 entries, libdeflate, AltiVec, sounds loaded when played. Built, not run on the PS3 yet.)
   (2026-10-18 result: ioquake3 0025-0026 took OpenArena's gameplay stalls from 9.9 and 9.0 s to 0 and
   its longest frame from 1057 and 807 ms to 64 and 47 ms, steady 64.6 -> 70.4 fps, same boot, row 59.)
   (2026-10-18: the bots' models load when OpenArena's qagame says loaddefered, which is why
   cg_deferPlayers did nothing; ioquake3 0025-0026 load them with the next load of the same map and
   keep textures across same-map reloads: docs/STALLS.md "OpenArena".)
9. ~~**Driver CPU cost**~~: done 2026-10-17 for now: ps3rsx 0.249 (validator fast path, shadow
   per burst, dirty bitmaps) and Mesa patches 0015-0031 (GAME-PLAN.md "Performance work done").
   What is left of the CPU is mostly per-call work in the games, which batching in the games cuts.

### Later: SPUs (compute instead of memory)

**Parked 2026-10-16** for the game suite and its driver work (GAME-SUITE.md), **resumed
2026-10-17** ordered by what the CPU-bound games spend. Done: the measurement harness, libspeq
(runtime, queue, probes) and the SPU DXT1 encoder behind Mesa (items 10-12's DXT part); fluidsynth's
voice DSP on the SPEs (measured, second version built); skinning (built). OpenAL Soft's mixer stays
on the PPE (under the 6% bar). The status: SPU-OFFLOAD-PLAN.md. Item 10's probes are still open.

10. **Probes first** (RSX-SPU-FEASIBILITY.md "Recommended experiments"):
    - E8b: what lv1 keeps of `ioremap_wc` (read the HPTE back), 16-byte stores, pre-touched;
    - E16: SPU DMA puts into a mapped VRAM buffer (expected to work: `bo_mmap` fills the page
      tables at mmap time and spufs resolves faults with `hash_page`); rate and correctness;
    - spufs context-switch and job-dispatch latency (quoted 20–200 µs, never measured here).
11. **libspeq** (task #4): resident SPU workers, a job queue, completion words, stdio via stop
    0x2100 in `spurun`. The base for everything below.
12. **SPU DXT encoder** behind Mesa's S3TC texstore (`mesa/patches/0011-s3tc-libspeq.patch`,
    libspeq loaded at run time): VRAM for textures ÷4 (11-14 MB in OpenArena, room for the VRAM
    swap) without slow loads; libspeq's PPE encoder first. Then **audio**: OpenAL mixing and Vorbis decoded
    per buffer on an SPU (~10% of OpenArena's frame rate, measured; no decoded PCM held in RAM).
13. If E16 shows GB/s: **SPU decompression and generation straight into VRAM** (assets stay
    compressed in RAM and on disk), and VRAM swap writes through an SPU. ClassiCube chunk
    meshing on SPUs as a showcase.
- Rejected, with reasons in SPU-OFFLOAD-SURVEY.md: kernel-side SPU use (validator, zram, vswap:
  the kernel can't drive SPEs), Mesa per-draw paths, compressed swap via a userspace block
  device (reclaim deadlocks).

### Later: GPU features

Only when content needs them (OpenArena and ClassiCube don't):
- **Vertex texture fetch:** 4 units (0x900+0x20·n), FP32 2D nearest. Mesa's
  `nv40_verttex_validate` is a stub that also reads the fragment samplers (copy-paste bug), the
  vertex compiler emits no TXL, and the sampler count is 0. Needed for meshes from textures.
- **Derivatives:** desktop GLSL `dFdx`/`dFdy` already reach DDX/DDY; only GLES2's
  `OES_standard_derivatives` is off. Enable after an egl-deriv check against softpipe.
- **Instancing:** method 0x1fc0 plus a divisor in VTXFMT bits 16–31; it divides the vertex
  index, there is no instance counter, so GL instancing would loop per instance.
- **Tiling, Z-cull, native 720p:** raise GPU throughput, but frames are PPE-bound today.

### Platform

- ~~**Packaging** (task #11)~~ and ~~**deb-src packaging** (task #9)~~: done 2026-10-16
  (docs/PACKAGING.md): Mesa and the kernel from Debian's sources, our code as native packages,
  the patch workflow (`<pkg>-work`/`-patches`/`-rebase`), dev and stable suites, image
  snapshots; the image and packages are reproducible (checked). Tested on the PS3 (NFS root
  netboot of both kernels, then installed on the USB root). Open: boot a new kernel from
  Petitboot's own menu (labels contain `+`) and choose the default (`PS3_DEFAULT_FLAVOUR` in
  /etc/default/ps3-boot); remove the legacy kernels from the PS3 (the owner does); signing the
  repository. The legacy build pipeline and the Mesa cross toolchain are gone (2026-10-16).
- **ClassiCube** (task #10): default.zip at build time, no first-run download.
- **Reproducibility check:** two image builds, same `make checksums`. Image size: Debian's
  Mesa and LLVM. `make disk-image` (raw `.img.xz`, `systemd-repart` grows it on first boot).
- **DualShock 3:** pairing works (one USB plug); check SDL mappings in play, rumble, LEDs.
- **Fan:** read the thermal sensors under Linux, read-only first: the System Manager answers the Linux LPAR
  (Cell 65.8 C, RSX 60.0 C, kernel .178); the hwmon driver is built, not booted yet
  (docs/THERMAL.md: hwmon ps3_sm in kernel patch local/0040, the bench stops at BENCH_TEMP_MAX).
  Fan policy (user decision 2026-10-18): local/0042, SYSCON automatic at boot and on every exit
  path, full speed or a fixed floor >= 60% through hwmon pwm1; built, not booted (THERMAL.md test
  plan: read the policy, full speed and back, a floor only with the user's OK).
- **Networking:** test Wi-Fi from nm-applet; NetworkManager could own eth0 now that the root
  isn't on NFS.
- **Distribution:** sign and host the apt repository; the SPU and PPE toolchains could ship
  from it. `make iso` and the installer (OtherOS++ region; ps3da/db/dc never written).
- **Game suite: the current work** (docs/GAME-PLAN.md; baseline done 2026-10-17, every game has run;
  now the performance and memory phase above). One stage per game, grouped from "runs as
  packaged" to "needs the most driver work"; research in docs/GAME-SUITE.md: stage 0 is the
  runner (`games/bench/`, `make bench-push`) and the `ps3-games` metapackage; then dsda-doom,
  X-Moto, Neverball, Extreme Tux Racer, Armagetron; QuakeSpasm, Yamagi Quake II; OpenTTD,
  Sauerbraten, Luanti; SuperTuxKart (legacy GL), OpenArena opengl2. Driver bugs are ranked by how
  many games they block. GL 3-only games stay out of reach. The image rebuild (fresh debs,
  docs/IMAGE-DIFF.md) waits until this plan is through (user, 2026-10-17).

## Done (summary)

- **Image:** reproducible two-layer build (`make rootfs`), pinned inputs, fixed
  `SOURCE_DATE_EPOCH`; packages in `build/debs`; `make usb-root-petitboot` writes the image onto
  the USB disk from Petitboot.
- **Kernels side by side:** `linux-image-<release>` packages (release and debug kernels) with
  their `ps3rsx-modules-<release>`; `ps3-boot` writes the boot menu.
- **GPU:** ps3rsx with validator, software context switch, KMS, vsync IRQ; GPU vertex fetch
  (ClassiCube 6.5 → 60 fps); Mesa fixes (fog, banded uploads, staging limits); dynamic GART via
  CMA (`rsxdrm-smoke gart`: 56 MB under memory pressure, data correct).
- **ClassiCube stutters and flashes:** fence polling fast only while someone waits (4000 → 1540
  timer interrupts/s); flip events wait for the flip to latch (no more half-redrawn frames).
- **Game mode** and **VRAM swap** (above).
- **SPU:** GCC 16.2 port, 1629/1677 execute tests on the SPEs; `spurun`; spufs at boot.
- **DualShock 3** pairing over one USB plug.

## Test loop

- **Getting to Linux needs the user:** fan on SYSCON in webMAN, the OtherOS flag in the XMB,
  Petitboot boots the default kernel (or the user picks another). Every reboot lands in GameOS.
  At the end of each session the flag goes back to GameOS.
- **Deploying:** `make <pkg>-deb` then `make deploy PKGS="…"`; kernels with `make deploy
  KERNELS=1 PKGS="linux-image-ps3 ps3rsx-modules-ps3"` (they install next to the running one).
  `DRY_RUN=1` shows what would change. ps3rsx can only be reloaded with X stopped (`systemctl stop getty@tty1`, then
  start it again) and, with VRAM swap on, after `swapoff /dev/ps3vswap0`.
- **Kernel changes need a reboot;** batch them. kexec from our Linux crashed the console.
- **A graph exception wedges the GPU until a console reboot.** Risky tests go last.
- **Testing block or memory code:** raise the console log level (`echo 7 >
  /proc/sys/kernel/printk`) so a hang prints full stacks over netconsole; put every SSH command
  under `timeout`; verify data with a pattern (write, drop caches, read back) before using a
  device as swap, then under a memory hog, then in a game.
- **Observing:** netconsole (`make listen`); the BRIO webcam (`tools/webcam-grab.sh`, only when
  the user isn't using it); fps via `GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false
  GALLIUM_HUD_DUMP_DIR=/tmp/hud`; blocked threads via `/proc/<pid>/task/*/stack` sampling.
- **Gotchas:**
  - `perf record -a -g` once hung the PS3; short supervised runs (`-- sleep 5`) work.
  - Keep netconsole quiet in normal use (loglevel 4); never `trace=1` through a GL program.
  - Programs that wait for a key need stdin held open over SSH (`sleep N |`).
  - `pkill -f` inside SSH commands kills the SSH shell itself: use `pgrep -x`.
  - The PS3's clock is UTC; the PC's is EEST.
  - Restarting the session uncleanly leaves stale X sockets: X comes up on `:1`/`:2` until reboot.

## Build setup

Everything builds in pinned Docker images: `docker/Dockerfile` (kernel and rootfs, mmdebstrap
under qemu-user), `Dockerfile.mesa` (C++ cross compiler and meson against a ppc64 sysroot),
`Dockerfile.games`, `Dockerfile.spu`, `Dockerfile.repo`. Outputs go to `build/`, which is ephemeral
except `build/aptly`: only versions published to the repository server are kept forever (CLAUDE.md).
Mesa and the kernel are Debian source packages plus our patch series (`mesa/patches`,
`linux/patches`; docs/PACKAGING.md "Changing patches"). SPU
toolchain experiments build side by side (`SPU_OUT=build/tmp/spu-<name>`). The tree is organized by
output package (README).

## The console

- **Model:** CECH-20xx (DYN-001, first Slim), NOR flash, 4.92 CEX Cobra 8.5D.
- **webMAN:** MOD 1.47.48 at http://10.0.0.76/.
- **Screen:** a Logitech BRIO webcam points at the monitor. HDMI capture is blocked by HDCP in
  the XMB.
- **USB disk:** AXAGON adapter (JMicron JMS567, 152d:0567): Bulk-Only Transport only at USB 2
  speed, queue depth 1.

## Open questions

- Does Remote Start (Remote Play settings) respond to Wake-on-LAN? That would allow remote
  power-on.
- ~~Can the PS3's thermal sensors be read under Linux without syscon writes?~~ Yes: the System Manager
  answers (2026-10-18); the hwmon driver waits for the test-kernel boot.
- Does `spu_base.enum_shared=1` give 7 SPEs on our kernel, and does anything we use break (FLASH-SYSCON.md)?
- ~~Does the fixed idle path (local/0009) end the lost decrementers?~~ Yes (2026-10-18, kernel .419: worst
  idle wakeup 4 ms against 1103 ms with it off). Whether idle threads run hotter with it was not
  measured as such; the .433 boot's temperature check passed.
- Does SuperTuxKart play sound on the PS3? (Every run logged `sound=none`.)
- How fast is the internal disk (`ps3db`, GameOS's) against the USB disk? A read-only O_DIRECT test is
  queued; if the internal disk is much faster, a root or swap on the OtherOS++ HDD region becomes
  worth weighing (its size and GameOS's data decide it: resizing the region wipes GameOS's disk).
- Can a Petitboot-only reboot clear a wedged GPU, instead of a full console reboot?
- Can the CPU still read VRAM after a graph exception? (VRAM swap's fallback assumes so.)
- The kernel tried to write the flash OS area ("os_area_queue_work_handler: Could not update
  FLASH ROM", 2026-10-15, likely the RTC offset when the clock was set). It failed, but flash
  writes are off-limits: find what triggers it and disable it.
