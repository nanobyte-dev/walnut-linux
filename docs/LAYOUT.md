# Repository layout and developer usage

Modern Debian (ppc64, debian-ports) on a CFW PS3 via OtherOS++/Petitboot, with the goal of running
nouveau's userspace (libdrm + Mesa nv30) on the RSX. Plan and background: [README.md](README.md).

## Layout

The tree, top two levels (each top folder has a three-line README.md):

```
packages/           every source package we build, by kind (make <name>-deb; docs/PACKAGING.md)
  walnut/           ours, native: system/ drivers/ tools/ apps/
  patched/          Debian's or upstream's source plus our patches: kernel/ graphics/ audio/ libs/
                    desktop/, and toolchain/spu/ (the SPU cross toolchain's patches)
  repackaged/       upstream releases packaged as they are
  licensed/         ps3-games-licensed, the metapackage of the licensed game data (never CI, never public)
apt-repo/           the apt repositories' client side: walnut-repo.py, keyrings/, licensed-publish.sh
servers/            walnut.nanobyte.dev/: its containers, nginx, the upload and the publisher
website/            site/ (walnut.nanobyte.dev's front page) and gamedb/ (Walnut Games, Hugo)
gamedb/             the games database: everything about a game, one folder each (its schema in
                    gamedb/README.md); games/<id>/ holds the catalog entry, its played sessions,
                    its packages and licensed data, its benchmark; engines/<e>/ the engines
build-containers/   the builders' Dockerfiles
github-ci/          what GitHub Actions runs, and the generator of .github/workflows/pkg-*.yml
system/             image/ (the root filesystem image's lists and settings) and installer/
bench/              the game benchmark harness that runs on the PS3 and its results (the recipes are
                    in gamedb/games/<id>/benchmark/)
tools/              the everyday scripts the Makefile and the developer call
docs/               the docs, indexed in docs/README.md
```

A package is named by its directory alone: `make xmoto-deb` builds `gamedb/games/xmoto/packages/xmoto/` into
`build/debs/xmoto`, and `tools/pkg-dir.sh xmoto` prints the directory. A new package goes where its
kind says (a game's: `gamedb/games/<id>/packages/`, an engine's: `gamedb/engines/<e>/packages/`, a
game's licensed data: `gamedb/games/<id>/licensed/`; otherwise in `packages/`, ours: `walnut/<area>/`;
Debian's or upstream's with patches: `patched/<area>/`; an upstream release unchanged: `repackaged/`),
then `python3 github-ci/gen-workflows.py` gives it a CI workflow (not for licensed ones). A package
directory may hold `upstream.mk` (its upstream pins) and `orig.mk` (its orig tarball recipe, included by
the Makefile: docs/PACKAGING.md "Upstream tarballs"), and `dfsg-exclude.txt` (paths left out of an
upstream tarball). A game's build and install facts (its packages, engine, suite, data, licensed data)
are in its bundle's `build.toml` (gamedb/README.md).

### packages/walnut/: ours

