# Release candidates: what goes from ps3-dev to the stable `ps3` suite

This is the release plan for 2026-10-18, "test first, then publish": every package is rebuilt as an official version, a fresh image is built from those debs, the image is tried on the PS3, and only then does the stable publish run, with the user's OK. As of 2026-10-19 the packages and the image are built and the dev suite has them; the image session on the PS3 has not run (docs/TESTS.md lists it under "Next", needing the user), and nothing is in stable. The release table, the test plan and the publish command below are what to use. The first plan (a draft at HEAD `0149148`, with the kernel `.178` as the stable kernel) is summarised at the end under History.

The rules (memory "Publish only when proven"; CLAUDE.md "Never delete what we publish"): a package goes to stable only after it is proven on the PS3, with an explicit `DEBS=` list, `build/aptly` backed up first, and never a `~dev` version. Stable versions are kept forever. Promotion (see [PACKAGING.md](PACKAGING.md#the-repository)) is: commit, rebuild the official version (`0.N`, `...+ps3.N`, no `~dev`), `make publish`.

"fps.csv row N" is the Nth data row of the matrices in `games/bench/results/` (same rows in every CSV; the `observations` column has the text). Proof rows have the picture checked on the webcam unless said otherwise. Terms: [glossary](README.md#glossary).

## The decision (the user, 2026-10-18 evening)

A stable release of every package and a fresh image, done as "test first, then publish":

1. every package rebuilt from the current tree as an official version (no `~dev`);
2. the fresh image built from those debs;
3. the image booted, smoke-tested and benchmarked on the PS3 in a later session;
4. only after that, the stable publish.

The default kernel is an official build of the current kernel source, the release flavour `6.12.111+ps3.472` with ps3rsx-modules built for it. The image session still has to check it as shipped in the image. (Since the image was built, `.472` has booted on the PS3 from the existing USB root: the ssd1 batch of 2026-10-19 02:37 ran on it, and tonight's SSD-root batches use it. That was not the release image, so the checks below stay.)

## State (2026-10-18 23:40, tree at `35f2600`)

Every package in the table is built as an official version in `build/debs`, and the rebuilt ones are published to `ps3-dev` (backup `build/old/aptly-20261018-233019.tgz`). Nothing is in stable yet and nothing went to `ps3-licensed`.

Rebuilt that night: kernel .472 (both flavours), ps3rsx-modules 0.443 for both, Mesa .476, plymouth .247, ps3-splash 0.247, ps3-boot 0.484, ps3-spu 0.395, classicube .374, cube2 .466, xmoto .441, darkplaces-gl2 .460, ps3-system 0.489 (with the SSD work's `PS3_VSWAP_DM` commit) and ps3-gpu-tools 0.476. The rest were official builds of the current tree already.

A side effect for the PS3 queue: `make deploy` now takes Mesa .476 from `ps3-dev` over .466~dev (same patches, 0037-0039 included, committed), and ps3-system 0.489 over 0.419.

## The image

`build/rootfs/debian-ppc64.tar`, 3,082,752,000 bytes (2.9 GiB), sha256 `65dae499d6bcab5effcf5233320b2f8f3124a8b7bb9fc8cbe0d403db67c8aa71` (`make checksums`: `build/SHA256SUMS`).

- A copy of this exact file is kept at `/mnt/data_linux/Temp/ps3/release-20261018/debian-ppc64.tar`, because `make nfs-root` and `make usb-root` rebuild the image first if any deb in `build/debs` is newer than the tar.
- The base layer `build/rootfs/base.tar` is from 2026-10-16 (no change to `image/packages.txt` since). 812 packages.
- Our packages in it are exactly the versions in the table below: both kernels (release and `-debug202610181726`) with their ps3rsx modules, `classicube-data` .366 and the other licensed data from `ps3-games-licensed`.
- `/etc/kboot.conf` has `default=6.12.111+ps3.472` (release; the debug entry is listed above it). systemd-networkd and ssh are enabled (the two fixes from [IMAGE-DIFF.md](IMAGE-DIFF.md)). LLVM and z3 are autoremoved.
- It is 0.9 GB larger than the 2026-10-16 image, almost all game data: cube2-data (260 MB) and sauerbraten-nonfree-data (146 MB) through `ps3-games-licensed`, the Quake II demo, the OpenArena sets.

## The release table

"Proven" means it ran on the PS3 with the picture checked on the webcam. `N` is `+ps3.N` for Debian-source packages and `0.N` for our native ones.

| Package (binaries) | Candidate (official) | What the PS3 proved, with which version | Check still needed in the image session |
|---|---|---|---|
| Kernel, release flavour: `linux-image-6.12.111+ps3.472`, `linux-headers-6.12.111+ps3.472`, `linux-headers-6.12.111+ps3.472-common`, `linux-kbuild-6.12.111+ps3.472`, metas `linux-image-ps3`, `linux-headers-ps3` | 6.12.111-1+ps3.472 | `.433~dev` booted 2026-10-18 and runs every bench since: idle fix (local/0009; on/off clean in row 68: gameplay stalls 0.4 vs 8-9 s), hwmon temperatures, fan SYSCON automatic and full speed, gelic, spufs, `dev_hdd0` read-only. Not booted: local/0043 (console frame release, off by default), the vfat NLS options (.457), local/0044 (OS area writes off, .472) | Boot; the kernel checks below (dmesg, temps, fan auto, `ps3_os_area` disabled line, poweroff, button taps, reboot of local/0041, `ps3-gameos-mount`) |
| `ps3rsx-modules-6.12.111+ps3.472`, meta `ps3rsx-modules-ps3` | 0.443+k6.12.111+ps3.472 | 0.249 on .178, .419, .433: every row from 35 to 68. 0.443 adds only `shadow_fast` (default off) | Loads at boot, `shadow_fast` N, `rsx-suite.sh all`, the smoke runs |
| Mesa set (`libegl-mesa0`, `libegl1-mesa-dev`, `libgbm-dev`, `libgbm1`, `libgl1-mesa-dev`, `libgl1-mesa-dri`, `libgles2-mesa-dev`, `libglx-mesa0`, `mesa-common-dev`, `mesa-drm-shim`, `mesa-libgallium`, `mesa-va-drivers`, `mesa-vdpau-drivers`) | 26.2.4-1+ps3.476 | 0012-0034 with the LUT default: .407/.408~dev rows 57-63; 0035-0036 .416~dev rows 64-65 (neutral); .442~dev (0037, off by default) in batch 17. 0038 (`NV30_FORCE_DXT`) and 0039 (`NV30_SHADER_CACHE`) are off by default and never ran on the PS3 | Smoke of every recipe (picture), the 5-game bench against row 68 |
| `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` | 0.395 | 0.394: DXT1/DXT5 on the SPEs (rows 6, 7, 59), fluidsynth pool (rows 50, 52). 0.395 adds only a doc | `SPEQ_DEBUG=1` line in one smoke run (dsda-doom music or Teeworlds) |
| `ps3-gpu-tools` | 0.476 | 0.453 runs every bench since batch 15 (`ps3-intfwatch`); 0.469 adds `BENCH_PERF_LOAD` (batch 17); 0.476 adds Mesa 0038's test | The bench runs themselves (`intf` column, temps) |
| `ps3-boot` | 0.484 | 0.176 (flavour names) in ps3-dev; no `kboot.conf` from it read on the PS3. 0.484 adds only `webman/autoexec.bat` (not installed by the package) | `default=` is the release kernel (read from the image tar); check which entry Petitboot preselects |
| `ps3-system` | 0.489 | 0.419 installed (apt sources and pins, `ps3-gameos-mount` for `dev_hdd0`). Not run: 0.457's vfat retry for `dev_hdd1`, 0.489's opt-in `PS3_VSWAP_DM=1` (off by default, ff91438); the networkd enable (bc0f08a) only shows on a fresh install | networkd and ssh enabled in the fresh image, DHCP renewal; `ps3-gameos-mount` both disks; `ps3-game-kick` skips itself on this kernel (`ps3_idle_prep=Y`) |
| `ps3-desktop`, `ps3-apps`, `ps3-games` | 0.474 | 0.474 installed (game prefetch off by default, row 68); 0.458's escape keys and jgmenu fix not yet tried at the TV | The desktop and game mode check below (with the user) |
| `plymouth`, `libplymouth5`, ..., `ps3-splash`, `ps3-no-initramfs` | 26.134.222-1+ps3.247, 0.247 | .182 / 0.182 installed since 2026-10-16; .247 is the same source rebuilt (a tooling commit moved N) | One cold boot on the release kernel: splash from Petitboot to the desktop |
| `libsdl3-0`, ... | 3.4.18+ds-1+ps3.254 | .254: OpenTTD colours and fps (row 35), dsda-doom, Chocolate/Crispy/Woof smoke | Smoke runs (OpenTTD, dsda-doom, the Doom ports) |
| `libluajit-5.1-2`, `libluajit-5.1-common`, `libluajit-5.1-dev`, `luajit` | 2.1.0+openresty20251030-1+ps3.247 | .247 under Luanti in every row from 36 | Luanti smoke and bench |
| `luanti`, `luanti-data`, `luanti-server` | 5.15.2+dfsg-2+ps3.405 | .405, rows 54-68 (base kept as the default, row 67) | Bench (5 games) |
| `ioquake3`, `ioquake3-server` | 1.36+u20260719.58839361+dfsg-1+ps3.462 | .396 proven (row 59: stalls 9-10 s -> 0); .462 adds 0027-0029 (opengl2 vertex formats, mipmaps, packed MD3, the last opt-in), in batch 17 | OpenArena smoke (opengl1 `base`) and `gl2min` in the bench |
| `yamagi-quake2`, `yamagi-quake2-core` | 8.70+dfsg-1+ps3.415 | .408 with `YQ2_KISS_QSIZE=4096` (row 57); .415 makes it the default | Smoke |
| `dsda-doom` | 0.30.0+dfsg-1+ps3.361 | .361 rows 52, 61 | Smoke, music by ear (PLAN D3) |
| `fluidsynth`, `libfluidsynth3`, `libfluidsynth-dev` | 2.6.1+dfsg-1+ps3.309 | .309 rows 50, 52, the user listened | With dsda-doom's smoke |
| `extremetuxracer`, `extremetuxracer-data` | 0.8.4-2+ps3.324 | .324 row 49 | Smoke |
| `neverball`, `neverputt`, `neverball-data`, `neverball-common`, ... | 1.6.0+git20180603-3+ps3.370 | .370 row 54 | Smoke |
| `cube2`, `cube2-server` | 0.0.20201229+dfsg-1+ps3.466 | .354/.355 rows 57-65; .442~dev (0011 off by default) in batch 17 | Bench (5 games) |
| `openttd`, `openttd-data` | 15.1-1+ps3.297 | .297 rows 58, 61 | Smoke |
| `xmoto`, `xmoto-data` | 0.6.3+repack-1+ps3.441 | .323 batching (row 49); .362~dev with the stream buffer (row 68: 162 fps); .441 is that source committed | Smoke |
| `classicube` (engine only) | 1.3.8~git20261014.aa47929-1+ps3.374 | .367~dev rows 61-68 (`base` 45.9 fps in row 68); the split upgrade from the bundled stable .152 checked in the chroot only | Smoke; the bench (5 games); on a PS3 with the bundled .152 the upgrade (only matters for installs, not the fresh image) |
| `supertuxkart`, `supertuxkart-data` | 1.5-2+ps3.361 | .361 rows 61-68 (`lean` 19-22 fps by the HUD, rows 68-74; the game's own 107 is its tick rate); `sound=RUNNING` since the SSD root, not heard by ear | Bench (`lean`); sound by ear (PLAN 2.1); PLAN D4 (`lean` as the default) is still open |
| `teeworlds`, `teeworlds-server`, `teeworlds-data` | 0.7.5-3+ps3.422 | .422 smoke only (row 62, 183 fps) | Smoke, sound by ear |

**Not in the stable list.**

- The debug kernel flavour (`linux-image-6.12.111+ps3.472-debug*`, `linux-image-ps3-debug`, `ps3rsx-modules-ps3-debug`) and all `*-dbgsym` stay in `ps3-dev` (dbgsym of a stable publish goes to `ps3-dbgsym` anyway).
- `openal-soft`: an unpatched rebuild made only for matching debug symbols (`5d43987`); publishing it would shadow Debian's `libopenal1` with an identical copy. Skip.
- `darkplaces-gl2` (Nexuiz): never run on the PS3 (PLAN D12). Not yet.
- Licensed packages (`classicube-data`, `quake-shareware`, `quake2-demo-data`, `sauerbraten-nonfree-data`, `ps3-games-licensed`) are already in `ps3-licensed` at the versions the image carries, so nothing licensed is published. The image carries `classicube-data` .366 from the pool, not the .374 the classicube rebuild made (identical files, and a new copy would sit in `ps3-licensed` forever). Publish only the classicube engine deb.
- The Debian packages the games need (openarena, quakespasm, armagetronad, freedoom, the openttd data sets, mpv, ...) are not in this list. The `deps` component is a separate approval (`make deps`). The index of `ps3/deps` has quakespasm, armagetronad, freedoom, openttd-open*, mousepad, zathura and mpv; `openarena` was not found there and comes from live debian-ports.
- Legacy and untouched in stable: `ps3-mesa` 26.2.4+ps3.99999, `ps3-kernel-6.12.112-ps3-debug-*`, `ps3rsx-6.12.112-*` (the old pipeline). Leave them.

**Packages that must go together.** Kernel .472 with `ps3rsx-modules` 0.443+k.472. The Mesa set with `ps3rsx-modules` (and `libspeq0`/`ps3-spu-programs` for the SPE paths). `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` (the SPU programs must match libspeq0). `ps3-desktop`, `ps3-apps`, `ps3-games` (one source) after `ps3-system` (>= 0.367~, which ps3-desktop depends on). `luanti` with `luajit`. `ps3-games` after all its games. `ps3-games-licensed` (already in `ps3-licensed`) after `ps3-games`: until `ps3-games` is in stable it installs on the PS3 only with `-t ps3-dev`.

## What stable holds now

Everything in the stable suite dates from the morning of 2026-10-16 (builds up to .163): the kernel and ps3rsx-modules .152, Mesa .152, ps3-gpu-tools 0.152, ps3-spu and ps3-spu-programs 0.152 (no `libspeq0`), ps3-boot 0.152, ps3-system and ps3-desktop/ps3-apps 0.160, classicube `1.3.8~git20261014.aa47929-1+ps3.152` (bundled Mojang data), `quake-shareware` and `quake2-demo-data` 89. `ps3-games`, plymouth, `ps3-splash`, `ps3-no-initramfs` and all the patched games are not in stable.

`ps3-licensed` holds `classicube-data` 1.3.8~git20261014.aa47929-1+ps3.366, `quake-shareware` 89, `quake2-demo-data` 89, `sauerbraten-nonfree-data` 0.0.20201229-1+ps3.388 and `ps3-games-licensed` 0.388 (and 0.374).

Versions depend on the last commit that touches a package or `tools/deb-*.sh`/`docker/Dockerfile.deb`. Any commit to those tools before the stable publish shifts every N in the table and the command; rebuild and regenerate the list then.

## PS3 test plan for the image session

With the user at the console. Before setting the OtherOS flag, webMAN fan control must be in SYSCON (automatic) mode or a fixed speed of at least 60%; stop on any "PS3 Thermal Alert"; the boot flag goes back to GameOS at the end. No other bench runs and no deploys during the session's runs (memory "No commands during runs"). Webcam microphone recording stays off until the user says so.

**1. Boot the image.** Over NFS (keeps the USB disk as it is):

    make checksums                        # compare with the sha256 above: the tar must be the tested one
    make nfs-root                         # sudo; old export kept as .bak-<date>; FRESH=1 for a clean state
    make listen                           # second terminal: netconsole
    make netboot-deb FLAVOUR=release ROOT=nfs   # PS3 in Petitboot: boots 6.12.111+ps3.472 with its .args

Or later as the root on the USB disk or the SSD region ([GAMEOS-HDD.md](GAMEOS-HDD.md), test drive): `make usb-root` (over SSH from the running PS3; the old root is moved aside on the disk) or `make usb-root-petitboot` from Petitboot, then `make netboot-deb FLAVOUR=release ROOT=usb` or the Petitboot entry.

If any deb in `build/debs` changed after the image, put the kept copy back first (`cp /mnt/data_linux/Temp/ps3/release-20261018/debian-ppc64.tar build/rootfs/debian-ppc64.tar`, then `touch` it) or `make` builds a new image. Petitboot ignores `default=` ([PACKAGING.md](PACKAGING.md#kernels)): pick the `6.12.111+ps3.472` entry by hand once, then it is the remembered default.

**2. Kernel checks** (release `6.12.111+ps3.472`, ps3rsx `0.443`):

- `uname -r` is `6.12.111+ps3.472`; `cat /etc/kboot.conf` has `default=6.12.111+ps3.472` and both entries (ps3-boot 0.484 with `PS3_DEFAULT_FLAVOUR=release`).
- `dmesg -l err,warn`; `dmesg | grep -i -e rcu -e stall -e "soft lockup" -e oops` is empty; `cat /sys/module/kernel/parameters/ps3_idle_prep` is `Y`; idle `uptime` load under 0.3.
- Temperatures: `cat /sys/class/hwmon/hwmon*/temp*_input` (ps3_sm, two zones, idle about 45-55 C).
- Fan: `dmesg | grep -e 'fan:' -e REQUEST_ERROR` shows `fan: SYSCON automatic` and no refusal.
- OS area: `dmesg | grep -e ps3_os_area -e 'FLASH ROM'` shows "ps3_os_area: writes to the OS area in flash disabled, RTC changes last until reboot", and still no "Could not update FLASH ROM" 12 minutes after `timedatectl show -p NTPSynchronized` says yes; `cat /sys/module/ps3_os_area/parameters/write` is `N`.
- ps3rsx: `lsmod | grep ps3rsx`; `cat /sys/module/ps3rsx/parameters/shadow_fast` is `N`; `rsx-suite.sh all` passes.
- Network and services: `systemctl is-enabled systemd-networkd ssh` (both enabled: the IMAGE-DIFF fix); `networkctl` shows eth0 configured by `20-ps3-wired.network`; a DHCP lease renewal in `journalctl -u systemd-networkd`; `apt update` sees `ps3`, `ps3-dev`, `ps3-licensed`.
- `sudo ps3-gameos-mount`: `dev_hdd0` and `dev_hdd1` both "(read-only)", no "retrying" line (NLS).
- Splash (release kernel only): from Petitboot to the desktop with the Walnut splash, no black gap, no text on tty1 (plymouth .247, ps3-splash 0.247).
- Power paths of local/0041, one each, watching the console and netconsole: `sudo poweroff` (off a few seconds after "System Halted", no "no shutdown command after 5 s"); boot again, `sudo reboot` (back to Petitboot/GameOS); boot again, `sudo poweroff` and tap the power button about once a second until it is off (never hold it). THERMAL.md "PS3 test steps" 5.

**3. Desktop and game mode, with the user** (ps3-desktop 0.474, PLAN 1.10). The desktop comes up (wallpaper, tint2, applets). Start a game from the Games menu and leave it. A fullscreen GL program started from a terminal gets game mode by itself (`journalctl -t ps3-game-mode`: on, then `left game mode`); `PS3_GAME_MODE=off` keeps it out. Ctrl+Alt+Esc and the PS button held 3 s end a game. Time the desktop's return. The wallpaper, applets and frozen services are back (`games/bench/gamemode-check.sh`).

**4. Smoke run of every game recipe, webcam on.** `make bench-push`, then on the PS3 `/var/tmp/bench/run.sh smoke <game>` for each of the 25 recipes in `games/bench/recipes/`, or `run.sh suite base 1` for the 12 suite ones plus the rest one by one. `tools/bench-cam.sh` photographs each run; look at every photo. Pass: the game draws its scene (no black screen, right colours), no kernel error, no RSX wedge, sound line not `none` where the game has sound. The user judges sound by ear (SuperTuxKart, Teeworlds, Luanti, dsda-doom music). The menu-only games (TORCS, Trigger Rally, Chromium B.S.U.) and Nexuiz (needs PLAN D12) need the user.

**5. Short bench of the 5 start-up games** ([PROFILING.md](PROFILING.md), "Start-up profiling": the same variants, x2 each, desktop stopped, swap emptied, 60 s warm-up and a 120 s window). The numbers to compare with are batch 16 (fps.csv row 68, kernel .433, 2026-10-18), steady fps / 1% low in play / gameplay stall s / longest gameplay frame ms:

| Game, variant | Row 68 |
|---|---|
| Sauerbraten `base` | 38.8 / 24.1 / 1.4 / 514 |
| OpenArena `gl2min` | 51.8 / 14.8 / 11.0 / 2038 |
| Luanti `base` | 46 / 17.5 / 2.5 / 358 |
| SuperTuxKart `lean` | 107.3 / 11.4 / 1.8 / 686 |
| ClassiCube `base` | 45.9 / 30.1 / 0.1 / 99 |

These are across boots, so they are a regression check, not an A/B (memory "Same-boot baselines"). Flag anything more than about 10% worse in steady fps or 1% low, or any new multi-second stall, and re-check it in the same boot against its switch. Row 68 ran with Mesa .442~dev and ioquake3 .396-.462; the image has Mesa .476 (0037-0039 off by default) and ioquake3 .462. Newer rows exist: batch 17 (OpenArena `gl2min` 57-60 fps with 1-2 s stalls, Sauerbraten +17%, results commit 3939a48) and the SSD-root batches of 2026-10-19 (docs/TESTS.md).

## The stable publish (not run)

Run only after the image session passes and with the user's OK. Drop any row that failed its check from the list first. The sequence checks the tree is clean, checks no `~dev` deb is around, takes the publish lock, backs up `build/aptly` (and keeps the previous backup until the new one is verified), publishes, then releases the lock.

```
git status --porcelain                  # nothing in the packages' paths: a rebuild gives the same N
ls build/debs/*/*.deb | grep "~dev"      # must print nothing
exec 9>>build/tmp/publish.lock && flock -n 9 && echo "stable publish $(date -Is)" > build/tmp/publish.lock
s=$(date +%Y%m%d-%H%M%S); tar -C build -czf build/old/aptly-$s.tgz aptly
[ "$(tar -tzf build/old/aptly-$s.tgz | grep -vc /$)" = "$(find build/aptly ! -type d | wc -l)" ] && \
  ls build/old/aptly-*.tgz | grep -v aptly-$s.tgz | xargs -r rm -f       # drop the previous backup only once verified
make publish DIST=ps3 DEBS="\
  build/debs/classicube/classicube_1.3.8~git20261014.aa47929-1+ps3.374_ppc64.deb \
  build/debs/classicube/classicube-dbgsym_1.3.8~git20261014.aa47929-1+ps3.374_ppc64.deb \
  build/debs/cube2/cube2_0.0.20201229+dfsg-1+ps3.466_ppc64.deb \
  build/debs/cube2/cube2-dbgsym_0.0.20201229+dfsg-1+ps3.466_ppc64.deb \
  build/debs/cube2/cube2-server_0.0.20201229+dfsg-1+ps3.466_ppc64.deb \
  build/debs/cube2/cube2-server-dbgsym_0.0.20201229+dfsg-1+ps3.466_ppc64.deb \
  build/debs/dsda-doom/dsda-doom_0.30.0+dfsg-1+ps3.361_ppc64.deb \
  build/debs/dsda-doom/dsda-doom-dbgsym_0.30.0+dfsg-1+ps3.361_ppc64.deb \
  build/debs/etr/extremetuxracer_0.8.4-2+ps3.324_ppc64.deb \
  build/debs/etr/extremetuxracer-data_0.8.4-2+ps3.324_all.deb \
  build/debs/etr/extremetuxracer-dbgsym_0.8.4-2+ps3.324_ppc64.deb \
  build/debs/fluidsynth/fluidsynth_2.6.1+dfsg-1+ps3.309_ppc64.deb \
  build/debs/fluidsynth/fluidsynth-dbgsym_2.6.1+dfsg-1+ps3.309_ppc64.deb \
  build/debs/fluidsynth/libfluidsynth3_2.6.1+dfsg-1+ps3.309_ppc64.deb \
  build/debs/fluidsynth/libfluidsynth3-dbgsym_2.6.1+dfsg-1+ps3.309_ppc64.deb \
  build/debs/fluidsynth/libfluidsynth-dev_2.6.1+dfsg-1+ps3.309_ppc64.deb \
  build/debs/ioquake3/ioquake3_1.36+u20260719.58839361+dfsg-1+ps3.462_ppc64.deb \
  build/debs/ioquake3/ioquake3-dbgsym_1.36+u20260719.58839361+dfsg-1+ps3.462_ppc64.deb \
  build/debs/ioquake3/ioquake3-server_1.36+u20260719.58839361+dfsg-1+ps3.462_ppc64.deb \
  build/debs/ioquake3/ioquake3-server-dbgsym_1.36+u20260719.58839361+dfsg-1+ps3.462_ppc64.deb \
  build/debs/libsdl3/libsdl3-0_3.4.18+ds-1+ps3.254_ppc64.deb \
  build/debs/libsdl3/libsdl3-0-dbgsym_3.4.18+ds-1+ps3.254_ppc64.deb \
  build/debs/libsdl3/libsdl3-dev_3.4.18+ds-1+ps3.254_ppc64.deb \
  build/debs/libsdl3/libsdl3-tests_3.4.18+ds-1+ps3.254_ppc64.deb \
  build/debs/libsdl3/libsdl3-tests-dbgsym_3.4.18+ds-1+ps3.254_ppc64.deb \
  build/debs/linux/linux-headers-6.12.111+ps3.472_6.12.111-1+ps3.472_ppc64.deb \
  build/debs/linux/linux-headers-6.12.111+ps3.472-common_6.12.111-1+ps3.472_all.deb \
  build/debs/linux/linux-headers-ps3_6.12.111-1+ps3.472_ppc64.deb \
  build/debs/linux/linux-image-6.12.111+ps3.472_6.12.111-1+ps3.472_ppc64.deb \
  build/debs/linux/linux-image-ps3_6.12.111-1+ps3.472_ppc64.deb \
  build/debs/linux/linux-kbuild-6.12.111+ps3.472_6.12.111-1+ps3.472_ppc64.deb \
  build/debs/linux/linux-kbuild-6.12.111+ps3.472-dbgsym_6.12.111-1+ps3.472_ppc64.deb \
  build/debs/luajit/libluajit-5.1-2_2.1.0+openresty20251030-1+ps3.247_ppc64.deb \
  build/debs/luajit/libluajit-5.1-2-dbgsym_2.1.0+openresty20251030-1+ps3.247_ppc64.deb \
  build/debs/luajit/libluajit-5.1-common_2.1.0+openresty20251030-1+ps3.247_all.deb \
  build/debs/luajit/libluajit-5.1-dev_2.1.0+openresty20251030-1+ps3.247_ppc64.deb \
  build/debs/luajit/luajit_2.1.0+openresty20251030-1+ps3.247_ppc64.deb \
  build/debs/luajit/luajit-dbgsym_2.1.0+openresty20251030-1+ps3.247_ppc64.deb \
  build/debs/luanti/luanti_5.15.2+dfsg-2+ps3.405_ppc64.deb \
  build/debs/luanti/luanti-data_5.15.2+dfsg-2+ps3.405_all.deb \
  build/debs/luanti/luanti-dbgsym_5.15.2+dfsg-2+ps3.405_ppc64.deb \
  build/debs/luanti/luanti-server_5.15.2+dfsg-2+ps3.405_ppc64.deb \
  build/debs/luanti/luanti-server-dbgsym_5.15.2+dfsg-2+ps3.405_ppc64.deb \
  build/debs/mesa/libegl1-mesa-dev_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libegl-mesa0_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libegl-mesa0-dbgsym_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libgbm1_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libgbm1-dbgsym_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libgbm-dev_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libgl1-mesa-dev_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libgl1-mesa-dri_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libgl1-mesa-dri-dbgsym_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libgles2-mesa-dev_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libglx-mesa0_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/libglx-mesa0-dbgsym_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/mesa-common-dev_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/mesa-drm-shim_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/mesa-drm-shim-dbgsym_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/mesa-libgallium_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/mesa-libgallium-dbgsym_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/mesa-va-drivers_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/mesa/mesa-vdpau-drivers_26.2.4-1+ps3.476_ppc64.deb \
  build/debs/neverball/neverball_1.6.0+git20180603-3+ps3.370_ppc64.deb \
  build/debs/neverball/neverball-common_1.6.0+git20180603-3+ps3.370_all.deb \
  build/debs/neverball/neverball-data_1.6.0+git20180603-3+ps3.370_all.deb \
  build/debs/neverball/neverball-dbgsym_1.6.0+git20180603-3+ps3.370_ppc64.deb \
  build/debs/neverball/neverputt_1.6.0+git20180603-3+ps3.370_ppc64.deb \
  build/debs/neverball/neverputt-data_1.6.0+git20180603-3+ps3.370_all.deb \
  build/debs/neverball/neverputt-dbgsym_1.6.0+git20180603-3+ps3.370_ppc64.deb \
  build/debs/openttd/openttd_15.1-1+ps3.297_ppc64.deb \
  build/debs/openttd/openttd-data_15.1-1+ps3.297_all.deb \
  build/debs/openttd/openttd-dbgsym_15.1-1+ps3.297_ppc64.deb \
  build/debs/plymouth/libplymouth5_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/libplymouth5-dbgsym_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/libplymouth-dev_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth-dbgsym_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth-label_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth-label-dbgsym_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth-themes_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth-themes-dbgsym_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth-x11_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/plymouth/plymouth-x11-dbgsym_26.134.222-1+ps3.247_ppc64.deb \
  build/debs/ps3-boot/ps3-boot_0.484_all.deb \
  build/debs/ps3-desktop/ps3-apps_0.474_all.deb \
  build/debs/ps3-desktop/ps3-desktop_0.474_all.deb \
  build/debs/ps3-desktop/ps3-games_0.474_all.deb \
  build/debs/ps3-gpu-tools/ps3-gpu-tools_0.476_ppc64.deb \
  build/debs/ps3-gpu-tools/ps3-gpu-tools-dbgsym_0.476_ppc64.deb \
  build/debs/ps3rsx-ps3/ps3rsx-modules-6.12.111+ps3.472_0.443+k6.12.111+ps3.472_ppc64.deb \
  build/debs/ps3rsx-ps3/ps3rsx-modules-ps3_0.443+k6.12.111+ps3.472_ppc64.deb \
  build/debs/ps3-splash/ps3-no-initramfs_0.247_all.deb \
  build/debs/ps3-splash/ps3-splash_0.247_all.deb \
  build/debs/ps3-spu/libspeq0_0.395_ppc64.deb \
  build/debs/ps3-spu/libspeq0-dbgsym_0.395_ppc64.deb \
  build/debs/ps3-spu/libspeq-dev_0.395_ppc64.deb \
  build/debs/ps3-spu/ps3-spu_0.395_ppc64.deb \
  build/debs/ps3-spu/ps3-spu-dbgsym_0.395_ppc64.deb \
  build/debs/ps3-spu/ps3-spu-programs_0.395_all.deb \
  build/debs/ps3-system/ps3-system_0.489_all.deb \
  build/debs/supertuxkart/supertuxkart_1.5-2+ps3.361_ppc64.deb \
  build/debs/supertuxkart/supertuxkart-data_1.5-2+ps3.361_all.deb \
  build/debs/supertuxkart/supertuxkart-dbgsym_1.5-2+ps3.361_ppc64.deb \
  build/debs/teeworlds/teeworlds_0.7.5-3+ps3.422_ppc64.deb \
  build/debs/teeworlds/teeworlds-data_0.7.5-3+ps3.422_all.deb \
  build/debs/teeworlds/teeworlds-dbgsym_0.7.5-3+ps3.422_ppc64.deb \
  build/debs/teeworlds/teeworlds-server_0.7.5-3+ps3.422_ppc64.deb \
  build/debs/teeworlds/teeworlds-server-dbgsym_0.7.5-3+ps3.422_ppc64.deb \
  build/debs/xmoto/xmoto_0.6.3+repack-1+ps3.441_ppc64.deb \
  build/debs/xmoto/xmoto-data_0.6.3+repack-1+ps3.441_all.deb \
  build/debs/xmoto/xmoto-dbgsym_0.6.3+repack-1+ps3.441_ppc64.deb \
  build/debs/yquake2/yamagi-quake2_8.70+dfsg-1+ps3.415_ppc64.deb \
  build/debs/yquake2/yamagi-quake2-core_8.70+dfsg-1+ps3.415_ppc64.deb \
  build/debs/yquake2/yamagi-quake2-core-dbgsym_8.70+dfsg-1+ps3.415_ppc64.deb \
  build/debs/yquake2/yamagi-quake2-dbgsym_8.70+dfsg-1+ps3.415_ppc64.deb \
"
rm -f build/tmp/publish.lock
```

Afterwards: fetch `http://10.0.0.10:8070/ps3/dists/ps3/main/binary-ppc64/Packages` and check every version above is there. Then `make repo-snapshot` (freezes `ps3` as `img-<stamp>`) and, if wanted, `make rootfs REPO_SUITE=img-<stamp>` to confirm the image from the snapshot equals the tested one (`make checksums`, same sha256).

A second publish, of anything that failed its check and was fixed later (xmoto, classicube, supertuxkart and teeworlds if they were held back), follows the same pattern with its own `DEBS` list. If `ps3-games` changes its Depends again: `make ps3-games-licensed-deb` and `make publish DEBS=build/debs/ps3-games-licensed/ps3-games-licensed_<N>_all.deb` (it goes to `ps3-licensed` whatever `DIST`).

## History

The first plan was drafted on 2026-10-18 at HEAD `0149148`, from the served indices of `http://10.0.0.10:8070/{ps3,ps3-dev,ps3-licensed}`, `build/debs`, `git log --since=2026-10-16` (277 commits) and the docs of the time. It sorted every package READY (proven, no known regression), NEEDS-CHECK (one named check first) or NOT YET, and proposed publishing the READY set on the kernel `.178` with Mesa `.415`, ps3-gpu-tools 0.428 and libspeq/ps3-spu 0.395, from `build/aptly/pool` for the kernel pair, and the rest in a second publish. The release of 2026-10-18 replaced it: instead of promoting old proven builds, everything is rebuilt as official from the current tree, the image is tested, and then everything publishes at once.

What the draft's verdicts decided that still matters:

- READY (15 items): kernel .178 + ps3rsx-modules 0.249, Mesa, libspeq/ps3-spu, ps3-gpu-tools, libsdl3, luajit, ioquake3, yamagi-quake2, dsda-doom, fluidsynth, extremetuxracer, neverball, luanti, cube2, openttd.
- NEEDS-CHECK: xmoto, classicube (the -4% steady fps for better lows in row 61, and the split upgrade), supertuxkart (`lean` default, sound), teeworlds (sound), ps3-system, ps3-desktop/apps/games (game mode and the kicker on the PS3), ps3-boot (kboot.conf), plymouth and ps3-splash (a cold boot). These became the "check still needed" column.
- NOT YET: kernels .402, .419 and .433~dev (boot tests first). Since then .419 and .433 booted (2026-10-18): lost-decrementer fix confirmed (worst idle wakeup 4 ms against 1103 ms with the switch off), `dev_hdd0` mounted read-only, hwmon temperatures, fan SYSCON automatic and full speed, gelic and spufs passed. The table's kernel is the current source built as official .472, which adds local/0043, the NLS options and local/0044 on top of .433.
- SKIP: openal-soft.
- Known gap of the .178 kernel, for the record: the lost-decrementer stalls (fixed in .402+), so games ran with the 10 ms kicker from `ps3-waker`. The `.472` kernel carries the fix (local/0009) and `ps3-game-kick` skips itself there.
- Mesa: 0034, 0035 and 0036 measured neutral in fps (rows 58, 64, 65). The +53% cited for 0033 (row 57) is from the spectator demo; the heavier first-person demo was not used for that A/B.
- Luanti .405: the clean re-runs (row 67) keep `base` as the default (`cliunload` is worse, `lean` draws nothing).

Commands of the draft (kernel `.178` pair from `build/aptly/pool`, a one-line `make publish` of the READY set after `cp -a build/aptly build/aptly-backup-<stamp>`, a second publish of the NEEDS-CHECK packages) are superseded by the command above; the archive keeps them with the full verdict tables and the evidence rows.

Older versions and the dated history: [archive/RELEASE-CANDIDATES.md](archive/RELEASE-CANDIDATES.md).
