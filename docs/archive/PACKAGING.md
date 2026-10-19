> Superseded on 2026-10-19 by docs/PACKAGING.md; kept unchanged for its history.

# Packaging and the apt repository

How our software becomes Debian packages, how they reach the PS3, and the rules that keep it
safe. Decided 2026-10-16 (plan: Debian source packages, a served repository, the repository
organized by output package).

## Kinds of source packages

Each package is a top-level directory with a `package.conf` (read by `tools/deb-*.sh`):

| `TYPE` | What it is | Examples | Version |
|---|---|---|---|
| `debsrc` | Debian's source package at the snapshot, plus our quilt series (`<pkg>/patches`, added as `debian/patches/ps3/`) and changes to `debian/` itself (`<pkg>/packaging`) | `mesa`, `linux`, `plymouth`, and the games and libraries we patch (README) | `<debver>+ps3.N` |
| `native` | Our own code: the directory is the source (`debian/` inside) | `ps3-boot`, `ps3-gpu-tools`, `ps3-spu`, `ps3rsx`, `ps3-system`, `ps3-desktop`, `ps3-splash` | `0.N` (`ps3rsx`: `0.N+k<kernel release>`, its `-` as `.`) |
| `upstream` | Software Debian lacks: an upstream tarball plus component tarballs | `classicube` | `<upver>-1+ps3.N` |

A `debsrc` package from outside Debian's main sets `COMPONENT` (`yquake2`: `COMPONENT=contrib`, since
debian-ports builds nothing arch-specific from contrib). The deb builder's apt sources are main only;
`tools/deb-fetch.sh` (and `pkg-rebase.sh fetch`) add that component's deb-src lines inside the
throwaway container for the one fetch, so `docker/Dockerfile.deb` and the builder's tag stay as
they are. Build dependencies still come from main.

N is the commit count up to the last commit that touched the package's directory or the shared
packaging tools (`tools/deb-version.sh`), so versions only grow. A build from a tree with
uncommitted changes is a dev build, `N+1~dev<stamp>`: it sorts above the current release and
below the next one, and can only go to the dev suite.

`BUILD` in `package.conf` says where it builds:

- `native`: in a ppc64 chroot under qemu-user (`make chroot` makes `build/chroot/sid-ppc64.tar`;
  build dependencies are cached per `debian/control` as `build/chroot/<pkg>-deps-<hash>.tar`;
  ccache in `build/ccache`). Mesa: about 25 minutes cold, 12 warm.
- `indep`: architecture-independent packages, in the amd64 deb builder.
- `split`: `-A` in the deb builder (with `INDEP_PATH`, e.g. our spu-elf toolchain), `-B` in the chroot.
- `cross`: cross-built in the deb builder (`ps3rsx` against our kernel tree).

Commands: `make <pkg>-src` (fetch and prepare the source package in `build/src/<pkg>`),
`make <pkg>-deb` (build into `build/debs/<pkg>`), `make packages` (everything, the kernel first,
`ps3rsx-modules` last). `build/debs/*/*.deb` is what deploys, images, `make publish` (its default
`DEBS`) and `make checksums` take.

The old pipeline (`make kernel` from a kernel.org tarball, `make debs` into `build/repo` through
`tools/build-kernel.sh`, `build-debs.sh` and `stage-package.sh`, the `ps3-kernel-<release>`
packages and the transitional `ps3-mesa`) was removed on 2026-10-16; the Debian source packages
replace all of it. Its `KERNEL_*` settings went from `config.mk` with it.

## Changing patches

For the Debian-source packages (`mesa`, `linux`) the repository stores only patch series; you
change them in a disposable git tree:

    make mesa-work          # build/work/mesa: commit "Debian <version>" (tag base/<version>),
                            # then one commit per patch of ours (packaging first)
    cd build/work/mesa      # plain git: edit, commit, rebase -i, fixup, ...
    make mesa-patches       # writes the commits back to mesa/patches and mesa/packaging
    git diff -- mesa        # review; then make mesa-deb and test
    make mesa-rebase VERSION=<newer Debian version>   # our commits onto a new Debian release

