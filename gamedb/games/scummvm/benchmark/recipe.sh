# ScummVM 2026.1.0 with Debian's free adventure games (Debian ports ppc64: scummvm 246 MB installed with
# scummvm-data and the libraries the image lacks; the games' data debs are arch all): smoke test, not
# benchmarked. base = Beneath a Steel Sky (package beneath-a-steel-sky, game id sky, 66 MB download).
# Variants queen (Flight of the Amazon Queen, id queen), drascula (Drascula: The Vampire Strikes Back,
# id drascula), lure (Lure of the Temptress, id lure, /en). Each game is started directly from its
# data directory (-p), as Debian's wrappers do, fullscreen at 1920x1080 with the software graphics
# manager (SurfaceSDL: the 320x200 or 640x480 picture scaled by SDL2's renderer; "normal" = the default
# manager); variant gl is ScummVM's OpenGL manager (--gfx-mode=opengl, GL 2.1 textures, no shaders).
# The games open with a long intro cutscene; during() presses Escape (it skips the intro in all four) after
# SV_KBD_WAIT s, then the player is in the game (point and click: the mouse does everything; no
# keyboard route, so a hands-off run shows the first screen only). Variant intro: leave the intro alone.
# ScummVM keeps its settings in ~/.config/scummvm/scummvm.ini (written at exit): BACKUP restores it;
# setup() writes fullscreen, the stretch mode "fit" (keeps the aspect ratio, 4:3 games get bars) and
# the output rate 22050 Hz (the SPU and the PPE both mix cheaper) into it, music through AdLib/FM
# emulation (no MIDI device here), subtitles on.
# What it exercises: SDL2 renderer/texture upload of a small 8-bit palettised surface at 1080p (the
# big-endian colour path of our SDL3 patch), SDL audio with the FM synth (MAME OPL) on the PPE, many
# small file reads (the games' own .dat/.dnr formats are little-endian: ScummVM byte-swaps explicitly,
# it has run on PowerPC consoles for years), RAM (the engine itself is 40-80 MB).
# Risks: the SurfaceSDL path's colours, the OPL emulator's CPU and underruns, scummvm-data's size.
COMM=scummvm
. "$BENCH_LIB/smoke.inc"
SV_CFG=$HOME/.config/scummvm/scummvm.ini
BACKUP=("$SV_CFG")
# played sessions' fresh defaults (session.sh, freshhome.py): the game's real directories (checked against its XDG/legacy paths)
SESSION_HOME=(.config/scummvm .local/share/scummvm .scummvmrc)
SV_GFX=		# empty: the backend default (--gfx-mode=normal was "unrecognized" in 2026.1.0, 2026-10-19)
SV_KEYS=1
SV_KBD_WAIT=8
SV_ID=sky
SV_TITLE="Beneath a Steel Sky"	# the first run (2026-10-19): xkeys found no window named "ScummVM*" 8 s in, so the intro ran on; the in-game window is probably titled after the game (not verified)
SV_DIR=/usr/share/scummvm/beneath-a-steel-sky
setup() {
	mkdir -p "${SV_CFG%/*}"
	printf '%s\n' "[scummvm]" "fullscreen=true" "stretch_mode=fit" "output_rate=22050" "subtitles=true" \
		"music_driver=adlib" "confirm_exit=false" "gui_return_to_launcher_at_exit=false" > "$SV_CFG"
}
sv_cmd() { CMD=(/usr/games/scummvm -f ${SV_GFX:+--gfx-mode="$SV_GFX"} --stretch-mode=fit -p "$SV_DIR" "$SV_ID"); }
sv_cmd
during() { [ "$SV_KEYS" = 1 ] || return 0; smoke_xkeys "$SV_TITLE" "$SV_KBD_WAIT" Escape +6 Escape; shot_mark; }
SHOT_FROM=mark	# the screenshots after the keys (the load's end in the variants without keys)
variant_queen() { SV_ID=queen; SV_TITLE="Flight of the Amazon Queen"; SV_DIR=/usr/share/scummvm/flight-of-the-amazon-queen; sv_cmd; }
variant_drascula() { SV_ID=drascula; SV_TITLE="Drascula"; SV_DIR=/usr/share/scummvm/drascula; sv_cmd; }
variant_lure() { SV_ID=lure; SV_TITLE="Lure of the Temptress"; SV_DIR=/usr/share/scummvm/lure-of-the-temptress/en; sv_cmd; }
variant_gl() { SV_GFX=opengl; sv_cmd; }
variant_intro() { SV_KEYS=0; }
variant_nosound() { CMD+=(--music-driver=null --no-sfx); }
