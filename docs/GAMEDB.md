# Walnut Games: the games database and launcher

A list of the games that run on Walnut, each played and measured on the PS3, compared with the same
game on a PC, and rated. It is two things over one catalog:

- **the website**, https://walnut.nanobyte.dev/gamedb/: a card for every game with its medal,
  filters (category, medal, tags) and search; each game's page has the
  screenshots (PS3 next to PC), the test results, and the `sudo apt install` line;
- **the desktop app** on Walnut (`walnut-games`): the same cards, with Install, Remove and Play.

Status (2026-10-23): designed with the user; the catalog, the scoring code and the site build exist
(`gamedb/`, `tools/gamedb/`) with three games (OpenArena, SuperTuxKart, Warzone 2100); the session
recorder exists, and the app (P4) is written (`walnut-games/`, below). The steps are in [PLAN.md](PLAN.md) ("Walnut Games"); this page
describes how it works.

## How a game is tested

Two tests, for different questions:

1. **A played session, about 10 minutes, the graded test.** Someone plays the game normally,
   started from the desktop menu like any user would: game mode, the settings Walnut ships, vsync,
   the 1080p TV. Players hit menus, saves, level changes and odd inputs that a scripted run never
   reaches. A session recorder records the frame times, every sound dropout (how long the output
   went dry, how loud it was around it), memory, the kernel log, how the game ended, and a
   screenshot about once a minute. When the game exits, a short form asks
   whether sound and music were heard, how the controls worked, and has a free-text box for notes
   on the gameplay; the notes are read with the numbers and become the game's known issues.
   A game with several launchers (OpenArena has "GL 1.2" and "GL 2.0") is played once per launcher:
   a session starts the chosen menu entry's command, records its Name, and each launcher is rated
   on its own. A session without a launcher counts for the game's default one.
2. **A scripted capture run, for the picture.** A recipe drives the game to fixed points (a menu, a
   map, a camera position), the PS3 captures them, and the same recipe captures them on a PC. The
   two sit side by side on one sheet and someone looks at it: missing textures, wrong colours,
   undrawn text. Not a pixel diff. The PC pictures are for that review only; the website and the
   app show the PS3's pictures alone (the point is how a game looks on the PS3).

Every session adds its play time to the game's totals, so the numbers sharpen as people play.

## How a game is rated

Each aspect gets a score from 0 to 100, on the same scale for every game; the medal comes from the
weighted mean of the scores.

| Aspect | Weight | Measured | 0 | 100 |
|---|---|---|---|---|
| Frame rate | 25 | median and 1% low over the session's play | median 20 fps, 1% low 10 | median 55, 1% low 45, or the game holds its own cap |
| Smoothness | 20 | stutter time per minute and the longest frame | 5 s a minute, or a freeze over 5 s | 0.1 s a minute, nothing over 250 ms |
| Sound | 15 | half: the right sounds playing (the player's form: effects and music heard); half: audible sound dropouts per minute of play (measured: the output went dry for 20 ms or more with sound on a side), or for runs without that measurement the sound faults per minute (ALSA underruns, OpenAL's xruns and waits); a wedged sound device | nothing heard, or wedged | effects and music heard, no dropouts |
| Picture | 15 | the capture points that differ from the PC | wrong, can't be played | the same as the PC |
| Stability | 15 | crashes per hour of play over all sessions, and clean quits | crashes in most sessions | no crashes, quits clean |
| Display | 5 | on the TV: full screen, aspect, scaling, legibility | cropped or off-screen | full screen at the right aspect, cleanly scaled |
| Load | 5 | launch to the first gameplay frame, not the first launch | 120 s | 15 s |

- **Resolution.** Games are tested on a 1080p TV as Walnut ships them. The game's own resolution
  isn't graded: a pixel-art game drawing 320x240 and scaled up to the TV is fine, and black bars
  around the right aspect cost nothing. Only stretching, a small window, unreadable text or a
  cropped picture lower Display.
