# Packaging and the apt repository

This is how our software becomes Debian packages, how those packages reach the PS3, and the rules that keep that safe. Everything we ship is a `.deb` built from a source package, served from walnut.nanobyte.dev's apt repository (published by GitHub Actions; licensed data from this PC to a password-protected suite), and installed on the console with apt. The base system, the compilers and the build images are described first, then the package kinds, the repository, deploys, kernels and the boot splash.

For terms see the [glossary](README.md#glossary). What could go from the dev suite to stable is in [RELEASE-CANDIDATES.md](RELEASE-CANDIDATES.md); a fresh image against the live system is in [IMAGE-DIFF.md](IMAGE-DIFF.md).

## Base and toolchain

Decided 2026-10-15, with the packaging plan of 2026-10-16.

**Base.** The base is a pinned Debian sid snapshot (`DEBIAN_SNAPSHOT` in `config.mk`, now `20261013T000000Z`, taken from snapshot.debian.org's debian-ports archive). Debian has no stable release for ppc64 (big-endian), only debian-ports unstable, so a frozen snapshot is the closest thing to an LTS. It moves on purpose, at the latest when Debian 14 is released, and never piecemeal.

**One compiler version everywhere: GCC 16.2,** the snapshot's system compiler.

- PPE side (kernel, Mesa, ps3rsx, games): Debian's `gcc-16-powerpc64-linux-gnu` 16.2.0-3cross1, from the amd64 sid snapshot of the same date.
- SPU side: our port of the SPU backend to GCC 16.2 (`packages/patched/toolchain/spu/gcc/`, see [SPU-GCC.md](SPU-GCC.md)).
- binutils 2.47 for the SPU, with one gas patch (`packages/patched/toolchain/spu/binutils/`): gas took `nop 127` for the `.nop` directive, which broke branch hints (the `hbrr` regression).
- newlib 4.6.0 for the SPU, with seven patches (`packages/patched/toolchain/spu/newlib-4.6/`).
- On the SPEs (2026-10-15) 1629 of 1677 execute tests pass, the same tests one by one as with binutils 2.33.1 and newlib 1.20.
- A PPE miscompile to know about (2026-10-20): GCC 16.2's loop vectorizer at `-O3` with `-mcpu=cell` (AltiVec without VSX; `-mcpu=970` and `-mcpu=power6 -maltivec` too, `-O2` not) gets stb_image_resize's 4-channel horizontal downsampling loop wrong, and the result is near-black, transparent images. It needs `-DNDEBUG`, because the asserts inside the loop stop the vectorizer. Warzone 2100 is fixed (patch 0007, [games/smoke-tested.md](games/smoke-tested.md)). Any other package that builds stb_image_resize, or other float accumulation loops like it, at `-O3 -mcpu=cell` can hit the same bug (most CMake Release builds use `-O3`). A minimal C reproducer is still missing, so nothing has gone upstream yet.

**Kernel.** 6.12 LTS (supported to December 2028), from Debian's `linux` source (trixie, 6.12.111-1) with our patches. Two flavours, named 2026-10-16:

- the release kernel `6.12.111+ps3.N`: 8.7 MB more `MemTotal` on the PS3 than the debug one (222704 kB against 213992 kB; vmlinux 11.9 MB against 21.2 MB, task #14);
- debug kernels `6.12.111+ps3.N-debug<stamp>`, each build its own release so they sit side by side.

**Mesa.** Debian's source package of the snapshot's release, rebuilt with our patches (`packages/patched/graphics/mesa/`, version `26.2.4-1+ps3.N`).

**Walnut Linux** is the distribution's name and look (decided 2026-10-16). The logo is a stand-in after the owner's reference (`packages/walnut/system/ps3-splash/art/make-logos.py`; a better one before any release). The walnut is the kernel's boot logo, there is a Plymouth splash (the logo and a progress bar with the walnut rolling along it, on the desktop's blue), and the wallpapers are the owner's photos of the walnut tree (CC BY-NC 4.0). The splash is under "The boot splash" below.

**Build images.** Everything builds in pinned Docker images under `build-containers/`:

- `Dockerfile`: the toolchain and mmdebstrap (kernel and rootfs, under qemu-user);
- `Dockerfile.deb`: Debian's packaging tools (the deb builder; aptly is still in it, unused since the LAN repository went);
- `Dockerfile.games`: ClassiCube's sound conversion and game-data-packager;
- `Dockerfile.spu`: the spu-elf toolchain.

The builder image is pinned by digest (`BUILDER_BASE`) with apt on the same snapshot. Every build (images, chroots, the image, the few direct downloads) fetches the snapshot through walnut.nanobyte.dev's cache of it, `SNAPSHOT_URL` in `config.mk` (`walnut-aptcache`, servers/walnut.nanobyte.dev/aptcache): snapshot.debian.org throttles bulk downloads with `503 TooManyRequests`, and a pinned snapshot never changes, so each file is fetched from it once. `SNAPSHOT_URL=http://snapshot.debian.org` bypasses the cache. Outputs go to `build/`, which is ephemeral except `build/aptly`. SPU toolchain experiments build side by side (`SPU_OUT=build/tmp/spu-<name>`). The tree is organised by package kind (docs/LAYOUT.md).

## Kinds of source packages

Each package is a directory with a `package.conf` under `packages/`: `walnut/<area>/` (ours), `patched/<area>/` (Debian's or upstream's with our patches), `repackaged/` (upstream releases as they are) or `licensed/` (docs/LAYOUT.md). It is named by its directory alone (`make xmoto-deb`, `build/debs/xmoto`); `tools/pkg-dir.sh <name>` finds the directory for `tools/deb-*.sh` and `tools/pkg-*.sh`.

| `TYPE` | What it is | Examples | Version |
|---|---|---|---|
| `debsrc` | Debian's source package at the snapshot, plus our quilt series (`<pkg>/patches`, added as `debian/patches/ps3/`) and changes to `debian/` itself (`<pkg>/packaging`) | `mesa`, `linux`, `plymouth`, the games and libraries we patch (README.md) | `<debver>+ps3.N` |
| `native` | Our own code: the directory is the source, `debian/` inside | `ps3-boot`, `ps3-gpu-tools`, `ps3-spu`, `ps3rsx`, `ps3-system`, `ps3-desktop`, `ps3-splash` | `0.N` (`ps3rsx`: `0.N+k<kernel release>`, its `-` as `.`) |
| `upstream` | Software Debian lacks: an upstream tarball plus component tarballs | `classicube`, `zlib-ng`, `warzone2100` | `<upver>-1+ps3.N` |

A `debsrc` package from outside Debian's main sets `COMPONENT`. `yquake2` uses `COMPONENT=contrib`, since debian-ports builds nothing arch-specific from contrib. The deb builder's apt sources are main only, so `tools/deb-fetch.sh` (and `pkg-rebase.sh fetch`) add that component's deb-src lines inside the throwaway container for the one fetch; `build-containers/Dockerfile.deb` and the builder's tag stay as they are. Build dependencies still come from main.

**Versions.** N is the commit count up to the last commit that touched the package's directory or the shared packaging tools (`tools/deb-version.sh`), so versions only grow. A build from a tree with uncommitted changes is a dev build, `N+1~dev<stamp>`: it sorts above the current release and below the next one, and only `make deploy` installs it (no suite takes one). A consequence: any commit to `tools/deb-*.sh` or `build-containers/Dockerfile.deb` shifts the N of every package.

**Where it builds.** `BUILD` in `package.conf` says:

- `native`: in a ppc64 chroot under qemu-user. `make chroot` makes `build/chroot/sid-ppc64.tar`; build dependencies are cached per `debian/control` as `build/chroot/<pkg>-deps-<hash>.tar`; ccache is in `build/ccache`. Before each build the chroot's ccache links (`/usr/lib/ccache/gcc`, `cc`, ...) are refreshed with `update-ccache-symlinks` and listed in the log; `cc` is the compiler a plain Makefile's `$(CC)` defaults to, and without its link such a package compiles cold on every run. Mesa takes about 25 minutes cold, 12 warm.
- `indep`: architecture-independent packages, in the amd64 deb builder.
- `split`: `-A` in the deb builder (with `INDEP_PATH`, e.g. our spu-elf toolchain), `-B` in the chroot.
- `cross`: cross-built in the deb builder (`ps3rsx` against our kernel tree).

**Commands.** `make <pkg>-src` fetches and prepares the source package in `build/src/<pkg>`. `make <pkg>-deb` builds into `build/debs/<pkg>`. `make packages` builds everything, the kernel first and `ps3rsx-modules` last. `build/debs/*/*.deb` is what deploys, images and `make checksums` take; `make publish` takes the licensed ones.

The old pipeline (`make kernel` from a kernel.org tarball, `make debs` into `build/repo`, the `ps3-kernel-<release>` packages, the transitional `ps3-mesa`, `config.mk`'s `KERNEL_*`) was removed on 2026-10-16. Debian source packages replace all of it.

## Changing patches

For the Debian-source packages (`mesa`, `linux`) the repository stores only patch series. You change them in a disposable git tree:

    make mesa-work          # build/work/mesa: commit "Debian <version>" (tag base/<version>),
                            # then one commit per patch of ours (packaging first)
    cd build/work/mesa      # plain git: edit, commit, rebase -i, fixup, ...
    make mesa-patches       # writes the commits back to packages/patched/graphics/mesa/patches and packages/patched/graphics/mesa/packaging
    git diff -- mesa        # review; then make mesa-deb and test
    make mesa-rebase VERSION=<newer Debian version>   # our commits onto a new Debian release

- A commit that touches only `debian/` becomes a packaging patch (`<pkg>/packaging`), any other a source patch (`<pkg>/patches`). One touching both is refused: split it. The `Gbp-Pq: Topic` and `Gbp-Pq: Name` trailers keep subdirectories and file names (`packages/patched/kernel/linux/patches/ps3/`, `local/`); a new commit without them gets `NNNN-<subject>.patch`.
- Patches are git format-patch files without what changes on every export (commit ids, `index` lines, stats). A round trip (`-work`, then `-patches`) changes nothing, and the source package made from the series equals the work tree (both checked for Mesa and the kernel, 2026-10-16). The series files' leading comments are kept.
- `make <pkg>-work` refuses to replace a tree with commits not exported yet (`FORCE=1` discards them). `make <pkg>-patches` refuses a tree that `-work` didn't finish. A work tree with unexported commits is unsaved source: never discard it.
- Debian's kernel tree has a `.gitignore` that ignores everything. In `build/work/linux`, files you add need `git add -f`; changes to existing files commit normally.
- `-rebase` stops at a conflict. Resolve it in the work tree (`git status`, `git rebase --continue`), then run `make <pkg>-patches` and set `VERSION` and `DSC_SHA256` in `package.conf` (it prints the hash). Without a conflict it does both itself.

## The repository

Everything is served from walnut.nanobyte.dev (servers/walnut.nanobyte.dev/, docs/WALNUT-SERVER.md): the public suites under `/apt`, built and published by GitHub Actions, and the password-protected `licensed` suite under `/licensed`, published from this PC. No suite has a database: `apt-repo/walnut-repo.py` builds a suite's next state from its published, signed one, and walnut-watch on the server checks every upload before it switches the suite over.

| Suite | Holds | Signed by | Kept | Published by |
|---|---|---|---|---|
| `testing` | every push's build: release and debug kernels, their modules, everything else, sources | the CI key | newest 5 source versions | CI, on every push touching the package |
| `testing-debug` | testing's `-dbgsym` | the CI key | newest 5 | CI, with testing |
| `stable` | builds made for stable by hand, without the debug kernel and its modules | the archive key's signing subkey | every version | CI, "Run workflow" with the owner's approval |
| `stable-debug` | stable's `-dbgsym` | the archive key | newest 3 | CI, with stable |
| `licensed` | game data we may not redistribute, and the sources it is made from | the archive key | every version, files never change | `make publish`, from this PC only |

The old LAN repository on tibi-server (`http://10.0.0.10:8070`: suites `ps3`, `ps3-dev`, `ps3-dbgsym`, `ps3-licensed` and the `img-*` snapshots, published with aptly from `build/aptly`) was retired and deleted on 2026-10-26. Its whole tree and `build/aptly` are kept in `/mnt/data_linux/Temp/ps3/retired-tibi-server-20261026/` (tarballs with SHA256SUMS; `ps3-licensed-debs/` holds the ten licensed packages it published, ready for `make publish` if they should go to the licensed suite unchanged).

### Walnut's suites on GitHub Actions

- Every package except the licensed ones has `.github/workflows/pkg-<name>.yml` (from `github-ci/gen-workflows.py`; run it after adding, moving or removing a package). A push touching what a package's version counts builds it and publishes it to testing. "Run workflow" builds it fresh for the suite picked there, testing or stable; a stable publish waits for the owner's approval after the build.
- Symbols go to `<suite>-debug` in the same upload, so both switch together. A console gets them with one more line, e.g. `deb https://walnut.nanobyte.dev/apt testing-debug main`.
- Each suite has its own pool (`pool/<suite>/main/...`): testing and stable are separate builds of one version.
- Each upload names the InRelease it was built on; walnut-watch refuses a stale one and `github-ci/publish.sh` makes it again. It deletes pool files no index names any more, 14 days after they were first seen unnamed (never in `licensed`).
- **When to publish to stable.** Only after the package is proven on the PS3, with the user's OK. Never a `~dev` version (CI builds from commits only).

### Licensed packages and the licensed suite

A package is licensed when its Debian Section is `non-free/*` or `local/*` (game-data-packager marks non-redistributable data `local/`), or when its name is in `tools/licensed-packages.txt` (which catches one whose Section doesn't say so). Today that is `classicube-data` (Mojang's textures and audio), `quake-shareware`, `quake2-demo-data`, `sauerbraten-nonfree-data` (Sauerbraten models and sounds Debian's cube2-data lacks), `sauerbraten-data` (the real Sauerbraten's whole data, launched by the public `sauerbraten` package on our `cube2` engine: docs/games/sauerbraten.md), `rct2-data`, `tyrian-data` and the metapackage `ps3-games-licensed`. Their source packages live in `packages/licensed/`; the others are made by tools from files the owner has (`make quake-data`, `tools/rct2-data.sh`, `tools/tyrian-data.sh`).

- **Built here only.** CI never builds them: they have no workflow (`gen-workflows.py` skips `packages/licensed/` and the list), and `github-ci/build-package.sh` refuses them. GitHub has no game data.
- **Never public.** The public GitHub copy leaves `packages/licensed/` out of every commit, and any `packages/**/<name>/` named in the list as a second check (`tools/public-export.sh`). `tools/leak-check.py` keeps licensed files out of every free package and source CI publishes.
- **`make publish [DEBS=...] [DRY_RUN=1]`** (`apt-repo/licensed-publish.sh`) publishes them to the `licensed` suite: by default every licensed `.deb` in `build/debs/*/` with its source from `build/src/`. It refuses a free package (CI publishes those), a `~dev` version, and a version already published with other bytes (`walnut-repo.py --strict`: the suite keeps every file forever, so a new file needs a new version). It signs with the archive key (from pass `Projects/Walnut/ArchiveKey` into a throwaway keyring; its passphrase is asked on the terminal, or read from `WALNUT_SIGN_PASSPHRASE_FILE`), reads the suite with its login (`apt-repo/licensed-auth.sh`: `WALNUT_LICENSED_USER`/`PASS` in config.local.mk, else pass `Projects/Walnut/LicensedRepo`), uploads through `servers/walnut.nanobyte.dev/walnut-upload.sh` (kind `licensed`, the deploy key `~/.ssh/walnut-deploy`) and waits for walnut-watch's verdict. `DRY_RUN=1` stops after the bundle (`build/tmp/licensed-bundle`).
- **On a console** (the owner's only), the suite is one more apt source with its login; nothing in a public package or image sets it up:

      # /etc/apt/sources.list.d/walnut-licensed.sources
      Types: deb
      URIs: https://walnut.nanobyte.dev/licensed
      Suites: licensed
      Components: main
      Signed-By: /usr/share/keyrings/walnut-archive-keyring.gpg

      # /etc/apt/auth.conf.d/walnut-licensed.conf (mode 600)
      machine walnut.nanobyte.dev/licensed login <user> password <password>

  The archive keyring is the one ps3-system already ships for `stable`.

## On the PS3

`ps3-system` ships the apt configuration (`packages/walnut/system/ps3-system/files/apt/`).

- **Sources.** Debian ports, live (`deb.debian.org/debian-ports`, unstable and unreleased): anything we don't build installs and upgrades from Debian. Walnut's `stable` from `https://walnut.nanobyte.dev/apt`, checked against the archive key (`/usr/share/keyrings/walnut-archive-keyring.gpg`); `testing` is in the same file (`/etc/apt/sources.list.d/walnut.sources`) with `Enabled: no`, checked against the CI key. The keyrings in `packages/walnut/system/ps3-system/files/apt/` are copies of `apt-repo/keyrings/*-keyring.gpg`: a key change goes into both.
- **Pins** (`/etc/apt/preferences.d/walnut`):
  - `stable` at 990, so our releases win over Debian's (500) whatever the versions, but an installed newer testing build is never downgraded (990 is below 1000): the next release, a higher version, takes over from it;
  - `testing` at 1: never chosen on its own, only with `apt install -t testing <pkg>`;
  - Debian's `linux-image-*`, `linux-headers-*`, `linux-kbuild-*`, `linux-support-*` and `src:mesa` at -1, so they are never candidates.
- **The owner's consoles** can add the licensed suite ("Licensed packages and the licensed suite" above). Consoles installed before the walnut.sources change (2026-10-23) keep the old `ps3.sources` and `preferences.d/ps3` (dpkg leaves a dropped conffile in place); dev images built before 2026-10-26 also have `ps3-lan.sources` and `preferences.d/ps3-lan` for the retired LAN repository, which make `apt update` fail on 10.0.0.10 until they are deleted.
- **`apt upgrade` is safe.** A Debian package that needs a newer Mesa than ours is kept back, and nothing is removed. Verified in the ppc64 chroot against live debian-ports (2026-10-16).
- Going back to an older version is explicit (`apt install pkg=<version>`).
- A rollback to Debian's Mesa means dropping the -1 pin by hand.
- New kernels arrive through meta packages (`linux-image-ps3` and `ps3rsx-modules-ps3`, the `-debug` ones for the debug kernel).

## Deploying and the image

`make deploy [PKGS=...] [AUTOREMOVE=1]` (`tools/deploy.sh`) does the following:

1. makes every local build (`build/debs/*`, debug symbols and licensed packages included) a flat apt repository in `build/tmp/deploy-repo` (suite `walnut-deploy`), serves it on this PC's loopback and opens an SSH reverse tunnel to it (`127.0.0.1:18071` on both ends, `DEPLOY_PORT`), so the PS3 fetches only what it installs and nothing is published anywhere;
2. refreshes only that repository's list on the PS3 (Debian's are about 100 MB);
3. with `-t walnut-deploy`, upgrades every installed package of ours that has a newer build, installs the named top-level packages, and upgrades again, each step simulated first, with `--no-remove` except removals named explicitly as `name-` in `PKGS` (then exactly those);
4. runs `ps3-migrate-home` once.

It brings its own sources, so it works whatever the PS3's apt configuration is. Only our builds are in view, so a Debian package that one of ours newly depends on must be installed from Debian first (apt names it; the LAN repository's `deps` component that used to carry them is gone). `DRY_RUN=1` runs only the simulations. Dev builds (`~dev`) install this way and nowhere else.

Rules, each with the reason it exists:

- **Apt never solves against Debian's full lists on the PS3.** Twice it stalled the machine for good (2026-10-16): the first time a packaging bug had disabled swap, the second time apt's own memory with about 60000 Debian packages. Hence "refreshes only our lists".
- **Kernels and ps3rsx modules go only with `KERNELS=1` or by name** in `PKGS` (`linux-image*`, `linux-headers*`, `linux-kbuild*`, `ps3rsx-modules*`). Since 2026-10-16 (df50edd): a deploy of two desktop packages installed an unfinished kernel build left in `build/debs`, without its ps3rsx modules, and that kernel had no graphics. A deploy picks up whatever is in `build/debs`.
- **A kernel install is** `make deploy KERNELS=1 PKGS="linux-image-ps3 ps3rsx-modules-ps3"`. Leave the debug kernel out unless it's wanted: one kernel unpacked at a time.
- **Kernel and Mesa installs stop the desktop first** (`make deploy` does it). Two kernels unpacked next to the desktop stalled the PS3 in swap on 2026-10-16 and needed a power cycle.
- **A Debian package can't ship its own config into another package's conffile** without ordering care (this bit Plymouth).
- **Every library a boot-time program loads costs seconds from the USB disk:** Pango for Plymouth's labels cost 10 to 11 s, an XKB keymap 3 s. Measure with `plymouth.debug=file:` (see the splash section).

**The image** (`tools/build-image.sh`) is made in layers:

1. the Debian-only base layer (`build/rootfs/base.tar`, dpkg under qemu, about 15 minutes, rebuilt only when `system/image/packages.txt` or the snapshot changes);
2. our packages installed by apt from a flat repository of our debs (`system/image/packages-ours.txt`; the licensed ones sit in `build/debs` like the rest, `classicube` pulls in `classicube-data`), with our Mesa replacing Debian's as an upgrade;
3. `autoremove --purge` (LLVM, z3 and sensors go);
4. `system/image/customize.sh` (hostname, fstab, user, key, locale).

The result is `build/rootfs/debian-ppc64.tar`. The image installs the same packages `make deploy` installs on the running PS3, so whatever is tried live is what the next image gets.

## Kernels

`make linux-deb` builds Debian's `linux` source with `packages/patched/kernel/linux/patches` and `packages/patched/kernel/linux/packaging`, using the full configs from `tools/linux-config.sh` (`ps3_defconfig` + `packages/patched/kernel/linux/config/ps3.config`, plus `ps3-release.config` for the release kernel). It is cross-built for ppc64, then `linux-kbuild` natively. The two kernels (decided 2026-10-16):

- **release:** release `6.12.111+ps3.N`, packages `linux-image-6.12.111+ps3.N` and `linux-headers-6.12.111+ps3.N`, metas `linux-image-ps3` and `linux-headers-ps3`;
- **debug:** release `6.12.111+ps3.N-debug<stamp>`, metas `linux-image-ps3-debug` and `linux-headers-ps3-debug`. The stamp (`YYYYMMDDHHMM`, UTC) is the date of the commit the version stands for (`tools/deb-version.sh`; a dirty tree uses the build time), so the same commit always gives the same release and every debug build installs next to the others.

Debian's flavours are `ps3` and `ps3-debug` (their names make the metas). A flavour's `localversion` in `defines.toml` replaces Debian's `-<flavour>` suffix of the release (`packages/patched/kernel/linux/packaging/0001-ps3-flavours.patch`, the stamp filled in by `tools/deb-prepare.sh`). Each image ships its kernel arguments as `/usr/lib/ps3/boot/<release>.args` (`packages/patched/kernel/linux/flavours/release.args`, `debug.args`). There is no initramfs (`ps3-no-initramfs` stands in for initramfs-tools).

`make ps3rsx-modules` builds the RSX driver for both kernels, natively against their headers packages: `ps3rsx-modules-<release>`, metas `ps3rsx-modules-ps3` (release) and `ps3rsx-modules-ps3-debug`.

Before installing a new kernel, `make netboot-deb FLAVOUR=release|debug ROOT=nfs|usb` boots it from Petitboot without touching the disk, with the arguments its package ships.

**The boot menu.** `ps3-boot` writes `/etc/kboot.conf` from the kernel hooks (`ps3-update-kboot`). The default is the newest kernel, unless `/etc/default/ps3-boot` sets:

- `PS3_DEFAULT_FLAVOUR`: `release` is the newest kernel without a `-debug` suffix, `debug` the newest `-debug*` one (the old names `ps3` and `ps3-release` still mean debug and release);
- or `PS3_DEFAULT`: a release, which wins while it is installed.

The image sets `PS3_DEFAULT_FLAVOUR=release`. Entries that aren't a kernel's (the SSD root's, [GAMEOS-HDD.md](GAMEOS-HDD.md) "Phase B") go in `/etc/kboot.d/*.conf`; `ps3-update-kboot` (0.498 on) appends them as written, leaving out one whose label is taken or whose image or `initrd=` is missing. The SSD root's initramfs is our own (`make initrd-ssd`), not initramfs-tools: kernels still boot without one, and only that entry passes `initrd=`.

**Caveat (2026-10-18, from the installed Petitboot's code, [GAMEOS-HDD.md](GAMEOS-HDD.md) "Petitboot's behaviour"):** this Petitboot ignores `default=`, `timeout` and `delay` in kboot.conf. Its default is the entry whose label matches the last one booted by hand (a hash kept in its OS-area settings), and its timeout comes from `ps3-bl-option`. The entry order still matters for the list. To be confirmed at the TV.

## The boot splash

Walnut Linux's splash is Plymouth with our theme, made to show early from the USB disk and to stay up until X draws (2026-10-16).

- **Plymouth.** `packages/patched/desktop/plymouth/` is Debian's `plymouth` 26.134.222-1 plus one patch, `0001-script-no-console-viewer.patch`. The script plugin's first full redraw called `ply_console_viewer_hide(NULL)` on a display without a console viewer, which is what you get without text rendering (no label plugin) or with `plymouth.prefer-fbcon`: the daemon died with SIGSEGV as the theme started. With the patch the theme needs no text rendering, so `plymouth-label` isn't installed. With it, loading Pango, Cairo and fonts took 10 to 11 s at boot from the USB disk.
- **The theme.** `ps3-splash` needs our Plymouth (`>= 26.134.222-1+ps3`) and provides the `walnut` script theme: the logo, a progress bar from small images, the walnut rotated live as it rolls.
  - Its preinst diverts Plymouth's DRM renderer (`renderers/drm.so`). That renderer can't drive ps3rsx at boot ("Could not initialize heads") and cost seconds of failed attempts before the framebuffer fallback (ps3fb). The diversion of `label-pango.so` that 0.180 made is removed on upgrade.
  - The postinst sets the theme and `DeviceTimeout=1` in `plymouthd.conf`. With udev, Plymouth waits 8 s for a DRM device before it takes the framebuffer. `plymouth.ignore-udev` instead showed nothing, since without udev it hands the framebuffer renderer the DRM device path.
  - Two drop-ins: `plymouth-start` runs with `XKB_CONFIG_ROOT=/nonexistent` (Plymouth compiled an XKB keymap for typed input, 3 s from the busy disk, and nothing types at boot), and `plymouth-quit` runs `plymouth quit --retain-splash`.
- **From the splash to the desktop.** The autologin getty on tty1 (`ps3-desktop`'s generator) has `TTYReset=no`, `TTYVHangup=no` and `TTYVTDisallocate=no`, so it leaves tty1 in graphics mode with the splash on it. (The reset switched back to text and showed the login for the seconds X takes to start.) The login shell switches tty1 back to text whenever no X runs on it. `ps3-session` puts the splash's last frame on X's root window (`ps3-splash` renders it, padded so `hsetroot -center` lands the logo where Plymouth drew it) and drops it once the desktop draws, freeing its 8 MB pixmap.
- **Which kernels.** Only kernels whose arguments contain `splash` show it: the release kernel (`packages/patched/kernel/linux/flavours/release.args`: `quiet splash`), not the debug one.
- **Debugging without text on tty1:** put `plymouth.debug=file:/var/log/plymouth-debug.log plymouth.debug=stream:/dev/null` on the kernel line. `file:` alone still printed the debug text on tty1; the second option sends the stream to `/dev/null`. For a test boot, append both to the one line of `/usr/lib/ps3/boot/<release>.args` and run `sudo ps3-update-kboot`. The file belongs to the kernel package, so the next upgrade of that package drops the change again.
- **pnmixer.** `packages/patched/desktop/pnmixer/` is Debian's `pnmixer` 0.7.2-3 (the tray's volume applet; upstream has been quiet since 2018) plus `0001-audio-don-t-crash-on-a-volume-change-without-a-sound.patch`. When it hooks no sound card it runs on with a NULL card, and a slider move, a scroll on the icon or a hotkey read the volume from it: SIGSEGV, and the tray had no volume icon on the first nightly. The three setters read the volume through `audio_get_volume`, which gives 0 without a card.

## Game data

Licensed data is packaged apart from the engines, and free packages never depend on it.

The goal for end users (PLAN 5.10, the user, 2026-10-20): the public engine packages copy the data
from the user's own copy on first run (a "First time setup" dialog: insert the CD, or point to a disc
image or an existing install). The licensed data packages below stay for our own machines.

### The rule: free packages never depend on licensed ones

Every free package (the engines, `ps3-games`, `ps3-desktop`, ...) must be installable and distributable on its own. None of them has a `Depends` or `Recommends` on a licensed package (one in `tools/licensed-packages.txt`, or with a Section `non-free/*` or `local/*`); `Suggests` is fine. The user puts the data in the game directory (each game's docs say where and how to get it).

Our own machines get the data from the licensed suite through the metapackage `ps3-games-licensed` (`packages/licensed/ps3-games-licensed/`, Section `local/games`, so the licensed suite only). It depends on `ps3-games`, `classicube-data`, `quake-shareware`, `quake2-demo-data` and `sauerbraten-nonfree-data`, so `sudo apt install ps3-games-licensed` gives the whole suite with its data. Personal use only. A new licensed data package goes into `tools/licensed-packages.txt` and into that metapackage's `Depends`.

### Quake data from game-data-packager

`make quake-data` (`tools/game-data.sh`, in the games builder with game-data-packager 89 from contrib and lhasa) writes two packages into `build/debs/quake-data`:

- `quake-shareware_89_all.deb`: Quake's shareware `id1/pak0.pak`, demos included, Section `non-free/games`;
- `quake2-demo-data_89_all.deb`: Quake II's demo `baseq2`, `demos/q2demo1.dm2` in its pak, Section `local/games`.

The shareware archives are downloaded from id's mirrors (kept in `build/downloads/game-data`). Deploys, images and `make publish` take the debs like any build. The data is licensed (Quake II's demo may not be redistributed at all), so they go to the licensed suite only (`make publish`).

game-data-packager's output isn't reproducible (the files' dates are the build time), and the kept suites never take a different file under a published version. So once a version is published, `make quake-data` downloads that file from the licensed suite instead of making it again (with the suite's login from `apt-repo/licensed-auth.sh`, checked against the suite's SHA256). The suite has nothing yet: the version 89 files the LAN repository published are in the retirement backup's `ps3-licensed-debs/`, so `make publish DEBS="<those two>"` before the next `make quake-data` keeps the same files. `REGEN=1` makes it anyway and refuses when the version it gets is already published. A new file only goes out under a new version (a newer game-data-packager).

### ClassiCube's data

The `classicube` source package builds two packages (since 2026-10-17, `+ps3.366`): `classicube`, the engine (ppc64, free), and `classicube-data` (all, Section `local/games`, so the licensed suite), which holds the texture pack and the audio, both Mojang's. Since then `classicube-data` is a source package of its own (`packages/licensed/classicube-data/`).

- The texture pack, `static.classicube.net/default.zip`, has the files ClassiCube's launcher otherwise extracts from Mojang's Minecraft Classic 0.30 and 1.6.2 client jars (`terrain.png`, `char.png`, the mob skins; the "classic jar files" in the pinned `src/Resources.c`) with ClassiCube's patches. The sounds and music come from Mojang's resource server (`packages/patched/games/classicube/sounds.sh`). Paths are as before (`/usr/lib/classicube/texpacks`, `audio/`).
- `classicube` only `Suggests` `classicube-data`, as the rule above requires. `classicube-data` has `Breaks`/`Replaces: classicube (<< 1.3.8~git20261014.aa47929-1+ps3.366~)` for the upgrade from the bundled package.
- Each new `classicube-data` (15 MB) goes to the licensed suite, kept forever.
- Checked with dpkg on 2026-10-17: the bundled 330 upgrades to the pair in any unpack order, and the files then belong to `classicube-data`.

Without the data the engine still works. `/usr/games/classicube` runs it from `~/.local/share/classicube`, and ClassiCube's launcher (the pinned source's `Resources_CheckExistence`) finds `texpacks/classicube.zip` missing and asks to fetch it and the sounds and music: the texture pack from `static.classicube.net`, the audio from Mojang's resource server (BearSSL is built in). It writes them under that directory, nothing outside it, and our patches leave this alone. The steps for a user, or for copying the files by hand, are in `/usr/share/doc/classicube/README.Debian` (`packages/patched/games/classicube/debian/classicube.README.Debian`).

### Sauerbraten's models and sounds (`sauerbraten-nonfree-data`)

Debian's `cube2-data` 1.3-1 is not a DFSG-cleaned Sauerbraten. It is the BananaBread content (gitlab.com/cube2-data/cube2-data, 295 MB, 989 files under `packages/`), installed in `/usr/share/games/cube2`, which its `/usr/games/cube2` passes to the engine as `-k`. The engine (cube2 0.0.20201229, Sauerbraten's `fpsgame`, which BananaBread forked) loads models and sounds by names compiled in (`entities.cpp`, `render.cpp`, `weapon.cpp`, `monster.cpp`) or from `data/sounds.cfg`. cube2-data lacks most of them, hence "could not load model" for the pickups, hud guns, gibs and so on, and "failed to load sample".

The match is the SourceForge release `2020_12_29` (`sauerbraten_2020_12_29_linux.tar.bz2`, 985,823,440 bytes, MD5 `b50d018f...` as SourceForge lists it, SHA256 `cdba7c4a...`, both in `config.mk`; kept in `build/downloads/game-data`).

Of the 8,914 files under that release's `packages/`, 8,780 are not in cube2-data (the maps' textures, map models, skyboxes, music; cube2-data's own maps don't use them). We package only what the game code and `sounds.cfg` need: `packages/licensed/sauerbraten-nonfree-data/files.txt`, 912 files, 149 MB unpacked.

- Models: `ammo health boost quad quadrings quadspheres teleporter carrot checkpoint hudguns vwep projectiles gibs debris barreldebris base skull flags armor`, the five player models (`mrfixit snoutx10k ogro2 inky captaincannon`, with their `armor/` and `hudguns/`), `ogro` and `monster`.
- Sounds: every one cube2-data lacks.
- Files cube2-data has are not repeated. A few differ in the release (e.g. `models/debris/md2.cfg`, `models/armor/armor.txt`) and cube2-data's win, so the package installs next to cube2-data without a conflict.
- Not packaged: the other 7,868 files (mapmodels, the Sauerbraten maps' textures and so on). Add them to `files.txt` if Sauerbraten's own maps are ever wanted.

Why Debian leaves them out: the licenses are not DFSG-free. By directory, from the license and readme files kept in the package:

- `ammo boost health flags gibs debris barreldebris` (makkE): CC BY-NC-SA;
- `hudguns vwep mrfixit inky captaincannon` (geartrooper and others): CC BY-NC 3.0;
- `snoutx10k` CC BY 3.0 and `skull` CC BY-SA 3.0 (free, but in the same release);
- `quad` by DCP, converted by makkE; `teleporter` (Todretter) "all rights reserved so far";
- `ogro` and `ogro2` carry the 1998 Quake II player model's own readme;
- the sounds are mixed: `ambience` CC Sampling+, `q009` CC BY-SA, `free`/`awesund` modified by Darthvim, others per their README.

So: personal use only, hence the licensed suite. Section `local/games`, version `0.0.20201229-1+ps3.N`, arch all. `cube2` and `cube2-server` don't depend on it (cube2 isn't rebuilt for a `Suggests`); the package `Depends: cube2-data`, and `ps3-games-licensed` depends on it.

The source tarball is made by `tools/sauerbraten-data.sh` (`make sauerbraten-nonfree-data-src`: the release downloaded once and checked, the listed files with a fixed owner and mtime, `gzip -n`). The deb is `make sauerbraten-nonfree-data-deb` (indep builder, no qemu). The same files give the same deb for a given commit. As with the Quake data, once a version is published, never build it again under that version: take the published file.

## Migration from the old image scripts

`ps3-system` and `ps3-desktop` took over the files `system/image/customize.sh` and `image/desktop-setup.sh` used to write. Their preinsts archive each legacy file that no package owns to `/var/backups/ps3-legacy-{system,desktop}-<date>.tar` and remove it (and its enable links). `ps3-migrate-home` deletes home-directory copies identical to the packaged defaults and moves the rest to `~/.local/share/ps3-legacy/`. Done on the PS3 on 2026-10-16.

## History

- 2026-10-16: the Debian-source-package pipeline replaced the old kernel.org tarball one; the repository got its `ps3` and `ps3-dev` suites; deploys got `KERNELS=1` after df50edd.
- 2026-10-17: `ps3-licensed` split off; ClassiCube's data split from the engine (`+ps3.366`).
- 2026-10-18: this Petitboot found to ignore `default=` in kboot.conf; the installed boot menu logic is in [GAMEOS-HDD.md](GAMEOS-HDD.md).
- 2026-10-21: walnut.nanobyte.dev's suites, built and published by GitHub Actions (docs/release/ALPHA.md M6).
- 2026-10-26: the LAN repository on tibi-server retired and deleted (its `make repo-snapshot` image snapshots and the `deps` component with it); licensed packages go to walnut.nanobyte.dev's `licensed` suite, `make deploy` serves `build/debs` itself. The tree reorganized: packages under `packages/` by kind (docs/LAYOUT.md).

Older versions and the dated history: [archive/PACKAGING.md](archive/PACKAGING.md), [archive/PLAN.md](archive/PLAN.md) ("Base and toolchain", "Build setup", "Earlier status (2026-10-16)").
