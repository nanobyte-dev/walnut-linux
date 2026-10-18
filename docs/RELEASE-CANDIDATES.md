# Release candidates: what could go from ps3-dev to the stable `ps3` suite

Drafted 2026-10-18 at HEAD `0149148`, for the user to approve in one decision. Nothing was
published, built or run on the PS3 for this list. The rule (memory "Publish only when proven"):
stable only after a package is proven on the PS3, an explicit `DEBS=` list, `build/aptly` backed up
first, never a `~dev` version. Promotion (PACKAGING.md): commit, rebuild the official version
(`0.N`, `...+ps3.N`, no `~dev`), `make publish`.

Sources: the served indices of `http://10.0.0.10:8070/{ps3,ps3-dev,ps3-licensed}` (fetched
2026-10-18), `build/debs`, `git log --since=2026-10-16` (277 commits), GAME-PLAN.md, HOTPATHS.md,
MEMORY.md, STALLS.md, DESKTOP-PLAN.md, and `games/bench/results/*.csv`. "fps.csv row N" is the
Nth data row of the matrices (same rows in every CSV; the `observations` column has the text).
Every row cited as proof has the picture checked on the webcam unless said otherwise.

Sections 0-4 below are the draft of 2026-10-18 (kernel `.178` as the stable kernel); the release
section that follows replaces their candidate versions and commands.

## Release 2026-10-18: test first, then publish

**The decision (the user, 2026-10-18 evening).** A stable release of every package and a fresh image,
done as "test first, then publish": every package rebuilt from the current tree as an official
version (no `~dev`), the fresh image built from those debs, the image booted and smoke-tested and
benchmarked on the PS3 in a later session, and only after that the stable publish. The default
kernel is an official build of the current kernel source, the release flavour `6.12.111+ps3.472`
with ps3rsx-modules built for it. It was never booted under that version; its boot check is part of
the image session. This replaces the older plan in sections 2.1 and 4 below (stable on kernel `.178`
with Mesa `.415` and so on). The evidence in sections 2-3 still holds for the older builds it names.

