# Game candidates for the suite (research, 2026-10-16)

This is the survey that picked the benchmark suite, done on 2026-10-16 before any of these games
ran on the PS3. Step 0 had rested on OpenArena alone, which misled us about VRAM and sound, and the
SPU offload and memory decisions needed 5-10 titles. Every pick has run since, and so have most
reserves: Yamagi Quake II, Extreme Tux Racer and Armagetron joined the suite, Teeworlds, SuperTux
and Red Eclipse were smoke-tested, TORCS waits for the user, and Nexuiz runs on our own older
DarkPlaces build. Current results are in [games/README.md](../games/README.md); where this survey
and the measurements differ, the measurements win.

Sources: the debian-ports snapshot we pin (`20261013T000000Z`, ppc64 and all, contrib too), live
debian-ports, buildd.debian.org logs, the Debian BTS, upstream trackers and the games' source at the
packaged versions. **UNVERIFIED** marks what no source or log confirmed. Sizes are installed sizes of
what the image didn't have yet (the dependency closure against `build/rootfs/base.tar`'s dpkg
status). The same day, reviews against the source corrected several entries; the corrected facts
are written in below and marked "(review)".

## The yardstick: what Mesa nv30 on NV47 offers

From `nv30_screen.c` and `nv30_format.c` (Mesa 26.2.4) and [RSX.md](../RSX.md):

- GL 2.1 compatibility, GLSL 1.20, GLES 2.0. No GL 3.x context of any profile: engines that ask for
  3.2 core with no fallback fail at startup.
- Fragment programs: 4096 instructions, 224 vec4 constants, 32 temps, 8 varyings, 4 render targets.
  Vertex programs: 512 instructions, about 462 vec4 constants. No real loops (NIR unrolls).
- **No vertex texture fetch** (0 vertex samplers; `nv40_verttex_validate` is a stub).
- **No float textures**: 16- and 32-bit float formats are vertex formats only (no ARB_texture_float,
  no float FBOs). sRGB sampling yes, sRGB render targets no.
- NPOT, EXT_framebuffer_object, depth textures and ARB_shadow, occlusion queries, MSAA 2/4, S3TC.
  **ARB_framebuffer_object is not exposed** (`mixed_framebuffer_sizes` is false, the cap Mesa ties
  it to): engines that look only for the ARB name, or GL 3.0, treat FBOs as absent.
- **No ARB_texture_rg** (review): engines that upload R8 or RG textures fail.
- No texture arrays, instancing, geometry shaders, integer textures, OES_standard_derivatives.
  3D textures sample fine (review; rendering into them is refused). TIMESTAMP and TIME_ELAPSED
  queries return a pixel count, not time ([MESA-METHODS.md](../MESA-METHODS.md)): any game's GPU
  timings are garbage here.
- Known RSX traps: a graph exception wedges the whole RSX until a console reboot; vertex format type 0
  and swizzled 1-pixel-wide render targets fault (the validator refuses them, and Mesa patch 0007
  covers 1-pixel textures, review); single-attribute inline draws need Mesa 0008. Hardware mipmap
  generation was a new risk for Neverball, SuperTuxKart and Luanti (review).
- Mesa's disk shader cache is on for nv30 (review).
- Texture compression at upload: generic `GL_COMPRESSED_RGB` becomes DXT1 and `GL_COMPRESSED_RGBA`
  DXT5 (`st_format.c`). libspeq's encoder did DXT1 only then, so DXT5 fell back to Mesa's PPE
  encoder at 2.6 Mpixel/s. (DXT5 on the SPEs came with libspeq 0.367 and Mesa 0032, 2026-10-18.)

## Surprises