| Path | What |
|---|---|
| `apps/walnut-games/` | The Walnut Games desktop app (native package): `files/bin/walnut-games` (launcher) and `files/lib/walnut_games.py` (Tk window: card grid, filters, details, Install/Remove/Play), `files/sbin/walnut-games-helper` (the root helper behind `debian/sudoers`), the packaged catalog unpacked to `/usr/share/walnut-games/` from `build/gamedb/catalog.tar` (`make gamedb-catalog`, `tools/gamedb/pack-catalog.py`); `tests/test_walnut_games.py` (suite `walnut-games`) |
| `drivers/ps3rsx/` | The RSX DRM driver (`src/`, registers as "nouveau": render ioctls, validator, software context switch, KMS, VRAM swap) the GPU probe modules (`probes/`) and a userspace equivalence fuzz of the state shadow's fast path (`test/`); `debian/` builds `ps3rsx-modules-<release>` against our kernels |
| `system/ps3-boot/` | `ps3-update-kboot`: Petitboot's boot menu from the installed kernels, plus the entries in `/etc/kboot.d/*.conf` as written (the SSD root's); `tests/test_kboot.py` (suite `ps3-boot`); `webman/autoexec.bat`: draft GameOS startup script that backs up the boot files from dev_hdd1 to dev_hdd0 and restores them (docs/GAMEOS-HDD.md) |
| `system/ps3-desktop/` | Desktop package: autologin, `ps3-session`, game mode (`ps3-game`, `files/libexec/ps3-game-detect` (automatic, for fullscreen 3D programs), `ps3-game-guard` its user side, `ps3-game-prefetch` (the desktop's files back on leaving, a game's own files read at its start; code in `ps3_game_prefetch.py`), `files/sbin/ps3-game-mode` its root side, `ps3-game-menu`; host tests in `tests/`: `menu-check.sh`, `autogm-check.sh`, `gprefetch-check.py`), Openbox/tint2/GTK/PCManFM defaults and default applications in `/etc/xdg/ps3`, X config; also `ps3-apps` (the everyday applications) |
| `system/ps3-splash/` | The Walnut Linux boot splash: Plymouth script theme (`theme/`), artwork generators (`art/`), `debian/` (the DRM renderer diverted, Plymouth drop-ins); also `ps3-no-initramfs` |
| `system/ps3-system/` | System configuration package: networking, SSH, swap (`ps3-vswap`: `PS3_VSWAP_DM=1` in `/etc/default/ps3-vswap` makes the VRAM swap's backing a device-mapper device, for the SSD swap arm), ps3gpu nodes, DS3 pairing, apt sources and pins, `ps3-gameos-mount`, OpenAL Soft's 16-bit output (`files/sound/alsoft.conf` as `/etc/xdg/alsoft.conf`) (`files/`, `debian/`) |
| `tools/ps3-gpu-tools/` | Programs run on the PS3: raw RSX probes and driver tests (`src/`: `rsx-*`, `rsxdrm-smoke`, `ps3-stallrec` the stall recorder and `ps3-waker` its A/B (bare periodic wakeups, cross-CPU kicks, per-CPU timer lateness), `ps3-intfwatch` the interference detector of every bench run (input, VT switches, devices, SSH logins and commands), docs/STALLS.md), scripts (`scripts/`: `rsx-suite.sh`, `rsx-x.sh`, `ps3-bench.sh`, `ps3-sm-temp.sh` the temperatures (docs/THERMAL.md), …), `preload/ps3-mallocstat.so` (glibc malloc statistics of a running game, docs/PROFILING.md), `preload/ps3-malloctrim.so` (gives a game's free malloc memory back every few seconds, docs/MEMORY.md), offscreen GL tests compared with softpipe (`mesa-tests/`) |
| `tools/ps3-spu/` | libspeq (`lib/`: resident SPE workers, job queues (bulk and the shared deadline pool), the DXT1/DXT5 encoders, the fluidsynth voice DSP, vertex skinning), the SPU programs (`spu/`: the runtime, `bench`, `dxt` (DXT1/DXT5), `deadline` (the synth and skin ops), `hello`), tools (`tools/`: `speq-dxt`, `speq-bench`, `speq-skin`; `src/spurun.c`), host tests (`tests/`, `make -C ps3-spu test`), design notes (`doc/skin.md`, `doc/pool.md`, `doc/dxt5.md`, `doc/dxt-rgb.md`); packages `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` |

### packages/patched/: Debian's or upstream's source with our patches

| Path | What |
|---|---|
| `audio/fluidsynth/` | FluidSynth: Debian's `fluidsynth` source package plus `patches/` (0001 the voice DSP rendered through libspeq on the SPEs, a round of blocks per round trip, docs/SPU-AUDIO.md "fluidsynth on the SPEs", `SPEQ_SYNTH=auto|spu|ppe|off`; 0002 setting overrides from `/etc/fluidsynth/settings` and `FLUIDSYNTH_SETTINGS` that win over the program's; 0003 the Dattorro reverb's taps without a divide per sample) |
| `audio/libsfml/` | SFML: Debian's `libsfml` source package plus `patches/` (audio playback period 64 to 1024 frames, `SFML_AUDIO_PERIOD_FRAMES` override; Extreme Tux Racer's xruns) |
| `audio/libvorbis/` | libvorbis: Debian's `libvorbis` source package plus `packaging/` (`-O3 -mcpu=cell`) and `patches/` (`ov_read`'s 16-bit packing in AltiVec; docs/SPU-AUDIO.md "Vorbis decoding on the PPE") |
| `audio/openal-soft/` | OpenAL Soft: Debian's `openal-soft` source package plus `packaging/` (built `-mcpu=cell`) and `patches/` (the mixer's per-sample integer/float conversions without load-hit-stores on the PPE: the resampler's fraction from tables, 16-bit samples in and out in AltiVec; docs/SPU-AUDIO.md "OpenAL Soft on the PPE"; the SPU mixer design that was not built: docs/research/OPENAL-SPU-MIXER.md) |
| `desktop/plymouth/` | Debian's `plymouth` source package plus `patches/` (the script plugin without a console viewer) |
| `desktop/pnmixer/` | Debian's `pnmixer` source package plus `patches/` (no crash on a volume change without a sound card) |
| `graphics/libpng1.6/` | libpng: Debian's `libpng1.6` source package plus `packaging/` (`-O3 -mcpu=cell`) and `patches/` (the row unfilters Up, Sub, Avg and Paeth for 3- and 4-byte pixels in AltiVec without load-hit-stores, `powerpc/filter_cell.h`; docs/PROFILING.md "Load-time decoders and linking") |
| `graphics/libsdl3/` | SDL3: Debian's `libsdl3` source package plus `patches/` (big-endian: the window framebuffer texture in ARGB8888/XRGB8888, uploaded natively by the OpenGL renderer) |
| `graphics/mesa/` | Our Mesa: Debian's `mesa` source package (`package.conf`: version and .dsc hash) plus `patches/` (our driver changes, a quilt series added as `debian/patches/ps3/`) and `packaging/` (changes to `debian/` itself: nouveau + softpipe only, no LLVM, on ppc64); `tests/` host tests of patch code (`make test`) |
| `graphics/sdl2-compat/` | SDL2 on SDL3: Debian's `sdl2-compat` source package plus `patches/` (closing the audio device while holding its lock doesn't deadlock: SDL 1.2 games through sdl12-compat do it, Armagetron Advanced hung on quit) |
| `kernel/linux/` | The kernel: `patches/series` (PS3 patches applied in order: `ps3/` from psx-place, `local/` ours), `config/*.config` fragments merged onto `ps3_defconfig`; as Debian's `linux` source package (`package.conf`: version, .dsc hash, flavours and their fragments): `packaging/` (changes to `debian/`: only our two kernels, release `6.12.111+ps3.N` and debug `6.12.111+ps3.N-debug<stamp>` (Debian flavours `ps3`, `ps3-debug`), no initramfs), `flavours/release.args`, `debug.args` (boot arguments, shipped as `/usr/lib/ps3/boot/<release>.args`); `tests/`: host tests of kernel patch logic (`ps3disk-guard.sh`: local/0045's range checks, from the patch itself; `guard-header.sh` prints the patch's `ps3disk_guard.h` for the tests that compile it; `sixaxis-idle-axes.sh`: local/0050 in a QEMU guest, an x86 kernel from `build/src/linux` and a DS3 made over uhid by `sixaxis-uhid-init.c`, hid-sony with and without the patch) and `ds3-idle-check.py` (on the PS3: the plugged-in DS3's axes before its PS button) |
| `libs/boost-context/` | Boost.Context, Coroutine and Fiber 1.92: Debian's `boost1.92` source package plus `packaging/` (0001: the fcontext assembler on ppc64 instead of ucontext, Debian #1146645; 0002: build profile `pkg.boost.contextonly`, only those three libraries and their -dev packages, the rest stays Debian's 1.92.0-3) and `patches/` (the ppc64 switch keeps F14-F31, FPSCR and V20-V31); `test/run-test.sh` (a fiber, asio::spawn, Coroutine and Fiber test plus Boost's own tests, under qemu) |
| `libs/luajit/` | LuaJIT: Debian's `luajit` source package plus `patches/` (ppc64 big-endian allocates in the low 2 GB, where it checks the state: Debian's fails `luaL_newstate()` about half the time) |
| `toolchain/spu/` | The spu-elf cross toolchain's patches: GCC 16.2 port (`gcc/`), binutils 2.47 fix, newlib 4.6 fixes, test scripts |

### packages/repackaged/ and packages/licensed/