**State (2026-10-18 23:40, tree at `35f2600`).** Every package below is built as an official version
in `build/debs`, and the rebuilt ones are published to `ps3-dev` (backup
`build/old/aptly-20261018-233019.tgz`). Nothing is in stable yet and nothing went to `ps3-licensed`. The PS3 was not
touched; a bench batch was running on it throughout. Rebuilt tonight: kernel .472 (both flavours),
ps3rsx-modules 0.443 for both, Mesa .476, plymouth .247, ps3-splash 0.247, ps3-boot 0.484, ps3-spu
0.395, classicube .374, cube2 .466, xmoto .441, darkplaces-gl2 .460, ps3-system 0.489 (with the SSD
agent's `PS3_VSWAP_DM` commit), ps3-gpu-tools 0.476. The rest were official builds of the current
tree already. Side effect for the PS3 queue: `make deploy` now takes Mesa .476 from `ps3-dev` over
.466~dev (same patches, 0037-0039 included, committed), and ps3-system 0.489 over 0.419.

**The image.** `build/rootfs/debian-ppc64.tar`, 3,082,752,000 bytes (2.9 GiB), sha256 `65dae499d6bcab5effcf5233320b2f8f3124a8b7bb9fc8cbe0d403db67c8aa71`
(`make checksums`: `build/SHA256SUMS`). A copy of this exact file is kept at
`/mnt/data_linux/Temp/ps3/release-20261018/debian-ppc64.tar`, because `make nfs-root` and `make usb-root` rebuild the image first if any deb in
`build/debs` is newer than the tar. The base layer `build/rootfs/base.tar` is from 2026-10-16 (no
change to `image/packages.txt` since). 812 packages. Our packages in it are exactly the versions in the table, both kernels (release and `-debug202610181726`) with their ps3rsx modules, `classicube-data` .366 and the other licensed data from `ps3-games-licensed`. `/etc/kboot.conf` has `default=6.12.111+ps3.472` (release; the debug entry is listed above it), systemd-networkd and ssh are enabled, LLVM and z3 autoremoved. It is 0.9 GB larger than the 2026-10-16 image, almost all game data: cube2-data (260 MB) and sauerbraten-nonfree-data (146 MB) through `ps3-games-licensed`, the Quake II demo, the OpenArena sets.

### The release table

"Proven" means it ran on the PS3 with the picture checked on the webcam (fps.csv rows as in
section 2). Versions: `N` is `+ps3.N` for Debian-source packages and `0.N` for our native ones.

| Package (binaries) | Candidate (official) | What the PS3 proved, with which version | Check still needed in the image session |
|---|---|---|---|
| Kernel, release flavour: `linux-image-6.12.111+ps3.472`, `linux-headers-6.12.111+ps3.472`, `linux-headers-6.12.111+ps3.472-common`, `linux-kbuild-6.12.111+ps3.472`, metas `linux-image-ps3`, `linux-headers-ps3` | 6.12.111-1+ps3.472 | `.433~dev` booted 2026-10-18 and runs every bench since: idle fix (local/0009; on/off clean in row 68: gameplay stalls 0.4 vs 8-9 s), hwmon temperatures, fan SYSCON automatic and full speed, gelic, spufs, `dev_hdd0` read-only. Not booted: local/0043 (console frame release, off by default), the vfat NLS options (.457), local/0044 (OS area writes off, .472) | Boot; the kernel checks below (dmesg, temps, fan auto, `ps3_os_area` disabled line, poweroff, button taps, reboot of local/0041, `ps3-gameos-mount`) |
| `ps3rsx-modules-6.12.111+ps3.472`, meta `ps3rsx-modules-ps3` | 0.443+k6.12.111+ps3.472 | 0.249 on .178, .419, .433: every row from 35 to 68. 0.443 adds only `shadow_fast` (default off) | Loads at boot, `shadow_fast` N, `rsx-suite.sh all`, the smoke runs |
| Mesa set (`libegl-mesa0`, `libegl1-mesa-dev`, `libgbm-dev`, `libgbm1`, `libgl1-mesa-dev`, `libgl1-mesa-dri`, `libgles2-mesa-dev`, `libglx-mesa0`, `mesa-common-dev`, `mesa-drm-shim`, `mesa-libgallium`, `mesa-va-drivers`, `mesa-vdpau-drivers`) | 26.2.4-1+ps3.476 | 0012-0034 with the LUT default: .407/.408~dev rows 57-63; 0035-0036 .416~dev rows 64-65 (neutral); .442~dev (0037, off by default) in batch 17. 0038 (`NV30_FORCE_DXT`) and 0039 (`NV30_SHADER_CACHE`) are off by default and never ran on the PS3 | Smoke of every recipe (picture), the 5-game bench against row 68 |
| `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` | 0.395 | 0.394: DXT1/DXT5 on the SPEs (rows 6, 7, 59), fluidsynth pool (rows 50, 52). 0.395 adds only a doc | `SPEQ_DEBUG=1` line in one smoke run (dsda-doom music or Teeworlds) |
| `ps3-gpu-tools` | 0.476 | 0.453 runs every bench since batch 15 (`ps3-intfwatch`); 0.469 adds `BENCH_PERF_LOAD` (batch 17); 0.476 adds Mesa 0038's test | The bench runs themselves (`intf` column, temps) |
| `ps3-boot` | 0.484 | 0.176 (flavour names) in ps3-dev; no `kboot.conf` from it read on the PS3. 0.484 adds only `webman/autoexec.bat` (not installed by the package) | `default=` is the release kernel (read from the image tar); check which entry Petitboot preselects |
| `ps3-system` | 0.489 | 0.419 installed (apt sources and pins, `ps3-gameos-mount` for `dev_hdd0`). Not run: 0.457's vfat retry for `dev_hdd1`, 0.489's opt-in `PS3_VSWAP_DM=1` (off by default, ff91438); the networkd enable (bc0f08a) only shows on a fresh install | networkd and ssh enabled in the fresh image, DHCP renewal; `ps3-gameos-mount` both disks; `ps3-game-kick` skips itself on this kernel (`ps3_idle_prep=Y`) |
| `ps3-desktop`, `ps3-apps`, `ps3-games` | 0.474 | 0.474 installed (game prefetch off by default, row 68); 0.458's escape keys and jgmenu fix not yet tried at the TV | Section 3 below (with the user) |
| `plymouth`, `libplymouth5`, ..., `ps3-splash`, `ps3-no-initramfs` | 26.134.222-1+ps3.247, 0.247 | .182 / 0.182 installed since 2026-10-16; .247 is the same source rebuilt (a tooling commit moved N) | One cold boot on the release kernel: splash from Petitboot to the desktop |
| `libsdl3-0`, ... | 3.4.18+ds-1+ps3.254 | .254: OpenTTD colours and fps (row 35), dsda-doom, Chocolate/Crispy/Woof smoke | Smoke runs (OpenTTD, dsda-doom, the Doom ports) |
| `libluajit-5.1-2`, `libluajit-5.1-common`, `libluajit-5.1-dev`, `luajit` | 2.1.0+openresty20251030-1+ps3.247 | .247 under Luanti in every row from 36 | Luanti smoke and bench |
| `luanti`, `luanti-data`, `luanti-server` | 5.15.2+dfsg-2+ps3.405 | .405, rows 54-68 (base kept as the default, row 67) | Bench (5 games) |
| `ioquake3`, `ioquake3-server` | 1.36+u20260719.58839361+dfsg-1+ps3.462 | .396 proven (row 59: stalls 9-10 s -> 0); .462 adds 0027-0029 (opengl2 vertex formats, mipmaps, packed MD3, the last opt-in), in batch 17 | OpenArena smoke (opengl1 `base`) and `gl2min` in the bench |
| `yamagi-quake2`, `yamagi-quake2-core` | 8.70+dfsg-1+ps3.415 | .408 with `YQ2_KISS_QSIZE=4096` (row 57); .415 makes it the default | Smoke |
| `dsda-doom` | 0.30.0+dfsg-1+ps3.361 | .361 rows 52, 61 | Smoke, music by ear (D3) |
| `fluidsynth`, `libfluidsynth3`, `libfluidsynth-dev` | 2.6.1+dfsg-1+ps3.309 | .309 rows 50, 52, the user listened | With dsda-doom's smoke |
| `extremetuxracer`, `extremetuxracer-data` | 0.8.4-2+ps3.324 | .324 row 49 | Smoke |
| `neverball`, `neverputt`, `neverball-data`, `neverball-common`, ... | 1.6.0+git20180603-3+ps3.370 | .370 row 54 | Smoke |
| `cube2`, `cube2-server` | 0.0.20201229+dfsg-1+ps3.466 | .354/.355 rows 57-65; .442~dev (0011 off by default) in batch 17 | Bench (5 games) |
| `openttd`, `openttd-data` | 15.1-1+ps3.297 | .297 rows 58, 61 | Smoke |
| `xmoto`, `xmoto-data` | 0.6.3+repack-1+ps3.441 | .323 batching (row 49); .362~dev with the stream buffer (row 68: 162 fps); .441 is that source committed | Smoke |
| `classicube` (engine only) | 1.3.8~git20261014.aa47929-1+ps3.374 | .367~dev rows 61-68 (`base` 45.9 fps in row 68); the split upgrade from the bundled stable .152 checked in the chroot only | Smoke; the bench (5 games); on a PS3 with the bundled .152 the upgrade (only matters for installs, not the fresh image) |
| `supertuxkart`, `supertuxkart-data` | 1.5-2+ps3.361 | .361 rows 61-68 (`lean` 107 fps in row 68); `sound=none` in every run | Bench (`lean`); sound by ear (2.1); D4 (`lean` as the default) is still open |
| `teeworlds`, `teeworlds-server`, `teeworlds-data` | 0.7.5-3+ps3.422 | .422 smoke only (row 62, 183 fps) | Smoke, sound by ear |

Not in the stable list: the debug kernel flavour (`linux-image-6.12.111+ps3.472-debug*`,
`linux-image-ps3-debug`, `ps3rsx-modules-ps3-debug`) and all `*-dbgsym` stay in `ps3-dev`
(dbgsym of a stable publish goes to `ps3-dbgsym` anyway); `openal-soft` (unpatched rebuild, section
2.3: SKIP); `darkplaces-gl2` (NOT YET: never run on the PS3, D12). Licensed packages
(`classicube-data`, `quake-shareware`, `quake2-demo-data`, `sauerbraten-nonfree-data`,
`ps3-games-licensed`) are already in `ps3-licensed` at the versions the image carries, so nothing
licensed is published. The image carries `classicube-data` .366 from the pool, not the .374 the
classicube rebuild made (identical files, and a new copy would sit in `ps3-licensed` forever).

### PS3 test plan for the image session

With the user at the console. Before the OtherOS flag: webMAN fan control in SYSCON (automatic) mode
or a fixed speed of at least 60%; stop on any "PS3 Thermal Alert"; the boot flag goes back to GameOS
at the end. No other bench runs and no deploys during the session's runs (memory "No commands during
runs").

**1. Boot the image.** Either over NFS (keeps the USB disk as it is):

    make checksums                        # compare with the sha256 above: the tar must be the tested one
    make nfs-root                         # sudo; old export kept as .bak-<date>; FRESH=1 for a clean state
    make listen                           # second terminal: netconsole
    make netboot-deb FLAVOUR=release ROOT=nfs   # PS3 in Petitboot: boots 6.12.111+ps3.472 with its .args

or later as the root on the USB disk or the SSD region (docs/GAMEOS-HDD.md, test drive), with
`make usb-root` (over SSH from the running PS3; the old root is moved aside on the disk) or
`make usb-root-petitboot` from Petitboot, then `make netboot-deb FLAVOUR=release ROOT=usb` or the
Petitboot entry. If any deb in `build/debs` changed after the image, put the kept copy back first
(`cp /mnt/data_linux/Temp/ps3/release-20261018/debian-ppc64.tar build/rootfs/debian-ppc64.tar`, then `touch` it) or `make` builds a new image.
Petitboot ignores `default=` (PACKAGING.md "Kernels"): pick the `6.12.111+ps3.472` entry by hand
once, then it is the remembered default.

**2. Kernel checks** (release `6.12.111+ps3.472`, ps3rsx `0.443`):
- `uname -r` is `6.12.111+ps3.472`; `cat /etc/kboot.conf` has `default=6.12.111+ps3.472` and both entries
  (ps3-boot 0.484 with `PS3_DEFAULT_FLAVOUR=release`).
- `dmesg -l err,warn`; `dmesg | grep -i -e rcu -e stall -e "soft lockup" -e oops` empty;
  `cat /sys/module/kernel/parameters/ps3_idle_prep` is `Y`; idle `uptime` load under 0.3.
- Temperatures: `cat /sys/class/hwmon/hwmon*/temp*_input` (ps3_sm, two zones, idle about 45-55 C).
- Fan: `dmesg | grep -e 'fan:' -e REQUEST_ERROR` shows `fan: SYSCON automatic` and no refusal.
- OS area: `dmesg | grep -e ps3_os_area -e 'FLASH ROM'` shows "ps3_os_area: writes to the OS area
  in flash disabled, RTC changes last until reboot", and still no "Could not update FLASH ROM" 12
  minutes after `timedatectl show -p NTPSynchronized` says yes;
  `cat /sys/module/ps3_os_area/parameters/write` is `N`.
- ps3rsx: `lsmod | grep ps3rsx`, `cat /sys/module/ps3rsx/parameters/shadow_fast` is `N`;
  `rsx-suite.sh all` passes.
- Network and services: `systemctl is-enabled systemd-networkd ssh` (both enabled: the IMAGE-DIFF
  fix), `networkctl` shows eth0 configured by `20-ps3-wired.network`, a DHCP lease renewal in
  `journalctl -u systemd-networkd`; `apt update` sees `ps3`, `ps3-dev`, `ps3-licensed`.
- `sudo ps3-gameos-mount`: `dev_hdd0` and `dev_hdd1` both "(read-only)", no "retrying" line (NLS).
- Splash (release kernel only): from Petitboot to the desktop with the Walnut splash, no black gap,
  no text on tty1 (plymouth .247, ps3-splash 0.247).
- Power paths of local/0041, one each, watching the console and netconsole: `sudo poweroff` (off a
  few seconds after "System Halted", no "no shutdown command after 5 s"); boot again, `sudo reboot`
  (back to Petitboot/GameOS); boot again, `sudo poweroff` and tap the power button about once a
  second until it is off (never hold it). THERMAL.md "PS3 test steps" 5.

**3. Desktop and game mode, with the user** (ps3-desktop 0.474, PLAN 1.10): the desktop comes up
(wallpaper, tint2, applets); start a game from the Games menu and leave it; a fullscreen GL program
started from a terminal gets game mode by itself (`journalctl -t ps3-game-mode`: on, then
`left game mode`), `PS3_GAME_MODE=off` keeps it out; Ctrl+Alt+Esc and the PS button held 3 s end a
game; time the desktop's return; the wallpaper, applets and frozen services are back
(`games/bench/gamemode-check.sh`).

**4. Smoke run of every game recipe, webcam on** (`make bench-push`, then on the PS3
`/var/tmp/bench/run.sh smoke <game>` for each of the 25 recipes in `games/bench/recipes/`, or
`run.sh suite base 1` for the 12 suite ones plus the rest one by one; `tools/bench-cam.sh` photographs
each run, look at every photo). Pass: the game draws its scene (no black screen, right colours), no
kernel error, no RSX wedge, sound line not `none` where the game has sound (the user judges sound by
ear: SuperTuxKart, Teeworlds, Luanti, dsda-doom music). The menu-only games (TORCS, Trigger Rally,
Chromium B.S.U.) and Nexuiz (needs D12) need the user.

**5. Short bench of the 5 start-up games** (docs/PROFILING.md "Start-up profiling": the same
variants, x2 each, desktop stopped, swap emptied, 60 s warm-up and a 120 s window). Today's numbers,
batch 16 (fps.csv row 68, kernel .433, 2026-10-18), steady fps / 1% low in play / gameplay stall s /
longest gameplay frame ms:

| Game, variant | Today (row 68) |
|---|---|
| Sauerbraten `base` | 38.8 / 24.1 / 1.4 / 514 |
| OpenArena `gl2min` | 51.8 / 14.8 / 11.0 / 2038 |
| Luanti `base` | 46 / 17.5 / 2.5 / 358 |
| SuperTuxKart `lean` | 107.3 / 11.4 / 1.8 / 686 |
| ClassiCube `base` | 45.9 / 30.1 / 0.1 / 99 |

These are across boots, so they are a regression check, not an A/B (memory "Same-boot baselines"):
flag anything more than about 10% worse in steady fps or 1% low, or any new multi-second stall,
and re-check it in the same boot against its switch. Note that row 68 ran with Mesa .442~dev and
ioquake3 .396-.462; the image has Mesa .476 (0037-0039 off by default) and ioquake3 .462.

### The stable publish (NOT run; after the image session passes, with the user's OK)

Drop any row that failed its check from the list first. Back up `build/aptly` and hold the lock as
for every publish:

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

Afterwards: fetch `http://10.0.0.10:8070/ps3/dists/ps3/main/binary-ppc64/Packages` and check every
version above is there; then `make repo-snapshot` (freezes `ps3` as `img-<stamp>`) and, if wanted,
`make rootfs REPO_SUITE=img-<stamp>` to confirm the image from the snapshot equals the tested one
(`make checksums`, same sha256).

## 0. Since the draft (2026-10-18 evening, dated note)

Still nothing published to stable. What changed for the verdicts below (rows 63-67 of the matrices,
the boots of .419 and .433):

- **Kernels.** .419 booted (lost-decrementer fix confirmed: worst idle wakeup 4 ms against 1103 ms
  with the switch off; `dev_hdd0` mounted read-only), then .433 (the fix, hwmon temperatures, fan
  SYSCON automatic and full speed, gelic, spufs: all passed). .433 moves from NOT YET towards
  NEEDS-CHECK: left are local/0041's power-off, button and reboot checks, then an official rebuild
  (it is `~dev`). .457 is built for the next boot (NLS for `dev_hdd1`, local/0043 off by default,
  ps3rsx 0.443 with `shadow_fast` off). .178 stays the READY kernel until then.
- **Mesa.** The READY candidate stays the proven tree (ps3.408~dev rebuilt as official). 0034, 0035 and
  0036 measured neutral in fps (rows 58, 64, 65). The +53% cited for 0033 (row 57) is from the
  spectator demo; the heavier first-person demo was not used for that A/B. ps3.442~dev (0037, switches
  off by default) is in ps3-dev and never ran on the PS3: not a candidate yet.
- **Luanti 405**: READY as before. The clean re-runs (row 67) keep base as the default, so the package
  needs no new default (`cliunload` is worse, `lean` draws nothing).
- **ioquake3**: the candidate stays 396; 462 (0027-0029) is unmeasured. **cube2**: the candidate stays
  354; 442~dev (0011, off by default) is unmeasured.
- **supertuxkart**: one more check: every run logged `sound=none` (the ALSA device never opened).
- **ps3-gpu-tools**: 0.453 (with `ps3-intfwatch`) runs on the PS3 in every bench run since batch 15;
  rebuild that as the candidate instead of 0.428.
- **ps3-desktop**: 0.458 (jgmenu freeze fix, escape keys) is installed on the PS3, not yet tried at
  the TV; the NEEDS-CHECK list below still applies, at the newer version.
- **ps3-system**: 0.457 built (the `ps3-gameos-mount` vfat retry); the game-mode kicker is still not
  seen running through game mode (and skips itself on the fixed kernels).
- **New, NOT YET**: `darkplaces-gl2` (Nexuiz, not run on the PS3).

## 1. What is where

### Stable `ps3` now (everything dates from 2026-10-16 morning, builds up to .163)

| Package | Stable | Latest in ps3-dev | Latest built locally / would build now |
|---|---|---|---|
| kernel (`linux-image-*`, headers, kbuild, metas) | 6.12.111-1+ps3.152 | .433~dev (also .178, .402, .419) | .433~dev; source now builds .435 |
| `ps3rsx-modules-*` | 0.152+k.152 | 0.249+k.178 / .402 / .419 / .433~dev | 0.249+k.433~dev |
| mesa set (13 binaries) | 26.2.4-1+ps3.152 | .408~dev (official .407) | .408~dev; source now .415 |
| `ps3-gpu-tools` | 0.152 | 0.412 | 0.412; source now 0.428 |
| `ps3-spu`, `ps3-spu-programs` | 0.152 | 0.394 (with `libspeq0`, `libspeq-dev`, which stable lacks) | 0.394; source now 0.395 |
| `ps3-boot` | 0.152 | 0.176 | 0.176; source now 0.247 |
| `ps3-system` | 0.160 | 0.419 | 0.419 (matches source) |
| `ps3-desktop`, `ps3-apps` | 0.160 | 0.375 (and 0.376~dev) | 0.376~dev; source now 0.406 |
| `ps3-games` | not in stable | 0.375 (0.376~dev) | 0.376~dev; source now 0.406 |
| `classicube` | 1.3.8~git20261014.aa47929-1+ps3.152 (bundled Mojang data) | .367~dev | .367~dev; source now .374 |
| `quake-shareware`, `quake2-demo-data` | 89 | none | 89 |
| plymouth, `ps3-splash`, `ps3-no-initramfs` | not in stable | 26.134.222-1+ps3.182, 0.182 | same; source now .247 / 0.247 |
| `libsdl3`, `luajit`, `luanti`, `cube2`, `ioquake3`, `yamagi-quake2`, `dsda-doom`, `extremetuxracer`, `xmoto`, `neverball`, `openttd`, `fluidsynth`, `openal-soft`, `supertuxkart`, `teeworlds` | none of them | see section 2 | see section 2 |

Also in stable, legacy and untouched: `ps3-mesa` 26.2.4+ps3.99999, `ps3-kernel-6.12.112-ps3-debug-*`,
`ps3rsx-6.12.112-*` (old pipeline). Leave them.

### `ps3-licensed` now (add-only, already published)

`classicube-data` 1.3.8~git20261014.aa47929-1+ps3.366, `quake-shareware` 89, `quake2-demo-data` 89,
`sauerbraten-nonfree-data` 0.0.20201229-1+ps3.388, `ps3-games-licensed` 0.388 (and 0.374).
Nothing new is waiting there. The catch: `ps3-games-licensed` Depends on `ps3-games`, which is not
in stable, so on the PS3 it installs only with `-t ps3-dev` until `ps3-games` is promoted.

### Rebuild status of `build/debs`

`build/debs` already holds official (non-`~dev`) builds of: libsdl3 254, luajit 247, luanti 405, cube2 354,
ioquake3 396, yquake2 415, dsda-doom 361, etr 324, xmoto 361, neverball 370, openttd 297, fluidsynth 309,
openal-soft 273, supertuxkart 361, teeworlds 422, ps3-system 419, ps3-games-licensed 388,
sauerbraten-nonfree-data 388, quake-data 89. These need no rebuild.

`build/debs` holds only `~dev` builds of: mesa (408~dev), classicube (367~dev), ps3-desktop/ps3-apps/ps3-games
(376~dev), linux and ps3rsx-modules (433~dev). `make publish` refuses those; rebuild first.

Stale (built before a later commit of the package's directory or of the shared packaging tools, so a
rebuild gives a higher version): ps3-gpu-tools 0.412 (now 428), ps3-spu 0.394 (395), ps3-boot 0.176,
plymouth 182 and ps3-splash 0.182 (all three 247, since `a148c56` touched `tools/deb-build.sh`).
N depends on the last commit touching the package or `tools/deb-*.sh`/`docker/Dockerfile.deb`, so
**any commit to those tools before the rebuild shifts every number below**. This file is docs only
and does not.

## 2. Verdicts

READY: proven on the PS3, no known regression. NEEDS-CHECK: one named check first. NOT YET: do not
publish. "Proven" below means a same-boot run on the PS3 with the picture seen on the webcam; where a
stable-worthy number exists it is cited. The 60 fps target (memory "60 fps target") is a performance
goal, not a stability gate; it is listed in the notes so the user can still say no.

### 2.1 Kernel and driver (go together)

| Item | Verdict | Evidence and gaps |
|---|---|---|
| Kernel `6.12.111-1+ps3.178` (release flavour, with headers, `-common`, kbuild, metas `linux-image-ps3`, `linux-headers-ps3`) plus `ps3rsx-modules-6.12.111+ps3.178` and meta `ps3rsx-modules-ps3` at `0.249+k6.12.111+ps3.178` | READY | The PS3 has run this kernel with ps3rsx 0.249 for the whole 2026-10-17/18 work: every fps.csv row from 35 ("ps3rsx 0.249 first boot") to 62 (HOTPATHS.md names kernel .178 for the profiling batch; THERMAL.md "Confirmed on the PS3 (2026-10-18, kernel .178)"). 0.249: GL tests pass, VT switch works with a GL client, no Neverball wedge (`1c1b09a`). Files already exist as official versions in `build/aptly/pool` (dev suite), so no rebuild and no new bits. Open: which flavour (release or debug) the user booted is not recorded in the docs; this list assumes release (the image default). The debug flavour 178 files exist the same way if wanted. Not verified: the splash and DPMS changes of `f30b505` were "tested on hardware" only by the PS3 working on the later boots, the log has no explicit user confirmation (see ps3-splash). |
| ps3rsx 0.249 alone (as `0.249+k` for 402/419/433) | NOT YET | Those module builds pair with unbooted kernels. |

Known gap of .178 that stays in stable if published: the lost-decrementer stalls (fixed only in the
unbooted .402+). Games run with the 10 ms kicker from `ps3-waker` (bench) for now; see 2.6.

### 2.2 Test kernels (separate: NOT YET until the user's reboot session)

| Kernel | What is in it | Verdict |
|---|---|---|
| .402 (dev, official version) | local/0009, idle without `lv1_pause` while an interrupt is pending (lost decrementer fix), `ps3_idle_prep` off switch (`b303e08`, `5de5764`). STALLS.md: "built, not yet booted". | NOT YET. Needs: boot, probe test 1 shows no late wakeups, OpenArena `stall_play_s` about 7 s or less with `-` / `STALLS=1` / kick equal, watch fan and `uptime` (idle threads could spin hotter). |
| .419 (dev, official version) | .402 plus local/0020-0032 survey patches (gelic RX, spufs fixes), local/0040 hwmon `ps3_sm` temperatures, UFS_FS=m read-only (`72b7be6`), local/0060 `/dev/ps3sysmngr` still present. "Nothing is booted yet" (OTHEROS-PATCHES.md). | NOT YET. Superseded by .433+. |
| .433~dev (built, dirty), source now .435 | .419 minus the removed patches (0060, 0090, 0170/0160/0316, `PS3_DISP_MANAGER` off, `3bd717c`), plus local/0041 (final shutdown poll, `bf2a6fe`) and local/0042 (fan policy through the System Manager: SYSCON automatic at boot and on every exit path, `ec834df`). Compile-tested only; the `get` reply layout is a guess confirmed only at boot; the first boot should use `fan_policy=keep` (THERMAL.md). | NOT YET. Needs the user's reboot session: boot with `keep`, then `auto`, confirm temperatures, fan state, power off and reboot paths. Never mix with a publish: rebuild as official only after that session (`make linux-deb`, then `make ps3rsx-modules`). |

### 2.3 Graphics stack and SPU

| Package | Stable -> candidate | Verdict | Evidence, gaps |
|---|---|---|---|
| mesa set (libegl-mesa0, libegl1-mesa-dev, libgbm-dev, libgbm1, libgl1-mesa-dev, libgl1-mesa-dri, libgles2-mesa-dev, libglx-mesa0, mesa-common-dev, mesa-drm-shim, mesa-libgallium, mesa-va-drivers, mesa-vdpau-drivers) | 26.2.4-1+ps3.152 -> **+ps3.415** (rebuild) | READY | Patches 0012-0034 over stable. L8A8/mipmap/offset fixes proven by gl-texfmt, gl-genmip, gl-mipread PASS (fps.csv rows 18, 22; Neverball no longer wedges). Per-draw rounds: ps3.262 OpenArena +23%, Yamagi +24%, X-Moto +21%, ETR +15% (row 38); ps3.299 clean same-boot A/B against .282 (rows 47, 48); `NV30_TRANSLATE_LUT` default: Sauerbraten 25.9 -> 39.6 steady (+53%, row 57), QuakeSpasm neutral (row 58). Mesa 408~dev (which includes the LUT default of `d7e057a`) ran in rows 59-62 with the picture right. The rebuilt .415 is the same source tree as that 408~dev (the only commit after the build is the one that made it clean); the contents should match apart from the version, but this has not been diffed. Known costs, accepted: X-Moto -5% on .299 vs .282 (row 47), ClassiCube -4% with the game-side batching (row 61), Armagetron's ps3.262 loss fixed by 0027. Should go with ps3rsx-modules 0.249 (assumption: 0027-0031 use GART index and fragment-program fetches and were only ever run on 0.249; no old-module run exists). Optional dlopen of libspeq: publish with libspeq0 so DXT5/DXT1 go to the SPEs. 0034 (`NV30_FENCE_BO_POOL`) and 0033's off switch are measured neutral. |
| `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` | 0.152 (no libspeq0) -> **0.395** (rebuild; 0.394 is the proven build, 0.395 adds only `ps3-spu/doc/dxt-rgb.md`, `18d2693`) | READY | Ship all four together (the SPU programs must match libspeq0). Proof: DXT1/DXT5 on the SPEs byte for byte against the reference (`speq-dxt --check`, 25 sizes, big-endian run in the chroot); `speq-bench` probes (rows 6, 7); RGB DXT 1024x1024 1.0 ms on 4 SPEs vs about 175 ms on the PPE (row 59); fluidsynth underruns 1231 -> 588 and 1331 -> 709 with the user's ear "night and day" (row 50); shared 2-SPE deadline pool 465 vs 1095 underruns, 0 timeouts (row 52); Sauerbraten skinning on the SPEs measured (row 54: the SPU path adds hitches, PPE path 28.1, cube2 defaults to CPU skinning). Gap: the SPU RGB-DXT1 path is exercised by no game (Sauerbraten's demo uploads none, row 59), covered by the unit test only. |
| `ps3-gpu-tools` | 0.152 -> **0.428** (rebuild) | READY | 0.412 ran on the PS3 (stall recorder `ps3-stallrec`, `ps3-waker`, `ps3-malloctrim.so`, `perf-post.sh` fixes: rows 55-62, docs/STALLS.md "Confirmed on the PS3"). 0.428 = 0.412 plus `ps3-sm-temp.sh` and docs (`b5b0cad`, `3fd288b`); the new script is run-tested only on the kernels that expose the System Manager temperatures (not booted), harmless without them. |
| `libsdl3` | none -> **3.4.18+ds-1+ps3.254** (as built) | READY | Native BGRA upload on big-endian: OpenTTD about 2 -> 8 fps with right colours (row 35), dsda-doom colours right, Chocolate/Crispy/Woof smoke runs right (GAME-PLAN smoke table, 2026-10-18). Pin 990 makes it win over Debian's libsdl3. |
| `libluajit-5.1-*`, `luajit` | none -> **2.1.0+openresty20251030-1+ps3.247** (as built) | READY | Fixes the `luaL_newstate` failure on ppc64 (about half of the starts); Luanti runs on it in every row from 36 on. Goes with luanti. |
| `openal-soft` | none | NOT NEEDED | Debian's source unpatched, built only to get matching debug symbols (`5d43987`). Publishing it would just shadow Debian's libopenal1 with an identical rebuild. Skip. |

