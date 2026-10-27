# Packaging and the apt repository

This is how our software becomes Debian packages, how those packages reach the PS3, and the rules that keep that safe. Everything we ship is a `.deb` built from a source package, served from walnut.nanobyte.dev's apt repository (published by GitHub Actions; licensed data from this PC to a password-protected suite), and installed on the console with apt. The base system, the compilers and the build images are described first, then the package kinds, the repository, deploys, kernels and the boot splash.

For terms see the [glossary](README.md#glossary). The 2026-10-18 release plan for the retired LAN repository is archived in [archive/RELEASE-CANDIDATES-2026-10-19.md](archive/RELEASE-CANDIDATES-2026-10-19.md); a fresh image against the live system is in [IMAGE-DIFF.md](IMAGE-DIFF.md).

## Base and toolchain

Decided 2026-10-15, with the packaging plan of 2026-10-16.

**Base.** The base is a pinned Debian sid snapshot (`DEBIAN_SNAPSHOT` in `config.mk`, now `20261013T000000Z`, taken from snapshot.debian.org's debian-ports archive). Debian has no stable release for ppc64 (big-endian), only debian-ports unstable, so a frozen snapshot is the closest thing to an LTS. It moves on purpose, at the latest when Debian 14 is released, and never piecemeal.

**One compiler version everywhere: GCC 16.2,** the snapshot's system compiler.

- PPE side (kernel, Mesa, ps3rsx, games): Debian's `gcc-16-powerpc64-linux-gnu` 16.2.0-3cross1, from the amd64 sid snapshot of the same date.
- SPU side: our port of the SPU backend to GCC 16.2 (`packages/patched/toolchain/spu/gcc/`, see [SPU-GCC.md](SPU-GCC.md)).
- binutils 2.47 for the SPU, with one gas patch (`packages/patched/toolchain/spu/binutils/`): gas took `nop 127` for the `.nop` directive, which broke branch hints (the `hbrr` regression).
- newlib 4.6.0 for the SPU, with seven patches (`packages/patched/toolchain/spu/newlib-4.6/`).
- On the SPEs (2026-10-15) 1629 of 1677 execute tests pass, the same tests one by one as with binutils 2.33.1 and newlib 1.20.
- A PPE miscompile to know about (2026-10-20): GCC 16.2's loop vectorizer at `-O3` with `-mcpu=cell` (AltiVec without VSX; `-mcpu=970` and `-mcpu=power6 -maltivec` too, `-O2` not) gets stb_image_resize's 4-channel horizontal downsampling loop wrong, and the result is near-black, transparent images. It needs `-DNDEBUG`, because the asserts inside the loop stop the vectorizer. Warzone 2100 is fixed (its patch 0007: [warzone2100](../gamedb/games/warzone2100/)). Any other package that builds stb_image_resize, or other float accumulation loops like it, at `-O3 -mcpu=cell` can hit the same bug (most CMake Release builds use `-O3`). A minimal C reproducer is still missing, so nothing has gone upstream yet.

**Kernel.** 6.12 LTS (supported to December 2028), from Debian's `linux` source (trixie, 6.12.111-1) with our patches. Two flavours, named 2026-10-16:

- the release kernel `6.12.111+ps3.N`: 8.7 MB more `MemTotal` on the PS3 than the debug one (222704 kB against 213992 kB; vmlinux 11.9 MB against 21.2 MB, task #14);
- debug kernels `6.12.111+ps3.N-debug<stamp>`, each build its own release so they sit side by side.

**Mesa.** Debian's source package of the snapshot's release, rebuilt with our patches (`packages/patched/graphics/mesa/`, version `26.2.4-1+ps3.N`).

**Walnut Linux** is the distribution's name and look (decided 2026-10-16). The logo is a stand-in after the owner's reference (`packages/walnut/system/ps3-splash/art/make-logos.py`; a better one before any release). The walnut is the kernel's boot logo, there is a Plymouth splash (the logo and a progress bar with the walnut rolling along it, on the desktop's blue), and the wallpapers are the owner's photos of the walnut tree (CC BY-NC 4.0). The splash is under "The boot splash" below.

**Build images.** Everything builds in pinned Docker images under `build-containers/`:

- `Dockerfile`: the toolchain and mmdebstrap (kernel and rootfs, under qemu-user);
- `Dockerfile.deb`: Debian's packaging tools (the deb builder; aptly is still in it, unused since the LAN repository went);
- `Dockerfile.games`: [ClassiCube](../gamedb/games/classicube/)'s sound conversion and game-data-packager;
- `Dockerfile.spu`: the spu-elf toolchain.

The builder image is pinned by digest (`BUILDER_BASE`) with apt on the same snapshot. Every build (images, chroots, the image, the few direct downloads) fetches the snapshot through walnut.nanobyte.dev's cache of it, `SNAPSHOT_URL` in `config.mk` (`walnut-aptcache`, servers/walnut.nanobyte.dev/aptcache): snapshot.debian.org throttles bulk downloads with `503 TooManyRequests`, and a pinned snapshot never changes, so each file is fetched from it once. `SNAPSHOT_URL=http://snapshot.debian.org` bypasses the cache. Outputs go to `build/`, which is ephemeral except `build/aptly`. SPU toolchain experiments build side by side (`SPU_OUT=build/tmp/spu-<name>`). The tree is organised by package kind (docs/LAYOUT.md).

## Kinds of source packages

Each package is a directory with a `package.conf`. A game's own packages live in its gamedb bundle, `gamedb/games/<id>/packages/`, its licensed data packages in `gamedb/games/<id>/licensed/`, an engine's in `gamedb/engines/<e>/packages/` (gamedb/README.md); everything else under `packages/`: `walnut/<area>/` (ours), `patched/<area>/` (Debian's or upstream's with our patches), `repackaged/` (upstream releases as they are) or `licensed/` (the metapackage `ps3-games-licensed`) (docs/LAYOUT.md). It is named by its directory alone (`make xmoto-deb`, `build/debs/xmoto`); `tools/pkg-dir.sh <name>` finds the directory for `tools/deb-*.sh` and `tools/pkg-*.sh`, `tools/pkg-dir.sh --all` lists them all (the Makefile's `PKG_DIRS`, CI's workflows) and `--order` in build order (`LOCAL_DEPS` first, linux first, ps3rsx last). Two packages of one name anywhere stop all of them with an error.

| `TYPE` | What it is | Examples | Version |
|---|---|---|---|
| `debsrc` | Debian's source package at the snapshot, plus our quilt series (`<pkg>/patches`, added as `debian/patches/ps3/`) and changes to `debian/` itself (`<pkg>/packaging`) | `mesa`, `linux`, `plymouth`, the games and libraries we patch (README.md) | `<debver>+ps3.N` |
| `native` | Our own code: the directory is the source, `debian/` inside | `ps3-boot`, `ps3-gpu-tools`, `ps3-spu`, `ps3rsx`, `ps3-system`, `ps3-desktop`, `ps3-games` (its game-specific part packed from gamedb, `COPY_IN`), `ps3-splash` | `0.N` (`ps3rsx`: `0.N+k<kernel release>`, its `-` as `.`) |
| `upstream` | Software Debian lacks: an upstream tarball plus component tarballs | `classicube`, `zlib-ng`, `warzone2100` | `<upver>-1+ps3.N` |

**Our version never changes the original's (the user, 2026-10-26).** A package we patch or repackage is versioned `<original version>+ps3.N`: Debian's full version when Debian has the package (`26.2.4-1+ps3.N`), else upstream's with `-1`. Dpkg reads only the last `-` as the revision, so `+ps3.N` keeps the original whole: it sorts above that exact original and below the original's next version or revision, so neither side ever outranks a newer release of the other by accident. Never an epoch, a higher revision or a bigger upstream number of our own. `0.N` is only for code that is ours alone, under a name no Debian or upstream package uses.

**Upstream tarballs (`upstream.mk`, `orig.mk`).** A package that starts from an upstream tarball pins it in `<pkg dir>/upstream.mk` (version, URL, sha256; the Makefile includes every one) and has its recipe in `<pkg dir>/orig.mk`: the rule that fetches the tarball (`$(call fetch,URL,SHA256)`, a checked download into `build/downloads`) or makes it ([ClassiCube](../gamedb/games/classicube/)'s `+dfsg` repack leaves out the paths its `dfsg-exclude.txt` lists, [OpenRCT2](../gamedb/games/openrct2/)'s `+ds` one drops upstream's `debian/`, the licensed data packages pack theirs), the `<pkg>-src` prerequisites, and `ORIG_VARS += X_ORIG` for each variable naming a tarball: the Makefile passes every one to the package tools (`DEB_ENV`'s `-e X_ORIG=...`), where `package.conf` uses it (`ORIG=$CC_ORIG`, `COMPONENTS="objects:$ORCT2_OBJECTS ..."`). The Makefile includes the `orig.mk` files in two passes, the free packages' first, then the licensed ones' (`gamedb/*/*/licensed/*`): a licensed recipe may use a free package's variables (classicube-data packs ClassiCube's `CC_AUDIO`), never the reverse, and the public copy, which has no licensed directory, has every free recipe. Both files sit in the package's directory, so they are part of its version and its CI filter. Today: `darkplaces-gl2`, `zlib-ng`, `warzone2100`, `openrct2` (and its four asset zips), `classicube` (also `make classicube-audio`), and the licensed `classicube-data`, `sauerbraten-nonfree-data` and `sauerbraten-data` (which shares the [Sauerbraten](../gamedb/games/sauerbraten/) release download of the first).

A `debsrc` package from outside Debian's main sets `COMPONENT`. `yquake2` and `game-data-packager` use `COMPONENT=contrib`, since debian-ports builds nothing from contrib. The deb builder's apt sources are main only, so `tools/deb-fetch.sh` (and `pkg-rebase.sh fetch`) add that component's deb-src lines inside the throwaway container for the one fetch; `build-containers/Dockerfile.deb` and the builder's tag stay as they are. Build dependencies still come from main.

A native Debian source (format `3.0 (native)`, no orig tarball: `game-data-packager`) works the same way: our quilt series is pushed onto the tree, the changelog entry is `<version>+ps3.N` (`dch --preserve` keeps the tree's name), and the new source package is the patched tree itself (without quilt's `.pc`), `debian/patches/ps3/` riding along as the record.

**Versions.** N is the commit count up to the last commit that touched the package's directory, the sources its `package.conf` `VERSION_PATHS` names, or the shared packaging tools (`tools/deb-version.sh`), so versions only grow. A `VERSION_PATHS` entry is a file, a directory or a pattern read the way GitHub reads its workflows' path filters (`github-ci/gen-workflows.py` passes the same strings): `*` within one path component, `**` across them. A build from a tree with uncommitted changes is a dev build, `N+1~dev<stamp>`: it sorts above the current release and below the next one, and only `make deploy` installs it (no suite takes one). A consequence: any commit to `tools/deb-*.sh` or `build-containers/Dockerfile.deb` shifts the N of every package.

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
| `testing` | every push's build: release and debug kernels, their modules, everything else, sources | the CI key | newest 5 source versions | CI's one testing publisher, minutes after each build |
| `testing-debug` | testing's `-dbgsym` | the CI key | newest 5 | CI, with testing |
| `stable` | builds made for stable by hand, without the debug kernel and its modules | the archive key's signing subkey | every version | CI, "Run workflow" with the owner's approval |
| `stable-debug` | stable's `-dbgsym` | the archive key | newest 3 | CI, with stable |
| `licensed` | game data we may not redistribute, and the sources it is made from | the archive key | every version, files never change | `make publish`, from this PC only |

The old LAN repository on tibi-server (`http://10.0.0.10:8070`: suites `ps3`, `ps3-dev`, `ps3-dbgsym`, `ps3-licensed` and the `img-*` snapshots, published with aptly from `build/aptly`) was retired and deleted on 2026-10-26. Its whole tree and `build/aptly` are kept in `/mnt/data_linux/Temp/ps3/retired-tibi-server-20261026/` (tarballs with SHA256SUMS; `ps3-licensed-debs/` holds the ten licensed packages it published, ready for `make publish` if they should go to the licensed suite unchanged).

### Walnut's suites on GitHub Actions

- Every package except the licensed ones has `.github/workflows/pkg-<name>.yml` (from `github-ci/gen-workflows.py`; run it after adding, moving or removing a package). A push touching what a package's version counts builds it for testing. "Run workflow" builds it fresh for the suite picked there, testing or stable. Package runs never publish to testing themselves: a build stores its output (debs, `-dbgsym` debs, the source package) as a GitHub artifact kept 14 days, named `walnut-<suite>@<package>@<source>=<version>`, so the name alone says what it would add.
- **testing has one publisher**, `.github/workflows/walnut-publish.yml` (`github-ci/batch-publish.py`). It starts when any package workflow finishes and every 10 minutes, and runs one at a time: a running publish is never cancelled. GitHub keeps one more run waiting and cancels an older waiting one; that loses nothing, because each run takes everything ready when it starts and the waiting run starts after the running one. Its first job (no secrets) compares the artifacts with testing's signed index: per package the newest build wins (highest version, then the newest artifact), and it is skipped when testing already has that version or a newer one. Only when something is left does the second job (environment walnut-testing) download those artifacts and publish them all in one `walnut-repo.py add` and one upload through `github-ci/publish.sh`, split into bundles of at most 3 GiB of artifacts (one bigger artifact goes alone; walnut-watch takes 8 GiB). Nothing records "published" anywhere but the index, so a failed publish leaves its builds for the next run and nothing goes out twice. A refused bundle is tried package by package, so one bad build can't hold back the rest. After a linux build goes out it starts ps3rsx's build for testing (the GPU module is built against testing's kernel).
- So a successful build is in testing a few minutes after it finishes (the publish itself, plus the one running ahead of it). If its trigger is lost or the publish fails, the next run takes it: the next build to finish, or the schedule, nominally 10 minutes (GitHub may start a scheduled run late when it is busy).
- **stable** publishes from the package's own run, one package at a time: the owner approves each publish (environment walnut-stable) after the build, and stable is never built automatically, so there is nothing to batch and no other writer to race.
- Symbols go to `<suite>-debug` in the same upload, so both switch together. A console gets them with one more line, e.g. `deb https://walnut.nanobyte.dev/apt testing-debug main`.
- Each suite has its own pool (`pool/<suite>/main/...`): testing and stable are separate builds of one version.
- Each upload names the InRelease it was built on; walnut-watch refuses a stale one and `github-ci/publish.sh` makes it again on the new state (after a random pause, up to 8 times; each verdict is awaited up to 30 minutes). With one testing publisher that is rare. It deletes pool files no index names any more, 14 days after they were first seen unnamed (never in `licensed`).
- **When to publish to stable.** Only after the package is proven on the PS3, with the user's OK. Never a `~dev` version (CI builds from commits only).

