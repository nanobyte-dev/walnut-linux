# The game suite: how it is run

`games/bench/` runs the games on the PS3 the same way every time and turns the runs into comparable
rows. One command does a first launch (`smoke`), a measured A/B (`bench`) or a round over every
suite game (`suite`); each game is a recipe file. The results go into the matrices in
`games/bench/results/`. What the numbers say per game is in [README.md](README.md); the stall
recorder, the profiles and the memory snapshots have their own docs ([STALLS.md](../STALLS.md),
[PROFILING.md](../PROFILING.md), [MEMORY.md](../MEMORY.md)).

## What is in games/bench

| Path | What |
|---|---|
| `run.sh` | the runner, on the PS3 as tibi over SSH: `smoke`, `bench`, `suite`, `list`, `session` |
| `session.sh`, `session-form.py` | a played session ([below](#played-sessions)) and its end-of-session form |
| `freshhome.py`, `inputlog.py`, `geom.py` | around a session: the game on fresh defaults (the player's directories set aside and back), the input devices, the window and the pointer |
| `client.sh` | one game run from a recipe (the X client `ps3-bench.sh x` starts) |
| `recipes/<game>.sh` | one per game; `recipes/README.md` lists the variables; `smoke.inc` and `openal.inc` are shared pieces |
| `assets/` | demos, replays, maps and mods: `openarena-bench.dm_71`, `sauerbraten-bench.dmo`, `xmoto-credits.rpl`, `armagetronad-bench.aarec`, `etr-race.evemu`, `classicube-bench.cw`, `.path`, `.so`, `luanti-ps3bench/` |
| `classicube-bench/` | ClassiCube's timedemo plugin (`ccbench.c`), built by `make classicube-bench` into `assets/classicube-bench.so` |
| `report.py` | one line per run, then the median per game and variant, then each variant against the first as ratios; `--stalls`, `--perf`, `--mem`, `--load`, `--include-interfered` |
| `matrix.py` | puts runs into the matrices: one CSV per metric, one row per tested configuration |
| `results/` | the matrices; `results/README.md` says how to read and add to them; `interfered.txt` lists runs left out |
| `stalls.py`, `passsplit.py` | why frames stall, from `ps3-stallrec`'s record; the stalls of a looped demo split into reload and play |
| `perf-post.sh`, `perfprof.py`, `memprof.py`, `loadprof.py` | the CPU profile of the window, the memory snapshots, the start-up profile |
| `hotpaths/` | perf.data symbolised and ranked on the PC ([HOTPATHS.md](../HOTPATHS.md)) |
| `interference-scan.py`, `intfcheck.py` | the retro-scan for runs before the interference detector; its selftest part |
| `logscan.py` | the errors and warnings in the runs' logs, grouped by game and message, with a new-since flag ([LOG-FINDINGS.md](LOG-FINDINGS.md)) |
| `selftest.py` | the harness without the PS3: client.sh, report.py and matrix.py with a fake game and made-up runs, and every recipe and variant sourced (`make test ONLY=bench-selftest`) |
| `gamemode-check.sh` | game mode on the desktop: memory idle, in game mode and after ([DESKTOP.md](../DESKTOP.md#game-mode)) |
| `ssd-arm.sh` | the SSD arms `GAMEDATA=ssd`, `SWAP=ssd` ([GAMEOS-HDD.md](../GAMEOS-HDD.md)) |
| `oal-perf.sh` | OpenAL Soft's mixing split from a profiled run ([SPU-AUDIO.md](../SPU-AUDIO.md)) |
| `xkeys.py` | keys through XTEST, for games that ignore the virtual keyboard |
| `shots.py` | the screenshots ([below](#screenshots)): `cost` (what a capture cost in frames), `convert` (xwd to PNG), `sheet` and `post` (the PS3 \| PC sheet), `pending` (sheets nobody reviewed) |

`make bench-push` copies `games/bench` to `/var/tmp/bench` on the PS3 (unpacked beside the old copy
and swapped in with a rename, so a run in progress keeps its files). `make bench-pull` fetches the
run directories from `/var/tmp/ps3-bench` into `build/bench/`; `PERFDATA=1` adds perf.data (about
20 MB a run), perf-load.data and perf-syms.tar. It also turns the runs' screenshots into PNGs and
makes their PS3 | PC sheets ([Screenshots](#screenshots)). `games/openarena/` keeps step 0's scripts
(`step0.sh`, `step0-report.py`, `oa-*.sh`).

## run.sh

    run.sh smoke GAME [VARIANT]        a first launch: VRAM swap off (NOVSWAP=0 keeps it), 360 s at
                                       most; prints the result and the kernel's and game's errors
    run.sh bench GAME "VARIANTS" [N]   N rounds (default 3), the variants interleaved in each round
    run.sh suite "VARIANTS" [N]        the same over every recipe with SUITE=1, games interleaved
                                       (a game without the variant runs base once per round)
    run.sh list                        recipes and their variants

Variant `base` is the recipe as written. Every run goes through `ps3-bench.sh x`, which stops the
desktop, empties the swaps, samples the system, runs the thermal watchdog, and starts a bare X with
`client.sh` and the recipe. Results land in `/var/tmp/ps3-bench/<stamp>-<game>-<variant>/`. End a
session with `ps3-bench.sh desktop`.

**`ENVS="TOKEN..."`** runs every variant once per token in each round, the tokens interleaved; a
token is `-` (nothing) or `NAME=VALUE` pairs joined by `+`. `ROTATE=1` rotates the tokens each round
against order bias. What a token can set:

- harness switches (the `BENCH_` prefix is added): `STALLS=1` the stall recorder; `PERF=1` a CPU
  profile of the window (`PERF_HZ`, default 499; `PERF_ALL=1` adds a system-wide record); `MEM=1`
  memory snapshots at the window's start, middle and end; `PERF_LOAD=1` a profile of the start-up
  instead, with the disk, VM and game counters and ld.so's statistics (`LDSTATS=0` without);
  `WAKER=MS` with `WAKER_MODE=sleep|kick` and `WAKER_CPU=LIST`, `PROBE=1` (ps3-waker);
  `STALLS_MS`, `STALLS_CPU`, `STALLS_NOWRITE=1`. See [STALLS.md](../STALLS.md) and
  [PROFILING.md](../PROFILING.md); profiled runs are for profiles, not for fps.
- `MALLOC=arena1|arena2|mmap|trim|tune` glibc malloc presets for the game, `MALLOC_TRIM_S` the trim
  period ([MEMORY.md](../MEMORY.md)).
- `GAMEPREFETCH=fetch|learn|on` (`GAMEPREFETCH_MB`, `_RESERVE_MB`, `_IO`): the game's own files read
  at its start; one `learn` run first ([STALLS.md, "Game prefetch"](../STALLS.md#game-prefetch)). Measured no gain,
  so off by default.
- `SHADERCACHE=cold|warm|off`: Mesa's disk shader cache for the run (the user's own cache untouched).
- `GAMEDATA=ssd`, `SWAP=ssd`: the recipe's `DATA_DIRS` bound from the SSD copy, the VRAM swap's spill
  on the SSD; `ssd-arm.sh` puts each arm in place before the run's quiet and resets to USB at the end.
- `SHOTS="30 90"`, `SHOTS=auto|off`: screenshots at those seconds after the recipe's game state
  ([Screenshots](#screenshots)); on (`auto`) for smoke runs, off for bench and suite runs unless given.
- `NV30_X=V`: a Mesa driver switch for the game; `PS3RSX_NAME=V`: a ps3rsx module parameter, written
  for that run and put back (`sudo -n`).
- Library switches ([SPU-AUDIO.md](../SPU-AUDIO.md), [PROFILING.md, "Load-time decoders and
  linking"](../PROFILING.md#load-time-decoders-and-linking)): `OAL=b16` or `OAL=b16,lin` OpenAL Soft's
  settings by `recipes/openal.inc`'s names on any variant of any game; `FLUIDFX=off|norev|nocho|cho1|on`
  FluidSynth's reverb and chorus whatever the game asks for; zlib-ng's `libz.so.1` in front (package
  `libz-ng-compat`) by default as in `ps3-game`, `ZLIB=stock` without it (bench.txt `zlib=`); `LDPATH=DIR` a directory in front of the game's `LD_LIBRARY_PATH` (copies of
  the libraries a package of ours replaced, for a same-boot A/B against the old version); `BINDNOW=1` (`LD_BIND_NOW`); `JSIMD=none|altivec` libjpeg-turbo's SIMD.
  A switch the installed library can't honour stops the run.

report.py shows each run's switches after its variant (`base@rec10`, `base@wkkick10`,
`base@translate_lut0`, `base@mtune`, `base@oalb16`, `base@fxoff`, `base@zng`, `base@gpffetch`, `base@sccold`, `base@gdssd+swssd`,
`base@rsx_name<V>`, `base@shot` for a run with a screenshot inside its window), so they never group
with plain runs.

**Interference.** `ps3-bench.sh`'s detector (`ps3-intfwatch`) flags a run somebody disturbed: input,
a VT switch, a device, an SSH login or command. run.sh warns after it, report.py marks it and leaves
it out of the medians, matrix.py skips it; `RERUN_INTERFERED=1` runs it once more at once (both
directories stay; the clean one counts).

## Recipes

A recipe is sourced bash, variables and functions only. `client.sh <game> <variant>` sources it and
calls `variant_<variant>` if it exists. The full list is in `games/bench/recipes/README.md`; the
ones that shape a run:

- `CMD=(...)`, `ENV=(...)`, `SUITE=1`, `COMM` (the process name for ps3rsx's per-client VRAM and the
  peak RSS), `BACKUP=(...)` (config files the game rewrites, restored after every run),
  `DATA_DIRS=(...)`, `setup()`, `during()` (in the background while the game runs), `parse()`
  (extra `key=value` lines into bench.txt).
- How the result is read: `FPS_RE`, `MAXMS_RE`, `LOAD_RE` (the last matching line), `LOAD_MARK` (a
  line whose time is the load time), `STEADY_RE`, `RESULT_FILE`, `HUD=1` (the Gallium HUD's fps per
  second and every frame's time), `HUD_SKIP`.
- How it ends: `END_RE` (stop after this line), `DURATION` (a safety cap, default 270 s),
  `WINDOW_STOP=1` (stop 2 s after the window), `PASSES=1` (the game repeats its demo; see below).
- `WM` (default `openbox`: SDL fullscreen windows never show without a window manager), `GAMEMODE=1`
  (start through `ps3-game` as the desktop does; default 0, the game gets `PS3_GAME_MODE=off`),
  `SOUND_AT` (when the sound card state is read, default 15 s).
- The screenshots: `SHOT_POINTS`, `SHOT_FROM`, `SHOT_RE`, `SHOT_STATIC`, `shot_mark` in `during()`, and
  the PC reference's `REF_PKGS`, `REF_IMAGE`, `REF_ENV` ([Screenshots](#screenshots)).

**The steady state** (the user, 2026-10-17). Every run leaves out a warm-up (`WARMUP`, 60 s after the
load ends) and measures at least `MEASURE` (120) s after it. The load ends at the `LOAD_MARK` line,
the first `LOAD_RE` line, or else the HUD's first frame. A game that repeats its demo (`PASSES=1`)
prints one line per pass; a pass counts when it starts at least 0.9 x `WARMUP` after the load, and
the game is stopped once the counted passes cover 0.95 x `MEASURE`. `fps`, `maxms`, `load` are then
the first pass's, as the run was before. A game can mark its own window with `WIN_ON_RE` and
`WIN_OFF_RE` (BananaBread (Debian's cube2) leaves its demo reload out that way). Each recipe's header says which way
it works and what isn't identical between runs. bench.txt gets `warmup`, `measure`, `load_end`,
`hud_t1`, `window`, `passes`, `measured` and `pass<N>` lines for report.py. This replaced the first
harness's 90 s cap and its 12 s HUD skip.

## Per game: command, end, result

| Game | Command (recipe) | Ends | Result read from | Display, input |
|---|---|---|---|---|
| OpenArena | `/usr/games/openarena +set cl_renderer opengl1\|opengl2 +set r_ext_compressed_textures 0\|1 +set r_swapInterval 0 +set com_maxfps 0 +set timedemo 1 +vstr bench_loop`, cvars in autoexec.cfg | passes | `N frames S seconds F fps min/avg/max/`, `CL_InitCGame: N seconds` | `r_mode -2 r_fullscreen 1`; DS3 through SDL GameController (bound in autoexec.cfg) |
| QuakeSpasm | `quakespasm -nopackedpixels -width 1920 -height 1080 -fullscreen` with our `~/.quakespasm/id1/quake.rc` | passes | `N frames S seconds F fps` | gamepad unverified |
| Yamagi Quake II | `/usr/lib/yamagi-quake2/quake2 +timedemo 1 +demomap q2demo1.dm2` | passes | `N frames, S seconds: F fps` | |
| dsda-doom | `dsda-doom -iwad freedoom2.wad -vidmode sw\|gl -width W -height H -fullscreen -timedemo demo1`, `DSDA_BENCH_LOOPS=6` | passes | `Timed N gametics in N realtics = F frames per second` | SDL GameController |
| X-Moto | `xmoto --nowww -fs -res 1920x1080 --replay credits --timedemo --benchmark`, `XMOTO_BENCH_LOOPS=2` | passes | `Average framerate: F fps` | |
| Neverball | `neverball --replay gui/demo1.nbr`, `NEVERBALL_BENCH=1 NEVERBALL_BENCH_LOOPS=20` | passes | our bench line per pass | SDL joystick works |
| Extreme Tux Racer | the recorded race replayed through evemu | `WINDOW_STOP` | HUD | the race is the user's |
| Armagetron | live AI rounds, the player spectating, keys through a virtual keyboard | `WINDOW_STOP` | HUD | |
| ClassiCube | the game with `CC_BENCH` and the ccbench plugin | the plugin quits | the plugin's fps per pass | |
| BananaBread (the recipe is still called `sauerbraten`) | `/usr/games/cube2` with the demo and a CubeScript frame counter (`-x`) | the script quits | `ps3bench: loop`, `ps3bench: steady` | no gamepad |
| Luanti | `luanti --go --world ... --config ...` with the ps3bench mod | `ps3bench: path end` | HUD from `ps3bench: path start` | joystick (`enable_joysticks`) unverified |
| SuperTuxKart | `supertuxkart --fullscreen --screensize=1920x1080 --track=lighthouse --numkarts=4 --profile-time=180 --no-start-screen`, `STK_PROFILE_WARMUP=60` | the game quits | the HUD (its `Average FPS: F` is the physics tick rate, [supertuxkart.md](supertuxkart.md#the-two-frame-rates)) | SDL GameController |
| OpenTTD | `/usr/games/openttd -x -c CFG -r 1920x1080 -v sdl\|sdl-opengl -d sl=1`; `sim`: `-g -G 12345 -v null:ticks=20000 -s null -m null` | `WINDOW_STOP`; sim exits | HUD (gl), none (software), wall time (sim) | no gamepad |

The GPU timers of any game's own profiler are garbage on the RSX: nv30's TIMESTAMP and TIME_ELAPSED
queries return a pixel count.

## A session on the PS3

The console's rules are in [CONSOLE.md](../CONSOLE.md#a-session): webMAN's fan on SYSCON (or at
least 60%) before the boot flag goes to OtherOS, the user boots through Petitboot, and the flag goes
back to GameOS at the end. For bench runs:

- Check first that nobody is using the PS3. No deploys and no commands on it during a run (they
  are interference: disk, swap-ins); `ps3-bench.sh` holds a lock in `/dev/shm` (a reboot clears it).
- Long batches start under `nohup` or `systemd-run --user`, so a dropped SSH connection doesn't end
  them. The picture check is the screenshots ([below](#screenshots)): look at each run's sheet before
  trusting it. `tools/bench-cam.sh` on the PC still photographs the TV 30 s and 60 s into every run
  over one SSH connection, for the TV itself (the HDMI output, a frozen console) and the sound clips
  (`tools/bench-audio.sh`). (The first watcher opened an SSH connection every 5 s and slowed the runs
  it watched: load average 5 at idle. Those runs of 2026-10-17 13:05-14:50 UTC were discarded.)
- A graph exception ends the session: only a console reboot clears the RSX, and the user goes back
  through Petitboot. Within a session the riskiest game goes last. After a wedge, don't run
  `ps3-bench.sh desktop` (it starts X on the wedged RSX). Each game's config backups are on disk
  (`/var/tmp/ps3-bench/backup/<game>`) and its next run restores them first.
- Compare load times only with the shader cache in the same state.
- `perf` on the PS3 needs `--proc-map-timeout=20000`, or it starts sampling before it has read a big
  process's maps and files most samples as `[unknown]` (77% of OpenArena's once).
- After the batch: `make bench-pull` (PNGs and sheets), the sheets reviewed (`make bench-shots` lists
  the open ones), `report.py`, then a row in the matrices.

## Played sessions

The graded test of [GAMEDB.md](../GAMEDB.md#how-a-game-is-tested): a person plays the game for
about ten minutes, started the way the desktop menu starts it. Smoke and bench runs stop the
desktop, empty the swaps and switch vsync off; a session does none of that.

**Start one** on the PS3, from a terminal of the desktop (over SSH works too: one command, then hands off):

```
/var/tmp/bench/session.sh GAME [LAUNCHER] [VARIANT]   # or: /var/tmp/bench/run.sh session GAME ...
```

**Play it on the PC alongside.** `tools/play-pc.sh GAME [LAUNCHER]` starts the same game and launcher on
the PC, from Debian at the PS3's snapshot (the game's Walnut Games `packages`), in a container on the
desktop's X server with Mesa's llvmpipe held to GL 2.1 (the RSX's rendering path) and sound through
the desktop's PulseAudio/PipeWire. The player plays both side by side: the PC is the reference for the
picture and behaviour, only the PS3 is measured. The game's PC home stays in `build/pc-play/GAME`.
`PLAY_PC_LIST=1` lists the launchers.

**Both sides from one command.** `tools/play-test.sh start GAME [LAUNCHER]` starts the PS3 session
(first: it counts the load) and the PC copy; `PC=0` for the PS3 only. `play-test.sh stop GAME` ends a
false start (the run stays on the PS3, unimported), `play-test.sh check` says whether the PS3 is idle
with the harness and the dropout logger in place, and `play-test.sh collect GAME` pulls the runs and
lists the game's sessions (end, play time, launcher, sound dropouts, the form's answers, imported or
new).

**Pick the launcher.** A game can have several menu entries (OpenArena: `openarena-gl1.desktop`
"OpenArena (GL 1.2)" and `openarena-gl2.desktop` "OpenArena (GL 2.0)"). `LAUNCHER` is an entry's file
name or its Name; the session starts exactly that entry's `Exec` (field codes cut out, no shell), so
the game gets the arguments the menu gives it. The recipe still supplies the assets, `PLAY_MARK`,
the HUD and the recording. The launchers are found as the Walnut Games app finds them
(`launcher.py`): the menu entries the game's packages ship (`dpkg -L`) and any shown entry that runs
one of their programs. Without `LAUNCHER` the session takes the game's only launcher, or the first of
several (shipped entries by file name) and prints the list; an unknown `LAUNCHER` prints the choices
and exits 2. A game with no menu entry runs the recipe's command. A second argument that names a
recipe variant (`base`, `kick100`) is still taken as the `VARIANT`; with a launcher the variant is the
third argument. `SESSION_DRY=1` shows the launcher and its command without starting anything.
`bench.txt` records `launcher=` (the Name) and `launcher_file=`; `import-run.py` writes the Name as
`launcher` in `result.toml` (`--launcher NAME` for a run that recorded none). Each launcher is rated
on its own: play every launcher of a game.

`make bench-push` puts the scripts there; the harness needs a `ps3-gpu-tools` with
`ps3-bench.sh session`. `run.sh list` names the games. A recipe needs a playable command: openarena
and xmoto have a `session()` function for it, and the `menu` variants (`warzone2100 menu`) start at
the game's own menu; a timedemo or autoplay recipe without one would run its bench command
([recipes/README.md](recipes/README.md): `session()`). The script moves into `ps3-game.slice`
(game mode freezes the terminal) and logs to `~/.cache/ps3-session/`. Nothing else is needed from
the terminal: play, then quit the game from its menu. There is no time limit. Don't use the PS3
for anything else meanwhile, and no SSH: a login is interference for what is measured.

**Marking a screenshot.** To show a glitch (or anything) during play: press **Alt+F12** on the keyboard,
or **Select and Start together** on the gamepad (within 0.3 s). The session takes a full-resolution
screenshot at that moment, the same capture as the timed ones (`shots/mark-<t>s.xwd`, `<t>` the seconds
after the start; bench.txt `shot_mark=<t>s:<file>:<capture ms>`), at least 2 s apart, at most 30 a
session (`BENCH_MARK_MAX`). There is no overlay on the game; a sound is possible by pointing
`SHOT_MARK_SOUND` at a wav file (off by default). A capture costs a stall (it reads video memory), so
the sound dropouts within -0.5 s to +2 s of it are counted as the recorder's, like a timed shot's.
Nothing is grabbed: **the game gets the keys too**, so a game that uses Alt+F12 or Select+Start itself
will see them (rare; the shot is taken anyway). Nothing on Walnut binds Alt+F12 (the Openbox keys, tint2
and the rest were checked on 2026-10-26; Print Screen stays the desktop's own scrot). The marked shots
are the player's evidence: `import-run.py` always publishes them in the game's gallery before the
representative timed ones (four pictures in all, more only when the marks alone exceed it: all of them up
to six, `ps3-mark-<t>s.jpg`), and `review.py` puts them first on the review sheet as "Player's shot"
with a line in REVIEW.md.

**What runs.** The game goes through `ps3-game` (game mode, the VRAM swap on, vsync as shipped, the
desktop's window manager), recorded by `ps3-bench.sh session` with `client.sh` (`BENCH_SESSION=1`):

| Recorded | Where |
|---|---|
| Gallium HUD frame times and fps (GL games) | `hud/`; `window=` is the play, for report.py |
| memory, swap and VRAM samples every 0.5 s; temperatures; dmesg; the pre/post snapshots | `samples`, `temps`, `dmesg.log`, `snap-*.txt` |
| sound: `underruns`, `oal_wedge`, `oal_wait`, `oal_xrun`, `snd_stall`, `snd_restarts`, `sound` (the card at 15 s) | `bench.txt` |
| sound dropouts, measured ([below](#sound-dropouts)): `xrunlog`, `xruns_logged`, `xruns_audible`, `xrun_gap_ms_total`, ... | `bench.txt`, `xruns.tsv`, `xruns.log` |
| a screenshot about every minute (xwd at nice 19; `SESSION_SHOT_EVERY`, at most `SESSION_SHOT_MAX`=40, 8 MB each), and the player's own on Alt+F12 or pad Select+Start (`mark-<t>s`) | `shots/`, `shot=`, `shot_mark=` |
| the game's window and the screen at each screenshot (`geom.py`: xwininfo and xprop from x11-utils, by the game's pid and its children; Display, [GAMEDB.md](../GAMEDB.md#how-a-game-is-rated)), and the X pointer: visible over the window or not (XFixes' cursor image through ctypes, libXfixes comes with Mesa; its theme name); a recipe may log `render_res=WxH` in bench.txt (recorded only) | `shots/geometry.tsv` (`at_s x y w h fullscreen screen_w screen_h by cursor cursor_name`) |
| the input devices (`inputlog.py`, started by client.sh at nice 19, as the player: the pads through logind's uaccess, keyboards and mice with the `input` group; a class it can't read is named in its header and its items stay untested): each device present or added with its class (pad, keyboard, mouse, motion, other), which ones the game or its children opened (`/proc/<pid>/fd` every 2 s, `jsN` counted as its `eventN`), every press at the kernel's timestamp (`key` rows: the level load ends at the play mark), presses and axis moves per class per 10 s. Read-only, never grabs. It records presses and times, never which key, with one exception: the two screenshot chords ([Marking a screenshot](#played-sessions)). For those it holds the state of Alt, F12, Select and Start (nothing else) and writes only a `mark` row with the chord's time, never a code; it also appends the time to `.mark-req`, which client.sh turns into a shot. `BENCH_INPUTLOG=0` leaves it out (and the marking with it) | `input.tsv`, `input.err` |
| the game on fresh defaults (`freshhome.py`, from session.sh): the recipe's `SESSION_HOME` directories (else the top directories of its `BACKUP` files) renamed to `<dir>.walnut-aside-<stamp>` before the game, the player's own renamed back after it; the session's fresh ones to the run directory up to 5 MB, else renamed to `<dir>.walnut-fresh-<stamp>`; never a delete, never a write over anything. The plan is written first (`~/.cache/ps3-session/freshhome/`), so a session cut short is put back at the next session's start, with a warning. `SESSION_FRESH=0` keeps the player's settings | `fresh-home/`; bench.txt `fresh_home`, `fresh_home_new`, `recovered`, `fresh_home_warn` |
| the desktop's return after the quit (session.sh watches `ps3-desktop.slice`'s `cgroup.freeze` once a second, builtins only): seconds from the game's end to the thaw; 0 when game mode never froze it | bench.txt `desktop_back_s`, or `desktop_stuck=1` |
| peak RSS, game output, package versions | `hwm`, `game.log`, `versions` |
| how it ended | `ended`, `ended_why`, `rc` |
| the player's answers | `session.toml` |

The thermal watchdog and the 80 C limit still stop the game; the interference detector and the
bench screen are off (the player's input is the point).

`bench.txt` has the usual fields plus `kind=session`, `play_from` (`mark`, `load` or `start`),
`play_start`, `play_seconds`, `vsync=shipped`, `gamemode=1`. The play starts at the recipe's
`PLAY_MARK` (the first gameplay frame), else at its load mark; without either `play_from=start` and
`play_seconds` is the whole run, an upper bound. `ended` is `quit` (exit code 0), `error` (another
code), `crash` (a signal, or a segfault of the game in `dmesg.log`), `oom` (the kernel killed it),
`closed` (SIGTERM or SIGKILL, Ctrl+Alt+Esc), `killed` (the harness stopped it: thermal alert,
temperature limit).

**Settings: none changed.** The game runs on its shipped defaults (freshhome.py, above) and the
player changes no setting; a game that can't be played without changing one goes in the notes.

**The form.** When the game exits and the desktop is back, a Tk dialog (Walnut colours, large type
for the TV, one screen) asks only what the recorder can't measure, one tap each, under a minute:
sound effects heard (yes, partly, no) and music heard (yes, no, no music in this game), both without
a default; then, with defaults, visual glitches (didn't notice any, minor, broken), the gamepad in
the menus (worked, partly, didn't, didn't try), the gamepad in play (seamless, needed setup, didn't
work, didn't try), the keyboard and mouse (worked, something didn't, didn't use), menus and modes
you opened (worked, something didn't), saving and loading (worked, didn't, didn't try); and a notes
box. A pad turned on mid-game and extra pads are never asked (one player, one DualShock 3 that can't
easily be turned off): scripted runs, below. Save writes `session.toml` (`form_version = 2`,
`sound`, `music`, `glitches`, `pad_menus`, `pad_play`, `keyboard_mouse`, `menus_modes`, `saves_load`,
the derived `controls`, `notes`, `saved`, `form`); Skip or closing the window writes nothing. `form_version = 2` files the session under rules 2.0; a
`session.toml` of the first form (sound, music, controls, notes) imports under 1.0 as before. The
notes are read with the numbers when the session is imported: what didn't work, a glitch in one
level, a sound problem become known issues and the reviewer's `broken:` lines. Needs `python3-tk`
on the PS3 (without it, or without a display, the form asks on the terminal, Enter taking the
default). `SESSION_FORM=0` leaves it out; `python3 -I session-form.py RUN_DIR GAME VARIANT` asks later.

**The review**, by the agent that imports the run, after `make bench-pull`:
`python3 -I tools/gamedb/review.py build/bench/<run>` writes into the run directory `shots-vs-pc.png`
(the PS3 screenshots next to the PC reference where one exists), `review-text.png` (1:1 crops of the
corners and the middle of two screenshots, each with a pixel ruler: ticks every 2 px, 10, 18 and 30 px
marked) and `REVIEW.md`, a fixed questionnaire with every answer `?` (an existing REVIEW.md is kept: the
template goes to `REVIEW.template.md`). The agent looks at the pictures and answers every line, never
by pixel metrics; import-run.py refuses a questionnaire with an answer left `?` or missing:

| Question | Answers |
|---|---|
| `verdict:` | `ok`, `wrong` (needs a defect), `unclear`, then words |
| `stretched:` stretched or squashed, circles and characters out of proportion (bars are fine) | `no`, or `yes: where`; the measured black bars are printed above it as a hint |
| `text_px:` the smallest menu or HUD text, in px at 1080p, off the ruler | `N: where`, or `none` |
| `pointer:` a mouse pointer over the game | `visible` or `hidden`: prefilled with the measurement, confirm or override |
| `garbage:`, `missing:`, `colours:`, `flicker:`, `lighting:`, `softer:` one line per defect category | `no`, or `pc: where` (the PC reference shows it too) or `ps3: where` (PS3/Walnut only); repeat a line for another place |
| `broken:` anything broken on screen or in the notes | `none`, or `<item> <pc\|ps3>: <what>`, a line each |

Optional lines: `check: <item> yes|partly|no|na` for an item the reviewer could tell (`online`), and
`defect: <category> <pc|ps3>: <what>`. The items are `pad_play`, `pad_setup`, `pad_menus`,
`pad_hotplug`, `pad_players`, `no_keyboard`, `keyboard_mouse`, `menus_modes`, `saves_load`, `online`
([GAMEDB.md](../GAMEDB.md#how-a-game-is-rated)).

**Collect** with `make bench-pull` (the screenshots become PNGs); the run directory is
`/var/tmp/ps3-bench/<stamp>-session-GAME-VARIANT/`, and `tools/gamedb/import-run.py` reads it.

### Scripted runs (planned)

What one player can't test in a session, run on the PS3 without the player (never during a played
session), as tests of their own (`kind = "script"`, their checklist answers in result.toml `[checks]`,
which score.py already merges with a launcher's other answers):

- **A virtual DualShock.** `uinput` is a module in our kernel (`CONFIG_INPUT_UINPUT=m`); python3 can
  drive `/dev/uinput` with `fcntl.ioctl` and `struct` alone (no python3-evdev in the image), as root:
  a device with the DualShock 3's name, ids and buttons, so SDL's mapping database treats it as one.
  Checks: create it after the game started and see the game open it (`pad_hotplug`); create a second
  and third, press Start on each and compare screenshots before and after (`pad_players`, judged by
  the reviewer); on the main menu press the d-pad down and confirm, and see the screenshot change
  (`pad_menus`).
- **Soaks.** A recipe's demo or attract loop left running for hours with nobody playing: crash-free
  hours for Stability.

Not built yet (PLAN.md, P5b): a runner (`script.sh GAME`) on top of client.sh, the uinput helper,
and per-recipe menu keys.

## Sound dropouts

Every harness run (benchmarks and played sessions) measures the moments the game's sound output
ran dry. Counting ALSA's "underrun occurred" lines told nothing about what was heard: an X-Moto
session counted 92 in six minutes while the player heard clean sound and music. What decides it is
how long the output stayed dry (a couple of ms is inaudible, tens of ms is a click or a chop) and
whether sound was playing around it.

**How sound flows on Walnut.** No sound server: PulseAudio was removed for memory, and PipeWire
isn't running (SDL tries its PipeWire backend first, hence the `pw.conf ... client.conf` lines in
`game.log`, then falls back to ALSA). Games reach ALSA through SDL or OpenAL Soft, both of which load
`libasound.so.2` with `dlopen` and fetch every function with `dlsym` on that handle, so an
`LD_PRELOAD` of `snd_pcm_*` never sees their calls (and interposing `dlsym` itself breaks the
`RTLD_NEXT`/`RTLD_DEFAULT` lookups of every other library, which glibc resolves by the caller). They open
`default`, which alsa-lib's `PS3.conf` makes `plug` -> `softvol` -> `dmix:CARD=PS3,FORMAT=S16` at
48 kHz -> `snd_ps3`. dmix keeps the card running with its stop threshold at the boundary, so the
card itself never underruns: the underruns are the game's own dmix client running dry, which is
why the kernel's `xrun_debug` can't see them.

**The measurement.** `ps3-xrunlog.so` (ps3-gpu-tools, `/usr/lib/ps3/lib`) is an ALSA filter plugin
(extplug) put in front of `default` for the game only. `client.sh` gives the game
`ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf:/usr/lib/ps3/lib/ps3-xrunlog.conf` and
`PS3_XRUNLOG=$D/xruns.log`; the conf's hook runs after alsa.conf's own loader (conf.d, asound.conf,
the user's `~/.asoundrc`), renames whatever `pcm.default` they made to `pcm.ps3xrun_orig` and puts the
plugin in front of it. No file of the user's is written or read differently. The plugin copies the
samples through unchanged with no buffer of its own, so the game's buffer, periods and timing stay
the slave's; per chunk it reads the queued frames (`snd_pcm_delay`) to know when the written sound
runs out, and keeps the loudness of the last 20 ms written. A dropout starts when the stream is
prepared again after frames were written (SDL's recover, OpenAL's silent one, or a restart of the
stream, which sounds the same), or when a chunk arrives after the queue already ran out without an
xrun. It ends when the restarted stream runs. `xruns.log` gets a line each: the dry moment
(`CLOCK_MONOTONIC` and `CLOCK_REALTIME`), `gap_ms`, `before_db` and `after_db` (RMS of the 20 ms written
before and after, dB relative to full scale; `-inf` digital silence, `nan` unknown), `via`, `resumed`.
The cost is one copy and one delay query per chunk.

**Audible.** `xruns.py` (run by `client.sh` at the end) applies the rule: a gap of 20 ms or more
with either side at -50 dBFS or louder (when neither side's loudness is known, the gap alone). It
writes `bench.txt`'s `xrunlog=on` (or `unused`: the game never played through `default`),
`xruns_logged`, `xruns_audible`, `xrun_gap_ms_total`, `xrun_gap_ms_max`, `xrun_audible_ms`, `xrun_rule`,
and `xruns.tsv`, one row per dropout (`at_s` after the run's `t0`, gap, loudness, audible). `xrunlog=off`:
`XRUNLOG=0` (an ENVS token, `BENCH_XRUNLOG=0`); `missing`: the plugin isn't installed. The old counts
(`underruns`, `oal_xrun`) stay for comparison. `import-run.py` scores the sound's cleanliness from
`xruns_audible` plus OpenAL's waits a minute when `xrunlog=on` ([GAMEDB.md](../GAMEDB.md)).

**Check it on the PS3.** `ps3-xrun-test` (ps3-gpu-tools) plays a quiet tone (-40 dBFS) through
`default` with the harness's setup and stops writing on purpose: after the queue runs out plus 10,
40 and 150 ms, and 60 ms with silence on both sides. It prints what the log says next to what it made
and exits 0 when every gap came out within max(6 ms, 20%) with the right loudness. `-m` writes as
OpenAL does (mmap, silent recover), the default as SDL does. On the PC, `selftest.py` runs it
against a pretend card (`ps3-gpu-tools/tests/xrun-pacer.c`) that plays nothing.

## Screenshots

The picture check since 2026-10-20 (the user: "We won't need the camera if this works"). The PS3
captures its own screen during a run, the PC runs the same recipe and captures its own at the same
point of the game, and a sheet puts the two side by side. The judge is a person or an agent looking at
the sheet: missing textures, a dark or flat 3D view, text that isn't drawn, wrong colours. It is not a
pixel comparison: the two runs are at different moments (the PS3 loads about 10x slower, physics and
AI aren't deterministic), so only a black or one-colour screen is flagged by itself. Menus and title
screens (`SHOT_STATIC=1`, the recipes' `menu` variants) are the place where a closer look is fair.

**On the PS3.** client.sh runs `xwd -root` (x11-apps, installed) at nice 19 and the idle I/O class, as
a background job, at each shot point. `SHOTS` (run.sh, an ENVS token too) is a list of whole seconds,
`auto` (the recipe's `SHOT_POINTS`, default `30 90`) or `off`; smoke runs take `auto`, bench and suite
runs none unless `SHOTS` is given. The points count from a game state, not from the start:

- `SHOT_FROM=load` (default): the load's end as the window has it (`LOAD_MARK`, `LOAD_RE`, else the
  HUD's first frame; for many smoke recipes the first frame is a loading screen, so they name a line);
- `SHOT_RE=regex`: the first game output line matching it (Warzone 2100: `Started writing replay file`,
  the match's start);
- `SHOT_FROM=mark`: when the recipe's `during()` calls `shot_mark`, after its keys (Endless Sky's
  flight, SuperTux, OpenTyrian, UQM, ScummVM, Crawl, Simutrans); a variant whose `during()` ends without
  it (no keys) falls back to the load's end (`shot_ref=load-nomark`);
- `SHOT_FROM=start`: the client's start.

A raw capture is 8.3 MB at 1920x1080. It goes to `/var/tmp/ps3-bench/.shots-<run>` on the disk while
the run directory is in `/dev/shm` (RAM is scarce), and run.sh moves it into the run's `shots/`
afterwards. bench.txt gets `shots=`, `shot_ref=<from>:<s after t0>`, per shot
`shot=<p>s:shots/shot-<p>s.xwd:<ms>` (the capture's wall time) and `shot_t=<p>s:<s after t0>:<n0>:<n1>`
(the HUD frame-time file's line counts before and after the capture: the frames it touched), and
`shot_skipped=` for points the run never reached. The capture is client.sh's child with `BENCH_DIR` in
its environment, so the interference detector counts it as the run's own.

**What a capture costs** is not measured yet (docs/TESTS.md, Next). `xwd -root` makes the X server copy
the whole front buffer out of VRAM, and that is Xorg's time, not the nice'd xwd's. Until it is known,
a bench run with `SHOTS` and a capture inside the window is tagged `@shot` (report.py) and kept apart
from plain runs; report.py's `shot_n`, `shot_ms`, `shot_win`, `shot_frame` columns and
`python3 shots.py cost <run dir>` (the capture's wall time, the frames it touched, the longest of them
against the 120 frames before) give the numbers.

**On the PC.** `make bench-ref GAME=warzone2100 [VARIANT=base] [SHOTS="30 90"]` (`tools/bench-ref.sh`)
runs the recipe through the same client.sh in an amd64 container (`docker/Dockerfile.benchref`, Debian
sid from the PS3's snapshot) under Xvfb 1920x1080, with llvmpipe forced to the RSX's GL 2.1 and GLSL
1.20 (`MESA_GL_VERSION_OVERRIDE=2.1`, `MESA_GLSL_VERSION_OVERRIDE=120`; the extension list is
llvmpipe's own), no sound device, and the same shot points after the same game state. The recipe says
where the game comes from: `REF_PKGS` (Debian packages, for the games we take from Debian: OpenArena,
SuperTuxKart) or `REF_IMAGE` (a local image with our amd64 build of a game we patch: Warzone 2100's
`wz-amd64-view`); `REF_ENV` adds NAME=VALUE for the PC run. Output: `build/bench-ref/<game>-<variant>/`
(the run directory) and `build/bench-ref/<game>-<variant>-<s>s.png`; an older reference moves to
`build/bench-ref/old/` first. The container is `shot-ref-<game>-<pid>`, removed at the end.

**The sheet.** `make bench-pull` leaves out captures it already has as PNGs, converts the new `.xwd`
files to PNG (the `.xwd` is removed once the PNG reads back at the same size) and runs `shots.py post`:
each run with screenshots gets `<run>/shots-vs-pc.png`, one row per shot point, PS3 left and PC right
(800x450 each, 1608 px wide), labelled with the game, variant, shot point, capture time and both
sides' builds (Mesa, the game, the kernel), and a `shotsheet:` line in its `result` (the sheet, the
reference it compares against, `PS3 BLACK at 30s` when a side is black or one colour). Without a
reference the PC side says so and names the `bench-ref` command. `make bench-shots` remakes the sheets
whose shots or reference changed and lists the ones without a `REVIEW.md`: the reviewer writes that
file next to the sheet, first line `verdict: ok|wrong|unclear [words]`, and report.py shows it as
`review=`.

## The results matrices

`games/bench/results/` holds one CSV per metric with the same rows (oldest first) and the same first
four columns (`test`, `date`, `runs`, `observations`), then one column per `<game>-<variant>`. A cell
is the median over the row's runs of that game-variant. `results/README.md` explains every file and
the caveats (rows 1-10 had nothing on screen; times in observations are UTC, the PS3's local time is
UTC+3; perf-recording rows are slower). To add a row:

    make bench-pull
    python3 games/bench/matrix.py "Mesa ps3.290 + ps3rsx 0.249" --obs "what the row shows" \
        build/bench/ps3-bench-20261018T*-armagetronad-*

`--replace` drops the row's old cells first; `--hud GAME[,GAME]` takes the HUD median as the fps
cell. Then fix the observations (plain sentences with names and numbers, caveats, the commit or
version the row proves) and commit `games/bench/results/` only. docs/TESTS.md names the results
commit of every batch.

## Adding a game

Every game went through the same steps:

1. **Get it**: a Debian main package (`image/deps.txt`, `make deps` with `build/aptly` backed up
   first), our package (a debsrc directory like `mesa/`: `make <pkg>-work`, `-patches`, `-deb`), or a
   data deb. Recommends are never installed (the image's apt config and aptly's
   `-filter-with-deps` both skip them), so every package a game needs goes into its Depends and
   `image/deps.txt` by name.
2. **Before the session**: `make deps` (one refresh per group: each is permanent in the `ps3` suite;
   `make du` first), the packages built, `make deploy`, `make bench-push`, `df /` on the PS3.
3. **Smoke**: `run.sh smoke <game> [variant]` (VRAM swap off, the first launch of anything that might
   wedge the GPU). Check the picture, the sound state in bench.txt and the errors run.sh prints. Each
   recipe's setup writes the game's config first: 1920x1080 fullscreen, first-run dialogs off. Record
   the first failure only, in one class: GL version or extension refused, shader compile or link,
   validator reject, graph exception, out of memory, wrong colours (big-endian). Silent failures
   count: grep stderr for `nouveau`, watch for a sudden slowdown.
4. **Fix**: a driver bug as a Mesa patch or in ps3rsx (new validator rejects and graph exceptions go
   into [RSX.md](../RSX.md) with a reproducer in `ps3-gpu-tools/mesa-tests`), a game bug in the game's
   own debsrc package. Then smoke again.
5. **Bench**: a recipe and its asset, then `run.sh bench <game> base 3` as the baseline.
6. **Done** when the picture and sound are right, no kernel errors, the recipe gives numbers, the
   baseline is in the matrices, and the light games reach about 60 fps at 1920x1080 (the user,
   2026-10-17: "aim is 60 or close to"). Correct but slow is "runs, needs performance work": a perf
   profile of the game process says where the time goes before any fix.

Then the game joins `ps3-games` (ps3-desktop's `debian/control`) once it runs with its sound.

## The metapackages

- **`ps3-games`** (in ps3-desktop's control, Architecture all; ps3-desktop recommends it): Depends on
  openarena, classicube, quakespasm, yamagi-quake2, armagetronad, neverball, xmoto, extremetuxracer,
  dsda-doom, freedoom, openttd, openttd-opengfx, openttd-opensfx, openttd-openmsx. Free packages only.
  The heavy games (BananaBread, Luanti, SuperTuxKart) deploy by name, e.g.
  `make deploy PKGS="ps3-games supertuxkart supertuxkart-data"`.
- **`ps3-games-licensed`** (`ps3-games-licensed/`, native, all, routed to the `ps3-licensed` suite):
  Depends on ps3-games, classicube-data, quake-shareware, quake2-demo-data, sauerbraten-nonfree-data.
  For personal and LAN use only; free packages never depend on it.

## Disk and download

Sizes computed on 2026-10-16 against the image of that day (OpenArena and ClassiCube in it),
before anything ran:

| Set | New packages | Download | Installed |
|---|---|---|---|
| The suite without SuperTuxKart | 56 | 298 MB | 649 MB |
| SuperTuxKart (its data 752 MB, Noto fonts) | +12 | +678 MB | +839 MB |
| The whole suite | 67 | 976 MB | 1.49 GB |
| Reserves (ETR, Armagetron, SuperTux, Teeworlds) | 44 | 228 MB | 377 MB |

The biggest items: supertuxkart-data 752 MB, cube2-data 260 MB, neverball data 108 MB (Neverputt
26 MB more), xmoto-data 62 MB, freedoom 57 MB. The first smoke set was about 1.5 GB to download and
2 GB installed (TORCS and Red Eclipse's data 1.5 GB of it); Nexuiz Classic about 800 MB.

## Stage 0: how the suite started

On 2026-10-16 the runner replaced `games/openarena/step0.sh`: `run.sh`, `client.sh`, `report.py`
(from step0-report.py), recipes and assets; config backups on disk, ALSA underrun counting, a 90 s
cap at the time. The first two recipes were OpenArena (tc0, tc1, gl2min, gl2) and ClassiCube (a
saved map, 60 s, HUD fps; later the ccbench timedemo). Games ran directly, with `GAMEMODE=1` as a
variant, because game mode changes memory and timing. It was accepted when both recipes gave
numbers with the picture on screen; matching step 0 was dropped as a criterion because step 0 ran
with nothing on screen. Two display bugs had to go first, both found because the screen stayed
black: SDL's fullscreen window needs a window manager (client.sh starts a bare Openbox, fa723f9), and
ps3rsx muted the video for every X started after another had quit (a1f1b91). Validation on ps3rsx
0.219, Mesa 26.2.4-1+ps3.186, libspeq0 0.210:

| Recipe | Runs | Load | fps | HUD median / p10 | Peak RSS | Peak VRAM |
|---|---|---|---|---|---|---|
| openarena tc1 | 3 | 10.8 s [10.6-11.9] | 42.2 [40.9-46.4] | 47 / 6-28 | 83 MB | 37-40 MB |
| classicube (60 s, saved map) | 2 | | | 50.5 / 44 | 51 MB | 61 MB |

Older versions and the dated history: [archive/GAME-SUITE.md](../archive/GAME-SUITE.md) ("Design:
games/bench/", "Per game"), [archive/GAME-PLAN.md](../archive/GAME-PLAN.md) ("Stage 0", "Every stage
has the same shape", "Sessions").
