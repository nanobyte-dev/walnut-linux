# gamedb/: everything about a game, in one place

This folder is the games database. Every fact about one game lives here and nowhere else: how it's
built and installed, its packages and patches, its menu entries, its played sessions and ratings,
its benchmarks, its technical state and the investigations behind it, its backlog. The Walnut Games
site (website/gamedb/, Hugo) and the `walnut-games` desktop app are front ends that read it.

Outside this folder a game may appear only as:

- a **generated view**: a marked block in a doc (`<!-- gamedb:NAME ARGS -->` ... `<!-- /gamedb:NAME -->`,
  rewritten by `tools/gamedb/blocks.py --write`, checked by `--check` in `make test`) or build output;
- a **reference**: `gamedb:<id>`, `gamedb:<id>/<investigation-id>` or `gamedb:batch/<name>`, or a plain
  link, where something game-agnostic says which game it came from (a Mesa patch "found by", a
  library patch's motivation); `tools/doclinks.py` checks that each one resolves;
- **frozen history** (docs/archive/, git, changelogs);
- **test fixtures** that use a game as sample data.

**Status (2026-10-26): step 2 is done, the catalog, the game packages, the benchmarks and the build facts live here.** Each game's
`index.md`, `thumb.png` and played sessions (`rating/sessions/<run>/`) are in `games/<id>/`; the site
mounts them (website/gamedb/hugo.toml) and the app's catalog is packed from them. Every game package
is in its bundle's `packages/`, the three engines (cube2, ioquake3, darkplaces-gl2) in `engines/<e>/`,
the licensed data packages and builders in `games/<id>/licensed/`, the teeworlds and yquake2 upstream
drafts in `upstream.md`; games with a package or a recipe but no published page have draft bundles.
Every bench recipe, with its assets and helpers, is in its bundle's `benchmark/`. How each game is built
and installed (its packages, engine, suite, data, licensed data) is its `build.toml`, which the Makefile,
the catalog and the licensed guards read; each package's orig tarball recipe is its `orig.mk`. The tools below already look here (marked *read now*);
the rest is marked *planned*, with the migration step that brings it.

## Layout

```
gamedb/
  README.md                         this file: the schema
  benchmarks/batches.toml           every benchmark batch once
  benchmarks/metrics.toml           metric names, units, direction, the tool that computes each
  research/<slug>.md                surveys across games
  engines/<e>/                      an engine that is its own upstream project (cube2, ioquake3, darkplaces-gl2)
    engine.md  build.toml  technical.md  investigations/  packages/<pkg>/
  games/<id>/
    index.md                        the player's page
    build.toml                      how it's built, installed and integrated
    technical.md                    the current technical state
    todo.toml                       the backlog
    upstream.md, upstream/*.patch   offers of our game patches to upstream (optional)
    thumb.png
    investigations/<date>-<slug>.md one investigation; <date>-<slug>/ holds its attachments
    packages/<pkg>/                 our source packages for this game
    licensed/<pkg>/                 licensed data packages and their builders (never public)
    desktop/                        menu entries, installed config files, the migrate-home list
    rating/test.toml                the played test
    rating/sessions/<run>/          the evidence of one played session
    benchmark/                      the A/B benchmark: recipe, analysis rules, helpers, assets, results
```

`<id>` matches `^[a-z0-9][a-z0-9-]{0,63}$` and is the folder's name. Ids, and an investigation's
`id`, are never renamed: references point at them.

## The files, their keys, and who reads them

### games/<id>/index.md

TOML front matter between `+++` lines, then the notes as markdown. Keys (today's set, from
`tools/gamedb/check.py`, which also allows only `index.md`, `thumb.png`, `build.toml`, `rating/sessions/`, `packages/`,
`licensed/`, `upstream.md`, `upstream/` and `benchmark/` in a game's folder until the later steps add their files):

| Key | Type | Meaning |
|---|---|---|
| `id`, `title` | string | required |
| `summary`, `license`, `homepage` | string | required for a published game |
| `categories` | list | one to three of check.py's CATEGORIES |
| `tags` | list | lower case words |
| `data_guide`, `data_steps` | string, list | how a player gets and sets up their own data: required when build.toml says `needs_data = true`, refused otherwise |
| `controller`, `designed_for` | string | gamepad support; `pad` or a PC input |
| `fps_cap`, `medal_note`, `known_issues` | number, string, list | |
| `draft` | bool | not published: catalog.py, pack-catalog.py and Hugo leave it out |
| `status` | string | `candidate`, `tried`, `blocked`, `dropped`, `impossible` or `tested`; required for a draft |
| `status_note` | string | why that status |
| `[controls]` | table | *planned*: the controls a player sees |

A draft needs only `id`, `title` and `status`. Every other key may be missing, and nothing is made
up to fill it; whatever a draft does have is checked like a published game's. Every candidate game
gets a draft bundle (step 7).

Read by: `check.py` (*read now*, drafts included), `catalog.py` and `pack-catalog.py` (drafts left
out), Hugo (website/gamedb/hugo.toml mounts `games/` as `content/games/` with an allowlist: `*/index.md`,
`*/thumb.png`, `*/rating/sessions/*/*.jpg`; nothing else of a bundle becomes a page or a resource),
`import-run.py`, `tools/play-pc.sh`, `tools/play-test.sh`.

### games/<id>/build.toml (and engines/<e>/build.toml)

How the game is built, installed and integrated (*read now*). TOML, a closed key set (`tools/gamedb/check.py`
BUILD_KEYS: an unknown key is an error, so a typo can't vanish). A published game needs `packages`, `suite`
and `needs_data`; a draft has a build.toml only when something about its build is known, and only those keys.

| Key | Type | Meaning |
|---|---|---|
| `packages` | list | the packages the app installs (`walnut-games-helper`; `tools/play-pc.sh` on the PC): Debian package names, none licensed; each one in the apt index (`check.py --apt-index URL`, e.g. the Debian snapshot's and our repository's Packages) or built by a `debian/control` of ours |
| `engine` | string | the engine bundle in `engines/` it runs on; that engine's `engine.md` `games` lists the game (check.py checks both ways) |
| `suite` | string | `free`: in `ps3-games` and the public `ps3-games-free`; `licensed`: in `ps3-games` only (`ps3-games-free` suggests it), its data is licensed (the public image leaves its packages out: `tools/image-packages.sh`); `none`: in neither. Drafts count too (ClassiCube is a draft in `ps3-games`) |
| `meta` | list | the packages `ps3-games` and `ps3-games-free` name for the game, when not all of `packages` (e.g. `["neverball"]`: its data comes with it); each one of `packages` |
| `needs_data` | bool | the player supplies data the game can't run without (index.md then has `data_guide` and `data_steps`) |
| `data` | list | the data packages (often licensed) the game plays with: on the PC, `tools/play-pc.sh` mounts each one's files from a `.deb` found in build/ |
| `desktop` | list | the game's own menu entries the app starts (`<name>.desktop`) |
| `divert` | list | absolute paths of files of the game's packages we divert: the game's own menu entries, which our launchers stand for (`ps3-games-integration`'s preinst moves each to `/usr/share/ps3-games/diverted<path>`, out of the menu's reach) |
| `image_base` | bool | its packages are in the image's package layer (`tools/gamedb/build-info.py image-base`, the Makefile's `ROOTFS_GAMES`; system/image/packages.txt names only generic packages) |
| `recipes` | list | the bench runs that stand for the game, `"<id>:<variant>"` of its own `benchmark/recipe.sh` (the catalog's `recipes`) |
| `[[licensed]]` | tables | the game's licensed data: `name` (the package), `builder` (`source`: a source package of ours in `licensed/<name>/`; `script`: `licensed/<name>/<name>.sh` run on the owner's own copy, `make licensed-data PKG=<name> INPUT=<file>`; `game-data-packager:<game>`: `make game-data`), `leak_files` (paths of its content in upstream tarballs, leak globs), `meta` (bool: `ps3-games-licensed` depends on it; by default true for a game with suite `licensed`; BananaBread's `sauerbraten-nonfree-data` sets it) |

Every `licensed/<pkg>/` directory of a bundle has its `[[licensed]]` entry, and every `source` or `script`
entry its directory (a checkout without licensed directories, the public copy, skips that direction).
The catalog's `patched_by` is no key: it is the source packages of ours in the bundle's `packages/` and its
engine's (`check.patched_by`). An engine's build.toml takes `packages` (the engine's packages its games run)
and `[[licensed]]` only.

Read by: `tools/gamedb/check.py` (validates), `catalog.py` (packages, needs_data, desktop, recipes),
`tools/licensed.py` (`[[licensed]]` names and leak_files), `tools/gamedb/build-info.py` (for the Makefile's
`make game-data` and `make licensed-data`, `tools/image-packages.sh`'s licensed-suite games, `tools/play-pc.sh`'s
packages and data, `image-base`, `own-packages` for `make game-debs`), `tools/gamedb/pack-integration.py`
(suite, packages, meta, divert, `[[licensed]]` meta: the metapackages' Depends and the integration package).

Versions: `walnut-games`, `ps3-games` and `ps3-games-licensed` carry `VERSION_PATHS` over
exactly the gamedb files they consume (index.md, thumb.png, build.toml, rating/test.toml,
rating/sessions/*/* and packages/*/patches/** for the app; build.toml and desktop/ for ps3-games, build.toml for
ps3-games-licensed), never a session's raw/, technical.md, investigations, todo.toml, benchmark/ or
research. `tools/deb-version.sh` reads a pattern there as GitHub's path filters do: `*` within one
path component, `**` across them.

### games/<id>/packages/<pkg>/, engines/<e>/packages/<pkg>/

Our source packages for one game (for an engine: `engines/<e>/packages/`; step 1b moved them all
here), each laid out as in `packages/`
(docs/PACKAGING.md): `package.conf`, `upstream.mk`,
`debian/`, `patches/`, `files/`, `tests/`, `orig.mk` (*now*: its orig tarball recipe, included by the
Makefile, free packages first, then the licensed ones; docs/PACKAGING.md "Upstream tarballs") and
`dfsg-exclude.txt` (*now*, classicube: paths left out of the upstream tarball, one per line, read by its
orig.mk; `tools/licensed.py` reads it as leak globs, an entry `E` as `E` and `E/*`).
A package is named by its directory alone, here as in `packages/`: `tools/pkg-dir.sh` *finds these
now*, and two packages of one name anywhere are an error. Shared libraries patched because of a
game (libsfml, luajit, sdl2-compat, libpng, ...) stay in `packages/` and cite `gamedb:<id>` in their
headers.

### games/<id>/licensed/<pkg>/

A licensed data package or its builder (step 1b: `classicube-data` in classicube, `sauerbraten-data` in
sauerbraten, `sauerbraten-nonfree-data` in bananabread, since it fills in the `cube2-data` BananaBread
plays, `rct2-data.sh` in openrct2, `tyrian-data.sh` in opentyrian): text only, built on our own PC, never by CI, published to
the password-protected licensed suite only. A directory here is licensed by its location:
`tools/licensed.py` *reads it now* (with `packages/licensed/` and the `[[licensed]]` entries of build.toml),
the public copy drops `gamedb/*/*/licensed/*` from every commit (`tools/public-export.sh`), CI never
gets a workflow for it (`github-ci/gen-workflows.py`). `licensed.py check` wants every one declared in
its own bundle's build.toml.

### games/<id>/desktop/

How the game is integrated into the PS3 desktop, as files (`tools/gamedb/check.py` allows only these):

- `launchers/*.desktop`: our menu entries, one per way to start the game (OpenArena's two renderers); the
  Exec starts it through `ps3-game` (game mode). `pack-integration.py` adds `TryExec=` the program Exec runs
  (past `env` and `VAR=value`), so the entry shows only when the game is installed.
- `files/<absolute path>`: config files installed as they are (OpenArena's PS3 defaults:
  `files/usr/lib/openarena/baseoa/autoexec.cfg`). Two games installing one path is an error.
- `migrate-home.txt`: `<path in the home> [<packaged default>]` per line (`#` comments): files the old image
  setup wrote into a home directory; `ps3-migrate-home` (ps3-desktop) removes each one that equals the
  default and moves the others to `~/.local/share/ps3-legacy/`.

`tools/gamedb/pack-integration.py` (`make gamedb-integration`) packs every game's desktop/ and the build.toml
keys above into build/gamedb/integration.tar, which the `ps3-games` source package
(packages/walnut/system/ps3-games/) takes in: `ps3-games-integration` installs the tree (the launchers in
/usr/share/applications/, the files, /usr/share/ps3-games/divert.list and migrate-home.list) and diverts the
listed files; `ps3-games` and `ps3-games-free` get their Depends and Suggests from it. `ps3-games-licensed`'s
Depends come from build/gamedb/ps3-games-licensed.substvars (kept out of the public tarball).

### games/<id>/rating/

Ratings come only from played sessions; benchmarks never rate (docs/GAMEDB.md).

- `test.toml`, *planned* (step 4): the played test as data the app reads, never executes: `process`,
  `home`, `play_mark`, `load_mark`, `command`.
- `sessions/<run>/`: `result.toml` (written by `import-run.py`; the run names are `<date>-<kind>`, with
  `-<hhmm>` for a second one that day) and the screenshots it lists (`ps3-<point>.jpg`, Git LFS); the
  catalog, the site and the app name a screenshot by its path in the bundle,
  `rating/sessions/<run>/<file>.jpg`, which is also its URL under `games/<id>/` on the site. *Planned*
  (step 6) `form.toml`, `review.md`, `shots/` (every screenshot, JPEG;
  `pc/` for the PC reference) and `raw/` (the harness's files, scrubbed; private: the public copy strips
  `gamedb/**/raw/**`).

catalog.json is a cache of the ratings computed from these at build time.

### games/<id>/benchmark/

The A/B benchmark of game and driver optimizations (docs/games/SUITE.md), *read now* (step 1c) by the
harness in `bench/`:

| File | What |
|---|---|
| `recipe.sh` | the recipe: code, sourced by `bench/client.sh` (below) |
| `analysis.toml` | data for the Python analysis tools: *now* `[legacy] recipe_names`, the recipe's file names before it moved here (`sauerbraten` in bananabread, `sauerbraten-edition` in sauerbraten); *planned* (step 4) the reload rule, the log lines to ignore, the interference pattern |
| `lib/` | the game's own helpers (Hedgewars' `hwdrive.py`), reached as `"$GAME_DIR/lib/..."` |
| `assets/` | the files a recipe puts in place (demos, replays, saves, maps, mods), reached as `"$ASSETS/..."`; small normal files, not Git LFS |
| `assets-src/` | how built assets are made: an `assets.mk` the top-level Makefile includes (ClassiCube's plugin, `make classicube-bench`; `make bench-assets` makes all), or the scripts that made them (OpenTTD's saves) |
| `legacy/` | superseded tools kept for the record (OpenArena's step 0) |
| `results/<batch>.toml`, `runs/<run>/raw/` | *planned* (steps 5, 6) |

Where the harness finds them: `bench/games.sh` (shell) and `bench/games.py` (Python) resolve a name to
the game's benchmark directory, here in the repository and in `/var/tmp/bench/games/<id>/` on the PS3
(`make bench-push` copies `recipe.sh`, `analysis.toml`, `lib/` and `assets/` of every bundle there).
A game is named by its id; `bench.txt` gets `game=` and `recipe=` (both the id). A run from before
the move (no `recipe=`) names the old recipe file, so its `game=` and its directory name are read
through the `[legacy] recipe_names` first (game=sauerbraten in such a run is BananaBread); `run.sh`
also takes an old name that is no id (`sauerbraten-edition`). Read by `bench/client.sh`, `run.sh`
(`list`, variants, `suite`), `session.sh`, `ssd-arm.sh`, `matrix.py` (run labels), `selftest.py` (every
recipe and variant sourced), `tools/gamedb/import-run.py` (a recipe's `PLAY_MARK`), `tools/play-test.sh`,
`tools/play-pc.sh` (`REF_IMAGE`), `tools/bench-ref.sh` (the container sees `bench/` and the game's
benchmark directory) and `tools/gamedb/check.py` (`benchmark/` holds only these files; a recipe.sh;
`analysis.toml` parses).

#### benchmark/recipe.sh

One bash file per game, sourced by `client.sh` (variables and functions only, nothing runs at source
time). `client.sh <game> <variant>` sources it, then calls `variant_<variant>` if it exists (`base`
needs none). Before it is sourced, `GAME_DIR` is the game's benchmark directory, `ASSETS` its
`assets/` and `BENCH_LIB` the directory of the generic pieces the recipes share (`bench/` for now,
a `lib/` of its own in step 9): `"$BENCH_LIB/smoke.inc"` (the header of the smoke recipes: `SUITE=0`, a 120 s
`DURATION` that `SMOKE_SECS` overrides, a short HUD window, `vblank_mode=0`, and `smoke_keys` /
`smoke_xkeys` for keys into the game; a smoke recipe is a game tried once to see whether it starts and
plays, docs/games/smoke-tested.md), `"$BENCH_LIB/openal.inc"` (the OpenAL Soft variants `allog`,
`b16`, `f44`, `b16f44`, `p1024`, `alnull` the OpenAL games' recipes share, docs/SPU-AUDIO.md M0, with
an optional perf profile, `OAL_PERF=N`, that `bench/oal-perf.sh` splits by symbol),
`"$BENCH_LIB/keyboard.evemu"` (a keyboard's description for an evemu virtual keyboard: ETR's
recording without its events) and `"$BENCH_LIB/xkeys.py"` (keys through XTEST).

| Name | Meaning |
|---|---|
| `CMD=(...)` | the game command, absolute path |
| `ENV=(...)` | `NAME=value` for the game's environment |
| `SUITE=1` | part of `run.sh suite` |
| `COMM` | the game's process name (ps3rsx per-client VRAM, peak RSS) |
| `BACKUP=(...)` | files the game rewrites (configs): restored after every run |
| `SESSION_HOME=(...)` | the game's settings and data directories under ~ (globs allowed): a played session sets them aside and runs the game on fresh defaults (freshhome.py); without it, the top directories of the BACKUP files |
| `DATA_DIRS=(...)` | the game's data directories (absolute): `GAMEDATA=ssd` (run.sh, an ENVS token) runs the game with each one bind-mounted from its SSD copy (`ssd-arm.sh copy GAME` makes it; docs/GAMEOS-HDD.md "Test drive, Phase A") |
| `setup()` | puts assets and config in place (from `$ASSETS`) |
| `END_RE` | output line after which the game is stopped (games that don't quit) |
| `DURATION` | seconds from start after which the game is stopped: a safety cap (default 270: load + 60 s warm-up + 120 s measured + margin) |
| `WARMUP`, `MEASURE` | the steady state (the user, 2026-10-17): `WARMUP` seconds after the load ends are left out (default 60), then at least `MEASURE` seconds are measured (default 120). The load ends at the `LOAD_MARK` line or the first `LOAD_RE` line, else at the HUD's first frame |
| `PASSES` | 1: the game repeats its demo in one session and every `FPS_RE` line ends a pass (`MAXMS_RE` read from the same line). A pass counts when it starts at least 0.9 x `WARMUP` after the load; the game is stopped once the counted passes cover 0.95 x `MEASURE` (ended=window). `fps`, `maxms`, `load` are then the first pass's (the run as it was before) |
| `WINDOW_STOP` | 1: the game is stopped 2 s after the window (load + `WARMUP` + `MEASURE`), ended=window; for games measured by the HUD that don't end by themselves |
| `STEADY_RE`, `STEADY_MAXMS_RE` | the game's own result over its own window (first capture group of the first match): bench.txt `steady_fps`, `steady_maxms` |
| `WIN_ON_RE`, `WIN_OFF_RE` | the game marks its own window, maybe in pieces: each ON line opens a piece, the next OFF line closes it (a piece still open at the end closes there); replaces load + `WARMUP` for the HUD metrics (e.g. a demo's reload between loops left out) |
| `FPS_RE`, `MAXMS_RE`, `LOAD_RE` | the first capture group of the last matching output line |
| `LOAD_MARK` | an output line whose time (seconds from start) is the load time |
| `PLAY_MARK` | played sessions only (`session.sh`): a regex on the game's output line that marks the first gameplay frame (a map loaded, a race started); bench.txt `play_from=mark`, `play_start`, `play_seconds`, and the HUD window starts there. Without it the session falls back to `LOAD_MARK`/`LOAD_RE` (`play_from=load`), else the whole run counts (`play_from=start`, an upper bound). Smoke and bench runs ignore it |
| `session()` | played sessions only: changes `CMD`/`ENV` into the command a player starts (the timedemo, autoplay and benchmark arguments out). A session already drops `vblank_mode=0`, `during()`, `parse()`, `END_RE`, `DURATION` and the passes/windows. Without `session()` the recipe's `CMD` runs as it is: right for a `menu` variant, wrong for a timedemo |
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
maxms counted`) for report.py. `python3 bench/selftest.py` checks client.sh, report.py and
matrix.py with a fake game and made-up runs, and sources every recipe and variant.

### games/<id>/technical.md, investigations/

*Planned* (steps 7, 8). `technical.md` is the current state, with links to the investigations. An
investigation, `investigations/<date>-<slug>.md`, has front matter `id` (never renamed), `games`,
`driver_patches`, `game_patches`, `sessions`, `batches`, and a body: symptom, evidence, cause, fix.
`gamedb:<id>/<investigation-id>` resolves by that `id` (`tools/doclinks.py`, *read now*). A driver bug a
game found: the investigation lives with the game, the mechanism in docs/DRIVER.md with a "found by"
link. An engine-wide one lives in `engines/<e>/investigations/`.

### games/<id>/todo.toml, upstream.md

`todo.toml`, *planned* (step 8): `[[item]]` with `kind` = `session`, `benchmark`, `package`, `fix`,
`research` or `upstream`: the game's backlog, its rows of docs/PLAN.md and its played-session queue.
`upstream.md` and `upstream/*.patch` (*now*, teeworlds and yquake2): what we would offer upstream of our
game patches; docs/upstream/README.md links every draft.

### engines/<e>/

`engine.md` (*now*: TOML front matter between `+++` lines with `id`, `title`, `homepage`, `license` and
`games = [...]`, the bundles that run on it; then a short note), `packages/<pkg>/` (*now*: cube2,
ioquake3, darkplaces-gl2), `build.toml` (*now*: `packages`, `[[licensed]]`), and *planned* `technical.md`,
`investigations/`, as for a game.

### benchmarks/batches.toml, benchmarks/metrics.toml

*Planned* (step 5). `batches.toml` has each batch once, as `[[batch]]` tables: `name`, `date`,
`stack`, `purpose`, `source`, `kind` = `run` or `summary`. `gamedb:batch/<name>` resolves against
these names (`tools/doclinks.py`, *read now*). `metrics.toml`: each metric's name, unit,
direction and the tool that computes it.

### research/<slug>.md

*Planned* (step 7): surveys across games (the GL3 census, candidates, re-evaluations, PC
classics), with their per-game tables generated. Emulator and platform research stays in
docs/research/.

## Generated from here

| Output | Tool |
|---|---|
| catalog.json (site, app); technical.json (*planned*) | `tools/gamedb/catalog.py` |
| the app's catalog.tar | `tools/gamedb/pack-catalog.py` |
| the integration tree, the ps3-games / ps3-games-free / ps3-games-licensed Depends | `tools/gamedb/pack-integration.py` (`make gamedb-integration`) |
| `PKG_DIRS`, `SRC_PACKAGES` (every package of ours, in build order) | `tools/pkg-dir.sh --all`, `--order` |
| orig recipes and `DEB_ENV`'s tarball variables | the packages' `orig.mk`, included by the Makefile |
| `make game-data`, `make licensed-data`, the public image's licensed-suite games, play-pc's packages and data | `tools/gamedb/build-info.py` |
| licensed names and leak globs | `tools/licensed.py` |
| CI workflows | `github-ci/gen-workflows.py` (`--check` in make test) |
| doc blocks | `tools/gamedb/blocks.py --write`, `--check` |
| the PS3's bench tree (`/var/tmp/bench/games/<id>/`) | `make bench-push` |

## Checks

`make test` runs `tools/licensed.py check`, `github-ci/gen-workflows.py --check`,
`tools/gamedb/blocks.py --check` and `tools/doclinks.py` (report only, for now). During the
migration, `tools/gamedb/baseline-check.py check` compares a phase's result with the baseline saved
before it started (build/tmp/gamedb-baseline/), and `tools/gamedb/catalog.py --diff OLD` lists every
field of the catalog that changed against an older catalog.json (games and tests matched by id).