- **Four Debian games don't build on ppc64** (latest buildd logs):
  - wesnoth-1.18: links fail on `boost::context` (Debian #1146645: Boost doesn't set
    BOOST_USE_UCONTEXT on ppc64). Not a Wesnoth bug; it built on powerpc and s390x.
  - hedgewars 1.0.3: builds, then 2 of 7 engine Lua tests fail (`EInvalidOp` in `stuckcake.lua`, wrong
    positions in `gravity_get_set.lua`). Looked like FPC floating point on PPC, not byte order
    (UNVERIFIED then; later reading of the build log: two different failures, see
    [games/smoke-tested.md](../games/smoke-tested.md#skipped)).
  - ddnet 20.1.1: the test run hangs in `Net.Ipv4AndIpv6Work` and the buildd kills it.
  - warzone2100 4.7.0: **a real big-endian bug**, a static assert in the bundled GameNetworkingSockets
    (`minbase_endian.h:26`, `LittleWord()` on a uint64). Moot anyway: it needs GL 3.0.
- **Luanti has no ppc64 binary**: debian/control lists only LuaJIT architectures. Big-endian itself
  works: s390x builds 5.15.2 and passes `--run-unittests`. A 2025 ppc64 attempt (5.10.0, ports
  LuaJIT) aborted in TestSAO. LuaJIT exists for ppc64 and Debian strips the bundled Lua, so our build
  keeps LuaJIT (review; building without it means restoring `lib/lua` and `-DREQUIRE_LUAJIT=FALSE`).
- **contrib is arch:all only on debian-ports**: nothing arch-specific there is built, so Yamagi Quake
  II (source package `yquake2`, review) doesn't exist for ppc64 although it builds on s390x and
  ppc64el.
- **Debian's darkplaces is GL 3.2 core only** (since 0~20180908), so Nexuiz and Xonotic are out with
  Debian's engine.
- **Sauerbraten and Red Eclipse turn texture compression off on Mesa**: `if(!mesa) usetexcompress = 2`
  in `rendergl.cpp`; only pre-made DDS files go in compressed.
- **QuakeSpasm 0.96.3 has a big-endian bug**: wrong colours and dlight wraparound from the
  packed-pixels path (quakespasm issue #114, a G5 on Debian ppc64); `-nopackedpixels` avoids it,
  upstream master disables the path on big-endian.
- **Freedoom 0.13's built-in demos desync** (freedoom issue #1532).
- Hardly any game runs tests on the buildds (most `dh_auto_test` steps do nothing), so "builds on
  ppc64" is the only big-endian evidence for most. Exceptions: OpenTTD (97/97 ctest pass on ppc64),
  Frozen Bubble (tests pass on ppc64), Luanti (s390x).
- The OpenArena GLSL renderer (`cl_renderer opengl2`) was tried once: commit 82b6b5f (2026-10-15) went
  back to opengl1 because "the GLSL one ran out of RAM compiling its shaders", no log kept. That was
  before the VRAM swap and game mode, so it deserved another try.

## Candidates

Tier is driver readiness: **A** likely runs as is, **B** likely needs driver work, **C** unknown until
tried, **X** can't run on NV47. MB = installed (engine + data + new libraries).

| Game (engine) | ppc64 package, MB | Big-endian status | GL need, tier | RAM | Benchmark mode | Exercises | Verdict |
|---|---|---|---|---|---|---|---|
| OpenArena `opengl1` (ioquake3 1.36) | openarena 0.8.8 (in image, 422) | runs (ours) | GL 1.1 fixed function, multitexture; A | 74 MB resident mid-game (measured) | timedemo, prints fps; load from `CL_InitCGame` | S3TC `r_ext_compressed_textures`; OpenAL Soft; WAV + Ogg; CPU/driver bound | **suite** (baseline) |
| OpenArena `opengl2` (renderergl2) | same | same | GLSL 1.20, about 100-115 programs at startup; FBO paths need ARB_fbo; B | ran out of RAM compiling (82b6b5f) | same timedemo | GLSL compile burst, normal/specular maps, S3TC + DDS | **suite** (driver work) |
| ClassiCube (ours) | classicube 1.3.8 (in image) | runs | its default GL backend (which one UNVERIFIED) | UNVERIFIED | none (HUD fps) | OpenAL, Ogg (Mojang audio); voxel meshing | **suite** |
| QuakeSpasm 0.96.3 | quakespasm, 2 + data | PPC supported; the packed-pixels bug, `-nopackedpixels` | GL 1.x + tiny GLSL (gamma, alias models); A | small; `-heapsize` | `+timedemo demo1`, prints fps, doesn't quit | SDL audio, own mixer; Ogg/Opus/MP3/FLAC music; no S3TC | **suite** |
| Yamagi Quake II 8.70, `gl1` | not on ppc64 (contrib): our build | upstream tests big-endian incl. PPC64 | GL 1.4 fixed function; A (gl3/gles3: X) | small | `+timedemo 1 +demomap q2demo1.dm2` (the demo pak's only demo, review) | OpenAL; WAV + Ogg; no S3TC | reserve (needs our build) |
| darkplaces / Nexuiz 2.5.2 | darkplaces + nexuiz, 771 | builds | **GL 3.2 core only** in Debian; X | heavy | `-benchmark demo` (the best of all) | S3TC + DDS cache `r_texture_dds_save` | out with Debian's engine |
| Xonotic | not in Debian, about 1 GB data | | darkplaces GL 3.2 | heavy | the-big-benchmark.sh | as above | out |
| ezquake 3.6.8 | ezquake, 6 | big-endian fixes upstream | modern GL 4.x: X; classic path: C | small | classic timedemo, UNVERIFIED | UNVERIFIED | out (Quake data; QuakeSpasm covers it) |
| Alien Arena | contrib + non-free data | | GLSL, UNVERIFIED | | | | out |
| Warsow / Warfork | not in Debian | | UNVERIFIED (likely GL 3) | | | | out |
| dsda-doom 0.30.0 (review) + freedoom 0.13 | dsda-doom, freedoom, 84 | builds, endian-aware swaps | `-vidmode gl` uploads indexed textures as GL_RG, absent on nv30 (review); GLSL `#version 110` mandatory there, EXT_fbo optional; `-vidmode sw` software | small | `-timedemo x.lmp`, prints fps, exits | SDL_mixer + fluidsynth MIDI; 2.5D texture path | **suite** |
| Chocolate / Crispy Doom | chocolate-doom 3.1.1 / crispy-doom 7.2, 65 | builds | software, SDL2 renderer blit; A | tiny | `-timedemo`, prints fps, exits | SDL_mixer | smoke test only (the safest first run) |
| Woof 16 | woof-doom, 82 | builds | software, SDL3 blit; A | tiny | `-timedemo` (ends in a message box, UNVERIFIED) | OpenAL | no (dsda covers it) |
| Doomsday 2.3.1 | doomsday, 203 | builds | shaders, Qt5, UNVERIFIED; C | heavy | UNVERIFIED | | no |
| Neverball 1.6 (+Neverputt) | neverball (+neverputt), 112 (138) | builds, tests disabled | GL fixed function; VBO and point parameters required with no fallback; GLSL/FBO optional; A | small | `--replay demo1.nbr` (shipped); fps on screen only, stops at a menu | SDL audio + Ogg; no S3TC | **suite** (patch or HUD) |
| Extreme Tux Racer 0.8.4 | extremetuxracer, 48 | builds; SFML 3.1 has its big-endian Ogg fix | GL 1.2 fixed function, lighting, texgen; `sf::RenderTexture` needs EXT_fbo; A | small | none | SFML audio (WAV/Ogg) | reserve (no benchmark) |
| Armagetron 0.2.9.3 | armagetronad, 27 | builds; risk: GL_RGBA or GL_BGRA picked from the SDL surface's red mask | GL 1.x, SDL 1.2; A | small | `--playback` of recordings, UNVERIFIED | SDL_mixer | reserve (soak test) |
| Sauerbraten (cube2 2020) | cube2 + cube2-data 1.3, 266 | builds, no tests | GL 2.0 + GLSL 1.20 + FBO (EXT is fine) required; float/VTF optional; GPU skinning's bone-array indexing becomes if/else trees on nv30; C | UNVERIFIED; `maxtexsize`, `texreduce` | scripted: `-x` CubeScript `getfps` + `quit` | SDL_mixer, Ogg/WAV; S3TC off on Mesa (DDS only); a step up in GPU load | **suite** |
| Tesseract | not in Debian | | requires 4 vertex texture units, a float G-buffer; X | | | | out |
| Red Eclipse 1.6 | redeclipse, 945 | builds | the same engine as Sauerbraten; C | heavy maps | scripted like cube2 | as cube2; no gamepad | reserve (Sauerbraten covers it, 936 MB of data) |
| SuperTuxKart 1.5 (legacy GL 2.1) | supertuxkart, 839 | builds; old #714519, wrong colours on powerpc (0.8 era) | GLSL path needs 3.1, legacy fixed-function fallback; C | **at least 1 GB (FAQ)** | `--benchmark`: replay, CSV report, exits | OpenAL + Ogg; `enable_texture_compression` (effect on legacy UNVERIFIED) | **suite** (stretch: RAM) |
| Trigger Rally 0.6.6 | trigger-rally, 122 | builds | GL 2.0 compat, fixed function, cube maps; A | small | none (no command line, no fps counter) | OpenAL + ALUT | reserve |
| Scorched3D 44 | scorched3d, 125 | builds | fixed function, optional GLSL/FBO/shadows; A | UNVERIFIED | none (`-startclient` AI game, UNVERIFIED) | OpenAL + Ogg, wxWidgets | no |
| TORCS 1.3.7 | torcs, 529 | builds | plib, fixed function; A | UNVERIFIED | `-r race.xml` runs headless (no fps) | **driver-side compression** `GL_COMPRESSED_*_ARB` (DXT1/DXT5 via Mesa); OpenAL | reserve (S3TC test) |
| BZFlag 2.4.30 | bzflag-client, 18 | builds | fixed function, GLEW; A | small | none | own mixer on SDL | no |
| X-Moto 0.6.3 | xmoto, 84 | builds | immediate mode, optional VBO/FBO/shaders (`--noexts` to bisect); A | small | `--replay F --timedemo --benchmark`, prints fps; a replay ships (`credits`, review) | SDL_mixer, Ogg/WAV; 2D | **suite** |
| Luanti 5.15.2 + Minetest Game | data only (27), engine ours | s390x passes the unit tests | `video_driver=opengl`: GLSL 1.20 mandatory, FBO for effects; C | default view likely over 200 MB, UNVERIFIED | none for rendering; needs a mod | OpenAL + Ogg; CPU: Lua, meshing | **suite** (stretch, our build) |
| SuperTux 0.6.3 | supertux, 292 | builds (an old big-endian FTBFS fixed) | `--renderer opengl20`: fixed function + FBO; A | UNVERIFIED | `--play-demo`, no fps output | OpenAL + Ogg | reserve |
| Teeworlds 0.7.5 | teeworlds, 11 | an old big-endian crash fixed in 2008 | GL 1.x, **3D textures** for tile maps; A/C | small | `"play demos/x.demo"`, on-screen fps, returns to the menu | SDL audio, WavPack; `gfx_texture_compression 1` gives DXT5 | reserve (the DXT5 case) |
| DDNet | uninstallable on ppc64 (the test hang) | upstream big-endian fixes | legacy GL 1.1 by default | | | | no |
| OpenTTD 15.1 | openttd + opengfx/opensfx/openmsx, 71 | **97/97 tests pass on ppc64** | `-v sdl-opengl` can't start (review: it needs GL 3.0 or ARB_texture_rg, its VAO check wants ARB *and* APPLE VAOs below GL 3.0, its sprite shaders use `texture2DLod`, which needs ARB_shader_texture_lod): B; `-v sdl` software: A | UNVERIFIED; map size | `-v null:ticks=N` simulation, timed | SDL audio + own mixer, WAV; MIDI via fluidsynth; CPU | **suite** |
| Chromium B.S.U. | chromium-bsu, 6 | builds | GL 1.x immediate mode; A | tiny | none (`--debug` prints start-up fps) | OpenAL + ALUT | smoke test only |
| Hedgewars 1.0.3 | not built (2 engine tests fail) | FP issue, UNVERIFIED | GL 1.x path in Debian; A | UNVERIFIED | `hwengine --stats-only demo.hwd` (headless) | SDL_mixer | no (needs a package fix) |
| Frozen Bubble 2.212 | frozen-bubble, 53 | tests pass on ppc64 | SDL 1.2 surfaces, no GL; A | small | none | SDL_mixer, Ogg/WAV; Perl | smoke test only |
| Pingus 0.7.6 | pingus, 46 | builds | `-r opengl` fixed function or `-r sdl`; A | small | demo playback, UNVERIFIED | SDL_mixer | no |
| Wesnoth 1.18 | not built on ppc64 (Boost) | big-endian fine on s390x/powerpc | SDL2 renderer; C | official minimum 2-4 GB | `--nogui` AI against AI with `--rng-seed` | SDL_mixer | no (RAM, build) |
| Warzone 2100 4.7 | not built (the static assert) | a real big-endian bug | **GL 3.0 / ES 3.0 required**; X | | | | out |

Every game here takes a resolution on the command line or in its config and none was expected to
need a window manager for fullscreen, except possibly SuperTuxKart (`IRR_DISABLE_NETWM=1` forces its
legacy fullscreen; UNVERIFIED). In practice SDL's fullscreen windows needed one: the runner starts
Openbox.

## The recommended suite and why

Ten games, eleven entries (OpenArena counts twice: its two renderers are different workloads).

| # | Entry | Why it's in | Covers |
|---|---|---|---|
| 1 | OpenArena `opengl1` | the baseline we had numbers for | Q3 shooter, S3TC switch, OpenAL |
| 2 | OpenArena `opengl2` | the only GLSL-heavy shooter with an S3TC switch and a timedemo | GLSL compile RAM, shaders against NV47's limits |
| 3 | ClassiCube | ours, measured | voxel, OpenAL + Vorbis |
| 4 | QuakeSpasm | the smallest footprint, GL 1.x, a timedemo | shooter, SDL audio + own mixer |
| 5 | Sauerbraten | a step up in GPU load: GLSL 1.20 everywhere, FBO water | shooter, SDL_mixer, DDS textures |
| 6 | SuperTuxKart (legacy) | the racer with a real benchmark; the RAM stress test | racer, OpenAL, 750 MB of data |
| 7 | Luanti + Minetest Game | CPU-heavy Lua and meshing, the other voxel engine | voxel, CPU, OpenAL |
| 8 | Neverball | light GL with VBOs, shipped replays | SDL audio + Ogg |
| 9 | dsda-doom + Freedoom | GL and software renderers in one binary, `-timedemo` | 2.5D texture path, SDL_mixer + MIDI |
| 10 | OpenTTD | CPU-heavy simulation with a clean headless benchmark | 2D, CPU, SDL audio |
| 11 | X-Moto | 2D in GL immediate mode with a built-in benchmark | 2D, SDL_mixer |

PrBoom+ GL (on the user's list) is covered by dsda-doom, its successor with the same GL renderer,
packaged for ppc64. Sound stacks: OpenAL Soft (1-3, 6, 7), SDL audio with the game's own mixer (4, 8,
10), SDL_mixer (5, 9, 11); SFML audio only in Extreme Tux Racer. Compressed textures: OpenArena (both
renderers), SuperTuxKart (a setting, effect unverified), Sauerbraten (with a one-line patch). For a
DXT5 case without patching, Teeworlds and TORCS were the reserves.

Reserves, in order: Yamagi Quake II gl1 (an OpenAL shooter with a timedemo; needs our build), Extreme
Tux Racer (SFML audio; no benchmark), Armagetron (a long-running stability soak), Teeworlds (DXT5 via
`gfx_texture_compression`, 3D textures), TORCS (driver-side DXT1/DXT5), SuperTux.

The suite that ran has 13 games: these ten plus Yamagi Quake II, Extreme Tux Racer and Armagetron.

## Per-game notes from the research

What the survey expected for each pick, before the runs (the commands that run now are in
[games/SUITE.md](../games/SUITE.md)):

- **OpenArena `opengl2`**: renderergl2 (read at upstream main) needs GL 2.0 and `#version 120`
  shaders, 30 source files, about 66 KB, the biggest `lightall_fp` 13.8 KB; about 100-115 programs
  compiled at every start, no binary cache, any compile or link error fatal. FBO features (HDR, tone
  mapping, sun and player shadows with 1024 and 512 depth maps, SSAO with an R32F target) need
  ARB_framebuffer_object or GL 3.0, which nv30 exposes neither of, so they should be off by
  themselves. Cubemaps are forced off below GL 3.0; no vertex texture fetch. The likely RAM cause: bone
  variants index a 105-entry uniform array, which nv30 turns into four 105-leaf if/else trees per
  program, compiled at every start though OpenArena's models never use them; about 200 shader
  objects stay attached all session. Plan: *min* settings first (normal, specular, deluxe mapping,
  sun, dlights, depth prepass, HDR, post-processing, tone mapping, auto exposure, SSAO, cube maps and
  FBOs off; lightall takes its `USE_FAST_LIGHT` path, about 18 fewer programs without the sun), then
  Debian's defaults; peak RSS during `R_Init`.
- **ClassiCube**: no timedemo; a fixed spawn and the HUD, a flight-path plugin later.
- **QuakeSpasm**: `-noglslgamma` (a full-screen GLSL pass at 1080p) and `-noglsl` as variants; load
  time from the out.log stamp of the first demo line.
- **Sauerbraten**: no demo playback in the client was assumed, so a fixed spawn for a fixed time;
  `texcompress` (textures of 1024 and up) only works when `usetexcompress` is set, which the engine
  skips on any `GL_VERSION` containing "Mesa": a one-line patch (`engine/rendergl.cpp:536`).
- **SuperTuxKart**: `--benchmark` with `force_legacy_device` replays `benchmark_black_forest.replay`
  and writes a CSV; the GPU CSV uses timer queries, wrong on the RSX. Lower RAM through
  `max_texture_size`, HD textures off, `geometry_level`, `animated_characters` off.
- **Luanti**: `viewing_range` 60, `client_mapblock_limit` 1000, `mesh_generation_threads` 1,
  `fps_max` 1000; a `ps3bench` mod to fly a fixed path and call `core.request_shutdown()`.
- **Neverball**: fps only on screen and it stops at a menu, so either the HUD with a timeout or a
  10-line patch in `st_demo_play` (that patch is what we did).
- **dsda-doom**: record our own demo with the same version and IWAD (`-record bench -warp 1 -skill
  4`); sound on by default.
- **OpenTTD**: simulation `-v null:ticks=N` timed by the runner minus a `ticks=1` run for loading;
  rendering for a fixed time with HUD fps. Music is MIDI through fluidsynth with a soundfont
  (TimGM6mb comes with the dependencies).
- **X-Moto**: replays are little-endian on every host; paced in real time, about 144 s, so the
  timeout must be above 180 s.

Packages proposed then: a `ps3-games` metapackage next to `ps3-apps` (Depends on the light picks,
Recommends the heavy ones); the Luanti engine as our package with its unit tests in the build; the
Quake shareware deb from game-data-packager (non-free, LAN repository only, installed with
`--no-install-recommends` because game-data-packager-runtime pulls GTK 4; LibreQuake, a free
replacement, only in Debian experimental); bench assets of our own in `games/bench/assets/`.

## The planned first session

One launch per game, the first failure of each recorded, a driver bug list ordered by how many games
each blocks; through `ps3-bench.sh` with the VRAM swap off and a 180 s timeout. Because a graph
exception ends the session, the order went from safest to riskiest:

| Order | Game | Tier | What it leans on |
|---|---|---|---|
| 1 | Chocolate Doom | A | software frame, SDL2 renderer blit |
| 2 | Frozen Bubble | A | SDL surfaces via sdl12-compat |
| 3 | X-Moto | A | `glBegin` immediate mode, optional VBO/FBO |
| 4 | Chromium B.S.U. | A | immediate mode, OpenAL + ALUT |
| 5 | QuakeSpasm | A | `-noglsl` first, then GLSL gamma and alias models; colours |
| 6 | Extreme Tux Racer | A | GL 1.2 fixed function, lighting, texgen; SFML on big-endian |
| 7 | Neverball | A | VBOs, point parameters, multitexture |
| 8 | Armagetron | A | `gluBuild2DMipmaps`, RGBA4/RGB5 textures |
| 9 | dsda-doom | A | `-vidmode sw`, then `gl`: NPOT, VBO, EXT_fbo, small GLSL |
| 10 | Teeworlds | A/C | 3D textures, client arrays, `GL_GENERATE_MIPMAP` |
| 11 | SuperTux | A | `--renderer opengl20`: fixed function plus FBO |
| 12 | OpenTTD | B | `-v sdl` first; then `sdl-opengl`: R8 and 1D palette textures, PBO, BGRA + `UNSIGNED_INT_8_8_8_8_REV` on big-endian, the VAO check |
| 13 | Luanti | C | GLSL 1.20 on every draw, FBO for effects; our build's unit tests first |
| 14 | Sauerbraten | C | GLSL 1.20 bump/parallax shaders against the 224-constant limit, EXT_fbo water, depth-stencil |
| 15 | SuperTuxKart | C | legacy fixed function, many textures: RAM and VRAM first |
| 16 | OpenArena `opengl2` | B | about 100+ GLSL programs, RAM during the compile burst |

Out of scope: anything with tier X (darkplaces from Debian, Tesseract, Warzone 2100).

## Sources

- Package data: `snapshot.debian.org/archive/debian-ports/20261013T000000Z` (main ppc64 and all,
  contrib ppc64), `deb.debian.org/debian-ports` (live), `deb.debian.org/debian` Contents-all.
- Build status and logs: `buildd.debian.org/status/package.php?p=<src>&suite=sid` and
  `logs.php?pkg=<src>&arch=ppc64`; Boost: bugs.debian.org/1146645.
- Upstream: github.com/ioquake/ioq3 (`code/renderergl2`), github.com/sezero/quakespasm/issues/114,
  github.com/yquake2/yquake2/issues/138, github.com/freedoom/freedoom/issues/1532,
  supertuxkart.net/FAQ and stk-code 1.5 (`main.cpp`, `central_settings.cpp`, `profiler.cpp`), luanti
  5.15.2 (`doc/os-compatibility.md`, `builtin/settingtypes.txt`, `cmake/Modules/FindLua.cmake`),
  OpenTTD 15.1 (`src/video/opengl.cpp`, `null_v.cpp`), warzone2100 4.7.0
  (`lib/ivis_opengl/gfx_api_gl.cpp`), cube2 and redeclipse `src/engine/rendergl.cpp`, tesseract
  `rendergl.cpp`, xmoto 0.6.3 `XMArgs.cpp`, `StateReplaying.cpp`, dsda-doom `prboom2/src`,
  game-data-packager 89 `data/quake.yaml`, `data/quake2.yaml`.
- Mesa 26.2.4: `src/gallium/drivers/nouveau/nv30/nv30_screen.c`, `nv30_format.c`,
  `src/mesa/state_tracker/st_extensions.c`, `st_format.c`.

Older versions and the dated history: [archive/GAME-SUITE.md](../archive/GAME-SUITE.md).
