# Smoke-tested games

Games outside the benchmark suite get one launch each to see whether they start and play: no
numbers kept, no repeats, not part of `run.sh suite` (their recipes set `SUITE=0`). Decided with the
user on 2026-10-17. Of the first eleven, all eight that start without someone at the controller run
(Teeworlds, SuperTux, Red Eclipse, Frozen Bubble, Chocolate, Crispy and Woof Doom, Pingus). The three
menu-only ones and Nexuiz wait for the user; nine more games got recipes on 2026-10-19, seven of
them had a first run that night (below). Red Eclipse is in [sauerbraten.md](sauerbraten.md), the Doom ports in
[doom.md](doom.md).

## How a smoke run works

A smoke recipe sources `games/bench/recipes/smoke.inc`: `SUITE=0`, a 120 s `DURATION` that
`SMOKE_SECS=N` overrides (`run.sh smoke` caps a run at 360 s; `BENCH_TIMEOUT=N` raises that), a short
HUD window for the report only (10 s after the load, then 90 s), and `vblank_mode=0`. setup() writes
the game's config (restored after the run) and starts it in the closest thing to play its command
line allows. `smoke_keys` types keys into a virtual keyboard (evemu) once the game's window is up,
`smoke_xkeys` through XTEST (`games/bench/xkeys.py`) where SDL ignored the virtual keyboard.
`run.sh smoke` runs with the VRAM swap off unless `NOVSWAP=0`, and prints the result, the picture
check and the game's and kernel's errors.

Every game here was in Debian ports ppc64 main at the version of our 20261013 snapshot (Crispy Doom
live: 7.2-1), and the PS3's apt reads live ports, so no `make deps` is needed; names go into
`image/deps.txt` only for the games that stay. Before the session (the desktop may be up; about
1.5 GB download, 2 GB installed for the first set; `df -h /` first, and redeclipse-data and torcs-data
last if space is short):

    sudo apt install teeworlds teeworlds-server supertux trigger-rally chromium-bsu frozen-bubble pingus
    sudo apt install --no-install-recommends chocolate-doom crispy-doom woof-doom freedoom
    sudo apt install torcs redeclipse
    make bench-push        # on the PC: games/bench to the PS3

Teeworlds needs our packages first: `make deploy PKGS="teeworlds teeworlds-server"` from the PC (it
puts the local builds in ps3-dev and installs them with `-t ps3-dev`), then `dpkg -l teeworlds
teeworlds-server | grep ps3` must show `0.7.5-3+ps3.422`. Then, as tibi on the PS3, the safe order
(software renderers first, the heaviest last: a graph exception ends the session):

    cd /var/tmp/bench
    ./run.sh smoke chocolate-doom;  ./run.sh smoke crispy-doom;  ./run.sh smoke woof
    ./run.sh smoke frozen-bubble;   ./run.sh smoke chromium-bsu; ./run.sh smoke pingus
    ./run.sh smoke supertux;        ./run.sh smoke teeworlds;    ./run.sh smoke trigger-rally
    ./run.sh smoke torcs;           ./run.sh smoke redeclipse

Menu games need time at the controller: `SMOKE_SECS=300 BENCH_TIMEOUT=420 ./run.sh smoke torcs`.
End the session with `ps3-bench.sh desktop`.

## Run on the PS3

