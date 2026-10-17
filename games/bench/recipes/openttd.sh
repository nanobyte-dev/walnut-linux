# OpenTTD 15.1 (Debian) with OpenGFX/OpenSFX/OpenMSX.
#   base: the title screen's own demo game (trains running, the camera panning by itself) at
#         1920x1080 fullscreen with the SDL software video driver; fps from the Gallium HUD. Steady
#         state: the HUD window (60 s from the first frame, 120 s, then stopped; 60 s in all
#         before). The title game is a savegame with its random state (deterministic, no AIs),
#         but its ticks and the camera's sign-driven pans run on the wall clock: at another
#         frame rate the view at a given second differs a little, close but not identical
#   sim:  simulation only, no video (null driver): a new game from a fixed seed, run for 20000 ticks
#         and timed (a CPU benchmark; it exits by itself)
#   gl:   the sdl-opengl driver (its default 40bpp-anim blitter: sprites still drawn on the CPU,
#         palette animation and the screen upload on the GPU); needs our openttd package
#         (openttd/: Debian's refuses nv30, no ARB_texture_rg); compare with base and accel
SUITE=1
COMM=openttd
OT_CFG=$HOME/.config/openttd/bench.cfg
BACKUP=("$OT_CFG")
setup() {
	mkdir -p "$(dirname "$OT_CFG")"
	printf '%s\n' "[misc]" "fullscreen = true" "resolution = 1920,1080" > "$OT_CFG"
}
OT_V=sdl
# SDL3's accelerated window framebuffer (a GL texture) shows wrong colours (a strong blue cast) on
# big-endian; its plain X11 path is right. Until the SDL3 fix (docs/GAME-PLAN.md), base uses X11.
ENV=(SDL_FRAMEBUFFER_ACCELERATION=0)
ot_cmd() { CMD=(/usr/games/openttd -x -c "$OT_CFG" -r 1920x1080 -v $OT_V); }
ot_cmd
DURATION=240
WINDOW_STOP=1
HUD_SKIP=10
variant_sim() {
	CMD=(/usr/games/openttd -x -c "$OT_CFG" -g -G 12345 -v null:ticks=20000 -s null -m null)
	HUD=0; WM=; DURATION=90; WINDOW_STOP=0
}
# SDL_FRAMEBUFFER_ACCELERATION is for the window surface the software driver uses: not here
# -d driver=2: the GL renderer, the luminance fallback and shader compiler warnings on stderr
variant_gl() { OT_V=sdl-opengl; ot_cmd; CMD+=(-d driver=2); ENV=(); }
# SDL3's default accelerated framebuffer (to check the SDL3 fix)
variant_accel() { ENV=(); }
# the accelerated framebuffer with OpenTTD's cheapest blitter (8-bit palette; the default is
# 32bpp-anim, which redraws palette animation per pixel in software)
variant_accel8() { ENV=(); CMD+=(-b 8bpp-optimized); }
