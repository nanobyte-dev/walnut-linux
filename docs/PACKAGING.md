# Packaging and the apt repository

How our software becomes Debian packages, how they reach the PS3, and the rules that keep it
safe. Decided 2026-10-16 (plan: Debian source packages, a served repository, the repository
organized by output package).

## Kinds of source packages

Each package is a top-level directory with a `package.conf` (read by `tools/deb-*.sh`):

| `TYPE` | What it is | Examples | Version |
|---|---|---|---|
| `debsrc` | Debian's source package at the snapshot, plus our quilt series (`<pkg>/patches`, added as `debian/patches/ps3/`) and changes to `debian/` itself (`<pkg>/packaging`) | `mesa`, `linux` | `<debver>+ps3.N` |
| `native` | Our own code: the directory is the source (`debian/` inside) | `ps3-boot`, `ps3-gpu-tools`, `ps3-spu`, `ps3rsx`, `ps3-system`, `ps3-desktop` | `0.N` (`ps3rsx`: `0.N+k<kernel release>`) |
| `upstream` | Software Debian lacks: an upstream tarball plus component tarballs | `classicube` | `<upver>-1+ps3.N` |

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
`make <pkg>-deb` (build into `build/debs/<pkg>`), `make packages` (everything).

## The repository

- Server: tibi-server, `http://10.0.0.10:8070/` (`tools/repo-server/`), static files from aptly's
  published tree. Two suites (decided with the user 2026-10-16, like Debian's unstable and
  experimental), each in its own prefix and pool so pruning one can't touch the other:
  - `ps3/`, suite `ps3`: stable releases (components `main` and `deps`), kept forever, versions
    only grow, `~dev` versions refused;
  - `ps3-dev/`, suite `ps3-dev`: dev builds with their debug symbols, the newest 5 versions of
    each package kept (`KEEP`), older ones pruned on every dev publish;
  - `ps3-dbgsym/`: debug symbols of stable releases, also the newest 5.
- Promotion: commit, rebuild (versions `0.N`, `...+ps3.N`, no `~dev`), `make publish`. The builds
  are reproducible, so apart from the version the contents are what was tested from `ps3-dev`.
- `make publish [DEBS=...] [DIST=dev]` (`tools/repo-publish.sh`, `tools/aptly-publish.sh`): staging
  copies, a version check (official versions only grow), aptly add and publish, an add-only sync
  to the server that refuses to change any published file, indices last, then the aptly database
  backed up to the server. `build/aptly` is the one part of `build/` that is never deleted.
- `make deps` refreshes the `deps` component: Debian packages from the snapshot listed in
  `image/deps.txt` (ones ours newly depend on, and Debian's Mesa set for rollback).
- Debug symbols (`*-dbgsym`) go to `ps3-dbgsym` only, never into deploys or images.

## On the PS3

`ps3-system` ships the apt configuration (`ps3-system/files/apt/`):

- Debian ports, live (`deb.debian.org/debian-ports`, unstable and unreleased): anything we don't
  build installs and upgrades from Debian.
- Our repository, `Trusted: yes` on the LAN.
- Pins (`/etc/apt/preferences.d/ps3`): the stable suite at 990, so our releases win over Debian's
  (500) whatever the versions, but an installed newer dev build is never downgraded (below 1000):
  the next release, a higher version, takes over from it. The dev suite at 1: never chosen on its
  own, only with `apt install -t ps3-dev <pkg>` (what `make deploy` does). Going back to an older
  version is explicit (`apt install pkg=<version>`). Debian's `linux-image-*`, `linux-headers-*`, `linux-kbuild-*`, `linux-support-*`
  and `src:mesa` at -1, so they are never candidates. `apt upgrade` is safe: a Debian package
  that needs a newer Mesa than ours is kept back, nothing is removed. Verified in the ppc64 chroot
  against live debian-ports (2026-10-16). The same version in `deps` and Debian counts as one, so
  the -1 also blocks `deps`' copy of Debian's Mesa: a rollback to Debian's Mesa means dropping
  the pin by hand.
- New kernels arrive through meta packages (`ps3rsx-modules-ps3`; the kernel's
  `linux-image-ps3` from milestone 4).

## Deploying and the image

- `make deploy [PKGS=...] [AUTOREMOVE=1]` (`tools/deploy.sh`): publishes every local build
  (debug symbols included) to `ps3-dev`, refreshes only our lists on the PS3 (Debian's are ~100
  MB), then with `-t ps3-dev`: upgrades every installed package of ours that has a newer build,
  installs the named top-level packages, upgrades again; each step simulated first;
  `--no-remove`, except removals named explicitly as `name-` in `PKGS` (and then exactly those).
  It brings its own copy of the sources and pins, so it works before the PS3 has them. Runs
  `ps3-migrate-home` once.
- The image (`tools/build-image.sh`): the Debian-only base layer, then our packages installed by
  apt from a flat repository of our debs (`image/packages-ours.txt`), our Mesa replacing Debian's
  as an upgrade, then `autoremove --purge` (LLVM, z3, sensors go), then `image/customize.sh`
  (hostname, fstab, user, key, locale).

## Migration from the old image scripts

`ps3-system` and `ps3-desktop` took over the files `image/customize.sh` and
`image/desktop-setup.sh` used to write. Their preinsts archive each legacy file that no package
owns to `/var/backups/ps3-legacy-{system,desktop}-<date>.tar` and remove it (and its enable
links); `ps3-migrate-home` deletes home-directory copies identical to the packaged defaults and
moves the rest to `~/.local/share/ps3-legacy/`. Done on the PS3 on 2026-10-16.
