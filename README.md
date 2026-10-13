# PS3 Linux + RSX

Modern Debian (ppc64, debian-ports) on a CFW PS3 via OtherOS++/Petitboot, with the goal of running
nouveau's userspace (libdrm + Mesa nv30) on the RSX. Plan and background: `docs/`.

## Layout

| Path | What |
|---|---|
| `config.mk` | All build/deploy settings (versions, hashes, snapshot date, IPs, kernel cmdline) |
| `kernel/patches/series` | PS3 kernel patches, applied in order to vanilla Linux |
| `kernel/config/*.config` | Config fragments merged onto `ps3_defconfig` |
| `rootfs/packages.txt`, `rootfs/customize.sh` | Debian root filesystem contents and setup |
| `docker/Dockerfile` | Pinned build environment (toolchain, mmdebstrap) |
| `tools/` | Scripts called by the Makefile |
| `gpu/tools/` | RSX test programs run on the PS3 (`rsx-clear`, `rsx-probe`, `rsx-peek`) |
| `docs/` | `PLAN.md` phases, `RESEARCH.md` background, `RSX.md` measured GPU facts |
| `build/` | Generated outputs (ignored); `build/old/` holds previous outputs, never deleted |
| `firmware/` | Petitboot image installed on the console (ignored) |
| `backups/` | **Private**: NOR flash dump (ignored, never commit) |

## Usage

    make kernel rootfs     # build (in the pinned container)
    make checksums         # compare between builds to confirm reproducibility
    make nfs-root          # sudo: install rootfs as NFS export
    make listen            # in another terminal: PS3 debug output
    make netboot           # PS3 in Petitboot -> boots our kernel with NFS root
    make gpu-deploy        # cross-compile gpu/tools and copy them to ~/bin on the NFS root

## Reproducibility

Kernel tarball is checked against `KERNEL_SHA256`; Debian packages come from snapshot.debian.org
at `DEBIAN_SNAPSHOT`; the builder image is pinned by digest with apt on the same snapshot;
builds use a fixed `SOURCE_DATE_EPOCH`, build user/host and no git version suffix.

Petitboot: `firmware/petitboot/dtbImage.ps3.bin` from github.com/xXEvilnatXx/xai_plugin
(`Petitboot/NOR`), MD5 `ca5e9520e03066290f68f67e9167c0fa`.