### 2.4 Games

All of these are ps3-dev builds without a stable version; publishing them brings the dev package into
the stable suite. "Perf" is the steady fps on the PS3 against the 60 fps target.

| Package | Candidate | Verdict | Evidence, gaps |
|---|---|---|---|
| `ioquake3`, `ioquake3-server` | 1.36+u20260719...+ps3.396 | READY | OpenArena: opengl2 memory patches 14.8 -> 26-32 fps and longest stall 24.7 -> 6 s (row 46); batching 0020-0022 neutral, atlas (0019) off by default after it cost about 10 s of hitches (rows 51, 53); `r_keepImages` + `cl_preloadLate` (0025, 0026): gameplay stalls 9-10 s -> 0, longest 1 s -> 50-60 ms, steady 65 -> 70.6 fps, same boot, webcam right (row 59, `2d3243c`). Perf: 70 fps opengl1; opengl2 53 fps (row 61). |
| `yamagi-quake2`, `yamagi-quake2-core` | 8.70+dfsg-1+ps3.415 (as built) | READY | Sound fix (+ps3.240, user confirmed, row 31), 63.9 fps (row 38); `YQ2_KISS_QSIZE` 4096 default: RSS 125 -> 56 MB, swap 80 -> 9 MB, gameplay stalls 22.6 -> 0.05 s, steady 65.7 -> 68.7 (row 57, run as variant kiss4k on .408; .415 makes the same value the default, so .415 itself was not run, equivalence by the env switch). Perf meets 60. |
| `dsda-doom` | 0.30.0+dfsg-1+ps3.361 | READY | Colour and GL fixes (row 28), GL batching: steady 55.9 vs 54.2 off, low1 40.8 vs 36.5 (row 61); with fluidsynth on the SPEs. Music still chops somewhat: 465 underruns a minute on the SPE pool vs 1095 on the PPE (row 52). Perf under 60 (40-56). |
| `fluidsynth`, `libfluidsynth3`, `libfluidsynth-dev` | 2.6.1+dfsg-1+ps3.309 | READY | Voice DSP on the SPEs through libspeq: CPU 44.5 -> 31.8 s a minute, underruns halved, the user listened (row 50); needs `libspeq0` >= 0.367 for the pool (row 52). Falls back to the PPE path without it. |
| `extremetuxracer`, `extremetuxracer-data` | 0.8.4-2+ps3.324 | READY | Game-side batching 19,400 -> 850 GL calls; race replay 52 -> 59, picture identical on the webcam (row 49). |
| `neverball`, `neverputt`, `-data`, `-common` | 1.6.0+git20180603-3+ps3.370 | READY | Batching: CPU 80 -> 65 s, smooth fps 79.1 vs 78.7, stalls similar (row 54); no RSX wedge on ps3rsx 0.249 (row 35). Perf meets 60. |
| `luanti`, `luanti-data`, `luanti-server` | 5.15.2+dfsg-2+ps3.405 | READY | Builds on luajit .247. Batching stalls 6.25 vs 9.1 s, longest 1.7 vs 3.4 s, no underruns (row 54); Ogg Vorbis byte-order fix (sounds were noise, `7b5b968`). 405 only adds `LU_TRIM_MB` with the old default (trim16 measured and rejected, row 57). Perf 45-47 steady, memory stalls remain. |
| `cube2`, `cube2-server` (Sauerbraten) | 0.0.20201229+dfsg-1+ps3.354 | READY | Without our memory patches it was OOM-killed; steady 25.9 -> 39.6 with Mesa's LUT (row 57), picture right. CPU skinning is the default; the SPU skinning hook is selectable only (row 54). Models and sounds need `sauerbraten-nonfree-data` (ps3-licensed). Perf 39 steady, under 60. |
| `openttd`, `openttd-data` | 15.1-1+ps3.297 | READY | Software video works (right colours with SDL3 .254); the new OpenGL driver: right picture on the webcam, 21 fps vs 8-12 software (rows 58, 61). The software drawing is "far under" 60. |
| `xmoto`, `xmoto-data` | 0.6.3+repack-1+ps3.361 | NEEDS-CHECK | The batching (+ps3.323) is proven: 44.5 -> 166.5 fps, picture identical (row 49). The .361 build adds only the env-gated `XMOTO_BENCH_LOOPS` patch (`f7edfa5`) and was not run on the PS3 (no X-Moto row after 49). Check: one `run.sh smoke xmoto` (or bench base) on the built .361 and the webcam; or publish the proven .323 from `build/aptly/pool` instead. |
| `classicube` (engine) | 1.3.8~git20261014.aa47929-1+ps3.**374** (rebuild) | NEEDS-CHECK | Batching 0001-0002 (`02e71ba`) run in row 61: base 47.3 vs gloff 49.4 fps steady (-4%, single pair), low1 31 vs 27.6, `NV30_FENCE_BO_POOL` neutral; the picture right. Check 1: decide if -4% steady for better lows is acceptable, or repeat the same-boot A/B with 3 rounds. Check 2 (the split): the upgrade from the bundled stable .152 (`classicube` files move to `classicube-data` in ps3-licensed) was checked with dpkg in the chroot only (PACKAGING.md), not on the PS3; run `make deploy PKGS="ps3-system classicube"` on the PS3 with a bundled version installed. `classicube-data` .366 is already in ps3-licensed, so publish only the engine deb (not `classicube-data` .374: a new 15 MB copy of the same files). |
| `supertuxkart`, `supertuxkart-data` | 1.5-2+ps3.361 | NEEDS-CHECK | Loads, karts and track draw (big-endian .spm fixes, webcam, row 61): `lean` 21.4 smooth fps vs 13 default, stalls 2.4 vs 13.5 s; "out of VRAM" lines remain; sound not recorded. Check: decide whether `lean` becomes the package default (it is the only playable setting), then one run with sound audible (`tools/webcam-audio.sh`) and no kernel errors. |
| `teeworlds`, `teeworlds-server`, `teeworlds-data` | 0.7.5-3+ps3.422 | NEEDS-CHECK | The big-endian `FIONBIO` fix (`840a3b5`) is the whole change: in game on dm1, 183 fps, one smoke run (row 62). Debian's package cannot connect on ppc64, so there is no regression risk. Check: sound audible and a second run; DXT5 path through libspeq (no `SPEQ` log line seen). Smoke-level only, not benchmarked. |

