# OpenTTD 15.1 (Debian, our openttd package for the GL driver) with OpenGFX/OpenSFX/OpenMSX.
#   base: a real game (2026-10-19). assets/openttd-bench.sav: 1962, 30 towns (65000 inhabitants),
#         318 stations, 132 trains, 600 road vehicles, 5 ships, 83 aircraft (a 256x256 map, played by
#         six AIs for 12 years on the PC, then every company made a human one: no script runs, no AI
#         window, no news; made with assets-src/). Loaded from the command line
#         (-g), 1920x1080 fullscreen, the camera on Taningville (a city of 5000 with a rail station
#         and a bus hub, ~35 vehicles within 16 tiles) at the normal zoom, the game at normal speed
#         for the whole run (OpenTTD has no timedemo: a fixed duration; the game thread's work per
#         frame is the same, the vehicle positions drift with the wall clock). The GL driver
#         (-v sdl-opengl, our package) with the package's default blitter (8bpp-optimized since
#         patch 0003; the ps3.297 package and Debian's use 32bpp-anim: the batches of 2026-10-19
#         ran the old default as `base`), fps from the Gallium HUD
#         (the load ends at the HUD's first frame, 60 s warm-up, 120 s measured, then the game is
#         stopped). Sound and music on as in play: the SDL sound driver at 44.1 kHz and the FluidSynth
#         music driver with the first soundfont it finds (TimGM6mb on the image), OpenMSX playing.
#         setup()'s config turns autosave off, the news popups off (no pause dialogs) and the
#         survey off (OpenTTD 14+ asks on the first start). The AI window can't open: no AI is left
#         in the save.
#         The PS3 needs the image's openttd, openttd-opengfx, openttd-opensfx, openttd-openmsx and
#         fluidsynth with a soundfont (all from Debian), no NewGRF, no AI.
#   sw:   the same game with the SDL software video driver through plain X11 (SDL3's accelerated
#         window framebuffer shows a blue cast on big-endian until the SDL3 fix): no GL, so no HUD and
#         no fps (CPU profiles, stalls, memory); the window starts at the savegame's load line
#         ("Loading savegame version", -d sl=1) + 60 s, 120 s measured
#   swaccel: sw with SDL3's accelerated window framebuffer (HUD fps: the SDL renderer is GL)
#   zoomout: base from assets/openttd-bench-far.sav: the same city one zoom step out (4x the area,
#         ~4x the sprites and station names on screen)
#   scroll: base with the camera panned by the keyboard (xkeys.py through XTEST): Right for 3 s,
#         Left for 3 s, over and over from the window's warm-up on; OpenTTD scrolls by the clock, so
#         the pan covers the same ground at any frame rate. Left/Right only: Up/Down clamp at the
#         map's edge from this camera and don't come back
#   nosound: effects off (-s null); nomusic: music off (-m null); quiet: both off
#   hz22, buf2048, buf4096: the SDL sound driver at 22.05 kHz (the mixer's rate, which the FluidSynth
#         music renders at too: half the synthesis), and with a 2048 or 4096 frame buffer (46 or 93 ms
#         at 44.1 kHz; the default is 1024 = 23 ms): sound candidates for underruns
#   blit8, blit32, blit32anim: base with the 8bpp-optimized, 32bpp-optimized or 32bpp-anim blitter
#         (32bpp-anim, the stock default and ours before patch 0003, is a 32-bit video buffer plus an
#         8-bit animation buffer, both mapped from a pixel buffer object that the CPU blends sprites
#         into): blit32anim is the old default, for comparisons with the batches before 0003
#   title: until 2026-10-18 base: the title screen's own demo game (trains running, the camera
#         panning by itself) with the software driver through X11, no HUD (the window starts at the
#         title game's load + 60 s). The title game is a savegame with its random state
#         (deterministic, no AIs), but its ticks and the camera's sign-driven pans run on the wall
#         clock. gl, accel, accel8 are title variants too (the GL driver; SDL3's accelerated
#         framebuffer; that with the 8bpp-optimized blitter), as they were.
#   simplay: the bench save's simulation only (null driver, 6000 ticks, exits by itself)
#   sim:  simulation only, no video (null driver): a new game from a fixed seed, run for 20000 ticks
#         and timed (a CPU benchmark; it exits by itself)
# The PS3 runs: `run.sh smoke openttd` (one run, 360 s at most), `run.sh bench openttd "base zoomout"`.
SUITE=1
COMM=openttd
OT_CFG=$HOME/.config/openttd/bench.cfg
BACKUP=("$OT_CFG")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against XDG config and data (saves); .openttd from older releases
SESSION_HOME=(.config/openttd .local/share/openttd .openttd)
# participate_survey lives in private.cfg since 13.0, but a config without [version] (this one:
# -x keeps OpenTTD from writing it back) is read as older than that split, so OpenTTD 15.1 takes
# the private settings from it as well (settings.cpp, IFV_PRIVATE_SECRETS)
setup() {
	mkdir -p "$(dirname "$OT_CFG")"
	{
		printf '%s\n' "[misc]" "fullscreen = true" "resolution = 1920,1080" "[network]" "participate_survey = no"
		printf '%s\n' "[gui]" "autosave_interval = 0" "autosave_on_exit = false"
		printf '%s\n' "[music]" "playing = true"
		printf '%s\n' "[news_display]"
		local n
		for n in arrival_player arrival_other accident company_info open close economy production_player \
			production_other production_nobody advice new_vehicles acceptance subsidies general; do
			printf '%s = off\n' "$n"
		done
	} > "$OT_CFG"
}
# which game, which video driver, how the sound is opened; ot_cmd builds the command from them
OT_GAME=play		# play: -g the save; title: the title screen's demo game
OT_SAVE=openttd-bench.sav
OT_V=sdl-opengl
OT_SND=()		# extra -s / -m arguments
OT_BLIT=()		# extra -b arguments
OT_SCROLL=0
ot_cmd() {
	CMD=(/usr/games/openttd -x -c "$OT_CFG" -r 1920x1080 -v $OT_V -d sl=1 -d driver=1 "${OT_BLIT[@]}" "${OT_SND[@]}")
	if [ "$OT_GAME" = play ]; then CMD+=(-g "$ASSETS/$OT_SAVE"); fi
}
# SDL3's accelerated window framebuffer (a GL texture) shows wrong colours (a strong blue cast) on
# big-endian; its plain X11 path is right. Until the SDL3 fix (docs/GAME-PLAN.md), the software
# variants use X11 (SDL_FRAMEBUFFER_ACCELERATION=0). vsync off, as in every other recipe
# (2026-10-18): Mesa ignores the swap interval the game asks for, so the numbers are uncapped and
# comparable (ETR's 59 fps looked like a 60 Hz cap)
ENV=(vblank_mode=0)
ot_cmd
DURATION=300	# the safety cap: GL start, load, 60 s warm-up, 120 s measured, margin (smoke stops at 360 s)
WINDOW_STOP=1
HUD_SKIP=10
HUD=1		# the load ends at the HUD's first frame; the software variants have no HUD
LOAD_MARK=
# the pan of variant scroll, started with the run: wait for the window, sit out the warm-up
during() {
	[ "$OT_SCROLL" = 1 ] || return 0
	local i
	for i in $(seq 1200); do xwininfo -root -tree 2>/dev/null | grep -qi '"openttd' && break; sleep 0.1; done
	echo "window at $EPOCHREALTIME"
	sleep 62
	while :; do
		XKEYS_HOLD=3 python3 "$BENCH_LIB/xkeys.py" openttd Right
		XKEYS_HOLD=3 python3 "$BENCH_LIB/xkeys.py" openttd Left
	done
}
variant_sw() { OT_V=sdl; ot_cmd; ENV+=(SDL_FRAMEBUFFER_ACCELERATION=0); HUD=0; LOAD_MARK='Loading savegame version'; }
variant_swaccel() { OT_V=sdl; ot_cmd; HUD=1; }
variant_zoomout() { OT_SAVE=openttd-bench-far.sav; ot_cmd; }
variant_scroll() { OT_SCROLL=1; }
variant_nosound() { OT_SND=(-s null); ot_cmd; }
variant_nomusic() { OT_SND=(-m null); ot_cmd; }
variant_quiet() { OT_SND=(-s null -m null); ot_cmd; }
variant_hz22() { OT_SND=(-s sdl:hz=22050); ot_cmd; }
variant_buf2048() { OT_SND=(-s sdl:samples=2048); ot_cmd; }
variant_buf4096() { OT_SND=(-s sdl:samples=4096); ot_cmd; }
variant_blit8() { OT_BLIT=(-b 8bpp-optimized); ot_cmd; }
variant_blit32() { OT_BLIT=(-b 32bpp-optimized); ot_cmd; }
variant_blit32anim() { OT_BLIT=(-b 32bpp-anim); ot_cmd; }
# the title screen's demo game, as base was until 2026-10-18 (software video, no HUD)
variant_title() { OT_GAME=title; OT_V=sdl; ot_cmd; ENV+=(SDL_FRAMEBUFFER_ACCELERATION=0); HUD=0; LOAD_MARK='Loading savegame version'; }
# the bench save's simulation without video: 6000 ticks (a quarter of a game year; 7 s on the PC,
# 830 ticks a second there against the 33 a second the game needs in real time): the CPU cost of
# 800 vehicles and 30 towns, no drawing, no sound; exits by itself
variant_simplay() {
	CMD=(/usr/games/openttd -x -c "$OT_CFG" -g "$ASSETS/openttd-bench.sav" -v null:ticks=6000 -s null -m null)
	HUD=0; WM=; DURATION=240; WINDOW_STOP=0; LOAD_MARK=
}
variant_sim() {
	CMD=(/usr/games/openttd -x -c "$OT_CFG" -g -G 12345 -v null:ticks=20000 -s null -m null)
	HUD=0; WM=; DURATION=90; WINDOW_STOP=0; LOAD_MARK=
}
# the title game with the sdl-opengl driver (the package's default blitter: 32bpp-anim before patch
# 0003, with sprites drawn on the CPU and the palette animation and the screen upload on the GPU); needs our openttd package (gamedb/games/openttd/packages/openttd/:
# Debian's refuses nv30, no ARB_texture_rg); compare with title and accel
# -d driver=2: the GL renderer, the luminance fallback and shader compiler warnings on stderr
variant_gl() { OT_GAME=title; OT_V=sdl-opengl; ot_cmd; CMD+=(-d driver=2); HUD=1; LOAD_MARK='Loading savegame version'; }
# the title game with SDL3's default accelerated framebuffer (to check the SDL3 fix)
variant_accel() { OT_GAME=title; OT_V=sdl; ot_cmd; HUD=1; LOAD_MARK='Loading savegame version'; }
# the accelerated framebuffer with OpenTTD's cheapest blitter (8-bit palette; the default is
# 32bpp-anim, which redraws palette animation per pixel in software)
variant_accel8() { OT_GAME=title; OT_V=sdl; ot_cmd; CMD+=(-b 8bpp-optimized); HUD=1; LOAD_MARK='Loading savegame version'; }