**Teeworlds 0.7.5** (our `teeworlds`, `teeworlds-server` `0.7.5-3+ps3.N`, package `teeworlds/`;
Debian's `teeworlds-data`). Runs: in game on dm1, 183 fps (2026-10-18, row 62). The recipe starts a
`teeworlds-server` on the PS3 (map dm1, no master server registration, 4 slots) and the SDL2 client
connects at 1920x1080; there is no single player and no bots, so the player runs around alone.
Variants `notc`, `menu`, `window`, `s3tcppe`, `s3tcspu`. `gfx_texture_compression 1` makes the client
upload `GL_COMPRESSED_RGBA`, which Mesa turns into DXT5, and the tile maps go up as one 3D texture.

The bug was big-endian, not the network or the textures: `net_set_non_blocking()` gives
`ioctl(FIONBIO)` a pointer to an `unsigned long`, Linux reads an `int` there, which on ppc64 is the
long's high half, 0. The UDP sockets stayed blocking: the client got the token and the accept, then
blocked in `recvfrom()` inside `CNetClient::Recv` waiting for a packet the server never sent (the
server waits for the client's NETMSG_INFO, sent only on its next loop pass). The screen kept the last
loading frame, the CPU idled. Found in the ppc64 chroot under qemu-user (Xvfb, llvmpipe, qemu's gdb
stub with Debian's dbgsym, libfaketime at a tenth of the speed so the connection's 10 s timeouts
outlast a frame). Our 0001 passes an `int` everywhere but Windows (840a3b5). No Debian or upstream
report existed; upstream master still has the `unsigned long`. A draft for upstream is in
`docs/upstream/`. Not checked yet: the sound by ear, a second run, and whether the DXT5 path logs
libspeq lines.

**SuperTux 0.6.3** (`supertux`, `supertux-data`, 158 MB download; the binary is `supertux2`). Runs:
in the level, 63-66 fps (2026-10-18). The recipe starts "Welcome to Antarctica" directly with
`--renderer opengl20` (fixed function with an FBO) and `--show-fps`. The level's title card waits
for a key with no timeout (`LevelIntro::update` leaves only on Jump, Action, Menu-select, Start or
Esc, and `GameSession::setup` shows it for every level with coins, badguys or secrets). SDL2 ignored
the virtual keyboard's Returns (why is not known), so `during()` presses the keys through XTEST
(`smoke_xkeys`: the window focused, keys held 0.25 s): Return, then Space and Return 8 s apart. Tux
then stands still until played. Variants `worldmap`, `menu`, `card`, `sdl`, `nomusic`.

**Frozen Bubble 2.212** (`frozen-bubble`, `-data`, 21 MB). Runs: level 1, right picture, 45 fps.
`--solo` starts a one-player game; the game is 640x480 on SDL 1.2 (sdl12-compat over our SDL3), so
fullscreen is that picture scaled. Perl starts it. Variants `menu`, `nosound`, `slow`, `window`.

**Pingus 0.7.6** (`pingus`, `-data`, 11 MB). Runs: the first tutorial level's start screen (it waits
for the Play click), 39 fps. `-r opengl` (textured quads in fixed function), `-f -R 1920x1080`;
the mouse does everything, but the penguins walk in and animate without input. Variants `menu`,
`sdl`, `delta`, `swcursor`, `nosound`.

## Waiting for the user

**TORCS 1.3.7** (`torcs`, `torcs-data`, 326 MB): menu only (`-r` is the console mode, no window):
Race > Quick Race > New Race, the arrow keys. setup() enables the game's "texture compression ARB"
option in `config/graph.xml` (plib uploads `GL_COMPRESSED_RGB(A)_ARB`, which Mesa makes DXT1/DXT5) and
a 1024 texture size limit; the key names come from the binary's strings and are unverified.
Fullscreen goes through GLUT game mode and XRandR. Risks: plib's state changes and big textures,
the simulation and 4 robots on the PPE. Variants `notc`, `window`, `single`.

**Trigger Rally 0.6.6** (`trigger-rally`, `-data`, 116 MB): menu only (no command line): Single Race,
the arrow keys. setup() writes cheap graphics (no foliage, weather, dirt, anisotropy; point
snowflakes) into its config. Variant `full`: the game's defaults.

**Chromium B.S.U.** (`chromium-bsu`, `-data`, 1 MB): menu only: Enter on Start Game; arrows or the
mouse move, Space fires. Medium graphics, OpenAL, its own fps counter on. Variants `full`, `min`,
`noblend`, `nosound`.

All three are installed on the PS3 (PLAN 1.11).

**Nexuiz Classic 2.5.2 on `darkplaces-gl2`** (PLAN 1.12, D12). Built and in ps3-dev, not installed:
`darkplaces-gl2`, `nexuiz-data`, `nexuiz-textures` are about 800 MB on the USB disk, which the user
decides. Debian's darkplaces (0~20180908 and the 2026 git snapshot) has only GL 3.2 core and GLES2
renderpaths, so it can't run on the RSX. The last snapshot with the GL 1.1/1.3/2.0 renderpaths
(`RENDERPATH_GL11/GL13/GL20`, an SDL2 window, `vid_gl20 0` for the combiner path) is
0~20180412~beta1, Debian buster's orig tarball (snapshot.debian.org, `DARKPLACES_*` in config.mk).
`darkplaces-gl2/` (`TYPE=upstream`, `make darkplaces-gl2-deb`, 2 MB) builds it with Debian's 13
patches (link options, no SSE rasterizer off x86, GPU skinning off) and `-std=gnu17` (the code uses
`false`/`true` as enum names; gcc 15 defaults to C23). It installs `/usr/games/darkplaces-gl2` beside
Debian's engine and `/usr/games/nexuiz-gl2` (Debian's wrapper: `-nexuiz -basedir
/usr/share/games/nexuiz`, then `nexuiz-lean.cfg`). The data is Debian's unchanged `nexuiz-data` and
`nexuiz-textures` 2.5.2-14 (764 MB); `nexuiz-music` is left out.

