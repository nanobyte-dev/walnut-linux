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

**Status (2026-10-26): step 1b is done, the catalog and the game packages live here.** Each game's
`index.md`, `thumb.png` and played sessions (`rating/sessions/<run>/`) are in `games/<id>/`; the site
mounts them (website/gamedb/hugo.toml) and the app's catalog is packed from them. Every game package
is in its bundle's `packages/`, the three engines (cube2, ioquake3, darkplaces-gl2) in `engines/<e>/`,
the licensed data packages and builders in `games/<id>/licensed/`, the teeworlds and yquake2 upstream
drafts in `upstream.md`; games with a package but no published page have draft bundles. The
benchmark recipes are still in `bench/` (step 1c). The tools below already look here (marked *read now*);
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
`tools/gamedb/check.py`, which also allows only `index.md`, `thumb.png`, `rating/sessions/`, `packages/`,
`licensed/`, `upstream.md` and `upstream/` in a game's folder until the later steps add their files):

| Key | Type | Meaning |
|---|---|---|
| `id`, `title` | string | required |
| `summary`, `license`, `homepage` | string | required for a published game |
| `categories` | list | one to three of check.py's CATEGORIES |
| `tags` | list | lower case words |
| `packages` | list | what the app installs (moves to build.toml in step 2) |
| `desktop`, `recipes`, `patched_by` | list | menu entries; bench recipes; our source packages that patch it |
| `needs_data`, `data_guide`, `data_steps` | bool, string, list | the player's own data and how to set it up |
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

How the game is built, installed and integrated. *Planned* (step 2), except `[[licensed]]`:

| Key | Meaning |
|---|---|
| `packages` | the packages the app installs |
| `engine` | the engine bundle in `engines/` it runs on |
| `suite` | `free`, `licensed` or `none` |
| `needs_data`, `data` | the player's own data; the data packages |
| `desktop`, `divert`, `image_base` | menu entries; files we divert; in the image's base set |
| `[[licensed]]` `name` | a licensed package name (*read now* by `tools/licensed.py`) |
| `[[licensed]]` `leak_files` | paths of its content in upstream tarballs, leak globs (*read now*) |
| `[[licensed]]` `builder` | how the data package is made (*planned*) |

Versions: `walnut-games` (*now*), `ps3-games` and `ps3-games-licensed` (*planned*) carry `VERSION_PATHS` over
exactly the gamedb files they consume (index.md, thumb.png, build.toml, rating/test.toml,
rating/sessions/*/* and packages/*/patches/** for the app; build.toml and desktop/ for ps3-games), never a session's raw/, technical.md, investigations, todo.toml, benchmark/ or
research. `tools/deb-version.sh` reads a pattern there as GitHub's path filters do: `*` within one
path component, `**` across them.

### games/<id>/packages/<pkg>/, engines/<e>/packages/<pkg>/

Our source packages for one game (for an engine: `engines/<e>/packages/`; step 1b moved them all
here), each laid out as in `packages/`
(docs/PACKAGING.md): `package.conf`, `upstream.mk`,
`debian/`, `patches/`, `files/`, `tests/`, plus *planned* `orig.mk` (its orig tarball recipe, included by
the Makefile; step 2) and `dfsg-exclude.txt` (paths left out of the upstream tarball, one per line;
`tools/licensed.py` *reads it now* as leak globs, an entry `E` as `E` and `E/*`).
A package is named by its directory alone, here as in `packages/`: `tools/pkg-dir.sh` *finds these
now*, and two packages of one name anywhere are an error. Shared libraries patched because of a
game (libsfml, luajit, sdl2-compat, libpng, ...) stay in `packages/` and cite `gamedb:<id>` in their
headers.

### games/<id>/licensed/<pkg>/

A licensed data package or its builder (step 1b: `classicube-data` in classicube, `sauerbraten-data` in
sauerbraten, `sauerbraten-nonfree-data` in bananabread, since it fills in the `cube2-data` BananaBread
plays, `rct2-data.sh` in openrct2, `tyrian-data.sh` in opentyrian): text only, built on our own PC, never by CI, published to
the password-protected licensed suite only. A directory here is licensed by its location:
`tools/licensed.py` *reads it now* (with `packages/licensed/` and `tools/licensed-packages.txt`),
the public copy drops `gamedb/*/*/licensed/*` from every commit (`tools/public-export.sh`), CI never
gets a workflow for it (`github-ci/gen-workflows.py`). `licensed.py check` wants every one declared.

### games/<id>/desktop/

*Planned* (step 3): `launchers/*.desktop`, `files/<absolute path>` (config files we install, e.g.
OpenArena's defaults), `migrate-home.txt`. `tools/gamedb/pack-integration.py` turns them into the
`ps3-games-integration` package's tree, and `ps3-games` / `ps3-games-free` / `ps3-games-licensed`'s
Depends.

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

*Planned* (steps 1c, 4, 5). The A/B benchmark of game and driver optimizations: `recipe.sh` (code, as
the recipes in `bench/recipes/` are today; it uses `"$BENCH_LIB/smoke.inc"` and `"$GAME_DIR"`),
`analysis.toml` (data the Python analysis tools read: the reload rule, log lines to ignore, legacy
names, the interference pattern), `lib/`, `assets/`, `assets-src/` (with `assets.mk`),
`results/<batch>.toml`, `runs/<run>/raw/`, `legacy/`.

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
ioquake3, darkplaces-gl2), and *planned* `build.toml`, `technical.md`, `investigations/`, as for a game.

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
| the integration tree and the ps3-games Depends (*planned*) | `tools/gamedb/pack-integration.py` |
| `PKG_DIRS` (and, *planned*, a complete `SRC_PACKAGES`) | `tools/pkg-dir.sh --all`, `--order` |
| licensed names and leak globs | `tools/licensed.py` |
| CI workflows | `github-ci/gen-workflows.py` (`--check` in make test) |
| doc blocks | `tools/gamedb/blocks.py --write`, `--check` |

## Checks

`make test` runs `tools/licensed.py check`, `github-ci/gen-workflows.py --check`,
`tools/gamedb/blocks.py --check` and `tools/doclinks.py` (report only, for now). During the
migration, `tools/gamedb/baseline-check.py check` compares a phase's result with the baseline saved
before it started (build/tmp/gamedb-baseline/), and `tools/gamedb/catalog.py --diff OLD` lists every
field of the catalog that changed against an older catalog.json (games and tests matched by id).
