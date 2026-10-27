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

**Status (2026-10-26): every migration step is done, the last one (9) the split of `bench/` into
harness, lib, analysis and tests, which holds no game files.** Each game's `index.md`, `thumb.png` and played
sessions (`rating/sessions/<run>/`, with their evidence: step 6) are in `games/<id>/`; the site mounts them
(website/gamedb/hugo.toml) and the app's catalog is packed from them. Every game package is in its
bundle's `packages/`, the three engines (cube2, ioquake3, darkplaces-gl2) in `engines/<e>/`, the licensed
data packages and builders in `games/<id>/licensed/`, the teeworlds and yquake2 upstream drafts in
`upstream.md`; every game the surveys and plans named has a bundle, a draft when it isn't published.
Every bench recipe, with its assets and helpers, is in its bundle's `benchmark/`. How each game is built
and installed is its `build.toml`, which the Makefile, the catalog and the licensed guards read; each
package's orig tarball recipe is its `orig.mk`; the integration (launchers, config files, diversions) is
each bundle's `desktop/`, packed into `ps3-games-integration` (step 3). Every benchmark result is a
record: the batches in `benchmarks/batches.toml`, each game's numbers in its
`benchmark/results/<batch>.toml` (step 5). Since step 8 the game-specific parts of the docs live here too:
each game's and engine's current state in its `technical.md`, the stories behind it in
`investigations/`, its open work in `todo.toml`; the docs keep the cross-game mechanisms, with generated
blocks and `gamedb:` references back into the bundles, and `tools/gamedb/game-mentions.py` (in `make
test`, a warning) lists the sections that name a game without linking it. The site's technical pages
(step 7) are made from all of this. The tools below look here (marked *read now*); what is still to come
is marked *planned*.

## Layout

