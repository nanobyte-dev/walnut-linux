# Recipes

One bash file per game, sourced by `client.sh` (variables and functions only, nothing runs at
source time). `client.sh <game> <variant>` sources `<game>.sh`, then calls `variant_<variant>`
if it exists (`base` needs none).

| Name | Meaning |
|---|---|
| `CMD=(...)` | the game command, absolute path |
| `ENV=(...)` | `NAME=value` for the game's environment |
| `SUITE=1` | part of `run.sh suite` |
| `COMM` | the game's process name (ps3rsx per-client VRAM, peak RSS) |
| `BACKUP=(...)` | files the game rewrites (configs): restored after every run |
| `setup()` | puts assets and config in place (`$ASSETS` is `games/bench/assets`) |
| `END_RE` | output line after which the game is stopped (games that don't quit) |
| `DURATION` | seconds from start after which the game is stopped (default 90: a game still running then is cut, and its fps comes from the HUD) |
| `FPS_RE`, `MAXMS_RE`, `LOAD_RE` | the first capture group of the last matching output line |
| `LOAD_MARK` | an output line whose time (seconds from start) is the load time |
| `HUD_SKIP` | HUD seconds left out as loading when there is no load time (default 12) |
| `RESULT_FILE` | files the game writes, copied into the run directory |
| `during()` | runs in the background while the game runs (its output in during.log), stopped at the end |
| `parse()` | prints extra `key=value` lines into bench.txt |
| `HUD` | 1 (default): Gallium HUD fps per second into `hud/fps` |
| `WM` | window manager started first (default `openbox`; empty: none, the game's window may not show) |
| `GAMEMODE` | 1: start through `ps3-game` (as the desktop does); default 0, as step 0 |
| `SOUND_AT` | seconds from start when the sound card state is read (default 15) |

`openal.inc` is not a recipe: the OpenAL Soft variants (`allog`, `b16`, `f44`, `b16f44`, `p1024`,
`alnull`) the OpenAL games' recipes share (docs/SPU-AUDIO.md, M0), with an optional perf profile
(`OAL_PERF=N`) that `games/bench/oal-perf.sh` splits by symbol.
