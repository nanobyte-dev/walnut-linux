# Walnut Games: the games database and launcher

A list of the games that run on Walnut, each played and measured on the PS3, compared with the same
game on a PC, and rated. It is two things over one catalog:

- **the website**, https://walnut.nanobyte.dev/gamedb/: a card for every game with its medal,
  filters (category, medal, tags) and search; each game's page has the
  screenshots (PS3 next to PC), the test results, and the `sudo apt install` line;
- **the desktop app** on Walnut (`walnut-games`): the same cards, with Install, Remove and Play.

Status (2026-10-26): the catalog, the scoring, the site, the session recorder and the app (P4) exist,
14 games are listed. Ratings are made under rules 2.0 from now on; the older ones keep rules 1.0
([Rules versions](#rules-versions)). The steps are in [PLAN.md](PLAN.md) ("Walnut Games").

**What a rating says.** How well a game plays on the PS3 under Walnut, not how good the game is.
Game design never scores: unforgiving gameplay, short levels, the game's own frame cap or simulation
rate (OpenTTD's 37 a second), a control layout made for a PC keyboard, how fun it is. Breakage a
Walnut player runs into does score, wherever it comes from, and says where: every picture defect and
broken feature is labelled "also on the PC" (upstream's, seen on the PC reference) or "PS3/Walnut
only", so a page tells our problems from upstream's.

**One player, one session.** The ratings are made by one person reviewing dozens of games, about
one ten-minute session each. What that player can't reasonably test (several pads, online play, a
mode nobody reached) is "not tested" and never costs anything: its weight goes to what was tested,
like an unmeasured aspect. What can be measured is measured; scripted runs without the player cover
some of the rest (planned, [games/SUITE.md](games/SUITE.md#scripted-runs-planned)); the form asks only
what the player saw anyway.

## How a game is tested

1. **A played session, about 10 minutes, the graded test.** Someone plays the game normally,
   started from the desktop menu like any user would: game mode, the settings Walnut ships, vsync,
   the 1080p TV. Players hit menus, saves, level changes and odd inputs that a scripted run never
   reaches. A session recorder records the frame times, every sound dropout (how long the output
   went dry, how loud it was around it), memory, the kernel log, how the game ended and how fast the
   desktop came back, a screenshot about once a minute with the game's window and the mouse pointer,
   the input devices (which ones the game opened, pad and keyboard use, counts only), and the
   game's settings files before and after (hashes only, never touched). When the game exits a short
   form, under a minute, asks what only the player knows (sound and music heard, glitches seen,
   what they played with, whether the menus worked with the pad, menus and modes, saves) and has a
   notes box; the notes are read with the numbers and become the game's known issues. A game with
   several launchers (OpenArena has "GL 1.2" and "GL 2.0") is played once per launcher: a session
   starts the chosen menu entry's command, records its Name, and each launcher is rated on its own.
   A session without a launcher counts for the game's default one.
2. **The review.** The reviewer (an agent) puts the session's screenshots next to the PC's
   (`tools/play-pc.sh` plays the same game on the PC alongside, a scripted capture run gives fixed
   points) and writes REVIEW.md: the verdict, each picture defect with its category and whether the
   PC shows it too, the smallest text's height, and any broken feature from the notes with its origin
   ([games/SUITE.md](games/SUITE.md#played-sessions)). Not a pixel diff. The PC pictures are for that
   review only; the website and the app show the PS3's pictures alone.
3. **A scripted capture run, for the picture** (optional). A recipe drives the game to fixed points
   (a menu, a map, a camera position), the PS3 captures them, and the same recipe captures them on a PC.

Every session adds its play time to the game's totals, so the numbers sharpen as people play;
scripted runs and unattended soaks (planned) add crash-free hours with nobody playing.

## How a game is rated

Each aspect gets a score from 0 to 100, on the same scale for every game; the medal comes from the
weighted mean of the scores. The current rules, 2.0 (2026-10-26):

| Aspect | Weight | Measured | 0 | 100 |
|---|---|---|---|---|
| Frame rate | 22 | median and 1% low over the session's play | median 20 fps, 1% low 10 | median 55, 1% low 45, or the game holds its own cap |
| Smoothness | 18 | stutter time per minute and the longest frame | 5 s a minute, or a freeze over 5 s | 0.1 s a minute, nothing over 250 ms |
| Sound | 12 | half: the right sounds playing (the form); half: audible sound dropouts per minute of play; a wedged sound device | nothing heard, or wedged | effects and music heard, no dropouts |
| Picture | 12 | fixed deductions per defect category (below) | garbage, or several kinds of fault | the same as the PC |
| Stability | 12 | crashes and unclean quits per hour of play over all tests | crashes in most sessions | no crashes, quits clean |
| Controls | 10 | a checklist (below): the share of the tested items that pass | nothing works with the pad | everything tested works |
| Display | 6 | the window against the screen at each screenshot, then deductions (below) | cropped or off-screen | fills the screen, right shape, as shipped |
| Features | 5 | a checklist (below) | everything tested is broken | everything tested works |
| Load | 3 | launch to the game's first frame (hud_t1; the player's time in its menus never counts) | 120 s | 15 s |

**Picture**, from 100, each category deducted once per launcher however often it shows, floor 0.
The reviewer's REVIEW.md lines `defect: <category> <pc|ps3>: <what>`:

| Category | Deduction |
|---|---|
| `garbage`: garbage textures or geometry | 60 |
| `missing`: a missing element (the HUD, models, shadows, an effect) | 25 |
| `colours`: wrong colours or blending | 25 |
| `flicker`: flicker, z-fighting, one-frame glitches | 15 |
| `lighting`: lighting or shading differs | 10 |
| `softer`: softer or less detailed from Walnut's own defaults (DXT, lean settings); the what says why | 5 |

The player's "Visual glitches" answer adds 25 for "broken" unless a reviewed defect already takes 25
or more, and 10 for "minor" unless one takes 10 or more; "didn't notice any" adds nothing. A launcher
with neither a review nor an answer has Picture unmeasured.

**Display.** The window geometry's scale first (a window covering the screen 100, a smaller one 30
from half the screen up, 10 below, cropped 0, the median sample of all the launcher's sessions), then:

| Deduction | | How it's known |
|---|---|---|
| stretched to the wrong shape | 30 | measured where the game reports its render size (`render_res`): its aspect against the window's, unless black bars on the screenshots hold the right shape; else the reviewer's `stretched: yes` |
| a desktop mouse pointer over the game | 10 | measured: an X pointer visible over the window in half the screenshots or more (XFixes); the reviewer's `pointer: hidden` corrects a game that hides it in a way XFixes can't see |
| UI text under 18 px at 1080p | 15 | the reviewer's `text_px: N`, the smallest menu or HUD text read off a screenshot |
| not as shipped | 20 | measured: the window changed size or fullscreen state between screenshots, or a settings file the game had before got new video lines (`r_mode`, `fullscreen`, ...) |

Black bars around the right shape cost nothing; the game's own resolution is never scored.

**Controls and Features** are checklists. Each item is yes, no or not tested; the score is the share
of the tested items that pass (x 100), and a list with nothing tested leaves the aspect unmeasured.
When the pad doesn't work without setup, Controls is at most 30. The old controller badge (full,
partial, keyboard) comes from the Controls items.

| Item | Aspect | Where it comes from |
|---|---|---|
| the pad works without setup | Controls | measured: the player used the pad in play and the game opened it (inputlog.py); no when the pad was used and the game never opened it, or the player says they played with the keyboard instead |
| the menus work with the pad | Controls | asked (the player went through them anyway); scripted runs, planned |
| a pad turned on later is found | Controls | measured when a pad appeared mid-session (opened by the game or not); scripted runs, planned |
| each extra pad joins as the next player | Controls | scripted runs, planned; else not tested |
| the keyboard works too | Controls | the player played with it (asked) |
| nothing needs the keyboard | Controls | measured: more than 2 key presses during a session played with the pad means no |
| every menu and mode opened works | Features | asked |
| settings stick across restarts | Features | measured in part: the settings files the game wrote in its last session are read back at the next start (confwatch.py, access times) |
| saves load | Features | asked ("didn't try": not tested) |
| online play works | Features | the reviewer (`check: online yes|no`) or a scripted run; else not tested |

The reviewer's `check: <item> yes|no|na` and `broken: <item> <pc|ps3>: <what>` lines win over the
measured and asked answers, and label a broken item with where it comes from. Over several sessions
the newest answer of each item counts.

**Stability.** Crashes and unclean quits per hour of play over all the launcher's tests (scripted runs
and soaks count their hours too). A quit is clean when the game exits with code 0 and the desktop is
back within 10 s of its end (session.sh's `desktop_back_s`); the player killing it (Ctrl+Alt+Esc), an
error exit, or a desktop that doesn't come back count like a crash in the rate. Only real crashes
(a signal, a segfault, out of memory) make the "crashes in most sessions" Scrap floor. The score of a
crash-free game starts at 90 and rises to 100 over its first three hours; with failures it is read
off the rate: 1 in 3 hours scores 75, 1 an hour 55, 4 an hour 35, 12 an hour 0. Under an hour of
play it is marked provisional.

- **Resolution.** Games are tested on a 1080p TV as Walnut ships them. The game's own resolution
  isn't graded: a pixel-art game drawing 320x240 and scaled up to the TV is fine.
- **Frame rate caps.** A game capped by design (vanilla Doom's 35 Hz, a game locked at 30, a
  simulation rate) gets the full score for holding its cap (`fps_cap` in its front matter).
- **Stutter time.** A stutter is a frame longer than 50 ms and three times the median frame; the
  time it lost is its length minus the median frame. Summed per minute of play.
- **Unmeasured and untested.** When an aspect can't be measured or nothing in it was tested, its
  weight goes to the others and the page says so.
- **What it rests on.** The page and the app say what a rating is made from ("1 session, 9 min of
  play") and under which rules.

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

## Where each criterion comes from

| Criterion | How |
|---|---|
| frame rate, smoothness, load | measured (the Gallium HUD's frame times) |
| sound dropouts, a wedged device | measured (ps3-xrunlog, the card's pointer) |
| sound and music heard | asked |
| picture defects, their origin | agent-reviewed (PS3 screenshots next to the PC) |
| visual glitches the screenshots missed | asked |
| crashes, unclean quits | measured (exit code, kernel log, the desktop's return) |
| window geometry, black bars, the mouse pointer | measured (geom.py at each screenshot, the screenshots) |
| stretched | measured where the render size is known, else agent-reviewed |
| text size | agent-reviewed |
| video settings changed | measured (window changes, the settings files' new lines) |
| pad without setup, a pad found later, nothing needs the keyboard | measured (inputlog.py); scripted later |
| menus with the pad | asked; scripted later |
| extra pads join as players | scripted (planned); not tested until then |
| keyboard works, menus and modes, saves | asked |
| settings stick | measured in part (confwatch.py) |
| online play | agent-reviewed or scripted; else not tested |
| hours of play | measured; scripted runs and soaks add more (planned) |

Asked, the whole form: sound, music, glitches, played with, the menus with the pad, menus and modes,
saves, and the notes.

## Rules versions

A rating says which rules made it ("rules 2.0 (2026-10-26)"), as `<data>.<formula>`:

- **The data version** says which data points a test has. A test keeps the data version it was
  measured under (result.toml `rules = "2.0"`; a test without the field is 1.0) until it is re-tested.
- **The formula revision** is the weights, thresholds and curves applied to that data. Tests store
  raw measurements and answers, never only scores; the catalog scores every test at build time with
  the current formula of its data version (score.py `FORMULA`).

Two kinds of change, so a later change is classified right:

1. **Weights or thresholds change, or an aspect is dropped**: it can be recalculated from stored data.
   Bump the formula revision (`FORMULA[2] = 1`: rules 2.1) and every test of that data version is
   re-scored on the next build, with no re-import.
2. **A criterion is added that needs new data points**: old tests can't have it. A new data version
   (3.0): new imports record it, old tests keep the rules they were measured under until re-tested.

A launcher is rated under the newest data version among its graded tests (sessions, benchmark runs),
from those tests alone: versions never merge. History has a row per Walnut and game version and
data version, and the test list names each test's rules. The website's about page explains the
current rules and links each rating's rules to its section.

**Rules 2.0 (2026-10-26)**, current: the tables above. A session is data version 2 when its form is the
second one (session.toml `form_version = 2`, `import-run.py --rules 2.0` for a run reviewed the new way).
Its data points beyond 1.0: the raw window samples with the pointer, black bars, the desktop's return
after the quit, the input log, the settings files, the form's answers and the reviewer's items.

**Rules 1.0 (2026-10-24)**, the ratings made before (kept as they are: their tests lack the new data):

| Aspect | Weight | Measured |
|---|---|---|
| Frame rate | 25 | as in 2.0 |
| Smoothness | 20 | as in 2.0 |
| Sound | 15 | as in 2.0 |
| Picture | 15 | the share of the capture points that differ from the PC (`verdict: ok` or `wrong` for all of them) |
| Stability | 15 | crashes per hour of play (the player killing it or a slow return don't count) |
| Display | 5 | the window geometry alone, no deductions (stored per test as `display_score`) |
| Load | 5 | as in 2.0 |

No Controls or Features (the controller badge is the catalog entry's `controller`), the same medals,
weakest link, Scrap floors and curves.

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
  (catalog.py `method`), nor one of another rules data version; it stays on the page as a test run.
- **The first 30 s of play are a warm-up** (the first level starting, caches filling): frame times,
  stutter and sound dropouts are measured from 30 s after the load (import-run.py `WARMUP_S`).
- **Play starts at the match.** In a played session the frames, stutter and dropouts are measured
  from the recipe's play mark (the line a game logs when the match or level starts: PLAY_MARK) plus
  the warm-up, so menus, setup screens and the player's time there never count as play.
- **No medal without frame rate and smoothness.** They are the core of a rating; a session where
  the frame timer saw nothing (a software renderer doesn't go through OpenGL, which the timer hooks)
  gets its numbers but no medal ("played, but not rated: frame rate and smoothness not measured"),
  unless it crashed, which makes it Scrap either way (score.py `REQUIRED`).
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
  1.7 h"); a full score needs three crash-free hours (0 crashes in 3 h bounds the rate under one an
  hour, the rule of three), and the 90 a crash-free game starts at is Platinum level, so a short test
  never caps the medal. Confidence grows with hours, and they needn't all be played: scripted runs and
  unattended soaks add crash-free hours with nobody at the PS3 (planned).
- **Badges, outside the medal:** controller support (full, partial, keyboard: from the Controls
  checks under rules 2.0, else the catalog entry's `controller`), needs your own game data, no sound,
  low memory (under 10 MB free at some point in play).
- **Every page shows the medal, the overall score ("Gold · 82"), each aspect's score and the
  numbers behind it.**

## Calibration: the 13 benchmarked games

Before any game was rated, the scale (rules 1.0) was tried on the 13 games of the benchmark suite with their
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
| BananaBread | 50 | 100 | 79 | 83 | 50 | Silver (capped) |
| Neverball | 95 | 30 (a 3.9 s reload) | 75 | 80 | 30 | Bronze (capped) |
| OpenTTD, 8bpp | 31 | 94 | 50 (choppy music) | 73 | 31 | Bronze (capped) |
| SuperTuxKart `lean` | 4 (21 fps) | 69 | 97 | 68 | 4 | Bronze (capped) |
| OpenTTD, 32bpp | 0 (11 fps) | 0 (52 s of stalls) | 50 | 45 | 0 | Scrap (too slow) |

Every medal below Gold has a real problem behind it: slow frame rates (Luanti, BananaBread,
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
  without the LFS objects fails the pack, and CI checks out with `lfs: true` for this package). It is
  what the root helper installs from and what the app shows offline.
- **Fresh ratings from the website.** On start the app shows the packaged catalog, or the cached web
  copy (`~/.cache/walnut-games/catalog.json`) when that is newer, and fetches
  `https://walnut.nanobyte.dev/gamedb/catalog.json` in a thread (8 s timeout). A download must validate
  (the games list and the fields the app reads, safe ids and picture paths) and be newer; then it is
  cached and the cards and details are redrawn in place, keeping filters, search, selection and
  scroll. Newer means a later `content_date` (the newest test date in the catalog), and on the same date a
  different `revision` (a hash of the games without the .deb links; both are written by `catalog.py`
  from the content, not the clock; `generated` is the build's clock and is not compared). Offline or
  broken: a line on stderr and nothing else. The **Refresh** button repeats the fetch and re-reads
  `dpkg-query` and the launchers; it is off while a refresh or an install runs.
  `WALNUT_GAMES_OFFLINE=1` disables the network.
- **Web-only games.** Thumbnails and screenshots missing from the package are fetched once from the
  website (`games/<id>/thumb.png`, the tests' picture paths) and cached in `~/.cache/walnut-games`; a
  missing thumbnail shows the placeholder. Install stays on the packaged catalog (the helper maps ids
  to packages from it): a game only on the website shows Install off with "Install it after the next
  Walnut Games update". Play and Remove work for installed games.
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
