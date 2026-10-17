# Benchmark result matrices

One CSV per metric. Every file has the same rows, in the order the configurations were tested
(oldest first), and the same first four columns, then one column per game-variant.

| File | Cell | Better |
|---|---|---|
| `fps.csv` | the game's own frame rate (timedemo, replay or fixed-frame counter), else the Gallium HUD median after loading | higher |
| `cpu.csv` | the client's user+sys seconds for the whole run | lower |
| `underruns.csv` | ALSA underruns in the run (audible chopping) | lower |
| `ram_free.csv` | lowest MemAvailable during the run, MiB | higher |
| `vram.csv` | the game's peak VRAM (ps3rsx per-client, all its buffers), MiB | lower |

Columns: `test` (the row name), `date` (UTC date of the newest run in the row, YYYYMMDD), `runs`
(run directories behind the row), `observations`, then `<game>-<variant>` columns sorted by name
(`openarena-tc1`, `dsda-doom-playspu`, ...). A cell is the median over the row's runs of that
game-variant; with two runs that is their mean. An empty cell means the game-variant was not run in
that row, or the run produced no such number (a failed launch, a run killed before its result).

## What a row is

One tested configuration or one fix: what was installed (Mesa ps3.NNN, ps3rsx, libspeq, a game
package), what was switched on, and the runs made with it. Runs that share a configuration share a
row; when a fix changed something, the runs after it start a new row. Rows from a failed smoke test
are kept, with the failure in `observations` and cells empty or odd. Row names have no commas.

`observations` is the point of the row: did the games run, was the picture right, was the sound
right, what bug was found, which fix (commit, package version) the row proves, and caveats
(perf recording during the run, nothing on screen, a single run, an outlier). Write plain sentences
with names and numbers, and say when a cause is not known.

`runs` counts every run directory in the row, tests included: driver tests (`gl-*`, `t-*`,
`xtest*`, `xfix*`), `speq-*` and `dxt-*` runs count but make no cell.

## Things to know when reading a cell

- Times in the observations are UTC (the PS3's local time is UTC+3).
- Rows 1-10 (until 2026-10-16 20:27Z) are not valid frame rates: nothing reached the screen
  (no window manager, then ps3rsx left the video muted for every X after the first, fixed by
  a1f1b91). They stay as history; load times, VRAM and the SPU results in them do not depend on
  the display. The first valid game numbers are in "Stage 0 validation".
- Runs of the first day have no `bench.txt` (the old OpenArena harness, or a run cut short). Their
  game and variant come from the label (`oa-tc1` is `openarena-tc1`), OpenArena's fps from the
  timedemo line in `result`.
- Runs before bench.txt recorded `underruns=` have the count taken from the `underrun occurred`
  lines in `game.log` (the numbers are the same kind, counted afterwards).
- Cells mix the game's own fps and the HUD median where a recipe has no own counter. Armagetron
  rows with a playback average use `--hud armagetronad` so every cell is the HUD median.
- Perf-recording runs (the "allog" OpenAL runs, the step 0 profiles) and the profile round of
  Mesa ps3.223 are slower than plain runs; their cells are not for comparison.
- Single runs vary: OpenArena (23 to 54 fps) and QuakeSpasm (66 to 84 fps) both did on Mesa
  ps3.262. A difference of a few fps between rows with one run each is noise.

## Adding a row after a session

1. `make bench-pull` copies the run directories into `build/bench/`
   (`ps3-bench-<UTC time>-<label>`).
2. Pick the runs of one configuration and run, from the repository root:

   ```
   python3 games/bench/matrix.py "Mesa ps3.290 + ps3rsx 0.249" --obs "what the row shows" \
       build/bench/ps3-bench-20261018T*-armagetronad-*
   ```

   The row is appended after the existing ones, so add rows oldest first. The runs' `versions`
   file names the game package, Mesa, libspeq, ps3-gpu-tools and the kernel (and ps3rsx, fluidsynth,
   OpenAL and SDL3 for runs made after d7e894d); for older runs take ps3rsx from the git log.
3. Options: `--replace` drops the row's old cells first (a row is otherwise extended when you run
   the same name again, and its run count adds up); `--hud GAME[,GAME]` takes the HUD median as the
   fps cell for those games.
4. Look at the result (`column -s, -t < games/bench/results/fps.csv` is wide; open it in a
   spreadsheet), fix `observations`, and commit `games/bench/results/` only.
