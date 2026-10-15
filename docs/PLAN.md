# Plan: modern Linux with a working GPU on the PS3

Hard constraints are in CLAUDE.md (no permanent changes beyond installing Linux, GameOS stays the
default boot OS, no syscon writes, supervised sessions). The roadmap below follows `backlog.txt`;
updated 2026-10-15.

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
- **Memory:** 177 MB of RAM visible. The GART reservation is 16 MB (9 MB used under load), swap
  is zram, and `vm.min_free_kbytes` is 8 MB (two hangs under memory pressure).

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
1. Switch over: the user runs `! make nfs-root` and reboots. This first time the host key is new:
   update `build/ps3_known_hosts`. Later installs keep it.
2. Verify the fresh root with no manual step:
   - desktop at boot;
   - `glxinfo -B` on NV47;
   - ClassiCube at 60 fps;
   - OpenArena starts;
   - DS3 pairing.
3. Check reproducibility: two image builds from the same inputs give the same `make checksums`.
4. Size: about 210 MB of the image is Debian's own Mesa and LLVM, pulled in by
   `xserver-xorg-core` and `mesa-utils`, though our Mesa is used. Drop it if the dependencies
   allow (equivs or a dummy package).
5. Later, install the RSX driver and Mesa as packages or in `/usr/local`, not in `~tibi`.
6. `build/old/` gets a 2.2 GB image per build: compress old images, or keep only the base layer
   when an image is reproducible from it.

### 2. GPU driver and games (gold standard: stable 60 fps)

Done:
- **ClassiCube:**
  - 60 fps fullscreen;
  - the sky/fog bug fixed (Mesa computed the fog distance as abs(eye z) per vertex: wrong
    across triangles behind the eye, which affects every Mesa driver);
  - Openbox starts it fullscreen.
  - Defaults: vsync, at most 4 chunk rebuilds per frame (`gfx-maxchunkupdates`), against
    stutters.

Open:
- ClassiCube: confirm the stutter fix in play. Confirm sound on the TV (the monitor has no
  speakers). Then judge whether the remaining stutters come from memory (kswapd/zram and NFS
  page faults were seen).
- **OpenArena (next):**
  - It is in the image (Debian's ioquake3 build). The defaults are fullscreen at the display
    resolution, vsync, com_maxfps 60 and DS3 analog bindings (`~/.openarena/baseoa/autoexec.cfg`).
    None of it is tested yet.
  - Measure its fps and RAM: 100–150 MB is expected, which is tight.
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
- **GART:** 16 MB is enough today. Watch the "out of GART" lines in dmesg.
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
- Test it: plug in by USB once, then press PS.
- Check the SDL GameController mapping in ClassiCube and OpenArena.
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

Linux gets 6 SPEs (`spufs`), and nothing uses them yet. Possible uses: zram compression, video
decode, vertex work.
- Fallback (1–3 days): a pinned spu-elf toolchain in Docker (GCC 9.5, the last release with
  the SPU port, against current binutils and newlib), proven by a hello world on one SPE.
- Forward port (evaluated 2026-10-14): the GCC 9.5 SPU port is about 22k lines and uses the old
  reload allocator, which GCC is dropping. Estimate: 4–8 weeks to a C hello world on trunk,
  plus 4–10 weeks for an LRA conversion, plus ongoing upkeep.
- Decision: start with 9.5, and revisit the port only if 9.5 limits us. LLVM's CellSPU backend
  was removed in 3.3 and is not an option.

### 7. A distribution

A repository of Cell-tuned packages, our kernel, and the fixed nouveau/Mesa and ps3rsx, so the
image (and other PS3 users) install them with apt instead of from build outputs. First steps:
- package ps3rsx (DKMS or built against our kernel) and the Mesa fork as .debs;
- host them in a signed apt repository.

### 8. Disk image and installer

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