```
gamedb/
  README.md                         this file: the schema
  benchmarks/batches.toml           every benchmark batch once
  benchmarks/interfered.txt         runs found disturbed after the fact
  benchmarks/README.md              the results' format
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
    thumb.png                       optional (the catalog's thumb is then null)
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
`tools/gamedb/check.py`, which also allows only `index.md`, `thumb.png`, `build.toml`, `rating/sessions/`, `rating/test.toml`, `packages/`,
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
gets a draft bundle (*now*, step 7: every game the research surveys and the plans named, including the
closed-source ones with no open engine, `impossible` with the reason; aliases and the engines of one game
merged into one bundle, its technical.md listing the engine options). What the statuses mean for a draft:
`candidate` a port worth trying, with its cost; `tried` started on the PS3, not published; `blocked` an
open project exists but can't run here yet (unfinished, needs the original x86 executable, a runtime with
no big-endian ppc64 port); `dropped` decided against (RAM, CPU, licence), the reason in `status_note`;
`impossible` no open engine, or one bound to a closed runtime (Unity).

Read by: `check.py` (*read now*, drafts included), `catalog.py` and `pack-catalog.py` (drafts left
out), Hugo (website/gamedb/hugo.toml mounts `games/` as `content/games/` with an allowlist: `*/index.md`,
`*/thumb.png`, `*/rating/sessions/*/shots/*.jpg`; nothing else of a bundle becomes a page or a resource:
the technical pages come from `data/technical.json`, below),
`import-run.py`, `tools/play-pc.sh`, `tools/play-test.sh`.

### games/<id>/build.toml (and engines/<e>/build.toml)

How the game is built, installed and integrated (*read now*). TOML, a closed key set (`tools/gamedb/check.py`
BUILD_KEYS: an unknown key is an error, so a typo can't vanish). A published game needs `packages`, `suite`
and `needs_data`; a draft has a build.toml only when something about its build is known, and only those keys.

| Key | Type | Meaning |
|---|---|---|
| `packages` | list | the packages the app installs (`walnut-games-helper`; `tools/play-pc.sh` on the PC): Debian package names, none licensed; each one in the apt index (`check.py --apt-index URL`, e.g. the Debian snapshot's and our repository's Packages) or built by a `debian/control` of ours |
| `former_packages` | list | older names of the game's packages, which earlier sessions recorded their versions under (catalog.py reads them after `packages`): a renamed package keeps its version history |
| `engine` | string | the engine bundle in `engines/` it runs on; that engine's `engine.md` `games` lists the game (check.py checks both ways) |
| `suite` | string | `free`: in `ps3-games` and the public `ps3-games-free`; `licensed`: in `ps3-games` only (`ps3-games-free` suggests it), its data is licensed (the public image leaves its packages out: `tools/image-packages.sh`); `none`: in neither. Drafts count too (ClassiCube is a draft in `ps3-games`) |
| `meta` | list | the packages `ps3-games` and `ps3-games-free` name for the game, when not all of `packages` (e.g. `["neverball"]`: its data comes with it); each one of `packages` |
| `needs_data` | bool | the player supplies data the game can't run without (index.md then has `data_guide` and `data_steps`) |
| `data` | list | the data packages (often licensed) the game plays with: on the PC, `tools/play-pc.sh` mounts each one's files from a `.deb` found in build/ |
| `desktop` | list | the game's own menu entries the app starts (`<name>.desktop`) |
| `divert` | list | absolute paths of files of the game's packages we divert: the game's own menu entries, which our launchers stand for (`ps3-games-integration`'s preinst moves each to `/usr/share/ps3-games/diverted<path>`, out of the menu's reach) |
| `image_base` | bool | its packages are in the image's package layer (`tools/gamedb/build-info.py image-base`, the Makefile's `ROOTFS_GAMES`; system/image/packages.txt names only generic packages) |
| `recipes` | list | the bench runs that stand for the game, `"<id>:<variant>"` of its own `benchmark/recipe.sh` (the catalog's `recipes`) |
| `[data_setup]` | table | the player's own data, made into packages by game-data-packager (below) |
| `[[licensed]]` | tables | the game's licensed data: `name` (the package), `builder` (`source`: a source package of ours in `licensed/<name>/`; `script`: `licensed/<name>/<name>.sh` run on the owner's own copy, `make licensed-data PKG=<name> INPUT=<file>`; `game-data-packager:<game>`: `make game-data`; `retired`: published once, built no more, no directory), `leak_files` (paths of its content in upstream tarballs, leak globs), `meta` (bool: `ps3-games-licensed` depends on it; by default true for a game with suite `licensed`; BananaBread's `sauerbraten-nonfree-data` sets it), `edition` (what it holds, one of `[data_setup]`'s editions, for a package `[data_setup] editions` doesn't name; every entry has one or the other), `release` (the release of the source a `source` or `script` build is made from: `2020_12_29`, `2.1`), `input` (true: game-data-packager makes it from the owner's copy, `make licensed-data PKG=<name> INPUT=<image, folder or installer>`; `make game-data` only takes its published file). The edition and release are the deb's `X-Walnut-Edition` and `X-Walnut-Release` (below) |

Every `licensed/<pkg>/` directory of a bundle has its `[[licensed]]` entry, and every `source` or `script`
entry its directory (a checkout without licensed directories, the public copy, skips that direction).
Our licensed builds carry the edition and release fields the console's import writes (docs/PACKAGING.md "What it holds"): `edition` and `release` here, or `[data_setup] editions`; a source package's `debian/control` says them as `XB-X-Walnut-Edition`/`XB-X-Walnut-Release` and `check.py` holds both to build.toml.
The catalog's `patched_by` is no key: it is the source packages of ours in the bundle's `packages/` and its
engine's (`check.patched_by`). An engine's build.toml takes `packages` (the engine's packages its games run)
and `[[licensed]]` only.

Read by: `tools/gamedb/check.py` (validates), `catalog.py` (packages, needs_data, desktop, recipes, data_setup),
`tools/licensed.py` (`[[licensed]]` names and leak_files, `[data_setup]` names), `tools/gamedb/build-info.py` (for the Makefile's
`make game-data` and `make licensed-data`, `tools/image-packages.sh`'s licensed-suite games, `tools/play-pc.sh`'s
packages and data, `image-base`, `own-packages` for `make game-debs`, `fields` for the licensed builders and
`tools/gamedb/stamp-data-deb.py`, `bench-data` for `make bench-push`), `tools/gamedb/pack-integration.py`
(suite, packages, meta, divert, `[[licensed]]` meta: the metapackages' Depends and the integration package).

Versions: `walnut-games`, `ps3-games` and `ps3-games-licensed` carry `VERSION_PATHS` over
exactly the gamedb files they consume (index.md, thumb.png, build.toml, rating/test.toml,
rating/sessions/*/*, rating/sessions/*/shots/* and packages/*/patches/** for the app; build.toml and desktop/ for ps3-games, build.toml for
ps3-games-licensed), never a session's raw/, technical.md, investigations, todo.toml, benchmark/ or
research. `tools/deb-version.sh` reads a pattern there as GitHub's path filters do: `*` within one
path component, `**` across them.

#### [data_setup]

How the player's own copy of the game's data becomes packages (PLAN 5.10: Walnut Games and `walnut-data`
run Debian's game-data-packager, gdp, on a disc, an ISO, an installer or an installed copy). Separate from
`data`, which only `tools/play-pc.sh` reads. A closed key set (`check.py` DATA_SETUP_KEYS):

| Key | Type | Meaning |
|---|---|---|
| `gdp` | string | required: the gdp game (`game-data-packager <gdp> ...`), e.g. `quake`, `final-doom`, `ttd` |
| `required` | list | required: gdp packages any one of which makes the game playable (Plutonia or TNT); empty when the data is optional (OpenTTD's original graphics): then `needs_data` is false, else true |
| `optional` | list | gdp packages that add to it (music, the original set in place of a free one) |
| `downloads` | list | the packages (of the two lists) whose gdp run may download the files gdp knows for them: official patches (Quake II 3.20, Quake 1.06's pak0, TNT's MAP31 fix), nothing else |
| `fetch` | list | the shareware or demo packages (of the two lists) gdp downloads entirely, so `walnut-data import -p <pkg> <game>` needs no SOURCE and the app's chooser offers "Download the free <edition>" (`quake-shareware`, `quake2-demo-data`); other packages stay in `downloads` or nowhere |
| `alternatives` | list | groups of `required`/`optional` packages of which any one member is enough (`[["transport-tycoon-deluxe-music", "transport-tycoon-deluxe-music-fm"]]`): when one is made, `walnut-data` and the app do not report the others of its group as missing; a package is in one group at most; the catalog carries it |
| `hint` | string | where the files are, shown by the app (the CD, an installed copy, Steam, GOG) |
| `original` | string | the original commercial game's name, named in the label under the app's "Add game files..." button ("REQUIRED: the game files from the original <original> release ..." or "OPTIONAL: the original <original> graphics and sound") and in the chooser; default: the game's title. Set it where the title is the engine's (`Quake` for QuakeSpasm, `Transport Tycoon Deluxe` for OpenTTD) |
| `editions` | table | required: `{ "<pkg>" = "<edition>" }` for every `required` and `optional` package, one of `shareware`, `demo`, `full`, `expansion`, `music` (`check.py` EDITIONS): the `X-Walnut-Edition` of the package the console builds |

`check.py` checks the gdp game and every package against `tools/gamedb/gdp-packages.json` (game -> its
packages, and gdp's version), a committed file, so the check needs no docker and no network. After our gdp
changes (packages/patched/tools/game-data-packager), `make game-data-packager-deb` and then `make gdp-index`
(`tools/gamedb/gdp-index.py`: the built deb installed over Debian's gdp in a throwaway games builder
container, gdp's own loader run there, `--network none`) rewrite it; commit the result. Every `required` and `optional` name is licensed
(`tools/licensed.py`: never in `packages`, never published by us).

The import's manifest, `tools/gamedb/gdp-manifest.json`, lists every `required` and `optional` package's
files as that gdp builds them: path, size and checksum of each version gdp accepts (sha256 when gdp has
it, else sha1 or md5) with gdp's name for that version (`release`: `1.9ud`, `cd`, `106`), which files
are required, gdp's version, the package's same-game Conflicts/Replaces (quake-registered replaces
quake-shareware; nothing else of gdp's relations) and its edition from `editions`. walnut-games ships it
root-owned as `/usr/share/walnut-games/data-manifest.json`, and `walnut-games-helper import-data` accepts
nothing it doesn't list (docs/PACKAGING.md "Data built on the console"). `make gdp-manifest`
(`tools/gamedb/gdp-manifest.py`, our gdp's loader the same way, `--network none`) regenerates it:
after our gdp changes, after a `[data_setup]` names a new package or changes an edition.
Commit the result; `check.py` reports a package missing from it or with another edition, and the
walnut-games version moves with it (its `VERSION_PATHS`).

The catalog carries the table with every key filled in and `present_any`: `required`, then the game's
`data` and `[[licensed]]` names (the licensed suite's builds of the same data, for the owner's console:
`rct2-data` next to `roller-coaster-tycoon2-data`, `quake-shareware`). The game's data counts as installed
when any one of `present_any` is (the app's "Your game data is installed"; the helper's `remove`, which
keeps an engine another installed game still uses: dsda-doom for Freedoom, Doom and Heretic, and removes
only the game's own packages and its data). A game without `[data_setup]` has no `data_setup` in the
catalog.

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
plays, `tyrian-data.sh` in opentyrian; openrct2's `rct2-data.sh` is retired, game-data-packager makes
`roller-coaster-tycoon2-data` from the CD instead): text only, built on our own PC, never by CI, published to
the password-protected licensed suite only. A directory here is licensed by its location:
`tools/licensed.py` *reads it now* (with `packages/licensed/` and the `[[licensed]]` entries of build.toml),
the public copy drops `gamedb/*/*/licensed/*` from every commit (`tools/public-export.sh`), CI never
gets a workflow for it (`github-ci/gen-workflows.py`). `licensed.py check` wants every one declared in
its own bundle's build.toml.

### games/<id>/desktop/

How the game is integrated into the PS3 desktop, as files (`tools/gamedb/check.py` allows only these):

- `launchers/*.desktop`: our menu entries, one per way to start the game (OpenArena's two renderers); the
  Exec starts it through `ps3-game` (game mode). `pack-integration.py` adds `TryExec=` the program Exec runs
  (past `env` and `VAR=value`), so the entry shows only when the game is installed, and `X-Walnut-Game=<id>`:
  the Walnut Games app (`play_commands`) and `bench/harness/launcher.py --game` keep only a game's own
  stamped entries, so games on one engine (dsda-doom: Freedoom, Doom, Heretic) don't list each other's.
  An entry that needs the player's data (one per IWAD, Quake II's full game) has `NoDisplay=true` and
  `X-Walnut-Requires=<the data file, an absolute path>`; ps3-game-menu is to show it once the file exists
  (PLAN 5.10, *planned*).
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

- `test.toml` (*read now*, step 4): the played test, below.
- `sessions/<run>/` (*now*, step 6): one played session, the record and its evidence
  (`tools/gamedb/evidence.py`). The folder names are `<date>-<kind>`, with `-<hhmm>` for a second one that
  day; `tools/gamedb/import-run.py` writes all of it from a harness run directory, and nothing else may be
  in it (`check.py`):

  | File | What |
  |---|---|
  | `result.toml` | the record: the numbers, the versions, the game's data packages (`[data]`: `<pkg> = { version, edition, release }`, from the harness's `data:` lines; editions and releases as their `X-Walnut-*` fields say, else as gamedb's `games/<id>/data.txt` does, which `make bench-push` writes from build.toml: `tools/gamedb/build-info.py bench-data`; older runs have none), the picture verdict, the form's and the review's answers (data version 2), and `[shots] ps3`, the gallery: the pictures the site and the app show, as paths in the folder (`shots/ps3-60s.jpg`). Derived from the evidence below alone, so `tools/gamedb/reimport.py` derives it again without the run |
  | `form.toml` | the player's answers as saved (the run's `session.toml`) |
  | `review.md` | the reviewer's questionnaire as answered (the run's `REVIEW.md`, `tools/gamedb/review.py`) |
  | `shots/` | every screenshot of the session as JPEG, 800x450: `ps3-<s>s.jpg` the timed ones, `ps3-mark-<t>s.jpg` the player's own (Alt+F12, Select+Start); `pc/pc-<s>s.jpg` the PC reference the review sheet had (build/bench-ref) |
  | `raw/` | the harness's files a re-derivation reads, each scrubbed (home directories, MAC and LAN addresses, the console's and this PC's user and host names) and xz-compressed as `raw/<its path in the run>.xz`: `bench.txt`, `versions`, `hud/` (the frame times), `samples`, `result`, `temps`, `out.log`, `game.log`, `dmesg.log` (the run's part), `input.tsv`, `xruns.tsv`, `xruns.log`, the memory snapshots, `Xorg.0.log`, `shots/geometry.tsv`, `fresh-home/` (the game's settings as the session wrote them: text files up to 64 KB); `pc/` the reference's `bench.txt` and `versions`; `import.toml` the importer's inputs (the run's name, `--launcher`, `--shots`, `--rules`, what was left out; `frozen = true`: a record kept as an older importer derived it) |

  `shots/` and `raw/` are Git LFS files; CI fetches only the gallery (`tools/gamedb/gallery.py`). `raw/` is
  private: the public copy strips `gamedb/**/raw/**` (`tools/public-export.sh`), and `check.py`'s
  public-safe scan refuses, in `result.toml`, `form.toml`, `review.md` and every `raw/` file (read
  decompressed), a home directory, a LAN address, a MAC address, the private user and host names
  (from config.local.mk and this machine; the public copy has none to look for) and a `raw/` path that
  matches a licensed leak glob. The catalog, the site and the app name a screenshot by its path in the
  bundle, `rating/sessions/<run>/shots/<file>.jpg`, which is also its URL under `games/<id>/` on the site.

  Records without a rules version (`rules` absent: the 18 sessions imported before 2026-10-26 with rules
  1) are frozen: their numbers came from an importer whose derivation has changed since, so the backfill
  kept them as they were (`import-run.py --keep-record`) and `reimport.py` leaves them; `reimport.py
  --unfreeze --check` shows what today's derivation would make of them (their ratings would move).

catalog.json is a cache of the ratings computed from these at build time.

#### rating/test.toml

The played test as data: the session harness and the app read it, nothing in it is run. TOML, a closed
key set (`bench/harness/playdef.py` KEYS; `tools/gamedb/check.py` runs its `validate()`, an unknown key is an error):

| Key | Type | Meaning |
|---|---|---|
| `process` | string | required: the game's process name (`pgrep -x`: peak RSS, the per-client VRAM; `tools/play-test.sh stop` ends it) |
| `home` | list | required: the game's settings and data directories under ~, relative, globs allowed, never one every game shares (`.config`, `.local/share`, ...): a session sets them aside and runs the game on fresh defaults (`bench/harness/freshhome.py`) |
| `play_mark` | string | a regex on the game's output: the first gameplay frame (a map loaded, a race started): bench.txt `play_from=mark`, `play_start`, `play_marks`, and the HUD window starts there; `import-run.py` reads it for runs recorded before `play_marks=` |
| `load_mark` | string | a regex on the game's output: the load's end (`play_from=load` when there is no play mark) |
| `command` | list | the command a player starts, the program as an absolute path: the session runs it when the game has no menu launcher (a launcher's Exec wins); without it the recipe's `CMD` |
| `env` | list | `NAME=value`: the game's environment in a session, instead of the recipe's `ENV` (the benchmark's switches); only what the recording needs (Wesnoth's log kept on stderr) |
| `sound_at` | integer | seconds from the start when the sound card's state is read (default 15) |
| `hud` | bool | the Gallium HUD's frame times (default true) |

Every game with a benchmark recipe has one, and its recipe has no session part (`bench/tests/selftest.py`: the
file is valid, the marks are bash regexes too, `process` is the recipe's `COMM`). `bench/harness/session.sh` and
`bench/harness/client.sh` read it through `playdef.py` (shlex-quoted `TEST_*` assignments; `bench_playdef` in
bench/harness/games.sh), `make bench-push` puts it on the PS3 as `/var/tmp/bench/games/<id>/rating/test.toml`.
A session takes no recipe variant (the menu launchers took their place); the recipe's base still gives
the recording settings (`LOAD_RE` and the like). A game without a test.toml runs its recipe as it is,
on fresh defaults from the top directories of its `BACKUP` files.

### games/<id>/benchmark/

The A/B benchmark of game and driver optimizations (docs/games/SUITE.md), *read now* (step 1c) by the
harness in `bench/`:

| File | What |
|---|---|
| `recipe.sh` | the recipe: code, sourced by `bench/harness/client.sh` (below) |
| `analysis.toml` | data for the Python analysis tools (below) |
| `lib/` | the game's own helpers (Hedgewars' `hwdrive.py`), reached as `"$GAME_DIR/lib/..."` |
| `assets/` | the files a recipe puts in place (demos, replays, saves, maps, mods), reached as `"$ASSETS/..."`; small normal files, not Git LFS |
| `assets-src/` | how built assets are made: an `assets.mk` the top-level Makefile includes (ClassiCube's plugin, `make classicube-bench`; `make bench-assets` makes all), or the scripts that made them (OpenTTD's saves) |
| `legacy/` | superseded tools kept for the record (OpenArena's step 0) |
| `results/<batch>.toml` | *now* (step 5): the game's numbers in one batch ([benchmarks/README.md](benchmarks/README.md)); `bench/analysis/matrix.py` writes them |
| `runs/<run>/raw/` | *planned* (step 6) |

Where the harness finds them: `bench/harness/games.sh` (shell) and `bench/harness/games.py` (Python) resolve a name to
the game's benchmark directory, here in the repository and in `/var/tmp/bench/games/<id>/` on the PS3
(`make bench-push` copies `recipe.sh`, `analysis.toml`, `lib/` and `assets/` of every bundle there).
A game is named by its id; `bench.txt` gets `game=` and `recipe=` (both the id). A run from before
the move (no `recipe=`) names the old recipe file, so its `game=` and its directory name are read
through the `[legacy] recipe_names` first (game=sauerbraten in such a run is BananaBread); `run.sh`
also takes an old name that is no id (`sauerbraten-edition`). Read by `bench/harness/client.sh`, `run.sh`
(`list`, variants, `suite`), `session.sh`, `ssd-arm.sh`, `matrix.py` (run labels), `selftest.py` (every
recipe and variant sourced), the analysis tools through `bench/harness/gamedata.py` (`analysis.toml`),
`tools/play-test.sh`, `tools/play-pc.sh` (`REF_IMAGE`), `tools/bench-ref.sh` (the container sees `bench/`
and the game's benchmark directory) and `tools/gamedb/check.py` (`benchmark/` holds only these files; a
recipe.sh; `analysis.toml` in gamedata.py's schema).

#### benchmark/analysis.toml

The data the analysis tools need about the game (*read now*, step 4): `bench/analysis/report.py`, `logscan.py`,
`matrix.py` and `interference-scan.py` read it through `bench/harness/gamedata.py` and hold no game of their own.
TOML, a closed key set (`gamedata.validate_analysis`, run by check.py); every regex compiles:

| Table | Keys | Read by |
|---|---|---|
| `[legacy]` | `recipe_names`: the recipe's file names before it moved here (`sauerbraten` in bananabread, `sauerbraten-edition` in sauerbraten): runs from before then are read as this game's and grouped with its new ones; `run_labels`: `{ re, variant, drop }`, an older harness's run directory names without a bench.txt (OpenArena's `oa-*`: the label matching `re`, the variant its expansion with each `drop` string removed) | games.py, games.sh, report.py and logscan.py (grouping), matrix.py (labels) |
| `[reload]` | `re` (the output line that ends a looped demo's reload) and `after` (seconds after it, default 1), or `fixed` (seconds after the pass's start) | report.py's `*_play` numbers, passsplit.py |
| `[[log_ignore]]` | `re`, `why`: log lines that are this game's noise (case-insensitive) | logscan.py, on this game's runs only, after its generic list |
| `[[log_strip]]` | `re`, `why`: text removed from each log line before messages are compared | logscan.py |
| `[interference]` | `file` (in the run directory), `action_re` (groups: the time, the action, the rest), `time_format` (strptime), `anchor_re` (group 1 a time that is the run's `load_end`, for runs without `t0`), `evidence` (the words after the count): the game's own log of what the player did | interference-scan.py |

An engine's `analysis.toml` (`engines/<e>/analysis.toml`: `[[log_ignore]]` and `[[log_strip]]` only)
holds the rules every game on the engine shares (its build.toml `engine`); they come before the game's
own. ioquake3's: the SIGTERM line Quake 3 engines print when the harness stops them, the colour codes.

#### benchmark/recipe.sh

One bash file per game, sourced by `client.sh` (variables and functions only, nothing runs at source
time). `client.sh <game> <variant>` sources it, then calls `variant_<variant>` if it exists (`base`
needs none). Before it is sourced, `GAME_DIR` is the game's benchmark directory, `ASSETS` its
`assets/` and `BENCH_LIB` the directory of the generic pieces the recipes share (`bench/lib/`): `"$BENCH_LIB/smoke.inc"` (the header of the smoke recipes: `SUITE=0`, a 120 s
`DURATION` that `SMOKE_SECS` overrides, a short HUD window, `vblank_mode=0`, and `smoke_keys` /
`smoke_xkeys` for keys into the game; a smoke recipe is a game tried once to see whether it starts and
plays, docs/games/SUITE.md "Smoke runs"), `"$BENCH_LIB/openal.inc"` (the OpenAL Soft variants `allog`,
`b16`, `f44`, `b16f44`, `p1024`, `alnull` the OpenAL games' recipes share, docs/SPU-AUDIO.md M0, with
an optional perf profile, `OAL_PERF=N`, that `bench/analysis/oal-perf.sh` splits by symbol),
`"$BENCH_LIB/keyboard.evemu"` (a keyboard's description for an evemu virtual keyboard: ETR's
recording without its events) and `"$BENCH_LIB/xkeys.py"` (keys through XTEST).

| Name | Meaning |
|---|---|
| `CMD=(...)` | the game command, absolute path |
| `ENV=(...)` | `NAME=value` for the game's environment |
| `SUITE=1` | part of `run.sh suite` |
| `COMM` | the game's process name (ps3rsx per-client VRAM, peak RSS) |
| `BACKUP=(...)` | files the game rewrites (configs): restored after every run |
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
| `HUD_SKIP` | HUD seconds left out as loading when there is no load time (default 12) |
| `RESULT_FILE` | files the game writes, copied into the run directory |
| `during()` | runs in the background while the game runs (its output in during.log), stopped at the end |
| `parse()` | prints extra `key=value` lines into bench.txt |

A played session's part (the process, the directories set aside, the play and load marks, the command
without a launcher, the environment) is no recipe variable: it is the game's `rating/test.toml` (above).
A session drops the recipe's `vblank_mode=0`, `during()`, `parse()`, `END_RE`, `DURATION` and the
passes and windows.
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
maxms counted`) for report.py. `python3 bench/tests/selftest.py` checks client.sh, report.py and
matrix.py with a fake game and made-up runs, and sources every recipe and variant.

### games/<id>/technical.md, investigations/

`technical.md` (*now*: every published game has one, which `check.py` requires, and the drafts that were
tried; step 8 moved docs/games' per-game files into them) is the current state, not a dated log: the
engine and version, our packages and why (linking the patches), the settings and defaults, how it
performs (citing `gamedb:batch/<name>`), the known problems, what was tried, and links to the
investigations. A draft's starts with a "Status" section when it was tried, has an "Engine options" list
when it has several, and a "Research" section: what the surveys said, each line with its source file,
section and date. An
investigation, `investigations/<date>-<slug>.md`, has front matter `id` (never renamed), `games`,
`driver_patches`, `game_patches`, `sessions`, `batches`, and a body: symptom, evidence, cause, fix.
A patch is named `<package>/<number>` (`mesa/0046`, `cube2/0010`, `linux/local-0009`), a session by its
folder under the game's `rating/sessions/`, a batch by its name in `benchmarks/batches.toml`;
`tools/gamedb/check.py` (*now*) checks the keys (a closed set, `id` required, the rest lists of strings),
that `id` is unique in its folder, and that the games, sessions and batches exist. Its body may carry
generated blocks (`batch-table`: a batch's numbers, `tools/gamedb/docblocks.py`).
`gamedb:<id>/<investigation-id>` resolves by that `id` (`tools/doclinks.py`, *read now*). A driver bug a
game found: the investigation lives with the game, the mechanism in docs/DRIVER.md with a "found by"
link. Several games on one engine: `engines/<e>/investigations/`; games on different engines: where it
was found, linked from the others.

On the site (*now*, step 7a): `tools/gamedb/technical.py`, run by catalog.py, writes
`website/gamedb/data/technical.json`, and Hugo's content adapters (`website/gamedb/content/technical/`,
`engines/`, `research/`: `_content.gotmpl`) make the pages from it, for published games only (a draft has
no page, its name is in no list, a link into its bundle keeps its text and loses the link):
`games/<id>/technical/` (technical.md, then the build facts of build.toml with our packages and their
patches, licensed data by name only, the played test of rating/test.toml with the medal, the recipe's
header and variants, the benchmark tables of `benchmark/results/`: the summary, then each batch newest
first, a column per metric with its unit), `games/<id>/investigations/<inv-id>/` (front matter as facts,
then the body), and the index `technical/`, a row per published game. A published game without a
technical.md gets its page from the data alone, which says it has no notes yet. A file's first `# `
heading is its page's title. Relative links are rewritten: to a survey, a published game, its technical
page or investigation, an engine: that page; any other file of the repository: the public repository
on GitHub; a draft's bundle, a licensed directory, raw/, a path that doesn't exist: the text only.
References become links too: `gamedb:<id>` to the game's technical page (an engine's page),
`gamedb:<id>/<inv-id>` to the investigation's page with its title as the text, `gamedb:batch/<name>` to
that batch's table (`#batch-<name>`) on the page's own game's technical page, or on the first published
game with results in it (the engine's games first); a draft's id stays plain text. Every
string of technical.json passes the public-safe scan first (tools/gamedb/evidence.py `leftovers`, and the
patterns of tools/public-replace.local.txt), or the build stops. The app's catalog never holds it
(pack-catalog.py packs catalog.build() only).

### games/<id>/todo.toml, upstream.md

`todo.toml` (*now*, step 8; an engine's at `engines/<e>/todo.toml`, the same form): the game's backlog,
its items of docs/PLAN.md and of docs/backlog/, and its place in the played-session queue. One
`[[item]]` table per item, the keys a closed set (`tools/gamedb/check.py` `todo_errors()`):

| Key | Type | Meaning |
|---|---|---|
| `kind` | string | `session` (a played session), `benchmark` (a scripted run: an A/B, a repeat, a smoke run), `package` (packaging, a default to set), `fix` (a game or engine patch), `research`, `upstream` |
| `text` | string | what to do, with the numbers it rests on and how to start; current state, not history |
| `added` | date | when the item was first written down (for a migrated item, its source's date) |
| `source` | string | where it came from: `PLAN 2.6`, `backlog/games.md rank 19`, `TESTS.md Next`, ... (one PLAN item per `PLAN N.N`) |
| `order` | integer | a session item's place in the played-session queue (1 first; games may share a place) |
| `launcher`, `pc` | string | a session item's launchers (PS3 and PC) and the PC side's state |

Generated from them (`tools/gamedb/blocks.py`): the played-session queue in docs/TESTS.md
(`session-queue`: the session items with an `order`, with each game's medal from the catalog) and the
per-game summaries in docs/PLAN.md and docs/backlog/games.md (`todo-summary`). Never part of a
package's version, never on the site.
`upstream.md` and `upstream/*.patch` (*now*, teeworlds and yquake2): what we would offer upstream of our
game patches; docs/upstream/README.md links every draft.

### engines/<e>/

`engine.md` (*now*: TOML front matter between `+++` lines with `id`, `title`, `homepage`, `license` and
`games = [...]`, the bundles that run on it; then a short note), `packages/<pkg>/` (*now*: cube2,
ioquake3, darkplaces-gl2), `build.toml` (*now*: `packages`, `[[licensed]]`), `analysis.toml` (*now*: the
log rules its games share, [above](#benchmarkanalysistoml)), `technical.md`, `investigations/` and
`todo.toml` (*now*, step 8), as for a game. Its page on the site, `engines/<e>/` (*now*): engine.md's note, its technical.md, our packages
with their patches, the published games on it; its investigations at `engines/<e>/investigations/<inv-id>/`.

### benchmarks/batches.toml, benchmarks/metrics.toml, games/<id>/benchmark/results/

*Read now* (step 5): the benchmark results as records, their format in
[benchmarks/README.md](benchmarks/README.md). `batches.toml` has each batch once, as `[[batch]]` tables
(`name`, `kind` = `run` or `summary`, `order`, `title`, `date`, `runs`, `stack`, `observations`, `purpose`,
`source`); `gamedb:batch/<name>` resolves against these names (`tools/doclinks.py`). `metrics.toml`: each
metric's name, unit, direction, whether it has a CSV matrix, and the tool that computes it.
`games/<id>/benchmark/results/<batch>.toml`: the game's numbers in that batch, `[variant."<variant>"]`
tables of metric = value. `interfered.txt`: the runs found disturbed after the fact, which report.py and
matrix.py leave out. Written by `bench/analysis/matrix.py` (a batch's runs, from build/bench), read by
`bench/analysis/benchrecords.py` for matrix.py, `make bench-matrix` (the CSV matrices into build/bench-matrix/),
`tools/gamedb/check.py` (validates: closed keys, every result's batch exists, metric names known),
`tools/gamedb/score.py` (the summary batch `suite-summary-2026-10`: the rules-1 calibration) and
`tools/gamedb/blocks.py` (docs/GAMEDB.md's calibration table, README.md's suite table).
`tools/gamedb/bench-import.py` made them from the old CSV matrices (the files under bench/ until
2026-10-26, removed after its `--verify`).

### research/<slug>.md

*Now* (step 7): surveys across games, `game-candidates.md` (the suite's 2026-10-16 pick),
`game-reeval.md`, `gl3-census.md` with its parts `gl3-census-<part>.md`, `pc-classics.md`. Their method,
scope and conclusions are text; their per-game tables are still kept by hand (*planned*: generated blocks,
`tools/gamedb/blocks.py`); per-game sections longer than ~50 lines moved into the game's investigations/
with a link left behind. Each one is a page on the site, `research/<slug>/` (*now*, technical.json; its
first `# ` heading the title). The census's driver part (docs/research/gl3-census/G-driver.md) and the emulator
and platform research (EMULATORS, CLASSIC-MACOS, DARLING-PPC) stay in docs/research/.

## Generated from here

| Output | Tool |
|---|---|
| catalog.json (site, app); technical.json (site only: the technical, investigation, engine and research pages) | `tools/gamedb/catalog.py` (technical.json through `tools/gamedb/technical.py`) |
| the app's catalog.tar | `tools/gamedb/pack-catalog.py` |
| the integration tree, the ps3-games / ps3-games-free / ps3-games-licensed Depends | `tools/gamedb/pack-integration.py` (`make gamedb-integration`) |
| `PKG_DIRS`, `SRC_PACKAGES` (every package of ours, in build order) | `tools/pkg-dir.sh --all`, `--order` |
| orig recipes and `DEB_ENV`'s tarball variables | the packages' `orig.mk`, included by the Makefile |
| `make game-data`, `make licensed-data`, the public image's licensed-suite games, play-pc's packages and data | `tools/gamedb/build-info.py` |
| licensed names and leak globs | `tools/licensed.py` |
| CI workflows | `github-ci/gen-workflows.py` (`--check` in make test) |
| doc blocks (`licensed-names`, `calibration`: docs/GAMEDB.md, `suite-table`: README.md) | `tools/gamedb/blocks.py --write`, `--check` |
| the CSV matrices of the benchmark results | `make bench-matrix` (`bench/analysis/matrix.py --csv`) into build/bench-matrix/ |
| the PS3's bench tree (`/var/tmp/bench/games/<id>/`) | `make bench-push` |
| a session's record (`result.toml`) from its evidence | `tools/gamedb/import-run.py`, `tools/gamedb/reimport.py` (`--check` in make test) |
| the gallery's list for `git lfs pull -I` in CI | `tools/gamedb/gallery.py --lfs-include` |

## Checks

`make test` runs `tools/licensed.py check`, `github-ci/gen-workflows.py --check`,
`tools/gamedb/blocks.py --check`, `tools/doclinks.py` (report only) and `tools/gamedb/game-mentions.py`
(a warning: the sections of docs/ and README.md that name a game without a link into its bundle;
`--list` shows them, `--fix` links each one's first mention of the title). During the
migration, `tools/gamedb/baseline-check.py check` compares a phase's result with the baseline saved
before it started (build/tmp/gamedb-baseline/), and `tools/gamedb/catalog.py --diff OLD` lists every
field of the catalog that changed against an older catalog.json (games and tests matched by id).
