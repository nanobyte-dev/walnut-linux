# Game support plan: one stage per game

2026-10-16. The SPU offload work is parked (SPU-OFFLOAD-PLAN.md) so the game suite and its driver
work come first. Rule from the user: game decisions rest on 5-10 games, not OpenArena alone. Some
games will need driver work (Mesa nv30, ps3rsx) before they run, and that comes before any
optimisation. The research behind each game (packages, big-endian status, GL needs, commands) is
in GAME-SUITE.md; this file is the order of work and its results.

The games: the user's list (OpenArena, QuakeSpasm, Yamagi Quake II, Extreme Tux Racer, Neverball,
Armagetron, Sauerbraten; stretch: SuperTuxKart, Luanti, PrBoom+ GL) and the research's picks
(ClassiCube, OpenTTD, X-Moto). PrBoom+ GL is covered by dsda-doom, its successor with the same GL
renderer, packaged for ppc64. Teeworlds and TORCS stay reserves: they test DXT5, which waits for
the SPU plan.

## Every stage has the same shape

- **Get:** a Debian main package (`image/deps.txt`, `make deps` with `build/aptly` backed up
  first), our package (a debsrc directory like `mesa/`: `make <pkg>-work`, `-patches`, `-deb`), or
  a data deb. The game joins the `ps3-games` metapackage (ps3-desktop's debian/control) once it runs.
  Recommends are never installed (the image's apt config, kept on the PS3, and aptly's
  `-filter-with-deps` both skip them): every package a game needs goes into Depends and
  `image/deps.txt` by name, and heavy games stay out of ps3-games' Depends and are deployed by name
  (`make deploy PKGS="ps3-games supertuxkart supertuxkart-data"`).
- **Before a session:** `make deps` (one refresh per group: each is permanent in the `ps3` suite,
  ~1 GB for all of them in `build/aptly`; `make du` first, /home is at 87%), the packages built,
  `make deploy`, `make bench-push`, `df /` on the PS3.
- **Expect:** the known risks, below per game.
- **Smoke:** `run.sh smoke <game> [variant]` on the PS3 (VRAM swap off, 180 s cap). Check the
  picture on the TV, the sound state in bench.txt, kernel and game errors (run.sh prints them).
  Each recipe's setup writes the game's config first: 1920x1080 fullscreen, first-run dialogs off.
  Record the first failure, classed as: GL version or extension refused, shader compile or link,
  validator reject, graph exception, out of memory, wrong colours (big-endian).
- **Fix:** a driver bug goes into the bug list below and is fixed as a Mesa patch or in ps3rsx; a
  game bug in the game's own debsrc package. Then smoke again.
- **Bench:** a recipe (`games/bench/recipes/<game>.sh`) and its asset, then
  `run.sh bench <game> base 3` as the baseline.
- **Done when:** right picture and sound, no kernel errors, numbers from the recipe, baseline in
  the results table, and about 60 fps at 1920x1080 for the light games (groups 1 and 2; the user,
  2026-10-17: "aim is 60 or close to"). Correct but slow is "runs, needs performance work": then a
  perf profile of the game process says where the time goes (game, Mesa, kernel) before any fix.

A graph exception ends the session: only a console reboot clears the RSX, and the user goes back
through Petitboot. Within a session the riskiest game goes last. After one, don't run
`ps3-bench.sh desktop` (it starts X on the wedged RSX): the user reboots from the console. The
game's config backups are on disk (`/var/tmp/ps3-bench/backup/<game>`) and the game's next run
restores them first. The bench lock is in /dev/shm, so the reboot clears it.

## Stage 0: groundwork

- `games/bench/`: `run.sh` (smoke, bench, suite, list), `client.sh` (one game run from a recipe),
  `report.py` (from step0-report.py), recipes (`recipes/README.md` lists the variables), assets.
  `make bench-push` copies it to `/var/tmp/bench` on the PS3, `make bench-pull` fetches results.
- Recipes for the two games that run already: OpenArena (variants tc0, tc1, gl2min, gl2) and
  ClassiCube (a saved map, 60 s, HUD fps; variant vsync). ClassiCube since got a timedemo: the
  ccbench plugin (`games/bench/classicube-bench`) flies a fixed 3000-frame camera path over the map
  after a warm-up that builds every chunk, prints fps and the slowest frame and quits (variants
  vsync, shots: keyframe screenshots; still: the old 60 s at the spawn).
- The `ps3-games` metapackage: OpenArena and ClassiCube to start with; ps3-desktop recommends it.
- `ps3-bench.sh` stays as it is until the recipe has validated (step0.sh and step0-report.py
  read its OpenArena greps, its q3config.cfg backup and its 12 s HUD skip); then those move out.
- Games run directly by default, as in step 0; `GAMEMODE=1` in a recipe starts them through
  `ps3-game` (its slice and game mode change memory and timing, so it is a variant to measure).
- Accepted when both recipes give numbers with the picture on screen (checked on the webcam:
  a run with a black screen doesn't count). Matching step 0 was dropped as a criterion: step 0
  ran with nothing on screen, so its game numbers are invalid (SPU-OFFLOAD-SURVEY.md).

**Done 2026-10-16.** Two display bugs had to go first, both found because the screen stayed black:
SDL's fullscreen window needs a window manager (client.sh starts a bare Openbox), and ps3rsx
muted the video for every X started after another had quit (a1f1b91). Validation (Openbox,
picture checked on camera, ps3rsx 0.219, Mesa 26.2.4-1+ps3.186, libspeq0 0.210):

| Recipe | Runs | Load | fps | HUD fps median / p10 | Peak RSS | Peak VRAM |
|---|---|---|---|---|---|---|
| openarena tc1 | 3 | 10.8 s [10.6-11.9] | 42.2 [40.9-46.4] | 47 / 6-28 | 83 MB | 37-40 MB |
| classicube (60 s, saved map) | 2 | | | 50.5 / 44 | 51 MB | 61 MB |

Showing the frames costs: the same OpenArena runs with nothing on screen gave 45-54 fps.

## Group 1: should run as packaged (Debian main, GL 1.x fixed function)

Installed 2026-10-17: dsda-doom 0.30.0, freedoom 0.13.0, xmoto 0.6.3, neverball 1.6.0+git20180603,
extremetuxracer 0.8.4, armagetronad 0.2.9.3 (in `image/deps.txt`, deployed by name; each joins
ps3-games once its smoke test passes).

Expectations below were checked against each game's source at the packaged version and against
our Mesa (reviews of this plan, 2026-10-16); nothing here has run yet.

**1.0 Driver pre-checks** (no game, `ps3-gpu-tools/mesa-tests`, before the group's session; each
settles a risk several games share):
- the extension list on the PS3 (`glxinfo -l`, kept in RSX.md): ARB_texture_rg,
  ARB_shader_texture_lod, EXT_vertex_array_bgra, point parameters, NPOT as Mesa really exposes them;
- **hardware mipmap generation** (glGenerateMipmap on a 64x64 RGBA texture): Mesa renders every
  level with a blit, power-of-two textures are swizzled, and the 1-pixel-wide levels make the
  swizzled render target ps3rsx's validator refuses (patch 0007 covers 1-pixel textures, not
  1-pixel mip levels). Neverball, SuperTuxKart and Luanti generate mipmaps this way;
- **16-bit texel byte order**: L8A8/R8G8 are byte arrays in Gallium but nv30 samples them as one
  16-bit A8L8 texel; if the RSX reads 16-bit texels as big-endian halfwords, luminance and alpha
  swap (fonts, Neverball's GL_LUMINANCE_ALPHA). One test over LA8, 565, 4444, 5551, L8, A8, I8.

| # | Game | Get | Expect | Bench |
|---|---|---|---|---|
| 1 | dsda-doom + Freedoom | Debian (dsda-doom 0.30.0), 84 MB | `-vidmode sw` runs (software). **`-vidmode gl` needs GL_RG textures** (every indexed texture: walls, sprites, menus), and nv30 has no ARB_texture_rg: missing or black textures. GLSL (`#version 110`) is mandatory there, EXT_fbo optional | `-timedemo`, prints `Timed ... frames per second` and exits; Freedoom's own DEMO lumps if they play without desync, else our plain `-record` demo (portable across endianness) |
| 2 | X-Moto | Debian, 84 MB | immediate mode, optional VBO/FBO (`--noexts` to bisect); no swap interval set: `vblank_mode=0` | `--replay credits --timedemo --benchmark` (a replay ships in xmoto-data; replays are little-endian on every host); exits after; paced in real time, ~144 s, so `BENCH_TIMEOUT` above 180 |
| 3 | Neverball | Debian (a 2018 snapshot of 1.6), 112 MB | VBO and point parameters are required with no fallback (a NULL call if missing); **mipmap generation** (1.0) with its default `mipmap 1`: `mipmap 0` is the workaround; GL_LUMINANCE_ALPHA textures (1.0) | `gui/demo1.nbr` (neverball-common), real-time paced, stops at a menu: HUD fps for a fixed time with vsync off |
| 4 | Extreme Tux Racer | Debian, 48 MB | GL 1.2 lighting and texgen; `sf::RenderTexture` needs EXT_fbo (exposed); SFML 3.1 has its big-endian Ogg fix | none: compatibility only (`framerate 0` in options.txt), not in the timed suite |
| 5 | Armagetron | Debian, 27 MB | textures are RGBA8 by default (`TEXTURES_HI 1`); the big-endian risk is in the game: it picks GL_RGBA or GL_BGRA from the SDL surface's red mask, so red and blue may swap (a game patch) | a real benchmark: `--record f.aarec`, then `--playback f.aarec --benchmark` (prints fps; recorded on the PS3). The soak: spectator mode with AI players (`SPECTATOR_MODE_1`, `SP_NUM_AIS`, ...), started once from the menu |

## Group 2: our package or data, light GL

QuakeSpasm (2026-10-17): runs, the timedemo through a quake.rc of ours in ~/.quakespasm/id1
(the shareware's own quake.rc starts the attract loop after +timedemo; -game is refused by the
shareware). 59.2 fps without GLSL, 64.8 with, 70.3 with the gamma pass: colours right.
Yamagi Quake II (2026-10-17): runs (gl1, the demo pak's q2demo1 timedemo), picture and sound right:
56.1 fps with OpenAL, 58.4 with its SDL backend (`s_openal 0`). The OpenAL white noise was its
16-bit WAV samples uploaded little-endian; our yquake2 8.70+dfsg-1+ps3.240 swaps them to host
order at load (the user confirmed the sound).

| # | Game | Get | Expect | Bench |
|---|---|---|---|---|
| 6 | QuakeSpasm | Debian, 2 MB; `quake-shareware` from game-data-packager (contrib, built in the Debian games builder with `lhasa`: `make quake-data` into `build/debs/quake-data`; non-free like ClassiCube's audio, so our LAN repository only; published once and reused, since its output isn't reproducible). Not the `quake` launcher package (it pulls GTK 4) | `-nopackedpixels` (the upstream fix disables 10-bit lightmaps on big-endian; nv30 has no 10-bit format anyway). Packed pixels only exist with GLSL, so `-noglsl` first, then GLSL with `gamma 0.9` (the gamma pass only runs off 1.0) | `+timedemo demo1`, `%i frames %5.1f seconds %5.1f fps`, doesn't quit: stopped at that line |
| 7 | Yamagi Quake II (gl1) | our debsrc build of Debian's **`yquake2`** 8.70 (contrib: debian-ports doesn't build it; the deb builder's sources need `contrib` for `apt-get source`; its build-deps, SDL3 included, are in main); `quake2-demo-data` from game-data-packager | gl1 is the default renderer (gl3/gles3 need GL 3); upstream tests PPC64 | `+timedemo 1 +demomap q2demo1.dm2` (the demo pak's only demo), `%i frames, %3.1f seconds: %3.1f fps`, returns to the console: stopped at that line |

Prepared 2026-10-17 (nothing has run yet): `make quake-data` gives `quake-shareware_89_all.deb` and
`quake2-demo-data_89_all.deb` (build/debs/quake-data; publish them once, later runs reuse the
published files, docs/PACKAGING.md); `make yquake2-deb` gives `yamagi-quake2` and
`yamagi-quake2-core` 8.70+dfsg-1+ps3.N with `ref_gl1.so` and `ref_gl3.so`, no patch (byte order is
detected at run time, textures and lightmaps go up as GL_RGBA bytes). quakespasm, libminizip1t64
(our Yamagi's one library the image lacks) and libopenal1 (loaded at run time, only Recommended)
are in `image/deps.txt`. Recipes `quakespasm` (base, `noglsl`, `glslgamma`) and `yquake2`. Known
risk for Yamagi: gl1 uploads every texture with GL_GENERATE_MIPMAP when NPOT is exposed (it is on
nv30), the hardware mipmap path fixed for Neverball. Deploy by name: `make deploy PKGS="quakespasm
quake-shareware yamagi-quake2 quake2-demo-data"` (after `make deps`).

## Group 3: GLSL and FBO engines, the first real driver risk

The first run of each with the VRAM swap off; the GL-risky one last in its session.

| # | Game | Get | Expect | Bench |
|---|---|---|---|---|
| 8 | OpenTTD | Debian, 71 MB (97/97 tests pass on ppc64) | `-v sdl` runs (software). **`-v sdl-opengl` refuses to start** (no GPU risk: it fails at init): it needs GL 3.0 or ARB_texture_rg (absent), and its VAO check wants ARB *and* APPLE vertex array objects below GL 3.0 (an upstream bug; Mesa has no APPLE one). Its sprite shaders use `texture2DLod` in the fragment stage, which needs ARB_shader_texture_lod (nv30: off, no TXL). Fix: a debsrc patch (`\|\|`, R8 fallback, no LOD in the shader), or ARB_texture_rg in nv30 | simulation `-v null:ticks=N` (CPU only, exits after N ticks; a recipe `MODE=run`, no X), rendering for a fixed time with HUD fps |
| 9 | Sauerbraten | Debian cube2 and data (20 maps: cave, colos, ...), 266 MB; then a debsrc cube2 with a one-line patch to its `usetexcompress` check (`engine/rendergl.cpp:536`, compression off on Mesa) | GL 2.0, GLSL 1.20 and an FBO required (EXT is fine; blit and packed depth-stencil exposed). GPU skinning: dynamic indexing into bone arrays becomes if/else trees on nv30 (no indirect constant addressing), big vertex programs that may exceed 512 instructions and fall back to the CPU silently; `gpuskel 0` is its switch | fixed spawn on a fixed map, `-x"... echo (getfps) ... quit"` |
| 10 | Luanti | our debsrc build: ppc64 added to Architecture, LuaJIT kept (`libluajit-5.1-dev` exists for ppc64; Debian strips the bundled Lua, so building without LuaJIT means restoring `lib/lua` and `-DREQUIRE_LUAJIT=FALSE`); a `CHECK=1` package.conf switch so deb-build.sh drops `nocheck` and its `--run-unittests` run (under qemu: a failure may be qemu's). `luanti-data` from our build too (luanti depends on its exact version); `luanti-game-minetest` from Debian | EXT or ARB FBOs both work; bloom and MSAA off by themselves; mipmap generation (1.0) if mipmapping is on | a server mod can't count rendered frames: our `ps3bench` mod flies the fixed path and shuts down, fps from the Gallium HUD |

OpenTTD (2026-10-17, Debian 15.1): the simulation benchmark (a fixed-seed new game, 20000 ticks,
null video) runs in 13.4 s. The title screen's demo game runs with the SDL software video driver,
but SDL3's accelerated window framebuffer (its default) shows a strong blue cast on big-endian;
SDL_FRAMEBUFFER_ACCELERATION=0 (plain X11) is right, so the recipe uses that until our SDL3 fix.
That path doesn't use GL, so no HUD fps: a rendering number still needs a counter. sdl-opengl
refuses to start as expected ("Single element texture formats not supported").

## Group 4: heavy, RAM and the biggest driver risk

| # | Game | Get | Expect | Bench |
|---|---|---|---|---|
| 11 | SuperTuxKart (legacy GL 2.1) | Debian, 839 MB, deployed by name | ≥1 GB RAM per its FAQ against our 217 MB: memory settings first (`max_texture_size`, HD textures off, `geometry_level`). GL 2.1 picks the legacy path by itself; `old_driver_popup false` or a dialog blocks the run. Mipmap generation (1.0). The old powerpc colour bug (#714519) needs EXT_vertex_array_bgra, likely absent on nv30 (1.0). Its GPU timers are garbage on the RSX | `--benchmark` writes `...perf-report-<track>.csv` (steady and typical fps) and exits |
| 12 | OpenArena opengl2 | in the image (Debian ships `renderer_opengl2.so`) | ~100-115 GLSL programs at every start, any compile or link error fatal (the 13 post-process ones too, FBO or not). The likely RAM cause: bone variants index a 105-entry uniform array, which nv30 (no indirect constant addressing) turns into four 105-leaf if/else trees per program, compiled at every start though OpenArena's models never use them; ~200 shader objects stay attached (NIR and source) all session. lightall's largest variant uses all 8 varyings. FBO features off without ARB_fbo (confirmed in source). *min* first, then *default*; peak RSS in `R_Init`. Fixes to weigh: `indirect_const_addr` for nv30 vertex programs (nvfx encodes ARL already), an ioq3 patch capping bones and deleting shaders after link | the OpenArena timedemo (variants gl2min, gl2) |

## Driver work: seeded now, ranked by the smoke tests

Each is fixed only once a stage needs it.
- **ARB_texture_rg on nv30** (dsda-doom gl, OpenTTD gl): needs R8 and R8G8 render targets; the
  hardware has a B8 colour format, a G8B8 one is unverified. Two games already gain.
- **Mipmap generation into 1-pixel levels** (Neverball, SuperTuxKart, Luanti): render those levels
  linear or on the CPU (1.0 proves it first).
- **16-bit texel byte order** (L8A8, R8G8): 1.0 settles it.
- **Indirect constant addressing for nv30 vertex programs** (OpenArena opengl2, Sauerbraten): turns
  if/else trees back into indexed loads; compile RAM and the 512-instruction limit.
- **ARB_shader_texture_lod** (OpenTTD's shaders): NV40 fragment TXL; only if no game patch is simpler.
- **ARB_framebuffer_object on nv30** (renderergl2's FBO features, Neverball's effects): the risk is
  not float targets (gated separately) but one RT format shared by colour and depth, which mixed
  sizes on swizzled surfaces would mis-address.
- Mesa's disk shader cache is on for nv30 already: the second start after a Mesa deploy skips the
  GLSL front end. Load numbers compare runs with the cache in the same state.
- Silent failures count as failures: a fragment program over nv30's limits keeps the previous one
  bound ("out of temps" on stderr), a vertex program over them falls back to the CPU. Smoke tests
  grep the game's stderr for `nouveau` and watch for a sudden slowdown.
- New validator rejects and graph exceptions go into RSX.md, with a reproducer in
  `ps3-gpu-tools/mesa-tests`.

### Performance: where the CPU goes (perf, 2026-10-17, Mesa ps3.223, 15 s per game)

Every game is CPU-bound on the PPE, and most of the CPU is the driver stack, not the game:

| Game (fps) | Mesa | ps3rsx | other kernel | game |
|---|---|---|---|---|
| X-Moto (35) | 56% | 13% | 8% | 9% |
| ClassiCube (54) | 51% | 16% | 13% | 8% |
| Extreme Tux Racer, menu (40) | 38% | 9% | 8% | 11% |
| OpenArena tc1 (37; 46% unresolved, likely its renderer) | 29% | 10% | 11% | 4% |
| Armagetron (102) | 28% | 6% | 21% | 11% |

The same per-draw and per-submit costs top every profile, so they are the shared targets:
1. ps3rsx's command validation and state shadow (`ps3rsx_validate`, `ps3rsx_shadow_*`): 5-16%.
2. Fence reference counting in the nouveau winsys (`nouveau_fence_ref`): 2-11%.
3. nv30 per-draw validation (`nv30_state_validate`, `nv30_vbo_validate`, u_vbuf): 3-10%.
4. Pushbuf relocation bookkeeping (`pushbuf_krel`, `pushbuf_kref`): 2-5%.
5. A kernel hot spot perf couldn't name as a user (2-6%): profile as root next time.
Mesa's debug symbols (mesa-libgallium-dbgsym, from the dev suite) name these functions; they were
installed for the profiles and removed again: reinstall them for the next profile.

Out of scope: DXT5 (Teeworlds, TORCS; it waits for the SPU plan) and games that need GL 3. 3D
textures are supported by nv30 (sampling; rendering into them is refused).

## Sessions

| Session | Content |
|---|---|
| S1 | the runner on OpenArena and ClassiCube; group 1 smoke tests and assets (demos, replays) |
| S1b | group 1 baselines; the Armagetron soak (`BENCH_TIMEOUT` over an hour; its logs grow in /dev/shm) |
| S2 | group 2, OpenTTD's software renderer and simulation (`ps3-bench.sh run`, no X: a recipe `MODE`) |
| S3... | group 3, the GL-risky game last in each session |
| S4... | group 4 |
| After | `run.sh suite base 3` over every game that passed: the baseline the SPU plan's item 1 starts from |

Before each session the webMAN fan is on SYSCON (or at least 60%) and the user boots through
Petitboot; after it `ps3-bench.sh desktop`, and the boot flag goes back to GameOS. No deploys
during a session (ps3-bench.sh's lock). Long runs start under `nohup` or `systemd-run --user`, so
a dropped SSH connection doesn't end them. Recording assets and the games without a benchmark
(Extreme Tux Racer) need someone playing, on the desktop rather than under the bench harness.
Load times are compared with the same shader cache state (Mesa's disk cache is on for nv30).

## Results

| Game | Smoke | First failure | Bug | Baseline |
|---|---|---|---|---|
| OpenArena opengl1 | runs | under 60 fps | performance | tc1: 42.2 fps, load 10.8 s (stage 0) |
| ClassiCube | runs | under 60 fps | performance | fly-through demo (plugin, 3000 fixed frames): 43.2 fps, frame ms 9/23/53/278 (min/avg/p99/max), 0 chunks built in the run |
| dsda-doom, software renderer 960x540 | runs, colours right (our dsda-doom package: the frame now uses the SDL texture's own pixel format; the old one swapped red and green on big-endian) | CPU-bound: sound can stutter | performance | 20 fps HUD median (cut at 90 s) |
| dsda-doom, GL renderer 1920x1080 | runs (our package: indexed textures as GL_LUMINANCE_ALPHA without ARB_texture_rg) | | performance | 48.2 fps, Freedoom DEMO1 timedemo complete |
| X-Moto | runs (Mesa ps3.186) | 35 fps, CPU-bound (one PPE thread flat out); no sound bug: the engine and effects play in replays (checked with the PS3's own binaries under qemu), but the engine is quiet and bass-heavy, ~15-20 dB under the music | performance; sound effects | 35.0 fps avg (credits replay, `--timedemo --benchmark`) |
| Neverball | runs (Mesa ps3.225): picture, fonts, overlays and sound right | the shipped replay is shorter than the 60 s run, so the HUD mixes replay and end menu | a patch printing fps and exiting at the replay's end | ~70 fps HUD median (replay + menu) |
| Extreme Tux Racer | runs (menus) | | | 42 fps HUD median in the menu; a race needs a player |
| Armagetron | runs: a recorded round of 4 AIs, spectated (assets/armagetronad-bench.aarec, 90 s), played back with --benchmark | the recording ends without a quit, so playback never prints its fps: fixed 100 s, HUD fps | | 89 fps HUD median live (p10 47); playback being measured |

## Driver bugs

| Bug | Games blocked | Fix |
|---|---|---|
| L8A8 (GL_LUMINANCE_ALPHA) samples with L and A swapped (gl-texfmt) | Neverball, fonts in many games | Mesa 0012 (ps3.223), gl-texfmt passes |
| GPU mipmap generation of non-square textures: 1-pixel levels are swizzled render targets the validator refuses, the push is lost (gl-genmip) | Neverball, SuperTuxKart, Luanti | Mesa 0013 (ps3.223), gl-genmip passes |
| Graph exception 0x105: nv30's 3D blit sampled a mip level at an offset not 128-byte aligned (Mesa's CPU mipmap generation reading back the 2x2 level of a 32x32 L8A8 texture) | Neverball (every game generating mipmaps for L8A8/L8/A8/I8/4444/5551) | Mesa 0014 (ps3.225); gl-mipread passes all 70 cases. A ps3rsx validator rule refusing unaligned texture offsets is proposed (not done: the 128-byte threshold is inferred) |
| No ARB_texture_rg (glxinfo) | dsda-doom GL, OpenTTD GL | dsda-doom: a GL_LUMINANCE_ALPHA fallback in our dsda-doom package (works); OpenTTD: open |
| Every X after the first started black: ps3rsx's reflip muted the video for an active CRTC with no framebuffer, and ps3fb's console blank muted it before X owned the GPU | all (every bench run) | a1f1b91, 8baa747 (ps3rsx 0.219) |
| No window manager under the bench's bare X: SDL fullscreen windows never shown | all SDL games | client.sh starts Openbox (fa723f9) |
