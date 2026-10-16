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
  the results table.

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
  ClassiCube (a saved map, 60 s, HUD fps; variant vsync).
- The `ps3-games` metapackage: OpenArena and ClassiCube to start with; ps3-desktop recommends it.
- `ps3-bench.sh` stays as it is until the recipe has validated (step0.sh and step0-report.py
  read its OpenArena greps, its q3config.cfg backup and its 12 s HUD skip); then those move out.
- Games run directly by default, as in step 0; `GAMEMODE=1` in a recipe starts them through
  `ps3-game` (its slice and game mode change memory and timing, so it is a variant to measure).
- Accepted when the OpenArena recipe matches `step0.sh tex` run interleaved in the same session
  (same packages: each run records its versions) and ClassiCube gives HUD numbers.

## Group 1: should run as packaged (Debian main, GL 1.x fixed function)

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

| # | Game | Get | Expect | Bench |
|---|---|---|---|---|
| 6 | QuakeSpasm | Debian, 2 MB; `quake-shareware` from game-data-packager (contrib, built in the Debian games builder with `lhasa`: `make quake-data` into `build/debs/quake-data`; non-free like ClassiCube's audio, so our LAN repository only; published once and reused, since its output isn't reproducible). Not the `quake` launcher package (it pulls GTK 4) | `-nopackedpixels` (the upstream fix disables 10-bit lightmaps on big-endian; nv30 has no 10-bit format anyway). Packed pixels only exist with GLSL, so `-noglsl` first, then GLSL with `gamma 0.9` (the gamma pass only runs off 1.0) | `+timedemo demo1`, `%i frames %5.1f seconds %5.1f fps`, doesn't quit: stopped at that line |
| 7 | Yamagi Quake II (gl1) | our debsrc build of Debian's **`yquake2`** 8.70 (contrib: debian-ports doesn't build it; the deb builder's sources need `contrib` for `apt-get source`; its build-deps, SDL3 included, are in main); `quake2-demo-data` from game-data-packager | gl1 is the default renderer (gl3/gles3 need GL 3); upstream tests PPC64 | `+timedemo 1 +demomap q2demo1.dm2` (the demo pak's only demo), `%i frames, %3.1f seconds: %3.1f fps`, returns to the console: stopped at that line |

## Group 3: GLSL and FBO engines, the first real driver risk

The first run of each with the VRAM swap off; the GL-risky one last in its session.

| # | Game | Get | Expect | Bench |
|---|---|---|---|---|
| 8 | OpenTTD | Debian, 71 MB (97/97 tests pass on ppc64) | `-v sdl` runs (software). **`-v sdl-opengl` refuses to start** (no GPU risk: it fails at init): it needs GL 3.0 or ARB_texture_rg (absent), and its VAO check wants ARB *and* APPLE vertex array objects below GL 3.0 (an upstream bug; Mesa has no APPLE one). Its sprite shaders use `texture2DLod` in the fragment stage, which needs ARB_shader_texture_lod (nv30: off, no TXL). Fix: a debsrc patch (`\|\|`, R8 fallback, no LOD in the shader), or ARB_texture_rg in nv30 | simulation `-v null:ticks=N` (CPU only, exits after N ticks; a recipe `MODE=run`, no X), rendering for a fixed time with HUD fps |
| 9 | Sauerbraten | Debian cube2 and data (20 maps: cave, colos, ...), 266 MB; then a debsrc cube2 with a one-line patch to its `usetexcompress` check (`engine/rendergl.cpp:536`, compression off on Mesa) | GL 2.0, GLSL 1.20 and an FBO required (EXT is fine; blit and packed depth-stencil exposed). GPU skinning: dynamic indexing into bone arrays becomes if/else trees on nv30 (no indirect constant addressing), big vertex programs that may exceed 512 instructions and fall back to the CPU silently; `gpuskel 0` is its switch | fixed spawn on a fixed map, `-x"... echo (getfps) ... quit"` |
| 10 | Luanti | our debsrc build: ppc64 added to Architecture, LuaJIT kept (`libluajit-5.1-dev` exists for ppc64; Debian strips the bundled Lua, so building without LuaJIT means restoring `lib/lua` and `-DREQUIRE_LUAJIT=FALSE`); a `CHECK=1` package.conf switch so deb-build.sh drops `nocheck` and its `--run-unittests` run (under qemu: a failure may be qemu's). `luanti-data` from our build too (luanti depends on its exact version); `luanti-game-minetest` from Debian | EXT or ARB FBOs both work; bloom and MSAA off by themselves; mipmap generation (1.0) if mipmapping is on | a server mod can't count rendered frames: our `ps3bench` mod flies the fixed path and shuts down, fps from the Gallium HUD |

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
| OpenArena opengl1 | runs (step 0) | | | |
| ClassiCube | runs | | | |

## Driver bugs

| Bug | Games blocked | Fix |
|---|---|---|