- Big-endian: DarkPlaces swaps through `LittleLong` and friends (`mem_bigendian` at run time) and
  Debian builds its successor on s390x. Under qemu-user the dedicated server loaded `aggressor` from
  the real data (1222 faces, 386 clusters, progs.dat, csprogs.dat, 206 entities, "Server spawned")
  and ran two minutes without a crash: file loading and QuakeC, not the renderer, sound or client.
- `nexuiz-lean.cfg`: `vid_gl20 0`, `gl_picmip 2`, no realtime lighting, bloom or water,
  `r_texture_dds_save 1` (a compressed texture cache in `~/.nexuiz`), mono 22.05 kHz sound, no decals.
  textures.pk3 is 522 MB, read on demand.
- Recipe `games/bench/recipes/nexuiz.sh` (`SUITE=0`): the engine's own `-benchmark demos/bench1.dem`
  with `-benchmarkruns N`, which prints "N frames S seconds F fps" per pass. Order on the PS3: base
  (the combiner path), then `gl20` and `hires`; also `stereo`, `nosound`, `bench` (every pass counted,
  as the Quake II recipe).
- Risks, in order: the GL 2.0 path's shaders on nv30, VRAM and RAM with Nexuiz's maps, the sound's
  byte order (SDL, 16-bit; the Quake II OpenAL noise was such a bug), the CPU cost of the generic
  skeletal code.

## New games: first runs

Recipes from 2026-10-19 (commit 6d9fcaf), first run in batch ssdroot3 (SSD root, kernel .472, 120 s
runs at 1920x1080, one run each, rows 76 of the matrix; the webcam photo at 60 s and the logs were
checked, the sound only by the PS3's `sound=` and `underruns=` lines, never by ear). No kernel
messages (dmesg.log empty) and no interference in any run. All from Debian ports ppc64.

| Game | Starts? | Picture at 60 s | Sound | RAM (min free / peak RSS) | What the recipe needed |
|---|---|---|---|---|---|
| Simutrans 124.5 + pak64 | yes | the new map under a credits dialog; HUD 25 fps | none (`-nosound`) | 34 / 74 MB | Escape to close the dialog (`during()` now sends it, window title `Simutrans` unverified) |
| Freeciv 3.2.6 | yes, but the game never started | a white GTK window: the pregame chat | RUNNING, 87 underruns (effects are off; the source is not known) | 15 / 94 MB | the server refused `start` (`minplayers 1` with no human) and `set skill` is not an option (it is `skilllevel`): both fixed; the map is still to be seen |
| Dungeon Crawl Stone Soup 0.34.1 | yes | still on "Loading maps..." in its small splash window; 67 s of CPU in the 120 s | none | 17 / 91 MB | a 300 s run (the load takes longer than 120 s; how much longer is open), then the fullscreen check; ending with SIGTERM prints Crawl's "crash" report, which is the harness |
| LinCity-NG 2.15.0 | yes | main menu, fullscreen (`SDL Mode 1918x1053`) | RUNNING, 16 underruns | 34 / 65 MB | menu only: the player loads a scenario |
| ScummVM 2026.1.0 | yes | Beneath a Steel Sky's intro art; HUD 5 fps median | RUNNING, 20 underruns; the mic hears it (12 dB over the room's floor) | 56 / 85 MB | Escape never went out: `xkeys` found no window named `ScummVM*` 8 s in, so the recipe now waits for the game's title (unverified); `--gfx-mode=normal` was "unrecognized" and is dropped |
| Flare 1.15 | yes | a language dialog over the title scene, fullscreen; HUD 59 fps (its `max_fps=60`) | RUNNING, 5 underruns | 48 / 72 MB | menu only: the player picks a language and starts |
| Warmux 11.04.1 | yes | the main menu in a small window | RUNNING, 26 underruns | 29 / 90 MB | fullscreen: the config is `~/.config/wormux/config.xml` (the BACKUP path was wrong); `setup()` now sets `full_screen` to true when the file exists, which it does after this first run |
| Widelands 1.3.1 | no: exited after 2 s | | | | `--fullscreen=true`: the option takes no value (fixed in e40e282); re-run pending |

Notes. Freeciv's server and client together left 15 MB free: a 1080p GTK3 client plus a server with
five AIs is about the limit of the console's RAM. Stone Soup's SDL window was a splash, not the
game, so its 17 MB free and 91 MB peak are the load's, not play's. ScummVM's ALSA sequencer errors
(`/dev/snd/seq`) are the MIDI port probe, harmless with the AdLib emulator. Cataclysm: DDA was
dropped from the batch (`cataclysm-dda-data` is not installable on ports). Re-runs and the games
that need the user are in [TESTS.md](../TESTS.md) Next.

