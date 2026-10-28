# Walnut Games: the games database and launcher

A list of the games that run on Walnut, each played and measured on the PS3, compared with the same
game on a PC, and rated. **Every rating comes from someone playing the game on a PS3; nothing scripted counts.** Benchmark, demo and
scripted runs (the harness for measuring game and GPU-driver optimizations) and unattended soaks never feed a
rating: no crash-free hours, no checklist answers, no frame-rate numbers. It is two things over one catalog:

- **the website**, https://walnut.nanobyte.dev/gamedb/: a card for every game with its medal,
  filters (medal, a tag panel) and search, and a page per tag; each game has four tabs, Overview (the rating, the
  screenshots, the installation), Test runs, Technical and Guides ([The website](#the-website));
- **the desktop app** on Walnut (`walnut-games`): the same cards, with Install, Remove and Play.

Status (2026-10-26): the catalog, the scoring, the site, the session recorder and the app (P4) exist,
14 games are listed. Ratings are made under rules 2.0 from now on; the older ones keep rules 1.0
([Rules versions](#rules-versions)). The steps are in [PLAN.md](PLAN.md) ("Walnut Games").

**What a rating says.** How well a game plays on the PS3 under Walnut, not how good the game is.
Game design never scores: unforgiving gameplay, short levels, the game's own frame cap or simulation
rate ([OpenTTD](../gamedb/games/openttd/)'s 37 a second), a control layout made for a PC keyboard, how fun it is. A game designed
for keyboard and mouse (an RTS, a PC shooter, a city builder) is never marked down for weak gamepad
support. Breakage a Walnut player runs into does score, wherever it comes from, and says where: every
picture defect and broken feature is labelled "also on the PC" (upstream's, seen on the PC reference)
or "PS3/Walnut only", so a page tells our problems from upstream's.

**One player, one session, the game as shipped.** The ratings are made by one person reviewing dozens
of games, about one ten-minute session each, on the game's shipped defaults (the harness sets the
player's own settings aside for the session) without changing any setting. What that player can't
reasonably test (several pads, a pad turned on mid-game, online play, a mode nobody reached) is "not
tested" and never costs anything: its weight goes to what was tested, like an unmeasured aspect. What
can be measured is measured; the reviewing agent answers a fixed questionnaire over the screenshots;
the form asks only what the player saw. Scripted runs ([games/SUITE.md](games/SUITE.md#scripted-runs)) test driver
and game optimizations and never answer any of it.

## How a game is tested

1. **A played session, about 10 minutes, the graded test.** Someone plays the game normally,
   started from the desktop menu like any user would: game mode, the settings Walnut ships, vsync,
   the 1080p TV. The harness first renames the game's settings and data directories aside, so the
   game starts as a new player's would, and renames the player's own back when it quits (lossless:
   a rename on the same filesystem, never a delete; a session cut short is put back at the next
   session's start; `freshhome.py`). The player changes no settings; a game that can't be played
   without changing one is reported in the notes. A session recorder records the frame times, every
   sound dropout (how long the output went dry, how loud it was around it), memory, the kernel log,
   how the game ended and how fast the desktop came back, a screenshot about once a minute with the
   game's window and the mouse pointer, and the input devices (which ones the game opened, pad and
   keyboard use, when each press came; counts and times only, never which key). When the game exits,
   a short form, under a minute, asks what only the player knows: sound and music heard, glitches
   seen, the gamepad in the menus and in play, the keyboard and mouse, menus and modes, saves; and a
   notes box. A game with several launchers ([OpenArena](../gamedb/games/openarena/) has "GL 1.2" and "GL 2.0") is played once per
   launcher: a session starts the chosen menu entry's command, records its Name, and each launcher is
   rated on its own. A session without a launcher counts for the game's default one.
2. **The review**, by the agent that imports the run. `tools/gamedb/review.py RUN` puts the session's
   screenshots next to the PC reference where one exists, adds 1:1 crops with a pixel ruler, and
   writes REVIEW.md: a fixed questionnaire (stretched or squashed; the smallest text's height; the
   mouse pointer, measured, confirmed or overridden; each defect category, with pc or ps3 and where;
   anything broken, with its origin). The agent answers every question by looking at the pictures,
   never by pixel metrics; import-run.py refuses a questionnaire left unanswered
   ([games/SUITE.md](games/SUITE.md#played-sessions)). The PC pictures are for that review only; the
   website and the app show the PS3's pictures alone.
3. **A scripted capture run, as a PC reference** (optional). A recipe drives the game to fixed points
   (a menu, a map, a camera position) and the same recipe captures them on a PC; the reviewer may look at them next
   to the session's screenshots, but the capture run itself rates nothing.

Every session adds its play time to the game's totals, so the numbers sharpen as people play;
only sessions count: scripted runs and unattended soaks add no hours.

## How a game is rated

Each aspect gets a score from 0 to 100, on the same scale for every game; the medal comes from the
weighted mean of the scores. The current rules, 2.2 (2026-10-26):

| Aspect | Weight | Measured | 0 | 100 |
|---|---|---|---|---|
| Frame rate | 22 | median and 1% low over the session's play | median 20 fps, 1% low 10 | median 55, 1% low 45, or the game holds its own cap |
| Smoothness | 18 | stutter time per minute and the longest frame | 5 s a minute, or a freeze over 5 s | 0.1 s a minute, nothing over 250 ms |
| Sound | 12 | half: the right sounds playing (the form); half: audible sound dropouts per minute of play; a wedged sound device | nothing heard, or wedged | effects and music heard, no dropouts |
| Picture | 12 | fixed deductions per defect category (below) | several kinds of fault | the same as the PC |
| Stability | 12 | crashes and unclean quits per hour of play over all tests | crashes in most sessions | no crashes, quits clean |
| Controls | 10 | a checklist (below): the share of the tested items that pass | nothing tested works | everything tested works |
| Display | 6 | the window against the screen at each screenshot, as shipped, then deductions (below) | cropped or off-screen | fills the screen, right shape, readable |
| Features | 5 | a checklist (below) | everything tested is broken | everything tested works |
| Load | 3 | the initial load plus the level load (below) | 120 s | 15 s |

**The medal is the weighted mean's**, with no weakest link: a weak aspect costs points, not a medal
level. The one exception: **a broken core subsystem makes the game Rust**, whatever its mean, and
the page names it next to the mean ("Rust · 82: picture broken (garbage textures or geometry)"). We
can't call a game with corrupted textures or no sound good because it runs at 400 fps, but it is still
playable, so it isn't Scrap ([The medals](#the-medals)). Broken is an explicit flag, not a score
threshold (a garbage-texture deduction still leaves Picture at 40):

| Core aspect | Broken when |
|---|---|
| Picture | the review finds garbage textures or geometry, or the player answers the glitches "Broken" |
| Sound | the player heard no sound effects, or the sound device wedged (missing music alone isn't) |
| Frame rate | the median is under 20 fps (the score's 0 point), unless the game's own cap is lower and held |
| Smoothness | a freeze over 5 s in play, or 5 s of stutter a minute |
| Stability | under an hour of play between crashes (occasional crashes; consistent ones are Scrap) |

Controls, Features, Display and Load never make a game Rust or Scrap: they only weigh in the mean.
Scrap is for a game that can't be played at all (score.py `scrap_floor_v2`): it won't start, a
session crashes at startup or before play, no session reaches play, it crashes in more than half
of its sessions, half its sessions or more crash within the first 5 minutes of play
(`EARLY_CRASH_S`), or the median is under 5 fps (`UNPLAYABLE_FPS`).

**Picture**, from 100, each category deducted once per launcher however often it shows, floor 0.
The reviewer's answers (`<category>: no`, or `<category>: pc|ps3: where`):

| Category | Deduction |
|---|---|
| `garbage`: garbage textures or geometry (also: picture broken) | 60 |
| `missing`: a missing element (the HUD, models, shadows, an effect) | 25 |
| `colours`: wrong colours or blending | 25 |
| `flicker`: flicker, z-fighting, one-frame glitches | 15 |
| `lighting`: lighting or shading differs | 10 |
| `softer`: softer or less detailed from Walnut's own defaults (DXT, lean settings); the where says why | 5 |

The player's "Visual glitches" answer adds 25 for "broken" unless a reviewed defect already takes 25
or more, and 10 for "minor" unless one takes 10 or more; "didn't notice any" adds nothing. A launcher
with neither a review nor an answer has Picture unmeasured.

**Display.** The game as shipped: the window geometry's scale first (a window covering the screen
100, a smaller one 30 from half the screen up, 10 below, cropped 0, the median sample of all the
launcher's sessions), then the reviewer's deductions:

| Deduction | | How it's known |
|---|---|---|
| stretched or squashed (circles and characters out of proportion) | 30 | the reviewer, looking; black bars measured on the screenshots are printed as a hint, never scored |
| a desktop mouse pointer over the game | 10 | measured (an X pointer visible over the window in half the screenshots or more, XFixes), confirmed or overridden by the reviewer |
| UI text under 18 px at 1080p | 15 | the reviewer, from 1:1 crops with a ruler |

Black bars around the right shape cost nothing; the game's own resolution is never scored.

**Controls and Features** are checklists. Each item is yes, partly (half), no or not tested; the
score is the share of the tested items that pass (x 100), and a list with nothing tested leaves the
aspect unmeasured.

| Item | Aspect | Where it comes from |
|---|---|---|
| the gamepad works in play | Controls (pad games) | asked: "Gamepad in play: seamless / needed setup / didn't work / didn't try"; else measured: the player used the pad and the game opened it (inputlog.py) |
| the gamepad works without setup | Controls (pad games) | the same answer: seamless is yes, needed setup and didn't work are no; Controls is then at most 30 |
| the menus work with the gamepad | Controls (pad games) | asked: "worked / partly / didn't / didn't try" |
| a gamepad turned on later is found | Controls (pad games) | measured if it happens in a session (the input log sees a pad added and the game open it); else not tested; never asked (a DualShock 3 can't easily be turned off mid-game) |
| each extra gamepad joins as the next player | Controls (pad games) | not tested unless it happens in a session; never asked (one player, one pad) |
| nothing needs the keyboard | Controls (pad games) | measured: more than 2 key presses during a session played with the pad means no; not tested while the logger can't read the keyboard (the `input` group) |
| keyboard and mouse work | Controls (all games) | asked: "worked / something didn't / didn't use" |
| every menu and mode opened works | Features | asked |
| saves load | Features | asked ("didn't try": not tested) |
| online play works | Features | the reviewer (`check: online yes\|no`); else not tested |

**Games designed for keyboard and mouse.** The catalog entry's `designed_for = "keyboard-mouse"`
(default `"pad"`) makes Controls score the keyboard and mouse alone; the gamepad items only make the
controller badge, and the page shows a "made for keyboard and mouse" badge.

The reviewer's `check: <item> yes|partly|no|na` and `broken: <item> <pc|ps3>: <what>` answers win
over the measured and asked ones, and label a broken item with where it comes from. Over several
sessions the newest answer of each item counts. The controller badge (full, partial, keyboard) comes
from the gamepad items.

**Stability.** Crashes and unclean quits per hour of play over the launcher's played sessions (scripted
runs and soaks count for nothing). A quit is clean when the game exits with code 0 and the desktop is
back within 10 s of its end (session.sh's `desktop_back_s`); the player killing it (Ctrl+Alt+Esc), an
error exit, or a desktop that doesn't come back count like a crash in the rate (but never make the
game broken). The freeze leading into a crash (the run of frames over 1 s at the end of a crashed run) is
the crash: Stability counts it, Smoothness and the input checks stop where it began (keys pressed to get
out of a frozen game don't mean the game needs a keyboard). The score of a crash-free game starts at 90 and rises to 100 over its first three
hours; with failures it is read off the rate: 1 in 3 hours scores 75, 1 an hour 55, 4 an hour 35, 12
an hour 0. Under an hour of play it is marked provisional.

**Load** is the time a player waits, never the time they spend in menus. With fresh defaults every
session, a game may need menus, a profile or a new game set up before play; that time is the
player's and doesn't count. Load = the initial load (launch to the game's first frame, `hud_t1`) +
the level load (from the player's last input before the play mark, inputlog.py's press times, to the
play mark: from "start game" to play actually starting). Without a play mark or an input log, Load is
the initial load only and the page says so. Frame rate, smoothness and dropouts are measured from the
play mark plus the 30 s warm-up.

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
twenty, 0 at forty. A count of underruns says nothing about what was heard: in an [X-Moto](../gamedb/games/xmoto/) session the
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
| frame rate, smoothness, initial load | measured (the Gallium HUD's frame times) |
| level load | measured (inputlog.py's press times, the recipe's play mark) |
| sound dropouts, a wedged device | measured (ps3-xrunlog, the card's pointer) |
| sound and music heard | asked |
| picture defects and their origin, stretched, text size | agent-reviewed (review.py's questionnaire over the PS3 screenshots and the PC reference, by looking) |
| visual glitches the screenshots missed | asked |
| crashes, unclean quits | measured (exit code, kernel log, the desktop's return) |
| window geometry | measured (geom.py at each screenshot), on shipped defaults (freshhome.py) |
| the mouse pointer | measured (XFixes), confirmed by the reviewer |
| black bars | measured, a hint for the reviewer only |
| the gamepad in play, without setup | asked; measured when not answered (inputlog.py) |
| the gamepad in the menus | asked |
| a gamepad found later, extra gamepads join | measured if it happens in a session; else not tested |
| nothing needs the keyboard | measured (inputlog.py, with the `input` group); else not tested |
| keyboard and mouse, menus and modes, saves | asked |
| online play | agent-reviewed; else not tested |
| hours of play | measured, sessions only |

Asked, the whole form: sound, music, glitches, the gamepad in the menus, the gamepad in play, the
keyboard and mouse, menus and modes, saves, and the notes (including a setting that had to be changed
to play).

## Rules versions

A rating says which rules made it ("rules 2.2 (2026-10-26)"), as `<data>.<formula>`:

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

**Rules 2.2 (2026-10-26)**, current: the tables above. A formula revision (`FORMULA[2] = 2`): "crashes in
more than half the sessions" needs at least two sessions, so a single late crash in a game's only session
is Rust until a second session shows whether it repeats (the user, 2026-10-26); an early crash (within
the first 5 minutes of play) or one at startup is still Scrap with one session. No medal moved.

**Rules 2.1 (2026-10-26)**: A formula revision of data version 2 (`FORMULA[2] = 1`):
the medals were redefined (the user, 2026-10-26), with the weights and curves unchanged. Platinum,
Gold, Silver and Bronze come from the mean as before; Tin is gone; Rust is a playable game with a
core subsystem broken (or a mean under 35); Scrap only a game that can't be played (consistent
crashes, under 5 fps). Every data-version-2 test was re-scored; no medal moved ([Mr.Boom](../gamedb/games/mrboom/) stays Scrap:
it crashed within 30 s of play in both its sessions).

**Rules 2.0 (2026-10-26)**: the same data and scores; a broken core subsystem made the game Scrap,
the scale was rules 1's (Tin included), and the Scrap floors were rules 1's. A session is data version 2 when its form is the
second one (session.toml `form_version = 2`, `import-run.py --rules 2.0` for a run reviewed the new way).
Its data points beyond 1.0: the game on shipped defaults (the settings set aside), the raw window
samples with the pointer, black bars, the desktop's return after the quit, the input log with press
times (the level load), the form's answers and the reviewer's questionnaire.

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

No Controls or Features (the controller badge is the catalog entry's `controller`), the player's own
settings, Load the initial load only, the same curves and Scrap floors, and the weakest-link cap: the
medal at most one level above the weakest aspect (counted on the Platinum-to-Bronze scale), where 2.0
has the broken-subsystem rule instead.

## The medals

Rules 2.1 (the user's meanings, 2026-10-26; score.py `MEDALS_V2`):

| Medal | Meaning | When |
|---|---|---|
| **Platinum** | awesome | a mean of 90 or more |
| **Gold** | playable, but below ideal | 75 or more |
| **Silver** | playable, with some rough edges, but not that bad | 55 or more |
| **Bronze** | at the limit of playable | 35 or more |
| **Rust** | playable, but some part is broken: no or bad sound, a very low frame rate, broken textures, big stutters, occasional crashes | a core subsystem broken (`broken_v2`, any mean), or a mean under 35 |
| **Scrap** | completely unplayable | won't start, crashes at startup or before play, no session reaches play, crashes in more than half the sessions or within 5 minutes of play in half of them or more, a median under 5 fps |
| **Untested** | nobody has played it on Walnut yet | |

Rules 1.0 keep their own scale (score.py `MEDALS`, `medal_of`): Platinum to Bronze as above, **Tin**
(playable, but rough) 25 to 35, **Rust** (barely playable) 15 to 25, **Scrap** under 15 or a floor
(won't start, doesn't reach play, crashes in most sessions, a median under 15 fps or over 10 s of
stutter a minute). No rating is Tin today; the site's and the app's medal filters list Tin only when a
game has it.

- **The mean, and broken means Rust** (rules 2.1). The mean gives leeway: a game perfect everywhere
  at 50 fps loses a few points, not a medal, and one weak aspect costs only its weight. A broken core
  subsystem isn't averaged away: the game is Rust, with the mean shown beside it and what's broken
  ([How a game is rated](#how-a-game-is-rated)). Rules 1.0 had a weakest-link cap instead.
- **Occasional crashes are Rust, consistent ones Scrap** (rules 2.1). A crash late in one of two
  sessions (12 min into play) is Rust, the crash on the page ("stability broken (1 crash in 0.4 h)");
  one crash with over an hour of play between crashes only lowers Stability. Scrap needs the crash
  to be consistent: at startup or before play in any session, in more than half the sessions, or
  within the first 5 minutes of play in half of them or more ([Mr.Boom](../gamedb/games/mrboom/): 30 s into both sessions). A
  single session that crashed late is Rust (rules 2.2): one session can't show the crash is consistent;
  one that crashed early or at startup is Scrap.
- **Rust and Scrap show their reason**: won't start, crashes (how many, how early), too slow, or what's
  broken.
- **Sessions measured alike are merged** (medians), those of the newest version measured the way its
  newest session was: a session from before the sound dropout logger isn't merged with one after it
  (catalog.py `method`), nor one of another rules data version; it stays on the page as a test run.
- **The first 30 s of play are a warm-up** (the first level starting, caches filling): frame times,
  stutter and sound dropouts are measured from 30 s after the load (import-run.py `WARMUP_S`).
- **Play starts at the match.** In a played session the frames, stutter and dropouts are measured
  from the game's play mark (the line a game logs when the match or level starts: `play_mark` in its
  gamedb `rating/test.toml`) plus
  the warm-up, so menus, setup screens and the player's time there never count as play.
- **Every level start is a load (rules 2).** A played session's play mark is logged at each race or level
  start (bench.txt `play_marks=`). The load before each later one (from the earlier of the player's last
  key press and the first frame over 1 s in the 30 s before it, so the end of the previous race and the
  menus go too) and the warm-up after it are left out of the play like the first; the level loads' median
  is Load's level load.
- **Stutter is local (rules 2).** A frame stutters against the median of the 60 frames on each side, not the
  session's: a heavy level running steadily slower counts in the frame rate, not as stutter.
- **Sessions merge on the same driver stack.** Only sessions with the newest one's Mesa and kernel versions
  are merged into a rating (a crash a newer Mesa fixed doesn't count against the next run); the history
  keeps them all.
- **Sessions merge on the same data edition.** The harness records the game's installed data packages
  with their edition and release (result.toml `[data]`, from each package's `X-Walnut-Edition` and
  `X-Walnut-Release`, else gamedb's editions: gamedb/README.md); a rating merges only sessions whose set
  of editions is the newest one's, so Quake on the shareware isn't averaged with Quake on the full game.
  The package names don't count (the licensed suite's `rct2-data` and the imported
  `roller-coaster-tycoon2-data` are both `full`), nor the release (a CD and a GOG copy of one edition
  play the same). A session that recorded no data, from before the harness did (2026-10-27) or of a game
  without data packages, merges as before (catalog.py `same_editions`).
- **No medal without frame rate and smoothness.** They are the core of a rating; a session where
  the frame timer saw nothing (a software renderer doesn't go through OpenGL, which the timer hooks)
  gets its numbers but no medal ("played, but not rated: frame rate and smoothness not measured"),
  unless a Scrap floor applies (a crash before play, consistent crashes), which makes it Scrap either way
  (score.py `REQUIRED`).
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
- **Launchers.** A game with several menu entries ([OpenArena](../gamedb/games/openarena/)'s GL 1.2 and GL 2.0 renderers) is rated
  once per launcher: each test records the entry it started (result.toml's `launcher`, the entry's
  Name; `import-run.py --launcher NAME` for runs from before it was recorded). The game's medal is its
  best launcher's, named with it ("Gold, with OpenArena (GL 1.2)"); the page and the app show every
  launcher's rating, and the app's Play menu lists the best first. A test without a launcher belongs to
  the game's only named one; with several, to "the default launcher". If a launcher other than the
  default rates best, the default is the thing to fix.
- **Stability needs hours.** The page shows the play time and crashes behind it ("0 crashes in
  1.7 h"); a full score needs three crash-free hours (0 crashes in 3 h bounds the rate under one an
  hour, the rule of three), and the 90 a crash-free game starts at is Platinum level, so a short test
  never caps the medal. Confidence grows with the hours played.
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
predate the dropout measurement, so their sound is scored from the raw fault counts. The numbers are the
summary batch gamedb:batch/suite-summary-2026-10 (each game's `benchmark/results/suite-summary-2026-10.toml`,
every value with its source); `tools/gamedb/score.py` reads it, and the table is generated from it
(`tools/gamedb/blocks.py`, checked by `make test`).

<!-- gamedb:calibration -->
| Game | Frame rate | Smoothness | Sound | Mean | Weakest | Medal |
|---|---|---|---|---|---|---|
| X-Moto | 100 (160.7 fps, 1% low 106.7) | 100 (0 s of stalls, longest 43 ms) | 100 (0 faults a minute) | 100 | 100 (frame) | Platinum |
| OpenArena, opengl1 | 85 (70.6 fps, 1% low 34.6) | 100 (0 s of stalls, longest 56 ms) | 100 (0 faults a minute) | 96 | 85 (frame) | Platinum |
| Yamagi Quake II | 80 (68.7 fps, 1% low 31.1) | 100 (0.1 s of stalls, longest 91 ms) | 100 (0 faults a minute) | 95 | 80 (frame) | Platinum |
| OpenArena, opengl2 | 72 (60.4 fps, 1% low 25.6) | 100 (0.2 s of stalls, longest 199 ms) | 100 (0 faults a minute) | 93 | 72 (frame) | Gold (capped) |
| QuakeSpasm | 100 (91 fps, 1% low 48.6) | 65 (2.5 s of stalls, longest 393 ms) | 100 (0 faults a minute) | 93 | 65 (smooth) | Gold (capped) |
| Armagetron | 70 (83 fps, 1% low 24.1) | 100 (0.1 s of stalls, longest 127 ms) | 100 (0 faults a minute) | 92 | 70 (frame) | Gold (capped) |
| ClassiCube | 68 (47 fps, 1% low 30.7) | 100 (0 s of stalls, longest 65 ms) | 100 (0 faults a minute) | 92 | 68 (frame) | Gold (capped) |
| Extreme Tux Racer | 75 (59 fps, 1% low 27.3) | 62 (2.9 s of stalls, longest 483 ms) | 100 (0 faults a minute) | 85 | 62 (smooth) | Gold |
| Luanti | 50 (46 fps, 1% low 19.1) | 58 (3.5 s of stalls, longest 724 ms) | 100 (0 faults a minute) | 78 | 50 (frame) | Silver (capped) |
| dsda-doom | 93 (55.9 fps, 1% low 40.3) | 100 (0 s of stalls, longest 34 ms) | 50 (465 faults a minute) | 90 | 50 (sound) | Silver (capped) |
| BananaBread | 50 (39.1 fps, 1% low 25.6) | 100 (0.05 s of stalls, longest 103 ms) | 79 (40 faults a minute) | 83 | 50 (frame) | Silver (capped) |
| Neverball | 95 (66.5 fps, 1% low 41.3) | 30 (2 s of stalls, longest 3900 ms) | 75 (50 faults a minute) | 80 | 30 (smooth) | Bronze (capped) |
| OpenTTD, 8bpp | 31 (38.3 fps, 1% low 13.4) | 94 (0.4 s of stalls, longest 210 ms) | 50 (3500 faults a minute) | 73 | 31 (frame) | Bronze (capped) |
| SuperTuxKart lean | 4 (21 fps, 1% low 11.8) | 70 (1.8 s of stalls, longest 513 ms) | 97 (3 faults a minute) | 68 | 4 (frame) | Bronze (capped) |
| OpenTTD, 32bpp | 0 (11 fps, 1% low 5.7) | 0 (52 s of stalls, longest 311 ms) | 50 (3500 faults a minute) | 45 | 0 (frame) | Scrap (too slow) |
<!-- /gamedb:calibration -->

Every medal below Gold has a real problem behind it: slow frame rates ([Luanti](../gamedb/games/luanti/), [BananaBread](../gamedb/games/bananabread/),
[SuperTuxKart](../gamedb/games/supertuxkart/)), [Neverball](../gamedb/games/neverball/)'s 3.9 s level reload, [dsda-doom](../gamedb/games/dsda-doom/)'s chopping sound, and the 32bpp [OpenTTD](../gamedb/games/openttd/) that
can't be played is Scrap. Sound is half presence, half cleanliness, so bad sound costs a medal step
but no longer sinks a game on its own. Open: whether strategy and turn-based games need their own
frame-rate scale (OpenTTD plays fine at 38 fps and scores 31 on frame rate); one scale for now, revisited after the first strategy sessions.

## The website

Built by `tools/gamedb/build.sh` (Hugo, website/gamedb/) from the catalog and technical.json; CI
publishes it on every push that changes it (.github/workflows/walnut-site.yml). One design serves the
browser, NetSurf and the app's HTML view (the user, 2026-10-28):

- **A game's tabs are pages**: `games/<id>/` Overview (the rating: medal, score and the aspects behind it, an
  aspect without a score saying "not tested" in its row and nothing in the measured cell, then one line, "Rated from N sessions, M minutes of
  play on <game version>", linking Test runs; the screenshots; the installation, only the easy way: Walnut
  Games' Install and Add game files..., with the copies that work, the apt line, and "More ways" on the Guides;
  the known issues), `games/<id>/test-runs/` (the played sessions, which make the rating (what every session shares, the Walnut release,
  the game's build and the platform builds, is said once above the table, which has a column only for what differs),
  each with a page of its own, `test-runs/<session>/`: the session rated on its own, every aspect's score and measurement, the
  sound dropouts, the builds, the form's answers and notes, the picture review, the screenshots; all of them
  as a CSV download, a `data:` link, since the site server takes no .csv; the rating on each version; then
  the scripted benchmarks in a section of their own, "Not rated: tests of driver and game changes", a table
  per batch: the variant, the conditions it adds (its ENV, the stall recorder or a profiler alongside), steady
  fps, 1% low and stalls, then the observations, always shown; the whole-run fps, the other metrics, the
  Mesa and kernel build, the resolution and the run length are under "All numbers" (a `<details>`, open in
  NetSurf); the recipe is a two-line summary with its full text behind a `<details>`; rows that
  gamedb/benchmarks/caveats.toml names are marked invalid or not comparable), `games/<id>/technical/` (the
  index.md text, technical.md, the build, the played test's setup, the investigations) and
  `games/<id>/guides/` (the bundle's `guides/*.md`: how-tos, tips, recommended settings; a game without any
  still has the tab, with a line saying so). The tab bar is a list of links with the current one marked; no
  JavaScript is needed anywhere (the filters and the Copy buttons are extras that show only where they
  work). An engine's page (`engines/<e>/`) has no tabs: it has no rating, sessions or guides, only technical
  content, so it stays one page in the same style.
- **Builds of a session**: the game's package as installed, then a fixed list of the platform Walnut patches
  for every game (kernel, Mesa, ps3rsx, libspeq, OpenAL Soft, FluidSynth, SDL3; `tools/gamedb/sessions.py`
  STACK), read from the session's `raw/versions.xz`. Not each game's dependency closure: that needs Debian's
  ppc64 package index, which the site build doesn't have. The desktop and the release metapackage stay out.
- **How a game is tested**: the Test runs tab says testing is done with the rating tool (`walnut-rate`,
  planned) and links `ratingDocs` (hugo.toml params; for now the about page's "How a game is tested"),
  the one place to point at the tool's own documentation when it exists.
- **Tags** (gamedb/tags.toml, [gamedb/README.md](../gamedb/README.md#tags)): every tag shown is a link to
  its page, `tags/<id>/` (content/tags/_content.gotmpl, from catalog.json's tags), which lists the games that
  have it as cards; `tags/` is the index, by group. A game's head shows its tags under small group labels; a
  card shows its genres and "supports gamepad". The home page's filter panel (assets/gamedb.js, where the
  browser has URLSearchParams and history.replaceState) has a box per group with a checkbox per tag
  (Keywords folded): ticked tags of one group are ORed, the groups ANDed, with the medal and the search as
  before; the state is the query string (`?tags=fps,local-multiplayer&medal=gold&q=...`), so a filtered
  view can be linked. Without the script (NetSurf) the panel stays hidden and the tag links are the way in.
- **"Open in Walnut Games"**: the Overview's head has a `walnut://game/<id>` link for a visitor on a
  Walnut console, with no script: the site's CSS hides `.walnut-os-only`, and walnut-games' postinst puts the rule that shows it
  into NetSurf's system user stylesheet, `/usr/share/netsurf/user.css` (NetSurf 3.11 reads `resource:user.css`
  from its resource path: `~/.config/netsurf/` first, then `/usr/share/netsurf/`, so a user's own
  `~/.config/netsurf/user.css` takes precedence and hides the link again). The rule (the package's
  `files/netsurf/user.css`, installed as `/usr/share/walnut-games/netsurf-user.css`) goes between
  `/* walnut-games begin */` and `/* walnut-games end */`: the file is made when missing, the block appended
  to rules already there, replaced when present; postrm (remove, purge) takes out only the block, and the
  file when nothing else is left. NetSurf itself is not patched and no package ships the file. NetSurf hands a scheme it
  can't fetch to the desktop's default handler for `x-scheme-handler/walnut` (tested in a container with a
  dummy handler set by `xdg-mime`); the app registers it. The app's own copy (`--app`) leaves the link out.
- **Styles**: `assets/gamedb.scss`, compiled by Hugo's libsass (css.Sass) to CSS 2.1: the palette is SCSS
  variables (no `var()` in the output), sizes are px, and flex or grid only ever comes after a float or
  inline-block fallback (the `row` and `card-grid` mixins), so NetSurf and Tkhtml lay the pages out
  without it and modern browsers keep the look. Old engines drop rounded corners, gradients and the
  grid's even rows; NetSurf runs no script here, so it shows the plain list without filters.
- **Links are relative** (build.sh rewrites every site link against the page after Hugo; Hugo's own
  relativeURLs breaks under the /gamedb/ base path), so a copy of the output works offline.
- **The app's copy**, `build.sh --app OUT` (designed, not used yet): the same pages without the site's
  header, footer and script, every folder link naming its `index.html` (a file:// view adds none), the
  screenshots at their display size, 352x198 (a decoded 800x450 JPEG costs Tkhtml about 3.5 MB on the PS3;
  the full one is behind the click), the tab links kept on one line, selectors CSS 2.1 only, and
  the game's actions as `walnut://install/<id>`, `walnut://add-data/<id>` and `walnut://play/<id>` links
  in place of the apt line (`layouts/partials/get-game.html`), for the Details view to catch.

## The desktop app

`walnut-games` (package `walnut-games`, Tk; the DualShock, mouse and keyboard) shows the catalog as cards: a
thumbnail, the medal with its score, the name and categories, an "Installed" mark. The top bar
has search, category, medal and sort, and "installed only". A card opens the game in the right-hand
pane (PLAN 5.11): its name, medal and score with the hours played, then the actions, then a quick glance.
The window follows the Walnut palette (`walnut_games.py` has the colours).

- **One main button follows the state** (`main_action`): Install; installed with its required data
  missing, Add game files...; then Play (with several launchers a menu of them, each with its medal);
  Importing... while an import of this game runs; off while the helper or an import runs, for a game
  only on the website, or without a launcher. Under it the smaller ones (`side_actions`): Add game
  files... when it isn't the main one (optional data, the full game in place of a free edition, or the
  data before the game is installed), Install from Walnut licensed, Try again with download, Remove and
  Remove data (only with data installed: the helper's `remove-data`, the game stays). The REQUIRED: or
  OPTIONAL: line is one line under the Add game files... button it concerns. The import's and the
  helper's progress, errors, the "Also add" buttons and a partial import's notice are under them.
- **The glance**: up to 3 key numbers (the median frame rate, the smoothness score, the installed size
  from `apt-cache show`), the controller and the installed data's edition, up to 4 screenshot thumbnails
  (a click opens it in Details), Details, Technical notes, Open on the web and the game's home page.
  The aspects, the launchers' ratings, known issues, the data steps and the patches are in Details.
- **Details** is the site's pages for the app (`tools/gamedb/build.sh --app`, packaged in
  `/usr/share/walnut-games/pages/`, docs/PACKAGING.md "Walnut Games' catalog and pages") in
  `tkinterweb.HtmlFrame`, full window in place of the list, zoomed like the app's text. tkinterweb is
  imported at the first open, so the app starts as small as before, and the frame is destroyed on Back.
  The tabs (Overview, Test runs, Technical, Guides) are links between the pages; L1 / R1 (and left /
  right) step through them. Every clicked link comes to the app (`link_target`): `walnut://install|add-data|play/<id>`
  (play may name a launcher, `?launcher=NAME`) closes Details and runs the same code as the main button;
  a page or picture of the app's copy loads in place (Back returns to it); the website's pages the copy
  doesn't carry and other sites open in the system browser (`webbrowser`, xdg-open); anything else is
  ignored. Without python3-tkinterweb (a Recommends), or for a game the copy doesn't have, Details opens
  the website in the browser.
- **The pad and the TV.** The text and the pane scale with the screen (1.5x at 1080 lines;
  `WALNUT_GAMES_SCALE` overrides) and the window fills it. The DualShock is the X pointer outside games
  (ps3-desktop's `40-joystick-pointer.conf`: the sticks move it, Cross is a left click, Circle a right
  click); the app reads the buttons the pointer leaves free from the pad's event device (no grab;
  presses queued while game mode froze the app are dropped): the d-pad moves the focus between the
  controls (the nearest one that way, `nav_pick`; the cards' arrows move between cards and leave at the
  grid's edge) and puts the pointer on it, so Cross presses the focused control; Square opens Details,
  L1 / R1 change its tab, Circle (a right click) and Escape go back (Play's menu, the chooser, Details'
  page before, Details). The keyboard does the same with the arrows, Enter and Escape. The focused
  control has a light border. `WALNUT_GAMES_PAD=off` stops the pad reading.
- **walnut://game/<id>** (the website's "Open in Walnut Games"): the .desktop file is the handler of
  `x-scheme-handler/walnut` (Exec `walnut-games %u`). Only that form is taken from outside; install,
  add-data and play links are never accepted on the command line. One app runs at a time: it holds
  `$XDG_RUNTIME_DIR/walnut-games/lock`; a second start writes the id to `$XDG_RUNTIME_DIR/walnut-games/open`
  and ends, and the running app (which looks every half second) comes forward on that game's Details.
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
  missing thumbnail (a game may have none: the catalog's `thumb` is then null) shows the placeholder. Install stays on the packaged catalog (the helper maps ids
  to packages from it): a game only on the website shows Install off with "Install it after the next
  Walnut Games update". Play and Remove work for installed games.
- **Installed** comes from `dpkg-query`. **Play** finds the game's launchers in its packages'
  manifest (`dpkg -L`): the menu entries they ship (a diverted one isn't there and drops out), and any
  shown menu entry that runs one of their programs (Walnut's own entries, like [OpenArena](../gamedb/games/openarena/)'s GL 1.2 and
  GL 2.0). The catalog's `desktop` list is optional: it orders the launchers or adds one the manifest
  can't lead to. Each launcher starts through its runtime menu copy
  (`$XDG_RUNTIME_DIR/ps3-game-menu/applications/`, Exec through `ps3-game`), else `ps3-game` plus the
  entry's Exec (field codes cut out, no shell). With more than one launcher, Play opens a menu of them
  by their names. Its output goes to `~/.cache/walnut-games/<id>.log`; a game that quits within 20 s
  says so in the status line. Play is
  off while an install or removal runs: game mode would freeze the helper with dpkg's lock held. After
  either, the app runs `ps3-game-menu "$XDG_RUNTIME_DIR/ps3-game-menu"` so the new entry exists.
- **The helper**, `/usr/lib/ps3/sbin/walnut-games-helper install|remove|status ID` (and the data actions
  below, and `adopt`), runs as root through
  `sudo -n` (`/etc/sudoers.d/walnut-games`: group sudo, no password, that one path). Its guarantees:
  the argument is a game id (`^[a-z0-9][a-z0-9-]{0,63}$`) found in the root-owned packaged catalog, which is
  the only source of package names (each checked against the Debian name rule; licensed or nonfree data
  packages refused); `python3 -I`, fixed PATH and environment, no shell; install runs `apt-get install
  --no-remove` (after `apt-get update` when the lists are over a day old) with the configuration files
  kept, noninteractive; remove simulates first and refuses, before touching anything, if the simulation
  removes anything but the game's own packages and the old `ps3-games` / `ps3-games-free` metapackages
  (built no more since 2026-10-28, docs/PACKAGING.md "No game metapackages"; a console keeps its last
  one), and in the metapackage case first marks the other installed packages it depends on as manually
  installed (a package another installed game lists too stays, below); `adopt`, which the app runs at
  every start, marks every installed package of a catalog game (and every dependency of an old
  metapackage still installed) manual when apt has it automatic, so removing a metapackage leaves no game
  to autoremove;
  it never runs autoremove; SIGHUP and SIGPIPE are ignored, so closing the app never stops dpkg halfway.
  Output is `progress N text`, `log`, `error` lines; exit 0 done, 2 refused, 1 apt failed.
- **Game data.** A game whose data comes from the player's own copy (Doom, Quake's full game, RollerCoaster
  Tycoon 2 for OpenRCT2, TTD's original graphics for OpenTTD) has a `[data_setup]` in its build.toml
  (gamedb/README.md): the game-data-packager (gdp) game, the packages any one of which makes it playable, the
  optional ones, the ones allowed to download their official patch files, a hint where the files are, and
  each package's edition. The catalog carries it with `present_any`, those packages plus the licensed
  suite's builds of the same data; the app shows "Your game data is installed" when one of them is, and
  the installed data's edition with the rating's facts. Games on one engine are kept apart: every launcher
  of ours is stamped `X-Walnut-Game=<id>`, Play lists a game's own, and removing Doom while Freedoom's game
  still uses dsda-doom removes only Doom's data.
- **The app.** A game page offers "Install from Walnut licensed" when the apt sources have one of its
  `present_any` names (the licensed suite; `apt-cache policy` in a thread, a candidate counts only from a
  package source), then one import button, "Add game files..." (required data missing, or optional data only like OpenTTD),
  with a label under it that starts with the tag REQUIRED: or OPTIONAL: and names the original game
  (`[data_setup] original`, default the game's title). Play stays off while required data is missing. The chooser is a window of big buttons (the pad is the pointer; arrows, Enter, Escape):
  the disc in the drive, `/media` and `/run/media` mounts, GOG and Steam folders, `*.iso`, `*.img`, `*.zip`,
  `*.exe` and `gog_*.sh` in Downloads and the home folder, Other file and Other folder, with the game's hint.
  The import runs in its own session with its output in `~/.cache/walnut-games/import.log`; the app shows
  the step, the file being read, the elapsed time, the last log lines and a moving bar. Closing the app
  leaves it running; reopened, the app finds it by walnut-data's lock. Errors are in English from gdp's
  kinds (`not-found`, `unknown-version`, `unsuitable`, `needs-download` with "Try again with download",
  `no-space` with walnut-data's figures, `failed` with the last log lines). After an import or an install
  `ps3-game-menu --reload` shows the launchers the data added (`X-Walnut-Requires`), and the page names them.
  Remove names the imported data and its size and runs `remove-data`.
- **walnut-data.** `walnut-data import <id> <source>...` (walnut-games, `/usr/bin`) runs our
  game-data-packager as the player, in a transient user unit (memory and time limits, no new
  privileges, no network unless the game may download its official patch files), on a folder, an
  installer, the mounted disc or an ISO it loop-mounts read-only through udisks. Each package gdp makes
  goes to the helper as a tar stream only: `walnut-games-helper import-data <id> <pkg>` takes regular
  files whose path, size and checksum are in the packaged manifest (`/usr/share/walnut-games/data-manifest.json`,
  `make gdp-manifest`), writes the control file itself (gdp's version, `Section: local/games`, the
  manifest's same-game Conflicts/Replaces, `X-Walnut-Edition` from `[data_setup] editions`, `X-Walnut-Release`
  from gdp's name of the file version that matched, e.g. `1.9ud`, `cd`), no maintainer scripts, and
  installs it after apt's simulation shows nothing else changing (docs/PACKAGING.md "Data built on the
  console"). `walnut-data list` shows each game's data state, `remove <id>` and `install-repo <id>` call
  the helper's `remove-data` and `install-repo-data`. Every helper action holds one lock; a second is
  refused as busy, and `status` says `importing` meanwhile.
- **Editions.** Sessions record the data (see "Sessions merge on the same data edition" above), and a
  rating merges only sessions on the newest one's editions.
- Tests: `packages/walnut/apps/walnut-games/tests/test_walnut_data.py` (make test ONLY=walnut-data): import-data
  against tar fixtures (links, devices, `..`, wrong sizes and checksums, a cut stream, extra files), the
  package it writes, the lock, remove-data, install-repo-data, and walnut-data with a stubbed gdp.
  `packages/walnut/apps/walnut-games/tests/test_walnut_games.py` (make test ONLY=walnut-games): the helper's validation,
  catalog mapping, cascade refusal (with a fake `apt-get -s`), install arguments and status relay (a fake
  apt-get writes to the status descriptor), Exec parsing, filters, the main button in every state, the
  walnut:// links and the hand-off, the pad's buttons, and window smoke tests on a private Xvfb/Xvnc
  (skipped when neither exists): the pane's states, Details with and without (a fake) tkinterweb.

**The HTML view** (Details, PLAN 5.11) is Tkhtml 3.0 through TkinterWeb: packages `tk-html3` and
`python3-tkinterweb` (packages/repackaged/; neither is in Debian, and PyPI's wheels carry Tkhtml for x86
and arm only). `tkinterweb.HtmlFrame(on_link_click=f)` hands every clicked link to `f` instead of
navigating, so the app runs `walnut://install/<id>` itself and loads the rest with `load_url`. PNG and
GIF go through Tk, JPEG through Pillow (python3-pil.imagetk, already a walnut-games dependency); no
libtk-img. Measured on the PS3 (2026-10-28: the `--app` build's SuperTuxKart pages in a 1280x720 window,
the desktop running): importing tkinterweb and making the frame adds 16 MB to a Tk process; a text page
(Technical, Test runs) 8-9 MB more and 1.2-1.3 s until drawn; the Overview with its 11 screenshots
(800x450 JPEGs, shown at about 290x160) 40 MB and 2.1-2.5 s. A decoded picture costs about 3.5 MB, so
the app's copy should carry the screenshots at the size shown. Scrolling takes 6-9 ms a step (44 ms at
worst). The PS3's pages look the same as the PC's (an amd64 build under Xvfb): no byte-order problem
(Tkhtml has no byte-order code; colours and images are right). Tkhtml knows CSS 2.1 selectors only
(`a[href^="walnut:"]` is an error: match `a[href]` and test the value).

## Adding a game

A game is one folder, `gamedb/games/<id>/` (gamedb/README.md has the schema): `index.md` with the catalog entry in its
front matter (title, summary, tags from gamedb/tags.toml, the packages to install, optionally the .desktop entries,
`fps_cap` if it has one, whether it needs the user's own data, the controller, `designed_for`:
`"pad"` by default or `"keyboard-mouse"`, set by the user per game) and notes as its
body, a `thumb.png`, and one folder per test (`rating/sessions/<date>-<kind>/`) with its generated
`result.toml` and the session's evidence: every screenshot (`shots/ps3-<point>.jpg`, 800x450 JPEG, Git
LFS), the player's `form.toml`, the reviewer's `review.md` and the harness's files (`raw/`, scrubbed,
private; gamedb/README.md has the layout). A session takes a screenshot every minute; the record's
gallery is up to four that differ (black frames and near-duplicates, like a menu caught three times,
dropped by comparing small greyscale versions; then the most varied), and `--shots 60,240` picks them by
hand instead. `tools/gamedb/import-run.py RUN... --game ID` writes a test folder from harness run
directories and never touches `index.md`; the record is derived from the evidence it keeps, so
`tools/gamedb/reimport.py` derives every record again without build/bench when the derivation changes
(`--check` in `make test`: nothing changes); `tools/gamedb/check.py` validates the catalog;
`tools/gamedb/build.sh OUT` builds the site ([The website](#the-website); website/gamedb/hugo.toml mounts `gamedb/games/` as its
`content/games/`, only `index.md`, `thumb.png` and the sessions' `shots/*.jpg`; CI fetches only the
gallery from Git LFS: `tools/gamedb/gallery.py`). A test is a `session` (a played session) or a `capture` (the
picture only). Ratings come from played sessions alone: the benchmark runs of the suite (the
desktop stopped, vsync off) don't describe what a player gets and aren't imported; the code still
reads a `demo` test (a harness run with a measured window) if one is ever imported on purpose. Games that need the user's own data (Quake,
Quake II, [Sauerbraten](../gamedb/games/sauerbraten/)) are listed and shown, with a guide to setting up the data; the data itself is never
offered (their screenshots, thumbnail included, are published like any game's).

All game-data instructions are on the game's Guides tab; the Overview's Installation gives only the easy
way (Walnut Games' Install, then Add game files... with the copies that work) and links "More ways: Guides"
(`layouts/partials/data-easy.html`). A game with a `[data_setup]` always has the guide "Setting up the game
data" (`#game-data`), generated so it can't drift from the data (`tools/gamedb/dataguide.py`, from the
`[data_setup]`, `tools/gamedb/gdp-manifest.json` and our game-data-packager patches;
`layouts/partials/data-guide.html`): what works (the hint), then three ways, each a section: 1. Walnut Games
(the button, or `walnut-data import <id> <source>`, the add-ons with `-p`, the CD music), 2. game-data-packager,
our patched Debian tool (the command per package, then `sudo apt install ./<deb>`; what our patches accept that
Debian's gdp may refuse), 3. by hand (a row per file: its path under the package's folder, the accepted versions and the size range;
the bytes and checksum of each version in a `<details>` under it, open in NetSurf). The bundle's `guides/game-data.md`, when there is one, adds the hand-written notes: which CD or
edition, quirks, what the result looks like. A game that needs data without a `[data_setup]` (Sauerbraten)
keeps `data_steps` in its front matter (a list of short steps, commands and paths in backticks; `check.py`
refuses a `needs_data` game without steps, and steps on a game without `needs_data`), shown as a numbered
list above its hand-written guide; the app's details pane shows the same steps as text. Every command in a
guide is run in a Debian container before it is written.
