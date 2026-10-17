# Red Eclipse 1.6.0 (Debian ports ppc64: redeclipse, redeclipse-common, redeclipse-data 842 MB
# download, 959 MB installed): smoke test, not benchmarked. The same engine family as Sauerbraten
# (Cube 2), so the same memory trouble is expected: Sauerbraten ran out of memory on this PS3 until
# our cube2 got its memory patches, and Red Eclipse has none of them. The smallest map and the lowest
# settings, then. Starts a deathmatch with bots on a map at once (`-x'dm <map>'`: setup.cfg's
# alias for `mode <deathmatch>; map <map>`, run by the engine's own loopback server; the player
# plays against the bots with WASD and the mouse, Space jumps; Esc is the menu). RE_MAP (environment)
# or a variant picks the map: torus (43 KB .mpz, the smallest real one; a gladiator-style duel
# map), singularity (58 KB), disco (9 KB, a test map whose config calls it "editing", may have no spawns); the next
# sizes up are cyanide, relax, oneiroi, eternal (320-370 KB).
# Settings: ~/.redeclipse/autoexec.cfg (run after config.cfg, so the saved ones can't override
# it; both restored after the run): vsync 0, maxfps 0, texreduce 2 and maxtexsize 512 (textures a
# quarter of the size per side, nothing above 512), shaderdetail 1, no water reflection or
# refraction, no grass, shadow maps, depth effects, motion blur, dynamic lights, bump-mapped
# models, GPU skinning (nv30 compiles the bone-array vertex shader to if/else trees: 11 fps against
# 28 in Sauerbraten), decals, anisotropy. The names are from the engine's variable list and
# some may not exist in 1.6 (the log would say "unknown command"). localexec.cfg (the loopback
# server's own) caps the bots at 2. Video from the command line: -df1 -dw1920 -dh1080 (saved in init.cfg too).
# Texture compression: like Sauerbraten the engine turns it off on Mesa (`if(!mesa) usetexcompress = 2`).
# Variant tc turns it on again (usetexcompress 2, textures from 256 pixels up): the DXT1/DXT5 path,
# with the encoders of libspeq on the SPEs for DXT1 and the PPE for DXT5 until Mesa 0032 is built.
# Risks: out of memory (watch dmesg.log and during.log's MemAvailable; the run keeps the VRAM
# swap off, NOVSWAP=0 turns it on again, as for Sauerbraten), GLSL 1.20 shaders against the 224
# fragment constants (shader compile or link errors in game.log), EXT_framebuffer_object water
# passes (off here), a long first load.
COMM=redeclipse
. "$here/recipes/smoke.inc"
RE_HOME=$HOME/.redeclipse
BACKUP=("$RE_HOME/autoexec.cfg" "$RE_HOME/localexec.cfg" "$RE_HOME/config.cfg" "$RE_HOME/init.cfg")
RE_MAP=${RE_MAP:-torus}
RE_SET=()	# extra CubeScript lines for autoexec.cfg (variants)
setup() {
	mkdir -p "$RE_HOME"
	printf '%s\n' "vsync 0" "maxfps 0" "menufps 30" "texreduce 2" "maxtexsize 512" "shaderdetail 1" \
		"waterreflect 0" "waterrefract 0" "waterfade 0" "grass 0" "shadowmap 0" "depthfx 0" \
		"motionblur 0" "maxdynlights 0" "bumpmodels 0" "gpuskel 0" "decals 0" "anisotropy 0" \
		"${RE_SET[@]}" > "$RE_HOME/autoexec.cfg"
	printf '%s\n' "botlimit 2" > "$RE_HOME/localexec.cfg"
}
re_cmd() { CMD=(/usr/games/redeclipse -g -df1 -dw1920 -dh1080 "-xdm $RE_MAP"); }
re_cmd
variant_singularity() { RE_MAP=singularity; re_cmd; }
variant_disco() { RE_MAP=disco; re_cmd; }
variant_cyanide() { RE_MAP=cyanide; re_cmd; }
variant_tc() { RE_SET+=("usetexcompress 2" "texcompress 256"); }
# every setting at the game's own defaults (no autoexec.cfg lines): the cost of the low settings
variant_defaults() { RE_SET=(); setup() { mkdir -p "$RE_HOME"; : > "$RE_HOME/autoexec.cfg"; printf '%s\n' "botlimit 2" > "$RE_HOME/localexec.cfg"; }; }
# the main menu only, no game (the engine, shaders and fonts, without a map)
variant_menu() { CMD=(/usr/games/redeclipse -g -df1 -dw1920 -dh1080); }
