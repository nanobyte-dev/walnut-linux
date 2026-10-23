# Walnut Games: the games database and launcher

A list of the games that run on Walnut, each played and measured on the PS3, compared with the same
game on a PC, and rated. It is two things over one catalog:

- **the website**, https://walnut.nanobyte.dev/gamedb/: a card for every game with its medal,
  filters (category, medal, tags, "patched by Walnut") and search; each game's page has the
  screenshots (PS3 next to PC), the test results, and the `sudo apt install` line;
- **the desktop app** on Walnut (`walnut-games`): the same cards, with Install, Remove and Play.

Status (2026-10-23): designed with the user, being built. The steps are in
[PLAN.md](PLAN.md) ("Walnut Games"); this page describes how it works.

## How a game is tested

Two tests, for different questions:

1. **A played session, about 10 minutes, the graded test.** Someone plays the game normally,
   started from the desktop menu like any user would: game mode, the settings Walnut ships, vsync,
   the 1080p TV. Players hit menus, saves, level changes and odd inputs that a scripted run never
   reaches. A session recorder records the frame times, the sound faults, memory, the kernel log,
   how the game ended, and a screenshot about once a minute. When the game exits, a short form asks
   whether sound and music were heard, how the controls worked, and has a free-text box for notes
   on the gameplay; the notes are read with the numbers and become the game's known issues.
2. **A scripted capture run, for the picture.** A recipe drives the game to fixed points (a menu, a
   map, a camera position), the PS3 captures them, and the same recipe captures them on a PC. The
   two sit side by side on one sheet and someone looks at it: missing textures, wrong colours,
   undrawn text. Not a pixel diff.

Every session adds its play time to the game's totals, so the numbers sharpen as people play.

## How a game is rated

Each aspect gets a score from 0 to 100, on the same scale for every game; the medal comes from the
weighted mean of the scores.

| Aspect | Weight | Measured | 0 | 100 |
|---|---|---|---|---|
| Frame rate | 25 | median and 1% low over the session's play | median 20 fps, 1% low 10 | median 55, 1% low 45, or the game holds its own cap |
| Smoothness | 20 | stutter time per minute and the longest frame | 5 s a minute, or a freeze over 5 s | 0.1 s a minute, nothing over 250 ms |
| Sound | 15 | sound faults per minute of play (ALSA underruns, OpenAL's xruns and waits); a wedged sound device; heard or not | silent, or wedged | no faults, sound and music heard |
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
scores 75 and 2 s scores 55; for sound, one fault a minute scores 75, six score 55, twenty score 35.

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
- **Scores are medians over sessions**, so one bad or lucky run doesn't flip a medal.
- **Stability needs hours.** The page shows the play time and crashes behind it ("0 crashes in
  1.7 h"); under an hour the stability score is marked provisional, and a full score needs three
  crash-free hours (0 crashes in 3 h bounds the rate under one an hour, the rule of three).
- **Badges, outside the medal:** controller support (full, partial, keyboard), needs your own game
  data, no sound, low memory (under 10 MB free at some point in play), patched by Walnut.
- **Every page shows the medal, the overall score ("Gold · 82"), each aspect's score and the
  numbers behind it.**

## Calibration: the 13 benchmarked games

Before any game was rated, the scale was tried on the 13 games of the benchmark suite with their
known numbers ([games/README.md](games/README.md)). Those runs had the desktop stopped and vsync off,
so they calibrate the formula; real medals come from played sessions. Stutter was approximated from
the stall time (frames over 100 ms), load is unmeasured there, stability assumed clean.

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
| dsda-doom | 93 | 100 | 0 (465 underruns a minute) | 82 | 0 | Bronze (capped) |
| Sauerbraten | 50 | 100 | ~18 | 74 | 18 | Bronze (capped) |
| Neverball | 95 | 30 (a 3.9 s reload) | ~9 | 70 | 9 | Bronze (capped) |
| OpenTTD, 8bpp | 31 | 94 | 0 (choppy music) | 65 | 0 | Bronze (capped) |
| SuperTuxKart `lean` | 4 (21 fps) | 69 | ~67 | 63 | 4 | Bronze (capped) |
| OpenTTD, 32bpp | 0 (11 fps) | 0 (52 s of stalls) | 0 | 37 | 0 | Scrap (too slow) |

Every Bronze has a real problem behind it (four have known-bad sound, SuperTuxKart runs at 21 fps),
and the 32bpp OpenTTD that can't be played is Scrap, not Bronze. Open: whether strategy and
turn-based games need their own frame-rate scale (OpenTTD plays fine at 38 fps and scores 31 on frame
rate); one scale for now, revisited after the first strategy sessions.

## Adding a game

A game is one folder, `gamedb/content/games/<id>/`: `index.md` with the catalog entry in its
front matter (title, summary, categories, tags, the packages to install, the .desktop entry,
`fps_cap` if it has one, whether it needs the user's own data, the controller) and notes as its
body, a `thumb.png`, and one folder per test with its generated `result.toml`. Screenshots live on
walnut.nanobyte.dev, not in the repository. Games that need the user's own data (OpenRCT2, Quake)
are listed and shown, with a guide to setting up the data; the data itself is never offered.