- A commit that touches only `debian/` becomes a packaging patch (`<pkg>/packaging`), any other
  a source patch (`<pkg>/patches`); one touching both is refused (split it). The `Gbp-Pq: Topic`
  and `Gbp-Pq: Name` trailers keep subdirectories and file names (`linux/patches/ps3/`,
  `local/`); a new commit without them gets `NNNN-<subject>.patch`.
- Patches are git format-patch files without what changes on every export (commit ids,
  `index` lines, stats); a round trip (`-work`, then `-patches`) changes nothing, and the source
  package made from the series equals the work tree (both checked for Mesa and the kernel,
  2026-10-16). The series files' leading comments are kept.
- `make <pkg>-work` refuses to replace a tree with commits not exported yet (`FORCE=1` discards
  them); `make <pkg>-patches` refuses a tree that `-work` didn't finish.
- Debian's kernel tree has a `.gitignore` that ignores everything: in `build/work/linux`, files
  you add need `git add -f` (changes to existing files commit normally).
- `-rebase` stops at a conflict: resolve it in the work tree (`git status`, `git rebase
  --continue`), then `make <pkg>-patches` and set `VERSION`/`DSC_SHA256` in `package.conf` (it
  prints the hash). Without a conflict it does both itself.

## The repository

- Server: tibi-server, `http://10.0.0.10:8070/` (`tools/repo-server/`), static files from aptly's
  published tree. Two suites (decided with the user 2026-10-16, like Debian's unstable and
  experimental), each in its own prefix and pool so pruning one can't touch the other:
  - `ps3/`, suite `ps3`: stable releases (components `main` and `deps`), kept forever, versions
    only grow, `~dev` versions refused;
  - `ps3-dev/`, suite `ps3-dev`: dev builds with their debug symbols, the newest 5 versions of
    each package kept (`KEEP`), older ones pruned on every dev publish;
  - `ps3-dbgsym/`: debug symbols of stable releases, also the newest 5;
  - `ps3-licensed/`, suite `ps3-licensed` (component `main`; since 2026-10-17): licensed game
    content we may not redistribute, see below.
