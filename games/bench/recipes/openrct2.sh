# OpenRCT2 0.5.6 (our big-endian build, openrct2/: openrct2 0.5.6+ds-1+ps3.N, plus openrct2-data and
# openrct2-music, both arch all; Debian's is little-endian only) with the user's RollerCoaster Tycoon 2 data
# (rct2-data, ps3-licensed suite, in /usr/share/games/rct2): smoke test, not benchmarked. The software
# renderer (the OpenGL engine needs GL 3.3): the game draws an 8-bit frame on the CPU and SDL's renderer
# (opengl on nouveau) scales and presents it. Base: borderless fullscreen 1920x1080 with window_scale 2, so the
# game renders 960x540 and SDL doubles it (the setting to ship: the PPE paints a quarter of the pixels).
# Variant full: window_scale 1 (renders 1920x1080). The park loads straight from the command line, no
# input needed: Electric Fields (small map, rides and guests from the start); variant big: Six Flags Magic
# Mountain (the largest RCT2 park; 2782 guests after 10000 ticks in our oracle run). Variant title: no park,
# the title sequence. Variants nosound (sound, ride music and title music off) and lean (nosound plus
# multithreading off and MALLOC_ARENA_MAX=1: the RAM knobs measured on the PC), sdlsw (SDL's software
# renderer and no GL framebuffer: no Mesa in the process at all).
# The simulation runs at its fixed 40 ticks per second; FPS counter on (top middle of the screen; the
# frame rate is capped at 40 too, uncap_fps off). The park's date at the bottom right should advance.
# Settings: a private user directory, $HOME/.config/OpenRCT2-ps3bench (--user-data-path; the player's own
# ~/.config/OpenRCT2 stays untouched) with config.ini written by setup() (BACKUP restores it; the game
# rewrites it at exit). infer_display_dpi=false (else the first start replaces window_scale with the
# display's DPI ratio), last_run_version set (no changelog window), autosave never (no disk writes during
# the run), game_path=/usr/share/games/rct2 (the game also finds it there itself, patch 0005).
# First start only: the game indexes its objects (2533 from objects 1.8.0 plus RCT2's ObjData),
# scenarios and tracks into the user directory: on the PC 0.1 s, on the PPE expect seconds to a minute.
# Risks: RAM. Measured on the PC (amd64 build of the same source, Xvfb 1920x1080, SDL software renderer
# without GL in the process; RSS median over 120 s after a 60 s warm-up, build/tmp/openrct2 ram.sh):
# Six Flags Magic Mountain (2782 guests): 309 MB at 1920x1080, 300 MB at 960x540 x2, 260 MB lean (anon 228);
# Electric Fields: 264 / 253 / 232 MB. Of that, 70 MB is the game state: the entity table is sized for
# 65535 entities (512 bytes each, 57.7 MB for the registry; RCT2 parks hold at most 10000), zero-filled at
# start. With SDL's opengl renderer the GL driver comes on top (llvmpipe on the PC: +140 MB; nv30 is less,
# and the 960x540 texture is VRAM). Expect swap: watch MemAvailable, swap-ins and stalls. Speed: the
# PPE paints the viewport every frame (dirty rectangles only when nothing moves), unknown; the sim alone
# under qemu runs ~10x slower than the PC.
COMM=openrct2
. "$here/recipes/smoke.inc"
# the first start indexes the objects (see above)
DURATION=${SMOKE_SECS:-300}
ORCT2_DIR=$HOME/.config/OpenRCT2-ps3bench
BACKUP=("$ORCT2_DIR/config.ini")
# played sessions' fresh defaults (session.sh, freshhome.py): the game's real directories (checked against its XDG/legacy paths)
# (ORCT2_DIR is the bench's own; a played session runs on the real ~/.config/OpenRCT2)
SESSION_HOME=(.config/OpenRCT2 .config/OpenRCT2-ps3bench)
DATA_DIRS=(/usr/share/openrct2 /usr/share/games/rct2)
ORCT2_SCALE=2
ORCT2_SOUND=true
ORCT2_MT=true
ORCT2_PARK="/usr/share/games/rct2/Scenarios/Electric Fields.SC6"
setup() {
	[ -e /usr/share/games/rct2/Data/g1.dat ] || echo "WARNING: no RCT2 data: apt install rct2-data (ps3-licensed suite)"
	mkdir -p "$ORCT2_DIR"
	printf '%s\n' "[general]" "game_path = \"/usr/share/games/rct2\"" "last_run_version = \"0.5.6\"" "play_intro = false" \
		"drawing_engine = SOFTWARE_HWD" "fullscreen_mode = 2" "fullscreen_width = 1920" "fullscreen_height = 1080" \
		"window_width = 1920" "window_height = 1080" "window_scale = $ORCT2_SCALE.000000" "infer_display_dpi = false" \
		"multithreading = $ORCT2_MT" "uncap_fps = false" "show_fps = true" "autosave = 5" \
		"[sound]" "master_sound = $ORCT2_SOUND" "sound = $ORCT2_SOUND" "ride_music = $ORCT2_SOUND" \
		"title_theme = $([ "$ORCT2_SOUND" = true ] && echo 2 || echo 0)" > "$ORCT2_DIR/config.ini"
}
# OpenRCT2 wants its options after the park path
orct2_cmd() { CMD=(/usr/games/openrct2 ${ORCT2_PARK:+"$ORCT2_PARK"} --user-data-path="$ORCT2_DIR" --no-install); }
orct2_cmd
variant_full() { ORCT2_SCALE=1; }
variant_big() { ORCT2_PARK="/usr/share/games/rct2/Scenarios/Six Flags Magic Mountain.SC6"; orct2_cmd; }
variant_title() { ORCT2_PARK=; orct2_cmd; }
variant_nosound() { ORCT2_SOUND=false; }
variant_lean() { ORCT2_SOUND=false; ORCT2_MT=false; ENV+=(MALLOC_ARENA_MAX=1); }
variant_sdlsw() { ENV+=(SDL_RENDER_DRIVER=software SDL_FRAMEBUFFER_ACCELERATION=0); }
