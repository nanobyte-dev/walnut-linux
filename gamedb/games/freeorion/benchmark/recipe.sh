# FreeOrion 0.5.1.2 (Debian ports ppc64: freeorion, freeorion-data 155 MB): smoke test, not benchmarked.
# `-q` (quickstart) starts a new game with the default galaxy and AI empires, bypassing the menus; `-f`
# fullscreen; `--auto-advance-n-turns N` presses "Turn" itself for the first N turns, so the AIs (Python,
# in their own processes) and the server work as in play. Settings and saves: ~/.config/freeorion and
# ~/.local/share/freeorion (both set aside and restored by BACKUP).
# What it exercises: GL 2.1 with GLSL (the galaxy map, the star and fleet sprites), the server and the
# AI processes on the PPE (three processes or more: RAM), Boost.Python. Risks: RAM, the AI turn time.
COMM=freeorion
. "$BENCH_LIB/smoke.inc"
# BACKUP takes files, not folders
BACKUP=("$HOME/.config/freeorion/config.xml" "$HOME/.config/freeorion/persistent_config.xml")
FO_TURNS=3
fo_cmd() { CMD=(/usr/games/freeorion -f -q --auto-advance-n-turns "$FO_TURNS"); }
fo_cmd