### 2.5 Desktop, system, boot

These share one source or one apt contract; promote together or not at all.

| Package | Candidate | Verdict | Evidence, gaps |
|---|---|---|---|
| `ps3-system` | 0.419 (as built) | NEEDS-CHECK | Contents since stable .160: `ps3-licensed` apt source (0.366), `ps3-kick` (10 ms kicker, `a46f930`), `ps3-gameos-mount` (`72b7be6`). The kicker as started by game mode and the GameOS mount (needs UFS_FS=m, in the unbooted kernel) have never run on the PS3. The bench-side `ps3-waker` kick is what rows 56-62 measured. Check: on the PS3, `ps3-game-kick` unit starts and stops with a game (`ps3-desktop/tests/kick-check.sh` is host-side only); `apt update` sees `ps3-licensed`. |
| `ps3-desktop`, `ps3-apps`, `ps3-games` | **0.406** (rebuild; built 0.376~dev) | NEEDS-CHECK | Game mode unloads the wallpaper, freezes services and brings the desktop back; one PS3 report exists for leaving (about 30 s, prompting the file prefetch of `a2e34fe`), the prefetch, the automatic `ps3-game-detect` and `gamemode-check.sh` are "not yet run on the PS3" (DESKTOP-PLAN.md, 2026-10-17). The Games menu entries per mode (`ebf96d4`) and the post-split `ps3-games` (`60a18e4`) are also unrun. `ps3-games` Depends on classicube, yamagi-quake2, neverball, xmoto, etr, dsda-doom, openttd and Debian's openarena/quakespasm/armagetronad/freedoom: promote those first. Check (user at the PS3): start a game from the menu and a fullscreen GL program without the menu, quit, time the desktop's return, `ps3-game-mode` journal has `left game mode`, the wallpaper and applets are back. |
| `ps3-boot` | 0.176 as built (rebuild gives 0.247) | NEEDS-CHECK | Stable 0.152 mishandles the `release`/`debug` flavour names (IMAGE-DIFF.md); 0.176 fixes it. No record of `/etc/kboot.conf` written by 0.176 on the PS3. Check: `ps3-update-kboot` on the PS3 gives the expected default entry; or build image and read it. |
| plymouth set, `ps3-splash`, `ps3-no-initramfs` | 26.134.222-1+ps3.182 / 0.182 (rebuild gives .247) | NEEDS-CHECK | The splash was written and fixed on 2026-10-16 with deploy fixes (`71d9264`); PLAN.md still lists splash timing and the DPMS fix as "not tested on hardware" for the .178 test boot, and no later entry confirms it. Check: the user reports one cold boot on the release kernel (splash shows from Petitboot to the desktop, no black gap, no text). |

