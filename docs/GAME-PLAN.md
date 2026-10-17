# Game support plan: one stage per game

## Status (end of the game stage, 2026-10-17)

The baseline is done: every game on the list has run on the PS3 except SuperTuxKart (a big-endian
fix in progress). Most are playable but not great. Four reach ~60 fps (QuakeSpasm, Neverball,
Armagetron, close: Yamagi), most others sit at 35-50 and are CPU-bound in the driver stack, and
four (Luanti, Sauerbraten, OpenArena opengl2, likely SuperTuxKart) render smoothly between stalls
of seconds because memory runs out. Getting here took six big-endian bugs in games and libraries
and five nv30/ps3rsx driver bugs, all fixed; no open GPU bug blocks a game. The first round of
driver speedups (Mesa ps3.246, ps3rsx 0.249) gave +2 to +9%.

**Update, 2026-10-17 evening** (the table below is current; the rounds behind it are under
"Measured"): Mesa ps3.262 to ps3.299 gave +5 to +24% in most games and, through 0027's adaptive
submission, took Armagetron back to 61.5. Batching the GL calls in the games themselves did far more:
our X-Moto runs its replay at 166.5 fps instead of 44.5, Extreme Tux Racer at 59 instead of 52.
OpenArena opengl2 with our ioquake3 memory patches: 26-32 fps, longest stall 6 s instead of 24.7.
SuperTuxKart now loads (karts render, the track doesn't: out of VRAM). dsda-doom's GL renderer and
ClassiCube got the same batching late in the day (built, not run on the PS3: dsda-doom's gl
timedemo ~1960 -> ~390 GL calls and ~205 -> ~44 draws a frame, ClassiCube's path 5127 -> 1524
calls and 1107 -> 294 draws, both counted on llvmpipe). SuperTuxKart ps3.306: VRAM 210 -> 132 MB
(98 MB with nohd or lean), HUD 13-19 fps, still "out of VRAM" lines, picture not yet confirmed.

Every number is 1920x1080, measured through games/bench with the picture checked on the webcam.

| Game | Runs | Picture | Sound | Demo (repeatable) | fps | Against ~60 | Left to do |
|---|---|---|---|---|---|---|---|
| QuakeSpasm | yes | right | right | timedemo demo1 (our quake.rc) | 78.6 (73-84, Mesa ps3.262) | **meets it** | - |
| Armagetron | yes | right | right | recorded AI round, `--playback --benchmark` | 61.5 HUD (Mesa ps3.299; 60 on ps3.246, 50-53 on ps3.262/282) | **meets it** | - (ps3.262's loss was the missing CPU/GPU overlap without relocations; 0027 fixed it) |
| Neverball | yes (ps3rsx 0.249: no wedge) | right | 15-36 underruns a run | shipped replay (only ~20 s); our NEVERBALL_BENCH prints fps and quits at its end | 70-76 (its own count); 320-380 ms stalls at the start | **meets it** | a longer replay of the user's (the start-up stutters weigh too much in 20 s) |
| Yamagi Quake II | yes | right | right (our fix) | timedemo q2demo1 | 63.9 (ps3.262) | **meets it** | - |
| dsda-doom (GL renderer) | yes | right (our fixes) | **choppy** (MIDI synthesis) | Freedoom DEMO1 timedemo | 48.8 | under | game-side batching (our dsda-doom 0005, built, to run); fluidsynth on the SPEs: v1 cut its CPU (49 -> 32 s a minute) but 1097 underruns remain, v2 built; music default |
| Extreme Tux Racer | yes | right | right | the user's race, replayed (evemu) | 59 HUD median (our batching, ps3.324; 52 without, Mesa ps3.299) | close | performance |
| ClassiCube | yes | right | right | fly-through plugin, 3000 fixed frames | 50.1, load 11.6 s (ps3.262); 46.5 on a later boot (ps3.299, ps3.282 46.9 on that boot) | under | game-side batching (our classicube 0001-0002, built, to run) |
| OpenArena (opengl1) | yes | right | right | timedemo bench | 52.4 (ps3.299); 54.4, load 8.2 s (ps3.262) | close | our ioquake3 0011-0018 (engine and renderer CPU) built, to run; ps3.362 adds 0019-0022 batching (host: 3022 -> 377 calls, 198 -> 79 draws a frame), to run |
| X-Moto | yes | right | right (engine quiet under the music) | credits replay | 166.5 HUD (our batching, ps3.323; 44.5 without, Mesa ps3.299) | **meets it** | - |
| dsda-doom (software) | yes | right (our fix) | choppy | as GL | 38, the timedemo now finishes (ps3.262) | under | the GL renderer is the way |
| OpenTTD | yes (software video) | right | choppy (~1000-1170 underruns a minute, either path) | title-screen demo game; simulation 20000 ticks in 13.4 s | 8 (SDL3 ps3.254 accel, HUD median; ps3.242 was ~2), 12 with the 8bpp blitter | far under | its own software drawing: our OpenGL driver patches (GL 2.1 without ARB_texture_rg) built, recipe variant `gl`, to run |
| Luanti | yes (first run 2026-10-17) | right (the user) | no underruns | our ps3bench mod flies a fixed path | 67 HUD median in flight, p10 23, stalls to 0 while chunks load | median meets it, stalls don't | memory (177 MB swapped out, 32 MB free): 20 recipe variants built (view range, block unloading, mesh delay, heap, lean), to run; package defaults once measured |
| Sauerbraten | yes, CPU skinning (2026-10-17) | right; "surprisingly smooth" between stalls (the user) | 236 underruns | CubeScript counts frames on map cave (not reached yet: loading takes over 60 s) | 15 HUD median incl. loading | under (memory) | OOM-killed without the VRAM swap (smoke's default); with it, 138 MB VRAM and the swap full: memory settings. GPU skinning runs (no wedge, 2026-10-17) but at 11 fps by its own counter and stutters badly: CPU skinning (`gpuskel 0`, 28) is the default in our cube2 (+ps3.272, with memory patches and the SPU skinning hook; to run). The hud gun: `-l` spawns with the pistol, which the data has no model for (variant `gun`) |
| SuperTuxKart | yes with our +ps3.264 (2026-10-17): karts render, **no track** | track missing | | profile mode: AI drives `--profile-time=60` on lighthouse, prints Average FPS | | under (memory) | VRAM exhausted (210 failed buffer allocations), minimap FBO incomplete, SIGSEGV at exit: fixes 0003-0004 (no crash, power-of-two minimap, opaque textures as DXT1) and memory variants (`lean`) built, to run |
| OpenArena (opengl2) | yes, gl2min (2026-10-17) | right (webcam) | right | timedemo bench (97 GLSL programs compiled twice: ~22-30 s each) | our ioquake3 +ps3.285: gl2min 26.1-31.8, longest frame 6 s (14.8 and a 24.7 s stall with the patches off), cpuanim 30.5 at 41 MB VRAM; gl2 (Debian's settings) 15.9 (10.7 on Debian's build) | under (memory) | 112-138 MB VRAM, 12-14 MB MemAvailable at the lowest: memory |

### Done (2026-10-16/17)

- **Stage 0:** the games/bench runner (recipes, a bare Openbox, config backups on disk, 90 s cap,
  ALSA underrun counting, report.py across games and variants), `ps3-games` metapackage started.
- **Display:** every X after the first started black (ps3rsx muted the video: fixed, a1f1b91);
  SDL fullscreen needs a window manager. Step 0's game numbers were taken with nothing on screen
  and are void.
- **Driver (Mesa nv30, ps3rsx):** luminance-alpha textures sampled with L and A swapped (0012);
  GPU mipmap generation broke on non-square textures (0013); a mip-level readback at an unaligned
  offset wedged the RSX (0014, Neverball; ps3rsx 0.249 now refuses such offsets); per-draw fence
  and kref work (0015-0017: X-Moto +2 fps, ClassiCube +1.7). ps3rsx 0.249: validator fast path,
  cheaper shadow and channel switches, and a multi-push validation bug fixed.
- **Games and libraries, all big-endian bugs:** dsda-doom (colours, its own sound lumps, GL renderer
  without GL_RG, screenshots), Yamagi Quake II (OpenAL white noise), SDL3 (window framebuffer
  colours), LuaJIT (state allocation failed ~40% of the time). X-Moto's "missing" engine sound was
  only quiet.
- **Demos for every game in the suite** (above), each under 90 s.
- **Packages:** our debsrc builds of dsda-doom, yquake2, luanti, luajit, cube2, libsdl3; Quake data
  debs (published once to the stable suite, the user's go-ahead); Debian games in image/deps.txt.

### Next phase: from playable to good

Three levers, in this order. Each change gets a recipe variant, so its gain is measured.

1. **Memory, in the games themselves.** Trimming the base system (task #15) would win back maybe
   20-30% of what it uses: still tight. The games hold far more, and much of it can go:
   - OpenArena opengl2: ~100-115 GLSL programs kept with their source and compiled IR all session
     (delete shaders after linking, an ioquake3 patch; compile only the variants the maps use);
     160 MB of VRAM.
   - Sauerbraten: 140 MB of VRAM; `texreduce`/`maxtexsize`; the shader models it never draws.
     Ship `gpuskel 0` (task #32).
   - Luanti: view range, mesh threads, map cache (our build, so its defaults can change).
   - SuperTuxKart: its texture and geometry settings, once it loads.
   - Why texture memory is RAM too: the VRAM swap reserves 96 of the 245 MB of VRAM, so a game
     wanting 160 MB of VRAM spills into GART (system RAM), which then swaps. The size of that
     reserve is a trade-off to measure. (Correction, 2026-10-17, from ps3rsx/src/mem.c: a buffer
     that finds no VRAM first makes the VRAM swap hand chunks back, `ps3rsx_vswap_shrink`, whose
     pages go to the USB disk; only if that fails does it land in GART. So a big game's VRAM costs
     disk swap traffic first. Which of the two OpenArena opengl2 hit is not measured.)
2. **Trade stored data for compute where there is headroom** (the user, 2026-10-17). Six SPEs sit
   idle in every game, and the RSX has headroom (the games are PPE-bound):
   | Stored today | Instead | On | Saving |
   |---|---|---|---|
   | RGBA8 textures (4 B/pixel) | DXT1/DXT5 (0.5-1 B/pixel), decoded by the RSX | SPU encoder (DXT1 in libspeq; DXT5 to add, which also unblocks Teeworlds and TORCS) | 4-8x texture memory; one Mesa hook for every game (an opt-in driconf switch: lossy) |
   | mip chains from the CPU | generated on the GPU | RSX (works since Mesa 0013) | a third per texture |
   | decoded 16-bit PCM samples | compressed, decoded when played or streamed | SPU (Vorbis) | ~10x on sound |
   | float32 vertex data | half floats or 16-bit, fetched natively by nv30 | RSX | ~2x vertex buffers |
   | shader IR kept by Mesa | dropped after linking, rebuilt from the disk cache when needed | PPE, rarely | tens of MB (OpenArena opengl2) |
   | Luanti's meshes of far blocks | re-meshed when back in view | SPU | most of its mesh memory |
3. **CPU: driver and SPU offload.** The driver stack still takes 30-55% of the CPU. Mesa nv30's
   per-draw work is being cut (agent: cache invalidates on every draw, relocations emitted twice,
   u_vbuf always on). Then the SPU plan (SPU-OFFLOAD-PLAN.md), starting with fluidsynth (3b:
   dsda-doom's chopping music, ~40% of a PPE thread). OpenTTD's GL renderer needs ARB_texture_rg.
   (2026-10-17 evening: the driver rounds are done for now, 0015-0031; fewer GL calls in the games
   pays more, X-Moto 3.7x and Extreme Tux Racer +13%, so CPU-bound games get that first: dsda-doom
   GL and ClassiCube in progress. OpenTTD's GL renderer is patched in the game instead. fluidsynth
   v1 measured, v2 built.)

Open from the stage: SuperTuxKart's big-endian mesh loader (agent; done 2026-10-17: it loads),
Sauerbraten's missing hud gun (explained: the pistol has no model), a longer Neverball replay (the user plays ~60 s), Yamagi's 0.249 number rechecked, ps3fb's dump
(task #29), deb-build.sh's hidden build-dep failure (task #31).

### Decisions for the user

- dsda-doom's music on the PS3 until SPU synthesis exists: OPL (cheaper, still some chopping) or off.
- Which proven packages go to the stable suite, and when (rule: only once proven on the PS3; ask
  first): candidates yamagi-quake2 +ps3.240, dsda-doom +ps3.247.

---

## About this plan

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
our Mesa (reviews of this plan, 2026-10-16), before the first runs; outcomes are in the status
table at the top and the driver bugs table.

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

Packages: quake-shareware and quake2-demo-data (`make quake-data`, game-data-packager; published
once to the stable suite), our yquake2 (`yquake2/`, contrib source), Debian's quakespasm.

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

### Performance work done (2026-10-17)

- Mesa ps3.246 (patches 0015-0017): no fence reference work when the fence is unchanged, one
  fence read per validate, no duplicate krefs in the pushbuf validation. Same command stream.
  gl-texsub/texfmt/genmip/mipread pass. X-Moto 35 -> 37 fps (HUD median; underruns 208 -> 126),
  ClassiCube's demo 43.2 -> 44.9 fps.
- ps3rsx 0.249 (9e2a3b9): a fast-path table in the validator (one lookup per word; inline data
  ~13x cheaper on the host), the shadow written per burst, dirty bitmaps so a channel switch
  restores and takes only what changed (~14.6 -> 1.3 us on the host), and a pre-existing bug
  fixed: a rejected push in a multi-push submit left the earlier pushes' object bindings in the
  validator's state (the 0x109 kind of wedge). Not yet run on the PS3: it needs a reboot, then the
  GL tests, X with a GL client, VT switches, Neverball, the suite with perf. (Run 2026-10-17
  morning: GL tests pass, VT switches with a GL client, Neverball without the wedge.)
- Mesa ps3.262 (patches 0019-0025; measured below, "Measured: Mesa ps3.262"): fewer words per draw
  for ps3rsx to copy and validate. 0019 no relocations on the PS3: the "second emission" of every
  relocated method was two zero words (NOPs the kernel would patch only if a buffer moved; ps3rsx
  never does) plus two relocation records, and the 1024-relocation limit forced a submission about
  every hundred draws. 0020 texture cache invalidate (10 words) only after a texture copy/upload,
  a framebuffer change or a flush, not before every draw; `NV30_TEX_INVALIDATE_ALWAYS=1` restores
  the old behaviour. 0021 no duplicate 0x1710. 0022 no identical VTXFMT rewrites when only buffers
  change. 0023 no constant-buffer allocation per constant update. 0024 no rewrite of texture units
  whose view and sampler stayed (and all units rewritten after a context switch, an old bug).
  0025 changed VP constants in consecutive slots uploaded in one burst. Estimate: a typical GL 1.x
  draw goes from ~40-75 words to ~15-50. Test: gl-texsub, gl-texfmt, gl-genmip, gl-mipread; then
  X-Moto, ClassiCube, Extreme Tux Racer, OpenArena, Neverball (render-to-texture/minimap-like
  updates, font glyph uploads); stale texels point at 0020 (retry with the env variable), wrong
  or exploded geometry at 0022/0025, wrong texture or filter on a surface at 0024.
- Leads (done since in ps3.299, patches 0028, 0030, 0031): client-memory index arrays (OpenArena's glDrawElements) are pushed inline word
  by word; GL_UNSIGNED_BYTE indices from a VBO are read back by the CPU (u_vbuf doesn't rewrite
  them since nv30 claims R8 index support); a fragment-program constant change re-uploads and
  migrates the whole program (GLSL games). Bug seen in passing: nv30_draw_elements' index buffer
  reference is added after validation and reset before the next one, so the index buffer is never
  in the kernel's buffer list nor fenced (an in-place index update can race the GPU).

### Built 2026-10-17 (measured since where noted)

- Mesa ps3.282 (measured: the Armagetron and kick100 rows below): a run-time switch per patch (NV30_PS3_RELOCS, NV30_VTX_INVALIDATE_DRAW,
  NV30_VTXFMT_ALWAYS, NV30_CONSTBUF_ALLOC, NV30_TEXUNIT_ALWAYS, NV30_VP_CONST_SINGLE, plus
  NV30_TEX_INVALIDATE_ALWAYS) and 0026 NV30_PS3_KICK_DRAWS=N. Likely cause of Armagetron's loss:
  0019 dropped the reloc-limit submissions, so a frame reaches the GPU only at swap (no CPU/GPU
  overlap).
- Mesa ps3.299 (patches 0027-0031; measured below): 0027 NV30_PS3_KICK_AUTO, an adaptive submission rule
  instead of a fixed draw count (kick100 on ps3.282: Armagetron +21%, OpenArena +3%,
  ClassiCube +1%, X-Moto -9%): submit at a draw start once the GPU has passed the last fence
  and 16 KB and 1/4 of the last frame are queued (=0: off; NV30_PS3_KICK_MIN_BYTES,
  NV30_PS3_KICK_SPLIT tune it; NV30_PS3_KICK_DRAWS=N overrides). 0028 index buffer fenced
  (bug; NV30_IDXBUF_LATE_REF=1). 0029 GPU state forgotten when the context that drew last is
  gone (bug; NV30_CTX_STATE_KEEP=1). 0030 client-memory and 8-bit index arrays (32+) uploaded
  to GART and fetched by the GPU (NV30_INDEX_UPLOAD=0: inline). 0031 fragment program
  constant changes run from a GART copy, no M2MF re-upload (NV30_FP_CONST_VRAM=1; first time
  the RSX fetches a fragment program from GART).
- OpenArena opengl1: our ioquake3 +ps3.362 (in ps3-dev), patches 0019-0022, each behind a cvar:
  lightmap atlas (r_mergeLightmaps; world batches 114 -> 27), GL state cache (r_glStateCache), no
  compiled vertex array locks (r_lockArrays 0), sky from vertex arrays (r_skyArrays; islanddm's sky
  1453 -> 18 calls). Host, llvmpipe at GL 2.1: 3022 -> 377 calls, 198 -> 79 draws a frame on the
  bench demo; delta, oa_dm6, oa_bases3, islanddm similar; pixels equal within 1-4 levels, and with
  every switch off identical to the old build. Recipe variants mergelm0, glcache0, lockarrays,
  skystrips, allbatchoff. To run on the PS3: tc1 against allbatchoff, same boot.
- SuperTuxKart (run: karts render, the track doesn't, out of VRAM; fixes 0003-0004 built since):
  our package 1.5-2+ps3.264 (.spm meshes and armatures byte-swapped, GL textures
  and vertex colours right on big-endian; upstream #5578 closed as not planned). All 1268 .spm
  files parse identically on amd64 and ppc64.
- OpenArena opengl2 (measured below): our ioquake3 +ps3.285, patches 0001-0010. The models, not the shaders, were
  most of the memory: MD3 frames kept at 28 B/vertex on the hunk and again in VBOs, all three LODs,
  a 21 MB VAO cache never drawn from; plus 27 bone and 15-33 FBO-only programs that can't be used.
  Estimate 161 -> ~115 MB VRAM and ~60 MB less RSS; gl2min_* variants isolate each patch.
- Sauerbraten: our cube2 +ps3.272, patches 0002-0005 (CPU skinning default, only drawable shader
  variants, the sky from its 256 px .dds instead of six 2048 px PNGs decoded twice: the likely
  load-time OOM; lightmap CPU copies freed). The missing hud gun: `-l` spawns with the pistol,
  which the BananaBread data has no model for (variant `gun`).
- Luanti: 20 recipe variants (chunksize, caches, malloc, mesh merging), memory logged by the
  ps3bench mod every 5 s; package defaults only once measured.
- Later the same day, also waiting: ioquake3 0011-0018 (OpenArena opengl1's CPU: shader lookups,
  pk3 reads through libdeflate without crc32, AltiVec, sounds loaded when played; a recipe
  variant per patch); OpenTTD 15.1-1 with its OpenGL driver on GL 2.1 (checked on llvmpipe forced
  to GL 2.1; variant `gl`); cube2 0006, Sauerbraten's skinning through libspeq on the SPEs
  (variants spuskin, ppeskin, offskin; ps3-spu/doc/skin.md); fluidsynth's second SPU version
  (SPU-AUDIO.md 5(e)); game mode's memory work (DESKTOP-PLAN.md "Game mode": the PCManFM desktop
  and wallpaper unloaded, services' heaps to swap, page cache dropped, every menu game through
  ps3-game; `games/bench/gamemode-check.sh`).

### Measured: Mesa ps3.282 and ps3.299, ioquake3 ps3.285, game-side batching (2026-10-17)

ps3rsx 0.249; two runs per cell unless noted; full observations in games/bench/results/fps.csv.

| Game | Before | After | What |
|---|---|---|---|
| Armagetron (HUD) | 50 (ps3.282 base) | 60.5 (relocs, kick50/100); 61.5 (ps3.299) | 0019's missing CPU/GPU overlap; 0027 submits once the GPU caught up |
| OpenArena tc1 | 51.3 (ps3.282) | 53.0 (kick100); 52.4 (ps3.299) | +3%, noise-sized |
| ClassiCube | 48.3 (ps3.282) | 48.9 (kick100, +9% CPU); 46.5 (ps3.299) against 46.9 (ps3.282) on the same boot | neutral |
| X-Moto (HUD) | 47.0 (ps3.282, same boot) | 44.5 (ps3.299) | **-5%**: neither 0027 nor 0030 off restores it; 0031 off (fpvram) and 0028 off (idxlate) 46 each, about a frame each |
| OpenArena opengl2 gl2min | 14.8, a 24.7 s stall, 103 s CPU (patches off) | 26.1-31.8, 6.0 s, 55 s CPU | 0007 (no CPU copy of MD3 vertices) is most of it (17.6 without it); lazy compile adds stalls |
| X-Moto (HUD) | 44.5 (nobatch) | **166.5** | our xmoto: 18,500 -> 165 GL calls, 1,960 -> 40 draws a frame; picture identical |
| Extreme Tux Racer (HUD) | 52 (gloff), 83.6 s CPU | 59, 53 s CPU | our etr: 19,400 -> 850 calls, 697 -> 72 draws a frame |
| dsda-doom play (fluidsynth) | 49.3 s CPU, 1335 underruns (off) | 31.7 s, 1097 (SPEs, v1) | the DSP moved (20227 of 20295 blocks), the chopping stayed; cause not established |

0031 was checked on the webcam with OpenArena opengl2 (the first time the RSX fetches fragment
programs from GART): right picture, no wedge. GL tests pass on ps3.299. The kick100 rule cost
X-Moto 9% on ps3.282, so a fixed draw count was no default; 0027's adaptive rule replaced it.

### Measured: Mesa ps3.262 (2026-10-17, ps3rsx 0.249, one suite round, repeats where noted)

| Game | ps3.246 | ps3.262 | Change |
|---|---|---|---|
| OpenArena tc1 | 44.2 | 54.4 | +23% |
| Yamagi Quake II | 51.4 (56 the day before) | 63.9 | +14-24% |
| X-Moto (HUD) | 39 | 47 | +21% |
| Extreme Tux Racer (HUD) | 46 | 53 | +15% |
| dsda-doom software (HUD) | 33, demo unfinished | 38, finished | +15% |
| ClassiCube | 47.6 | 50.1 | +5% |
| QuakeSpasm | 70.5 | 78.6 (73, 84) | +11% |
| Neverball | 78.2 | 75.9 (74, 78) | noise |
| Armagetron (HUD) | 60 (59, 61, A/B same boot) | 52.5 (53, 53, 52, 51) | **-13%**, not 0020 (NV30_TEX_INVALIDATE_ALWAYS=1: 52) |

gl-texsub/texfmt/genmip/mipread pass, no validator rejects, glxgears 1250 -> 1360. The Mesa
agent's estimate ("a few percent, up to 10%") was low for the PPE-bound games.

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

Before each session the webMAN fan is on SYSCON (or at least 60%) and the user boots through
Petitboot; after it `ps3-bench.sh desktop`, and the boot flag goes back to GameOS. No deploys
during a session (ps3-bench.sh's lock). Long runs start under `nohup` or `systemd-run --user`, so
a dropped SSH connection doesn't end them. Recording assets and the games without a benchmark
(Extreme Tux Racer) need someone playing, on the desktop rather than under the bench harness.
Load times are compared with the same shader cache state (Mesa's disk cache is on for nv30).

## Driver bugs

| Bug | Games blocked | Fix |
|---|---|---|
| L8A8 (GL_LUMINANCE_ALPHA) samples with L and A swapped (gl-texfmt) | Neverball, fonts in many games | Mesa 0012 (ps3.223), gl-texfmt passes |
| GPU mipmap generation of non-square textures: 1-pixel levels are swizzled render targets the validator refuses, the push is lost (gl-genmip) | Neverball, SuperTuxKart, Luanti | Mesa 0013 (ps3.223), gl-genmip passes |
| Graph exception 0x105: nv30's 3D blit sampled a mip level at an offset not 128-byte aligned (Mesa's CPU mipmap generation reading back the 2x2 level of a 32x32 L8A8 texture) | Neverball (every game generating mipmaps for L8A8/L8/A8/I8/4444/5551) | Mesa 0014 (ps3.225); gl-mipread passes all 70 cases. ps3rsx 0.249 refuses texture offsets not 128-byte aligned (8e99b76; loaded since the 2026-10-17 boot) |
| SDL3's accelerated window framebuffer on big-endian was BGRA8888, so apps writing 0xAARRGGBB words (OpenTTD) showed a blue cast | OpenTTD (any SDL window-surface app) | our libsdl3 +ps3.242 (colours right, the user confirmed); but that path converted every frame on the CPU twice (SDL's swizzle, then Mesa's): OpenTTD ~2 fps by the HUD, audio starved. ps3.254 (patch 0002) uploads ARGB/XRGB8888 natively with GL_BGRA + 8_8_8_8_REV as SDL2 did (upload 2.3x faster under qemu, colours exact on opengl and opengles2); on the PS3 (2026-10-17) OpenTTD went from ~2 to 8 fps with it |
| Debian's LuaJIT on big-endian ppc64 allocates between 2 and 4 GB, which it then refuses: luaL_newstate failed ~40% of the time | Luanti | our luajit package (331c539); Luanti's 330 unit tests pass |
| No ARB_texture_rg (glxinfo) | dsda-doom GL, OpenTTD GL | dsda-doom: a GL_LUMINANCE_ALPHA fallback in our dsda-doom package (works); OpenTTD: GL_LUMINANCE8 in our openttd package (built, to run) |
| Every X after the first started black: ps3rsx's reflip muted the video for an active CRTC with no framebuffer, and ps3fb's console blank muted it before X owned the GPU | all (every bench run) | a1f1b91, 8baa747 (ps3rsx 0.219) |
| No window manager under the bench's bare X: SDL fullscreen windows never shown | all SDL games | client.sh starts Openbox (fa723f9) |
