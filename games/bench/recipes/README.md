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
| `DATA_DIRS=(...)` | the game's data directories (absolute): `GAMEDATA=ssd` (run.sh, an ENVS token) runs the game with each one bind-mounted from its SSD copy (`ssd-arm.sh copy GAME` makes it; docs/GAMEOS-HDD.md "Test drive, Phase A") |
| `setup()` | puts assets and config in place (`$ASSETS` is `games/bench/assets`) |
| `END_RE` | output line after which the game is stopped (games that don't quit) |
| `DURATION` | seconds from start after which the game is stopped: a safety cap (default 270: load + 60 s warm-up + 120 s measured + margin) |
| `WARMUP`, `MEASURE` | the steady state (the user, 2026-10-17): `WARMUP` seconds after the load ends are left out (default 60), then at least `MEASURE` seconds are measured (default 120). The load ends at the `LOAD_MARK` line or the first `LOAD_RE` line, else at the HUD's first frame |
| `PASSES` | 1: the game repeats its demo in one session and every `FPS_RE` line ends a pass (`MAXMS_RE` read from the same line). A pass counts when it starts at least 0.9 x `WARMUP` after the load; the game is stopped once the counted passes cover 0.95 x `MEASURE` (ended=window). `fps`, `maxms`, `load` are then the first pass's (the run as it was before) |
| `WINDOW_STOP` | 1: the game is stopped 2 s after the window (load + `WARMUP` + `MEASURE`), ended=window; for games measured by the HUD that don't end by themselves |
| `STEADY_RE`, `STEADY_MAXMS_RE` | the game's own result over its own window (first capture group of the first match): bench.txt `steady_fps`, `steady_maxms` |
| `WIN_ON_RE`, `WIN_OFF_RE` | the game marks its own window, maybe in pieces: each ON line opens a piece, the next OFF line closes it (a piece still open at the end closes there); replaces load + `WARMUP` for the HUD metrics (e.g. a demo's reload between loops left out) |
| `FPS_RE`, `MAXMS_RE`, `LOAD_RE` | the first capture group of the last matching output line |
| `LOAD_MARK` | an output line whose time (seconds from start) is the load time |
| `HUD_SKIP` | HUD seconds left out as loading when there is no load time (default 12) |
| `RESULT_FILE` | files the game writes, copied into the run directory |
| `during()` | runs in the background while the game runs (its output in during.log), stopped at the end |
| `parse()` | prints extra `key=value` lines into bench.txt |
| `HUD` | 1 (default): Gallium HUD fps per second into `hud/fps` and every frame's time (ms) into `hud/frametime_(ms)` |
| `WM` | window manager started first (default `openbox`; empty: none, the game's window may not show) |
| `GAMEMODE` | 1: start through `ps3-game` (as the desktop does); default 0, as step 0 (the game gets `PS3_GAME_MODE=off`, so ps3-game-detect leaves it alone too) |
| `SOUND_AT` | seconds from start when the sound card state is read (default 15) |
| `SHOT_POINTS` | the screenshots' points (whole seconds after `SHOT_FROM`) for `SHOTS=auto`, the smoke default (default `30 90`); docs/games/SUITE.md "Screenshots" |
| `SHOT_FROM` | what the shot points count from: `load` (default: the load's end as above), `mark` (when `during()` calls `shot_mark`, e.g. after its keys; the load's end if `during()` ends without it), `start` (the client's start) |
| `SHOT_RE` | the shot points count from the first output line matching it (sets `SHOT_FROM=re`; Warzone 2100: the match's start) |
| `SHOT_STATIC` | 1: the shots are of a static screen (menu, title): the sheet says a close comparison is fair (the `menu` variants) |
| `REF_PKGS`, `REF_IMAGE`, `REF_ENV` | the PC reference (tools/bench-ref.sh): Debian packages with the game, or a local image with our amd64 build of it; extra `NAME=VALUE` for the PC run |

Each recipe's header says how its warm-up and window work (passes, the game's own window, or the
HUD window, and what is not identical between runs). `bench.txt` gets `warmup`, `measure`,
`load_end` and `hud_t1` (seconds from the start: the load's end, the HUD's first value), `window`
(start and end of the measured window), `passes`, `measured` and `pass<N>` lines (`start end fps
maxms counted`) for report.py. `python3 games/bench/selftest.py` checks client.sh, report.py and
matrix.py with a fake game and made-up runs, and sources every recipe and variant.

`openal.inc` is not a recipe: the OpenAL Soft variants (`allog`, `b16`, `f44`, `b16f44`, `p1024`,
`alnull`) the OpenAL games' recipes share (docs/SPU-AUDIO.md, M0), with an optional perf profile
(`OAL_PERF=N`) that `games/bench/oal-perf.sh` splits by symbol.

`smoke.inc` is not a recipe either: the shared header of the smoke recipes (`SUITE=0`, a 120 s
`DURATION` that `SMOKE_SECS` overrides, a short HUD window, `vblank_mode=0`). A smoke recipe is a
game tried once to see whether it starts and plays, not measured (docs/games/smoke-tested.md): `run.sh smoke <game> [variant]`. Current ones: teeworlds, torcs, supertux,
trigger-rally, redeclipse, chromium-bsu, frozen-bubble, chocolate-doom, crispy-doom, woof, pingus,
scummvm (Beneath a Steel Sky; variants queen, drascula, lure), simutrans, crawl-tiles, freeciv (gtk3, an AI game it observes), lincity-ng,
flare, widelands, warmux, cataclysm-tiles, wesnoth (tutorial; variants httt, ai, menu, nosound, window), warzone2100 (AI-vs-AI autogame at 1x; variants lobby, menu, nomusic, nosound), ddnet, opentyrian, uqm, hedgewars, openrct2 (Electric Fields at 960x540 x2; variants full, big, title, nosound, lean, sdlsw), endless-sky (a saved pilot, e d Return and Up by key into flight in Rutilicus; variants menu, cheap, nodefer, full, threads4).