- **Frame rate caps.** A game capped by design (vanilla Doom's 35 Hz, a game locked at 30) gets the
  full score for holding its cap.
- **Stutter time.** A stutter is a frame longer than 50 ms and three times the median frame; the
  time it lost is its length minus the median frame. Summed per minute of play.
- **Unmeasured aspects.** When an aspect can't be measured for a game (no frame times for a game
  that doesn't draw with OpenGL, no marker for when play starts), its weight goes to the others and
  the page says so.

Between the 0 and 100 points each score rises in steps: for smoothness, 0.5 s of stutter a minute
scores 75 and 2 s scores 55; sound is two halves: 50 for the right sounds (effects and music both heard, or the game has no
music; 25 with one missing or the effects only partly; nothing heard makes the whole score 0, and
a benchmark run with no form counts the sound as present), and 50 for cleanliness.

Cleanliness comes from the audible dropouts a minute: 45 at one, 35 at four, 22 at ten, 10 at
twenty, 0 at forty. A count of underruns says nothing about what was heard: in an X-Moto session the
player heard clean sound and music while 92 underruns were counted in six minutes. So the recorder
measures each one (`ps3-xrunlog`, an ALSA plugin in front of the game's sound device for the run,
[games/SUITE.md](games/SUITE.md#sound-dropouts)): how long the output stayed dry, and how loud the
20 ms before and after were. A dropout is audible when the gap is 20 ms or more and either side is
at -50 dBFS or louder; a gap of a few ms isn't heard, and neither is one in silence. OpenAL's
"Wait timeout" lines (a second without sound each) count as audible too. Every counted dropout is a
click or a chop someone hears, so this curve falls much faster than the old one for raw counts.
Runs from before the measurement (and any where it couldn't run) keep the raw counts, on a gentler
curve because most of those faults were never heard: 40 at ten a minute, 32 at thirty, 22 at sixty,
0 at 240.

## The medals

| Medal | Meaning | Weighted mean |
|---|---|---|
| **Platinum** | plays like on a PC | 90 or more |
| **Gold** | plays well | 75 or more |
| **Silver** | good, with compromises | 55 or more |
| **Bronze** | works, with clear problems | 35 or more |
| **Tin** | playable, but rough | 25 to 35 |
| **Rust** | barely playable | 15 to 25 |
| **Scrap** | doesn't work: won't start, doesn't reach play, crashes in most sessions, or too slow (a median under 15 fps, or over 10 s of stutter a minute) | under 15, or any of those |
| **Untested** | nobody has played it on Walnut yet | |

- **Leeway, with a weakest link.** The mean gives leeway: a game perfect everywhere at 50 fps loses
  a few points, not a medal. But the medal is at most one level above the game's weakest aspect,
  counted on the Platinum-to-Bronze scale (an aspect under 35 counts as below Bronze). One near-miss
  doesn't cost a medal; one real failure isn't averaged away. A game that plays perfectly but is
  silent is Bronze, with a "no sound" badge.
- **Scrap shows its reason**: won't start, crashes, or too slow.
- **Sessions measured alike are merged** (medians), those of the newest version measured the way its
  newest session was: a session from before the sound dropout logger isn't merged with one after it
  (catalog.py `method`); it stays on the page as a test run.
- **The first 30 s of play are a warm-up** (the first level starting, caches filling): frame times,
  stutter and sound dropouts are measured from 30 s after the load (import-run.py `WARMUP_S`).
- **The recorder's own dropouts don't count.** Taking a screenshot reads 8 MB of video memory and
  starves the sound for a moment; the audible dropouts from half a second before a capture to two
  seconds after it are left out (result.toml keeps their number as `sound_dropouts_at_shots`).
- **Versions.** Every test records the Walnut release it ran on (os-release's `VERSION_ID`; runs from
  before 0.1.0 count as "pre-0.1") and the game's package version (filed without our `+ps3.N` build
  number, which changes with every rebuild). The rating comes from the newest
  version (Walnut release and game version) that has a session or a benchmark run; capture runs of any
  version add their picture. The app and the page say which version that was ("Tested on Walnut 0.1
  with xmoto 0.6.3+repack-1"). The website's History table rates every version from its own
  tests alone, newest first, so a game can be compared between versions.
- **Launchers.** A game with several menu entries (OpenArena's GL 1.2 and GL 2.0 renderers) is rated
  once per launcher: each test records the entry it started (result.toml's `launcher`, the entry's
  Name; `import-run.py --launcher NAME` for runs from before it was recorded). The game's medal is its
  best launcher's, named with it ("Gold, with OpenArena (GL 1.2)"); the page and the app show every
  launcher's rating, and the app's Play menu lists the best first. A test without a launcher belongs to
  the game's only named one; with several, to "the default launcher". If a launcher other than the
  default rates best, the default is the thing to fix.
- **Stability needs hours.** The page shows the play time and crashes behind it ("0 crashes in
  1.7 h"); under an hour the stability score is marked provisional, and a full score needs three
  crash-free hours (0 crashes in 3 h bounds the rate under one an hour, the rule of three). The
  score of a crash-free game starts at 90 (Platinum level, so a short test never caps the medal) and
  rises to 100 over its first three hours; with crashes it is read
  off the rate: 1 in 3 hours scores 75, 1 an hour 55, 4 an hour 35, 12 an hour 0.
- **Badges, outside the medal:** controller support (full, partial, keyboard), needs your own game
  data, no sound, low memory (under 10 MB free at some point in play).
- **Every page shows the medal, the overall score ("Gold · 82"), each aspect's score and the
  numbers behind it.**

## Calibration: the 13 benchmarked games

Before any game was rated, the scale was tried on the 13 games of the benchmark suite with their
known numbers ([games/README.md](games/README.md)). Those runs had the desktop stopped and vsync off,
so they calibrate the formula; real medals come from played sessions. Stutter was approximated from
the stall time (frames over 100 ms), load is unmeasured there, stability assumed clean. Those runs
predate the dropout measurement, so their sound is scored from the raw fault counts.

| Game | Frame rate | Smoothness | Sound | Mean | Weakest | Medal |
|---|---|---|---|---|---|---|
| X-Moto | 100 | 100 | 100 | 100 | 100 | Platinum |
| OpenArena, opengl1 | 85 | 100 | 100 | 96 | 85 | Platinum |
| Yamagi Quake II | 80 (1% low 31) | 100 | 100 | 95 | 80 | Platinum |
| OpenArena, opengl2 | 72 (1% low 26) | 100 | 100 | 93 | 72 | Gold (capped) |
| QuakeSpasm | 100 | 65 (2.5 s of stalls) | 100 | 93 | 65 | Gold (capped) |
| Armagetron | 70 (1% low 24) | 100 | 100 | 92 | 70 | Gold (capped) |
| ClassiCube | 68 (47 fps) | 100 | 100 | 92 | 68 | Gold (capped) |
| Extreme Tux Racer | 75 | 62 | 100 | 85 | 62 | Gold |
| Luanti | 50 (1% low 19) | 58 | 100 | 78 | 50 | Silver (capped) |
| dsda-doom | 93 | 100 | 50 (465 underruns a minute) | 90 | 50 | Silver (capped) |
| Sauerbraten | 50 | 100 | 79 | 83 | 50 | Silver (capped) |
| Neverball | 95 | 30 (a 3.9 s reload) | 75 | 80 | 30 | Bronze (capped) |
| OpenTTD, 8bpp | 31 | 94 | 50 (choppy music) | 73 | 31 | Bronze (capped) |
| SuperTuxKart `lean` | 4 (21 fps) | 69 | 97 | 68 | 4 | Bronze (capped) |
| OpenTTD, 32bpp | 0 (11 fps) | 0 (52 s of stalls) | 50 | 45 | 0 | Scrap (too slow) |

Every medal below Gold has a real problem behind it: slow frame rates (Luanti, Sauerbraten,
SuperTuxKart), Neverball's 3.9 s level reload, dsda-doom's chopping sound, and the 32bpp OpenTTD that
can't be played is Scrap. Sound is half presence, half cleanliness, so bad sound costs a medal step
but no longer sinks a game on its own. Open: whether strategy and turn-based games need their own
frame-rate scale (OpenTTD plays fine at 38 fps and scores 31 on frame rate); one scale for now, revisited after the first strategy sessions.

## The desktop app

`walnut-games` (package `walnut-games`, Tk, mouse and keyboard) shows the catalog as cards: a
thumbnail, the medal with its score, the name and categories, an "Installed" mark. The top bar
has search, category, medal and sort, and "installed only". A click opens the
details: medal and overall score, the aspect scores as bars, hours played and crashes, one screenshot at a
time (PS3 first, then the PC's, with prev/next), known issues, the measurements, the patch list, and the
buttons Install, Remove, Play and "Open on the web". The window follows the Walnut palette
(`walnut_games.py` has the colours).

- **The catalog is the packaged one**, `/usr/share/walnut-games/` (made by `make gamedb-catalog`:
  `catalog.json`, thumbnails and the screenshots the catalog lists, from the Git LFS files; a checkout
  without the LFS objects fails the pack, and CI checks out with `lfs: true` for this package). New games
  arrive with package updates; the website is the always-current view. A screenshot missing from the
  package is fetched once from the website and cached in `~/.cache/walnut-games`; offline the app works
  with what is packaged.
- **Installed** comes from `dpkg-query`. **Play** finds the game's launchers in its packages'
  manifest (`dpkg -L`): the menu entries they ship (a diverted one isn't there and drops out), and any
  shown menu entry that runs one of their programs (Walnut's own entries, like OpenArena's GL 1.2 and
  GL 2.0). The catalog's `desktop` list is optional: it orders the launchers or adds one the manifest
  can't lead to. Each launcher starts through its runtime menu copy
  (`$XDG_RUNTIME_DIR/ps3-game-menu/applications/`, Exec through `ps3-game`), else `ps3-game` plus the
  entry's Exec (field codes cut out, no shell). With more than one launcher, Play opens a menu of them
  by their names. Its output goes to `~/.cache/walnut-games/<id>.log`; a game that quits within 20 s
  says so in the status line. Play is
  off while an install or removal runs: game mode would freeze the helper with dpkg's lock held. After
  either, the app runs `ps3-game-menu "$XDG_RUNTIME_DIR/ps3-game-menu"` so the new entry exists.
- **The helper**, `/usr/lib/ps3/sbin/walnut-games-helper install|remove|status ID`, runs as root through
  `sudo -n` (`/etc/sudoers.d/walnut-games`: group sudo, no password, that one path). Its guarantees:
  the argument is a game id (`^[a-z0-9][a-z0-9-]{0,63}$`) found in the root-owned packaged catalog, which is
  the only source of package names (each checked against the Debian name rule; licensed or nonfree data
  packages refused); `python3 -I`, fixed PATH and environment, no shell; install runs `apt-get install
  --no-remove` (after `apt-get update` when the lists are over a day old) with the configuration files
  kept, noninteractive; remove simulates first and refuses, before touching anything, if the simulation
  removes anything but the game's own packages and the `ps3-games` / `ps3-games-free` metapackages, and in
  the metapackage case first marks the other installed packages it depends on as manually installed;
  it never runs autoremove; SIGHUP and SIGPIPE are ignored, so closing the app never stops dpkg halfway.
  Output is `progress N text`, `log`, `error` lines; exit 0 done, 2 refused, 1 apt failed.
- Tests: `walnut-games/tests/test_walnut_games.py` (make test ONLY=walnut-games): the helper's validation,
  catalog mapping, cascade refusal (with a fake `apt-get -s`), install arguments and status relay (a fake
  apt-get writes to the status descriptor), Exec parsing, filters, and a window smoke test on a private
  Xvfb/Xvnc (skipped when neither exists).

## Adding a game

A game is one folder, `gamedb/content/games/<id>/`: `index.md` with the catalog entry in its
front matter (title, summary, categories, tags, the packages to install, optionally the .desktop entries,
`fps_cap` if it has one, whether it needs the user's own data, the controller) and notes as its
body, a `thumb.png`, and one folder per test (`tests/<date>-<kind>/`) with its generated
`result.toml` and the screenshots it lists (`ps3-<point>.jpg`, 800x450 JPEG, kept in Git LFS). A
session takes a screenshot every minute; the importer publishes up to four that differ (black
frames and near-duplicates, like a menu caught three times, dropped by comparing small greyscale
versions; then the most varied), and `--shots 60,240` picks them by hand instead. `tools/gamedb/import-run.py RUN... --game ID` writes a test folder from harness run
directories and never touches `index.md`; `tools/gamedb/check.py` validates the catalog;
`tools/gamedb/build.sh OUT` builds the site. A test is a `session` (a played session) or a `capture` (the
picture only). Ratings come from played sessions alone: the benchmark runs of the suite (the
desktop stopped, vsync off) don't describe what a player gets and aren't imported; the code still
reads a `demo` test (a harness run with a measured window) if one is ever imported on purpose. Games that need the user's own data (OpenRCT2, Quake)
are listed and shown, with a guide to setting up the data; the data itself is never offered (their
screenshots, thumbnail included, are published like any game's).
