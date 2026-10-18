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
| `ps3-gpu-tools/` | Programs run on the PS3: raw RSX probes and driver tests (`src/`: `rsx-*`, `rsxdrm-smoke`, `ps3-stallrec` the stall recorder and `ps3-waker` its A/B (bare periodic wakeups, cross-CPU kicks, per-CPU timer lateness), docs/STALLS.md), scripts (`scripts/`: `rsx-suite.sh`, `rsx-x.sh`, `ps3-bench.sh`, …), `preload/ps3-mallocstat.so` (glibc malloc statistics of a running game, docs/PROFILING.md), `preload/ps3-malloctrim.so` (gives a game's free malloc memory back every few seconds, docs/MEMORY.md), offscreen GL tests compared with softpipe (`mesa-tests/`) |
| `mesa/` | Our Mesa: Debian's `mesa` source package (`package.conf`: version and .dsc hash) plus `patches/` (our driver changes, a quilt series added as `debian/patches/ps3/`) and `packaging/` (changes to `debian/` itself: nouveau + softpipe only, no LLVM, on ppc64) |
| `ps3-spu/` | libspeq (`lib/`: resident SPE workers, job queues (bulk and the shared deadline pool), the DXT1/DXT5 encoders, the fluidsynth voice DSP, vertex skinning), the SPU programs (`spu/`: the runtime, `bench`, `dxt` (DXT1/DXT5), `deadline` (the synth and skin ops), `hello`), tools (`tools/`: `speq-dxt`, `speq-bench`, `speq-skin`; `src/spurun.c`), host tests (`tests/`, `make -C ps3-spu test`), design notes (`doc/skin.md`, `doc/pool.md`, `doc/dxt5.md`, `doc/dxt-rgb.md`); packages `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` |
| `plymouth/` | Debian's `plymouth` source package plus `patches/` (the script plugin without a console viewer) |
| `ps3-splash/` | The Walnut Linux boot splash: Plymouth script theme (`theme/`), artwork generators (`art/`), `debian/` (the DRM renderer diverted, Plymouth drop-ins); also `ps3-no-initramfs` |
| `ps3-boot/` | `ps3-update-kboot`: Petitboot's boot menu from the installed kernels |
| `yquake2/` | Yamagi Quake II: Debian's `yquake2` source package (`package.conf`: version, .dsc hash, `COMPONENT=contrib`); the game data (Quake shareware, Quake II demo) comes from `make quake-data` |
| `dsda-doom/` | dsda-doom (PrBoom+ successor): Debian's `dsda-doom` source package (`package.conf`: version and .dsc hash) plus `patches/` (big-endian fixes: dsda-doom.wad's DMX sounds, the software renderer's colours, screenshots; GL renderer without ARB_texture_rg for nv30; batched GL draws, `DSDA_GL_*` switches) |
| `luanti/` | Luanti: Debian's `luanti` source package plus `packaging/` (ppc64 added to the Architecture of `luanti` and `luanti-server`; LuaJIT kept); `CHECK=1` runs its unit tests in the build; built against our `luajit` |
| `luajit/` | LuaJIT: Debian's `luajit` source package plus `patches/` (ppc64 big-endian allocates in the low 2 GB, where it checks the state: Debian's fails `luaL_newstate()` about half the time) |
| `fluidsynth/` | FluidSynth: Debian's `fluidsynth` source package plus `patches/` (the voice DSP rendered through libspeq on the SPEs, a round of blocks per round trip, docs/SPU-AUDIO.md 5; `SPEQ_SYNTH=auto|spu|ppe|off`) |
| `openal-soft/` | OpenAL Soft: Debian's `openal-soft` source package (`package.conf`: version and .dsc hash), unpatched so far (the SPU voice mixing of docs/SPU-AUDIO.md will be its patch) |
| `cube2/` | Sauerbraten (Cube 2 engine): Debian's `cube2` source package plus `patches/` (textures compressed at upload on Mesa too; CPU skinning by default, optionally on the SPEs through libspeq; only the drawable shader variants; sky from its .dds; lightmap copies freed; game data from Debian's `cube2-data`) |
| `ioquake3/` | ioquake3 (OpenArena's engine): Debian's `ioquake3` source package plus `patches/` (0001-0010 renderer_opengl2 memory: shaders freed after link, no unusable bone/FBO programs, programs kept across map loads, no CPU copy of model vertices, the VAO cache only on demand; 0011-0018 CPU time of the engine and renderer_opengl1: shader lookups, pk3 reads through libdeflate without crc32, AltiVec, sounds loaded when played) |
| `supertuxkart/` | SuperTuxKart: Debian's `supertuxkart` source package plus `patches/` (big-endian: the .spm mesh and armature loader byte-swaps the little-endian data instead of refusing to run; textures and vertex colours in the right channel order on the OpenGL driver; legacy renderer: no crash when render to texture fails, a power-of-two minimap, opaque textures as DXT1) |
| `xmoto/` | X-Moto: Debian's `xmoto` source package plus `patches/` (fewer, larger GL draws: DrawLib's primitives batched per state run, the level's blocks and edges from one buffer; `XMOTO_GL_BATCH`, `XMOTO_GL_BLOCKS`, `XMOTO_GL_STATS`) |
| `etr/` | Extreme Tux Racer: Debian's `extremetuxracer` source package plus `patches/` (fewer, larger GL submissions: terrain from buffer objects, Tux, shadow, track marks, trees and snow as arrays, a bind cache; `ETR_GL*` switches) |
| `neverball/` | Neverball: Debian's `neverball` source package plus `patches/` (`NEVERBALL_BENCH=1`: a replay prints its frames and fps and the game quits; fewer GL calls and draws: shared mesh buffers, items in a row, batched background billboards; `NEVERBALL_GL*` switches) |
| `openttd/` | OpenTTD: Debian's `openttd` source package plus `patches/` (the OpenGL video driver on GL 2.1 without ARB_texture_rg or ARB_shader_texture_lod, the sprite recolour in the GLSL 1.10 shader) |
| `libsdl3/` | SDL3: Debian's `libsdl3` source package plus `patches/` (big-endian: the window framebuffer texture in ARGB8888/XRGB8888, uploaded natively by the OpenGL renderer) |
| `classicube/` | ClassiCube: `package.conf` (pinned upstream tarball plus texpack/audio components), `patches/` (fewer GL calls per frame: vertex pointers once per buffer, a chunk part in one draw; `CC_GL*` switches), `debian/` (launcher, defaults; packages `classicube`, the engine, and `classicube-data`, Mojang's textures and audio, for `ps3-licensed`), `sounds.sh` (sounds/music from Mojang by SHA-1) |
| `ps3-system/` | System configuration package: networking, SSH, swap, ps3gpu nodes, DS3 pairing, apt sources and pins (`files/`, `debian/`) |
| `ps3-desktop/` | Desktop package: autologin, `ps3-session`, game mode (`ps3-game`, `files/libexec/ps3-game-detect` (automatic, for fullscreen 3D programs), `ps3-game-guard` its user side, `ps3-game-prefetch` (the desktop's files back on leaving), `files/sbin/ps3-game-mode` its root side, `ps3-game-menu`; host tests in `tests/`: `menu-check.sh`, `autogm-check.sh`), Openbox/tint2/GTK/PCManFM defaults and default applications in `/etc/xdg/ps3`, X config; also `ps3-apps` (the everyday applications) |
| `ps3-games-licensed/` | Metapackage (native, all) that pulls the licensed game data in with `ps3-games`: `classicube-data`, `quake-shareware`, `quake2-demo-data`; routed to the `ps3-licensed` suite, free packages never depend on it |
| `image/` | The root filesystem image: `packages.txt` (slow Debian layer), `packages-ours.txt` (ours, installed with apt), `deps.txt` (Debian packages in our repository), `customize.sh` (per-installation settings: hostname, user, key, locale) |
| `spu-toolchain/` | The spu-elf cross toolchain's patches: GCC 16.2 port (`gcc/`), binutils 2.47 fix, newlib 4.6 fixes, test scripts |
| `games/bench/` | The game suite runner (docs/GAME-PLAN.md): `run.sh smoke\|bench\|suite` on the PS3 through `ps3-bench.sh`, one recipe per game in `recipes/`, demos and maps in `assets/`, `report.py`; `stalls.py` (why frames stall, from `ps3-stallrec`'s record: `STALLS=1`, docs/STALLS.md); `perf-post.sh`, `perfprof.py` (CPU profiles of the window: `PERF=1`) and `memprof.py` (the game's memory by mapping class, malloc, GPU buffers: `MEM=1`), docs/PROFILING.md; `hotpaths/` (perf.data read and symbolised on the PC, the graphics hot paths ranked across games: `symroot.sh`, `analyze.py`, `categorize.py`, docs/HOTPATHS.md; `perfq.sh`: the PS3's perf under qemu for perf-post.sh on the PC; `elfsym.py` also serves perfprof.py on the PS3); `selftest.py` (the harness without the PS3); `classicube-bench/` (ClassiCube's timedemo plugin: a camera path through the bench map, built by `make classicube-bench` into `assets/classicube-bench.so`); `oal-perf.sh` (OpenAL Soft's mixing split from a profiled run, docs/SPU-AUDIO.md M0); `results/` (the measured matrices, one row per tested configuration, filled by `matrix.py`; its README says how to read them); `gamemode-check.sh` (game mode on the desktop: memory idle, in game mode and after, and checks that everything frozen or unloaded comes back); `make bench-push` / `bench-pull` |
| `games/openarena/` | OpenArena benchmark demo and step 0's scripts |
| `config.mk` | All build/deploy settings (snapshot date, builder pins, IPs, root and netconsole arguments; the kernel's and Mesa's versions are in their `package.conf`) |
| `docker/` | Pinned build environments: `Dockerfile` (toolchain, mmdebstrap), `.deb` (Debian packaging tools, aptly), `.games` (ClassiCube's sound conversion), `.spu` |
| `tools/` | Scripts called by the Makefile (`deb-*.sh`: Debian source packages; `repo-*.sh`, `aptly-publish.sh`: the repository, `licensed-packages.txt`: the packages that go to its `ps3-licensed` suite only; `deploy.sh`); `webcam-grab.sh`, `bench-cam.sh` (webcam photos 30 s and 60 s into every PS3 bench run, over one SSH connection) |
| `docs/` | `PLAN.md` the plan, `PACKAGING.md` packages, repository and deploys, `DRIVER-PLAN.md` GPU driver phases, `RSX.md` measured GPU facts, `RSX-PROGRAMS.md`, `MESA-METHODS.md`, `RESEARCH.md`, `SPU-GCC.md`, `KERNEL-MEMORY.md`, `HARDWARE-LEVERAGE.md`, `RSX-SPU-FEASIBILITY.md`, `SPU-OFFLOAD*.md`, `SPU-AUDIO.md` (OpenAL mixing and fluidsynth on the SPEs), `GAME-PLAN.md` (the game stages, their status and measurements), `GAME-SUITE.md` (the game benchmark suite), `STALLS.md` (the stall recorder), `PROFILING.md` (CPU and memory profiles: `PERF=1`, `MEM=1`), `HOTPATHS.md` (the graphics stack's CPU hot paths ranked across the games, Mesa 0033/0034 and their A/B), `MEMORY.md` (where each game's memory goes, the cuts ranked, the malloc presets), `DESKTOP-PLAN.md` (the desktop and game mode), `IDEAS.md` (the open ideas, ranked), `IMAGE-DIFF.md` (a fresh image against the live PS3) |
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
| `ps3-gpu-tools` | RSX probes, driver tests, the GL tests, `ps3-bench.sh`, `ps3-stallrec` and `ps3-waker` in `/usr/lib/ps3/bin` (on `PATH`), `ps3-mallocstat.so` and `ps3-malloctrim.so` in `/usr/lib/ps3/lib` |
| `mesa-libgallium`, `libgl1-mesa-dri`, `libegl-mesa0`, `libglx-mesa0`, `libgbm1`, … | Debian's Mesa packages rebuilt from `mesa/` (`26.2.4-1+ps3.N`), replacing Debian's |
| `dsda-doom` | `/usr/games/dsda-doom` (`0.30.0+dfsg-1+ps3.N`), replacing Debian's: right colours on big-endian, `-vidmode gl` on nv30 |
| `luanti`, `luanti-data`, `luanti-server` | `/usr/games/luanti` (`5.15.2+dfsg-2+ps3.N`; Debian builds none for ppc64); Minetest Game from Debian (`luanti-game-minetest`) |
| `libluajit-5.1-2`, `luajit`, ... | LuaJIT (`2.1.0+openresty20251030-1+ps3.N`), replacing Debian's: `luaL_newstate()` no longer fails on ppc64 |
| `libopenal1`, `libopenal-data`, `libopenal-dev`, `openal-tools` | OpenAL Soft (`1:1.25.2-1+ps3.N`), replacing Debian's: unpatched so far, built here so `libopenal1-dbgsym` matches what runs (docs/SPU-AUDIO.md M0) |
| `cube2`, `cube2-server` | Sauerbraten's engine (`0.0.20201229+dfsg-1+ps3.N`), replacing Debian's: texture compression on Mesa, CPU skinning (on the SPEs when libspeq takes it), less memory; data from Debian's `cube2-data` |
| `ioquake3`, `ioquake3-server` | OpenArena's engine (`1.36+u20260719.58839361+dfsg-1+ps3.N`), replacing Debian's: renderer_opengl2 memory patches, engine and renderer_opengl1 CPU patches |
| `supertuxkart`, `supertuxkart-data` | `/usr/games/supertuxkart` (`1.5-2+ps3.N`), replacing Debian's: loads its .spm models on big-endian (Debian's quits while loading); `supertuxkart-data` is Debian's data, rebuilt because `supertuxkart` needs it at its own version |
| `xmoto`, `xmoto-data` | X-Moto (`0.6.3+repack-1+ps3.N`), replacing Debian's: batched GL drawing |
| `extremetuxracer`, `extremetuxracer-data` | Extreme Tux Racer (`0.8.4-2+ps3.N`), replacing Debian's: batched GL drawing |
| `neverball`, `neverputt`, ... | Neverball (`1.6.0+git20180603-3+ps3.N`), replacing Debian's: the bench mode, batched GL drawing |
| `openttd`, `openttd-data` | OpenTTD (`15.1-1+ps3.N`), replacing Debian's: the OpenGL driver on nv30 |
| `libsdl3-0`, ... | SDL3 (`3.4.18+ds-1+ps3.N`), replacing Debian's: window framebuffer colours and native uploads on big-endian |
| `libfluidsynth3`, `fluidsynth`, ... | FluidSynth (`2.6.1+dfsg-1+ps3.N`), replacing Debian's: voice DSP on the SPEs through libspeq (`SPEQ_SYNTH`) |
| `classicube`, `classicube-data` | `/usr/games/classicube` (game data per user in `~/.local/share/classicube`); `classicube-data`: its texture pack, sounds and music (Mojang's), in `ps3-licensed` only |
| `yamagi-quake2`, `yamagi-quake2-core` | Yamagi Quake II (`/usr/lib/yamagi-quake2/quake2`, renderers `ref_gl1`, `ref_gl3`), built from Debian's contrib source (debian-ports builds no contrib) |
| `quake-shareware`, `quake2-demo-data` | game data from game-data-packager (`make quake-data`, `build/debs/quake-data`): `/usr/share/games/quake/id1/pak0.pak`, `/usr/share/games/quake2-demo/baseq2/`; non-free, our LAN repository's `ps3-licensed` suite only |
| `libspeq0` | libspeq: SPU jobs from any program (resident workers on the SPEs); DXT1 compression for Mesa, fluidsynth's voice DSP and Sauerbraten's skinning, each loading it at run time (`SPEQ_S3TC`, `SPEQ_SYNTH`, `SPEQ_SKIN` = `auto\|spu\|ppe\|off`, `SPEQ_S3TC_RGB=ppe`, `SPEQ_DEBUG=1`) |
| `ps3-spu`, `ps3-spu-programs` | `spurun`, `speq-dxt`, `speq-bench`, spufs mounted at `/spu`; the SPU programs in `/usr/lib/ps3/spu` |
| `ps3rsx-modules-<release>`, `ps3rsx-modules-ps3`, `ps3rsx-modules-ps3-debug` | the RSX driver (loaded at boot) and probe modules for a kernel; the metas follow the release and debug kernels |
| `ps3-splash`, `plymouth`, `libplymouth5`, … | the boot splash (Walnut theme) on our patched Plymouth; `ps3-no-initramfs` stands in for initramfs-tools |
| `ps3-system`, `ps3-desktop` | the system and desktop configuration (see the layout above) |
| `ps3-apps` | Mousepad, GPicView, zathura, Xarchiver, Galculator, LXTask, scrot (Print key), mpv, opened by default from PCManFM; remove it to remove them |
| `ps3-games` | the game suite's games that run on the RSX (docs/GAME-PLAN.md): OpenArena, ClassiCube, QuakeSpasm, Yamagi Quake II, Armagetron, Neverball, X-Moto, Extreme Tux Racer, dsda-doom with Freedoom, OpenTTD, none of the licensed data (that is `ps3-games-licensed`); the heavy ones (Sauerbraten, Luanti, SuperTuxKart) deploy by name |

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

