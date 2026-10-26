# Warzone 2100 4.3.5 (our ppc64 build, packages/patched/games/warzone2100/: warzone2100 4.3.5-1+ps3.N, plus warzone2100-data
# and warzone2100-music, both arch all; Debian has 4.5/4.7, which need GL 3): smoke test, not
# benchmarked. 4.3.5 is the last release with an OpenGL 2.1 / GLES 2 renderer (GLSL 1.20 shaders: 19
# of them, no instancing, UBOs or texture arrays; docs/research/gl3-census/C-rts.md). The game asks
# SDL for a 3.1 core context first (Mesa's nv30 refuses it, the log says "Failed to create an OpenGL
# Core context") and then for a 2.1 compatibility one; gfxbackend stays opengl.
# What base does: an AI-vs-AI skirmish on Sk-HighGround (2 players, the smallest 2-player map: 48x96
# tiles; SemperFi both, Easy and Medium; base level 2 = bases with defences, no scavengers), straight
# from the command line, no input needed: `--autogame --skirmish=ps3bench.json`. The local player is
# an AI too, so nobody is needed at the keyboard; the camera sits on that player's base (the player
# with the HUD, power bar and minimap) and the other AI is played against it at normal speed. Upstream's
# --autogame runs the game at 500x speed (for automated tests: 324 s of game time in 13 s of real time
# on the PC); our patch 0002 reads WZ_AUTOGAME_SPEED, which this recipe sets to 1. The match is
# deterministic: it ends (the game quits) when player 0 is defeated, at 318002 ms of game time (5.3
# min), on the PC and in the ppc64 build run headless under qemu alike. A 5.3 min match at 1x is
# shorter than the default window (load + 60 s warm-up + 120 s): the run just ends with the game.
# Variant lobby: the same settings but as a human: `--skirmish=ps3bench.json` opens the skirmish
# lobby with the map and the AI already set; click "Start Hosting Game" (bottom of the options
# column) and then the READY box of the Player row (top right), and the game starts with you as the
# player against SemperFi (mouse: edge scrolling, left-drag selects, right click orders; Esc is the
# menu). Variant menu: the main menu. Variants nomusic, nosound: audio off.
# Settings: a private config directory, $HOME/.local/share/warzone2100-ps3bench (--configdir; the
# game's own ~/.local/share/warzone2100-4.3 stays untouched), with config written by setup()
# (BACKUP restores it; the game rewrites it at exit) and tests/ps3bench.json (the match). The lowest
# settings that keep the picture right: fullscreen 1920x1080, texturesize 512 (the default 2048
# costs +66 MB of texture RAM on the PC; 128 and 512 measured the same), shadows off, fog off,
# vsync off, no anti-aliasing, UPnP off, FPS counter on (bottom right), the music at its default
# volume (the CPU cost of Opus on the PPE is unknown: variant nomusic). 4.3.5's textures are PNG
# (the Basis Universal encoder is off in our build): the game compresses RGBA pages to DXT5 itself
# when it uploads (texturecompression=1, the default; 485 of 1348 textures), the rest is RGBA8 or
# LUMINANCE (on big-endian the game's own DXT encoder, etcpak, made wrong blocks: black terrain and
# missing models until patch 0004; GCC 16 at -O3 -mcpu=cell broke the mipmaps, made by stb_image_resize,
# until patch 0007: games/smoke-tested.md). First start only: the game writes its
# stats.db and caches in the config directory. Videos: the campaign videos (sequences.wz, the
# warzone2100-video package) aren't installed; fmvmode is irrelevant to that, the game shows a
# "Campaign videos are missing" notification at the menu and over the match until it is dismissed
# with "do not show again", so setup() writes that choice into notifications.json.
# Risks: RAM. Measured on the PC (amd64 build of the same source, Debian sid, llvmpipe with
# MESA_GL_VERSION_OVERRIDE=2.1, Xvfb 1920x1080, these settings, 1x speed AI-vs-AI): VmHWM 366-379 MB
# (97 MB of it libLLVM and Mesa's libraries); heaptrack: peak heap 259 MB = llvmpipe 118 (incl. the
# textures, ~77 MB live) + its JIT 49 + QuickJS 72 (the AI scripts, rules and the stats as JS
# objects: the game's biggest own consumer) + 20 other. Human against one AI: 370 MB (the second
# AI costs only ~5). Without any GL (--headless, 500x autogame, PC): VmHWM 164 MB, which is the game
# itself with its libraries. PS3 estimate: 180-205 MB (that 164 plus ~15 for Mesa's nv30 userland, or
# the census formula: RSS - 100 llvmpipe baseline - texture MB - buffers + 10); the textures are VRAM here. Knobs measured (PC RSS):
# texturesize 2048 +66 MB, texturecompression=0 +72 MB, shadows on +2, fog on +1. Watch dmesg.log,
# MemAvailable and swap-ins. Other things to look at: the first load (the game reads base.wz, 180
# MB, and mp.wz through physfs and builds the stats: 10 s on the PC, ~10x that on the PPE?), the
# JS AIs' CPU (semperfi runs every game tick, 10 per second), the GL call load (171 draws and
# ~2900 GL calls per frame, 429 glTexParameteri per frame: PPE-bound), nouveau "out of temps" on
# the terrain or water fragment shaders (grep stderr and the game log), and the update check the game
# starts at launch (a curl to wz2100.net; it fails quietly without network).
# Benchmark idea (not wired up): the base command again as a SUITE=1 recipe with the HUD window: a
# fixed match (deterministic, see above, so the scene is the same each run), WARMUP 30 s after the
# load, MEASURE 120 s (the match ends at 318 s of game time, so the window must fit before that);
# fps from the Gallium HUD (the game's own "FPS:" counter, bottom right, is in screenshots). The
# AI-only cost with no drawing: `--autogame --skirmish=... --headless` (WZ_AUTOGAME_SPEED=1 for
# real time, 500 for the whole match): 198 s at 500x under qemu on the PC, a PPE timing wanted.
COMM=warzone2100
. "$here/recipes/smoke.inc"
# loading the game: base.wz and mp.wz, the stats, the map; 300 s as for the other slow loaders
DURATION=${SMOKE_SECS:-300}
WZ_CFG=$HOME/.local/share/warzone2100-ps3bench
BACKUP=("$WZ_CFG/config")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against the XDG data directory (versioned names, hence the globs), older releases' ~/.warzone2100-x.y
SESSION_HOME=('.local/share/warzone2100*' '.warzone2100*')
DATA_DIRS=(/usr/share/warzone2100)
ENV+=(WZ_AUTOGAME_SPEED=1)
WZ_MUSIC=true
WZ_TC=1
WZ_STRIPES=0
WZ_SOUND=1
setup() {
	mkdir -p "$WZ_CFG/tests"
	printf '%s\n' "[general]" "width=1920" "height=1080" "fullscreen=1" "fullscreenwidth=1920" "fullscreenheight=1080" \
		"screen=0" "fullscreenscreen=0" "bpp=32" "vsync=0" "antialiasing=0" "texturesize=512" "texturecompression=$WZ_TC" \
		"shadows=0" "fog=false" "showfps=1" "upnp=0" "autorating=false" "fmvmode=0" "sound=$WZ_SOUND" \
		"music_enabled=$WZ_MUSIC" "gfxbackend=opengl" "jsbackend=quickjs" "difficulty=2" "colourMP=6" "configversion=2" > "$WZ_CFG/config"
	# colourMP=6: player 0 always pink (the default -1 shuffles the colours at random every run), so the
	# PS3 | PC screenshot sheets compare like for like; player 1 stays random (the json has no colour)
	# the match: bases 2 (bases with defences), no scavengers, SemperFi both sides
	printf '%s\n' '{' '	"challenge": {' '		"bases": 2,' '		"difficulty": "Easy",' '		"map": "Sk-HighGround",' \
		'		"maxPlayers": 2,' '		"powerLevel": 1,' '		"scavengers": "false",' '		"version": 2' '	},' \
		'	"player_0": {' '		"team": 0,' '		"ai": "multiplay/skirmish/semperfi.js"' '	},' '	"player_1": {' \
		'		"difficulty": "Medium",' '		"team": 1,' '		"ai": "multiplay/skirmish/semperfi.js"' '	}' '}' \
		> "$WZ_CFG/tests/ps3bench.json"
	# variant stripes: the diagnostic shader mod (assets/warzone2100-stripes) in the autoload mods; every
	# other variant removes it again
	rm -rf "$WZ_CFG/mods/4.3.5/autoload/stripes"
	if [ "$WZ_STRIPES" = 1 ]; then
		mkdir -p "$WZ_CFG/mods/4.3.5/autoload"
		cp -r "$ASSETS/warzone2100-stripes" "$WZ_CFG/mods/4.3.5/autoload/stripes"
	fi
	# the "Campaign videos are missing" notification, dismissed for good (the game keeps the file)
	printf '%s\n' '{' '    "notifications": {' '        "campaignVideoNotification": {' '            "skip": true' \
		'        }' '    }' '}' > "$WZ_CFG/notifications.json"
}
WZ=(/usr/bin/warzone2100 --configdir="$WZ_CFG" --fullscreen --resolution=1920x1080)
CMD=("${WZ[@]}" --autogame --skirmish=ps3bench.json)
# Screenshots (docs/games/SUITE.md "Screenshots"): the shot points count from the match's start (the
# replay file opens when the map has loaded: 80 s into a PS3 run, a few seconds on the PC), not from the
# first frame, which is the loading screen. The PC reference: our amd64 build of 4.3.5 in the local
# image wz-amd64-view (tools/bench-ref.sh warzone2100; deleted 2026-10-20, the game is done: a docker
# commit of an amd64 build container with our debs installed brings it back).
SHOT_RE='Started writing replay file'
PLAY_MARK=$SHOT_RE	# the match has started (a played session; no session() command yet)
REF_IMAGE=wz-amd64-view
variant_lobby() { CMD=("${WZ[@]}" --skirmish=ps3bench.json); SHOT_RE=; }
variant_menu() { CMD=("${WZ[@]}"); SHOT_RE=; SHOT_STATIC=1; }
variant_nomusic() { WZ_MUSIC=false; }
# Diagnostics for the RSX's dark 3D view (2026-10-20). stripes: the 3D shaders (terrain, decals,
# models) sample in four modes in vertical stripes 120 px wide, repeating left to right: TEX without
# bias, TXB with bias -20 (mip level 0), the game's TXB with its bias 0, no texture (flat grey 0.6).
# tc0: texturecompression=0 (no DXT). noaniso: anisotropic filtering hidden from the game.
variant_stripes() { WZ_STRIPES=1; }
variant_tc0() { WZ_TC=0; }
variant_noaniso() { ENV+=(MESA_EXTENSION_OVERRIDE="-GL_EXT_texture_filter_anisotropic -GL_ARB_texture_filter_anisotropic"); }
variant_nosound() { WZ_SOUND=0; WZ_MUSIC=false; CMD+=(--nosound); }