### 2.6 Not promoted, and things to know

- `ps3rsx-modules-ps3-debug`, the debug kernel flavours, `*-dbgsym`: dev only (dbgsym goes to `ps3-dbgsym`).
- Old dev builds below the current ones are irrelevant (pruned after 5).
- The stall fix for games exists in three places, none in stable-ready form: the kernel local/0009 (.402+, not booted),
  the `ps3-kick` in ps3-system (not run through game mode), and bench-only `ps3-waker`. Games run today with the kicker
  started by hand, so stable users without it will see the 1+ s hitches (STALLS.md).
- Packages that must go together: kernel .178 + `ps3rsx-modules` 0.249+k.178; Mesa set + `ps3rsx-modules` 0.249
  (+ `libspeq0`/`ps3-spu-programs` for the SPE paths); `libspeq0` + `ps3-spu-programs` + `ps3-spu` + `libspeq-dev`;
  `ps3-desktop` + `ps3-apps` + `ps3-games` (one source) after `ps3-system` (>= 0.367~, which ps3-desktop Depends on);
  `luanti` + `luajit`; `ps3-games` after all its games; `ps3-games-licensed` (already in ps3-licensed) after `ps3-games`.
- Classicube engine/data split: engine in `ps3`, data stays at .366 in `ps3-licensed`.
- Debian packages the games need (openarena, quakespasm, armagetronad, freedoom, openttd data sets, mpv, ...) are
  not in this list; the `deps` component is a separate approval (`make deps`). The index of `ps3/deps` has quakespasm,
  armagetronad, freedoom, openttd-open*, mousepad, zathura, mpv; `openarena` was not found there and comes from
  live Debian ports.

