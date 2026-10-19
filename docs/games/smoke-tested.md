# Smoke-tested games

Games outside the benchmark suite get one launch each to see whether they start and play: no
numbers kept, no repeats, not part of `run.sh suite` (their recipes set `SUITE=0`). Decided with the
user on 2026-10-17. Of the first eleven, all eight that start without someone at the controller run
(Teeworlds, SuperTux, Red Eclipse, Frozen Bubble, Chocolate, Crispy and Woof Doom, Pingus). The three
menu-only ones and Nexuiz wait for the user; nine more games got recipes on 2026-10-19 and wait for
their first run. Red Eclipse is in [sauerbraten.md](sauerbraten.md), the Doom ports in
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

## New games: not run yet

Recipes written 2026-10-19 (commit 6d9fcaf), scheduled as TESTS.md batch "smoke-new" after
ssdroot2, with an `apt install` between batches and `make bench-push`. All from Debian ports ppc64.

| Game | Package, size | What the recipe does | Risks named in the recipe |
|---|---|---|---|
| ScummVM 2026.1.0 | `scummvm` 246 MB installed with `scummvm-data`; the game data debs are arch all | base: Beneath a Steel Sky (`beneath-a-steel-sky`, 66 MB); variants `queen`, `drascula`, `lure`, `gl` (ScummVM's OpenGL manager), `intro`, `nosound`. Escape skips the intro after `SV_KBD_WAIT` s; 22050 Hz output, AdLib/FM music, the "fit" stretch mode | the SurfaceSDL path's colours, the OPL emulator's CPU |
| Simutrans 124.5 with pak64 | +30 MB installed, 6 MB download | a new game on the default random map at once, `-nosound`; variants `sound`, `hw`, `fps25`, `fps60`, `small`, `scenario` | RAM at 1080p, its software blitter repainting 2 Mpixel on the PPE |
| Dungeon Crawl Stone Soup 0.34.1 tiles | +47 MB, 22 MB download | a Human Berserker on seed 1, the tiles window at 1920x1080; variants `explore` (auto-explore every 6 s), `sprint`, `menu` | the 4096x4096 atlas upload |
| Freeciv 3.2.6 | `freeciv-client-gtk3` + `freeciv-server` + `freeciv-data`, +52 MB | a local server starts five novice AIs on a small map with a 3 s turn timeout; the GTK3 client observes; variant `play` (the player starts), `sdl2` | GTK3 cairo drawing at 1080p on the PPE; the fullscreen option's name is unverified |
| Widelands 1.3.1 | +628 MB installed, 422 MB download | the first tutorial ("Basic Control") at once; variants `menu`, `nosound`, `skip` | RAM, GLSL 1.20 limits, load time |
| LinCity-NG 2.15.0 (menu only) | +53 MB, on SDL3 | the main menu; the player loads a scenario; variants `gl`, `mute`, `window` | the SDL renderer's texture count at 1080p; mouse only |
| Flare 1.15 (menu only) | 620 MB of data, 570 MB download | the "Empyrean Campaign" title; the player creates a character; variants `swsurf`, `nosound` | 620 MB on the root file system, the tilesets decoded whole |
| Warmux 11.04.1 (menu only) | +119 MB, mostly CJK fonts | Play > Local game by mouse; the first run is windowed; variant `debug` | SDL_gfx rotozoom's pixel order on big-endian |
| Cataclysm: Dark Days Ahead 0.F-3 (menu only) | `cataclysm-dda-sdl` + data, +399 MB | MOTD, New Game, a world, a character (about a minute of keys), then the tiles view; software SDL renderer; variant `accel` | world generation allocates hundreds of MB: an OOM kill is the likely result, and the point of the test |

OpenRCT2 has no recipe yet. `tools/rct2-data.sh <RCT2.iso>` builds the licensed `rct2-data` deb it
needs (arch all, `/usr/share/games/rct2`, about 115 MB without the ride music; `RCT2_MUSIC=1` adds
it) from the user's own RollerCoaster Tycoon 2 CD. It unpacks the InstallShield cabinets with
`unshield` and runs nothing from the disc; the package goes to `ps3-licensed` only
(`tools/licensed-packages.txt`) and the data is never committed.

## Skipped

- **Hedgewars 1.0.3**: Debian builds no ppc64 binary; 2 of 7 engine Lua tests fail, in two different
  ways (build log hedgewars 1.0.3-5, ppc64, 2026-09-21): `stuckcake.lua` dies with `EInvalidOp:
  Invalid floating point operation`, and in `gravity_get_set.lua` the grenade stops at the wrong
  height when gravity is 200 or 300 (Y -10 and -4, expected 2 and 253), a physics divergence with no
  exception. So masking FPU exceptions would not be a test-only fix. What differs on ppc64 is the
  compiler: `debian/rules` builds the engine with Free Pascal 3.2.2 where `fp-compiler` exists, and
  through `pas2c` (GHC) and clang on armel, armhf, i386, mipsel and ppc64el, where it passes. Trying
  that on ppc64 means hours of GHC and clang under qemu with no guarantee the C engine is big-endian
  clean, plus a Qt5 frontend on 217 MB. Not started.
- **Wesnoth**: 2-4 GB of RAM, and Debian doesn't build it for ppc64 (Boost, #1146645).
- The research's other exclusions (DDNet's test hang, Warzone 2100's GL 3.0 and big-endian static
  assert, Xonotic, Tesseract) are in [research/GAME-CANDIDATES.md](../research/GAME-CANDIDATES.md).

Older versions and the dated history: [archive/GAME-PLAN.md](../archive/GAME-PLAN.md) ("Smoke tests",
"Smoke fixes", "Nexuiz Classic").
