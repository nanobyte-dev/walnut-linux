# SuperTuxKart 1.5 (our build, packages/patched/games/supertuxkart/: big-endian .spm loading, legacy-renderer fixes;
# 839 MB with its data, deployed by name, not in ps3-games). Its profile mode (--profile-time)
# starts a race at once with every kart driven by the AI, drives for the given seconds, prints
# "Number of frames: N time T, Average FPS: F" and quits. GL 2.1 picks the legacy renderer by
# itself ("Using the fixed pipeline"). RAM and VRAM are the risk (its FAQ wants 1 GB against our
# 217 MB): a small track and 4 karts.
# setup() writes the whole config (~/.config/supertuxkart/config-0.10/config.xml) every run, so
# the run doesn't depend on what an earlier one saved: the settings below, everything else at the
# game's defaults (a file without a key keeps that key's default). Names from src/config/
# user_config.hpp; the file has one element per group with the settings as attributes. The game
# rewrites it when it quits (BACKUP restores it).
# base: the game's defaults (HD textures on: up to 2048x2048; texture compression on, which our
# 0004 makes reach the legacy renderer: opaque power-of-two textures as DXT1, compressed by Mesa
# at upload; the minimap shown, 512x512 render target with 0003), but vsync off and the
# old-driver popup off. Every variant changes one thing, except lean (the candidate PS3 default).
# Memory per run: during.log has MemAvailable and SwapFree every 5 s, bench.txt their lowest.
# Steady state: the HUD window, 60 s from the end of the track load (LOAD_MARK, the Track: line
# printed when the scene is built; the race starts at once, no countdown), then 120 s. Our patch
# 0005 (ProfileWorld::update) with STK_PROFILE_WARMUP=60 restarts the tick count and the clock
# once the race time passes 60 s and ends the race at 60 + the profile time (180 s: 240 s of race,
# 180 s measured); the "Average FPS" line it prints is ticks per second (see FPS_RE below). The race is AI-driven
# (rand() seeded from the clock, frame-time physics): close between runs, not identical.
COMM=supertuxkart
REF_PKGS=supertuxkart	# the PC reference (tools/bench-ref.sh): Debian's supertuxkart
# the game data GAMEDATA=ssd takes from the SSD (supertuxkart-data)
DATA_DIRS=(/usr/share/games/supertuxkart)
STK_CFG=$HOME/.config/supertuxkart/config-0.10/config.xml
BACKUP=("$STK_CFG")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against src/io/file_manager.cpp: XDG config, and addons, replays, screenshots in XDG data; the cache stays
SESSION_HOME=(.config/supertuxkart .local/share/supertuxkart)
STK_TRACK=lighthouse STK_KARTS=4 STK_TIME=180
ENV=(STK_PROFILE_WARMUP=60)
declare -A STK_VIDEO=([enable_high_definition_textures]=1 [max_texture_size]=512
	[enable_texture_compression]=true [old_driver_popup]=false)
declare -A STK_GFX=([particles-effecs]=2 [animated-characters]=true [geometry-level]=2
	[swap-interval-vsync]=0)
declare -A STK_MINIMAP=([display]=0)
stk_group() {	# <name> <assoc array name>: one config element
	local -n a=$2
	local k
	printf '    <%s\n' "$1"
	for k in "${!a[@]}"; do printf '        %s="%s"\n' "$k" "${a[$k]}"; done
	printf '    />\n'
}
setup() {
	mkdir -p "${STK_CFG%/*}"
	{
		printf '<?xml version="1.0"?>\n<stkconfig version="8" >\n'
		stk_group Video STK_VIDEO
		stk_group GFX STK_GFX
		stk_group Minimap STK_MINIMAP
		printf '</stkconfig>\n'
	} > "$STK_CFG"
}
stk_cmd() {
	CMD=(/usr/games/supertuxkart --fullscreen --screensize=1920x1080 --track=$STK_TRACK
		--numkarts=$STK_KARTS --profile-time=$STK_TIME --no-start-screen)
}
stk_cmd
# No FPS_RE: the game's "Average FPS" is its physics tick rate (ProfileWorld::update runs once per
# 120 Hz tick: 21602 "frames" in every run, 0.92 x real time at 110), not rendered frames; the HUD
# (19-21 fps) is the frame rate. parse() keeps the tick numbers as stk_ticks, stk_tickrate.
LOAD_MARK='Track: Overall scene complexity'
PLAY_MARK=$LOAD_MARK	# the track is built, the race starts (a played session; no session() command yet)
SOUND_AT=70	# the game opens its audio device late on a slow root: a read at 15 s said sound=none until 2026-10-18
DURATION=420	# a safety cap: the load (~60 s, more when it swaps), 240 s of race, the quit
during() {
	while :; do
		printf '%(%s)T %s\n' -1 "$(awk '/^(MemAvailable|SwapFree):/ { printf "%s%s ", $1, $2 }' /proc/meminfo)"
		sleep 5
	done
}
parse() {
	sed -n 's/.*Number of frames: \([0-9]*\) time \([0-9.]*\), Average FPS: \([0-9.]*\).*/stk_ticks=\1\nstk_tick_s=\2\nstk_tickrate=\3/p' \
		"$D/game.log" 2>/dev/null | tail -3
	awk '{ for (i = 2; i <= NF; i++) if ($i ~ /^MemAvailable:/) { v = substr($i, 14) + 0; if (!n++ || v < m) m = v } }
	     END { if (n) print "min_memavailable_kb=" m }' "$D/during.log" 2>/dev/null
}
# HD textures off (bit 1: the user's choice, so the game doesn't turn them back on): textures are
# scaled down to max_texture_size (512) at load instead of 2048. The biggest single VRAM and RAM cut
# expected: a 2048x2048 RGBA8 texture is 16 MB, at 512 1 MB (DXT1: 128 KB)
variant_nohd() { STK_VIDEO[enable_high_definition_textures]=2; }
# the same capped at 256
variant_tex256() { STK_VIDEO[enable_high_definition_textures]=2; STK_VIDEO[max_texture_size]=256; }
# no texture compression: every texture RGBA8, as Debian's build (isolates 0004: VRAM, load time)
variant_notc() { STK_VIDEO[enable_texture_compression]=false; }
# minimap hidden: its render target (FBO texture plus depth buffer) isn't made (0003)
variant_nomap() { STK_MINIMAP[display]=2; }
# no particles (skid, nitro, weather, explosions): fewer billboards and their textures
variant_noparticles() { STK_GFX[particles-effecs]=0; }
# drivers not animated: the legacy renderer skins every animated kart on the CPU each frame
variant_static() { STK_GFX[animated-characters]=false; }
# LOD switched at the shortest distances (Very Low): draw cost, not memory (every level is loaded)
variant_geo0() { STK_GFX[geometry-level]=0; }
# vsync on, the game's default (base turns it off)
variant_vsync() { STK_GFX[swap-interval-vsync]=1; }
# fewer karts on the track (their models load at startup anyway; this is draw and AI cost)
variant_karts1() { STK_KARTS=1; stk_cmd; }
# the candidate PS3 default if the single steps hold: HD textures off (512), compression on, no
# minimap, important particles only, static drivers
variant_lean() {
	STK_VIDEO[enable_high_definition_textures]=2
	STK_MINIMAP[display]=2
	STK_GFX[particles-effecs]=1 STK_GFX[animated-characters]=false
}