OpenRCT2 has a recipe (`openrct2.sh`) and a baseline on the PS3 ([openrct2.md](openrct2.md#on-the-ps3-batch-ssdroot8)). `tools/rct2-data.sh <RCT2.iso>` builds the licensed `rct2-data` deb it
needs (arch all, `/usr/share/games/rct2`, about 115 MB without the ride music; `RCT2_MUSIC=1` adds
it) from the user's own RollerCoaster Tycoon 2 CD. It unpacks the InstallShield cabinets with
`unshield` and runs nothing from the disc; the package goes to `ps3-licensed` only
(`tools/licensed-packages.txt`) and the data is never committed.

**Warzone 2100 4.3.5** (our `warzone2100/`, the last release with a GL 2.1 renderer; recipe
`warzone2100.sh`, its header has the settings and the PC's RAM numbers). The first PS3 smoke run
(ssdroot6, ps3.566) had the HUD and the minimap but a black 3D view. Two causes, neither of them
the GL 2.1 driver:

- the game's own DXT encoder (etcpak) wrote broken blocks on big-endian: patch 0004 (ps3.586);
- GCC 16.2 miscompiles stb_image_resize at `-O3 -mcpu=cell`: the AltiVec loop vectorizer gets the
  4-channel horizontal downsampling loop wrong, so every mipmap level and every image that
  `texturesize` scales down came out nearly black and transparent. Terrain sampled black, the
  models' alpha test threw them away, the HUD (no mipmaps) was fine. Patch 0007 turns loop
  vectorization off for that one file when the target has AltiVec but no VSX (ps3.617, and in
  every later build). See
  [PACKAGING.md](../PACKAGING.md) for what this means for the other packages.

How it was found (2026-10-20): the amd64 build on llvmpipe, cut down to the PS3's GL 2.1 extension
list (`MESA_GL_VERSION_OVERRIDE=2.1`, `MESA_GLSL_VERSION_OVERRIDE=120`, `MESA_EXTENSION_OVERRIDE`
minus everything nv47 lacks, ARB_texture_rg, ARB_texture_float and EXT_texture_array among them),
renders the skirmish normally, so it is not a GL 2.1 limit. The ppc64 build under qemu-user with
llvmpipe and with softpipe was black, as on the PS3. Shader overrides as an autoload mod showed the
lightmap (fog of war) right and the textures black; logging each texture upload against the amd64
run showed level 0 identical and level 1 down from 103538663 to 465002 (byte sum of a 512x512
level). The game's compiled `stb_image_resize.cpp.o` reproduces it on its own; it takes
`-mcpu=cell` (or 970, power6 with AltiVec), `-O3` and `-DNDEBUG` together, and `#pragma GCC
novector` on that one loop fixes it.

The second PS3 smoke run (2026-10-20, ps3.617, stopped at 110 s): the 60 s photo showed the
loading screen, not a broken 3D view. Warzone's loading screen is the map preview, a tall column
in the middle of the screen with the start positions marked, and a row of white "stars" streaking
along the bottom (wrappers.cpp's loading animation). The amd64 build shows the same screen for its
first few seconds. On the PS3 the map load ran from 43 s to 80 s (`gameLoad` to the replay file in
game.log), so photos need to be taken after about 90 s. The sound was white noise. The Opus
decoder (the soundtrack, warzone2100-music) wrote its samples little-endian, but OpenAL's
AL_FORMAT_*16 expects native order. Patch 0008 copies them as they are (ps3.619). Checked under
qemu with OpenAL Soft's wave writer at the main menu: zero-crossing rate 0.44 with ps3.617 (noise),
0.01 with ps3.619 (the menu music). The decode itself was right on big-endian (track19.opus through
the game's opusfile, built with its flags, matches amd64 within 8 LSB), and the Vorbis effects
already asked ov_read for big-endian.

The third run (ps3.619): the sound was right, and the user heard music and effects. The 3D view was
dark: terrain black, structures flat and untextured. The UI was fine. Every 3D shader of the game
samples through texture2D(tex, uv, bias) with a bias of 0, and the UI shaders call texture2D without
one. The `stripes` variant of the recipe (assets/warzone2100-stripes, a shader mod in the autoload
mods) draws the 3D shaders in vertical stripes 120 px wide. The four modes repeat from the left
edge: TEX without bias, TXB with bias -20, TXB with the game's bias 0, no texture (flat grey). On
the PS3 the TEX stripe was fully right, terrain included (cobbled tiles, the "F1 F2" labels), and
both TXB stripes were dark and flat. So the RSX's TXB samples wrong whatever the bias. Mesa patch
0045 emits TXB as TEX. Warzone patch 0009 drops the bias argument on the GLSL 1.20 path, so the
game also works with a Mesa that doesn't have 0045. Its first version did that only for a bias of
0 and changed nothing (ps3.629): the default LOD distance ("High", `lodDistanceBias` -50) makes the
bias -0.5. Shaders captured under qemu with `MESA_SHADER_CAPTURE_PATH` showed it. ps3.633 drops the
bias whatever the setting, so the LOD distance option does nothing on GLSL 1.20. The variants `noaniso` (anisotropic filtering hidden)
and `tc0` (no DXT) ran in the same batch and were not needed for the answer.

A trap for the qemu checks: Mesa's software S3TC decoding is wrong on big-endian (llvmpipe and
softpipe: green comes back 0, so DXT textures look magenta), although the game's DXT blocks are
byte-identical to the amd64 build's. Under qemu use `texturecompression=0`; the RSX decodes DXT in
hardware, so this doesn't apply to the console.

## Skipped

- **Hedgewars 1.0.3**: our ppc64 engine (`hedgewars-engine`, FPU exceptions masked at start, patch
  0001) runs without the Qt frontend: `games/bench/hwdrive.py` starts it with `--internal --port N` and
  plays the frontend's IPC for a training mission (recipe `hedgewars.sh`). hwengine takes `--showfps` with
  no value: a stray argument is read as a demo file name. Runs on the PS3 with sound (562-650
  underruns), but the game ends by itself after 54-55 s with rc=0 (two runs, base on 2026-10-19 19:19Z
  and window at 23:00Z, no input in either). That is the engine's end of game, not our driver: the
  last lines of game.log are the water Droplet sounds and `Victory.ogg`, hwdrive logs `engine says q`,
  and `q` is what the engine sends from `gtATFinishGame` (uGearsHandlersMess.pas) after the mission's
  `EndGame()`. The Movement mission calls that from `victory()`, which runs when its final crate
  (`crates[2]`, spawned at 2639,1973) is deleted, normally by a hedgehog collecting it. Nobody walks
  here, so on the PS3 the crate is probably lost to the water. The same mission with Debian's amd64
  engine under Xvfb, driven by the same hwdrive.py with no input, ran the whole 250 s without
  ending, so the ppc64 engine's world differs: the upstream engine Lua tests already fail 2 of 7 on
  ppc64 (`stuckcake.lua`: `EInvalidOp`; `gravity_get_set.lua`: wrong grenade height at gravity 200
  and 300), so the physics of falling gears is the suspect, not the IPC, the recipe or the map data
  (the `draw` command reads its points byte by byte). Not fixed: that is an engine bug to find with the
  Lua tests on ppc64 (a crate-drop test would show it), and it is parked. The smoke is useful for what
  it shows (engine starts, loads, draws, plays sound for 55 s), not for a 120 s window; hwdrive.py's
  message now says that `q` is the game's own end. Nothing to change in the recipe or driver: they
  send no quit and set no time limit.
- **Wesnoth 1.18.8**: our ppc64 build (Debian has none, Boost.Context #1146645; recipe `wesnoth.sh`).
  By default it redirects its own stdout and stderr to `~/.config/wesnoth-1.18/logs/`, which the
  bench harness reads as the game having ended; the recipe passes `--no-log-to-file`. Variants:
  `nosound`, `window` (windowed 1920x1080), `menu`, `httt`, `ai`. Expect 2-4 GB of address space and
  about 130-215 MiB RSS. Variant `ai` (two AIs on The Freelands, `--nodelay`, 20 turns) ran its whole
  360 s on 2026-10-19 22:54Z with the right picture (units moving, "2 enemy units sighted", 68 fps
  steady, HUD median 114, 1% low 5.7 fps, 98 frames over 100 ms with a 7.9 s load frame), peak RSS
  110 MB, lowest MemAvailable 15.3 MiB, CPU 179 s, 83 underruns; the 20 turns did not finish in
  that time. game.log is full of `cannot open track '*.ogg'`: the Wesnoth music package is not
  installed, so there is only the effects' sound.
- The research's other exclusions (DDNet's test hang, Warzone 2100's GL 3.0 and big-endian static
  assert, Xonotic, Tesseract) are in [research/GAME-CANDIDATES.md](../research/GAME-CANDIDATES.md).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md) ("Smoke tests",
"Smoke fixes", "Nexuiz Classic").
