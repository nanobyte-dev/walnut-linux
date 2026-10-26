# Red Eclipse 1.6.0 (Debian ports ppc64: redeclipse, redeclipse-common, redeclipse-data 842 MB
# download, 959 MB installed): smoke test, not benchmarked. The same engine family as Sauerbraten
# (Cube 2), so the same memory trouble is expected: BananaBread ran out of memory on this PS3 until
# our cube2 got its memory patches, and Red Eclipse has none of them. The smallest map and the lowest
# settings, then. Starts a deathmatch with bots on a map at once (`-x'dm <map>'`: setup.cfg's
# alias for `mode <deathmatch>; map <map>`, run by the engine's own loopback server; the player
# plays against the bots with WASD and the mouse, Space jumps; Esc is the menu). RE_MAP (environment)
# or a variant picks the map: torus (43 KB .mpz, the smallest real one; a gladiator-style duel
# map), singularity (58 KB), disco (9 KB, a test map whose config calls it "editing", may have no spawns); the next
# sizes up are cyanide, relax, oneiroi, eternal (320-370 KB).
# Settings (run of 2026-10-18: OOM-killed while loading torus; the game's investigations/2026-10-18-oom-the-memory-is-the-sound.md):
# - Sound was the memory. SDL_mixer decodes every sample whole into PCM in the device's format at
#   registration: the game's ~190 samples at start-up and every `mapsound` of the map's .cfg at the
#   map load (torus.cfg: 74 ambience files, 24 MB of .ogg). Debian's amd64 build under Xvfb and
#   llvmpipe (the host, same code): 534 MB RSS after the load with 44.1 kHz stereo, 381 MB with no
#   sample loaded, 397 MB with `soundmono 1` and `soundfreq 11025` (~137 MB less). On the PS3 the
#   process grew to ~200 MB (55 resident, ~150 in zram) during that load. soundmono and soundfreq
#   are read only when the audio device opens, before autoexec.cfg: setup() writes them to
#   init.cfg (run before initsound; the engine rewrites it at exit, BACKUP restores it).
#   Variable volumes (mastervol, soundvol) don't stop the decoding; SDL_AUDIODRIVER=none makes
#   SDL_Init fail (the engine inits audio with video) and the game quits.
# - Bots: 1.6 reads localinit.cfg for the loopback server (localexec.cfg, the recipe's file until
#   2026-10-18, is never read), and the variable is sv_botlimit: 1 bot (default: up to 3).
# - autoexec.cfg (run after config.cfg, so the saved ones can't override it): vsync 0, maxfps 0,
#   texreduce 3 (world texture slots only, 1/8 per side) and maxtexsize 256 (every mipmapped
#   texture: models, sky, HUD; host RSS 375 -> 326 MB, on the PS3 mostly VRAM, which is also
#   swap), shaderdetail 1, no water reflection, refraction or env map, no caustics, glare, grass,
#   shadow maps, depth effects, motion blur, dynamic lights, bump- or env-mapped models, GPU
#   skinning (nv30 compiles the bone-array vertex shader to if/else trees: 11 fps against 28 in
#   BananaBread), decals, anisotropy, gore, ragdolls, mumble. Every name checked in the 1.6
#   source (several are already 0 on GL 2.1). Video from the command line: -df1 -dw1920 -dh1080.
# - The "Welcome to Red Eclipse" profile menu of the first start (run of 2026-10-18: the load went
#   through and the game sat on it): game.cpp's update shows the `profile` menu over the match for as
#   long as needname() holds, i.e. player1's name is empty (no other first-run flag exists), and a
#   player does not spawn meanwhile (game.cpp, "prevent spawning"). The name is the persistent
#   playername variable (client.cpp, SVARF: setting it from a script sets the player's name), so
#   autoexec.cfg ends with `playername RE_NAME` (also for variant defaults, after RE_SET).
# Texture compression: like BananaBread the engine turns it off on Mesa (rendergl.cpp:
# `if(!mesa) usetexcompress = 2`), and usetexcompress is read-only (VAR min 1 > max 0), so no
# setting turns it on; our cube2 patch 0001 applies to Red Eclipse's tree (variant tc was a no-op
# and is gone). Engine patches that would cut more, none built: samples loaded when first played
# (sound.cpp addsound/playsound: the PCM of sounds never heard, most of the ambience), cube2
# 0002/0003 (fewer shader variants), 0005 (lightmap CPU copies freed), the sky cube map shrunk to
# envmapsize as its six sides are decoded (texture.cpp cubemaploadwildcard).
# Risks: out of memory still (watch dmesg.log and during.log's MemAvailable; the run keeps the
# VRAM swap off, NOVSWAP=0 turns it on again, as for BananaBread), GLSL 1.20 shaders against the
# 224 fragment constants (shader compile or link errors in game.log), a long first load (the local
# server has the client send it the map, then waits ~10 s before the match starts).
COMM=redeclipse
. "$BENCH_LIB/smoke.inc"
RE_HOME=$HOME/.redeclipse
BACKUP=("$RE_HOME/autoexec.cfg" "$RE_HOME/localinit.cfg" "$RE_HOME/config.cfg" "$RE_HOME/init.cfg")
RE_MAP=${RE_MAP:-torus}
RE_SET=("vsync 0" "maxfps 0" "menufps 30" "texreduce 3" "maxtexsize 256" "shaderdetail 1"
	"waterreflect 0" "waterrefract 0" "waterfade 0" "waterenvmap 0" "waterfallrefract 0" "caustics 0"
	"glare 0" "grass 0" "shadowmap 0" "depthfx 0" "motionblur 0" "maxdynlights 0" "bumpmodels 0"
	"envmapmodels 0" "gpuskel 0" "decals 0" "anisotropy 0" "nogore 2" "deathanim 0" "mumble 0")
RE_INIT=("soundmono 1" "soundfreq 11025")	# init.cfg: read before the audio device opens
RE_BOTS=1
RE_NAME=Bench	# the player name: without one the first-start profile menu blocks the match
setup() {
	mkdir -p "$RE_HOME"
	{ printf '%s\n' "${RE_SET[@]}"; echo "playername $RE_NAME"; } > "$RE_HOME/autoexec.cfg"
	printf '%s\n' "${RE_INIT[@]}" > "$RE_HOME/init.cfg"
	printf '%s\n' "sv_botlimit $RE_BOTS" > "$RE_HOME/localinit.cfg"
}
re_cmd() { CMD=(/usr/games/redeclipse -g -df1 -dw1920 -dh1080 "-xdm $RE_MAP"); }
re_cmd
variant_singularity() { RE_MAP=singularity; re_cmd; }
variant_disco() { RE_MAP=disco; re_cmd; }
variant_cyanide() { RE_MAP=cyanide; re_cmd; }
# the textures of the first smoke run (texreduce 2, maxtexsize 512)
variant_tex512() { RE_SET+=("texreduce 2" "maxtexsize 512"); }
# sound at the game's own 44.1 kHz stereo (the OOM of 2026-10-18 on torus)
variant_fullsound() { RE_INIT=(); }
# no bots (one player model less), or three (the game's default)
variant_bots0() { RE_BOTS=0; }
variant_bots3() { RE_BOTS=3; }
# every setting at the game's own defaults (empty autoexec.cfg and init.cfg, 3 bots): the old OOM
variant_defaults() { RE_SET=(); RE_INIT=(); RE_BOTS=3; }
# the main menu only, no game (the engine, shaders, fonts and the game's sounds, without a map)
variant_menu() { CMD=(/usr/games/redeclipse -g -df1 -dw1920 -dh1080); SHOT_STATIC=1; }