### Licensed packages and the licensed suite

A package is licensed when its Debian Section is `non-free/*` or `local/*` (game-data-packager marks non-redistributable data `local/`), or when its name is a licensed name. `tools/licensed.py` is the one place that answers it, and every guard below asks it: the names are every directory under `packages/licensed/` and `gamedb/*/*/licensed/`, the `[[licensed]]` entries of gamedb's `build.toml` files (every game's licensed data, also what has no directory of ours: game-data-packager's packages; in the public copy, which has no licensed directory, the names come from these), and the generic `ps3-games-licensed` (`GENERIC` in licensed.py: no game's, licensed in every checkout); the leak globs are `tools/licensed-files.txt` (the generic ones, Sony's `*.prx`/`*.sprx`) plus the `[[licensed]]` `leak_files` and every `dfsg-exclude.txt` entry. It fails closed: a list it can't read stops the guard. `licensed.py check` (in `make test`) wants every `gamedb/*/*/licensed/<pkg>/` declared in its own bundle's `build.toml`, no licensed name outside a licensed directory, a `local/*` or `non-free/*` Section on exactly the licensed packages of ours, and a checkout with all licensed directories or none; `check --since REV` that no name has left the set since REV. Today that is every package of the table in [Game data](#the-licensed-data-packages) and the metapackage `ps3-games-licensed`. A `[[licensed]]` entry's `builder` says how it is made: `source` (a source package of ours in `licensed/<pkg>/`), `script` (`licensed/<pkg>/<pkg>.sh` from the owner's own copy: `make licensed-data PKG=<pkg> INPUT=<the owner's CD image or archive>`) or `game-data-packager:<game>` (`make game-data`, below). `tools/gamedb/build-info.py` reads these for the Makefile.

- **Built here only.** CI never builds them: they have no workflow (`gen-workflows.py` skips every licensed name and directory), and `github-ci/build-package.sh` refuses them. GitHub has no game data.
- **Never public.** The public GitHub copy leaves `packages/licensed/` and `gamedb/*/*/licensed/` out of every commit, and any `packages/**/<name>/` of a licensed name as a second check (`tools/public-export.sh`). The image's public profile leaves them out too (`tools/image-packages.sh`). `tools/leak-check.py` keeps licensed files out of every free package and source CI publishes.
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

**A kernel comes with its GPU driver.** ps3rsx is built for each kernel release after the kernel (CI starts it once the kernel is in testing), so for a while testing has a kernel without its module. The image metas depend on `ps3rsx-modules-<release>` (packaging patch 0003), so apt holds a kernel upgrade back until the matching module is published and installs the two together; and `ps3-update-kboot` lists kernels without their module after the others, so Petitboot never boots one first (ps3rsx's postinst and postrm rerun it). Before this, a console that upgraded in that window booted a kernel with no GPU driver and no desktop (2026-10-27).

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

Every free package (the engines, `ps3-games`, `ps3-desktop`, ...) must be installable and distributable on its own. None of them has a `Depends` or `Recommends` on a licensed package (`tools/licensed.py names`, or one with a Section `non-free/*` or `local/*`); `Suggests` is fine. The user puts the data in the game directory (each game's docs say where and how to get it).

Our own machines get the data from the licensed suite through the metapackage `ps3-games-licensed` (`packages/licensed/ps3-games-licensed/`, Section `local/games`, so the licensed suite only). It depends on `ps3-games` and the licensed data gamedb names for it (the `[[licensed]]` entries of the games with suite `licensed`, and those with `meta = true`; `tools/gamedb/pack-integration.py` writes its substvars), so `sudo apt install ps3-games-licensed` gives the whole suite with its data. Personal use only. A new licensed data package gets a `[[licensed]]` entry in its game's `build.toml`, which puts it into that metapackage's `Depends`.

### The licensed data packages

Each is declared by a `[[licensed]]` entry in its game's `build.toml` (gamedb/README.md); what it holds,
where its files come from and their licences are on the game's technical page.

<!-- gamedb:licensed-table -->
| Package | Game | Built by | In ps3-games-licensed |
|---|---|---|---|
| `sauerbraten-nonfree-data` | [BananaBread](../gamedb/games/bananabread/) | `games/bananabread/licensed/sauerbraten-nonfree-data/` | yes |
| `classicube-data` | [ClassiCube](../gamedb/games/classicube/) | `games/classicube/licensed/classicube-data/` | yes |
| `rct2-data` | [OpenRCT2](../gamedb/games/openrct2/) | `games/openrct2/licensed/rct2-data/` | no |
| `tyrian-data` | [OpenTyrian](../gamedb/games/opentyrian/) | `games/opentyrian/licensed/tyrian-data/` | no |
| `quake-shareware` | [QuakeSpasm](../gamedb/games/quakespasm/) | game-data-packager (`make game-data`) | yes |
| `sauerbraten-data` | [Sauerbraten](../gamedb/games/sauerbraten/) | `games/sauerbraten/licensed/sauerbraten-data/` | no |
| `quake2-demo-data` | [Yamagi Quake II](../gamedb/games/yquake2/) | game-data-packager (`make game-data`) | yes |
<!-- /gamedb:licensed-table -->

A licensed package is built on our own PC, never by CI, and goes to the licensed suite only
(`make publish`), kept forever. Free packages only `Suggests` it (the rule above), and a package that
replaces data a free package used to bundle carries the `Breaks`/`Replaces` for that upgrade.

### Built by game-data-packager

**Our game-data-packager.** `packages/patched/tools/game-data-packager/` is Debian's `game-data-packager` 89 from contrib (`89+ps3.N`; `NATIVE_PROFILES=pkg.game-data-packager.nowrappers` builds only `game-data-packager` and `game-data-packager-runtime`, not the quake/quake2/quake3 wrappers). Walnut Games imports the player's game data with it (PLAN 5.10): `--progress-fd FD` writes `step`, `file`, `generated` and one `error <kind>` line (`not-found`, `unknown-version`, `needs-download`, `no-space`, `failed`) to FD, `--no-launchers` keeps the Doom family's `.desktop` files and icons out (our launchers do that), and the unpackers (innoextract, unshield, cabextract, 7zip, lhasa, unzip) are Recommends. `make game-data` (the licensed suite's data) stays on Debian's own copy in `Dockerfile.games`, so its published files keep their versions.

`make game-data` (`tools/game-data.sh`, in the games builder with game-data-packager 89 from contrib and lhasa) makes every `build.toml` `[[licensed]]` with `builder = "game-data-packager:<game>"` (`tools/gamedb/build-info.py game-data`) into `build/debs/game-data` (`build/debs/quake-data` before 2026-10-26, as `make quake-data`); the archives it downloads are kept in `build/downloads/game-data`. Deploys, images and `make publish` take the debs like any build.

game-data-packager's output isn't reproducible (the files' dates are the build time), and the kept suites never take a different file under a published version. So once a version is published, `make game-data` downloads that file from the licensed suite instead of making it again (with the suite's login from `apt-repo/licensed-auth.sh`, checked against the suite's SHA256). `REGEN=1` makes it anyway and refuses when the version it gets is already published. A new file only goes out under a new version (a newer game-data-packager).

### Built from a source package or a script

A licensed source package lives in its game's bundle, `gamedb/games/<id>/licensed/<pkg>/` (licensed by its place: text only, never public). Its tarball is made by its `orig.mk` or the builder script there (the upstream archive downloaded once and checked, the listed files with a fixed owner and mtime, `gzip -n`), so the same files give the same deb for a given commit; `make <pkg>-src` and `make <pkg>-deb` (indep builder) build it. A builder script without a `package.conf` (`builder = "script"`) makes the deb itself from the owner's own copy. As with game-data-packager's output, once a version is published, never build it again under that version: take the published file.

## Migration from the old image scripts

`ps3-system` and `ps3-desktop` took over the files `system/image/customize.sh` and `image/desktop-setup.sh` used to write. Their preinsts archive each legacy file that no package owns to `/var/backups/ps3-legacy-{system,desktop}-<date>.tar` and remove it (and its enable links). `ps3-migrate-home` deletes home-directory copies identical to the packaged defaults and moves the rest to `~/.local/share/ps3-legacy/`. Done on the PS3 on 2026-10-16.

## History

- 2026-10-16: the Debian-source-package pipeline replaced the old kernel.org tarball one; the repository got its `ps3` and `ps3-dev` suites; deploys got `KERNELS=1` after df50edd.
- 2026-10-17: `ps3-licensed` split off; [ClassiCube](../gamedb/games/classicube/)'s data split from the engine (`+ps3.366`).
- 2026-10-18: this Petitboot found to ignore `default=` in kboot.conf; the installed boot menu logic is in [GAMEOS-HDD.md](GAMEOS-HDD.md).
- 2026-10-21: walnut.nanobyte.dev's suites, built and published by GitHub Actions (docs/release/ALPHA.md M6).
- 2026-10-26: the LAN repository on tibi-server retired and deleted (its `make repo-snapshot` image snapshots and the `deps` component with it); licensed packages go to walnut.nanobyte.dev's `licensed` suite, `make deploy` serves `build/debs` itself. The tree reorganized: packages under `packages/` by kind (docs/LAYOUT.md).

Older versions and the dated history: [archive/PACKAGING.md](archive/PACKAGING.md), [archive/PLAN.md](archive/PLAN.md) ("Base and toolchain", "Build setup", "Earlier status (2026-10-16)").
