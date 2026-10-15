# Plan: modern Linux with a working GPU on the PS3

Hard constraints are in CLAUDE.md (no permanent changes beyond installing Linux, GameOS stays the
default boot OS, no syscon writes, supervised sessions). The original wish list is `backlog.txt`.
Restructured 2026-10-15 evening around what the day established: **RAM, not the GPU or the CPU,
is the binding constraint**, and most of the next work is about it.

## Base and toolchain (decided 2026-10-15)

- **Base:** a pinned Debian sid snapshot (`DEBIAN_SNAPSHOT`, now 2026-10-13) is our release.
  Debian has no stable release for ppc64 (big-endian), only debian-ports unstable, so a frozen
  snapshot is the closest thing to an LTS. It moves deliberately, at the latest when Debian 14
  is released, and never piecemeal.
- **One compiler version everywhere: GCC 16.2,** the snapshot's system compiler.
  - **PPE** (kernel, Mesa, ps3rsx, games): Debian's `gcc-16-powerpc64-linux-gnu`
    16.2.0-3cross1, from the amd64 sid snapshot of the same date.
  - **SPU:** our port of the SPU backend to GCC 16.2 (docs/SPU-GCC.md, `spu/port/`).
  - **binutils:** 2.47 for the PPE. The SPU still uses 2.33.1 until the `hbrr` regression is
    fixed (agent at work, task #6). **newlib** for the SPU: 4.6.0 (seven patches,
    `spu/newlib-4.6/`), builds; not yet run on an SPE (task #7).
- **Kernel:** 6.12 LTS (supported to Dec 2028). **Mesa:** our fork of the snapshot's release.
- **Repository:** everything of ours is a .deb in `build/repo`; the image installs from it and
  `make deploy` upgrades the running PS3. Source: git.tibich.com/chibicitiberiu/ps3-linux.

## Long-term goal (stated 2026-10-14)

A live environment for the PS3 that boots from USB or over NFS through Petitboot, with the RSX
driver, desktop and games working out of the box, plus an optional installer that installs it
permanently (to the OtherOS++ Linux region of the HDD, within the safety rules).

## Where we are (2026-10-15)

- **Boot:** the image lives on a USB disk; Petitboot boots it on its own from `/etc/kboot.conf`
  (written by `ps3-update-kboot`, one entry per installed kernel, newest the default, the older
  ones as fallbacks). NFS netboot still works for development (`make netboot`).
- **GPU:** `ps3rsx` (registers as "nouveau") runs Mesa nv30 from our fork: Xorg with glamor,
  GL games, KMS with real vsync. The GART is dynamic (CMA, 1 MB chunks on demand). Details:
  DRIVER-PLAN.md, RSX.md.
- **Desktop:** Openbox + tint2 + PCManFM, NetworkManager (never on eth0), pnmixer, dunst; Shut
  down / Reboot in the menu. The user D-Bus runs under systemd (`dbus-user-session`).
- **Game mode:** every game starts through `ps3-game`, which keeps the game and X resident
  (cgroup `memory.low`), freezes the desktop helpers, gvfs, udisks2 and polkit, and pushes their
  memory out; all of it thaws when the game exits.
- **Swap:** `ps3vswap0`, a ps3rsx block device that keeps swapped pages in spare VRAM (up to
  96 MB), with the USB swap partition behind it. Recently swapped pages stay in VRAM, cold ones
  are demoted to the disk, and games get VRAM back on demand.
- **Games:** ClassiCube locked at 60 fps (1080p, vsync); OpenArena timedemo median 50–60 fps,
  with stalls of up to a second or more still in live play.

## Memory: the central problem

Measured mid-game (OpenArena, 2026-10-15). Reports: KERNEL-MEMORY.md (kernel side, measured),
HARDWARE-LEVERAGE.md (techniques), RSX-SPU-FEASIBILITY.md (VRAM and SPU access paths),
SPU-OFFLOAD-SURVEY.md.

| Of 256 MB XDR | MB | Note |
|---|---:|---|
| Hypervisor (never reaches Linux) | 16 | |
| Kernel image | 21.1 | 6.2 of it alignment padding (`STRICT_KERNEL_RWX`) |
| ps3fb console frame | 9.2 | 1080p; 720p needs 5 |
| `struct page`, hashes, stacks | ~4.7 | |
| Slab, page tables, vmalloc | ~29 | zram table 1.5, seccomp filters 1.0, `cfg80211` 0.5 |
| Kept free (reserve, watermarks) | ~20 | `min_free_kbytes` 8 MB: two hangs without it |
| GART (CMA, on demand) | 6–14 | the rest of the 64 MB CMA area serves normal pages |
| OpenArena | 74 (+10 swapped) | engine data: map, models, decoded sounds, hunk |
| X | 6 (+27 swapped) | 8 MB wallpaper segment, 16 MB heap (glamor/Mesa) |
| Services, desktop helpers, file cache | the rest | helpers frozen in game mode |

Why it hurts so much: whatever doesn't fit comes back from a USB 2 disk whose bridge (JMS567)
speaks only Bulk-Only Transport, one command at a time. A game's page fault queues behind swap
writes; the kernel stacks of blocked threads showed the game faulting its own code back in, and
with the game pinned, X swapping in (X is on every frame's path).

Facts that decide the design (RSX.md, RSX-SPU-FEASIBILITY.md):
- **The PPE must not read VRAM:** 10 MB/s measured. Writes reach 124 MB/s with 8-byte stores;
  uncached stores leave the chip as single transactions of at most 16 bytes, so ~250 MB/s is the
  ceiling. Only 128-byte transfers (SPU DMA, the RSX's own copies) reach GB/s.
- **glibc's memcpy into a VRAM mapping is a trap:** `__memcpy_cell` uses `dcbz`, which faults on
  cache-inhibited memory and is emulated by the kernel per 128-byte line (19 MB/s).
- **The RSX copies fast:** M2MF VRAM→XDR ~600 MB/s, XDR→VRAM ~2 GB/s.
- **VRAM is mostly idle:** X ~19 MB, a game ~70 MB, of ~250 MB.
- **Textures already cost VRAM, not RAM:** Mesa keeps no CPU copy, ioquake3 frees decoded
  images after upload.

Goal (stated 2026-10-15): **a game can use 2/3 of the RAM (~160 MB) without stalls, without
removing features.** The levers, in the order of the queue below: fast swap in VRAM (GPU
copies), a leaner kernel (−8.7 MB measured, −4 MB more at 720p), userspace trims (zram,
wallpaper, unused modules), then per-game settings and, with the SPUs, compute instead of
memory (compressed textures, streamed audio).

## Work queue

Task numbers refer to the session task list. "Reboot" marks steps that need one.

### Now

1. **VRAM swap, GPU copies** (task #13). Swap-in and write-back through M2MF, following the
   design in RSX-SPU-FEASIBILITY.md §3: a fixed 1 MB GART bounce area; the swap path takes the
   ring only with trylocks (`own_lock`, `exec_lock`), backs off while a channel has a primitive
   open, records its M2MF state in `hw_shadow` so the next client restores its own; any failure,
   timeout or wedge falls back to the PPE read, so no swapped page can be lost. Use the GPU when
   it's idle or the request is large (≥ 8 pages); a queued game frame can add 16–33 ms.
   Expected: one page 0.4 ms → ~20 µs, a chunk write-back 100 ms → ~2 ms. Then the OpenArena
   comparison (timedemo + `oa-run.sh` live play) against today's numbers.
   - Done so far: the device, two tiers, games-first VRAM release, async disk I/O, own bio
     reserve, a work queue on a fresh stack, background demotion. Hard-won lessons in
     `gpu/ps3rsx/vswap.c`'s header (two deadlocks and a NULL-bioset oops on the way).
2. **SPU toolchain versions** (tasks #6, #7): binutils 2.47 with the `hbrr` fix (agent), newlib
   4.6 built. Then one `make spu-toolchain` and the 1677 execute tests on the SPEs (1629 pass
   with 1.20/2.33.1 today); hello via `spurun`.

### Next: memory

3. **Release kernel config** (reboot). From KERNEL-MEMORY.md, measured: `STRICT_KERNEL_RWX=n`
   (−6.0 MB of padding), `KALLSYMS_ALL=n` (−1.0 MB, −0.6 MB in modules), drop IPv6, io_uring,
   quota, kexec, hugetlbfs, NFSv4 as a module (−1.0 MB), smaller log buffer, no dynamic debug,
   IKCONFIG or debugfs (−1.1 MB). Total −8.7 MB. Keep `SLUB_DEBUG` (99 KB, gives slabinfo) and
   built-ins (a module costs 1.5–3× its size). Check ps3rsx builds without debugfs. As a new
   kernel package next to the debug one, so Petitboot offers both.
4. **720p console** (`ps3fb=5M`, mode 131; −4 MB). The user decides: X shares the mode, so the
   desktop and games run at 720p too (also 2.25× fewer pixels per frame).
5. **Userspace trims:** zram off now that VRAM swap works (1.5 MB table plus its pages);
   wallpaper out of the 8 MB shared segment (in VRAM, or a solid colour); unload `cfg80211`
   when Wi-Fi is unused; `systemd-networkd` duplicates NetworkManager here; journal size limit;
   the seccomp filters of services that don't need them (1 MB). Re-measure the budget after.
6. **Validator safety:** the vertex-texture methods 0x900–0x97c are allowed with unchecked data;
   a bad setup could wedge the GPU. Check them like the fragment texture units.

### Next: games and CPU

7. **Free per-game experiments:** OpenArena with S3TC (`r_ext_compressed_textures 1`: VRAM and
   GART ÷4–8, slower loads while Mesa compresses on the PPE), `s_khz 11` (halves decoded sound),
   OpenAL int16 and a linear resampler; ClassiCube view distance. Measure each with the
   timedemo and game mode.
8. **OpenArena:** build ioquake3 ourselves (`-mcpu=cell`, pinned, like ClassiCube); load player
   models at map start (`cg_deferPlayers` made no difference: the loads happen when bots
   appear); listen on the TV; DS3 bindings in play.
9. **Driver CPU cost:** `ps3rsx_validate` + `ps3rsx_shadow_write` are ~15% of ClassiCube's CPU;
   Mesa's per-draw state work (fence and BO refs per draw) next.

### Later: SPUs (compute instead of memory)

10. **Probes first** (RSX-SPU-FEASIBILITY.md "Recommended experiments"):
    - E8b: what lv1 keeps of `ioremap_wc` (read the HPTE back), 16-byte stores, pre-touched;
    - E16: SPU DMA puts into a mapped VRAM buffer (expected to work: `bo_mmap` fills the page
      tables at mmap time and spufs resolves faults with `hash_page`); rate and correctness;
    - spufs context-switch and job-dispatch latency (quoted 20–200 µs, never measured here).
11. **libspeq** (task #4): resident SPU workers, a job queue, completion words, stdio via stop
    0x2100 in `spurun`. The base for everything below.
12. **SPU DXT encoder** behind Mesa's S3TC pack: the biggest memory win for textures (÷4–8 in
    VRAM and GART staging) without slow loads. Then **audio**: OpenAL mixing and Vorbis decoded
    per buffer on an SPU (5–8% of OpenArena's CPU, and no decoded PCM held in RAM).
13. If E16 shows GB/s: **SPU decompression and generation straight into VRAM** (assets stay
    compressed in RAM and on disk), and VRAM swap writes through an SPU. ClassiCube chunk
    meshing on SPUs as a showcase.
- Rejected, with reasons in SPU-OFFLOAD-SURVEY.md: kernel-side SPU use (validator, zram, vswap:
  the kernel can't drive SPEs), Mesa per-draw paths, compressed swap via a userspace block
  device (reclaim deadlocks).

### Later: GPU features

Only when content needs them (OpenArena and ClassiCube don't):
- **Vertex texture fetch:** 4 units (0x900+0x20·n), FP32 2D nearest. Mesa's
  `nv40_verttex_validate` is a stub that also reads the fragment samplers (copy-paste bug), the
  vertex compiler emits no TXL, and the sampler count is 0. Needed for meshes from textures.
- **Derivatives:** desktop GLSL `dFdx`/`dFdy` already reach DDX/DDY; only GLES2's
  `OES_standard_derivatives` is off. Enable after an egl-deriv check against softpipe.
- **Instancing:** method 0x1fc0 plus a divisor in VTXFMT bits 16–31; it divides the vertex
  index, there is no instance counter, so GL instancing would loop per instance.
- **Tiling, Z-cull, native 720p:** raise GPU throughput, but frames are PPE-bound today.

### Platform

- **Packaging** (task #11): `ps3-system` and `ps3-desktop` packages for what `customize.sh` and
  `desktop-setup.sh` write today, so fixes go out with `make deploy`, not image reinstalls (the
  game-mode and VRAM-swap setup were applied to the live system by hand-run script excerpts).
- **deb-src packaging** (task #9): build our packages from Debian's source packages with our
  patches, `+ps3.N` versions, so our Mesa replaces Debian's by version (and drops ~210 MB of
  Debian's Mesa/LLVM from the image).
- **ClassiCube** (task #10): default.zip at build time, no first-run download.
- **Reproducibility check:** two image builds, same `make checksums`. Image size: Debian's
  Mesa and LLVM. `make disk-image` (raw `.img.xz`, `systemd-repart` grows it on first boot).
- **DualShock 3:** pairing works (one USB plug); check SDL mappings in play, rumble, LEDs.
- **Fan:** read the thermal sensors under Linux, read-only first. Control needs syscon writes:
  the user's decision.
- **Networking:** test Wi-Fi from nm-applet; NetworkManager could own eth0 now that the root
  isn't on NFS.
- **Distribution:** sign and host the apt repository; the SPU and PPE toolchains could ship
  from it. `make iso` and the installer (OtherOS++ region; ps3da/db/dc never written).
- **Candidate games:** QuakeSpasm, Yamagi Quake II (gl1), Extreme Tux Racer, Neverball,
  Armagetron, Sauerbraten; stretch: SuperTuxKart (GL 2.1 mode, RAM), Luanti, PrBoom+. GL 3-only
  games stay out of reach.

## Done (summary)

- **Image:** reproducible two-layer build (`make rootfs`), pinned inputs, fixed
  `SOURCE_DATE_EPOCH`; packages in `build/repo`; `make usb-root-petitboot` writes the image onto
  the USB disk from Petitboot.
- **Kernels side by side:** `ps3-kernel-<version>-ps3-debug-<date>` packages with their
  `ps3rsx-<release>`; `ps3-boot` writes the boot menu.
- **GPU:** ps3rsx with validator, software context switch, KMS, vsync IRQ; GPU vertex fetch
  (ClassiCube 6.5 → 60 fps); Mesa fixes (fog, banded uploads, staging limits); dynamic GART via
  CMA (`rsxdrm-smoke gart`: 56 MB under memory pressure, data correct).
- **ClassiCube stutters and flashes:** fence polling fast only while someone waits (4000 → 1540
  timer interrupts/s); flip events wait for the flip to latch (no more half-redrawn frames).
- **Game mode** and **VRAM swap** (above).
- **SPU:** GCC 16.2 port, 1629/1677 execute tests on the SPEs; `spurun`; spufs at boot.
- **DualShock 3** pairing over one USB plug.

## Test loop

- **Getting to Linux needs the user:** fan on SYSCON in webMAN, the OtherOS flag in the XMB,
  Petitboot boots the default kernel (or the user picks another). Every reboot lands in GameOS.
  At the end of each session the flag goes back to GameOS.
- **Deploying:** `make debs` then `make deploy PKGS="…"` (kernel packages install next to the
  running one). ps3rsx can only be reloaded with X stopped (`systemctl stop getty@tty1`, then
  start it again) and, with VRAM swap on, after `swapoff /dev/ps3vswap0`.
- **Kernel changes need a reboot;** batch them. kexec from our Linux crashed the console.
- **A graph exception wedges the GPU until a console reboot.** Risky tests go last.
- **Testing block or memory code:** raise the console log level (`echo 7 >
  /proc/sys/kernel/printk`) so a hang prints full stacks over netconsole; put every SSH command
  under `timeout`; verify data with a pattern (write, drop caches, read back) before using a
  device as swap, then under a memory hog, then in a game.
- **Observing:** netconsole (`make listen`); the BRIO webcam (`tools/webcam-grab.sh`, only when
  the user isn't using it); fps via `GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false
  GALLIUM_HUD_DUMP_DIR=/tmp/hud`; blocked threads via `/proc/<pid>/task/*/stack` sampling.
- **Gotchas:**
  - `perf record -a -g` once hung the PS3; short supervised runs (`-- sleep 5`) work.
  - Keep netconsole quiet in normal use (loglevel 4); never `trace=1` through a GL program.
  - Programs that wait for a key need stdin held open over SSH (`sleep N |`).
  - `pkill -f` inside SSH commands kills the SSH shell itself: use `pgrep -x`.
  - The PS3's clock is UTC; the PC's is EEST.
  - Restarting the session uncleanly leaves stale X sockets: X comes up on `:1`/`:2` until reboot.

## Build setup

Everything builds in pinned Docker images: `docker/Dockerfile` (kernel and rootfs, mmdebstrap
under qemu-user), `Dockerfile.mesa` (C++ cross compiler and meson against a ppc64 sysroot),
`Dockerfile.games`, `Dockerfile.spu`, `Dockerfile.repo`. Outputs go to `build/`; earlier outputs
move to `build/old/`, never deleted (pruned by hand). `mesa/fork/` holds every Mesa file we
change, whole; its first commit is the pristine release. SPU toolchain experiments build side by
side (`SPU_OUT=build/spu-<name>`).

## The console

- **Model:** CECH-20xx (DYN-001, first Slim), NOR flash, 4.92 CEX Cobra 8.5D.
- **webMAN:** MOD 1.47.48 at http://10.0.0.76/.
- **Screen:** a Logitech BRIO webcam points at the monitor. HDMI capture is blocked by HDCP in
  the XMB.
- **USB disk:** AXAGON adapter (JMicron JMS567, 152d:0567): Bulk-Only Transport only at USB 2
  speed, queue depth 1.

## Open questions

- Does Remote Start (Remote Play settings) respond to Wake-on-LAN? That would allow remote
  power-on.
- Can the PS3's thermal sensors be read under Linux without syscon writes?
- Can a Petitboot-only reboot clear a wedged GPU, instead of a full console reboot?
- Can the CPU still read VRAM after a graph exception? (VRAM swap's fallback assumes so.)
- The kernel tried to write the flash OS area ("os_area_queue_work_handler: Could not update
  FLASH ROM", 2026-10-15, likely the RTC offset when the clock was set). It failed, but flash
  writes are off-limits: find what triggers it and disable it.
