# Diablo on DevilutionX 1.5.5 (our devilutionx package, gamedb/games/diablo/packages/devilutionx; never
# public: its licence is not a free one) with the player's own DIABDAT.MPQ in
# ~/.local/share/diasurgical/devilution/ (the game's own folder, SDL's pref path for "diasurgical",
# "devilution": its settings and saves are there too; nothing of it is in the repository: its
# sha256 is in technical.md): smoke test, not benchmarked. `-n` skips the intro videos; then Return
# through the menus by XTEST (Single Player, New Hero, the Warrior, a typed name: DevilutionX fills one in only for a pad) into
# Tristram. The game's settings: diablo.ini in that folder (restored after the run;
# none written here: the package's defaults, fullscreen at the screen's size, the picture scaled up).
# What it exercises: SDL2 through sdl2-compat (a 32-bit texture of the 8-bit picture, scaled by the GPU),
# the MPQ reader and its decompression, SDL audio at 22050 Hz (patch 0001: host byte order), the save
# writer (an MPQ). Risks: colours (the palette to 32-bit path on big-endian), the save format, RAM.
COMM=devilutionx
. "$BENCH_LIB/smoke.inc"
BACKUP=("$HOME/.local/share/diasurgical/devilution/diablo.ini")
# the run's hero goes to a save folder of its own, made new every run (the player's saves untouched)
DX_SAVES=/var/tmp/ps3-bench/diablo-saves
setup() { rm -rf -- "${DX_SAVES:?}"; mkdir -p "$DX_SAVES"; }
CMD=(/usr/games/devilutionx --diablo -n --save-dir "$DX_SAVES")
DX_KEYS=(Return +4 Return +4 Return +4 W a l n u t +1 Return +5 Return +15)	# Single Player, New Hero, Warrior, a name (only a pad fills one in), OK, Normal: Tristram
during() { smoke_xkeys DevilutionX 12 "${DX_KEYS[@]}"; shot_mark; }
SHOT_FROM=mark
variant_menu() { DX_KEYS=(); SHOT_STATIC=1; }
variant_verbose() { CMD+=(--verbose); DX_KEYS=(); }
