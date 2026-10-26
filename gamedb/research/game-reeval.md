# Excluded games re-evaluated (research, 2026-10-19)

The second pass over games first ruled out for reasons other than GL 3: RAM measured on the PC (amd64, Xvfb + llvmpipe, which adds about 100-200 MB), build and big-endian costs estimated. The first pass is build/tmp/newgames/RESEARCH.md, summarised in the games' technical notes (gamedb/games/<id>/technical.md). Builds of DDNet, OpenTyrian, UQM and Hedgewars started the same night.

**Where this lives now (2026-10-26).** This survey moved from docs/research to gamedb/research. Every game it names has a bundle in gamedb/games/ (a draft when it is only a candidate, `status` and `status_note` saying where it stands) whose technical.md cites this file by section. Part B (OpenRCT2's big-endian port estimate) moved into the game's investigations/. The per-game tables and blocks here are kept by hand for now; they become generated blocks (tools/gamedb/blocks.py) in a later step, and until then a game's current state is its bundle's, not this file's.


Rules applied (user, 2026-10-19): only a GL 3+/GLES 3/Vulkan requirement with no fallback renderer is a hard no.
Missing ppc64 binary, contrib, FTBFS, broken deps and BE bugs in a game's own code are costs. RAM bar: under 512 MB
fine; 512 MB to 1 GB stays a candidate (VRAM swap, trims, game-side memory patches), with the biggest consumers
named; 2 GB or more is not worth it unless mostly cheaply avoidable. Novelty (no PS3 GameOS homebrew port) ranks
higher at equal cost. Java and .NET are judged on the game's own needs first; the runtime is a separate cost line.

Detail files: this one (my games), `reeval-part-A.md` (Reddit-list engine ports, source-read, all RAM are
estimates there), `reeval-part-B-openrct2.md` (OpenRCT2, with measured RSS). Scripts for my measurements:
`build/tmp/reeval-pc/`.

## How RAM was measured, and what I installed

Throwaway podman containers (`debian:sid-slim` amd64, storage on `/mnt/data_linux/Temp/ps3/pm`, nothing installed
on the host), Xvfb 1280x720, Mesa llvmpipe (software GL), `VmHWM` from `/proc/<pid>/status`. Caveat for every
number below: llvmpipe lives inside the game process and adds roughly 100-200 MB (LLVM JIT, textures in system
RAM), so the PS3 figure is lower for GL games; amd64 pointers are the same size as ppc64's. Installed in the
containers: xvfb, mesa, 0ad + 0ad-data (3.5 GB), hedgewars, ddnet, openjdk-21-jre, freecol, opentyrian, uqm +
uqm-content; downloaded Mindustry v160.7 jar and tyrian21.zip to `/mnt/data_linux/Temp/ps3/dl`.

## Ranked table

"GameOS port?" is from my memory and unverified unless noted. Rank = verdict first, then novelty, then cost.

| # | Game | Hard no? | ppc64 binary | Costs (build / BE / data) | RAM | GameOS port? | Effort | Verdict |
|---|---|---|---|---|---|---|---|---|
| 1 | DDNet 20.1.1 | no: tried GL 3.3, then "Setting GL version 1.1.0 and trying again" (measured in log) | main has 16.0.2 (old); 20.1.1 FTBFS only by a sandbox test hang (`Net.Ipv4AndIpv6Work`): build with nocheck | BE fixes upstream; data in package; needs a server (ddnet-server local, or internet) | 328 MB client at menu (includes llvmpipe; real likely 150-250) | none known | S | **try now** (rebuild 20.1.1 nocheck, or install the old 16.0.2 for a first look) |
| 2 | Hedgewars 1.0.3 | no (GL 1.x path, Qt frontend) | no (buildd: tests fail) | FPC `EInvalidOp` in 2 lua tests: FPC on PPC traps FP invalid ops that x86 masks; hwengine.pas never calls SetExceptionMask. Fix = mask FP exceptions at start (`SetExceptionMask([...])`), 5-10 lines, rebuild nocheck, re-run tests. hwFloat physics is fixed point and already has `ENDIAN_LITTLE/BIG` variants (uFloat.pas), so netplay desync risk is low. Data 160 MB | engine peak 231 MB (one lua test, 1.6 s, so a floor; menu + Qt frontend add ~100) | none known | S | **try now** (build with the FPU mask patch) |
| 3 | OpenTyrian 2.1 | no (SDL2 software) | contrib: no binary | plain `apt source` build in the chroot; Tyrian 2.1 freeware zip (4.7 MB, camanis.net) | 140 MB at menu (measured, mostly SDL/libs) | none known | S | **try now** |
| 4 | Ur-Quan Masters 0.8.0 | no (SDL2 software/GL) | contrib: no binary | plain build; `uqm-content` 11 MB (non-free, arch all, goes to ps3-licensed or just install) | 146 MB at menu (measured) | none known | S | **try now** |
| 5 | Micropolis | no (Tk/X11 + SDL 1.2 sound, no GL) | Debian dropped it (`micropolis-activity` 0.0.20071228-10 is gone from sid, only the games-* metapackages still mention it) | package from upstream (SimHacker/micropolis, GPL) or snapshot.debian.org source; Tcl/Tk 8.6 + X11 shm patches; BE: code is from Sun/SGI-era, I did not verify the swap macros | estimate 30-60 MB (Tk) | none | S-M | **worth a build** (cheap, novel, low appeal) |
| 6 | Freeciv SDL2 client | no | `freeciv-client-sdl` is in main | only the Depends: fonts-noto-cjk, fonts-ipafont-gothic, fonts-arphic-uming (+174 MB). Edit the control file of the .deb (or rebuild with them moved to Recommends); the client falls back to DejaVu for non-CJK text. No code change | like GTK3 client, small (not measured; GTK3 client is already in the suite) | none | S | try now (low value: GTK3 client already covers Freeciv) |
| 7 | OpenRCT2 0.5.6 | **no hard no**: GL renderer needs GL 3.3 core (no GL2 path) but the software renderer through SDL works | contrib, `architecture-is-little-endian` | BE port: reuse the closed AmigaOS PR #27114 (~1,700 endian lines in 33 files, applies to 0.5.6 with 2 conflicts), plus TD6/TD4 import, RLE zoom, ImageImporter, Ogg/FLAC fixes: 7-11 person-days. Data: our rct2-data deb | measured: GUI no-GL 282-300 MB RSS (295 peak), llvmpipe 540-578 MB peak, headless 154 MB; trimmed estimate 210-230 MB | none; this would be a first | L | **worth a build, staged** (RAM decides; see B) |
| 8 | 0 A.D. 0.28 | no: GL 2.0 + VBO + FBO (EXT ok), GLSL path, ARB-program fallback still exists in `gl/Device.cpp`; Vulkan optional | no (arch list amd64 arm64 i386 ppc64el) | not SpiderMonkey: mozjs 128 compiled fine on ppc64 in the buildd log. The real FTBFS is two `#error` in `BinarySerializer.cpp` (lines 165, 456): "convert JS ArrayBuffer data / JS strings to little-endian" (saves, replays, sync hashes). Fix = byte-swap the buffer/UTF-16 payloads (M). SpiderMonkey has no ppc64 JIT in 128, so the JS runs in the interpreter (AI and GUI scripts slow: unmeasured). Build is hours. Data 0ad-data 3.5 GB installed | **measured 800 MB RSS** (arcadia scenario, 2 players + 1 AI, 1280x720 low, 3 min steady; 584 MB at 10 s, 790 MB at 30 s); minus llvmpipe 100-200 MB => ~600-700 MB | none | L | **worth a build, low priority** (candidate in the 512 MB-1 GB band; consumers to cut: textures (texture quality/half-res), SpiderMonkey heap, 3.5 GB data on disk, map size). CPU is the larger risk. |
| 9 | Xonotic / darkplaces | **no hard no**: Debian's darkplaces requests GL 3.2 core, but the engine has a `USE_GLES2` build (vid_sdl.c, makefile.inc lines 247/265, ES 2.0 context) and nv30 does GLES 2.0 | none (not in Debian); Debian darkplaces is GL 3.2 only | build darkplaces with `-DUSE_GLES2` (or our darkplaces-gl2 for Nexuiz); Xonotic data ~1 GB; GLSL ES shaders, no float textures/VTF needed for the basic path (unverified) | estimate 700 MB-1.2 GB at low (not measured: 1 GB download), textures dominate (`gl_picmip`, `r_picmipworld`) | none | L | not worth it now (revisit after darkplaces-gl2/Nexuiz runs) |
| 10 | OpenMW 0.51 | **no**: shaders are `#version 120` compatibility profile (`files/shaders/compatibility`); core-profile shaders are not implemented (shadermanager TODO) | yes, 0.51.0-1+b4 (53 deps incl. GDAL/HDF5, 282 MB) | data: Morrowind (retail, GOG/Steam; no free set; the OpenMW example suite is only test content). BE: `components/misc/endianness.hpp` exists, ESM/NIF byte order not verified | not measurable without data; estimate 600 MB-1.2 GB mid-game (cell cache, Morrowind.esm), 3.2 GHz in-order PPE CPU-bound OSG scene is the larger problem | none | M (config) | try now **if the user owns Morrowind**; otherwise skip |
| 11 | Mindustry v160.7 (Java) | no: Arc/libGDX GL 2.0 path | needs Arc/LWJGL/SDL natives for ppc64 BE (not shipped: jar carries x86/arm only) | Runtime: Debian `openjdk-21/25-jre` on ppc64 is full **HotSpot** (`hotspot_archs` includes ppc64: C1/C2 JIT, Zero only as alt VM), so the VM is not the blocker. Real cost: build the natives (gdx-jnigen/Arc, OpenAL JNI) for ppc64 BE, check Arc's byte-order assumptions in its native buffers | **measured 760 MB RSS at the menu with `-Xmx300m -XX:+UseSerialGC`** (JVM+JIT+natives; 100-200 MB of it is llvmpipe) | none | L | not worth it now (candidate band, natives L; revisit) |
| 12 | FreeCol 1.2.0 (Java, Swing, no GL) | no GL | main package (arch all) + HotSpot | The game itself refuses to start below ~0.55 GB heap ("You need to assign more than 518979584 bytes", measured with -Xmx512M) and warns "recommended 2 GB" at 600M. A code patch could drop the check but the maps/AI need the memory | >= 600 MB heap + JVM, recommended 2 GB | none | M | **not worth it** (RAM by its own check) |
| 13 | OpenRA, Barotrauma (.NET) | **yes: OpenRA** builds GL profiles ANGLE / Modern (3.2 core) / Embedded (GLES 3.0), `Sdl2PlatformWindow.SetSDLAttributes`; no legacy path. Barotrauma is MonoGame GL 3.x and proprietary | n/a | .NET runtime: Microsoft's has no ppc64 port; Debian ships Mono 6.14.1 for ppc64 (installable), no dotnet. OpenRA targets .NET 6/8, not Mono | n/a | none | n/a | **drop** (GL 3.2 core, no fallback) |
| 14 | Warzone 2100 4.7 | **yes**: `gfx_api_gl.cpp:3951` returns false below GL 3.0 / GLES 3.0 (the GLSL 120 shaders exist only for 3.0/3.1 contexts); also a BE static assert in GameNetworkingSockets | no | | | | | **drop 4.7** (confirmed); **4.3.5 is being ported** instead, the last release with a GL 2.1 / GLES 2 renderer (package in gamedb/games/warzone2100/packages/warzone2100/, in smoke testing) |

The Reddit-list games are merged into the ranking below from `reeval-part-A.md`.

## Merged ranking of everything (top is best)

Try now, novel, cheap (all unmeasured on the PS3, first six measured on the PC or source-read):

1. DDNet (S). 328 MB client measured; rebuild with nocheck.
2. OpenTyrian (S). 140 MB measured; free data.
3. UQM (S). 146 MB measured; content from non-free.
4. Hedgewars (S). 231 MB engine floor measured; FPU-mask patch.
5. fheroes2 (S) (A): contrib build, HoMM2 demo data supported, est. 100-250 MB.
6. devilutionX (S) (A): own build, `spawn.mpq` shareware, est. 60-120 MB.
7. eduke32 (S) (A): own build, Duke3D shareware, est. 40-100 MB (Wii port exists upstream: partial GameOS-family precedent, no PS3 port known).
8. UFO:AI (S) (A): ppc64 binary in main, free data, but est. 400-900 MB (largest consumer likely textures/maps): candidate band, not a quick win.
9. VVVVVV (S) (A): private build only (licence), Make-and-Play data.
10. Micropolis (S-M): packaging only.
11. ET:Legacy (S-M) (A), measure fps first.

Worth a build, own data needed: exult, iortcw, rednukem, NBlood, openxcom/OXCE (vanilla first), doukutsu-rs (cargo, BE unproven).

Worth a build, larger: OpenRCT2 (staged, RAM decides), 0 A.D. (RAM ~650 MB, BE `#error` x2, interpreter-only JS).

Needs data the user must own: OpenMW (Morrowind).

Not worth it: FreeCol (own 512 MB+ heap check), Mindustry (natives L, ~600 MB), Xonotic (1 GB, L), arx-libertatis, vcmi, dhewm3, openmohaa, Raze (see part A).

Hard no (GL 3+ with no fallback): Warzone 2100 4.7 (4.3.5 is ported instead, see the table), OpenRA, Barotrauma, fs2_open, rbdoom3bfg, barony, hypersomnia, veloren, The Dark Mod. Confirmed in source for Warzone, OpenRA; the rest per part A.

## Facts behind the verdicts

- **0 A.D.**: Debian dropped ppc64 on 2026-02-24 ("will FTBFS on all big-endian arches"). The 0.28.0-2 ppc64 buildd log shows SpiderMonkey 128 starting its configure/build and the game compiling until
  `BinarySerializer.cpp:165: #error TODO: need to convert JS ArrayBuffer data to little-endian` and `:456: #error TODO: probably need to convert JS strings to little-endian`. Those are the only errors in the log, but make stops there, so more may follow. GL: `Device.cpp` requires GL 2.0 (GLSL), `GL_ARB_vertex_buffer_object`, and EXT or ARB framebuffer objects; an ARB vertex/fragment-program path remains for GL < 2.0. nv30 offers all of these.
- **Hedgewars**: the buildd log fails `stuckcake.lua` with `EInvalidOp` before any simulation step (`Getting game config...`), consistent with an FPC FPU trap, not physics drift. `gravity_get_set.lua` fails separately (likely the same mask or its own float compare); not reproduced here since I have no ppc64 FPC. A pas2c/clang build would avoid FPC altogether but is a bigger job (M-L); the FPC mask fix is the thing to try first.
- **DDNet**: current buildd failure is a test hang (IPv6 in the sandbox), not the code.
- **OpenRCT2 vs the first pass**: the first pass said "weeks"; part B found an existing, closed BE patch (AmigaOS PR #27114) that cuts it to 7-11 days.
- **Java**: HotSpot on ppc64 BE is the default VM in Debian openjdk-21 and -25, so a JIT exists. The JVM's own overhead (about 100 MB beyond the heap at idle) is what hurts at 217 MB.
- **.NET**: Mono 6.14.1 is installable on ppc64 (main), so a Mono game could run; OpenRA does not (needs .NET 6+, and GL 3.2).

## Top candidates to build next, with what each needs

1. **DDNet**: `apt source ddnet` (20.1.1+ds) in the ppc64 chroot with `DEB_BUILD_OPTIONS=nocheck`; test `gfx_gl_major`/`gfx_backend` fallback to GL 1.1 on nv30; local ddnet-server for an offline run.
2. **OpenTyrian + UQM**: `apt source` both in the chroot, plain build; tyrian21.zip and `uqm-content` as data. Both rely on SDL2 software surfaces, so they are the safest GPU-free runs.
3. **Hedgewars**: `apt source hedgewars`, add `SetExceptionMask(GetExceptionMask + [exInvalidOp, exDenormalized, exZeroDivide, exOverflow, exUnderflow, exPrecision])` to the engine start (hwengine.pas), `nocheck`, then run the 7 lua tests; engine needs `fpc` on ppc64 (the buildd has it).
4. **fheroes2, devilutionX, eduke32** (from part A): contrib or upstream builds, free demo data, no GL needed.
5. **OpenRCT2** staged build per part B (milestone 1 headless under qemu-user, then one supervised PS3 run; optional 1-hour memory-cap probe first).
6. **0 A.D.** only after the cheap ones: patch the two serializer `#error`s, mozjs 128 already builds on ppc64, 3.5 GB data on the PS3 disk, expect ~650 MB RSS and slow JS.
7. **OpenMW**: already installable; needs only Morrowind data from the user and a trial run with `force shaders`, speed on the PPE is doubtful but the GL path fits.

## Cleanup note

Part A's clones live in `build/tmp/newgames/src/` (3.9 GB), part B's in `build/tmp/orct2-be/` (344 MB); both are ephemeral. My podman storage on the overflow disk was removed after the write-up (see the final message).


---

## Re-evaluation, part A (2026-10-19)

Method: shallow clones of each upstream into `build/tmp/newgames/src/<name>` (read the renderer init, CMake and endian code directly), Debian data from the sid Sources.xz (main and contrib) and the debian-ports ppc64 Packages.xz. Debian ppc64 builds only main and contrib arch:all, so every contrib game below has no ppc64 binary and needs our own build. RAM numbers are all "estimate" unless stated: web searches found no measured RSS for any of these games, and I did not run anything. Estimates come from original system requirements, data sizes and engine class.

Git dates are the HEAD of the clone I read (all 2026-08 to 2026-10 unless noted).

### devilutionx (Diablo 1)
1. Hard no: no. Software renderer: 8-bit palette surface rendered by the game itself, `SDL_CreateRenderer` only presents/scales it (`Source/utils/display.cpp`). SDL1.2, SDL2 or SDL3 (CMake `USE_SDL1`, `USE_SDL3`). No GL requirement. https://github.com/diasurgical/devilutionX
2. Debian: absent from sid (no source or binary package). Own build. CMake, bundled third-party libs.
3. Big-endian: good. `Source/utils/endian_{read,write,stream,swap}.hpp` use explicit LE loads/stores (SDL_Swap*LE). Changelog lists "Added support for 32bit PowerPc", "Added support for macOS 10.4 PowerPC" (via `CMake/platforms/macos_tiger.cmake`, SDL1) and "Correct networking support on big-endian systems". Amiga M68K is also a supported port. Estimated BE cost: S (probably zero).
4. RAM: estimate, no measured figure found. It runs on the 3DS (`Packaging/ctr/template.rsf` sets SystemMode 80MB, so the app gets well under 100 MB), Vita, Amiga builds and a DOS build (`docs/building.md`, devx.exe about 5 MB). Expect 60 to 120 MB at 640x480 on a desktop. Far under 512 MB.
5. Data: `spawn.mpq` from the Blizzard shareware (https://github.com/diasurgical/devilutionx-assets/releases/latest/download/spawn.mpq, also http://ftp.blizzard.com/pub/demos/diablosw.exe), shareware portion only (first dungeon levels, warrior/rogue/sorcerer). Size is about 50 MB from memory, not verified here. Redistribution is the project's own asset repo, so treat as tolerated, not an open licence. Full game needs `DIABDAT.MPQ` (GOG).
6. Effort: S. Verdict: TRY NOW. Best 2D candidate on this list: tiny RAM, software path, BE-proven.

### exult (Ultima VII engine)
1. Hard no: no. SDL2 software image buffers (`imagewin/ibuf8`, scalers hq2x/xBR etc. in CPU code). No GL renderer in the tree (grep finds none; only an old `shapes/glshape` reference in the ChangeLog). https://github.com/exult/exult
2. Debian: contrib/games `exult` 1.12.1-3 in sid. No ppc64 binary (contrib). Autotools build, Build-Depends include gtk3, exiv2 and fluidsynth only for the studio/tools; client needs SDL2, zlib, libpng, ogg/vorbis. Cost: S-M to build the client alone.
3. Big-endian: ChangeLog shows a PPC OS X history ("fixed speech output on big endian systems", "fixing crashes on PPC Macs", PPC cross-compile fix). Data formats read through Read2/Read4 style helpers. Estimated BE cost: S.
4. RAM: estimate, no measured figure. Engine runs a 320x200 8-bit surface and loads shapes lazily; U7 itself ran in DOS in a few MB of XMS. Expect 60 to 150 MB plus the sound font if MIDI goes through fluidsynth (a General MIDI sf2 is 30 to 140 MB; use a small one or timidity patches).
5. Data: Ultima VII Black Gate / Serpent Isle are proprietary (GOG "Ultima 7 Complete", about 200 MB). EA never released a free set. No shareware or demo I know of.
6. Effort: S-M. Verdict: WORTH A BUILD, but only if the user owns/buys the data. No free test content, so it cannot go into a free-games suite.

### arx-libertatis (Arx Fatalis)
1. Hard no: no. OpenGL renderer needs GL 1.5, or 1.4 plus ARB_vertex_buffer_object; GLES 1.x also accepted (`src/graphics/opengl/OpenGLRenderer.cpp` lines 190 to 230). Fixed function, VBOs, NPOT optional with a warning. No GLSL. nv30 meets it. https://github.com/arx/ArxLibertatis (HEAD 2024-08-22, last tagged release 1.2.1)
2. Debian: not in sid. Own build; CMake, deps boost, SDL2, glew, openal, freetype, zlib, glm, libepoxy-free.
3. Big-endian: poor. `src/platform/Endian.h` has endian helpers but only 2 source files include it. `io/resource/PakReader.cpp`, FTL models, DLF/LLF levels, FAST/save formats are read as raw packed structs. `PakReader.cpp:480` casts bytes to `u32*` directly. No PPC mentioned in `Architecture.h`'s arch list (x86, ia64, arm, alpha, m68k, mips...). Estimated BE cost: L (audit of every loader and the save format; M if only the shipped content formats matter and saves can break).
4. RAM: estimate. Original game: 128 MB min / 256 MB recommended on Windows XP (vendor spec, not measured here). Arx loads whole levels plus textures; expect 300 to 700 MB. Candidate-level, biggest consumers are textures (texture quality/mipmap cvars) and the level geometry.
5. Data: retail only. Arx Fatalis demo exists (the project's `scripts/arx-install-data` has `install-demo`, names `arx_demo_english.zip` etc., url `https://arx.vg/demo-data`; size not verified, roughly 100 MB). Demo is vendor freeware, not open-licensed.
6. Effort: L. Verdict: NOT WORTH IT for now. Fits the GL path but the BE audit is large and the game is heavy on a 1-core PPE.

### vcmi (Heroes 3 engine)
1. Hard no: no. SDL2 client uses `SDL_CreateRenderer` without forcing accelerated, "SW renderer will also be possible" (`clientsdl2/render/ScreenHandler.cpp:357`); an SDL3 client also exists. No GL path. https://github.com/vcmi/vcmi (1.7.x)
2. Debian: contrib `vcmi` 1.7.3+dfsg-1. No ppc64 binary. Cost: M for the build (boost >= 1.74, SDL2 + image/mixer/ttf, ffmpeg optional via `ENABLE_VIDEO`, Qt launcher optional, minizip, TBB; a very heavy C++ compile that needs cross-building).
3. Big-endian: real support. `lib/filesystem/CBinaryReader` converts LE data on BE hosts (`VCMI_ENDIAN_BIG`), `BinaryDeserializer` has `reverseEndianness`, CMake runs `test_big_endian`. Estimated BE cost: S-M (save compat and some `memcpy` struct reads untested).
4. RAM: estimate. Original H3 min 128 MB. VCMI decompresses the sprite DEFs into 32-bit surfaces and keeps the town/battle atlases, I expect 600 MB to 1.2 GB. Biggest consumers: unpacked animation frames (battle creature DEFs), 32-bit conversion of 8-bit art, music/video if loaded. Over the bar unless those are cut.
5. Data: Heroes of Might and Magic III files (GOG or retail CD, `Data/*.lod`, `*.snd`, `*.vid`). The HoMM3 demo is not a documented supported base; no free set.
6. Effort: L. Verdict: NOT WORTH IT (RAM, build weight, no free data).

### fheroes2 (Heroes 2 engine)
1. Hard no: no. SDL2 + software compositing of an 8-bit palette surface (`src/engine/screen.cpp` converts palette to the window surface format, handling `SDL_BIG_ENDIAN` branches). No GL. https://github.com/ihhub/fheroes2
2. Debian: contrib `fheroes2` 1.1.17+dfsg-1. No ppc64 binary. Build-Depends are tiny (sdl2, sdl2-mixer, libsmacker, zlib): cost S. Upstream also is 1.1.14+ (Feb 2026 Flathub changelog).
3. Big-endian: `engine/serialize` has `getLE16`/`getBE16` explicit helpers; screen.cpp has BE pixel-format branches; the AGG parser is built on that stream class. I found no BE issue in the tree. Estimated BE cost: S.
4. RAM: estimate; the Flathub install is only about 56 MiB on disk. Runs on PS Vita (README_PSV.md) and Switch. Expect 100 to 250 MB RSS (AGG file is read into memory by file, sprites decoded on use). Biggest consumer when it bites: the world-map sprite cache and the 32-bit scaled output at 1920x1080 (cut by running 640x480 or 1x with SDL scaling).
5. Data: Heroes of Might and Magic II files. Free path: the HoMM II demo, supported (README: "free demo version", `docs/INSTALL.md`; the project ships `extract_homm2_resources` scripts). The demo is vendor-released and has limited maps. Full data from GOG.
6. Effort: S. Verdict: TRY NOW. Second best on this list (Debian recipe exists, tiny deps, demo data available).

### openxcom and OpenXcom Extended (OXCE)
1. Hard no: no. SDL 1.2 + SDL_gfx, 8-bit software blitter. OpenGL is optional (`SDL_OPENGL` flag, GLSL shader scalers, `-D__NO_OPENGL` supported); default is the software path. https://github.com/OpenXcom/OpenXcom (HEAD 2026-09), https://github.com/MeridianOXC/OpenXcom (OXCE, HEAD 2026-09; same SDL 1.2 / `OpenGL.cpp` layout, CMake still lists SDL, SDL_gfx, SDL_mixer, SDL_image)
2. Debian: absent in sid (checked main and contrib). Own build, CMake plus yaml-cpp (bundled). Needs SDL 1.2 compat (sdl12-compat on Debian is fine) and SDL_gfx.
3. Big-endian: some code. 7 files use `SDL_Swap*`/`BIG_ENDIAN` (`Surface.cpp`, `CatFile.cpp`, `FlcPlayer.cpp`, `Zoom.cpp`, `SaveConverter.cpp`, `RuleGlobe.cpp`, `MapDataSet.cpp`). A forum user ran 1.0 on a PowerPC G4 Leopard and needed a local fix for FLI/FLC intro movie detection on BE (https://openxcom.org/forum/index.php/topic,8638.0.html); the FLC player has since been replaced. Estimated BE cost: M (findings are from 2020, retest of ruleset file loading and the movie player).
4. RAM: estimate. Original UFO: 4 MB EMS era data. OpenXcom's resident size is modest at vanilla (I expect 120 to 250 MB); OXCE with big mods can go 400 MB to 1 GB. Cut: mods, extra resolution/Zoom scaler (use none), geoscape globe textures.
5. Data: original UFO: Enemy Unknown / X-COM Collection files (Steam/GOG, proprietary). I know of an X-COM demo that people use for testing, but did not verify project support. No free full set.
6. Effort: M. Verdict: WORTH A BUILD only with the user's own UFO data; the vanilla build is the safe one, skip OXCE until vanilla works.

### ufoai (UFO: Alien Invasion)
1. Hard no: no. Quake2-style renderer; GL 1.x fixed function (also builds against GLES1: `GL_VERSION_ES_CM_1_0` branches) with GLSL shaders optional via cvar `r_programs` ("0 - disabled, 1 - low..." in `src/client/renderer/r_main.cpp:1080`), plus checks on ARB_multitexture, ARB texture compression, FBO optional. No GL 3 requirement. nv30 should run it with `r_programs 0` (the GLSL levels need testing). https://github.com/ufoai/ufoai (clone HEAD 2025-03-24) and Debian source `ufoai` 2.5-11.
2. Debian: main. `ufoai` 2.5-11 is already built for ppc64 on debian-ports (installed size 3.6 MB; buildd.debian.org shows ppc64 2.5-11 Installed). Data packages are arch:all in main: `ufoai-data` 306 MB, `-maps` 379 MB, `-textures` 329 MB, `-music` 205 MB, `-sound` 18 MB, `-misc` 12.6 MB installed, about 1.25 GB of disk total. Cost to use: none for the engine; trim music/textures for space (the music alone is 205 MB).
3. Big-endian: Quake-heritage byte-swap macros (`src/shared/byte.h`: `LittleLong` = `SDL_SwapLE32` etc., used in 22 files). Debian builds it on ppc64 (BE) and on s390x/other BE ports. I have no report of runtime correctness on BE, so unverified. Estimated BE cost: S-M (the BSP/MD2/MD3/UFO-script loaders go through those macros; check shader/script binary data and savegames).
4. RAM: estimate; no measured figure found. The game is a full 3D tactical engine with 1.25 GB of data; loading a battle map with textures I would put at 400 to 900 MB, and the geoscape is lighter. Biggest cuts: `r_texturemode`/texture downscale (`r_maxtexres`), disabling shaders/lightmaps/normalmaps, music streaming, single-player low-poly maps.
5. Data: free, GPL (the Debian packages). No licence issue.
6. Effort: S (install the Debian packages). Verdict: TRY NOW. It is a free-data, already-packaged, GL1.x game; measure RAM first, expect candidate-level.

### FreeSpace Open (fs2_open)
1. Hard no: YES. `code/graphics/opengl/gropengl.cpp:53`: "minimum GL / GLES version we can reliably support is 3.2", `MIN_REQUIRED_GL_VERSION = 32`, GLSL 150 required, the context is created at 3.2; there is also a Vulkan renderer (`FSO_BUILD_WITH_VULKAN`). No fixed-function or software fallback. https://github.com/scp-fs2open/fs2open.github.com
2. Debian: absent (only `freespace2-launcher-wxlauncher` in contrib).
3-5. Not evaluated beyond the veto.
6. Verdict: DROP (hard no).

### iortcw (Return to Castle Wolfenstein) and ET:Legacy
1. Hard no: no. iortcw (https://github.com/iortcw/iortcw, clone date 2024-05) has an OpenGL 1 renderer (default) and an optional "rend2" GL2/GLSL renderer. ET:Legacy (https://github.com/etlegacy/etlegacy) CMake: `FEATURE_RENDERER1` (OpenGL 1/2) ON by default, `FEATURE_RENDERER2` (GL 3, per its own label) OFF, a GLES and a Vulkan renderer exist (OFF). So the defaults on both run GL 1.x fixed function.
2. Debian: iortcw is contrib `iortcw` 1.51.c+dfsg1-10 (no ppc64 binary; the `+dfsg` strips non-free bits). Own build needed, trivial Makefile. ET:Legacy is not in Debian (own build, CMake, bundled GLEW and libs; renderer1 build only).
3. Big-endian: ioquake3 lineage, `q_platform.h` has `idppc`/`idppc_altivec` and `Q3_BIG_ENDIAN`; `LittleLong` etc. throughout (renderer BSP loader, botlib, models). PPC OS X builds of ioq3/iortcw were shipped for years. ioquake3 itself is in Debian main and already built for ppc64 (1.36+u20260719). Estimated BE cost: S for iortcw SP/MP; S-M for ET:Legacy (more new code, but same `LittleLong` discipline).
4. RAM: estimate. RTCW's original requirement was 64 to 128 MB; the engine loads whole pk3 maps into hunk memory (`com_hunkMegs` default 128 for iortcw, can be tuned). Expect 150 to 350 MB with low textures (`r_picmip 2`, `r_texturebits 16`). Under 512 MB.
5. Data: RTCW retail pak0.pk3 etc (proprietary). Single-player demo existed (Activision, freely distributed ~2001; not verified that iortcw accepts it). Enemy Territory (freeware by Splash Damage/Activision, not open): pak0.pk3, pak1, pak2 from the ET 2.60b download (about 270 MB), ET:Legacy installs from that and includes its own "legacy" mod. ET gives a free, legal multiplayer/bots data set.
6. Effort: S (iortcw), S-M (ET:Legacy). Verdict: iortcw WORTH A BUILD (needs user's RTCW data). ET:Legacy TRY NOW if the freeware data is acceptable: free, GL1, but 1080p multiplayer maps and 1 core need a measured frame rate. Neither is a perf win on 1 PPE at 60 fps before testing.

### dhewm3 (Doom 3 engine)
1. Hard no: not strictly, but risky. It has only the ARB2 path (`ARB_vertex_program` + `ARB_fragment_program`, `draw_arb2.cpp`, `R_ARB2_Init`), the other fallbacks (NV10/NV20/R200) were removed in dhewm3. OpenGL 2.0 era, no GLSL needed (GLSL optional path exists since 1.5.x for some effects, unverified). Whether Mesa nv30 exposes `GL_ARB_fragment_program` with enough instruction slots for the interaction shaders is untested, and nv30 has strict ALU limits. https://github.com/dhewm/dhewm3
2. Debian: contrib `dhewm3` 1.5.5+dfsg-2. No ppc64 binary; own build (CMake, SDL2, OpenAL, libjpeg, libvorbis, curl).
3. Big-endian: upstream treats it as supported: `Lib.cpp` defines `D3_IS_BIG_ENDIAN` from CMake and cross-checks SDL, changelog "Fix bugs on 64bit Big Endian platforms (#472, #625)". PPC Mac builds historically. Estimated BE cost: S.
4. RAM: estimate; original Doom 3 minimum 384 MB RAM, 512 MB recommended. dhewm3 at Low quality (`image_downSize`, `image_useCache 0`, smaller models) I estimate 400 to 700 MB. Over the RAM bar: textures, the generated shadow volumes, sounds (OpenAL uncompressed), and megatexture-style image cache. Plus a very heavy per-pixel lighting load for a 7600-class RSX at 1080p. No path to 60 fps.
5. Data: retail only, `base/pak000-008.pk4` patched to 1.3.1 (README). The 2004 demo is no longer officially distributed. No free set.
6. Effort: M. Verdict: NOT WORTH IT (RAM and ARB2 risk, no free data).

### rbdoom3bfg (RBDOOM-3-BFG)
1. Hard no: YES. NVRHI abstraction for DX12 and Vulkan (README line 53, `r_graphicsAPI`), the OpenGL path is 4.x-era. `README.md` also asks for the Vulkan SDK. https://github.com/RobertBeckebans/RBDOOM-3-BFG
2. Debian: main `rbdoom3bfg` 1.4.0+dfsg-9, and a ppc64 build exists (1.4.0+dfsg-9+b1), but it still needs GL 4.5/Vulkan hardware.
3-5. Not evaluated beyond the veto. (BFG data is retail only.)
6. Verdict: DROP (hard no).

### openmohaa (Medal of Honor: Allied Assault)
1. Hard no: no. CMake `BUILD_RENDERER_GL1` ON (default), `BUILD_RENDERER_GL2` OFF. GL1 renderer: fixed function, ioq3 lineage. https://github.com/openmoh/openmohaa (clone date 2026-04-24)
2. Debian: contrib `openmohaa` 0.82.1+dfsg-4. No ppc64 binary. Own build. CMake, SDL2, OpenAL, openal-soft; bigger C++ code base (script VM, TIKI, skeletor).
3. Big-endian: the changelog says "Improved support for big-endian architectures (like PowerPC 32/64 bits...)" and "This new version ships with binaries for PowerPC, PowerPC64 and PowerPC64 Little-Endian. Currently experimental and the client may not work correctly." `LittleLong` used in renderergl1 (`tr_bsp`, `tr_model`, `tr_terrain`, `tr_marks_permanent`), `skeletor_loadanimation.cpp`, `tiki_main.cpp`. The maintainer fixed an issue from a Talos II tester (https://github.com/openmoh/openmohaa/issues/618, discussed at https://forums.raptorcs.com/index.php/topic,550.25.html), but nobody has reported a working Linux ppc64 BE client; the only well-tested PPC port is MorphOS. Estimated BE cost: M (TIKI/skeletal animation are the risk).
4. RAM: estimate. Original min 128 MB RAM (2002). Expect 300 to 600 MB at low textures; the TIKI model set and the large outdoor maps are the consumers.
5. Data: retail MOHAA / Spearhead / Breakthrough pak files (proprietary). A demo exists historically (EA, about 100 MB); I did not verify that openmohaa supports it.
6. Effort: M. Verdict: NOT WORTH IT now (heavy, BE experimental, no free data).

### eduke32
1. Hard no: no. Classic Build software renderer (8-bit palette, default) is always there; Polymost (OpenGL, `USE_OPENGL`) and Polymer (`POLYMER`, GLSL/ARB shaders) are build/runtime options. `Common.mak` lets you turn GL off (`USE_OPENGL := 0`). https://voidpoint.io/terminx/eduke32 (HEAD 2026-08-07; I cloned the GitLab master, the GitHub mirror 404s)
2. Debian: absent in sid. Own build: GNU make, SDL2 (+ SDL2_mixer), libvpx optional, flac/vorbis optional. Straight `make`.
3. Big-endian: Build engine has `B_BIG_ENDIAN`/`B_SWAP*` in `compat.h`, 18 occurrences in `source/build/src` and 8 files in game code; `Common.mak` carries a DEVKITPPC (Wii/GameCube) platform. BE support is routine for the Build engine. Estimated BE cost: S.
4. RAM: estimate. Duke3D ran in 4 MB. EDuke32 with the classic renderer and 8-bit art: 40 to 100 MB. With Polymost and hightile packs it grows, so stay classic (or Polymost without hightile).
5. Data: Duke Nukem 3D shareware (`DUKE3D.GRP` v1.3d, episode 1 "L.A. Meltdown", freely distributable by 3D Realms, about 11 MB). Retail Atomic Edition from GOG. Also freely usable total conversions that ship a GRP or ZIP.
6. Effort: S. Verdict: TRY NOW. The best FPS candidate: software rendering, tiny RAM, BE-proven, free shareware data.

### rednukem, NBlood, Raze
1. Hard no:
   - rednukem (Redneck Rampage, also Redneck Rampage Rides Again): built from the NBlood tree (`GNUmakefile: rr_game := rednukem`, `source/rr`). Same Build engine software renderer, GL optional. No hard no. https://github.com/nukeykt/NBlood (HEAD 2026-09-14)
   - NBlood (Blood): same, classic software renderer default. https://github.com/NBlood/NBlood is a stale mirror with older commits; use nukeykt/NBlood.
   - Raze (Build engine on GZDoom's renderer): GL backend requires 3.3 (`gl_interface.cpp:141`: "At least OpenGL 3.3 is required"). There is a `gles` backend that picks `GLES_MODE_OGL2` below 3.3 with shader version "100", so it has a GLES2-class path, not a hard no. Raze also has a software mode. But Raze's GL/GLES backend wants FBOs, npot, mapped buffers and GLSL ES 1.00; on nv30 (no FBO through the ARB name, GL 2.1, GLSL 1.20) this is unproven. https://github.com/ZDoom/Raze (clone HEAD 2025-11-19)
2. Debian: all absent in sid. Own build. Raze needs cmake, zmusic, many bundled libs (heavy).
3. Big-endian: NBlood/rednukem: `B_BIG_ENDIAN` appears in 21 places in Blood alone (`db.cpp`, `resource.cpp`, `tile.cpp`...), plus the shared Build engine. Estimated BE cost: S-M (Blood's own file formats are less tested on BE than Duke). Raze: has `m_swap.h` for LittleLong with a big-endian branch (inherited from GZDoom, which ran on BE Macs historically); GZDoom-derived code has regressed on BE before. Estimated BE cost: M-L.
4. RAM: estimate. rednukem and NBlood classic: 60 to 150 MB (Blood uses the more intensive tile cache; stay below 200 MB). Raze: 300 to 600 MB, GZDoom-style renderer overhead; I have no measured number.
5. Data: Blood and Redneck Rampage are commercial (no free demo set I know of; Blood's "Blood: One Unit Whole Blood" is retail). Raze plays Duke, Blood, Shadow Warrior, Redneck, Exhumed, Witchaven, TekWar: shareware Duke and Shadow Warrior shareware (Wang, 3D Realms, freely distributable, `SW.GRP` shareware ~ 23 MB) work. 
6. Effort: S (rednukem, NBlood), M (Raze). Verdict: rednukem, NBlood WORTH A BUILD only if the user supplies data. Raze NOT WORTH IT (EDuke32 covers Duke with a better renderer for this GPU).

### barony
1. Hard no: YES. `src/init.cpp:1563`: `SDL_GL_CONTEXT_MAJOR_VERSION 3`, minor 2, `PROFILE_CORE`; GLSL `#version 150 core` in `src/shader.cpp:81`; glew. No other renderer. https://github.com/TurningWheel/Barony
2. Debian: absent.
3-5. The game also needs the purchased game data (README: "you still need a purchased copy of Barony").
6. Verdict: DROP (hard no).

### doukutsu-rs (Cave Story)
1. Hard no: no. Rust, `backend-sdl` has candidates in order GL (desktop core), GLES, then `SDL2Renderer` (SDL_Renderer fallback, `backend_sdl2.rs:715-721`); `CAVESTORY_NO_OPENGL=1` forces the SDL2 renderer. Game is 320x240 sprite art. The OpenGL path uses GLES3 / GL 3 shaders, so it must run on the SDL2 renderer here. https://github.com/doukutsu-rs/doukutsu-rs (HEAD 2026-10-13, `rust-version = "1.87"`)
2. Debian: absent. Own build with cargo. The default feature set pulls `discord-rpc`, `webbrowser`, a `glow` crate and so on; for ppc64 BE, build with `--no-default-features --features backend-sdl,ogg-playback,exe` and system SDL2 (or the bundled one). Cost: M for crate fetch/vendor and any crates with LE-only assumptions.
3. Big-endian: Rust, `byteorder` for data files (explicit LE reads). Unsafe spots: `sound/mod.rs:522` uses `i16::from_ne_bytes` on audio data (check the producer's byte order), `sprite_batch.rs`/`opengl_impl.rs` upload native vertex data (fine). Cave Story data uses LE structs read through `byteorder`. Estimated BE cost: S-M. Rust has `powerpc64-unknown-linux-gnu` as a supported target, and Debian sid ships rustc on ppc64.
4. RAM: estimate. Pixel art, small data (about 5 to 10 MB loaded), so 40 to 100 MB. Ok.
5. Data: Cave Story freeware (Studio Pixel, 2004 `Doukutsu.exe` distribution, about 5 MB). Freeware, not open licence; the project's wiki tells users to supply it.
6. Effort: M. Verdict: WORTH A BUILD. Good tiny 60 fps target if the SDL renderer path holds; costs are the Rust toolchain/crates.

### VVVVVV (open-source edition)
1. Hard no: no. SDL renderer (`desktop_version/src/Screen.cpp:70`), software-capable, and it uses the SDL3 API in the current tree (`SDL_CreateSurfaceFrom`, `physfssdl3.c`, FAudio SDL3 platform). Older tags used SDL2. https://github.com/TerryCavanagh/VVVVVV
2. Debian: absent (not even non-free). Own build, CMake, bundles FAudio, physfs, lodepng, tinyxml2. Needs SDL3 (check sid `libsdl3-dev` on ppc64) or an older SDL2 tag.
3. Big-endian: explicit handling in `GraphicsResources.cpp` (`#if SDL_BYTEORDER == SDL_BIG_ENDIAN` selects `SDL_PIXELFORMAT_RGBA8888`). LodePNG and TinyXML2 are endian-neutral. Not known to have been run on BE Linux. Estimated BE cost: S.
4. RAM: estimate. Tiny 2D game, tilesheets decoded to a few MB. 50 to 100 MB.
5. Data: `data.zip` from your copy of the game, or the free Make and Play Edition (https://thelettervsixtim.es/makeandplay/), per `desktop_version/README.md`. Legal: the engine's licence is non-commercial source-available (not DFSG), so redistribute no binary; building for private use is allowed.
6. Effort: S. Verdict: TRY NOW. Also needs the free M&P data. Not packageable in a public repository.

### hypersomnia
1. Hard no: YES. `src/` sets GL 3.2 core (GLSL `#version 150`), or GLES 3.0 (`#version 300 es`), or a desktop fallback `#version 130`; no software path; renderer needs shader programs and FBOs. https://github.com/TeamHypersomnia/Hypersomnia (HEAD 2026-10-18)
2. Debian: absent.
3-5. Not evaluated beyond the veto.
6. Verdict: DROP (hard no, GL 3.2 core/GLES3 only).

### veloren
1. Hard no: YES (expected). wgpu renderer: Vulkan, Metal, DX11/12. A FreeBSD packaging review (https://reviews-dev.freebsd.org/R11:609b3b168f392f5a36779fe4a33ce9d8ae4f5f46) reports that forcing OpenGL through wgpu still panics on missing features (depth clamping, clamp-to-border). Linux Links lists a 4 GB RAM minimum (https://www.linuxlinks.com/veloren-multiplayer-voxel-rpg). I did not clone veloren (gitlab.com/veloren/veloren).
2. Debian: absent.
6. Verdict: DROP (wgpu, 4 GB).

### The Dark Mod
1. Hard no: YES (expected). 2.08 moved all GLSL to 3.3 core ("all of TDM's GLSL is OpenGL 3.3 core based", https://dsogaming.com/?p=141701; GamingOnLinux 2019 note: 2.08 "will require at least OpenGL 3.3", https://www.gamingonlinux.com/2019/02/the-dark-mod-the-free-standalone-inspired-by-thief-has-a-new-release-out/). Linux Links minimums: 2 GB RAM, 30 GB disk (https://linuxlinks.com/the-dark-mod-stealth-gaming). Did not check 2.12+, the requirement can only have grown.
2. Debian: absent (only `darkradiant-plugins-darkmod`, the map editor plugin).
6. Verdict: DROP.

## Summary table

| game | hard-no? | ppc64 pkg | BE cost | RAM (all estimates) | data | effort | verdict |
|---|---|---|---|---|---|---|---|
| devilutionx | no (software 8-bit) | none in Debian; own build | S (official PPC Tiger/Amiga support) | 60-120 MB | spawn.mpq shareware (about 50 MB, shareware) | S | TRY NOW |
| exult | no (SDL2 software) | contrib 1.12.1-3, no ppc64 bin; own build | S | 60-150 MB (+soundfont) | Ultima VII proprietary, no free set | S-M | worth a build (own data) |
| arx-libertatis | no (GL 1.5 fixed function) | none in Debian; own build | L (raw struct loaders) | 300-700 MB | retail; vendor demo exists | L | not worth it |
| vcmi | no (SDL2 renderer, SW ok) | contrib 1.7.3, no ppc64 bin | S-M (BE reader exists) | 600 MB-1.2 GB | H3 retail only | L | not worth it |
| fheroes2 | no (SDL2 software) | contrib 1.1.17, no ppc64 bin; trivial build | S | 100-250 MB | HoMM2 demo supported (free); full from GOG | S | TRY NOW |
| openxcom / OXCE | no (SDL1.2 software, GL optional) | none in Debian; own build | M | 120-250 MB vanilla, OXCE+mods 400 MB-1 GB | UFO:EU retail only | M | worth a build (own data), vanilla first |
| ufoai | no (GL1.x + optional GLSL via r_programs) | main 2.5-11, ppc64 binary exists; data arch:all in main (about 1.25 GB) | S-M (LittleLong macros) | 400-900 MB | free GPL data, in Debian | S | TRY NOW |
| fs2_open | YES (GL 3.2 / GLSL 150 / Vulkan) | none | n/a | n/a | n/a | n/a | DROP |
| iortcw | no (GL1 default, rend2 optional) | contrib 1.51c, no ppc64 bin | S | 150-350 MB | RTCW retail; SP demo existed (unverified) | S | worth a build (own data) |
| ET:Legacy | no (renderer1 GL1/2 default) | none in Debian; own build | S-M | 150-350 MB | ET freeware pak0-2 (about 270 MB) | S-M | TRY NOW (measure fps) |
| dhewm3 | no, but ARB2-only (ARB programs on nv30 unproven) | contrib 1.5.5, no ppc64 bin | S (D3_IS_BIG_ENDIAN, BE fixes) | 400-700 MB | retail pk4 only | M | not worth it |
| rbdoom3bfg | YES (NVRHI DX12/Vulkan, GL 4.x) | main + ppc64 bin exist, useless | n/a | n/a | n/a | n/a | DROP |
| openmohaa | no (GL1 default) | contrib 0.82.1, no ppc64 bin | M (BE "experimental") | 300-600 MB | retail only | M | not worth it |
| eduke32 | no (classic software default) | none in Debian; own build | S (B_BIG_ENDIAN, Wii port) | 40-100 MB classic | Duke3D shareware (about 11 MB, free) | S | TRY NOW |
| rednukem | no (Build software) | none; own build | S-M | 60-150 MB | Redneck retail only | S | worth a build (own data) |
| NBlood | no (Build software) | none; own build | S-M | 80-200 MB | Blood retail only | S | worth a build (own data) |
| Raze | no strictly (GL 3.3, or gles OGL2 mode, unproven) | none; own build | M-L | 300-600 MB | Duke/SW shareware ok | M | not worth it |
| barony | YES (GL 3.2 core, GLSL 150) | none | n/a | n/a | purchased copy needed | n/a | DROP |
| doukutsu-rs | no (SDL2 renderer fallback) | none; cargo build | S-M | 40-100 MB | Cave Story freeware (about 5 MB) | M | worth a build |
| VVVVVV | no (SDL renderer, SW ok) | none; own build | S | 50-100 MB | Make and Play Edition, free | S | TRY NOW (private build, licence not redistributable) |
| hypersomnia | YES (GL 3.2 core / GLES3) | none | n/a | n/a | n/a | n/a | DROP |
| veloren | YES (wgpu, Vulkan) | none | n/a | 4 GB listed | n/a | n/a | DROP |
| The Dark Mod | YES (GL 3.3 core) | none | n/a | 2 GB listed | n/a | n/a | DROP |

## Addendum: GameOS port exists? (requested later)

Not researched with sources: this is from memory of the PSL1GHT/homebrew scene and the platform lists in each README (devilutionX's README badges list PS4, Xbox, Switch, Vita, 3DS, Amiga, but no PS3). Treat every "no" as "I know of none", and verify before relying on it. Novel-on-PS3 means no known GameOS port, which ranks higher at equal cost.

| game | GameOS port? |
|---|---|
| devilutionx | no known (README platform list has no PS3) |
| exult | no known |
| arx-libertatis | no |
| vcmi | no |
| fheroes2 | no known (Vita/Switch only) |
| openxcom / OXCE | no known |
| ufoai | no |
| fs2_open | no |
| iortcw / ET:Legacy | no known for these; Quake 3 engine ports on PSL1GHT may exist (partial, other engine) |
| dhewm3 | no |
| rbdoom3bfg | no |
| openmohaa | no |
| eduke32 / rednukem / NBlood / Raze | partial: EDuke32 has Wii/PSP ports, no known PS3 one |
| barony | no |
| doukutsu-rs | no known (NXEngine-style Cave Story ports exist on other consoles, possibly PS3: unverified) |
| VVVVVV | no known (Vita/3DS/Switch) |
| hypersomnia, veloren, The Dark Mod | no |

Ranking effect: devilutionX, fheroes2, ufoai, eduke32, VVVVVV are all novel on PS3 at "try now" cost.


---

## OpenRCT2 on the PS3: big-endian port estimate (re-evaluation, part B)

Moved to the game's bundle: [gamedb/games/openrct2/investigations/2026-10-19-big-endian-port-estimate.md](../games/openrct2/investigations/2026-10-19-big-endian-port-estimate.md) (370 lines: packed structs, streams, bit-fields, pixels, third-party parts, network, the gaps in the AmigaOS PR #27114, tests, RAM, speed, other ports, effort, risks). Its verdict: **worth a build, staged, with RAM as the go/no-go.** Upstream refuses big-endian on purpose (a static_assert since PR #23684), but the closed AmigaOS PR #27114 did most of the byte-order work (about 1,700 endian lines in 33 files, applies to v0.5.6 with 2 conflicts), which leaves 7-11 person-days to a measured PS3 run; the GUI measured 282-300 MB RSS on amd64 with no GL, about 210-230 MB after trims, on a 217.5 MiB machine; the renderer must be the software one (the OpenGL engine needs GL 3.3 core).
