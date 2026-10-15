# Plan: modern Linux with a working GPU on the PS3

Hard constraints are in CLAUDE.md (no permanent changes beyond installing Linux, GameOS stays the
default boot OS, no syscon writes, supervised sessions). The roadmap below follows `backlog.txt`;
updated 2026-10-15.

## Base and toolchain (decided 2026-10-15)

- **Base:** a pinned Debian sid snapshot (`DEBIAN_SNAPSHOT`, now 2026-10-13) is our release.
  Debian has no stable release for ppc64 (big-endian), only debian-ports unstable, so a frozen
  snapshot is the closest thing to an LTS. It moves deliberately, at the latest when Debian 14
  is released, and never piecemeal.
- **One compiler version everywhere: GCC 16.2,** the snapshot's system compiler (`gcc-16`
  16.2.0-3).
  - **PPE** (kernel, Mesa, ps3rsx, games): Debian's `gcc-16-powerpc64-linux-gnu`
    16.2.0-3cross1 from the amd64 sid snapshot of the same date, so the build container comes
    from that snapshot too (not Debian 13, whose GCC is 14).
  - **SPU:** our port of the SPU backend to GCC 16.2 (docs/SPU-GCC.md, `spu/port/`).
  - **binutils:** 2.47 (the snapshot's), for both.
- **Kernel:** 6.12 LTS (supported to Dec 2028). Mesa: our fork of the snapshot's release.
- Later, our builds become .debs in our own apt repository, layered over the snapshot.
- Verified (2026-10-15): the whole stack built with GCC 16.2 boots from the USB disk through
  Petitboot on its own (`/etc/kboot.conf`, no network). `rsx-suite.sh all` passes. ClassiCube
  averages 59.7 fps; the OpenArena timedemo is unchanged (median 54–59).

## Long-term goal (stated 2026-10-14)

A live environment for the PS3 that boots from USB or over NFS through Petitboot, with the RSX
driver, desktop and games working out of the box, plus an optional installer that installs it
permanently (to the OtherOS++ Linux region of the HDD, within the safety rules). The NFS netboot
used for development is the first form of it.

## Where we are (2026-10-15)

- **Boot:** Debian sid ppc64 (debian-ports snapshot), our kernel netbooted from Petitboot, root
  on NFS from the dev PC. Boot to SSH in about 21 s.
- **GPU:** `ps3rsx` (registers as "nouveau") runs stock Mesa nv30 through our Mesa fork
  (`mesa/fork/`).
  - glxgears, kmscube and Xorg with glamor run on the RSX.
  - vblank comes from the real vsync interrupt.
  - Vertex and index buffers are fetched by the GPU from VRAM. The big-endian inline-vertex
    path was what made games CPU-bound.
  - Details: DRIVER-PLAN.md (phases and results), RSX.md (measured hardware facts).
- **Desktop:** Openbox + tint2 + PCManFM, NetworkManager (never on eth0), pnmixer, dunst
  (DESKTOP-PLAN.md). About 129 MB in all. Ctrl+Alt+Fn text consoles work.
- **Games:** ClassiCube runs at a locked 60 fps fullscreen at 1080p with vsync, at about 26% CPU
  (it was 6.5 fps before the vertex-fetch fix).
- **Memory:** 214 MB of RAM visible (177 MB with the old fixed GART). The GART is dynamic: a
  64 MB CMA area that ordinary pages use while the GPU doesn't (`ps3rsx_cma=64M`); the idle
  desktop holds 6 MB of it, ClassiCube about 10 MB. Swap on the USB disk (zram on the NFS
  root), `vm.min_free_kbytes` 8 MB (two hangs under memory pressure).
- **Kernels side by side:** each kernel is its own package (`ps3-kernel-<release>`, release
  `<version>-ps3-debug-<date of the last kernel/ commit>`, with its `ps3rsx-<release>`).
  `ps3-boot`'s `ps3-update-kboot` writes Petitboot's `/etc/kboot.conf` with every installed
  kernel, newest the default; each kernel package ships its own boot arguments.

## Roadmap

### 1. Reproducible image (in progress)

The image must come entirely from the build. Fixes go into the scripts, never onto the live
root.

Done:
- `make rootfs` builds the whole image: Debian, our config, the kernel modules, the RSX driver and
  tools, Mesa, and ClassiCube with its sounds and music. `make nfs-root` only installs it; an
  existing root is moved to `/srv/ps3root.bak-<date>`. SSH host keys, Bluetooth pairings and Wi-Fi
  connections come over from the old root (`FRESH=1` doesn't).
- Two layers (2026-10-15): the packages (`base.tar`, about 15 minutes under qemu, .debs cached in
  `build/apt-cache/`) are rebuilt only when `rootfs/packages.txt` or the snapshot changes. Our
  config and builds go on top in a minute or two.
- The `*-deploy` targets update the live root with the same steps the image build uses
  (`tools/install-builds.sh`), for quick iteration. The fix still has to be in the scripts.
- Inputs are pinned:
  - the Debian snapshot date;
  - the kernel, Mesa and ClassiCube tarballs, by SHA-256;
  - game assets, by SHA-1;
  - the builder images, by digest and snapshot.
- Builds use a fixed `SOURCE_DATE_EPOCH`. Recommends are off and every package is listed.
- An audit of the live root (2026-10-15) found that all the config was already in the scripts.
  The missing packages and the resolver it found were added to the scripts.

Next:
1. Switched over (2026-10-15): the fresh root boots with no manual step. Desktop at boot, no
   failed units, `glxinfo -B` on NV47, ClassiCube at 59–60 fps (one dip to 37 in 70 s).
2. Verified on the fresh root: OpenArena starts, DS3 pairing over USB. Still open: sound on
   the TV.
3. Check reproducibility: two image builds from the same inputs give the same `make checksums`.
4. Size: about 210 MB of the image is Debian's own Mesa and LLVM, pulled in by
   `xserver-xorg-core` and `mesa-utils`, though our Mesa is used. Drop it if the dependencies
   allow (equivs or a dummy package).
5. Later, install the RSX driver and Mesa as packages or in `/usr/local`, not in `~tibi`.
6. `build/old/` gets a 2.2 GB image per build. Pruning it automatically clashes with the
   "never delete build outputs" rule, so it is pruned by hand for now (55 GB on 2026-10-15).
   Builds that run as root hand their backups to the user.
7. Disk image (stated 2026-10-15): `make disk-image` gives a minimal raw `.img.xz` (MBR, one ext4
   root, fixed disk id, built with `mke2fs -d`, reproducible). It is written with `dd`, and on the
   first boot `systemd-repart` grows the root and adds swap, then `x-systemd.growfs` grows ext4.
   `make usb-root` then just streams it to the disk.

### 2. GPU driver and games (gold standard: stable 60 fps)

Done:
- **ClassiCube:**
  - 60 fps fullscreen;
  - the sky/fog bug fixed (Mesa computed the fog distance as abs(eye z) per vertex: wrong
    across triangles behind the eye, which affects every Mesa driver);
  - Openbox starts it fullscreen.
  - Defaults: vsync, at most 4 chunk rebuilds per frame (`gfx-maxchunkupdates`), against
    stutters.

- **ClassiCube stutters and white flashes (2026-10-15):**
  - Stutters: the driver polled fences every 250 µs whenever work was pending, about 4000
    timer interrupts a second through lv1 in games. Now fast polling only while a fence has
    a waiter, else 100 ms, and `.signaled` reads the reference register: 1540/s in play.
  - Flashes in the bottom half: flip events went out at the first vblank, before lv1 had
    latched the flip; the game then cleared and redrew the buffer still on screen. Events now
    wait until the head's flip-pending bit clears. The user saw no more flashes.

Open:
- ClassiCube: confirm sound on the TV (the monitor has no speakers). About 30% of its CPU time
  is kernel, half of it the validator and shadow (`ps3rsx_validate`, `ps3rsx_shadow_write`).
- **OpenArena (in progress, parked 2026-10-15 for item 6):**
  - Works: renders correctly on the `opengl1` renderer (now the default; `opengl2` ran out of
    RAM compiling GLSL), ALSA sound initialises, DS3 pairs. Not yet heard on the TV, DS3
    bindings not tried in play.
  - Fixed on the way: a texture-upload burst exhausted the 16 MB GART and Xorg crashed. Mesa nv30
    now keeps at most 4 MB of upload staging in flight per context and waits before failing.
  - Driver CPU cost halved (validator bitmap, one decode pass for validation and shadow, lazy
    GPU shadow, no full-ring drain at wraps); Mesa built without assertions.
  - Measured (4 bots on oa_dm4, 1080p; benchmark: `games/openarena/oa-bench.sh bench` replays
    `bench.dm_71` as a timedemo without vsync, `oa-run.sh` plays a live bot match):
    - NFS root: 46–56 fps timedemo, live play mostly 59–60 with dips to 20–40. Under memory
      pressure the kernel re-read evicted code over NFS (3–10 MB/s, kswapd ~40% CPU).
    - USB disk root: the game stays resident (92 MB, 3 MB swapped, no NFS traffic). Timedemo
      median 47–56 fps, live play mostly 55–60 with dips to 8–40.
    - 960×540 reaches 65 fps, so most of the frame is CPU. Profile of the game thread: Mesa 30%,
      kernel 23% (our driver about a third of it), renderer 15%, engine 9%, zlib 6%.
    - No effect: `mesa_glthread` (no gain), `r_vertexLight`/LOD/dynamic-light settings (a few
      %), DRI2 vs DRI3, `s_useOpenAL 0`.
  - Left to do:
    1. Build ioquake3 ourselves (engine and `renderer_opengl1`, pinned, `-mcpu=cell`), as with
       ClassiCube; Debian's is built for generic ppc64.
    2. Load all player models at map start instead of when a bot first appears: those loads
       stall 1–5 s (worse from the USB disk).
    3. Mesa per-draw cost (`_mesa_update_texture_state`, fence/BO refs per draw) and the rest of
       the driver's validator.
    4. Listen on the TV; try the DS3 bindings in play.
- Test tools: `mesa/tests/gl-fog` and `gl-sky` (fixed-function fog against softpipe), and the
  EGL checksum ladder. Every new game bug gets a small test like these.

Candidates, in order:
- Strong (GL 1.x/2.1, light, big-endian history from PowerPC Macs):
  - QuakeSpasm;
  - Yamagi Quake II (gl1);
  - Extreme Tux Racer;
  - Neverball/Neverputt;
  - Armagetron Advanced (long stability runs);
  - Sauerbraten (a step up in GPU load).
- Stretch:
  - SuperTuxKart in its legacy GL 2.1 mode (RAM);
  - Luanti (CPU);
  - PrBoom+ GL.
- Out of reach: GL 3.x-only games (GZDoom, Warzone 2100, 0 A.D.).

Driver work these will likely need (DRIVER-PLAN.md "Later"):
- **GART:** 16 MB kept running out (2026-10-15): OpenArena's texture bursts; SDL's GL renderer
  streaming a whole 1080p window (8100 KiB contiguous for one upload: ClassiCube's launcher drew
  stale areas). Now:
  - **Done:** Mesa bounds upload staging in flight (4 MB per context) and splits big
    `texture_subdata` uploads into ≤1 MB bands.
  - **Done, dynamic GART via CMA (2026-10-15):** the kernel reserves a CMA area
    (`ps3rsx_cma=64M`, patch local/0007) that movable pages use while the GPU doesn't; ps3rsx
    backs GART IO pages from it in 1 MB chunks on demand and gives a chunk back once it has
    been idle 2 s and the GPU has passed its last fence (`gpu/ps3rsx/gart.c`). Unbacked IO
    pages point at a scratch chunk. Measured: `rsxdrm-smoke gart` (in rsx-suite) gets 56 MB of
    GART with the CMA area full of other pages and a 170 MB hog in swap, data correct, and
    the memory comes back after 2 s. `ps3rsx_gart=<size>` (fixed reservation) still works.
    OpenArena timedemo on it: median 48–58 fps (as before), GART peak 14 MB, all of it backed
    while loading (`/sys/kernel/mm/cma/ps3rsx/alloc_pages_*`: none during play, no failures).
    The dips in play are memory pressure, not the GART: kswapd reclaiming 16–22 MB/s of page
    cache in bursts, and kcompactd's proactive compaction moving ~20 MB at a time (fps 2–4 in
    those seconds). `vm.compaction_proactiveness=0` removed the compaction bursts, but the
    reclaim dips stayed and the 10th-percentile fps didn't clearly improve: left at 20 for now.
- **Game mode (in progress, 2026-10-15):** where the OpenArena stalls come from, from kernel
  stacks of the blocked threads: the game page-faulting its own code back in (42 of 61
  samples), swap-ins (16), its own file reads (3). With the game's pages locked
  (`mlockall(MCL_ONFAULT)`, 94 MB) the stalls moved to Xorg swapping its pages back in (40
  samples): X is on every frame's path. The USB disk makes each fault slow: the JMS567 bridge
  speaks only Bulk-Only Transport at USB 2 speed (no UAS), one command at a time, so a fault
  waits behind swap writes. Queue depth, swappiness and readahead made no measurable
  difference over run-to-run noise.
  - The desktop helpers own little (freezing them and `MADV_PAGEOUT` freed ~10 MB: the rest is
    shared libraries). Running the game on KMS without X works (SDL kmsdrm) but is per game,
    and SDL's flips there blocked in `drm_atomic_helper_wait_for_vblanks` (30 fps cap): parked.
  - Generic fix: cgroup v2 (kernel MEMCG + CGROUP_SCHED). User slices `ps3-display` (X and
    Openbox, `MemoryLow`), `ps3-desktop` (helpers, frozen and `memory.reclaim`ed while a game
    runs) and `ps3-game` (`MemoryLow`, high CPU weight); `ps3-game` switches it on and off, so
    every game gets it. Not yet measured.
- **CPU-side Mesa cost:** state validation and the kernel's validator and shadow show up in
  `perf`.
- **Mesa error paths:** two crashed on out-of-memory and are fixed. Expect more under memory
  pressure.

### 3. DualShock 3

Done:
- `hid-sony` and BlueZ are in the image.
- One USB plug pairs a DS3 with the PS3's Bluetooth. `rootfs/ds3-pair` runs from udev, writes
  the host address over hidraw, and registers the controller as trusted with BlueZ's sixaxis
  HID record.
- `ClassicBondedOnly=false`.
- Games run through `ps3-game`, so the gamepad stops moving the X pointer meanwhile.

Why it didn't pair before: the adapter wasn't connectable (BlueZ only page-scans for known
devices), and BlueZ's sixaxis plugin needs an agent, which the session doesn't run.

Next:
- Pairing tested (2026-10-15): one USB plug paired it (`ds3-pair` in the journal).
- Check the SDL GameController mapping in ClassiCube and OpenArena in play.
- Rumble and the LEDs (player number) are untested.

### 4. Fan control

Nothing controls the fan under Linux: webMAN's loop dies with GameOS. The current rule is
webMAN in SYSCON (automatic) mode, or a fixed speed of at least 60%, before booting Linux, plus
supervised sessions (CLAUDE.md). SYSCON's automatic mode is its own thermal table.

Possible next steps:
- **Read-only first:** temperatures under Linux. Find out whether lv1 or the sys-manager
  exposes the thermal sensors, and log them during game sessions.
- **Controlling the fan:** that needs syscon commands. CLAUDE.md forbids syscon writes unless
  explicitly asked, so this needs the user's decision and a read of what OtherOS++ allows.

### 5. Networking

eth0 carries the NFS root. It is set up by the kernel and systemd-networkd, and NetworkManager
never touches it. DNS comes from the kernel's DHCP (`/proc/net/pnp`). NetworkManager and
nm-applet handle Wi-Fi and USB networking. The kernel has the PS3 Wi-Fi driver
(`GELIC_WIRELESS`), and wpasupplicant is in the image.

Next:
- Test Wi-Fi: scan and join from nm-applet.
- Settings UI for wired: eth0 has to stay as it is while the root is on NFS. A local-root
  image (item 8) would let NetworkManager own it.
- Hostname and time sync: systemd-timesyncd is in the image; check it.

### 6. SPEs: a GCC toolchain for the Cell's accelerators

Linux gets 6 SPEs (`spufs`). Possible uses: zram compression, video decode, vertex work.
Report: docs/SPU-GCC.md (2026-10-15).

Done:
- `make spu-toolchain`: pinned spu-elf GCC 9.5 + binutils 2.33.1 + newlib 1.20.0 (newlib 3.3.0
  doesn't build for SPU). `spu/spurun` runs SPU programs through spufs, no libspe2. Proven:
  hello on a real SPE.
- Forward port: the GCC 9.5 backend builds in GCC 16.2 after about 20 mechanical edits
  (`spu/port/`). On old reload it passes 1621 execute torture tests on the SPEs, against 1618
  for GCC 9.5, with no regressions.

Next:
- Undo the two SPU version exceptions (docs/SPU-GCC.md), so the SPU uses the same versions as
  the rest:
  - **binutils 2.33.1 → 2.47:** bisect 2.34–2.47 for the `hbrr` "Relocation doesn't fit"
    regression (repro: newlib's `libgloss/spu/open.c` at `-O2`) and patch it
    (`spu/binutils/`).
  - **newlib 1.20.0 → 4.6:** three fixes are known: `sys/sched.h`, the `math.h`
    `isinf`/`isnan` declarations (both patches written, kept in
    `spu/newlib-4.6/` until they apply), and the generic `fenv` code against SPU's `fenv.h`.
    Continue until it builds.
  - Then rerun the execute tests on the SPEs, and offer the fixes upstream.
- spufs at boot (module, `/spu` mount) and stdio for SPU programs: serve newlib's PPE-assisted
  calls (stop 0x2100) in spurun.
- Use GCC 9.5 for now. If newer C or optimisations matter, carry the GCC 16 port and pin it.
- The LRA conversion (108 compiler crashes, from the preferred-slot subreg model) only becomes
  necessary when GCC removes old reload (announced for 16, still present in 17). Estimate
  4–8+ weeks.

### 7. A distribution

A repository of Cell-tuned packages, our kernel, and the fixed nouveau/Mesa and ps3rsx, so the
image (and other PS3 users) install them with apt instead of from build outputs.

Design (2026-10-15). The first step, before any hosting, is that the image gets everything of
ours as .debs from a local repository layered over the snapshot:
- **Packages, built with `dpkg-deb`** in the builder (reproducible with `SOURCE_DATE_EPOCH`),
  installed out of `~tibi` into system paths:

  | Package | Contents |
  |---|---|
  | `ps3-kernel` | `/boot/vmlinux-<ver>`, `/lib/modules/<ver>` |
  | `ps3rsx` | `/lib/modules/<ver>/updates/ps3rsx.ko`, loaded with `modprobe` (depmod in postinst) |
  | `ps3-gpu-tools` | RSX probes and tests in `/usr/lib/ps3/bin` |
  | `ps3-mesa` | the Mesa fork under `/usr/lib/ps3-mesa` (ld.so.conf.d); later replacing Debian's Mesa packages outright, which also drops their ~210 MB of LLVM |
  | `classicube` | `/usr/lib/classicube`; a launcher seeds a per-user game directory |
  | `ps3-spu` | `spurun`, the SPU runtime and tests |
  | `ps3-desktop` | session, `ps3-game`, `ds3-pair` and udev rules, game defaults |
- **Versions:** `<upstream>+ps3.<n>`, or for our own code `0.<commit count>` (computed on the
  host and passed in).
- **Done (2026-10-15), verified on the PS3:** the image installs the six packages with dpkg,
  `make deploy` upgrades them on the running PS3, and `make usb-root-petitboot` writes the image
  onto the USB disk from Petitboot (2 minutes; the old root is kept on the disk). The suite passes
  on the packaged system. Next (task): build them from Debian's source packages (`deb-src`, our
  patches in `debian/patches`, `+ps3.N` versions), so our Mesa replaces Debian's by version.
- **Repository:** `build/repo` made with `apt-ftparchive`. The image layer installs from it
  (`apt-get install`), and the package layer stays the snapshot. Next: sign it and host it.
  The SPU and PPE toolchains could ship from it too.

### 8. Disk image and installer

- **Petitboot boots the disk on its own:** the image carries the kernel in `/boot` and
  `/etc/kboot.conf` (Petitboot's 2.6.30 kernel reads ext4; this Petitboot parses kboot.conf and
  yaboot.conf and boots the default entry after its countdown). Not yet seen working: needs a
  reboot into Petitboot.
- **USB disk root (2026-10-15):** `make usb-root` installs the image onto a USB HDD plugged into
  the PS3 (4 GiB swap, the rest ext4 root, fixed disk id so `root=PARTUUID=50533301-02`), and
  `make netboot ROOT=usb` boots it. Why: with the root on NFS, memory pressure in games made the
  kernel re-read evicted code over the network (3–10 MB/s, kswapd ~40% CPU), and zram held the
  idle desktop's pages in RAM; swap on the disk gives that RAM back.

- **`make iso`:** a bootable image for a USB drive, booted from Petitboot. Petitboot's 2.6.30
  kernel must read the boot files (ISO9660, ext2 or FAT). Open questions: raw vs hybrid image,
  read-only live root vs writable root.
- **Installer:** to the OtherOS++ HDD region, within the safety rules. The GameOS disks
  (ps3da/db/dc) are never written.

## Test loop

- **Getting to Linux needs the user:** fan on SYSCON in webMAN, the OtherOS flag set in the
  XMB, Petitboot comes up, then `make netboot`. Every reboot lands in GameOS. At the end of each
  session the flag goes back to GameOS.
- **Driver, Mesa and game changes:** build with make, then deploy with `make gpu-deploy`,
  `make mesa-deploy` or `make classicube-deploy`.
  - Mesa must not be replaced under a running X: the NFS rename makes X die from SIGBUS. Stop X
    first (touch `~/.no-desktop`, kill Xorg), deploy, then restart `getty@tty1`.
  - ps3rsx reloads with X stopped: `rmmod`, then `insmod ~/bin/ps3rsx.ko`.
- **Kernel changes need a reboot.** kexec from our Linux crashed the console, so kernel changes
  are batched.
- **A graph exception wedges the GPU until a console reboot.** Risky tests go last, one per
  boot.
- **Observing:**
  - logs: netconsole (`make listen`), and the journal on the NFS root;
  - screen: the BRIO webcam (`tools/webcam-grab.sh`);
  - fps: `GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_DUMP_DIR=/tmp/hud`.
- **Gotchas:**
  - `perf record -a -g` once hung the PS3 (2026-10-14). A short supervised run
    (`-- sleep 5`) worked on 2026-10-15.
  - Keep netconsole quiet: loglevel=4, and never `trace=1` through a GL program.
  - Programs that wait for a key need stdin held open over SSH (`sleep N |`).
  - `pkill -f` inside SSH commands kills the SSH shell itself: use `pgrep -x` and kill by PID.

## Build setup

Everything builds in pinned Docker images:
- `docker/Dockerfile`: the kernel and rootfs builder, with mmdebstrap under qemu-user;
- `Dockerfile.mesa`: the C++ cross compiler and meson, built against a Debian ppc64 sysroot;
- `Dockerfile.games`: oggdec, for game assets.

Outputs go to `build/`, and earlier outputs are moved to `build/old/`, never deleted.
`mesa/fork/` holds every Mesa file we change, whole. Its first commit is the pristine release,
so `git diff` shows our changes.

## The console

- **Model:** CECH-20xx (DYN-001, first Slim), NOR flash, 4.92 CEX Cobra 8.5D.
- **webMAN:** MOD 1.47.48 at http://10.0.0.76/.
- **Screen:** a Logitech BRIO webcam points at the monitor. HDMI capture is blocked by HDCP in
  the XMB.

## Open questions

- Does Remote Start (Remote Play settings) respond to Wake-on-LAN? That would allow remote
  power-on.
- Can the PS3's thermal sensors be read under Linux without syscon writes (item 4)?
- Can a Petitboot-only reboot clear a wedged GPU, instead of a full console reboot?
- The kernel tried to write the flash OS area ("os_area_queue_work_handler: Could not update
  FLASH ROM", 2026-10-15, likely the RTC offset when the clock was set). It failed, but flash
  writes are off-limits: find what triggers it and disable it.
