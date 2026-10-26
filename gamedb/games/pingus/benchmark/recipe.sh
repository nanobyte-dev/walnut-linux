# Pingus 0.7.6 (Debian ports ppc64: pingus, pingus-data 11 MB download; SDL 1.2 through
# sdl12-compat over our SDL3, optional OpenGL renderer): smoke test, not benchmarked. Starts a
# tutorial level at once (the first one, "basher-tutorial": a file on the command line); the
# mouse does everything (pick an ability from the bar at the bottom, click a penguin; Space
# fast-forwards, Backspace blows them all up, Esc leaves; a pad can't drive it). Without
# input the penguins still walk in and animate, so a hands-off run shows the drawing. Variant menu: the main menu (no level).
# Settings: -r opengl (Pingus' GL renderer: textured quads in GL fixed function), -f -R 1920x1080
# fullscreen; ~/.pingus/config is saved by the game at exit, BACKUP restores it. Variants sdl
# (-r sdl: SDL surfaces, software, at 1920x1080 on the PPE) and delta (-r delta, only changed areas).
# Risks: sdl12-compat creating an OPENGL surface at fullscreen (SDL_SetVideoMode path), the GL
# renderer's texture uploads for the big level backgrounds (RAM/VRAM), the pointer (the mouse is the
# only control; variant swcursor draws Pingus' own cursor).
COMM=pingus
. "$BENCH_LIB/smoke.inc"
BACKUP=("$HOME/.pingus/config")
PG_LEVEL=/usr/share/games/pingus/data/levels/tutorial/basher-tutorial-grumbel.pingus
PG_R=opengl
pg_cmd() { CMD=(/usr/games/pingus -r "$PG_R" -f -R 1920x1080 "$@"); }
pg_cmd "$PG_LEVEL"
variant_menu() { pg_cmd; SHOT_STATIC=1; }
variant_sdl() { PG_R=sdl; pg_cmd "$PG_LEVEL"; }
variant_delta() { PG_R=delta; pg_cmd "$PG_LEVEL"; }
variant_swcursor() { CMD+=(--software-cursor); }
variant_nosound() { CMD+=(-s -m); }