| Path | What |
|---|---|
| `repackaged/zlib-ng/` | zlib-ng 2.3.3 (upstream tarball, `packages/repackaged/zlib-ng/upstream.mk`; Debian has none) in zlib-compat mode: package `libz-ng-compat`, a second `libz.so.1` in `/usr/lib/powerpc64-linux-gnu/zlib-ng` that every game started through `ps3-game` loads (`LD_LIBRARY_PATH`; `PS3_ZLIB=stock` or the bench token `ZLIB=stock` off); zlib1g stays the system zlib (docs/PROFILING.md "Load-time decoders and linking") |
| `licensed/ps3-games-licensed/` | Metapackage (native, all) that pulls the licensed game data in with `ps3-games`: `classicube-data`, `quake-shareware`, `quake2-demo-data`; published to the licensed suite only, free packages never depend on it |

### gamedb/: the games' packages

A package that belongs to one game lives in that game's bundle, `gamedb/games/<id>/packages/<pkg>/`; an engine that is
an upstream project of its own (with `engine.md`: its games) in `gamedb/engines/<e>/packages/<pkg>/`; a licensed data
package or its builder in `gamedb/games/<id>/licensed/<pkg>/` (licensed by its place: built here only, never by CI,
never public). A game without a published page has a draft bundle (`draft = true` in its `index.md`). The schema is
gamedb/README.md.

