> Superseded on 2026-10-19 by docs/games/SUITE.md and docs/research/GAME-CANDIDATES.md; kept unchanged for its history.

# Game test suite: candidates, picks and a benchmark runner

2026-10-16. Research only, nothing here was run on the PS3. (2026-10-17: every pick has run since;
results and status are in GAME-PLAN.md, which supersedes this file where they differ.) Why: step 0 (SPU-OFFLOAD-SURVEY.md)
rested on OpenArena alone, which misled us about VRAM and sound; SPU offload (DXT, audio) and memory
decisions need 5-10 titles. Sources: the debian-ports snapshot we pin (`20261013T000000Z`, ppc64 and
all; contrib too), live debian-ports, buildd.debian.org logs, the Debian BTS, upstream issue trackers
and the games' source at the packaged versions. **UNVERIFIED** marks anything not confirmed in a
source or log. Sizes are installed sizes of the packages the image doesn't have yet (dependency
closure against `build/rootfs/base.tar`'s dpkg status).

**Corrections (2026-10-16, reviews against source):** GAME-PLAN.md has the checked expectations
and supersedes this file where they differ. Wrong here: dsda-doom's GL mode needs GL_RG textures
(absent on nv30) and its version is 0.30.0; OpenTTD's GL mode can't start (ARB_texture_rg, a VAO
check bug, texture2DLod); X-Moto ships a replay (`credits`); Yamagi's source package is `yquake2`
and its demo `q2demo1.dm2`; Luanti can keep LuaJIT (it exists for ppc64; Debian strips the bundled
Lua); Mesa's shader cache is on for nv30; 3D textures sample fine; the 1-pixel render target fix is
Mesa patch 0007; hardware mipmap generation is a new risk (Neverball, SuperTuxKart, Luanti).

## The yardstick: what Mesa nv30 on NV47 offers

From `nv30_screen.c`/`nv30_format.c` (Mesa 26.2.4) and docs/RSX.md:
- GL 2.1 compatibility, GLSL 1.20, GLES 2.0. No GL 3.x context of any profile: engines that ask for
  3.2 core with no fallback fail at startup.
- Fragment programs: 4096 instructions, 224 vec4 constants, 32 temps, 8 varyings, 4 render targets.
  Vertex programs: 512 instructions, ~462 vec4 constants. No real loops (NIR unrolls).
- **No vertex texture fetch** (0 vertex samplers; `nv40_verttex_validate` is a stub, PLAN.md).
- **No float textures**: 16/32-bit float formats are vertex formats only (no ARB_texture_float, no
  float FBOs). sRGB sampling yes, sRGB render targets no.
- NPOT, EXT_framebuffer_object, depth textures and ARB_shadow, occlusion queries, MSAA 2/4,
  S3TC. **ARB_framebuffer_object is not exposed** (`mixed_framebuffer_sizes` is false, the cap Mesa
  ties it to): engines that look only for the ARB name (or GL 3.0) treat FBOs as absent.
- No texture arrays, instancing, geometry shaders, integer textures, OES_standard_derivatives.
  TIMESTAMP/TIME_ELAPSED queries return a pixel count, not time (MESA-METHODS.md): GPU timings from
  any game's profiler are garbage here.
- Known RSX traps (RSX.md): a graph exception wedges the whole RSX and only a console reboot clears
  it; vertex format type 0 and swizzled 1-pixel-wide render targets fault (the validator and
  Mesa 0012 now refuse them); single-attribute inline draws need Mesa 0008.
- Texture compression at upload: generic `GL_COMPRESSED_RGB` becomes DXT1 and `GL_COMPRESSED_RGBA`
  DXT5 (`st_format.c`). **libspeq's encoder does DXT1 only**; DXT3/5 return -ENOTSUP and fall back to
  Mesa's PPE encoder at 2.6 Mpixel/s. Games that compress alpha textures need DXT5 in libspeq.

## Surprises

- **Four Debian games don't build on ppc64 right now** (latest buildd logs):
  - wesnoth-1.18: links fail on `boost::context` (Debian #1146645, Boost doesn't set
    BOOST_USE_UCONTEXT on ppc64). Not a Wesnoth bug; it built on powerpc and s390x.
  - hedgewars 1.0.3: builds, then 2 of 7 engine Lua tests fail (`EInvalidOp` in `stuckcake.lua`,
    wrong positions in `gravity_get_set.lua`). Looks like FPC floating point on PPC, not byte order
    (UNVERIFIED).
  - ddnet 20.1.1: the test run hangs in `Net.Ipv4AndIpv6Work` and the buildd kills it.
  - warzone2100 4.7.0: **a real big-endian bug**, a static assert in the bundled GameNetworkingSockets
    (`minbase_endian.h:26`, `LittleWord()` on a uint64). Moot here anyway: it needs GL 3.0.
