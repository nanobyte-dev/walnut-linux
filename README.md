# PS3 Linux + RSX

Modern Debian (ppc64, debian-ports) on a CFW PS3 via OtherOS++/Petitboot, with the goal of running
nouveau's userspace (libdrm + Mesa nv30) on the RSX. Plan and background: `docs/`.

## Layout

The tree is organized by output package: each top-level package directory holds what goes into that
package (being converted to Debian source packages, docs/PLAN.md).

| Path | What |
|---|---|
| `linux/` | The kernel: `patches/series` (PS3 patches applied in order: `ps3/` from psx-place, `local/` ours), `config/*.config` fragments merged onto `ps3_defconfig` |
| `ps3rsx/` | The RSX DRM driver (`src/`, registers as "nouveau": render ioctls, validator, software context switch, KMS, VRAM swap) and the GPU probe modules (`probes/`) |
| `ps3-gpu-tools/` | Programs run on the PS3: raw RSX probes and driver tests (`src/`: `rsx-*`, `rsxdrm-smoke`), scripts (`scripts/`: `rsx-suite.sh`, `rsx-x.sh`, …), offscreen GL tests compared with softpipe (`mesa-tests/`) |
| `mesa/` | Our Mesa: Debian's `mesa` source package (`package.conf`: version and .dsc hash) plus `patches/` (our driver changes, a quilt series added as `debian/patches/ps3/`) and `packaging/` (changes to `debian/` itself: nouveau + softpipe only, no LLVM, on ppc64) |
| `ps3-spu/` | SPE test programs and `spurun` (runs SPU ELFs via spufs); packages `ps3-spu` and `ps3-spu-programs` |
| `ps3-boot/` | `ps3-update-kboot`: Petitboot's boot menu from the installed kernels |
| `classicube/` | ClassiCube: `package.conf` (pinned upstream tarball plus texpack/audio components), `debian/` (launcher, defaults), `sounds.sh` (sounds/music from Mojang by SHA-1) |
| `ps3rsx/` | The RSX DRM driver (`src/`) and probe modules (`probes/`); `debian/` builds `ps3rsx-modules-<release>` against our kernel |
| `ps3-system/` | System configuration package: networking, SSH, swap, ps3gpu nodes, DS3 pairing, apt sources and pins (`files/`, `debian/`) |
| `ps3-desktop/` | Desktop package: autologin, `ps3-session`, game mode, Openbox/tint2/GTK/PCManFM defaults in `/etc/xdg/ps3`, X config |
| `image/` | The root filesystem image: `packages.txt` (slow Debian layer), `packages-ours.txt` (ours, installed with apt), `deps.txt` (Debian packages in our repository), `customize.sh` (per-installation settings: hostname, user, key, locale) |
| `spu-toolchain/` | The spu-elf cross toolchain's patches: GCC 16.2 port (`gcc/`), binutils 2.47 fix, newlib 4.6 fixes, test scripts |
| `sysroot/` | ppc64 sysroot contents (for the cross builds of ClassiCube and the GL tests) |
| `games/openarena/` | OpenArena benchmark demo and scripts |
| `config.mk` | All build/deploy settings (versions, hashes, snapshot date, IPs, kernel cmdline) |
| `docker/` | Pinned build environments: `Dockerfile` (toolchain, mmdebstrap), `.mesa` (cross compilers and the sysroot for ClassiCube and the GL tests), `.games`, `.spu`, `.deb` (Debian packaging tools, aptly) |
| `tools/` | Scripts called by the Makefile (`deb-*.sh`: Debian source packages; `repo-*.sh`, `aptly-publish.sh`: the repository; `deploy.sh`) |
| `docs/` | `PLAN.md` the plan, `DRIVER-PLAN.md` GPU driver phases, `RSX.md` measured GPU facts, `RSX-PROGRAMS.md`, `MESA-METHODS.md`, `RESEARCH.md`, `SPU-GCC.md`, `KERNEL-MEMORY.md`, `HARDWARE-LEVERAGE.md`, `RSX-SPU-FEASIBILITY.md`, `SPU-OFFLOAD*.md` |
| `build/` | Generated (ignored). Ephemeral except `build/aptly` (the published repository's state); see CLAUDE.md |
| `firmware/` | Petitboot image installed on the console (ignored) |
| `backups/` | **Private**: NOR flash dump (ignored, never commit) |

## Usage

    make rootfs            # the whole image (our packages included: kernel, RSX driver, Mesa, games)
    make checksums         # compare between builds to confirm reproducibility
    make nfs-root          # sudo: install the image as the NFS export (old root kept as .bak-<date>)
    make usb-root          # or: the image onto the USB disk on the PS3 (FORMAT=1 partitions it first)
    make listen            # in another terminal: PS3 debug output
    make netboot           # PS3 in Petitboot -> boots our kernel with NFS root (ROOT=usb: USB disk root)
    make debs              # our own packages -> build/repo
    make mesa-deb          # Mesa from Debian's source + mesa/patches, native ppc64 build under qemu
                           #   (make chroot first: the build chroot) -> build/debs/mesa
    make deploy            # install with apt on the running PS3: upgrades ours, installs PKGS
                           #   (top-level names; AUTOREMOVE=1 purges what nothing needs any more)
    make publish           # publish to the repository server (DEBS=..., DIST=dev for test builds)
    make spu-toolchain     # spu-elf GCC 16.2 (our port) for the SPEs

The image builds in two layers. `build/rootfs/base.tar` has the Debian packages (dpkg under qemu,
about 15 minutes; downloads cached in `build/apt-cache/`) and is rebuilt only when
`image/packages.txt` or the snapshot changes. `build/rootfs/debian-ppc64.tar` adds our config and
our own packages, in a minute or two. Everything we build is a .deb in `build/repo` or
`build/debs/<pkg>`:

| Package | Installs |
|---|---|
| `ps3-kernel-<release>` | `/boot/vmlinux-<release>`, `/lib/modules/<release>`, its boot arguments |
| `ps3-boot` | `ps3-update-kboot`: Petitboot's `/etc/kboot.conf` from the installed kernels (newest first, the default) |
| `ps3-gpu-tools` | RSX probes, driver tests and the GL tests in `/usr/lib/ps3/bin` (on `PATH`) |
| `mesa-libgallium`, `libgl1-mesa-dri`, `libegl-mesa0`, `libglx-mesa0`, `libgbm1`, … | Debian's Mesa packages rebuilt from `mesa/` (`26.2.4-1+ps3.N`), replacing Debian's |
| `ps3-mesa` | transitional, empty: removes the old `/usr/lib/ps3-mesa` |
| `classicube` | `/usr/games/classicube` (game data per user in `~/.local/share/classicube`) |
| `ps3-spu`, `ps3-spu-programs` | `spurun`, spufs mounted at `/spu`; the SPU test programs |
| `ps3rsx-modules-<release>`, `ps3rsx-modules-ps3` | the RSX driver (loaded at boot) and probe modules for a kernel; the meta follows the current kernel |
| `ps3-system`, `ps3-desktop` | the system and desktop configuration (see the layout above) |

The image installs them with apt, and `make deploy` installs the same packages on the running
PS3. So whatever is tried live is what the next image gets. On the PS3, apt sees Debian (live
debian-ports) and our repository: `apt install`/`apt upgrade` work as on Debian, while the pins in
`image/apt/ps3.pref` keep our packages on top and Debian's kernels and Mesa out.
`make nfs-root` carries over the PS3's own state from the previous root: SSH host keys, Bluetooth
pairings, saved Wi-Fi connections (`FRESH=1` starts clean).

On the PS3 every program uses our Mesa (it replaces Debian's packages); `egl-gears` etc. print a checksum to compare with
a softpipe run (`LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe`).

    sudo modprobe ps3rsx              # on the PS3 (loaded at boot anyway)
    rsx-suite.sh all            # regression: smoke, Mesa ladder, concurrent processes
    rsx-x.sh 30 glxgears -info  # Xorg (modesetting + glamor) on the RSX, one client
    tools/webcam-grab.sh   # on the PC: photo of the PS3's screen -> build/tmp/screen.jpg

## Reproducibility

Kernel and Mesa tarballs are checked against `KERNEL_SHA256`/`MESA_SHA256`; Debian packages come from snapshot.debian.org
at `DEBIAN_SNAPSHOT`; the builder image is pinned by digest with apt on the same snapshot;
builds use a fixed `SOURCE_DATE_EPOCH`, build user/host and no git version suffix.

Petitboot: `firmware/petitboot/dtbImage.ps3.bin` from github.com/xXEvilnatXx/xai_plugin
(`Petitboot/NOR`), MD5 `ca5e9520e03066290f68f67e9167c0fa`.