- Licensed packages: a package is licensed when its Debian Section is `non-free/*` or `local/*`
  (game-data-packager marks non-redistributable data `local/`) or its name is in
  `tools/licensed-packages.txt` (the list catches one whose Section doesn't say so). Now:
  `classicube-data` (Mojang's textures and audio), `quake-shareware`, `quake2-demo-data`,
  `sauerbraten-nonfree-data` (Sauerbraten models and sounds Debian's cube2-data lacks).
  `tools/aptly-publish.sh` sends them to `ps3-licensed` whatever `DIST` is, never into `ps3` or
  `ps3-dev` (and removes any it finds in `ps3-dev`), so licensed files never sit in the `ps3`
  pool. `ps3-licensed` is kept like the stable suite: versions only grow, nothing is removed, the
  sync to the server is add-only; it takes no `~dev` builds (a dev publish leaves one out with a
  note: commit, rebuild). It has its own prefix, so an access restriction can be added on the
  server later (`tools/repo-server/nginx.conf`); now it's served like the others, LAN-only.
- What was published before the split stays: the `ps3` suite's classicube versions with the
  Mojang files inside (`0+git.aa47929+ps3.144`, `1.3.8~git20261014.aa47929-1+ps3.152`), its
  `quake-shareware`/`quake2-demo-data` 89 and the `img-*` snapshots holding them (the user's
  decision, 2026-10-17: the repository is LAN-only and nothing was distributed). If `ps3` is ever
  made public, purge those first. The dev suite's copies were removed (2026-10-17).
- Promotion: commit, rebuild (versions `0.N`, `...+ps3.N`, no `~dev`), `make publish`. The builds
  are reproducible, so apart from the version the contents are what was tested from `ps3-dev`.
- `make publish [DEBS=...] [DIST=dev]` (`tools/repo-publish.sh`, `tools/aptly-publish.sh`): staging
  copies, licensed packages set apart for `ps3-licensed`, a version check (official versions only
  grow, `ps3` and `ps3-licensed`), aptly add and publish, an add-only sync of `ps3/` and
  `ps3-licensed/` to the server that refuses to change any published file, indices last, then
  the aptly database backed up to the server. `build/aptly` is the one part of `build/` that is never deleted.
- `make deps` refreshes the `deps` component: Debian packages from the snapshot listed in
  `image/deps.txt` (ones ours newly depend on, ps3-apps' applications, and Debian's Mesa set for
  rollback), with their dependencies (the PS3 sees only our repository: an application's libraries
  the base layer lacks must be there too). 440 packages on 2026-10-16, after ps3-apps.
- Debug symbols (`*-dbgsym`) go to `ps3-dbgsym` only, never into deploys or images.

## On the PS3

`ps3-system` ships the apt configuration (`ps3-system/files/apt/`):

- Debian ports, live (`deb.debian.org/debian-ports`, unstable and unreleased): anything we don't
  build installs and upgrades from Debian.
- Our repository, `Trusted: yes` on the LAN: `ps3`, `ps3-dev` and `ps3-licensed`.
- Pins (`/etc/apt/preferences.d/ps3`): the stable suite at 990, so our releases win over Debian's
  (500) whatever the versions, but an installed newer dev build is never downgraded (below 1000):
  the next release, a higher version, takes over from it. `ps3-licensed` at 990 as well (it has
  no dev builds: a dev build of the engine takes its data from there). The dev suite at 1: never chosen on its
  own, only with `apt install -t ps3-dev <pkg>` (what `make deploy` does). Going back to an older
  version is explicit (`apt install pkg=<version>`). Debian's `linux-image-*`, `linux-headers-*`, `linux-kbuild-*`, `linux-support-*`
  and `src:mesa` at -1, so they are never candidates. `apt upgrade` is safe: a Debian package
  that needs a newer Mesa than ours is kept back, nothing is removed. Verified in the ppc64 chroot
  against live debian-ports (2026-10-16). The same version in `deps` and Debian counts as one, so
  the -1 also blocks `deps`' copy of Debian's Mesa: a rollback to Debian's Mesa means dropping
  the pin by hand.
- New kernels arrive through meta packages (`linux-image-ps3` and `ps3rsx-modules-ps3`, the
  `-debug` ones for the debug kernel).

## Deploying and the image

- `make deploy [PKGS=...] [AUTOREMOVE=1]` (`tools/deploy.sh`): publishes every local build
  (debug symbols included) to `ps3-dev`, refreshes only our lists on the PS3 (Debian's are ~100
  MB), then with `-t ps3-dev`: upgrades every installed package of ours that has a newer build,
  installs the named top-level packages, upgrades again; each step simulated first;
  `--no-remove`, except removals named explicitly as `name-` in `PKGS` (and then exactly those).
  Kernels and ps3rsx modules (`linux-image*`, `linux-headers*`, `linux-kbuild*`,
  `ps3rsx-modules*`) are upgraded only with `KERNELS=1` or when named in `PKGS` (since
  2026-10-16, df50edd: a deploy of two desktop packages installed an unfinished kernel build left
  in `build/debs`, without its ps3rsx modules, and that kernel had no graphics). `DRY_RUN=1`
  publishes to `ps3-dev` and runs only the simulations. A kernel install is
  `make deploy KERNELS=1 PKGS="linux-image-ps3 ps3rsx-modules-ps3"`; leave the debug kernel out
  unless it's wanted (one kernel unpacked at a time).
  It brings its own copy of the sources and pins, so it works before the PS3 has them; licensed
  builds land in `ps3-licensed` (release versions only) and upgrade from there. Runs
  `ps3-migrate-home` once.
- The image (`tools/build-image.sh`): the Debian-only base layer, then our packages installed by
  apt from a flat repository of our debs (`image/packages-ours.txt`; the licensed ones are in
  `build/debs` like the rest, `classicube` pulls in `classicube-data`), our Mesa replacing Debian's
  as an upgrade, then `autoremove --purge` (LLVM, z3, sensors go), then `image/customize.sh`
  (hostname, fstab, user, key, locale).

## Kernels

- `make linux-deb`: Debian's `linux` source with `linux/patches` and `linux/packaging`, the full
  configs from `tools/linux-config.sh` (`ps3_defconfig` + `linux/config/ps3.config`, plus
  `ps3-release.config` for the release kernel), cross-built for ppc64, then `linux-kbuild`
  natively. Two kernels (decided 2026-10-16):
  - release: release `6.12.111+ps3.N`, packages `linux-image-6.12.111+ps3.N` and
    `linux-headers-6.12.111+ps3.N`, metas `linux-image-ps3`, `linux-headers-ps3`;
  - debug: release `6.12.111+ps3.N-debug<stamp>`, the stamp (`YYYYMMDDHHMM`, UTC) being the date
    of the commit the version stands for (`tools/deb-version.sh`; a dirty tree: the build time),
    so the same commit always gives the same release and every debug build installs next to
    the others; metas `linux-image-ps3-debug`, `linux-headers-ps3-debug`.

  Debian's flavours are `ps3` and `ps3-debug` (their names make the metas); a flavour's
  `localversion` in `defines.toml` replaces Debian's `-<flavour>` suffix of the release
  (`linux/packaging/0001-ps3-flavours.patch`, the stamp filled in by `tools/deb-prepare.sh`).
  Each image ships its kernel arguments as `/usr/lib/ps3/boot/<release>.args`
  (`linux/flavours/release.args`, `debug.args`).
- `make ps3rsx-modules`: the RSX driver for both kernels, built natively against their headers
  packages: `ps3rsx-modules-<release>`, metas `ps3rsx-modules-ps3` (release) and
  `ps3rsx-modules-ps3-debug`.
- Before installing a new kernel: `make netboot-deb FLAVOUR=release|debug ROOT=nfs|usb` boots
  it from Petitboot without touching the disk, with the arguments its package ships.
- The boot menu: `ps3-boot` writes `/etc/kboot.conf` from the kernel hooks; the default is the
  newest kernel unless `/etc/default/ps3-boot` sets `PS3_DEFAULT_FLAVOUR` (`release`: the newest
  kernel without a `-debug` suffix; `debug`: the newest `-debug*` one; the old names `ps3` and
  `ps3-release` still mean debug and release) or `PS3_DEFAULT` (a release, wins while it is
  installed). The image sets `PS3_DEFAULT_FLAVOUR=release`. Entries that aren't a kernel's (the SSD
  root's, docs/GAMEOS-HDD.md "Phase B") go in `/etc/kboot.d/*.conf`: ps3-update-kboot (0.498 on)
  appends them as written, leaving out one whose label is taken or whose image or `initrd=` is
  missing. The SSD root's initramfs is our own (`make initrd-ssd`), not initramfs-tools: kernels still
  boot without one (`ps3-no-initramfs`), only that entry passes `initrd=`.
  **Caveat (2026-10-18, from the installed Petitboot's code, docs/GAMEOS-HDD.md "Petitboot's
  behaviour"):** this Petitboot ignores `default=`, `timeout` and `delay` in kboot.conf. Its default is
  the entry whose label matches the last one booted by hand (a hash kept in its OS-area settings), its
  timeout comes from `ps3-bl-option`. The entry order still matters for the list; to be confirmed at
  the TV.
- Kernel and Mesa installs on the running PS3 stop the desktop first (`make deploy` does it):
  two kernels unpacked next to the desktop stalled the PS3 in swap on 2026-10-16 (power cycle).

## The boot splash

Walnut Linux's splash is Plymouth with our theme, made to show early from the USB disk and to stay
up until X draws (2026-10-16):

- `plymouth/`: Debian's `plymouth` 26.134.222-1 plus one patch,
  `0001-script-no-console-viewer.patch`. The script plugin's first full redraw called
  `ply_console_viewer_hide(NULL)` on a display without a console viewer, which is what you get
  without text rendering (no label plugin) or with `plymouth.prefer-fbcon`: the daemon died with
  SIGSEGV as the theme started. With the patch the theme needs no text rendering at all, so
  `plymouth-label` isn't installed (with it, loading Pango, Cairo and fonts took 10 to 11 s at
  boot from the USB disk).
- `ps3-splash` (needs our Plymouth, `>= 26.134.222-1+ps3`): the `walnut` script theme (the logo,
  a progress bar from small images, the walnut rotated live as it rolls). Its preinst diverts
  Plymouth's DRM renderer (`renderers/drm.so`): it can't drive ps3rsx at boot ("Could not
  initialize heads") and cost seconds of failed attempts before the framebuffer fallback (ps3fb).
  0.180's diversion of `label-pango.so` is removed on upgrade. The postinst sets the theme and
  `DeviceTimeout=1` in `plymouthd.conf` (with udev Plymouth waits 8 s for a DRM device before it
  takes the framebuffer; `plymouth.ignore-udev` instead showed nothing, since without udev it
  hands the framebuffer renderer the DRM device path). Two drop-ins: `plymouth-start` runs with
  `XKB_CONFIG_ROOT=/nonexistent` (Plymouth compiled an XKB keymap for typed input, 3 s from the
  busy disk, and nothing here types at boot), `plymouth-quit` runs `plymouth quit --retain-splash`.
- From the splash to the desktop: the autologin getty on tty1 (`ps3-desktop`'s generator) has
  `TTYReset=no`, `TTYVHangup=no`, `TTYVTDisallocate=no`, so it leaves tty1 in graphics mode with
  the splash on it (the reset switched back to text and showed the login for the seconds X takes
  to start); the login shell switches tty1 back to text whenever no X runs on it. `ps3-session`
  puts the splash's last frame (`ps3-splash` renders it, padded so `hsetroot -center` lands the
  logo where Plymouth drew it) on X's root window and drops it once the desktop draws, freeing
  its 8 MB pixmap.
- Only kernels whose arguments contain `splash` show it: the release kernel
  (`linux/flavours/release.args`: `quiet splash`), not the debug one.
- Debugging it without text on tty1: `plymouth.debug=file:/var/log/plymouth-debug.log
  plymouth.debug=stream:/dev/null` on the kernel line. `file:` alone still printed the debug
  text on tty1; the second option sends the stream to `/dev/null`. For a test boot, append both
  to the one line of `/usr/lib/ps3/boot/<release>.args` and run `sudo ps3-update-kboot` (the
  file belongs to the kernel package, so the next upgrade of that package drops it again).

## Game data from game-data-packager

`make quake-data` (`tools/game-data.sh`, in the games builder with game-data-packager 89 from
contrib and lhasa) writes `quake-shareware_89_all.deb` (Quake's shareware `id1/pak0.pak`, demos
included, Section `non-free/games`) and `quake2-demo-data_89_all.deb` (Quake II's demo `baseq2`,
`demos/q2demo1.dm2` in its pak, Section `local/games`) into `build/debs/quake-data`, downloading
the shareware archives from id's mirrors (kept in `build/downloads/game-data`). Deploys, images
and `make publish` take them like any build. The data is licensed (Quake II's demo may not be
redistributed at all): `make publish` puts them in the `ps3-licensed` suite only, with either
`DIST`, like ClassiCube's Mojang data.

game-data-packager's output isn't reproducible (the files' dates are the build time), and the kept
suites never take a different file under a published version. So once a version is published,
`make quake-data` downloads that file from the repository (`ps3-licensed` first, then `ps3`, where
version 89 went before the split; checked against the suite's SHA256) instead of making it again;
`REGEN=1` makes it anyway and refuses when the version it gets is already published. A new file
only goes out under a new version (a newer game-data-packager).

### ClassiCube's data

The `classicube` source package builds two packages (since 2026-10-17, `+ps3.366`): `classicube`
(the engine, ppc64, free) and `classicube-data` (all, Section `local/games`, so `ps3-licensed`):
the texture pack and the audio, both Mojang's. The texture pack, `static.classicube.net/default.zip`,
holds the files ClassiCube's launcher otherwise extracts from Mojang's Minecraft Classic 0.30 and
1.6.2 client jars (`terrain.png`, `char.png`, the mob skins..., the "classic jar files" in the
pinned `src/Resources.c`) with ClassiCube's patches; the sounds and music come from Mojang's
resource server (`classicube/sounds.sh`). Same paths as before (`/usr/lib/classicube/texpacks`,
`audio/`). `classicube` does not depend on `classicube-data` (only `Suggests`: free packages never
depend on licensed ones, see "The rule" below); `classicube-data` has
`Breaks`/`Replaces: classicube (<< 1.3.8~git20261014.aa47929-1+ps3.366~)`, the upgrade from the
bundled package. Each release of the source package also publishes a new `classicube-data` (15 MB) to
`ps3-licensed`, kept forever like the bundled versions in `ps3` were. On a PS3 with the bundled
package, the split needs ps3-system 0.366 or later first (the `ps3-licensed` source), then
classicube: `make deploy PKGS="ps3-system classicube"` does both (it brings the new sources
along); by hand, `sudo apt install -t ps3-dev ps3-system`, `sudo apt update`,
`sudo apt install -t ps3-dev classicube` (classicube-data comes from `ps3-licensed`). Checked
with dpkg, 2026-10-17: the bundled 330 upgrades to the pair in any unpack order, the files then
belong to classicube-data.

Without the data the engine still works: `/usr/games/classicube` runs it from
`~/.local/share/classicube`, and ClassiCube's launcher (the pinned source's `Resources_CheckExistence`)
finds `texpacks/classicube.zip` missing, asks to fetch it and the sounds and music (the texture pack
from `static.classicube.net`, the audio from Mojang's resource server; BearSSL is built in), and
writes them under that directory, nothing outside it. Our patches leave this alone. The steps for a
user, or for copying the files by hand, are in `/usr/share/doc/classicube/README.Debian`
(`classicube/debian/classicube.README.Debian`).

### Sauerbraten's models and sounds (`sauerbraten-nonfree-data`)

Debian's `cube2-data` 1.3-1 is not a DFSG-cleaned Sauerbraten: it is the BananaBread content
(gitlab.com/cube2-data/cube2-data, 295 MB, 989 files under `packages/`, installed in
`/usr/share/games/cube2`, which its `/usr/games/cube2` passes to the engine as `-k`). The engine
(cube2 0.0.20201229, Sauerbraten's `fpsgame`) loads models and sounds by names compiled in
(`entities.cpp`, `render.cpp`, `weapon.cpp`, `monster.cpp`) or from `data/sounds.cfg`, and BananaBread
lacks most of them, hence "could not load model" for the pickups, hud guns, gibs and so on and
"failed to load sample". The match is the SourceForge release `2020_12_29`
(`sauerbraten_2020_12_29_linux.tar.bz2`, 985,823,440 bytes, MD5 `b50d018f...` as SourceForge lists
it, SHA256 `cdba7c4a...`, both in `config.mk`; kept in `build/downloads/game-data`).

Of the 8,914 files under that release's `packages/`, 8,780 are not in cube2-data (the maps' textures,
map models, skyboxes, music; cube2-data's own maps don't use them). Package only what the game
code and `sounds.cfg` need: `sauerbraten-nonfree-data/files.txt`, 912 files, 149 MB unpacked: the
models `ammo health boost quad quadrings quadspheres teleporter carrot checkpoint hudguns vwep
projectiles gibs debris barreldebris base skull flags armor` and the five player models (`mrfixit
snoutx10k ogro2 inky captaincannon`, with their `armor/` and `hudguns/`), `ogro` and `monster`, plus
every sound cube2-data lacks. Files cube2-data has are not repeated (a few differ in the release,
e.g. `models/debris/md2.cfg`, `models/armor/armor.txt`: cube2-data's win), so the package installs
next to cube2-data without a conflict. Not packaged: the other 7,868 files (mapmodels, the Sauerbraten maps'
textures and so on); add them to `files.txt` if Sauerbraten's own maps are ever wanted.

Why they are left out of Debian: the licenses are not DFSG-free. By directory, from the license
and readme files kept in the package: `ammo boost health flags gibs debris barreldebris` (makkE)
CC BY-NC-SA; `hudguns vwep mrfixit inky captaincannon` (geartrooper and others) CC BY-NC 3.0;
`snoutx10k` CC BY 3.0 and `skull` CC BY-SA 3.0 (free, but in the same release); `quad` by DCP,
converted by makkE; `teleporter` (Todretter) "all rights reserved so far"; `ogro`/`ogro2` carry the
1998 Quake II player model's own readme; the sounds are mixed (`ambience`: CC Sampling+; `q009`
CC BY-SA; `free`/`awesund` modified by Darthvim, others per their README). Personal and LAN use
only, so `ps3-licensed`. Section `local/games`, version `0.0.20201229-1+ps3.N`, arch all.
`cube2` and `cube2-server` don't depend on it (cube2 isn't rebuilt for a `Suggests`); the package
`Depends: cube2-data`, and `ps3-games-licensed` depends on it.

The source tarball is made by `tools/sauerbraten-data.sh` (`make sauerbraten-nonfree-data-src`:
the release downloaded once and checked, the listed files with a fixed owner and mtime, `gzip -n`),
the deb with `make sauerbraten-nonfree-data-deb` (indep builder, no qemu); the same files give the
same deb for a given commit. As for the Quake data, once a version is published, never build it again
under that version: take the published file.

### The rule: free packages never depend on licensed ones

Every free package (the engines, `ps3-games`, `ps3-desktop`, ...) must be installable and
distributable on its own, so none of them has a `Depends` or `Recommends` on a licensed package
(`tools/licensed-packages.txt`, or a Section `non-free/*` or `local/*`); `Suggests` is fine. The
user puts the data in the game directory (each game's docs say where and how to get it). Our own
machines get the data from the `ps3-licensed` suite through the metapackage `ps3-games-licensed`
(`ps3-games-licensed/`, Section `local/games`, so `ps3-licensed` only): it depends on `ps3-games`,
`classicube-data`, `quake-shareware`, `quake2-demo-data` and `sauerbraten-nonfree-data`, so `sudo apt install ps3-games-licensed`
gives the whole suite with its data. Personal and LAN use only. A new licensed game data package
goes into `tools/licensed-packages.txt` and into that metapackage's `Depends`.

## Image snapshots

`make repo-snapshot` freezes the stable suite as `img-<stamp>` (published next to `ps3`, kept
forever), and `ps3-licensed` under the same name in its own prefix; `make rootfs
REPO_SUITE=img-<stamp>` builds the image from them instead of the local build outputs (the
licensed snapshot only when it exists: older ones have their licensed packages in `ps3`). Checked 2026-10-16: the image built both ways is byte-identical.

## Migration from the old image scripts

`ps3-system` and `ps3-desktop` took over the files `image/customize.sh` and
`image/desktop-setup.sh` used to write. Their preinsts archive each legacy file that no package
owns to `/var/backups/ps3-legacy-{system,desktop}-<date>.tar` and remove it (and its enable
links); `ps3-migrate-home` deletes home-directory copies identical to the packaged defaults and
moves the rest to `~/.local/share/ps3-legacy/`. Done on the PS3 on 2026-10-16.