- **Luanti has no ppc64 binary**: debian/control lists only LuaJIT architectures. Big-endian itself
  works: s390x builds 5.15.2 and passes `--run-unittests`. A 2025 ppc64 attempt (5.10.0, ports
  LuaJIT) aborted in TestSAO. Upstream builds without LuaJIT (`-DENABLE_LUAJIT=0`, bundled Lua 5.1).
- **contrib is arch:all only on debian-ports**: nothing arch-specific there is built, so
  `yamagi-quake2` (contrib) doesn't exist for ppc64 although it builds on s390x and ppc64el.
- **Debian's darkplaces is GL 3.2 core only** (since 0~20180908), so Nexuiz (in Debian) and Xonotic
  are out with Debian's engine; Nexuiz runs on our older build (`darkplaces-gl2`, docs/GAME-PLAN.md).
- **BananaBread (Debian's cube2) and Red Eclipse turn texture compression off on Mesa**: `if(!mesa) usetexcompress = 2`
  in `rendergl.cpp`; only pre-made DDS files go in compressed.
- **QuakeSpasm 0.96.3 has a BE bug**: wrong colours and dlight wraparound from the packed-pixels path
  (quakespasm issue #114, a G5 on Debian ppc64); `-nopackedpixels` avoids it, upstream master
  disables the path on BE.
- **Freedoom 0.13's built-in demos desync** (freedoom issue #1532): record our own.
- Hardly any game runs tests on the buildds (most `dh_auto_test` steps do nothing), so "builds on
  ppc64" is the only BE evidence for most of them. Exceptions: OpenTTD (97/97 ctest pass on ppc64),
  Frozen Bubble (tests pass on ppc64), Luanti (s390x).
- The OpenArena GLSL renderer (`cl_renderer opengl2`) was tried once: commit 82b6b5f (2026-10-15)
  went back to opengl1 because "the GLSL one ran out of RAM compiling its shaders". No log was kept.
  That was before the VRAM swap and game mode, so it deserves another try (below).

## Candidates

Tier = driver readiness: **A** likely runs as is, **B** likely needs driver work, **C** unknown until
tried, **X** can't run on NV47. MB = installed (engine + data + new libraries).

| Game (engine) | ppc64 package, MB | BE status | GL need, tier | RAM | Benchmark mode | Exercises | Verdict |
|---|---|---|---|---|---|---|---|
| OpenArena, `opengl1` (ioquake3 1.36) | openarena 0.8.8 (in image, 422) | runs (ours) | GL 1.1 fixed function, multitexture; A | 74 MB resident mid-game (measured) | timedemo, prints fps; load from `CL_InitCGame` | S3TC `r_ext_compressed_textures`; OpenAL Soft; WAV + Ogg; CPU/driver bound | **suite** (baseline) |
| OpenArena, `opengl2` (renderergl2) | same | same | GLSL 1.20, ~100-115 programs at startup; FBO paths need ARB_fbo; B | ran out of RAM compiling shaders (82b6b5f) | same timedemo | GLSL compile burst, normal/specular maps, S3TC + DDS | **suite** (driver work) |
| ClassiCube (ours) | classicube 1.3.8 (in image) | runs | its default GL backend (which one UNVERIFIED); 60 fps at 1080p | UNVERIFIED | none (HUD fps) | OpenAL, Ogg (Mojang audio); voxel, meshing | **suite** (done) |
| QuakeSpasm 0.96.3 | quakespasm, 2 + data | PPC supported; packed-pixels BE bug, `-nopackedpixels` | GL 1.x + tiny GLSL (gamma, alias models); A | small; `-heapsize` | `+timedemo demo1`, prints fps, doesn't quit | SDL audio, own mixer; Ogg/Opus/MP3/FLAC music; no S3TC | **suite** |
| Yamagi Quake II 8.70, `gl1` | not on ppc64 (contrib), build ours | upstream tests BE incl. PPC64 | GL 1.4 fixed function; A (gl3/gles3: X) | small | `+timedemo 1 +nextdemo quit +demomap demo1.dm2` | OpenAL; WAV + Ogg; no S3TC | reserve (needs our build) |
| darkplaces / Nexuiz 2.5.2 | darkplaces + nexuiz, 771 | builds | **GL 3.2 core only** in Debian; X | heavy | `-benchmark demo` (best of all) | S3TC + DDS cache `r_texture_dds_save` | out with Debian's engine; our `darkplaces-gl2` (2018-04 snapshot, GL 1.x/2.0) builds, not run on the PS3 yet: docs/GAME-PLAN.md, "Nexuiz Classic" |
| Xonotic | not in Debian, ~1 GB data | | darkplaces GL 3.2 | heavy | the-big-benchmark.sh | as above | out |
| ezquake 3.6.8 | ezquake, 6 | BE fixes upstream | modern GL 4.x: X; classic GL path: C | small | classic timedemo, UNVERIFIED | UNVERIFIED | out (Quake data, QuakeSpasm covers it) |
| Alien Arena | contrib + non-free data | | GLSL, UNVERIFIED | | | | out |
| Warsow / Warfork | not in Debian | | UNVERIFIED (likely GL 3) | | | | out |
| dsda-doom 0.30.1 + freedoom 0.13 | dsda-doom, freedoom, 84 | builds, endian-aware swaps | GL fixed function + VBO, needs NPOT + multitexture, EXT_fbo optional, small GLSL; A. Also `-vidmode sw` | small | `-timedemo x.lmp`, prints fps, exits | SDL_mixer + fluidsynth MIDI; WAD; texture/2.5D path | **suite** |
| Chocolate/Crispy Doom | chocolate-doom 3.1.1 / crispy-doom 7.2, 65 | builds | software, SDL2 renderer blit; A | tiny | `-timedemo`, prints fps, exits | SDL_mixer | smoke test only (safest first run) |
| Woof 16 | woof-doom, 82 | builds | software, SDL3 blit; A | tiny | `-timedemo` (ends in a message box, UNVERIFIED) | OpenAL | no (dsda covers it) |
| Doomsday 2.3.1 | doomsday, 203 | builds | shaders, Qt5, UNVERIFIED; C | heavy | UNVERIFIED | | no |
| Neverball 1.6 (+Neverputt) | neverball(+neverputt), 112 (138) | builds, tests disabled | GL fixed function, VBO, point params required; GLSL/FBO optional; A | small | `--replay demo1.nbr` (shipped); fps on screen only, stops at a menu | SDL audio + Ogg; no S3TC | **suite** (patch or HUD) |
| Extreme Tux Racer 0.8.4 | extremetuxracer, 48 | builds | GL 1.2 fixed function (SFML 3.1); A | small | none | SFML audio (WAV/Ogg) | reserve (no benchmark) |
| Armagetron 0.2.9.3 | armagetronad, 27 | builds | GL 1.x, SDL 1.2; A | small | `--playback` of debug recordings, UNVERIFIED | SDL_mixer | reserve (soak test) |
| BananaBread (cube2 2020) | cube2 + cube2-data 1.3, 266 | builds, no tests | GL 2.0 + GLSL 1.20 + FBO (EXT ok) required; float/VTF optional; C | UNVERIFIED; `maxtexsize`, `texreduce` | scripted: `-x` CubeScript `getfps` + `quit` | SDL_mixer, Ogg/WAV; S3TC off on Mesa (DDS only); GPU step up | **suite** |
| Tesseract | not in Debian | | requires 4 vertex texture units, float G-buffer; X | | | | out |
| Red Eclipse 1.6 | redeclipse, 945 | builds | same engine as BananaBread; C | heavy maps | scripted like cube2 | as cube2; no gamepad | reserve (BananaBread covers it, 936 MB data) |
| SuperTuxKart 1.5 (legacy GL 2.1) | supertuxkart, 839 | builds; old #714519 wrong colours on powerpc (0.8 era) | GLSL path needs 3.1, legacy fixed-function fallback; C | **>= 1 GB (FAQ)** | `--benchmark`: replay, CSV report, exits | OpenAL + Ogg; `enable_texture_compression` (likely no effect on legacy, UNVERIFIED) | **suite** (stretch: RAM) |
| Trigger Rally 0.6.6 | trigger-rally, 122 | builds | GL 2.0 compat, fixed function, cube maps; A | small | none (no CLI, no fps counter) | OpenAL + ALUT | reserve |
| Scorched3D 44 | scorched3d, 125 | builds | fixed function, optional GLSL/FBO/shadows; A | UNVERIFIED | none (`-startclient` AI game, UNVERIFIED) | OpenAL + Ogg, wxWidgets | no |
| TORCS 1.3.7 | torcs, 529 | builds | plib, fixed function; A | UNVERIFIED | `-r race.xml` runs headless (no fps) | **driver-side compression** `GL_COMPRESSED_*_ARB` (DXT1/DXT5 via Mesa); OpenAL | reserve (S3TC test, no benchmark) |
| BZFlag 2.4.30 | bzflag-client, 18 | builds | fixed function, GLEW; A | small | none | own mixer on SDL | no |
| X-Moto 0.6.3 | xmoto, 84 | builds | immediate mode, optional VBO/FBO/shaders; A | small | `--replay F --timedemo --benchmark`, prints fps | SDL_mixer, Ogg/WAV; 2D | **suite** |
| Luanti 5.15.2 + minetest game | data only (27), engine ours | s390x passes unit tests | `video_driver=opengl`: GLSL 1.20 mandatory, FBO for effects; C | default view likely > 200 MB, UNVERIFIED | none for rendering; needs a mod | OpenAL + Ogg; CPU: Lua, meshing | **suite** (stretch, our build) |
| SuperTux 0.6.3 | supertux, 292 | builds (old BE FTBFS fixed) | `--renderer opengl20`: fixed function + FBO; A | UNVERIFIED | `--play-demo`, no fps output | OpenAL + Ogg | reserve |
| Teeworlds 0.7.5 | teeworlds, 11 | old BE crash fixed 2008 | GL 1.x, **3D textures** for tilemaps; A/C | small | `"play demos/x.demo"`, on-screen fps, returns to menu | SDL audio, WavPack; `gfx_texture_compression 1` → DXT5 | reserve (DXT5 case) |
| DDNet | uninstallable on ppc64 (test hang) | upstream BE fixes | legacy GL 1.1 by default | | | | no |
| OpenTTD 15.1 | openttd + opengfx/opensfx/openmsx, 71 | **97/97 tests pass on ppc64** | `-v sdl-opengl`: GL 2.0 shaders, R8/1D textures, PBO, VAO check: B; `-v sdl` software: A | UNVERIFIED; map size | `-v null:ticks=N` simulation, timed | SDL audio + own mixer, WAV; MIDI via fluidsynth; CPU | **suite** |
| Chromium B.S.U. | chromium-bsu, 6 | builds | GL 1.x immediate mode; A | tiny | none (`--debug` prints startup fps) | OpenAL + ALUT | smoke test only |
| Hedgewars 1.0.3 | not built (2 engine tests fail) | FP issue, UNVERIFIED | GL 1.x path in Debian; A | UNVERIFIED | `hwengine --stats-only demo.hwd` (headless) | SDL_mixer | no (needs a package fix) |
| Frozen Bubble 2.212 | frozen-bubble, 53 | tests pass on ppc64 | SDL 1.2 surfaces, no GL; A | small | none | SDL_mixer, Ogg/WAV; Perl | smoke test only |
| Pingus 0.7.6 | pingus, 46 | builds | `-r opengl` fixed function or `-r sdl`; A | small | demo playback, UNVERIFIED | SDL_mixer | no |
| Wesnoth 1.18 | not built on ppc64 (Boost) | BE fine on s390x/powerpc | SDL2 renderer; C | official minimum 2-4 GB | `--nogui` AI vs AI with `--rng-seed` | SDL_mixer | no (RAM, build) |
| Warzone 2100 4.7 | not built (BE static assert) | real BE bug | **GL 3.0 / ES 3.0 required**; X | | | | out |

Fullscreen and gamepad for the picks are in their sections below. Every game here takes a resolution
on the command line or in its config and none needs a window manager for fullscreen, except
possibly SuperTuxKart (`IRR_DISABLE_NETWM=1` forces its legacy fullscreen; UNVERIFIED).

## Recommended suite

Ten games, eleven entries (OpenArena counts twice: its two renderers are different workloads).

| # | Entry | Why it's in | Covers |
|---|---|---|---|
| 1 | OpenArena `opengl1` | the baseline we have numbers for | Q3 shooter, S3TC switch, OpenAL |
| 2 | OpenArena `opengl2` | the only GLSL-heavy shooter with an S3TC switch and a timedemo | GLSL compile RAM, shaders vs NV47 limits |
| 3 | ClassiCube | ours, measured | voxel, OpenAL + Vorbis |
| 4 | QuakeSpasm | smallest footprint, GL 1.x, timedemo | shooter, SDL audio + own mixer |
| 5 | BananaBread | a step up in GPU load: GLSL 1.20 everywhere, FBO water | shooter, SDL_mixer, DDS textures |
| 6 | SuperTuxKart (legacy) | the racer with a real benchmark; the RAM stress test | racer, OpenAL, 750 MB of data |
| 7 | Luanti + minetest game | CPU-heavy Lua and meshing, the other voxel engine | voxel, CPU, OpenAL |
| 8 | Neverball | light GL with VBOs, shipped replays | SDL audio + Ogg |
| 9 | dsda-doom + Freedoom | GL and software renderers in one binary, `-timedemo` | 2.5D/texture path, SDL_mixer + MIDI |
| 10 | OpenTTD | CPU-heavy simulation with a clean headless benchmark | 2D, CPU, SDL audio |
| 11 | X-Moto | 2D in GL immediate mode with a built-in benchmark | 2D, SDL_mixer |

Sound stacks: OpenAL Soft (1-3, 6, 7), SDL audio with the game's own mixer (4, 8, 10), SDL_mixer
(5, 9, 11). SFML audio is only in Extreme Tux Racer (reserve). Compressed textures: OpenArena (both
renderers, the switch we already use), SuperTuxKart (setting, effect UNVERIFIED), BananaBread (needs
a one-line patch, below). For a DXT5 case without patching, Teeworlds and TORCS are the reserves.

Reserves, in order: Yamagi Quake II gl1 (OpenAL shooter, timedemo; needs our build like ClassiCube),
Extreme Tux Racer (SFML audio; no benchmark), Armagetron (long-running stability soak), Teeworlds
(DXT5 via `gfx_texture_compression`, 3D textures), TORCS (driver-side DXT1/DXT5), SuperTux.

### Packages

`image/deps.txt` lists the Debian names (`make deps` pulls their dependencies, from debian-ports
main only). Then a `ps3-games` metapackage next to `ps3-apps` in ps3-desktop's debian/control:
Depends on the light picks, Recommends the heavy ones, so the image can drop them.

```
# ps3-games: the benchmark suite (docs/GAME-SUITE.md)
quakespasm
cube2
cube2-data
neverball
dsda-doom
freedoom
openttd
openttd-opengfx
openttd-opensfx
openttd-openmsx
xmoto
luanti-data
luanti-game-minetest
supertuxkart
supertuxkart-data
```

Not from Debian:
- **Luanti engine**: our package, Debian's source with `ppc64` added to Architecture and
  `-DENABLE_LUAJIT=0` instead of `-DREQUIRE_LUAJIT=TRUE` (no BE LuaJIT risk). Run its unit tests in
  the build: they are our only BE evidence on ppc64.
- **Quake shareware data**: `game-data-packager quake` on the build PC downloads Quake 1.06
  shareware and makes `quake-shareware_*.deb` (pak0.pak, 18 MB, demo1-3). It is non-free:
  LAN repository only, like ClassiCube's audio. A registered copy needs the user's `id1/pak0.pak` and
  `pak1.pak`. LibreQuake (a free replacement) is only in Debian experimental. Install it with
  `--no-install-recommends`: game-data-packager-runtime pulls GTK 4 (46 MB).
- **Bench assets** (ours, small, in `games/bench/assets/`): OpenArena `bench.dm_71` (exists), a dsda
  demo, an OpenTTD savegame, an X-Moto replay, a Luanti world and its bench mod.

### Per game: command, parsing, compressed textures, display, gamepad

1. **OpenArena `opengl1`.** As `step0.sh`: `/usr/games/openarena +set cl_renderer opengl1
   +set r_swapInterval 0 +set com_maxfps 0 +set timedemo 1 +set nextdemo quit +demo bench`. Parse
   `(\d+) frames ([\d.]+) seconds ([\d.]+) fps [\d.]+/[\d.]+/([\d.]+)/` (fps, longest frame) and
   `CL_InitCGame: ([\d.]+) seconds` (load). S3TC: `r_ext_compressed_textures 1` (shipped on).
   Display `r_mode -2 r_fullscreen 1`; DS3 via SDL GameController (bound in autoexec.cfg).
2. **OpenArena `opengl2`.** The same with `+set cl_renderer opengl2`, in two variants:
   - *min*: `r_normalMapping 0 r_specularMapping 0 r_deluxeMapping 0 r_sunlightMode 0
     r_sunShadows 0 r_dlightMode 0 r_depthPrepass 0 r_hdr 0 r_postProcess 0 r_toneMap 0
     r_autoExposure 0 r_ssao 0 r_cubeMapping 0 r_ext_framebuffer_object 0` (lightall takes its
     small `USE_FAST_LIGHT` path; ~18 fewer programs without the sun);
   - *default*: Debian's defaults.
   What it needs (ioq3 `renderergl2`, read at upstream main): GL 2.0 and `#version 120` shaders, 30
   source files, ~66 KB, biggest `lightall_fp` 13.8 KB; ~100-115 programs compiled at every start,
   no binary cache, and any compile or link error is fatal. FBO-based features (HDR, tone mapping,
   sun and player shadows with 1024 and 512 depth maps, SSAO with an R32F target) need
   ARB_framebuffer_object or GL 3.0; **nv30 exposes neither, so on our Mesa they should all be off
   by themselves** (UNVERIFIED at run time) and the frame renders straight to the window.
   Cubemaps are forced off below GL 3.0. No vertex texture fetch in its shaders. S3TC:
   `r_ext_compressed_textures 1` also loads `.dds` files next to the TGAs; levels 1-2 use RGTC/BPTC
   where present (not here). First measurements: peak RSS during `R_Init` (`VmHWM`), whether every
   program links, then fps. If the compile burst is the problem, Mesa's disk shader cache
   (`MESA_SHADER_CACHE_DIR`) may help the second start (UNVERIFIED for nv30).
3. **ClassiCube.** No timedemo: fixed spawn, fixed time, fps from the Gallium HUD (as measured so
   far). A deterministic flight path needs a plugin (later).
4. **QuakeSpasm.** `quakespasm -nopackedpixels -width 1920 -height 1080 -fullscreen +timedemo demo1`
   (data path from the quake-shareware deb). Parse `(\d+) frames\s+([\d.]+) seconds\s+([\d.]+) fps`.
   It doesn't quit after a timedemo: the runner kills it on that line. Load time isn't printed: use
   the out.log stamp of the first demo line. No S3TC. Optional: `-noglslgamma` (a full-screen GLSL
   pass at 1080p) and `-noglsl` as the GL 1.x-only variant. Gamepad UNVERIFIED.
5. **BananaBread.** `/usr/games/cube2 -w1920 -h1080 -t1 -l<map> -x"sleep 30000 [echo (getfps 1);
   quit]"`. No demo playback in the client, so a fixed spawn in a fixed map for a fixed time; the exact
   `getfps` output and which map ships in cube2-data 1.3 are UNVERIFIED (pick in the first session).
   Texture compression: `texcompress` (default: textures of 1024+ pixels) only works when
   `usetexcompress` is set, which the engine skips on any `GL_VERSION` containing "Mesa". A one-line
   patch in a local cube2 build makes it a third S3TC game; otherwise only its DDS files are
   compressed. No gamepad.
6. **SuperTuxKart.** `supertuxkart -f -s 1920x1080 --benchmark` with `force_legacy_device` true in
   config.xml (GL 2.1 legacy path). It replays `benchmark_black_forest.replay`, writes
   `*.perf-report-black_forest.csv` (frame count, profiling ms, steady/typical fps, slow frames) to
   its config directory and exits. Parse the CSV; fps = frames / seconds. No load time. The GPU
   CSV uses timer queries: wrong on RSX, ignore it. S3TC `enable_texture_compression` (the legacy
   texture path may ignore it, UNVERIFIED). Lower RAM: `max_texture_size`, high-definition textures
   off, `geometry_level`, `animated_characters` off. Gamepad via SDL GameController.
7. **Luanti.** `luanti --go --worldname bench --config bench.conf` with `video_driver = opengl`,
   `viewing_range` 60, `client_mapblock_limit` 1000, `mesh_generation_threads` 1, `fps_max` 1000,
   fullscreen 1920x1080. Our `ps3bench` mod (to write) flies a fixed path, logs frames and time at
   the end and calls `core.request_shutdown()`; the Gallium HUD gives per-second fps meanwhile. No
   texture compression. Joystick support exists (`enable_joysticks`), UNVERIFIED in 5.15.
8. **Neverball.** `neverball --replay /usr/share/games/neverball/gui/demo1.nbr` with fullscreen,
   1920x1080 and `fps 1` in `~/.neverball/neverballrc`. It shows fps only on screen and stops at a
   menu: either the Gallium HUD with the runner's timeout, or a ~10-line patch in `st_demo_play`
   (print frames and time, exit when the replay ends) in a local build. No S3TC. SDL joystick works.
9. **dsda-doom.** `dsda-doom -iwad freedoom2.wad -vidmode gl -geometry 1920x1080f -timedemo bench.lmp`
   and the same with `-vidmode sw`. Parse `Timed (\d+) gametics in (\d+) realtics = ([\d.]+) frames
   per second`; it exits (`dsda_demo_end_quit`). Demo: record once with the same dsda version and IWAD
   (`-record bench -warp 1 -skill 4`). Sound on by default (SDL_mixer, fluidsynth for music). No
   S3TC setting found. Gamepad via SDL GameController.
10. **OpenTTD.** Simulation: `openttd -X -x -c bench.cfg -g bench.sav -v null:ticks=10000 -s null
    -m null`, timed by the runner, minus a `ticks=1` run for loading. Rendering: `openttd -g bench.sav
    -r 1920x1080 -f -v sdl-opengl` (then `-v sdl`) for a fixed time, fps from the Gallium HUD (the game
    runs on with no input). Savegame: ours, fixed `-G` seed, AIs. No S3TC. Music is MIDI: fluidsynth
    with a soundfont (TimGM6mb comes with the dependencies). No gamepad.
11. **X-Moto.** `xmoto --nowww -fs -res 1920x1080 --replay bench --timedemo --benchmark`. Parse
    `\* (\d+) frames rendered in ([\d.]+) seconds` and `Average framerate: ([\d.]+) fps`. No replays
    ship in xmoto-data: record one. Whether the process exits after the summary is UNVERIFIED (the
    runner's end regex covers it). No S3TC.

## Disk and download

Against the current image (OpenArena and ClassiCube are in it):

| Set | New packages | Download | Installed |
|---|---|---|---|
| Suite without SuperTuxKart | 56 | 298 MB | 649 MB |
| SuperTuxKart (its data 752 MB, Noto fonts) | +12 | +678 MB | +839 MB |
| Whole suite | 67 | 976 MB | 1.49 GB |
| Plus Quake shareware deb, our Luanti build | | ~10 MB | ~35 MB (Luanti binary size UNVERIFIED) |
| Reserves (ETR, Armagetron, SuperTux, Teeworlds) | 44 | 228 MB | 377 MB |

The biggest items: supertuxkart-data 752 MB, cube2-data 260 MB, neverball data 108 MB (Neverputt
would add 26 MB; it's left out), xmoto-data 62 MB, freedoom 57 MB.

## First session: smoke test

Goal: one launch per game, a list of driver bugs ordered by how many games each blocks. Before any
optimisation.

Protocol, per game:
- Through `ps3-bench.sh x smoke-<game>` with `BENCH_NOVSWAP=1` (first run of anything that might wedge
  the GPU) and `BENCH_TIMEOUT=180`: the bare X, the thermal watchdog and `dmesg.log` come with it.
- Check: the picture on the TV (ask the user, or a webcam shot only when the user isn't using it),
  sound (`/proc/asound/card*/pcm*p/sub*/status` RUNNING, and the user's ears), the game's own log.
- Record the **first** failure only, in one of these classes: game refuses the GL version or an
  extension; Mesa shader compile or link error (stderr); ps3rsx validator reject (dmesg);
  graph exception (dmesg; the RSX is wedged); out of memory; wrong colours (BE byte order).
- **A graph exception ends the session**: only a console reboot recovers the RSX, and reboots land
  in GameOS (the user goes back through Petitboot). So the order runs from safest to riskiest:

| Order | Game | Tier | What it leans on (watch for) |
|---|---|---|---|
| 1 | Chocolate Doom | A | software frame, SDL2 renderer blit (glamor-like) |
| 2 | Frozen Bubble | A | SDL surfaces via sdl12-compat |
| 3 | X-Moto | A | `glBegin` immediate mode, optional VBO/FBO (`--noexts` to bisect) |
| 4 | Chromium B.S.U. | A | immediate mode, OpenAL + ALUT |
| 5 | QuakeSpasm | A | `-noglsl` first, then GLSL gamma and alias models; colours (BE) |
| 6 | Extreme Tux Racer | A | GL 1.2 fixed function, lighting, texgen; SFML on BE |
| 7 | Neverball | A | VBOs, point parameters, multitexture; GLSL/FBO optional |
| 8 | Armagetron | A | `gluBuild2DMipmaps`, RGBA4/RGB5 textures |
| 9 | dsda-doom | A | `-vidmode sw`, then `gl`: NPOT, VBO, EXT_fbo, small GLSL |
| 10 | Teeworlds | A/C | **3D textures** (tilemaps), client arrays, `GL_GENERATE_MIPMAP` |
| 11 | SuperTux | A | `--renderer opengl20`: fixed function plus FBO |
| 12 | OpenTTD | B | `-v sdl` first; then `sdl-opengl`: R8 and 1D palette textures, PBO, BGRA + `UNSIGNED_INT_8_8_8_8_REV` on BE, a VAO extension check (it may refuse to start) |
| 13 | Luanti | C | GLSL 1.20 on every draw, FBO for effects; our build's unit tests first |
| 14 | BananaBread | C | GLSL 1.20 bump/parallax shaders against the 224-constant limit, EXT_fbo water reflection and refraction, depth-stencil |
| 15 | SuperTuxKart | C | legacy fixed function, many textures: RAM and VRAM first, then GL |
| 16 | OpenArena `opengl2` | B | ~100+ GLSL programs (`lightall_fp`), RAM during the compile burst; *min* first, then *default* |

Result: a table game → first failure → bug, and the bug list sorted by games blocked. Out of
scope for this session: anything with tier X (darkplaces, Tesseract, Warzone 2100).

## Design: games/bench/ (generalising step0.sh)

Not implemented. The pieces:

- **`games/bench/run.sh <game|suite> <variants> [N]`**: for each of N rounds, for each variant, runs
  `ps3-bench.sh x <game>-<variant> -- games/bench/client.sh <game> <variant>`, interleaved like
  `step0.sh tex` so drift spreads across variants. `suite` loops over every recipe: one switch (an
  environment variable such as `SPEQ_S3TC=ppe|spu`, or an OpenAL config) measured across all games
  in one sitting. `run.sh smoke <game>` is the first-session mode above.
- **Recipes, `games/bench/recipes/<game>.sh`** (sourced bash, variables only):
  - `CMD=(...)`, plus `variant_<name>()` functions that add arguments, environment or config;
  - `SETUP`: copy the bench asset into place (demo, replay, savegame, world, mod) and write the
    config file; `BACKUP`: config files the game rewrites on exit (q3config.cfg, config.xml,
    neverballrc), restored after every run;
  - `FPS_RE`, `LOAD_RE` (or `LOAD_MARK`: a line whose stamp ends loading), `MAXMS_RE`, and
    `RESULT_FILE` for games that write a file (SuperTuxKart's CSV);
  - `END_RE`: a line after which the client kills the game (QuakeSpasm, X-Moto, BananaBread);
    `DURATION`: fixed-time runs for games without an end (OpenTTD rendering, ClassiCube, Neverball);
  - `COMM`: the process name for ps3rsx's per-client VRAM (`ioquake3`, `cube2_client`, ...);
  - `HUD=1` when the game prints no fps (the Gallium HUD dump becomes the fps source).
- **`games/bench/client.sh`**: the X client. Sources the recipe, runs `SETUP`, starts the game through
  `ps3-game` (game mode, as the desktop does), watches its output for `END_RE`, enforces
  `DURATION`, copies `RESULT_FILE` into `$BENCH_DIR`, restores `BACKUP`.
- **Changes in `ps3-bench.sh`**: the OpenArena-specific parts move out. `q3config.cfg` backup becomes
  the recipe's `BACKUP` (the session-level restore stays as a safety net), and the `result` file's
  OpenArena greps (`CL_InitCGame`, `frames.*seconds`) go to the report. The HUD skip of the first 12
  seconds becomes the recipe's load time.
- **`games/bench/report.py`**: `step0-report.py` with the regexes and `COMM` from the recipe named in
  each run directory (`cmd` records it). Same columns (load, fps, longest frame, HUD median/p10/min,
  CPU, VRAM, GART, MemAvailable, swap-outs, VRAM swap), then per label the median and range, then
  for a `suite` run one line per game with the B/A ratio of fps and load, so a decision reads across
  games, not one.
- `games/openarena/` keeps its scripts until the OpenArena recipe reproduces step 0's numbers.

## Sources

- Package data: `snapshot.debian.org/archive/debian-ports/20261013T000000Z` (main ppc64 and all,
  contrib ppc64), `deb.debian.org/debian-ports` (live), `deb.debian.org/debian` Contents-all.
- Build status and logs: `buildd.debian.org/status/package.php?p=<src>&suite=sid` and
  `logs.php?pkg=<src>&arch=ppc64`; Boost: bugs.debian.org/1146645.
- Upstream: github.com/ioquake/ioq3 (`code/renderergl2`), github.com/sezero/quakespasm/issues/114,
  github.com/yquake2/yquake2/issues/138, github.com/freedoom/freedoom/issues/1532,
  supertuxkart.net/FAQ and stk-code 1.5 (`main.cpp`, `central_settings.cpp`, `profiler.cpp`),
  luanti 5.15.2 (`doc/os-compatibility.md`, `builtin/settingtypes.txt`, `cmake/Modules/FindLua.cmake`),
  OpenTTD 15.1 (`src/video/opengl.cpp`, `null_v.cpp`), warzone2100 4.7.0
  (`lib/ivis_opengl/gfx_api_gl.cpp`), cube2 and redeclipse `src/engine/rendergl.cpp`, tesseract
  `rendergl.cpp`, xmoto 0.6.3 `XMArgs.cpp`, `StateReplaying.cpp`, dsda-doom `prboom2/src`,
  game-data-packager 89 `data/quake.yaml`, `data/quake2.yaml`.
- Mesa 26.2.4: `src/gallium/drivers/nouveau/nv30/nv30_screen.c`, `nv30_format.c`,
  `src/mesa/state_tracker/st_extensions.c`, `st_format.c`.
