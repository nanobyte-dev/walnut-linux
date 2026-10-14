# Plan: nouveau on the PS3 RSX

Goal: run the existing nouveau userspace (libdrm_nouveau + Mesa's nv30 driver) on the PS3's RSX, writing as little new code as possible. Optimisation and bug-fixing come later.

Hard constraints are in CLAUDE.md (no permanent changes beyond installing Linux, GameOS stays the default boot OS).

## Long-term goal (stated 2026-10-14)

A live environment for the PS3 that boots from USB or over NFS through Petitboot, with the
RSX driver and the desktop (docs/DESKTOP-PLAN.md) working out of the box, plus an optional
installer that installs it permanently (to the OtherOS++ Linux region of the HDD, within the
safety rules in CLAUDE.md). The NFS netboot used for development is the first form of it.

## Deliverable

`make iso` produces a bootable image that can be dd-ed to a flash drive and booted from Petitboot.
Development and testing use netboot (Petitboot -> kexec our kernel, root on NFS from the dev PC).
Kernel and rootfs are shared between both; only the final packaging differs.
Open for later: boot files must be readable by Petitboot's 2.6.30 kernel (ISO9660/ext2/FAT);
raw ISO vs partitioned/hybrid image; live (read-only) vs writable root.

## Approach

Nouveau's kernel driver can't run as-is. It expects a PCI card it initialises itself, while on the PS3 the hypervisor (lv1) owns the RSX and hands Linux a GPU context through hypercalls.

So: a thin PS3 DRM driver on top of the lv1 GPU calls that exposes nouveau's ioctl interface (channel alloc, buffer alloc in VRAM/XDR, pushbuf submit, fences). Mesa nv30 then runs on top with minimal changes.

Starting point: mainline `ps3fb` and `ps3vram` already allocate an lv1 GPU context and submit FIFO commands to the RSX.

## The console

CECH-20xx (DYN-001, first Slim; from IDPS model byte 0x09, confirm with sticker), NOR flash, 4.92 CEX Cobra 8.5D,
webMAN MOD 1.47.48 at http://10.0.0.76/. Screen visible via the Logitech BRIO pointed at the monitor (HDMI capture is blocked by HDCP in the XMB).

## Phase 0: Linux on the PS3 (DONE 2026-10-14)

Debian sid ppc64 boots via `make netboot` with root on NFS; systemd healthy, SSH as tibi with ~/.ssh/id_ps3.
Gotchas found on the way (details in git log): kexec must go through Petitboot's `reboot` path,
kernel must be ELFv1, no gelic early debug, and a local vDSO getrandom fix for BE ELFv1.


1. Confirm OtherOS++ supports the CECH-20xx (psdevwiki lists all fats and only some Slims). Stop if it doesn't.
2. Dump the full NAND/NOR flash, verify it (e.g. PyPS3checker), keep copies on the dev PC. Nothing touches flash before this.
3. Install OtherOS++ and Petitboot following psdevwiki for this exact model. Set up the HDD region for Linux.
4. Install a distro: Debian ppc64 (debian-ports) or Gentoo (PS3 overlay + Cell binhost). Enable SSH.
5. Set up webMAN MOD so the dev PC can launch the "boot OtherOS" app over HTTP.

## Phase 1: does OtherOS++ expose the 3D engine? (YES, 2026-10-14)

`gpu/tools/rsx-clear` takes over ps3fb's FIFO via /dev/ps3gpu_* (patch 0030 + local/0002 mmap fix) and
issues NV40 3D methods on subchannel 0 (object 0x31337000, bound by ps3fb's reset program): surface
setup + CLEAR_SURFACE turned the whole 1080p screen red, no graphics exception. Linux has the 3D engine.


Small kernel module that binds the 3D class in our lv1 GPU context and clears the screen to a colour. Check with the capture dongle and by reading the framebuffer back over SSH.

- Works: Phase 2 is engineering.
- Fails: work out what the hypervisor blocks before going further.

## Phase 2: nouveau-compatible shim

Detailed plan: docs/DRIVER-PLAN.md. A DRM driver `ps3rsx` (registers as "nouveau") on lv1 GPU
calls, plus a small Mesa nv30 patch for the hypervisor's fixed object handles, up to glxgears
in Xorg. **Status (2026-10-14): every phase done; glxgears runs in Xorg on the RSX** (glamor on
NV47, GL 2.1, 60 fps vsync-paced), checked with `glxinfo -B`, `xwd` and the webcam. kmscube runs
at 60 fps through KMS. Results per phase: docs/DRIVER-PLAN.md "Results"; GPU facts found on the
way: docs/RSX.md. Next: the "Later" table in DRIVER-PLAN.md (pushing the hardware limits).

## Test loop

- Getting to Linux needs the user: fan on SYSCON in webMAN, set the OtherOS flag in the XMB,
  Petitboot comes up (at PETITBOOT_IP), then `make netboot`. Every reboot lands in GameOS.
- Module and userspace changes: `make gpu-deploy`, then `insmod`/run over SSH. No reboot.
- Kernel changes need a reboot (kexec from our Linux crashed the console), so they are batched.
- A graph exception wedges the GPU until a console reboot: risky tests go last, one per boot.
- Logs: netconsole (`make listen`); the PS3's journal is on the NFS root.
- Screen: BRIO webcam pointed at the monitor, frames grabbed with ffmpeg.
- Keep netconsole quiet: loglevel=4, and never the driver's `trace=1` through a GL program (the
  volume hung the PS3). Mesa's `NOUVEAU_LIBDRM_DEBUG=1` dumps pushes in userspace instead.
- Swap is zram: a swapfile on the NFS root deadlocked under memory pressure (write error -12).
- Programs that wait for a key (modetest, kmscube) need stdin held open over SSH (`sleep N |`),
  or they quit at once.

## Build setup

- Kernel: Fedora cross compiler `gcc-powerpc64-linux-gnu`.
- Userland (libdrm, Mesa, test programs): ppc64 BE root on the dev PC run through `qemu-user-static`. It can double as the PS3's NFS root to spare its 256 MB RAM.

## Side project: the SPEs (stated 2026-10-14)

Linux gets 6 SPEs (`spufs`), and nothing uses them yet. Uses later: vertex conversion for Mesa on
big-endian, zram compression, video decode. Toolchain first:
- Fallback: pinned spu-elf toolchain in Docker (GCC 9.5, the last release with the SPU port,
  plus matching binutils/newlib), proven by a hello world on one SPE.
- To evaluate: forward-porting GCC's SPU target (removed in GCC 10) to current GCC.
  LLVM's CellSPU backend was removed in 3.3 (2012) and was experimental; not an option.

## Open questions

- Does the PS3 Petitboot build support network boot?
- Does HDMI capture work under Linux, or does HDCP blank it?
- Does Remote Start (Remote Play settings) respond to a Wake-on-LAN packet? Would solve remote power-on.
- Exactly how much RSX access OtherOS++ gives (psdevwiki lv1 / OtherOS++ pages, René Rebe's driver work).