## 3. Summary

- READY (15 items): kernel .178 + ps3rsx-modules 0.249; Mesa .415; libspeq/ps3-spu 0.395;
  ps3-gpu-tools 0.428; libsdl3; luajit; ioquake3; yamagi-quake2; dsda-doom; fluidsynth; extremetuxracer; neverball;
  luanti; cube2; openttd.
- NEEDS-CHECK: xmoto (run .361 once), classicube (the -4% and the split upgrade), supertuxkart (`lean` default, sound),
  teeworlds (sound), ps3-system / ps3-desktop+apps+games (game mode and kicker on the PS3), ps3-boot (kboot.conf),
  plymouth + ps3-splash (a cold boot).
- NOT YET: kernels .402, .419, .433 (and the source's .435) until the user's reboot session; ps3rsx-modules for them.
- SKIP: openal-soft.

## 4. Proposed commands (not run)

Nothing below is run by this file. Check that the PS3 is not in use and no bench session holds the lock
before anything touches it; publishing does not touch the PS3.

### 4.1 Rebuilds for the READY set (official versions, clean tree required)

```
git status --short                      # must print nothing; a dirty tree gives ~dev
make mesa-deb                           # 26.2.4-1+ps3.415, about 12-25 min under qemu
make ps3-spu-deb                        # libspeq0, libspeq-dev, ps3-spu, ps3-spu-programs 0.395
make ps3-gpu-tools-deb                  # 0.428
ls build/debs/*/*.deb | grep '~dev'     # lists the dirs still on dev builds: linux, ps3rsx-*, classicube, ps3-desktop
                                        # until their rebuilds; none of those may be in DEBS
```

No rebuild for libsdl3 254, luajit 247, ioquake3 396, yquake2 415, dsda-doom 361, fluidsynth 309, etr 324,
neverball 370, luanti 405, cube2 354, openttd 297: their `build/debs` files are official versions already.
The kernel .178 pair comes from `build/aptly/pool` (also official versions).

### 4.2 Stable publish of the READY set

Back up first (the user's rule; 5.1 GB, /home has 33 GB free):

```
cp -a build/aptly build/aptly-backup-$(date +%Y%m%d%H%M%S)    # remove after the publish is verified (ephemeral copy)
K=$(find build/aptly/pool \( -name '*_6.12.111-1+ps3.178_*.deb' -o -name '*_0.249+k6.12.111+ps3.178_ppc64.deb' \) \
      ! -name '*debug*' ! -name '*dbgsym*')
make publish DEBS="$K \
  $(ls build/debs/mesa/*.deb build/debs/ps3-spu/*.deb build/debs/ps3-gpu-tools/*.deb \
       build/debs/libsdl3/*.deb build/debs/luajit/*.deb build/debs/ioquake3/*.deb build/debs/yquake2/*.deb \
       build/debs/dsda-doom/*.deb build/debs/fluidsynth/*.deb build/debs/etr/*.deb build/debs/neverball/*.deb \
       build/debs/luanti/*.deb build/debs/cube2/*.deb build/debs/openttd/*.deb)"
```

The `$K` set is eight files: `linux-image-6.12.111+ps3.178`, `linux-headers-6.12.111+ps3.178`, its `-common`,
`linux-kbuild-6.12.111+ps3.178`, metas `linux-image-ps3` and `linux-headers-ps3`, `ps3rsx-modules-6.12.111+ps3.178`
and `ps3rsx-modules-ps3` (0.249). The dbgsym debs in those directories go to `ps3-dbgsym`, as `make publish`
does today; `aptly-publish.sh` refuses any `~dev` version and the check "versions only grow" runs before the add.
Afterwards verify by fetching `http://10.0.0.10:8070/ps3/dists/ps3/main/binary-ppc64/Packages` and counting the
new versions, then `make repo-snapshot` only if the user wants an image snapshot.

### 4.3 After the NEEDS-CHECK items pass (second publish, same pattern)

```
make classicube-deb                        # +ps3.374; publish only the engine deb
make ps3-desktop-deb                       # ps3-desktop, ps3-apps, ps3-games 0.406
make ps3-boot-deb ps3-splash-deb plymouth-deb   # 0.247 / 0.247 / +ps3.247, only if their checks pass
make publish DEBS="build/debs/classicube/classicube_1.3.8~git20261014.aa47929-1+ps3.374_ppc64.deb \
   build/debs/ps3-system/ps3-system_0.419_all.deb build/debs/ps3-desktop/*.deb \
   build/debs/xmoto/*.deb build/debs/supertuxkart/*.deb build/debs/teeworlds/*.deb \
   build/debs/ps3-boot/*.deb build/debs/ps3-splash/*.deb build/debs/plymouth/*.deb"
```

(Adjust the list to what passed; `ls` the directories first so the classicube path is exact.)

### 4.4 ps3-licensed

Nothing to publish: `classicube-data` .366, `sauerbraten-nonfree-data` .388, `ps3-games-licensed` .388,
`quake-shareware` 89, `quake2-demo-data` 89 are all in `ps3-licensed` already, and `build/debs` holds the same
versions. Do not publish `classicube-data` .374 (see classicube). If `ps3-games` changes its Depends again,
`make ps3-games-licensed-deb` and `make publish DEBS=build/debs/ps3-games-licensed/ps3-games-licensed_<N>_all.deb`
(goes to `ps3-licensed` whatever `DIST`). Once `ps3-games` is in stable, a PS3 can install
`ps3-games-licensed` without `-t ps3-dev`.

### 4.5 Do not publish

The `~dev` builds in `build/debs` (mesa 408~dev, classicube 367~dev, ps3-desktop 376~dev, linux and ps3rsx-modules
433~dev), openal-soft, and anything kernel newer than .178 until the reboot session.
