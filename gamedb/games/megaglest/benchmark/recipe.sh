# MegaGlest 3.13.0 (Debian ports ppc64: megaglest, megaglest-data 400 MB, wxWidgets for its tools): smoke
# test, not benchmarked. `--auto-test=SECONDS,,exit` plays the game's scenarios by itself (the AI on every
# side) and exits after them, a demo without a player. Settings: ~/.megaglest/glestuser.ini (written here,
# the folder restored after the run): fullscreen (Windowed=false) at 1920x1080.
# What it exercises: GL 2.x with optional shaders, FTGL text, the units' models (G3D), the RTS AI on the
# PPE, OpenAL. Risks: RAM (the data is 400 MB), the PPE with many units.
COMM=megaglest
. "$BENCH_LIB/smoke.inc"
MG_DIR=$HOME/.megaglest
BACKUP=("$MG_DIR")
setup() {
	mkdir -p "$MG_DIR"
	printf '%s\n' "Windowed=false" "ScreenWidth=1920" "ScreenHeight=1080" "CheckGlCaps=false" "${MG_INI[@]}" > "$MG_DIR/glestuser.ini"
}
MG_INI=()	# more glestuser.ini lines (variants)
CMD=(/usr/games/megaglest --auto-test=150,,exit)
# VRAM ran full (220 of 220 MB) and the VRAM swap thrashed (2026-10-29): Mesa's DXT at upload, and shadows off
variant_dxt() { ENV+=(NV30_FORCE_DXT=all); }
variant_lean() { ENV+=(NV30_FORCE_DXT=all); MG_INI+=("Shadows=Disabled"); }
