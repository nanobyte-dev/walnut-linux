# OpenTTD 15.1 (Debian) with OpenGFX/OpenSFX/OpenMSX.
#   base: the title screen's own demo game (trains running, the camera panning by itself) at
#         1920x1080 fullscreen with the SDL software video driver through plain X11: no GL, so no
#         Gallium HUD and no fps (CPU profiles, stalls, memory). Steady state: the window starts
#         at the title game's load ("Loading savegame version", -d sl=1) + 60 s, 120 s measured,
#         then the game is stopped. The title game is a savegame with its random state
#         (deterministic, no AIs), but its ticks and the camera's sign-driven pans run on the
#         wall clock: at another frame rate the view at a given second differs a little.
#         Until 2026-10-18 the window waited for the HUD's first frame, which never came without
#         GL: no window, no PERF=1 samples, no MEM=1 snapshots (suite run 20261018T011738Z).
#         OpenTTD 14+ asks on the first title screen whether to take part in its survey (a
#         dialog over the title game): setup's config answers it (participate_survey = no)
#   sim:  simulation only, no video (null driver): a new game from a fixed seed, run for 20000 ticks
#         and timed (a CPU benchmark; it exits by itself)
#   gl:   the sdl-opengl driver (its default 40bpp-anim blitter: sprites still drawn on the CPU,
#         palette animation and the screen upload on the GPU); needs our openttd package
#         (openttd/: Debian's refuses nv30, no ARB_texture_rg); compare with base and accel
SUITE=1
COMM=openttd
OT_CFG=$HOME/.config/openttd/bench.cfg
BACKUP=("$OT_CFG")
# participate_survey lives in private.cfg since 13.0, but a config without [version] (this one:
# -x keeps OpenTTD from writing it back) is read as older than that split, so OpenTTD 15.1 takes
# the private settings from it as well (settings.cpp, IFV_PRIVATE_SECRETS)
setup() {
	mkdir -p "$(dirname "$OT_CFG")"
	printf '%s\n' "[misc]" "fullscreen = true" "resolution = 1920,1080" "[network]" "participate_survey = no" > "$OT_CFG"
}
OT_V=sdl
# SDL3's accelerated window framebuffer (a GL texture) shows wrong colours (a strong blue cast) on
# big-endian; its plain X11 path is right. Until the SDL3 fix (docs/GAME-PLAN.md), base uses X11.
ENV=(SDL_FRAMEBUFFER_ACCELERATION=0)
ot_cmd() { CMD=(/usr/games/openttd -x -c "$OT_CFG" -r 1920x1080 -v $OT_V -d sl=1); }
ot_cmd
DURATION=240
WINDOW_STOP=1
HUD_SKIP=10
HUD=0	# software video: the GL variants turn it on
LOAD_MARK='Loading savegame version'	# -d sl=1, the title game (opntitle.dat)
variant_sim() {
	CMD=(/usr/games/openttd -x -c "$OT_CFG" -g -G 12345 -v null:ticks=20000 -s null -m null)
	HUD=0; WM=; DURATION=90; WINDOW_STOP=0; LOAD_MARK=
}
# SDL_FRAMEBUFFER_ACCELERATION is for the window surface the software driver uses: not here
# -d driver=2: the GL renderer, the luminance fallback and shader compiler warnings on stderr
variant_gl() { OT_V=sdl-opengl; ot_cmd; CMD+=(-d driver=2); ENV=(); HUD=1; }
# SDL3's default accelerated framebuffer (to check the SDL3 fix)
variant_accel() { ENV=(); HUD=1; }
# the accelerated framebuffer with OpenTTD's cheapest blitter (8-bit palette; the default is
# 32bpp-anim, which redraws palette animation per pixel in software)
variant_accel8() { ENV=(); CMD+=(-b 8bpp-optimized); HUD=1; }
