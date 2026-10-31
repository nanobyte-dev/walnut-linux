# Benchmark results

Every number the A/B benchmarks measured (docs/games/SUITE.md) is a record here, in gamedb. A
**batch** is one tested configuration or one fix: what was installed (Mesa ps3.NNN, ps3rsx, libspeq, a
game package), what was switched on, and the runs made with it. Runs that share a configuration share
a batch; when a fix changed something, the runs after it start a new one. Batches from a failed smoke
test are kept, with the failure in the observations and odd or no numbers. Benchmarks never rate a game
(docs/GAMEDB.md): a medal comes from played sessions only.

| File | What |
|---|---|
| `batches.toml` | every batch once, in the order they were run |
| `metrics.toml` | every metric once: name, unit, which way is better, whether it has a CSV matrix, the tool that computes it |
| `caveats.toml` | batches (or one game's rows in them) whose numbers aren't what the column says or aren't comparable with plain runs ("Caveats" below); the Test runs tab marks those rows |
| `interfered.txt` | runs found disturbed after the fact (`bench/analysis/interference-scan.py --write`, docs/STALLS.md "Interference"): the run directory, then the evidence; `bench/analysis/report.py` and `bench/analysis/matrix.py` leave them out. A list of runs, not of batches: a batch keeps its clean runs |
| `gamedb/games/<id>/benchmark/results/<batch>.toml` | one game's numbers in one batch |

`bench/analysis/benchrecords.py` reads and writes all of them (stdlib only); `tools/gamedb/check.py` validates them
(closed key sets, unique names, run batches in order, every result's batch exists, metric names known) and
warns on a batch without `tested` or `conclusion`;
`gamedb:batch/<name>` links resolve against the batch names (`tools/doclinks.py`).

## batches.toml

`[[batch]]` tables, oldest first. Keys (a closed set):

| Key | Type | Meaning |
|---|---|---|
| `name` | string | required, `^[a-z0-9][a-z0-9.-]*$`: the date and a few words (`2026-10-18-batch16`, `2026-10-19-ssdroot5-openal`). Never renamed: `gamedb:batch/<name>` links point at it |
| `kind` | string | required: `run` (runs on the PS3, a row of the CSV matrices) or `summary` (numbers gathered from other batches and docs, with a source each: the suite's calibration) |
| `order` | integer | a run batch's place in the matrices (the old CSV row number); increasing through the file |
| `title` | string | a run batch's name as the matrices' `test` column shows it: what was installed or switched on |
| `date` | date | the UTC date of the newest run in it |
| `runs` | integer | the run directories behind it, tests included: driver tests (`gl-*`, `t-*`, `xtest*`, `xfix*`), `speq-*` and `dxt-*` runs count but make no number |
| `stack` | inline table | the platform versions the title names, when it does: `kernel`, `mesa`, `ps3rsx`, `libspeq`, `ps3-gpu-tools`, `sdl3` (strings) |
| `tested` | string | by hand, for a reader of the website: one or two plain sentences on what was being tried and why ("Whether running Luanti's background threads at a lower priority stops the main thread's stutters at view range 120.") |
| `conclusion` | string | by hand: one to three plain sentences on what it showed and what was decided ("Yes: stalls fell from 31 s to 1.8 s per 135 s and the frame rate rose from 21 to 38 fps; shipped as luanti 0010."). An invalid batch (nothing on screen, interfered, superseded) says so; where the record has no decision, the numbers and "No decision recorded." |
| `observations` | string | by hand: what was observed. Did the games run, was the picture right, was the sound right, what bug was found, which fix (package version, patch) it proves, and caveats (perf recording during the run, nothing on screen, a single run, an outlier, interference). Short plain sentences, or `- ` lines in a multi-line string; every number that carries a finding with what it means; no run-time windows, file names or harness internals unless they are the point; say when a cause is not known |
| `purpose` | string | why the batch was run (a summary's: what it gathers) |
| `source` | string | required: where it came from (`bench/results row N`: the CSV matrices it was imported from, 2026-10-26; `bench/analysis/matrix.py`) |

## games/<id>/benchmark/results/<batch>.toml

```toml
batch = "2026-10-19-ssdroot1-baseline"
column = "sauerbraten"          # only when the runs named the game otherwise (below)

[variant."base@rec10"]
fps = 36.0
fps_steady = 39.1
stall_play_s = 0.2
```

`batch` is the file's name. Each `[variant."<variant>"]` table holds that game-variant's numbers in the
batch, every metric it has, keyed by `metrics.toml`'s names: the median over the batch's runs of that
game-variant, rounded to 0.1 (with two runs, their mean). The variant is the recipe's variant as the run
recorded it, with the harness suffix (`@rec10`: the stall recorder at 10 ms, `@wkkick10`: the waker kick,
`+force_dxt_stats50`: a switch; report.py's labels). A metric a run didn't produce is absent: the game
wasn't measured that way (a failed launch, a run killed before its result, a run before the steady
state). `column`: runs from before the recipes moved into gamedb (2026-10-26) name BananaBread by its old
recipe, `sauerbraten`; the file keeps that name so the matrices come back with their old columns.

A summary batch's entries carry, beside the numbers, `label` (the row's name), `order` (its place in the
summary), `note` (what the numbers don't say: picture and sound) and a `[variant.<v>.source]` table: for
each value, the batch and column it was taken from (`gamedb:batch/<name> <game>-<variant>`) or the doc it
was read from where no single cell holds it (a range, a reading of a sound column).

`suite-summary-2026-10` is the suite's 13 games (15 rows: OpenArena's two renderers, OpenTTD's two
blitters) as of 2026-10: `tools/gamedb/score.py` reads it as the rules-1 calibration, docs/GAMEDB.md's
calibration table and README.md's suite table are generated from it (`tools/gamedb/blocks.py`
`calibration`, `suite-table`).

## metrics.toml

`[[metric]]`: `name`, `unit`, `better` (`higher` or `lower`), `matrix` (`always`: a CSV matrix of its
own; `when-set`: one once a batch has the number; `none`: no matrix, a summary's number), `tool` (what
computes it: bench/analysis/report.py's key, then matrix.py's median), `what`. In short:

| Metric | Value | Better |
|---|---|---|
| `fps` | the game's own frame rate (timedemo, replay or fixed-frame counter), else the Gallium HUD median after loading | higher |
| `cpu` | the client's user+sys seconds for the whole run | lower |
| `underruns` | ALSA underruns in the run (audible chopping) | lower |
| `ram_free` | lowest MemAvailable during the run, MiB | higher |
| `vram` | the game's peak VRAM (ps3rsx per-client, all its buffers), MiB | lower |
| `fps_steady` | the steady-state frame rate: the median of the game's own fps over its counted passes, the game's own result over its window, else the median of the HUD's per-second fps in the window | higher |
| `low1` | the 1% low in the window: 1000 / the 99th percentile frame time (HUD frame times; with per-second data only, the slowest 1% of seconds) | higher |
| `stalls` | frames over 100 ms in the window (per-second data only: seconds under 10 fps) | lower |
| `longest_ms` | the longest frame in the window, ms (HUD frame times, else the game's own per-pass longest) | lower |
| `stall_disk_s`, `stall_swap_s`, `stall_reclaim_s`, `stall_gpu_s`, `stall_cpu_s` | seconds of the window's stalls spent on each cause (runs recorded with `STALLS=1`; docs/STALLS.md says how a cause is decided) | lower |
| `stalls_play`, `stall_play_s`, `longest_play_ms`, `low1_play` | the same four over the gameplay only: the window less the reload of a game that loops its demo (OpenArena's map reload at each pass, Neverball's replay restart; docs/STALLS.md). A game without a reload has the plain numbers; a run that can't tell (OpenArena before `t0`, 2026-10-17 23:00) has none; batches before 2026-10-18 have them only where their runs were fed in again | lower / lower / lower / higher |
| `sound_faults_per_min` | sound faults a minute (the summary's; played sessions compute their own) | lower |

The steady-state metrics (since 2026-10-17, the user's decision): every run leaves out a warm-up
(`WARMUP`, 60 s after the game's load ends) and measures at least `MEASURE` (120) s after it; each
recipe's header says how (the demo repeated in one session, the game's own window, or the HUD
window). Runs before that have none. `fps` keeps its meaning where it can: for a repeated demo its
value is the first pass, the run as it was before. Where the run itself changed length (SuperTuxKart's
profile 185 s instead of 60, BananaBread's and Luanti's longer runs, X-Moto's replay now finishing and
printing its average over the whole replay), compare `fps` across that date with care, and use
`--hud xmoto` to keep X-Moto's `fps` the HUD median as before.

## Things to know when reading a number

- Times in the observations are UTC (the PS3's local time is UTC+3).
- Batches 1-10 (order; until 2026-10-16 20:27Z) are not valid frame rates: nothing reached the screen
  (no window manager, then ps3rsx left the video muted for every X after the first, fixed by
  a1f1b91). They stay as history; load times, VRAM and the SPU results in them do not depend on
  the display. The first valid game numbers are in gamedb:batch/2026-10-16-stage0-validation.
- Runs of the first day have no `bench.txt` (the old OpenArena harness, or a run cut short). Their
  game and variant come from the label (`oa-tc1` is `openarena-tc1`), OpenArena's fps from the
  timedemo line in `result`.
- Runs before bench.txt recorded `underruns=` have the count taken from the `underrun occurred`
  lines in `game.log` (the numbers are the same kind, counted afterwards).
- `fps` mixes the game's own fps and the HUD median where a recipe has no own counter. Armagetron
  batches with a playback average use `--hud armagetronad` so every value is the HUD median (the same
  option picks the HUD window median for `fps_steady`).
- Perf-recording runs (the "allog" OpenAL runs, the step 0 profiles) and the profile round of
  Mesa ps3.223 are slower than plain runs; their numbers are not for comparison.
- SuperTuxKart's own "Average FPS" is its physics tick rate (120 Hz x the race time: 21602 ticks in
  every run), not a frame rate. Its `fps` and `fps_steady` are the Gallium HUD's median (19-22 fps);
  batches 61, 68 and 69 (gamedb:batch/2026-10-18-batch8, gamedb:batch/2026-10-18-batch16,
  gamedb:batch/2026-10-18-batch17) had 106-111 in them until the 2026-10-19 results commit.
- Single runs vary: OpenArena (23 to 54 fps) and QuakeSpasm (66 to 84 fps) both did on Mesa
  ps3.262. A difference of a few fps between batches with one run each is noise.

## Caveats

`caveats.toml` holds `[[caveat]]` tables: `batch` (required, a name in batches.toml), `game` and `variant`
(optional: only that game's rows, only that variant), `kind` (`invalid`: the frame rate is not a frame rate of
the picture shown, e.g. nothing reached the screen, or the HUD's number stands in for a missing fps line;
`not-comparable`: measured right but slowed by a profiler or another recording) and `why` (one sentence,
from the batch's observations). `tools/gamedb/check.py` validates it (caveats_errors); the game's Test runs
tab shows the caveat above the batch's table and flags its rows. A batch keeps its numbers: the caveat only
says how to read them.

## Adding a batch after a session

1. `make bench-pull` copies the run directories into `build/bench/`
   (`ps3-bench-<UTC time>-<label>`).
2. Pick the runs of one configuration and run, from the repository root:

   ```
   python3 bench/analysis/matrix.py "Mesa ps3.290 + ps3rsx 0.249" --name 2026-10-27-mesa-290 \
       --obs "what the batch shows" build/bench/ps3-bench-20261027T*-armagetronad-*
   ```

   The first argument is the batch's title (or the name of one that exists: the runs join it); a new
   batch goes after the others, so add batches oldest first. `--name` picks its name (else the date and
   the title's first words). The runs' `versions` file names the game package, Mesa, libspeq,
   ps3-gpu-tools, the kernel, ps3rsx, fluidsynth, OpenAL and SDL3.
3. Options: `--replace` drops the batch's old numbers, runs and date first (otherwise a batch
   is extended when you name it again, and its run count adds up; `tested`, `conclusion` and the
   observations stay, `--obs` replaces the observations); `--hud GAME[,GAME]` takes the HUD
   median as the `fps` value for those games; `--include-interfered` keeps disturbed runs.
4. Look at the result (`make bench-matrix` writes the CSV matrices into `build/bench-matrix/`;
   `column -s, -t < build/bench-matrix/fps.csv` is wide, a spreadsheet is easier), write the batch's
   `tested` and `conclusion` and fix its observations in `batches.toml` (matrix.py keeps all three when it
   rewrites the file; `tools/gamedb/check.py` warns while `tested` or `conclusion` is missing), and commit `gamedb/benchmarks/batches.toml` and the new
   `gamedb/games/*/benchmark/results/` files.

## The CSV matrices

`make bench-matrix` (`bench/analysis/matrix.py --csv DIR`, `benchrecords.to_csv`) makes one CSV per metric with a
matrix: one row per run batch in order, columns `test` (the title), `date` (YYYYMMDD), `runs`,
`observations`, then `<game>-<variant>` sorted by name (`openarena-tc1`, `dsda-doom-playspu`, ...), only
the columns with a value; a cell is the record's value. They are build output, not kept: until
2026-10-26 these files were the records themselves (`bench/results/*.csv`, "fps.csv row N" in older
docs is the batch with `order = N`). `tools/gamedb/bench-import.py` turned them into these records and
`--verify DIR` checks a directory of them against the records: each of the 4870 cells found in exactly
one record with the same value, and the matrices made from the records byte for byte the old files.

## The technical pages

A game's Test runs tab on the site (`games/<id>/test-runs/`, under "Scripted benchmarks", from `technical.json`: `tools/gamedb/technical.py`,
run by `tools/gamedb/catalog.py`) shows its results as tables made from these records: the summary batch
first, then the run batches newest first, each with its date, title, run count, stack and observations,
a row per variant and a column per metric the game has in that batch, with the metric's unit from
`metrics.toml` (`benchrecords.results()` gives `{batch: {game: table}}`, `load_batches()` the batches'
order and facts). Tables only, no graphs yet.
