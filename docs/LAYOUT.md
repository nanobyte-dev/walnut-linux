# Repository layout and developer usage

Modern Debian (ppc64, debian-ports) on a CFW PS3 via OtherOS++/Petitboot, with the goal of running
nouveau's userspace (libdrm + Mesa nv30) on the RSX. Plan and background: [README.md](README.md).

## Layout

The tree, top two levels (each top folder has a three-line README.md):

```
packages/           every source package we build, by kind (make <name>-deb; docs/PACKAGING.md)
  walnut/           ours, native: system/ drivers/ tools/ apps/
  patched/          Debian's or upstream's source plus our patches: kernel/ graphics/ audio/ libs/
                    desktop/ tools/, and toolchain/spu/ (the SPU cross toolchain's patches)
  repackaged/       upstream releases packaged as they are
apt-repo/           the apt repositories' client side: walnut-repo.py, keyrings/, licensed-publish.sh
servers/            walnut.nanobyte.dev/: its containers, nginx, the upload and the publisher
website/            site/ (walnut.nanobyte.dev's front page) and gamedb/ (Walnut Games, Hugo)
gamedb/             the games database: everything about a game, one folder each (its schema in
                    gamedb/README.md); games/<id>/ holds the catalog entry, its played sessions,
                    its packages and licensed data, its benchmark, its technical notes
                    (technical.md, investigations/) and backlog (todo.toml); engines/<e>/ the
                    engines; research/ the surveys; benchmarks/ the batches
build-containers/   the builders' Dockerfiles
github-ci/          what GitHub Actions runs, and the generator of .github/workflows/pkg-*.yml
system/             image/ (the root filesystem image's lists and settings) and installer/
bench/              the game benchmark harness: harness/ runs games and records sessions and benchmarks
                    on the PS3, lib/ the recipes' shared pieces, analysis/ reads the runs, tests/ checks
                    it all (the recipes and results are in gamedb/games/<id>/benchmark/, the batches in
                    gamedb/benchmarks/)
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
| `system/ps3-games/` | The games' integration (native), one binary: `ps3-games-integration` (our launchers, the games' config files, diversions, migrate-home list; no game metapackages since 2026-10-28, docs/PACKAGING.md); the game-specific part is gamedb's, packed by `tools/gamedb/pack-integration.py` (`make gamedb-integration`) into `build/gamedb/integration.tar` (`COPY_IN`) |
| `system/ps3-splash/` | The Walnut Linux boot splash: Plymouth script theme (`theme/`), artwork generators (`art/`), `debian/` (the DRM renderer diverted, Plymouth drop-ins); also `ps3-no-initramfs` |
| `system/ps3-system/` | System configuration package: networking, SSH, swap (`ps3-vswap`: `PS3_VSWAP_DM=1` in `/etc/default/ps3-vswap` makes the VRAM swap's backing a device-mapper device, for the SSD swap arm), ps3gpu nodes, DS3 pairing, apt sources and pins, `ps3-gameos-mount`, OpenAL Soft's 16-bit output (`files/sound/alsoft.conf` as `/etc/xdg/alsoft.conf`) (`files/`, `debian/`) |
| `tools/ps3-gpu-tools/` | Programs run on the PS3: raw RSX probes and driver tests (`src/`: `rsx-*`, `rsxdrm-smoke`, `ps3-stallrec` the stall recorder and `ps3-waker` its A/B (bare periodic wakeups, cross-CPU kicks, per-CPU timer lateness), `ps3-intfwatch` the interference detector of every bench run (input, VT switches, devices, SSH logins and commands), docs/STALLS.md), scripts (`scripts/`: `rsx-suite.sh`, `rsx-x.sh`, `ps3-bench.sh`, `ps3-sm-temp.sh` the temperatures (docs/THERMAL.md), …), `preload/ps3-mallocstat.so` (glibc malloc statistics of a running game, docs/PROFILING.md), `preload/ps3-malloctrim.so` (gives a game's free malloc memory back every few seconds, docs/MEMORY.md), offscreen GL tests compared with softpipe (`mesa-tests/`) |
| `tools/ps3-spu/` | libspeq (`lib/`: resident SPE workers, job queues (bulk and the shared deadline pool), the DXT1/DXT5 encoders, the fluidsynth voice DSP, vertex skinning), the SPU programs (`spu/`: the runtime, `bench`, `dxt` (DXT1/DXT5), `deadline` (the synth and skin ops), `hello`), tools (`tools/`: `speq-dxt`, `speq-bench`, `speq-skin`; `src/spurun.c`), host tests (`tests/`, `make -C ps3-spu test`), design notes (`doc/skin.md`, `doc/pool.md`, `doc/dxt5.md`, `doc/dxt-rgb.md`); packages `libspeq0`, `libspeq-dev`, `ps3-spu`, `ps3-spu-programs` |

### packages/patched/: Debian's or upstream's source with our patches

| Path | What |
|---|---|
| `audio/fluidsynth/` | FluidSynth: Debian's `fluidsynth` source package plus `patches/` (0001 the voice DSP rendered through libspeq on the SPEs, a round of blocks per round trip, docs/SPU-AUDIO.md "fluidsynth on the SPEs", `SPEQ_SYNTH=auto|spu|ppe|off`; 0002 setting overrides from `/etc/fluidsynth/settings` and `FLUIDSYNTH_SETTINGS` that win over the program's; 0003 the Dattorro reverb's taps without a divide per sample) |
| `audio/libsfml/` | SFML: Debian's `libsfml` source package plus `patches/` (audio playback period 64 to 1024 frames, `SFML_AUDIO_PERIOD_FRAMES` override; [Extreme Tux Racer](../gamedb/games/etr/)'s xruns) |
| `audio/libvorbis/` | libvorbis: Debian's `libvorbis` source package plus `packaging/` (`-O3 -mcpu=cell`) and `patches/` (`ov_read`'s 16-bit packing in AltiVec; docs/SPU-AUDIO.md "Vorbis decoding on the PPE") |
| `audio/openal-soft/` | OpenAL Soft: Debian's `openal-soft` source package plus `packaging/` (built `-mcpu=cell`) and `patches/` (the mixer's per-sample integer/float conversions without load-hit-stores on the PPE: the resampler's fraction from tables, 16-bit samples in and out in AltiVec; docs/SPU-AUDIO.md "OpenAL Soft on the PPE"; the SPU mixer design that was not built: docs/research/OPENAL-SPU-MIXER.md) |
| `desktop/badwolf/` | Badwolf 1.4.0, the WebKitGTK shell for Walnut's browser, not the default yet (docs/research/BROWSERS.md: Debian's WebKitGTK crashes on big-endian) (upstream tarball, `upstream.mk`; Debian has none) plus `patches/` (one web context and web process for every tab, hardware acceleration off and the other memory defaults, JavaScript on, "Walnut" in the User-Agent, search and a start page, the common shortcuts, other schemes to the desktop) and `debian/start.html`, the start page (docs/DESKTOP.md "The browser") |
| `desktop/webkit2gtk/` | WebKitGTK: Debian's `webkit2gtk` 2.52.6-1 source package (from the 2026-09-25 snapshot) plus `packaging/` (the GTK 3 flavour only, the features a PS3 browser leaves out, `-mtune=cell`) and `patches/` (the big-endian fixes: CompactVariant, the HTML parser's UTF-16 de-interleave, Uint8Array hex; no view transitions without compositing; an include); `BUILD=xcc`, built on the dev PC (docs/research/BROWSERS.md "Our build", docs/PACKAGING.md "Built on the dev PC") |
| `desktop/plymouth/` | Debian's `plymouth` source package plus `patches/` (the script plugin without a console viewer) |
| `desktop/pnmixer/` | Debian's `pnmixer` source package plus `patches/` (no crash on a volume change without a sound card) |
| `graphics/libpng1.6/` | libpng: Debian's `libpng1.6` source package plus `packaging/` (`-O3 -mcpu=cell`) and `patches/` (the row unfilters Up, Sub, Avg and Paeth for 3- and 4-byte pixels in AltiVec without load-hit-stores, `powerpc/filter_cell.h`; docs/PROFILING.md "Load-time decoders and linking") |
| `graphics/libsdl3/` | SDL3: Debian's `libsdl3` source package plus `patches/` (big-endian: the window framebuffer texture in ARGB8888/XRGB8888, uploaded natively by the OpenGL renderer; a faster scalar 8888 alpha blit between formats, [Warmux](../gamedb/games/warmux/)) and `tests/` (that blit against the original) |
| `graphics/mesa/` | Our Mesa: Debian's `mesa` source package (`package.conf`: version and .dsc hash) plus `patches/` (our driver changes, a quilt series added as `debian/patches/ps3/`) and `packaging/` (changes to `debian/` itself: nouveau + softpipe only, no LLVM, on ppc64); `tests/` host tests of patch code (`make test`) |
| `graphics/sdl12-compat/` | SDL 1.2 on SDL2: Debian's `sdl12-compat` source package plus `patches/` (the screen texture in the game's own 32-bit format: no whole-frame conversion on every update, [Warmux](../gamedb/games/warmux/)) |
| `graphics/sdl2-compat/` | SDL2 on SDL3: Debian's `sdl2-compat` source package plus `patches/` (closing the audio device while holding its lock doesn't deadlock: SDL 1.2 games through sdl12-compat do it, [Armagetron Advanced](../gamedb/games/armagetronad/) hung on quit) |
| `kernel/linux/` | The kernel: `patches/series` (PS3 patches applied in order: `ps3/` from psx-place, `local/` ours), `config/*.config` fragments merged onto `ps3_defconfig`; as Debian's `linux` source package (`package.conf`: version, .dsc hash, flavours and their fragments): `packaging/` (changes to `debian/`: only our two kernels, release `6.12.111+ps3.N` and debug `6.12.111+ps3.N-debug<stamp>` (Debian flavours `ps3`, `ps3-debug`), no initramfs), `flavours/release.args`, `debug.args` (boot arguments, shipped as `/usr/lib/ps3/boot/<release>.args`); `tests/`: host tests of kernel patch logic (`ps3disk-guard.sh`: local/0045's range checks, from the patch itself; `guard-header.sh` prints the patch's `ps3disk_guard.h` for the tests that compile it; `sixaxis-idle-axes.sh`: local/0050 in a QEMU guest, an x86 kernel from `build/src/linux` and a DS3 made over uhid by `sixaxis-uhid-init.c`, hid-sony with and without the patch) and `ds3-idle-check.py` (on the PS3: the plugged-in DS3's axes before its PS button) |
| `libs/boost-context/` | Boost.Context, Coroutine and Fiber 1.92: Debian's `boost1.92` source package plus `packaging/` (0001: the fcontext assembler on ppc64 instead of ucontext, Debian #1146645; 0002: build profile `pkg.boost.contextonly`, only those three libraries and their -dev packages, the rest stays Debian's 1.92.0-3) and `patches/` (the ppc64 switch keeps F14-F31, FPSCR and V20-V31); `test/run-test.sh` (a fiber, asio::spawn, Coroutine and Fiber test plus Boost's own tests, under qemu) |
| `libs/luajit/` | LuaJIT: Debian's `luajit` source package plus `patches/` (ppc64 big-endian allocates in the low 2 GB, where it checks the state: Debian's fails `luaL_newstate()` about half the time) |
| `tools/game-data-packager/` | Debian's `game-data-packager` 89 (contrib, a native source) plus `packaging/` (0001: build profile `pkg.game-data-packager.nowrappers`, only the tool and its runtime; 0002: the unpackers as Recommends) and `patches/` (0003 `--progress-fd FD`, machine-readable progress and error kinds; 0004 `--no-launchers`, the Doom family without menu entries or icons); `tests/`: `test_gdp_patches.py` on the built deb in the deb builder image (`run-in-builder.sh`, suite `game-data-packager`) |
| `toolchain/spu/` | The spu-elf cross toolchain's patches: GCC 16.2 port (`gcc/`), binutils 2.47 fix, newlib 4.6 fixes, test scripts |

### packages/repackaged/

| Path | What |
|---|---|
| `repackaged/zlib-ng/` | zlib-ng 2.3.3 (upstream tarball, `packages/repackaged/zlib-ng/upstream.mk`; Debian has none) in zlib-compat mode: package `libz-ng-compat`, a second `libz.so.1` in `/usr/lib/powerpc64-linux-gnu/zlib-ng` that every game started through `ps3-game` loads (`LD_LIBRARY_PATH`; `PS3_ZLIB=stock` or the bench token `ZLIB=stock` off); zlib1g stays the system zlib (docs/PROFILING.md "Load-time decoders and linking") |
| `repackaged/tk-html3/` | Tkhtml 3.0, TkinterWeb's fork (the 3.0 branch's commit of 2026-09-19, `upstream.mk`; Debian dropped its tk-html3): package `tk-html3`, the Tcl package `Tkhtml` in `/usr/lib/tcltk/<triplet>/tkhtml3.0`, the HTML/CSS 2.1 widget of Walnut Games' Details view (docs/GAMEDB.md "The desktop app") |
| `repackaged/tkinterweb/` | TkinterWeb 4.25.5 (PyPI's sdist, `upstream.mk`): package `python3-tkinterweb`, Tkhtml for Tkinter (`HtmlFrame`); `debian/tkinterweb_tkhtml/` replaces upstream's loader package, which bundles Tkhtml binaries for x86 and arm only, with one that loads tk-html3's |

### gamedb/: the games' packages

A package that belongs to one game lives in that game's bundle, `gamedb/games/<id>/packages/<pkg>/`; an engine that is
an upstream project of its own (with `engine.md`: its games) in `gamedb/engines/<e>/packages/<pkg>/`; a licensed data
package or its builder in `gamedb/games/<id>/licensed/<pkg>/` (licensed by its place: built here only, never by CI,
never public). A game without a published page has a draft bundle (`draft = true` in its `index.md`). The schema is
gamedb/README.md.

<!-- gamedb:gamedb-packages -->
| Path (in gamedb/) | For | Source | Patches |
|---|---|---|---|
| `engines/cube2/packages/cube2/` | [Cube 2 (engine)](../gamedb/engines/cube2/) | Debian's `cube2` 0.0.20201229+dfsg-1 | 11 |
| `engines/darkplaces/packages/darkplaces-gl2/` | [DarkPlaces (engine)](../gamedb/engines/darkplaces/) | upstream tarball (`upstream.mk`) | 14 (+ `debian/`) |
| `engines/ioquake3/packages/ioquake3/` | [ioquake3 (engine)](../gamedb/engines/ioquake3/) | Debian's `ioquake3` 1.36+u20260719.58839361+dfsg-1 | 30 (+ `packaging/`) |
| `games/armagetronad/packages/armagetronad/` | [Armagetron Advanced](../gamedb/games/armagetronad/) | Debian's `armagetronad` 0.2.9.3.0-1 | 2 |
| `games/bomberclone/packages/bomberclone/` | [BomberClone](../gamedb/games/bomberclone/) | Debian's `bomberclone` 0.11.9-9 | 3 |
| `games/bzflag/packages/bzflag/` | [BZFlag](../gamedb/games/bzflag/) | Debian's `bzflag` 2.4.30-4 | 1 |
| `games/chromium-bsu/packages/chromium-bsu/` | [Chromium B.S.U.](../gamedb/games/chromium-bsu/) | Debian's `chromium-bsu` 0.9.16.1-5 | 2 |
| `games/classicube/packages/classicube/` | [ClassiCube](../gamedb/games/classicube/) | upstream tarball (`upstream.mk`) | 4 (+ `debian/`) |
| `games/colobot/packages/colobot/` | [Colobot: Gold Edition](../gamedb/games/colobot/) | Debian's `colobot` 0.2.2-4 | 2 (+ `packaging/`) |
| `games/crawl-tiles/packages/crawl-walnut/` | [Dungeon Crawl Stone Soup](../gamedb/games/crawl-tiles/) | native (ours) | none (+ `debian/`) |
| `games/ddnet/packages/ddnet/` | [DDNet](../gamedb/games/ddnet/) | Debian's `ddnet` 20.1.1+ds-1 | 1 |
| `games/diablo/packages/devilutionx/` | [Diablo (DevilutionX)](../gamedb/games/diablo/) | upstream tarball (`upstream.mk`) | 1 (+ `debian/`) |
| `games/dsda-doom/packages/dsda-doom/` | [dsda-doom](../gamedb/games/dsda-doom/) | Debian's `dsda-doom` 0.30.0+dfsg-1 | 7 |
| `games/endless-sky/packages/endless-sky/` | [Endless Sky](../gamedb/games/endless-sky/) | Debian's `endless-sky` 0.11.2-1 | 11 (+ `packaging/`) |
| `games/etr/packages/etr/` | [Extreme Tux Racer](../gamedb/games/etr/) | Debian's `extremetuxracer` 0.8.4-2 | 8 |
| `games/hedgewars/packages/hedgewars/` | [Hedgewars](../gamedb/games/hedgewars/) | Debian's `hedgewars` 1.0.3-5 | 1 (+ `packaging/`) |
| `games/heroes-of-might-and-magic-2/packages/fheroes2/` | [Heroes of Might and Magic II (fheroes2)](../gamedb/games/heroes-of-might-and-magic-2/) | Debian's `fheroes2` 1.1.17+dfsg-1 | 1 |
| `games/luanti/packages/luanti/` | [Luanti](../gamedb/games/luanti/) | Debian's `luanti` 5.15.2+dfsg-2 | 10 (+ `packaging/`) |
| `games/megaglest/packages/megaglest/` | [MegaGlest](../gamedb/games/megaglest/) | Debian's `megaglest` 3.13.0-10.1 | 1 |
| `games/mrboom/packages/mrboom/` | [Mr.Boom](../gamedb/games/mrboom/) | Debian's `mrboom` 5.5+ds-1 | 1 |
| `games/neverball/packages/neverball/` | [Neverball](../gamedb/games/neverball/) | Debian's `neverball` 1.6.0+git20180603-3 | 4 |
| `games/openrct2/packages/openrct2/` | [OpenRCT2](../gamedb/games/openrct2/) | upstream tarball (`upstream.mk`) | 7 (+ `debian/`) |
| `games/openttd/packages/openttd/` | [OpenTTD](../gamedb/games/openttd/) | Debian's `openttd` 15.1-1 | 3 |
| `games/opentyrian/packages/opentyrian/` | [OpenTyrian](../gamedb/games/opentyrian/) | Debian's `opentyrian` 2.1.20260912-1 | none (+ `packaging/`) |
| `games/sauerbraten/packages/sauerbraten-walnut/` | [Sauerbraten](../gamedb/games/sauerbraten/) | native (ours) | none (+ `debian/`) |
| `games/scorched3d/packages/scorched3d/` | [Scorched3D](../gamedb/games/scorched3d/) | Debian's `scorched3d` 44+dfsg-8 | 1 |
| `games/supertuxkart/packages/supertuxkart/` | [SuperTuxKart](../gamedb/games/supertuxkart/) | Debian's `supertuxkart` 1.5-2 | 7 |
| `games/teeworlds/packages/teeworlds/` | [Teeworlds](../gamedb/games/teeworlds/) | Debian's `teeworlds` 0.7.5-3 | 1 |
| `games/trigger-rally/packages/trigger-rally/` | [Trigger Rally](../gamedb/games/trigger-rally/) | Debian's `trigger-rally` 0.6.6.1-4 | 2 |
| `games/uqm/packages/uqm/` | [The Ur-Quan Masters](../gamedb/games/uqm/) | Debian's `uqm` 0.8.0+dfsg-4 | none (+ `packaging/`) |
| `games/warmux/packages/warmux/` | [Warmux](../gamedb/games/warmux/) | Debian's `warmux` 1:11.04.1+repack2-6 | 2 |
| `games/warzone2100/packages/warzone2100/` | [Warzone 2100](../gamedb/games/warzone2100/) | upstream tarball (`upstream.mk`) | 10 (+ `debian/`) |
| `games/wesnoth/packages/wesnoth/` | [Battle for Wesnoth](../gamedb/games/wesnoth/) | Debian's `wesnoth-1.18` 1:1.18.8-1 | none (+ `packaging/`) |
| `games/xmoto/packages/xmoto/` | [X-Moto](../gamedb/games/xmoto/) | Debian's `xmoto` 0.6.3+repack-1 | 5 |
| `games/yquake2/packages/yquake2/` | [Yamagi Quake II](../gamedb/games/yquake2/) | Debian's `yquake2` 8.70+dfsg-1 | 3 |
| `games/bananabread/licensed/sauerbraten-nonfree-data/` | [BananaBread, licensed data](../gamedb/games/bananabread/) | upstream tarball (`upstream.mk`) | none (+ `debian/`) |
| `games/classicube/licensed/classicube-data/` | [ClassiCube, licensed data](../gamedb/games/classicube/) | upstream tarball (`upstream.mk`) | none (+ `debian/`) |
| `games/opentyrian/licensed/tyrian-data/` | [OpenTyrian, licensed data](../gamedb/games/opentyrian/) | script `tyrian-data.sh` | none |
| `games/sauerbraten/licensed/sauerbraten-data/` | [Sauerbraten, licensed data](../gamedb/games/sauerbraten/) | upstream tarball (`upstream.mk`) | none (+ `debian/`) |
<!-- /gamedb:gamedb-packages -->

What a package's patches do: its `patches/series` and the patch headers, and its game's technical page
(the site lists every package with its patch subjects). The upstream drafts of our game patches are each
game's `upstream.md` with `upstream/*.patch` ([upstream/README.md](upstream/README.md) lists them).

### Everything else

| Path | What |
|---|---|
| `apt-repo/` | `walnut-repo.py` (a suite's next state from its published, signed one, no database: `add`, `fetch`, `base`; the licensed suite's login from `WALNUT_APT_AUTH_FILE`, `--strict` for its files that never change) and `test_walnut_repo.py` (against the real walnut-watch, suite `walnut-repo`), `keyrings/` (the archive key: releases, stable, licensed; the CI key: testing; ps3-system ships copies), `licensed-publish.sh` (`make publish`: licensed packages built here to the licensed suite), `licensed-auth.sh` (that suite's login from config.local.mk or pass, into a 600 file) |
| `servers/walnut.nanobyte.dev/` | walnut.nanobyte.dev on the OVH box (docs/release/ALPHA.md M4): `walnut-repo` (nginx: the apt suites, downloads and the password-protected licensed suite), `walnut-upload` (`upload/`: sshd + rrsync, write-only uploads into incoming/), `walnut-watch` (publishes uploads atomically; `test_walnut_watch.py`), `walnut-upload.sh` and `publish-site.sh` (the client side, also used by `.github/workflows/walnut-site.yml`), `walnut-site` (nginx with website/site/ built in), the main proxy's site config; `make walnut-server` deploys it, `make walnut-site` publishes the site |
| `website/site/` | walnut.nanobyte.dev's front page (static: `index.html`, `style.css`, the public keys); published with the gamedb by `servers/walnut.nanobyte.dev/publish-site.sh` |
| `website/gamedb/` | The Walnut Games site (Hugo; https://walnut.nanobyte.dev/gamedb/): `hugo.toml`, `layouts/` (card grid home, a game's four tabs), `assets/gamedb.scss` (compiled to CSS 2.1) and `gamedb.js` (filters, search, copy button; optional), `content/` the site's own pages (`content/games/_index.md`: the games section; `content/tabs/`: the Test runs and Guides tabs); the games themselves come from `gamedb/games/<id>/`, mounted as `content/games/<id>/` (`hugo.toml`'s `[[module.mounts]]`, an allowlist: `index.md`, `thumb.png`, the sessions' `shots/*.jpg`): `index.md` (the catalog entry as TOML front matter, notes as the body), `thumb.png`, `rating/sessions/<date>-<kind>/result.toml` (generated by `import-run.py`) with the session's evidence: `shots/` (every screenshot, `ps3-*.jpg`; `pc/pc-*.jpg` the PC reference; Git LFS, `.gitattributes`), `form.toml`, `review.md`, `raw/` (private); `data/catalog.json` is generated and ignored |
| `build-containers/` | Pinned build environments: `Dockerfile` (toolchain, mmdebstrap), `.deb` (Debian packaging tools, the deb builder), `.games` ([ClassiCube](../gamedb/games/classicube/)'s sound conversion), `.spu`, `.benchref` (the PC reference of a bench recipe: Xvfb, llvmpipe, the game; tools/bench-ref.sh), `.playpc` (a game to play on the PC beside a PS3 session: llvmpipe at GL 2.1 on the desktop's X, its sound; tools/play-pc.sh), `.xcc` (the ppc64 cross compiler that `BUILD=xcc` runs inside the ppc64 chroot; tools/xcc-build.sh) |
| `github-ci/` | GitHub Actions' side (docs/release/ALPHA.md M6): `build-package.sh` (one package, no secrets; refuses licensed ones), `publish.sh` (builds to testing or stable in one upload, through apt-repo/walnut-repo.py), `batch-publish.py` (testing's one publisher: every build testing lacks, from the builds' artifacts) and `test_batch_publish.py`, `build-release.sh`, `publish-release.sh`, `release-id.sh`, `release-notes.sh` (the release image), `runner-prep.sh`, `gen-workflows.py` (`.github/workflows/pkg-<name>.yml` for every package but the licensed ones, and `walnut-publish.yml`) |
| `system/image/` | The root filesystem image: `packages.txt` (slow Debian layer), `packages-ours.txt` (ours, installed with apt), `customize.sh` (per-installation settings: hostname, user, key, locale) |
| `system/installer/` | `walnut-install` (the PC-side installer of a release onto the PS3's internal disk or a USB drive), `test_walnut_install.py`, the release archive's README |
| `bench/` | The game suite runner (docs/games/SUITE.md), no game files in it: four folders, the same on the PS3 (`make bench-push` copies them to `/var/tmp/bench/`, every game's benchmark and played test to `/var/tmp/bench/games/<id>/`). `harness/` runs games and records sessions and benchmarks, on the PS3: `run.sh smoke\|bench\|suite\|list\|session` through `ps3-bench.sh`, `client.sh` (one run from a recipe, `gamedb/games/<id>/benchmark/recipe.sh`), `session.sh` with `session-form.py`, `launcher.py`, `freshhome.py`, `inputlog.py`, `geom.py` (a played session), `xruns.py`, `perf-post.sh` (the profiles after a `PERF=1` run), `ssd-arm.sh` (the SSD arms `GAMEDATA=ssd`, `SWAP=ssd`, docs/GAMEOS-HDD.md "Test drive, Phase A"), `gamemode-check.sh` (game mode on the desktop: memory idle, in game mode and after, and checks that everything frozen or unloaded comes back), and the game resolver: `games.sh` and `games.py` find a game's benchmark, `playdef.py` reads its played test (`rating/test.toml`) for session.sh and client.sh, `gamedata.py` its analysis rules (`benchmark/analysis.toml`) for the analysis tools. `lib/` is the recipes' `$BENCH_LIB`: `smoke.inc`, `openal.inc`, `xkeys.py`, `keyboard.evemu`. `analysis/` reads the runs (on the PC; report.py and the profile readers also on the PS3): `report.py`; `stalls.py`, `passsplit.py` (why frames stall, from `ps3-stallrec`'s record: `STALLS=1`, docs/STALLS.md); `perfprof.py` (CPU profiles of the window: `PERF=1`), `memprof.py` (the game's memory by mapping class, malloc, GPU buffers: `MEM=1`), `loadprof.py`, `irqoff.py`, docs/PROFILING.md; `hotpaths/` (perf.data read and symbolised on the PC, the graphics hot paths ranked across games: `symroot.sh`, `analyze.py`, `categorize.py`, `stacks.py` (callers, callees and leaves of a function), docs/HOTPATHS.md; `perfq.sh`: the PS3's perf under qemu for perf-post.sh on the PC; `elfsym.py` also serves perfprof.py on the PS3); `interference-scan.py` (runs from before the interference detector: what their files show of somebody disturbing them, `--write` into `gamedb/benchmarks/interfered.txt`, which report.py and matrix.py leave out; docs/STALLS.md "Interference"); `oal-perf.sh` (OpenAL Soft's mixing split from a profiled run, docs/SPU-AUDIO.md M0); `matrix.py` (a batch's runs into the benchmark records: gamedb/benchmarks/batches.toml and each game's `benchmark/results/<batch>.toml`, read and written through `benchrecords.py`, which tools/gamedb reads too; gamedb/benchmarks/README.md says how to read them; `--csv DIR`, `make bench-matrix`: the CSV matrices, one per metric, into build/bench-matrix/); `logscan.py` (the runs' log errors, docs/games/LOG-FINDINGS.md); `dumpana.sh` (a failure dump of a `DUMPON=1` run, core and state, analysed with gdb and the run's debug symbols; docs/games/LOG-FINDINGS.md "Catching failures"); `shots.py` (the PS3's screenshots: `SHOTS=`, xwd to PNG, the PS3 \| PC sheet `<run>/shots-vs-pc.png`, the capture's cost in frames, the sheets nobody reviewed; docs/games/SUITE.md "Screenshots"). `tests/`, without the PS3 (`make test`): `selftest.py` (the harness, the analysis and every recipe; `intfcheck.py` its interference part), `test_launcher.py` (launcher.py and session.sh's launcher choice). A script finds the others from its own place: the shell ones set `BENCH` (the bench tree, `${here%/*}`) and call `$BENCH/analysis/...`, the Python ones put `bench/analysis` and `bench/harness` on `sys.path`. `make bench-push` / `bench-pull` / `bench-shots` |
| `gamedb/games/<id>/benchmark/` | A game's benchmark (gamedb/README.md): `recipe.sh`, `assets/`, `assets-src/` (ClassiCube's timedemo plugin, built by `make classicube-bench` into `assets/classicube-bench.so` with its `assets.mk`; [OpenTTD](../gamedb/games/openttd/)'s save generator), `lib/` ([Hedgewars](../gamedb/games/hedgewars/)' `hwdrive.py`), `analysis.toml` (the recipe's old name), `legacy/` ([OpenArena](../gamedb/games/openarena/)'s step 0 scripts) |
| `tools/` | Scripts called by the Makefile (`deb-*.sh`, `pkg-*.sh`: the source packages, `pkg-dir.sh NAME` the directory of one, `--all` every one, `--order` in build order; `deploy.sh`: `make deploy`; `licensed.py`: what is licensed (never built by CI nor published outside the licensed suite), from the licensed directories, gamedb's `build.toml` `[[licensed]]` entries and `dfsg-exclude.txt` files and `licensed-files.txt` (the generic leak globs), asked by every guard; `leak-check.py`; `public-export.sh`: the public GitHub copy, without the licensed directories; `image-packages.sh`: the image's package list per profile (the public one without the games whose data is licensed: `tools/gamedb/build-info.py needs-licensed`); `tools/gamedb/build-info.py`: what the Makefile and tools need from gamedb's `build.toml` files (`make game-data`, `make licensed-data`, play-pc's packages and data); `doclinks.py`: the docs' links and `gamedb:` references, checked or rewritten after moves; `test_repo_tools.py`: host tests of these); `webcam-grab.sh`, `bench-cam.sh` (webcam photos 30 s and 60 s into every PS3 bench run, over one SSH connection: the TV and the sound; the picture check is the screenshots now); `bench-ref.sh` (a recipe run on the PC in an amd64 container under Xvfb with llvmpipe at GL 2.1, screenshots at the PS3 run's shot points -> build/bench-ref, `make bench-ref GAME=x`); `ppecost/` (a qemu TCG plugin counting the PPE's load-hit-stores, multiplies and divides per guest PC, `attrib.py` per function, and the decoder/mixer harnesses it was run on: docs/PROFILING.md "Load-time decoders and linking") |
| `tools/gamedb/` | Walnut Games' tools (docs/GAMEDB.md): `score.py` (aspect scores, the weighted mean, the weakest-link cap and the Scrap floors; run by itself it prints the calibration table), `import-run.py` (harness run directories into a game's `rating/sessions/<date>-<kind>/`: the evidence, `evidence.py`: every screenshot as an 800x450 JPEG, the form, the review, the harness's files scrubbed in `raw/`; then `result.toml`, derived from that evidence alone), `reimport.py` (every session's record derived again from its evidence, no build/bench; `--check`), `gallery.py` (the gallery's paths, for CI's `git lfs pull -I`), `test_evidence.py` (suite `gamedb-evidence`), `check.py` (the catalog's schema, ids, packages, no licensed package), `catalog.py` (`catalog.json`: ratings, tests, "patched by Walnut" patch subjects, .deb links with `--deb-index`; drafts left out; `--diff OLD` prints what changed against an older catalog.json), `blocks.py` (the generated `<!-- gamedb:NAME -->` blocks in the docs: `--write`, `--check`; the docs' generators in `docblocks.py` and `gamesblocks.py`), `technical.py` (`technical.json`, the site's technical, investigation, engine and research pages, run by catalog.py; links and `gamedb:` references made site links, the public-safe scan; `test_technical.py`, suite `gamedb-technical`), `game-mentions.py` (the docs' sections that name a game without a GameDB link: a warning in make test, `--list`, `--fix`), `bench-import.py` (the old CSV matrices into the benchmark records, kept for `--verify`), `review.py` (what the reviewing agent needs to judge a played session's picture: crops and sheets of its screenshots), `pack-integration.py` (the ps3-games source's `integration.tar`, ps3-games-integration's tree, from every game's build.toml and `desktop/`), `baseline-check.py` (the GameDB migration's before/after check: `save`, `check` against build/tmp/gamedb-baseline), `build.sh OUT` (check, catalog, the pinned Hugo of `hugo.version`, then walnut-watch's site rules on the output; `--preview` for localhost), `test_gamedb.py` (suite `gamedb`) |
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
our own packages, in a minute or two. Everything we build is a .deb in `build/debs/<pkg>`. The system,
driver and library packages (each game's own packages are in the gamedb table above, and what they install
on the game's technical page):

| Package | Installs |
|---|---|
| `linux-image-<release>`, `linux-image-ps3`, `linux-image-ps3-debug` | the kernels (release `6.12.111+ps3.N`, debug `6.12.111+ps3.N-debug<stamp>`): `/boot/vmlinux-<release>`, modules, boot arguments; the metas follow the newest |
| `ps3-boot` | `ps3-update-kboot`: Petitboot's `/etc/kboot.conf` from the installed kernels (newest first, the default) and `/etc/kboot.d/*.conf` |
| `ps3-gpu-tools` | RSX probes, driver tests, the GL tests, `ps3-bench.sh`, `ps3-stallrec`, `ps3-waker` and `ps3-intfwatch` in `/usr/lib/ps3/bin` (on `PATH`), `ps3-mallocstat.so` and `ps3-malloctrim.so` in `/usr/lib/ps3/lib` |
| `mesa-libgallium`, `libgl1-mesa-dri`, `libegl-mesa0`, `libglx-mesa0`, `libgbm1`, … | Debian's Mesa packages rebuilt from `packages/patched/graphics/mesa/` (`26.2.4-1+ps3.N`), replacing Debian's |
| `libluajit-5.1-2`, `luajit`, ... | LuaJIT (`2.1.0+openresty20251030-1+ps3.N`), replacing Debian's: `luaL_newstate()` no longer fails on ppc64 |
| `libopenal1`, `libopenal-data`, `libopenal-dev`, `openal-tools` | OpenAL Soft (`1:1.25.2-1+ps3.N`), replacing Debian's: the mixer without load-hit-stores on the PPE (bit-identical output) |
| `libsfml-audio3.1` (and the other `libsfml*` binaries) | SFML (`3.1.0+dfsg-4+ps3.N`), replacing Debian's: a 1024 frame playback period instead of 64 (about 64 ms of buffer, `SFML_AUDIO_PERIOD_FRAMES` overrides) |
| `libvorbis0a`, `libvorbisfile3`, `libvorbisenc2`, `libvorbis-dev` | libvorbis (`1.3.7-3+ps3.N`), replacing Debian's: `ov_read`'s 16-bit packing in AltiVec (bit-identical output) |
| `libpng16-16t64`, `libpng-dev`, `libpng-tools` | libpng (`1.6.59-1+ps3.N`), replacing Debian's: PNG row unfiltering in AltiVec (bit-identical output) |
| `libz-ng-compat` | zlib-ng's zlib-compat `libz.so.1` in `/usr/lib/powerpc64-linux-gnu/zlib-ng`, used only by programs started with that directory in `LD_LIBRARY_PATH` |
| `badwolf` | Badwolf (`1.4.0-1+ps3.N`) on WebKitGTK, patched for 256 MB; registers `x-www-browser`. Not in the image and not the default: it waits for our `webkit2gtk`'s PS3 test (PLAN 4.16) |
| `libwebkit2gtk-4.1-0`, `libjavascriptcoregtk-4.1-0`, `libjavascriptcoregtk-bin`, their -dev | WebKitGTK (`2.52.6-1+ps3.N`), replacing Debian's: works on big-endian, the GTK 3 flavour only, slimmed (no WebGL, WebRTC, WebAudio, speech, ...); video with GStreamer kept. Not in the image yet (PLAN 4.16) |
| `tk-html3`, `python3-tkinterweb` | Tkhtml 3.0 (`3.0+git20260919.d6a6df3-1+ps3.N`) and TkinterWeb (`4.25.5-1+ps3.N`): HTML and CSS 2.1 pages in a Tk window, without JavaScript; Walnut Games' Details view |
| `libboost-context1.92.0`, `libboost-coroutine1.92.0`, `libboost-fiber1.92.0`, their -dev | Boost 1.92's context libraries (`1.92.0-3+ps3.N`), replacing Debian's: the fcontext assembler on ppc64 (Debian's build lacks make/jump_fcontext, and its libboost_coroutine can't link) with the FPRs and AltiVec registers kept across a switch |
| `libsdl3-0`, ... | SDL3 (`3.4.18+ds-1+ps3.N`), replacing Debian's: window framebuffer colours and native uploads on big-endian, a faster alpha blit without SIMD |
| `libsdl1.2debian`, ... | SDL 1.2 as sdl12-compat (`1.2.78-1+ps3.N`), replacing Debian's: the screen texture in the game's own format (no conversion per frame) |
| `libsdl2-2.0-0`, ... | SDL2 as sdl2-compat (`2.32.74+ds-1+ps3.N`), replacing Debian's: no deadlock when an app (or an SDL 1.2 game through sdl12-compat) closes the audio device with its lock held |
| `libfluidsynth3`, `fluidsynth`, ... | FluidSynth (`2.6.1+dfsg-1+ps3.N`), replacing Debian's: voice DSP on the SPEs through libspeq (`SPEQ_SYNTH`), setting overrides (`/etc/fluidsynth/settings`, `FLUIDSYNTH_SETTINGS`), a cheaper reverb |
| `libspeq0` | libspeq: SPU jobs from any program (resident workers on the SPEs); DXT1 compression for Mesa, fluidsynth's voice DSP and the Cube 2 engine's skinning, each loading it at run time (`SPEQ_S3TC`, `SPEQ_SYNTH`, `SPEQ_SKIN` = `auto\|spu\|ppe\|off`, `SPEQ_S3TC_RGB=ppe`, `SPEQ_DEBUG=1`) |
| `ps3-spu`, `ps3-spu-programs` | `spurun`, `speq-dxt`, `speq-bench`, spufs mounted at `/spu`; the SPU programs in `/usr/lib/ps3/spu` |
| `ps3rsx-modules-<release>`, `ps3rsx-modules-ps3`, `ps3rsx-modules-ps3-debug` | the RSX driver (loaded at boot) and probe modules for a kernel; the metas follow the release and debug kernels |
| `ps3-splash`, `plymouth`, `libplymouth5`, … | the boot splash (Walnut theme) on our patched Plymouth; `ps3-no-initramfs` stands in for initramfs-tools |
| `ps3-system`, `ps3-desktop` | the system and desktop configuration (see the layout above; ps3-system also makes OpenAL Soft's output 16-bit, `/etc/xdg/alsoft.conf`) |
| `ps3-apps` | Mousepad, GPicView, zathura, Xarchiver, Galculator, LXTask, scrot (Print key), mpv, opened by default from PCManFM; remove it to remove them |
| `walnut-games`, `ps3-games-integration` | Walnut Games, which installs the games (none is in the image: no game metapackages since 2026-10-28, docs/PACKAGING.md); ps3-games-integration: our launchers, the games' config files, diversions and migrate-home list (each game's gamedb `desktop/`). On the dev PS3 a game also deploys by name (`make game-deploy GAME=<id>`) |

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
    make test ONLY=ps3-spu       # one suite: bench-selftest, bench-launcher, gprefetch, ps3rsx-shadow, ps3-spu, xmoto-glbatch,
                                 #   mesa-forcedxt, ssdimg, ps3-boot, leak-check, repo-tools, licensed, workflows,
                                 #   blocks, doclinks and game-mentions (warnings only), walnut-repo, walnut-watch,
                                 #   batch-publish, gamedb, gamedb-evidence, gamedb-technical, walnut-games,
                                 #   ps3disk-guard, autogm, ssdimg-vm (the last two with make test-slow)

`tools/run-tests.sh` runs them: `bench/tests/selftest.py`, `bench/tests/test_launcher.py`, `packages/walnut/system/ps3-desktop/tests/gprefetch-check.py`,
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

