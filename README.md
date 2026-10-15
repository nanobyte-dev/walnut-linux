# PS3 Linux + RSX

Modern Debian (ppc64, debian-ports) on a CFW PS3 via OtherOS++/Petitboot, with the goal of running
nouveau's userspace (libdrm + Mesa nv30) on the RSX. Plan and background: `docs/`.

## Layout

| Path | What |
|---|---|
| `config.mk` | All build/deploy settings (versions, hashes, snapshot date, IPs, kernel cmdline) |
| `kernel/patches/series` | PS3 kernel patches, applied in order to vanilla Linux |
| `kernel/config/*.config` | Config fragments merged onto `ps3_defconfig` |
| `rootfs/packages.txt`, `rootfs/customize.sh` | Debian root filesystem: packages (the image's slow layer) and setup (fast layer) |
| `docker/Dockerfile` | Pinned build environment (toolchain, mmdebstrap) |
| `docker/Dockerfile.mesa` | Mesa build environment on top of it (C++ cross compiler, meson) |
| `mesa/fork/` | Our fork of Mesa's nouveau driver (and the few loader/dri/gbm files we change), copied over the release tarball; edit directly |
| `mesa/sysroot-packages.txt`, `mesa/cross-ppc64.ini` | Debian ppc64 sysroot contents and meson cross file |
| `mesa/tests/` | Offscreen tests compared with softpipe: EGL/GLES2 (`egl-*`), desktop GL fixed function (`gl-fog`, `gl-sky`) |
| `games/` | Game builds and defaults: `classicube/` (pinned cross build, sounds/music from Mojang by SHA-1) |
| `rootfs/desktop-setup.sh`, `rootfs/ds3-pair` | Desktop session, game defaults, `ps3-game` wrapper; DualShock 3 USB-plug pairing |
| `gpu/ps3rsx/` | The DRM driver (registers as "nouveau"): render ioctls, validator, software context switch, KMS |
| `tools/` | Scripts called by the Makefile |
| `gpu/tools/` | Programs and scripts run on the PS3: raw RSX probes (`rsx-*`), driver tests (`rsxdrm-smoke`), `rsx-suite.sh` (smoke, Mesa vs softpipe, concurrent GL), `rsx-x.sh` (Xorg with one client) |
| `docs/` | `PLAN.md` phases, `DRIVER-PLAN.md` the GPU driver plan, `RSX.md` measured GPU facts, `RSX-PROGRAMS.md` raw draw/shader words, `MESA-METHODS.md` Mesa nv30 methods vs RSX, `RESEARCH.md` background |
| `build/` | Generated outputs (ignored); `build/old/` holds previous outputs, never deleted |
| `firmware/` | Petitboot image installed on the console (ignored) |
| `backups/` | **Private**: NOR flash dump (ignored, never commit) |

## Usage

    make rootfs            # the whole image (kernel modules, RSX driver, Mesa, games included)
    make checksums         # compare between builds to confirm reproducibility
    make nfs-root          # sudo: install the image as the NFS export (old root kept as .bak-<date>)
    make usb-root          # or: the image onto the USB disk on the PS3 (FORMAT=1 partitions it first)
    make listen            # in another terminal: PS3 debug output
    make netboot           # PS3 in Petitboot -> boots our kernel with NFS root (ROOT=usb: USB disk root)
    make gpu-deploy        # quick updates of the live root while iterating: RSX driver + tools
    make mesa-deploy       #   -> ~/bin, Mesa + mesa/tests -> ~/mesa (stop X first),
    make classicube-deploy #   ClassiCube -> ~/classicube, kernel modules (make nfs-modules)

The image builds in two layers. `build/rootfs/base.tar` has the Debian packages (dpkg under qemu,
about 15 minutes; downloads cached in `build/apt-cache/`) and is rebuilt only when
`rootfs/packages.txt` or the snapshot changes. `build/rootfs/debian-ppc64.tar` adds our config and
everything we build, in a minute or two. The deploy targets run the same install steps as the
image (`tools/install-builds.sh`), so whatever is tried live is also what the next image gets.
`make nfs-root` carries over the PS3's own state from the previous root: SSH host keys, Bluetooth
pairings, saved Wi-Fi connections (`FRESH=1` starts clean).

On the PS3, `. ~/mesa/env.sh` selects our Mesa; `egl-gears` etc. print a checksum to compare with
a softpipe run (`LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe`).

    sudo modprobe drm_kms_helper && sudo insmod ~/bin/ps3rsx.ko   # on the PS3
    ~/bin/rsx-suite.sh all            # regression: smoke, Mesa ladder, concurrent processes
    ~/bin/rsx-x.sh 30 glxgears -info  # Xorg (modesetting + glamor) on the RSX, one client
    tools/webcam-grab.sh   # on the PC: photo of the PS3's screen -> build/tmp/screen.jpg

## Reproducibility

Kernel and Mesa tarballs are checked against `KERNEL_SHA256`/`MESA_SHA256`; Debian packages come from snapshot.debian.org
at `DEBIAN_SNAPSHOT`; the builder image is pinned by digest with apt on the same snapshot;
builds use a fixed `SOURCE_DATE_EPOCH`, build user/host and no git version suffix.

Petitboot: `firmware/petitboot/dtbImage.ps3.bin` from github.com/xXEvilnatXx/xai_plugin
(`Petitboot/NOR`), MD5 `ca5e9520e03066290f68f67e9167c0fa`.
