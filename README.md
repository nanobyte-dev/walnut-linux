# PS3 Linux + RSX

Modern Debian (ppc64, debian-ports) on a CFW PS3 via OtherOS++/Petitboot, with the goal of running
nouveau's userspace (libdrm + Mesa nv30) on the RSX. Plan and background: `docs/`.

## Layout

The tree is organized by output package: each top-level package directory holds what goes into that
package (Debian source packages; docs/PACKAGING.md).

| Path | What |
|---|---|
| `linux/` | The kernel: `patches/series` (PS3 patches applied in order: `ps3/` from psx-place, `local/` ours), `config/*.config` fragments merged onto `ps3_defconfig`; as Debian's `linux` source package (`package.conf`: version, .dsc hash, flavours and their fragments): `packaging/` (changes to `debian/`: only our two kernels, release `6.12.111+ps3.N` and debug `6.12.111+ps3.N-debug<stamp>` (Debian flavours `ps3`, `ps3-debug`), no initramfs), `flavours/release.args`, `debug.args` (boot arguments, shipped as `/usr/lib/ps3/boot/<release>.args`) |
| `ps3rsx/` | The RSX DRM driver (`src/`, registers as "nouveau": render ioctls, validator, software context switch, KMS, VRAM swap) and the GPU probe modules (`probes/`); `debian/` builds `ps3rsx-modules-<release>` against our kernels |
| `ps3-gpu-tools/` | Programs run on the PS3: raw RSX probes and driver tests (`src/`: `rsx-*`, `rsxdrm-smoke`), scripts (`scripts/`: `rsx-suite.sh`, `rsx-x.sh`, …), offscreen GL tests compared with softpipe (`mesa-tests/`) |
| `mesa/` | Our Mesa: Debian's `mesa` source package (`package.conf`: version and .dsc hash) plus `patches/` (our driver changes, a quilt series added as `debian/patches/ps3/`) and `packaging/` (changes to `debian/` itself: nouveau + softpipe only, no LLVM, on ppc64) |
| `ps3-spu/` | libspeq (`lib/`: resident SPE workers, a job queue, the DXT1 encoders), the SPU programs (`spu/`: the runtime, `bench`, `dxt`, `hello`), tools (`tools/`: `speq-dxt`, `speq-bench`; `src/spurun.c`), host tests (`tests/`, `make -C ps3-spu test`); packages `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` |
| `plymouth/` | Debian's `plymouth` source package plus `patches/` (the script plugin without a console viewer) |
| `ps3-splash/` | The Walnut Linux boot splash: Plymouth script theme (`theme/`), artwork generators (`art/`), `debian/` (the DRM renderer diverted, Plymouth drop-ins); also `ps3-no-initramfs` |
| `ps3-boot/` | `ps3-update-kboot`: Petitboot's boot menu from the installed kernels |
| `yquake2/` | Yamagi Quake II: Debian's `yquake2` source package (`package.conf`: version, .dsc hash, `COMPONENT=contrib`); the game data (Quake shareware, Quake II demo) comes from `make quake-data` |
| `dsda-doom/` | dsda-doom (PrBoom+ successor): Debian's `dsda-doom` source package (`package.conf`: version and .dsc hash) plus `patches/` (big-endian fixes: dsda-doom.wad's DMX sounds, the software renderer's colours, screenshots; GL renderer without ARB_texture_rg for nv30) |
| `luanti/` | Luanti: Debian's `luanti` source package plus `packaging/` (ppc64 added to the Architecture of `luanti` and `luanti-server`; LuaJIT kept); `CHECK=1` runs its unit tests in the build; built against our `luajit` |
| `luajit/` | LuaJIT: Debian's `luajit` source package plus `patches/` (ppc64 big-endian allocates in the low 2 GB, where it checks the state: Debian's fails `luaL_newstate()` about half the time) |
| `openal-soft/` | OpenAL Soft: Debian's `openal-soft` source package (`package.conf`: version and .dsc hash), unpatched so far (the SPU voice mixing of docs/SPU-AUDIO.md will be its patch) |
| `cube2/` | Sauerbraten (Cube 2 engine): Debian's `cube2` source package plus `patches/` (textures compressed at upload on Mesa too; game data from Debian's `cube2-data`) |
| `ioquake3/` | ioquake3 (OpenArena's engine): Debian's `ioquake3` source package plus `patches/` (renderer_opengl2 memory: shaders freed after link, no unusable bone/FBO programs, programs kept across map loads, no CPU copy of model vertices, the VAO cache only on demand) |
| `supertuxkart/` | SuperTuxKart: Debian's `supertuxkart` source package plus `patches/` (big-endian: the .spm mesh and armature loader byte-swaps the little-endian data instead of refusing to run; textures and vertex colours in the right channel order on the OpenGL driver) |
| `classicube/` | ClassiCube: `package.conf` (pinned upstream tarball plus texpack/audio components), `debian/` (launcher, defaults), `sounds.sh` (sounds/music from Mojang by SHA-1) |
| `ps3-system/` | System configuration package: networking, SSH, swap, ps3gpu nodes, DS3 pairing, apt sources and pins (`files/`, `debian/`) |
| `ps3-desktop/` | Desktop package: autologin, `ps3-session`, game mode, Openbox/tint2/GTK/PCManFM defaults and default applications in `/etc/xdg/ps3`, X config; also `ps3-apps` (the everyday applications) |
| `image/` | The root filesystem image: `packages.txt` (slow Debian layer), `packages-ours.txt` (ours, installed with apt), `deps.txt` (Debian packages in our repository), `customize.sh` (per-installation settings: hostname, user, key, locale) |
| `spu-toolchain/` | The spu-elf cross toolchain's patches: GCC 16.2 port (`gcc/`), binutils 2.47 fix, newlib 4.6 fixes, test scripts |
| `games/bench/` | The game suite runner (docs/GAME-PLAN.md): `run.sh smoke\|bench\|suite` on the PS3 through `ps3-bench.sh`, one recipe per game in `recipes/`, demos and maps in `assets/`, `report.py`; `classicube-bench/` (ClassiCube's timedemo plugin: a camera path through the bench map, built by `make classicube-bench` into `assets/classicube-bench.so`); `oal-perf.sh` (OpenAL Soft's mixing split from a profiled run, docs/SPU-AUDIO.md M0); `make bench-push` / `bench-pull` |
| `games/openarena/` | OpenArena benchmark demo and step 0's scripts |
| `config.mk` | All build/deploy settings (snapshot date, builder pins, IPs, root and netconsole arguments; the kernel's and Mesa's versions are in their `package.conf`) |
| `docker/` | Pinned build environments: `Dockerfile` (toolchain, mmdebstrap), `.deb` (Debian packaging tools, aptly), `.games` (ClassiCube's sound conversion), `.spu` |
| `tools/` | Scripts called by the Makefile (`deb-*.sh`: Debian source packages; `repo-*.sh`, `aptly-publish.sh`: the repository; `deploy.sh`) |
| `docs/` | `PLAN.md` the plan, `PACKAGING.md` packages, repository and deploys, `DRIVER-PLAN.md` GPU driver phases, `RSX.md` measured GPU facts, `RSX-PROGRAMS.md`, `MESA-METHODS.md`, `RESEARCH.md`, `SPU-GCC.md`, `KERNEL-MEMORY.md`, `HARDWARE-LEVERAGE.md`, `RSX-SPU-FEASIBILITY.md`, `SPU-OFFLOAD*.md`, `SPU-AUDIO.md` (OpenAL mixing on an SPE), `GAME-SUITE.md` (the game benchmark suite), `IMAGE-DIFF.md` (a fresh image against the live PS3) |
| `build/` | Generated (ignored). Ephemeral except `build/aptly` (the published repository's state); see CLAUDE.md |
| `firmware/` | Petitboot image installed on the console (ignored) |
| `backups/` | **Private**: NOR flash dump (ignored, never commit) |

## Usage

    make packages          # all our packages -> build/debs/<pkg> (make chroot first: the build chroot)
    make rootfs            # the whole image from them (kernels, RSX driver, Mesa, games)
    make checksums         # sha256 of the image and the debs: compare builds for reproducibility
    make nfs-root          # sudo: install the image as the NFS export (old root kept as .bak-<date>)
    make usb-root          # or: the image onto the USB disk on the PS3 (FORMAT=1 partitions it first)
    make listen            # in another terminal: PS3 debug output
    make netboot-deb       # PS3 in Petitboot -> boots a kernel from build/debs/linux with NFS root
                           #   (FLAVOUR=release|debug, ROOT=usb: USB disk root)
    make mesa-deb          # Mesa from Debian's source + mesa/patches, native ppc64 build under qemu
                           #   (make chroot first: the build chroot) -> build/debs/mesa
    make linux-deb         # the kernel from Debian's source + linux/patches, cross-built (linux-kbuild
                           #   natively under qemu) -> build/debs/linux; KEEP_TREE=1 keeps the build tree
    make ps3rsx-modules    # the RSX driver for both kernels in build/debs/linux
    make deploy            # install with apt on the running PS3: upgrades ours, installs PKGS
                           #   (top-level names; KERNELS=1 upgrades kernels too; DRY_RUN=1 simulates;
                           #   AUTOREMOVE=1 purges what nothing needs any more)
    make publish           # publish build/debs to the repository server (DEBS=..., DIST=dev for test builds)
    make spu-toolchain     # spu-elf GCC 16.2 (our port) for the SPEs

The image builds in two layers. `build/rootfs/base.tar` has the Debian packages (dpkg under qemu,
about 15 minutes; downloads cached in `build/apt-cache/`) and is rebuilt only when
`image/packages.txt` or the snapshot changes. `build/rootfs/debian-ppc64.tar` adds our config and
our own packages, in a minute or two. Everything we build is a .deb in `build/debs/<pkg>`:

| Package | Installs |
|---|---|
| `linux-image-<release>`, `linux-image-ps3`, `linux-image-ps3-debug` | the kernels (release `6.12.111+ps3.N`, debug `6.12.111+ps3.N-debug<stamp>`): `/boot/vmlinux-<release>`, modules, boot arguments; the metas follow the newest |
| `ps3-boot` | `ps3-update-kboot`: Petitboot's `/etc/kboot.conf` from the installed kernels (newest first, the default) |
| `ps3-gpu-tools` | RSX probes, driver tests and the GL tests in `/usr/lib/ps3/bin` (on `PATH`) |
| `mesa-libgallium`, `libgl1-mesa-dri`, `libegl-mesa0`, `libglx-mesa0`, `libgbm1`, … | Debian's Mesa packages rebuilt from `mesa/` (`26.2.4-1+ps3.N`), replacing Debian's |
| `dsda-doom` | `/usr/games/dsda-doom` (`0.30.0+dfsg-1+ps3.N`), replacing Debian's: right colours on big-endian, `-vidmode gl` on nv30 |
| `luanti`, `luanti-data`, `luanti-server` | `/usr/games/luanti` (`5.15.2+dfsg-2+ps3.N`; Debian builds none for ppc64); Minetest Game from Debian (`luanti-game-minetest`) |
| `libluajit-5.1-2`, `luajit`, ... | LuaJIT (`2.1.0+openresty20251030-1+ps3.N`), replacing Debian's: `luaL_newstate()` no longer fails on ppc64 |
| `libopenal1`, `libopenal-data`, `libopenal-dev`, `openal-tools` | OpenAL Soft (`1:1.25.2-1+ps3.N`), replacing Debian's: unpatched so far, built here so `libopenal1-dbgsym` matches what runs (docs/SPU-AUDIO.md M0) |
| `cube2`, `cube2-server` | Sauerbraten's engine (`0.0.20201229+dfsg-1+ps3.N`), replacing Debian's: texture compression on Mesa; data from Debian's `cube2-data` |
| `ioquake3`, `ioquake3-server` | OpenArena's engine (`1.36+u20260719.58839361+dfsg-1+ps3.N`), replacing Debian's: renderer_opengl2 memory patches |
| `supertuxkart`, `supertuxkart-data` | `/usr/games/supertuxkart` (`1.5-2+ps3.N`), replacing Debian's: loads its .spm models on big-endian (Debian's quits while loading); `supertuxkart-data` is Debian's data, rebuilt because `supertuxkart` needs it at its own version |
| `classicube` | `/usr/games/classicube` (game data per user in `~/.local/share/classicube`) |
| `yamagi-quake2`, `yamagi-quake2-core` | Yamagi Quake II (`/usr/lib/yamagi-quake2/quake2`, renderers `ref_gl1`, `ref_gl3`), built from Debian's contrib source (debian-ports builds no contrib) |
| `quake-shareware`, `quake2-demo-data` | game data from game-data-packager (`make quake-data`, `build/debs/quake-data`): `/usr/share/games/quake/id1/pak0.pak`, `/usr/share/games/quake2-demo/baseq2/`; non-free, our LAN repository only |
| `libspeq0` | libspeq: SPU jobs from any program (resident workers on the SPEs); DXT1 compression for Mesa, which loads it at run time (`SPEQ_S3TC=auto\|spu\|ppe\|off`, `SPEQ_DEBUG=1`) |
| `ps3-spu`, `ps3-spu-programs` | `spurun`, `speq-dxt`, `speq-bench`, spufs mounted at `/spu`; the SPU programs in `/usr/lib/ps3/spu` |
| `ps3rsx-modules-<release>`, `ps3rsx-modules-ps3`, `ps3rsx-modules-ps3-debug` | the RSX driver (loaded at boot) and probe modules for a kernel; the metas follow the release and debug kernels |
| `ps3-splash`, `plymouth`, `libplymouth5`, … | the boot splash (Walnut theme) on our patched Plymouth; `ps3-no-initramfs` stands in for initramfs-tools |
| `ps3-system`, `ps3-desktop` | the system and desktop configuration (see the layout above) |
| `ps3-apps` | Mousepad, GPicView, zathura, Xarchiver, Galculator, LXTask, scrot (Print key), mpv, opened by default from PCManFM; remove it to remove them |
| `ps3-games` | the game suite's games that run on the RSX (docs/GAME-PLAN.md): OpenArena and ClassiCube so far |

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

Debian's kernel and Mesa sources are checked against `DSC_SHA256` in their `package.conf`; Debian packages come from snapshot.debian.org
at `DEBIAN_SNAPSHOT`; the builder image is pinned by digest with apt on the same snapshot;
builds use a fixed `SOURCE_DATE_EPOCH`, build user/host and no git version suffix.

Petitboot: `firmware/petitboot/dtbImage.ps3.bin` from github.com/xXEvilnatXx/xai_plugin
(`Petitboot/NOR`), MD5 `ca5e9520e03066290f68f67e9167c0fa`.