| Path (in gamedb/) | What |
|---|---|
| `engines/cube2/packages/cube2/` | The Cube 2 engine (BananaBread, and Sauerbraten through `games/sauerbraten/packages/sauerbraten/`): Debian's `cube2` source package plus `patches/` (textures compressed at upload on Mesa too; CPU skinning by default, optionally on the SPEs through libspeq; only the drawable shader variants; sky from its .dds; lightmap copies freed; fewer GL calls; water queries without waiting; world normals as shorts nv30 fetches, `vanormalshort`, off by default; game data from Debian's `cube2-data`) |
| `engines/darkplaces/packages/darkplaces-gl2/` | DarkPlaces 0~20180412 (the last with GL 1.x/2.0 renderpaths; `package.conf`, Debian's patches in `patches/`, `debian/`: wrapper `nexuiz-gl2` and lean config) for Debian's `nexuiz-data`; the tarball is pinned in its `upstream.mk`, fetched by its `orig.mk`; docs/games/smoke-tested.md "Nexuiz Classic" |
| `engines/ioquake3/packages/ioquake3/` | ioquake3 (OpenArena's engine): Debian's `ioquake3` source package plus `patches/` (0001-0010 renderer_opengl2 memory: shaders freed after link, no unusable bone/FBO programs, programs kept across map loads, no CPU copy of model vertices, the VAO cache only on demand; 0011-0018 CPU time of the engine and renderer_opengl1: shader lookups, pk3 reads through libdeflate without crc32, AltiVec, sounds loaded when played; 0019-0024 renderer_opengl1 batching, the lightmap atlas off by default; 0025-0026 textures kept and late loads preloaded across same-map reloads; 0027-0029 renderer_opengl2 vertex formats nv30 fetches, mipmaps without powf, packed MD3 frames) |
| `games/armagetronad/packages/armagetronad/` | Armagetron Advanced: Debian's `armagetronad` plus `patches/` (a fresh config starts fullscreen at the desktop's resolution; a fixed game-controller scheme, controller N plays local player N) |
| `games/bomberclone/packages/bomberclone/` | Debian's `bomberclone` (an Atomic Bomberman clone) plus `patches/` (a clean quit: no segfault in the audio thread at exit; full screen by default) |
| `games/classicube/packages/classicube/` | ClassiCube: `package.conf` (pinned upstream tarball plus texpack/audio components), `patches/` (fewer GL calls per frame: vertex pointers once per buffer, a chunk part in one draw; `CC_GL*` switches), `debian/` (launcher, defaults; the package `classicube`, the engine; Mojang's textures and audio are `licensed/classicube-data/`), `sounds.sh` (sounds/music from Mojang by SHA-1) |
| `games/ddnet/packages/ddnet/` | DDNet 20.1.1: Debian's newer `ddnet` source (`SOURCE_SNAPSHOT`) plus `patches/` (non-blocking sockets on big-endian 64-bit: the unsigned long FIONBIO bug) |
| `games/dsda-doom/packages/dsda-doom/` | dsda-doom (PrBoom+ successor): Debian's `dsda-doom` source package (`package.conf`: version and .dsc hash) plus `patches/` (big-endian fixes: dsda-doom.wad's DMX sounds, the software renderer's colours, screenshots; GL renderer without ARB_texture_rg for nv30; batched GL draws, `DSDA_GL_*` switches) |
| `games/endless-sky/packages/endless-sky/` | Endless Sky 0.11.2: Debian's `endless-sky` source package plus `patches/` (big-endian pixel and WAV byte order, mono sounds, `ES_LOW_MEMORY` defaults, the game's OpenGL 2 path with GLSL 1.20 shaders in `shaders-gl2`, sprites as 2D atlases, fog map as luminance, a cheap outline shader) and `packaging/` (unit tests only, a decode hash dump) |
| `games/etr/packages/etr/` | Extreme Tux Racer: Debian's `extremetuxracer` source package plus `patches/` (fewer, larger GL submissions: terrain from buffer objects, Tux, shadow, track marks, trees and snow as arrays, a bind cache; `ETR_GL*` switches) |
| `games/hedgewars/packages/hedgewars/` | Hedgewars: Debian's `hedgewars` plus `packaging/` and `patches/` (the engine starts with the FPU exceptions masked) |
| `games/luanti/packages/luanti/` | Luanti: Debian's `luanti` source package plus `packaging/` (ppc64 added to the Architecture of `luanti` and `luanti-server`; LuaJIT kept); `CHECK=1` runs its unit tests in the build; built against our `luajit` |
| `games/mrboom/packages/mrboom/` | Debian's `mrboom` (Mr.Boom, a Bomberman clone for up to 8 players) plus `patches/` (the big-endian register layout for the SDL2 build: Debian's ppc64 binary segfaults on its first text) |
| `games/neverball/packages/neverball/` | Neverball: Debian's `neverball` source package plus `patches/` (`NEVERBALL_BENCH=1`: a replay prints its frames and fps and the game quits; fewer GL calls and draws: shared mesh buffers, items in a row, batched background billboards; `NEVERBALL_GL*` switches) |
| `games/openrct2/packages/openrct2/` | OpenRCT2 0.5.6: upstream's release and asset zips (`upstream.mk`) with a Debian-style `debian/`, plus `patches/` (the big-endian port: docs/games/openrct2.md); `rct2-data` comes from the owner's CD (`gamedb/games/openrct2/licensed/rct2-data/rct2-data.sh`, licensed) |
| `games/openttd/packages/openttd/` | OpenTTD: Debian's `openttd` source package plus `patches/` (the OpenGL video driver on GL 2.1 without ARB_texture_rg or ARB_shader_texture_lod, the sprite recolour in the GLSL 1.10 shader) |
| `games/opentyrian/packages/opentyrian/` | OpenTyrian 2.1: Debian's `opentyrian` plus `packaging/` (the data package only Suggested; `tyrian-data` comes from `gamedb/games/opentyrian/licensed/tyrian-data/tyrian-data.sh`, licensed) |
| `games/sauerbraten/packages/sauerbraten/` | Sauerbraten's launcher (native, all): `files/sauerbraten` runs our `cube2` engine on the real game's data (`sauerbraten-data`, licensed) with its own home `~/.sauerbraten`, the menu entry and icon; docs/games/sauerbraten.md |
| `games/supertuxkart/packages/supertuxkart/` | SuperTuxKart: Debian's `supertuxkart` source package plus `patches/` (big-endian: the .spm mesh and armature loader byte-swaps the little-endian data instead of refusing to run; textures and vertex colours in the right channel order on the OpenGL driver; legacy renderer: no crash when render to texture fails, a power-of-two minimap, opaque textures as DXT1) |
| `games/teeworlds/packages/teeworlds/` | Teeworlds: Debian's `teeworlds` source package plus `patches/` (big-endian: the UDP sockets really non-blocking, FIONBIO given an int; without it the client hangs in recvfrom() after connecting) |
| `games/uqm/packages/uqm/` | The Ur-Quan Masters: Debian's `uqm` plus `packaging/` (the content packages only Suggested, so `uqm` installs without them) |
| `games/warzone2100/packages/warzone2100/` | Warzone 2100 4.3.5 (the last with a GL 2.1 renderer): upstream's release tarball (`upstream.mk`) with a Debian-style `debian/`, plus `patches/` (GCC 16, big-endian decals, `WZ_AUTOGAME_SPEED`, the stb_image_resize miscompile) |
| `games/wesnoth/packages/wesnoth/` | Battle for Wesnoth 1.18: Debian's `wesnoth-1.18` source package plus `packaging/` (build profile `pkg.wesnoth.archonly`: only `wesnoth-1.18` and `wesnoth-1.18-server`, the data and campaigns are Debian's); built against our `boost-context` |
| `games/xmoto/packages/xmoto/` | X-Moto: Debian's `xmoto` source package plus `patches/` (fewer, larger GL draws: DrawLib's primitives batched per state run, the level's blocks and edges from one buffer, batches written into a mapped stream buffer; `XMOTO_GL_BATCH`, `XMOTO_GL_BLOCKS`, `XMOTO_GL_STREAM`, `XMOTO_GL_STATS`); `test/glbatch-test.sh` (the GL layer on the PC's llvmpipe: same pixels with and without the stream buffer) |
| `games/yquake2/packages/yquake2/` | Yamagi Quake II: Debian's `yquake2` source package (`package.conf`: version, .dsc hash, `COMPONENT=contrib`); the game data (Quake shareware, Quake II demo) comes from `make game-data` |
| `games/bananabread/licensed/sauerbraten-nonfree-data/` | The models and sounds of the Sauerbraten release that Debian's cube2-data lacks (`files.txt`, 912 files; `sauerbraten-data.sh` here); Section local/games. BananaBread's: it fills in Debian's `cube2-data` (its Depends), the data BananaBread plays (the bench recipe `sauerbraten.sh`); the real Sauerbraten has `sauerbraten-data` |
| `games/classicube/licensed/classicube-data/` | ClassiCube's texture pack and audio, Mojang's (`upstream.mk`, the tarball made by its `orig.mk` from the texture pack and `classicube/sounds.sh`'s audio); Section local/games |
| `games/sauerbraten/licensed/sauerbraten-data/` | The Sauerbraten 2020_12_29 release's whole game data for the real Sauerbraten (`sauerbraten-full-data.sh` here makes the tarball); Section local/games |
| `games/openrct2/licensed/rct2-data/` | `rct2-data.sh`: the licensed `rct2-data` deb from the owner's RollerCoaster Tycoon 2 CD (no package.conf: the script builds the deb itself) |
| `games/opentyrian/licensed/tyrian-data/` | `tyrian-data.sh`: the licensed `tyrian-data` deb from Tyrian 2.1's freeware zip (no package.conf: the script builds the deb itself) |
| `games/teeworlds/upstream.md`, `games/yquake2/upstream.md` | Drafts offering our patches upstream, with the patches in `upstream/` (docs/upstream/README.md lists every draft) |

### Everything else

| Path | What |
|---|---|
| `apt-repo/` | `walnut-repo.py` (a suite's next state from its published, signed one, no database: `add`, `fetch`, `base`; the licensed suite's login from `WALNUT_APT_AUTH_FILE`, `--strict` for its files that never change) and `test_walnut_repo.py` (against the real walnut-watch, suite `walnut-repo`), `keyrings/` (the archive key: releases, stable, licensed; the CI key: testing; ps3-system ships copies), `licensed-publish.sh` (`make publish`: licensed packages built here to the licensed suite), `licensed-auth.sh` (that suite's login from config.local.mk or pass, into a 600 file) |
| `servers/walnut.nanobyte.dev/` | walnut.nanobyte.dev on the OVH box (docs/release/ALPHA.md M4): `walnut-repo` (nginx: the apt suites, downloads and the password-protected licensed suite), `walnut-upload` (`upload/`: sshd + rrsync, write-only uploads into incoming/), `walnut-watch` (publishes uploads atomically; `test_walnut_watch.py`), `walnut-upload.sh` and `publish-site.sh` (the client side, also used by `.github/workflows/walnut-site.yml`), `walnut-site` (nginx with website/site/ built in), the main proxy's site config; `make walnut-server` deploys it, `make walnut-site` publishes the site |
| `website/site/` | walnut.nanobyte.dev's front page (static: `index.html`, `style.css`, the public keys); published with the gamedb by `servers/walnut.nanobyte.dev/publish-site.sh` |
| `website/gamedb/` | The Walnut Games site (Hugo; https://walnut.nanobyte.dev/gamedb/): `hugo.toml`, `layouts/` (card grid home, game page), `assets/gamedb.css` and `gamedb.js` (filters, search, copy button), `content/` the site's own pages (`content/games/_index.md`: the games section); the games themselves come from `gamedb/games/<id>/`, mounted as `content/games/<id>/` (`hugo.toml`'s `[[module.mounts]]`, an allowlist: `index.md`, `thumb.png`, the sessions' JPEGs): `index.md` (the catalog entry as TOML front matter, notes as the body), `thumb.png`, `rating/sessions/<date>-<kind>/result.toml` (generated by `import-run.py`) with its `ps3-*.jpg` and `pc-*.jpg` screenshots (Git LFS, `.gitattributes`); `data/catalog.json` is generated and ignored |
| `build-containers/` | Pinned build environments: `Dockerfile` (toolchain, mmdebstrap), `.deb` (Debian packaging tools, the deb builder), `.games` (ClassiCube's sound conversion), `.spu`, `.benchref` (the PC reference of a bench recipe: Xvfb, llvmpipe, the game; tools/bench-ref.sh), `.playpc` (a game to play on the PC beside a PS3 session: llvmpipe at GL 2.1 on the desktop's X, its sound; tools/play-pc.sh) |
| `github-ci/` | GitHub Actions' side (docs/release/ALPHA.md M6): `build-package.sh` (one package, no secrets; refuses licensed ones), `publish.sh` (builds to testing or stable in one upload, through apt-repo/walnut-repo.py), `batch-publish.py` (testing's one publisher: every build testing lacks, from the builds' artifacts) and `test_batch_publish.py`, `build-release.sh`, `publish-release.sh`, `release-id.sh`, `release-notes.sh` (the release image), `runner-prep.sh`, `gen-workflows.py` (`.github/workflows/pkg-<name>.yml` for every package but the licensed ones, and `walnut-publish.yml`) |
| `system/image/` | The root filesystem image: `packages.txt` (slow Debian layer), `packages-ours.txt` (ours, installed with apt), `customize.sh` (per-installation settings: hostname, user, key, locale) |
| `system/installer/` | `walnut-install` (the PC-side installer of a release onto the PS3's internal disk or a USB drive), `test_walnut_install.py`, the release archive's README |
| `bench/` | The game suite runner (docs/games/SUITE.md): `run.sh smoke\|bench\|suite` on the PS3 through `ps3-bench.sh`, one recipe per game in its GameDB bundle (`gamedb/games/<id>/benchmark/recipe.sh`, with its demos and maps in `assets/`; `games.sh` and `games.py` find them, `make bench-push` copies them to `/var/tmp/bench/games/<id>/`), the shared recipe pieces `smoke.inc`, `openal.inc`, `keyboard.evemu`, `xkeys.py`, `report.py`; `stalls.py` (why frames stall, from `ps3-stallrec`'s record: `STALLS=1`, docs/STALLS.md); `perf-post.sh`, `perfprof.py` (CPU profiles of the window: `PERF=1`) and `memprof.py` (the game's memory by mapping class, malloc, GPU buffers: `MEM=1`), docs/PROFILING.md; `hotpaths/` (perf.data read and symbolised on the PC, the graphics hot paths ranked across games: `symroot.sh`, `analyze.py`, `categorize.py`, `stacks.py` (callers, callees and leaves of a function), docs/HOTPATHS.md; `perfq.sh`: the PS3's perf under qemu for perf-post.sh on the PC; `elfsym.py` also serves perfprof.py on the PS3); `selftest.py` (the harness without the PS3; `intfcheck.py` its interference part); `interference-scan.py` (runs from before the interference detector: what their files show of somebody disturbing them, `--write` into `results/interfered.txt`, which report.py and matrix.py leave out; docs/STALLS.md "Interference"); `oal-perf.sh` (OpenAL Soft's mixing split from a profiled run, docs/SPU-AUDIO.md M0); `results/` (the measured matrices, one row per tested configuration, filled by `matrix.py`; its README says how to read them); `gamemode-check.sh` (game mode on the desktop: memory idle, in game mode and after, and checks that everything frozen or unloaded comes back); `dumpana.sh` (a failure dump of a `DUMPON=1` run, core and state, analysed with gdb and the run's debug symbols; docs/games/LOG-FINDINGS.md "Catching failures"); `ssd-arm.sh` (the SSD arms `GAMEDATA=ssd`, `SWAP=ssd`: a recipe's `DATA_DIRS` bound from the SSD copy, the VRAM swap's spill switched to the SSD; docs/GAMEOS-HDD.md "Test drive, Phase A"); `shots.py` (the PS3's screenshots: `SHOTS=`, xwd to PNG, the PS3 \| PC sheet `<run>/shots-vs-pc.png`, the capture's cost in frames, the sheets nobody reviewed; docs/games/SUITE.md "Screenshots"); `make bench-push` / `bench-pull` / `bench-shots` |
| `gamedb/games/<id>/benchmark/` | A game's benchmark (gamedb/README.md): `recipe.sh`, `assets/`, `assets-src/` (ClassiCube's timedemo plugin, built by `make classicube-bench` into `assets/classicube-bench.so` with its `assets.mk`; OpenTTD's save generator), `lib/` (Hedgewars' `hwdrive.py`), `analysis.toml` (the recipe's old name), `legacy/` (OpenArena's step 0 scripts) |
| `tools/` | Scripts called by the Makefile (`deb-*.sh`, `pkg-*.sh`: the source packages, `pkg-dir.sh NAME` the directory of one, `--all` every one, `--order` in build order; `deploy.sh`: `make deploy`; `licensed.py`: what is licensed (never built by CI nor published outside the licensed suite), from the licensed directories, gamedb's `build.toml` `[[licensed]]` entries and `dfsg-exclude.txt` files and `licensed-files.txt` (the generic leak globs), asked by every guard; `leak-check.py`; `public-export.sh`: the public GitHub copy, without the licensed directories; `image-packages.sh`: the image's package list per profile (the public one without the games whose data is licensed: `tools/gamedb/build-info.py needs-licensed`); `tools/gamedb/build-info.py`: what the Makefile and tools need from gamedb's `build.toml` files (`make game-data`, `make licensed-data`, play-pc's packages and data); `doclinks.py`: the docs' links and `gamedb:` references, checked or rewritten after moves; `test_repo_tools.py`: host tests of these); `webcam-grab.sh`, `bench-cam.sh` (webcam photos 30 s and 60 s into every PS3 bench run, over one SSH connection: the TV and the sound; the picture check is the screenshots now); `bench-ref.sh` (a recipe run on the PC in an amd64 container under Xvfb with llvmpipe at GL 2.1, screenshots at the PS3 run's shot points -> build/bench-ref, `make bench-ref GAME=x`); `ppecost/` (a qemu TCG plugin counting the PPE's load-hit-stores, multiplies and divides per guest PC, `attrib.py` per function, and the decoder/mixer harnesses it was run on: docs/PROFILING.md "Load-time decoders and linking") |
| `tools/gamedb/` | Walnut Games' tools (docs/GAMEDB.md): `score.py` (aspect scores, the weighted mean, the weakest-link cap and the Scrap floors; run by itself it prints the calibration table), `import-run.py` (harness run directories into a game's `rating/sessions/<date>-<kind>/result.toml` and its screenshots as 800x450 JPEGs), `check.py` (the catalog's schema, ids, packages, no licensed package), `catalog.py` (`catalog.json`: ratings, tests, "patched by Walnut" patch subjects, .deb links with `--deb-index`; drafts left out; `--diff OLD` prints what changed against an older catalog.json), `blocks.py` (the generated `<!-- gamedb:NAME -->` blocks in the docs: `--write`, `--check`), `baseline-check.py` (the GameDB migration's before/after check: `save`, `check` against build/tmp/gamedb-baseline), `build.sh OUT` (check, catalog, the pinned Hugo of `hugo.version`, then walnut-watch's site rules on the output; `--preview` for localhost), `test_gamedb.py` (suite `gamedb`) |
| `tools/ssdimg/` | The SSD image (docs/GAMEOS-HDD.md "Test drive, Phase A" and "Phase B: the whole system on the SSD"): `stamp.py` (PC: the stamped image streamed to dev_hdd0 over webMAN's FTP), `ps3-imgmap` (PS3, root: the file's blocks by FIBMAP from the read-only UFS2 mount, every stamp verified from `/dev/ps3db`, rechecked, then dm-linear disks over them; `export-boot` the boot map), `ps3-imgmap-boot.c` (the initramfs's recheck and dm devices, static C, dm ioctls; registers and seals the kernel guard's ranges; `guard-fake.h`: its test build's model of the guard), `initrd/init` + `build-initrd.sh` + `mkcpio.py` (the SSD root's initramfs, `make initrd-ssd`), `ssd-install-image.sh` (PS3, root: the release image onto the SSD root, the boot entry on the USB root), `test_ssdimg.py` (host tests, suite `ssdimg`), `vm-e2e.sh` + `vm-guest*.sh`, `vm-init-e2e.sh`, `vm-testroot-init.sh`, `vm-outside.py`, `vm-hdrbreak.py` (the same on a real big-endian UFS2 image in a KVM guest of the PC's kernel: Phase A's tools, then the C tool, the install script and boots of the initramfs, suite `ssdimg-vm`), `guard-sim/` (`ps3guard_sim`, a module for that guest: disks ps3da and ps3db over the virtual disks with local/0045's own guard code and sysfs, `extract.sh` takes them from the patch) |
| `config.mk` | The global build/deploy settings (snapshot date, builder pins, root and netconsole arguments; the LAN in the untracked `config.local.mk`). Each package's upstream pins (version, URL, sha256) are in its own `upstream.mk` in its package directory (the SPU toolchain's in `packages/patched/toolchain/spu/`, the initramfs's in `tools/ssdimg/upstream.mk`); the kernel's and Mesa's versions in their `package.conf` |
| `docs/` | The docs, indexed in `docs/README.md` (one line per doc, and a glossary): `PLAN.md` the plan and its stages, `TESTS.md` the PS3 test log, the measured facts at the top level (`RSX.md`, `DRIVER.md`, `STALLS.md`, `PROFILING.md`, `HOTPATHS.md`, `MEMORY.md`, `KERNEL-MEMORY.md`, `SPU.md`, `SPU-AUDIO.md`, `THERMAL.md`, `CONSOLE.md`, `DESKTOP.md`, `GAMEOS-HDD.md`, `PACKAGING.md`, ...), `games/` (every game's state, one file per game or group, the suite runner), `backlog/` (what we haven't tried, ranked), `research/` (read or reasoned, not measured here), `archive/` (the originals before the 2026-10-19 rewrite), `upstream/` (drafts of upstream bug reports and patches, none sent) |
| `build/` | Generated (ignored), ephemeral (see CLAUDE.md); `build/aptly` is the retired LAN repository's emptied aptly tree (backup in /mnt/data_linux/Temp/ps3/retired-tibi-server-20261026/) |
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
    make mesa-deb          # Mesa from Debian's source + packages/patched/graphics/mesa/patches, native ppc64 build under qemu
                           #   (make chroot first: the build chroot) -> build/debs/mesa
    make linux-deb         # the kernel from Debian's source + packages/patched/kernel/linux/patches, cross-built (linux-kbuild
                           #   natively under qemu) -> build/debs/linux; KEEP_TREE=1 keeps the build tree
    make ps3rsx-modules    # the RSX driver for both kernels in build/debs/linux
    make snd-ps3-ko REL=6.12.111+ps3.472   # snd_ps3.ko with packages/patched/kernel/linux/patches' sound patches for that kernel,
                           #   to try without a new kernel -> build/debs/snd-ps3/<REL> (not a package)
    make deploy            # install with apt on the running PS3 from build/debs (a flat repository this
                           #   PC serves through an SSH tunnel): upgrades ours, installs PKGS (top-level
                           #   names; KERNELS=1 upgrades kernels too; DRY_RUN=1 simulates;
                           #   AUTOREMOVE=1 purges what nothing needs any more)
    make publish           # licensed packages built here -> walnut.nanobyte.dev's licensed suite (DEBS=...,
                           #   DRY_RUN=1: the bundle only); free packages are published by CI
    make spu-toolchain     # spu-elf GCC 16.2 (our port) for the SPEs
    make initrd-ssd IMGMAP=<copy of the PS3's map.json>   # the SSD root's initramfs -> build/initrd
                           #   (docs/GAMEOS-HDD.md "Phase B: the whole system on the SSD")

The image builds in two layers. `build/rootfs/base.tar` has the Debian packages (dpkg under qemu,
about 15 minutes; downloads cached in `build/apt-cache/`) and is rebuilt only when
`system/image/packages.txt` or the snapshot changes. `build/rootfs/debian-ppc64.tar` adds our config and
our own packages, in a minute or two. Everything we build is a .deb in `build/debs/<pkg>`:

| Package | Installs |
|---|---|
| `linux-image-<release>`, `linux-image-ps3`, `linux-image-ps3-debug` | the kernels (release `6.12.111+ps3.N`, debug `6.12.111+ps3.N-debug<stamp>`): `/boot/vmlinux-<release>`, modules, boot arguments; the metas follow the newest |
| `ps3-boot` | `ps3-update-kboot`: Petitboot's `/etc/kboot.conf` from the installed kernels (newest first, the default) and `/etc/kboot.d/*.conf` |
| `ps3-gpu-tools` | RSX probes, driver tests, the GL tests, `ps3-bench.sh`, `ps3-stallrec`, `ps3-waker` and `ps3-intfwatch` in `/usr/lib/ps3/bin` (on `PATH`), `ps3-mallocstat.so` and `ps3-malloctrim.so` in `/usr/lib/ps3/lib` |
| `mesa-libgallium`, `libgl1-mesa-dri`, `libegl-mesa0`, `libglx-mesa0`, `libgbm1`, … | Debian's Mesa packages rebuilt from `packages/patched/graphics/mesa/` (`26.2.4-1+ps3.N`), replacing Debian's |
| `dsda-doom` | `/usr/games/dsda-doom` (`0.30.0+dfsg-1+ps3.N`), replacing Debian's: right colours on big-endian, `-vidmode gl` on nv30 |
| `luanti`, `luanti-data`, `luanti-server` | `/usr/games/luanti` (`5.15.2+dfsg-2+ps3.N`; Debian builds none for ppc64); Minetest Game from Debian (`luanti-game-minetest`) |
| `libluajit-5.1-2`, `luajit`, ... | LuaJIT (`2.1.0+openresty20251030-1+ps3.N`), replacing Debian's: `luaL_newstate()` no longer fails on ppc64 |
| `libopenal1`, `libopenal-data`, `libopenal-dev`, `openal-tools` | OpenAL Soft (`1:1.25.2-1+ps3.N`), replacing Debian's: the mixer without load-hit-stores on the PPE (bit-identical output) |
| `libsfml-audio3.1` (and the other `libsfml*` binaries) | SFML (`3.1.0+dfsg-4+ps3.N`), replacing Debian's: a 1024 frame playback period instead of 64 (about 64 ms of buffer, `SFML_AUDIO_PERIOD_FRAMES` overrides) |
| `libvorbis0a`, `libvorbisfile3`, `libvorbisenc2`, `libvorbis-dev` | libvorbis (`1.3.7-3+ps3.N`), replacing Debian's: `ov_read`'s 16-bit packing in AltiVec (bit-identical output) |
| `libpng16-16t64`, `libpng-dev`, `libpng-tools` | libpng (`1.6.59-1+ps3.N`), replacing Debian's: PNG row unfiltering in AltiVec (bit-identical output) |
| `libz-ng-compat` | zlib-ng's zlib-compat `libz.so.1` in `/usr/lib/powerpc64-linux-gnu/zlib-ng`, used only by programs started with that directory in `LD_LIBRARY_PATH` |
| `cube2`, `cube2-server` | BananaBread's engine (`0.0.20201229+dfsg-1+ps3.N`), replacing Debian's: texture compression on Mesa, CPU skinning (on the SPEs when libspeq takes it), less memory; data from Debian's `cube2-data` |
| `ioquake3`, `ioquake3-server` | OpenArena's engine (`1.36+u20260719.58839361+dfsg-1+ps3.N`), replacing Debian's: renderer_opengl2 memory patches, engine and renderer_opengl1 CPU patches |
| `supertuxkart`, `supertuxkart-data` | `/usr/games/supertuxkart` (`1.5-2+ps3.N`), replacing Debian's: loads its .spm models on big-endian (Debian's quits while loading); `supertuxkart-data` is Debian's data, rebuilt because `supertuxkart` needs it at its own version |
| `xmoto`, `xmoto-data` | X-Moto (`0.6.3+repack-1+ps3.N`), replacing Debian's: batched GL drawing |
| `libboost-context1.92.0`, `libboost-coroutine1.92.0`, `libboost-fiber1.92.0`, their -dev | Boost 1.92's context libraries (`1.92.0-3+ps3.N`), replacing Debian's: the fcontext assembler on ppc64 (Debian's build lacks make/jump_fcontext, and its libboost_coroutine can't link) with the FPRs and AltiVec registers kept across a switch |
| `wesnoth-1.18`, `wesnoth-1.18-server` | Battle for Wesnoth (`1:1.18.8-1+ps3.N`), which Debian has no ppc64 build of; data and campaigns from Debian's arch-all packages |
| `endless-sky`, `endless-sky-data` | Endless Sky (`0.11.2-1+ps3.N`), replacing Debian's: runs on big-endian and on OpenGL 2.1 (Debian's has no GL2 shaders and wrong pixel/sound byte order) |
| `teeworlds`, `teeworlds-server`, `teeworlds-data` | Teeworlds (`0.7.5-3+ps3.N`), replacing Debian's: connects on big-endian (Debian's client hangs on the loading screen) |
| `extremetuxracer`, `extremetuxracer-data` | Extreme Tux Racer (`0.8.4-2+ps3.N`), replacing Debian's: batched GL drawing |
| `neverball`, `neverputt`, ... | Neverball (`1.6.0+git20180603-3+ps3.N`), replacing Debian's: the bench mode, batched GL drawing |
| `openttd`, `openttd-data` | OpenTTD (`15.1-1+ps3.N`), replacing Debian's: the OpenGL driver on nv30 |
| `libsdl3-0`, ... | SDL3 (`3.4.18+ds-1+ps3.N`), replacing Debian's: window framebuffer colours and native uploads on big-endian |
| `libsdl2-2.0-0`, ... | SDL2 as sdl2-compat (`2.32.74+ds-1+ps3.N`), replacing Debian's: no deadlock when an app (or an SDL 1.2 game through sdl12-compat) closes the audio device with its lock held |
| `libfluidsynth3`, `fluidsynth`, ... | FluidSynth (`2.6.1+dfsg-1+ps3.N`), replacing Debian's: voice DSP on the SPEs through libspeq (`SPEQ_SYNTH`), setting overrides (`/etc/fluidsynth/settings`, `FLUIDSYNTH_SETTINGS`), a cheaper reverb |
| `classicube`, `classicube-data` | `/usr/games/classicube` (game data per user in `~/.local/share/classicube`); `classicube-data`: its texture pack, sounds and music (Mojang's), in the licensed suite only |
| `yamagi-quake2`, `yamagi-quake2-core` | Yamagi Quake II (`/usr/lib/yamagi-quake2/quake2`, renderers `ref_gl1`, `ref_gl3`), built from Debian's contrib source (debian-ports builds no contrib) |
| `quake-shareware`, `quake2-demo-data` | game data from game-data-packager (`make game-data`, `build/debs/game-data`): `/usr/share/games/quake/id1/pak0.pak`, `/usr/share/games/quake2-demo/baseq2/`; non-free, the licensed suite only |
| `libspeq0` | libspeq: SPU jobs from any program (resident workers on the SPEs); DXT1 compression for Mesa, fluidsynth's voice DSP and BananaBread's skinning, each loading it at run time (`SPEQ_S3TC`, `SPEQ_SYNTH`, `SPEQ_SKIN` = `auto\|spu\|ppe\|off`, `SPEQ_S3TC_RGB=ppe`, `SPEQ_DEBUG=1`) |
| `ps3-spu`, `ps3-spu-programs` | `spurun`, `speq-dxt`, `speq-bench`, spufs mounted at `/spu`; the SPU programs in `/usr/lib/ps3/spu` |
| `ps3rsx-modules-<release>`, `ps3rsx-modules-ps3`, `ps3rsx-modules-ps3-debug` | the RSX driver (loaded at boot) and probe modules for a kernel; the metas follow the release and debug kernels |
| `ps3-splash`, `plymouth`, `libplymouth5`, … | the boot splash (Walnut theme) on our patched Plymouth; `ps3-no-initramfs` stands in for initramfs-tools |
| `ps3-system`, `ps3-desktop` | the system and desktop configuration (see the layout above; ps3-system also makes OpenAL Soft's output 16-bit, `/etc/xdg/alsoft.conf`) |
| `ps3-apps` | Mousepad, GPicView, zathura, Xarchiver, Galculator, LXTask, scrot (Print key), mpv, opened by default from PCManFM; remove it to remove them |
| `ps3-games` | the game suite's games that run on the RSX (docs/games/README.md): OpenArena, ClassiCube, QuakeSpasm, Yamagi Quake II, Armagetron, Neverball, X-Moto, Extreme Tux Racer, dsda-doom with Freedoom, OpenTTD, none of the licensed data (that is `ps3-games-licensed`); the heavy ones (BananaBread, Luanti, SuperTuxKart) deploy by name |

The image installs them with apt, and `make deploy` installs the same packages on the running
PS3. So whatever is tried live is what the next image gets. On the PS3, apt sees Debian (live
debian-ports) and Walnut's signed `stable` suite: `apt install`/`apt upgrade` work as on Debian, while
ps3-system's pins (`packages/walnut/system/ps3-system/files/apt/walnut.pref`) keep our packages on top and Debian's kernels
and Mesa out.
`make nfs-root` carries over the PS3's own state from the previous root: SSH host keys, Bluetooth
pairings, saved Wi-Fi connections (`FRESH=1` starts clean).

On the PS3 every program uses our Mesa (it replaces Debian's packages); `egl-gears` etc. print a checksum to compare with
a softpipe run (`LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe`).

    sudo modprobe ps3rsx              # on the PS3 (loaded at boot anyway)
    rsx-suite.sh all            # regression: smoke, Mesa ladder, concurrent processes
    rsx-x.sh 30 glxgears -info  # Xorg (modesetting + glamor) on the RSX, one client
    tools/webcam-grab.sh   # on the PC: photo of the PS3's screen -> build/tmp/screen.jpg
    tools/walnut-diag.sh   # on the PS3 (from a USB stick, as root): network/sound state and link tests into a folder beside it

Host tests (no PS3 needed):

    make test                    # quick suites, then a table of suite, pass/FAIL/skip, seconds
    make test-slow               # plus autogm-check.sh (needs Xvfb+openbox, or docker/podman) and the
                                 #   SSD test drive in a KVM guest (tools/ssdimg/vm-e2e.sh: docker, qemu, /dev/kvm)
    make test ONLY=ps3-spu       # one suite: bench-selftest, gprefetch, ps3rsx-shadow, ps3-spu, xmoto-glbatch,
                                 #   mesa-forcedxt, ssdimg, ps3-boot, leak-check, repo-tools, licensed, workflows,
                                 #   blocks, doclinks (a warning only), walnut-repo, walnut-watch, gamedb,
                                 #   walnut-games, ps3disk-guard, autogm, ssdimg-vm (the last two with make test-slow)

`tools/run-tests.sh` runs them: `bench/selftest.py`, `packages/walnut/system/ps3-desktop/tests/gprefetch-check.py`,
`packages/walnut/drivers/ps3rsx/test` (x86 fuzz with short seeds and the validator's vertex format checks; the big-endian run under qemu when the cross gcc, qemu and
the Mesa sysroot exist), `make -C packages/walnut/tools/ps3-spu test`, `gamedb/games/xmoto/packages/xmoto/test/glbatch-test.sh` (needs `make xmoto-work`).
A suite missing its prerequisites is reported as skip. Logs are in `build/tmp/tests/<suite>.log`;
the exit status is non-zero when a suite failed.

## Reproducibility

Debian's kernel and Mesa sources are checked against `DSC_SHA256` in their `package.conf`; Debian packages come from snapshot.debian.org
at `DEBIAN_SNAPSHOT`; the builder image is pinned by digest with apt on the same snapshot;
builds use a fixed `SOURCE_DATE_EPOCH`, build user/host and no git version suffix.

Petitboot: `firmware/petitboot/dtbImage.ps3.bin` from github.com/xXEvilnatXx/xai_plugin
(`Petitboot/NOR`), MD5 `ca5e9520e03066290f68f67e9167c0fa`.

