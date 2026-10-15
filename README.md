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
| `mesa/` | Our Mesa changes: `fork/` holds every Mesa file we change, whole, copied over the release tarball (becoming a patch series on Debian's mesa) |
| `ps3-spu/` | SPE test programs and `spurun` (runs SPU ELFs via spufs) |
| `ps3-boot/` | `ps3-update-kboot`: Petitboot's boot menu from the installed kernels |
| `classicube/` | ClassiCube: pinned cross build, sounds/music from Mojang by SHA-1, launcher and defaults |
| `image/` | The root filesystem image: `packages.txt` (slow layer), `customize.sh` and `desktop-setup.sh` (setup), `ds3-pair` |
| `spu-toolchain/` | The spu-elf cross toolchain's patches: GCC 16.2 port (`gcc/`), binutils 2.47 fix, newlib 4.6 fixes, test scripts |
| `sysroot/` | ppc64 sysroot contents and meson cross file (for the cross builds of Mesa and ClassiCube) |
| `games/openarena/` | OpenArena benchmark demo and scripts |
| `config.mk` | All build/deploy settings (versions, hashes, snapshot date, IPs, kernel cmdline) |
| `docker/` | Pinned build environments: `Dockerfile` (toolchain, mmdebstrap), `.mesa`, `.games`, `.spu`, `.repo` |
| `tools/` | Scripts called by the Makefile |
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
    make debs              # our packages -> build/repo (an apt repository)
    make deploy            # install them on the running PS3 (PKGS="ps3-mesa ..."; or the
                           #   shortcuts kernel-, gpu-, mesa-, classicube-, spu-deploy)
    make spu-toolchain     # spu-elf GCC 16.2 (our port) for the SPEs

The image builds in two layers. `build/rootfs/base.tar` has the Debian packages (dpkg under qemu,
about 15 minutes; downloads cached in `build/apt-cache/`) and is rebuilt only when
`image/packages.txt` or the snapshot changes. `build/rootfs/debian-ppc64.tar` adds our config and
our own packages, in a minute or two. Everything we build is a .deb in `build/repo`:

| Package | Installs |
|---|---|
| `ps3-kernel-<release>` | `/boot/vmlinux-<release>`, `/lib/modules/<release>`, its boot arguments |
| `ps3rsx-<release>` | RSX driver for that kernel, loaded at boot |
| `ps3-boot` | `ps3-update-kboot`: Petitboot's `/etc/kboot.conf` from the installed kernels (newest first, the default) |
| `ps3-gpu-tools` | RSX probes and tests in `/usr/lib/ps3/bin` (on `PATH`) |
| `ps3-mesa` | our Mesa in `/usr/lib/ps3-mesa`, plus its GL tests |
| `classicube` | `/usr/games/classicube` (game data per user in `~/.local/share/classicube`) |
| `ps3-spu` | `spurun` and SPU tests; spufs mounted at `/spu` |

The image installs them with dpkg, and `make deploy` installs the same packages on the running
PS3. So whatever is tried live is what the next image gets.
`make nfs-root` carries over the PS3's own state from the previous root: SSH host keys, Bluetooth
pairings, saved Wi-Fi connections (`FRESH=1` starts clean).

On the PS3, `. /usr/lib/ps3-mesa/env.sh` selects our Mesa for programs run from a shell (the desktop uses it through ld.so.conf); `egl-gears` etc. print a checksum to compare with
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
