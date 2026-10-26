+++
title = "About the ratings"
summary = "How Walnut Games tests a game on the PS3 and turns the measurements into a medal."
+++

**Every rating comes from someone playing the game on a PS3; nothing scripted counts.**A rating says how well a game plays on a PS3 under Walnut, not how good the game is.** Game
design never scores: a punishing difficulty, short levels, a game's own frame cap or simulation
speed, controls laid out for a PC keyboard, or whether it's fun. A game made for keyboard and mouse
(a strategy game, a PC shooter) isn't marked down for weak gamepad support. Anything broken that you'd run into
playing it on Walnut does score, wherever it comes from, and the page says where: each problem is
marked "also on the PC" (the game's own, it happens on a PC too) or "PS3/Walnut only" (ours).

Every game here was installed on a PlayStation 3 running Walnut and played. What you see on a
game's page is what we measured, not an impression: the frame rate, the stutters, the sound, the
picture next to the same game on a PC, crashes, how it handles a controller, and how long it takes to
load. The medal sums that up; the numbers behind it are always on the page, with what the rating
rests on ("1 session, 9 min of play").

## How a game is tested

**A played session.** One person plays the game for about ten minutes, the way you would: started
from the Walnut desktop, on a 1080p TV, as the game ships: the tester's own settings for it are put
aside for the session and come back afterwards, so the game starts like it would for you on day one,
and no setting is changed while testing. While they play, a recorder notes every frame's time, every
sound dropout, the memory, how the game ended and how quickly the desktop came back, a screenshot
about once a minute with where the game's window and the mouse pointer were, and which controllers
the game picked up and when buttons were pressed (never which keys, never what was typed). At the end
the player answers a few one-tap questions about what only they could tell: whether they heard the
sound and music, glitches they saw, how the gamepad worked in the menus and in play, the keyboard and
mouse, whether saving worked, and writes down anything odd; those notes become the game's known
issues.

**A picture check.** The screenshots are put next to the same game on a PC, and a reviewer answers
the same fixed questions for every game by looking at them: is the picture stretched or squashed, how
small is the smallest text, is a mouse pointer in the way, is anything garbled, missing, wrongly
coloured, flickering or lit differently, is anything broken. Each problem is noted with whether the PC
shows it too. The screenshots on these pages are all from the PS3.

**What one player can't test isn't held against a game.** A second or third controller, online play,
a mode nobody reached: those are "not tested", and their weight goes to what was. Scripted runs and unattended
soaks are for testing optimizations and never count toward a rating.

## The scores

Each part of the experience gets a score from 0 to 100. The same scale applies to every game.

| What | Weight | 0 means | 100 means |
|---|---|---|---|
| Frame rate | 22 | under 20 frames a second (1% low under 10) | 55 or more (1% low 45 or more), or the game holds its own cap |
| Smoothness | 18 | 5 s of stutter a minute, or a freeze over 5 s | barely any stutter (0.1 s a minute), no frame over a quarter second |
| Sound | 12 | nothing heard, or the sound stops working | effects and music heard, no dropouts you could hear |
| Picture | 12 | garbled, or several kinds of fault | the same as on a PC |
| Stability | 12 | crashes in most sessions | no crashes, quits cleanly back to the desktop |
| Controls | 10 | nothing works with the controller | everything tested works with the controller |
| Display | 6 | cropped or off-screen | full screen, right shape, readable, as shipped |
| Features | 5 | everything tested is broken | menus, modes, saves and settings work |
| Load | 3 | two minutes of waiting | 15 seconds (starting the game, plus from "start" to play; your time in its menus doesn't count) |

**Picture** starts at 100 and loses a fixed amount for each kind of fault, counted once however
often it shows:

| Fault | Points |
|---|---|
| garbage textures or geometry | 60 |
| something missing: the HUD, models, shadows, an effect | 25 |
| wrong colours or blending | 25 |
| flicker, z-fighting, one-frame glitches | 15 |
| lighting or shading differs from the PC | 10 |
| softer or less detailed because of Walnut's own lighter settings (the page says which) | 5 |

**Display** starts from how the game's window covers the screen (full screen 100, a smaller window
30 or 10, cropped 0) and loses:

| Problem | Points |
|---|---|
| stretched or squashed out of shape | 30 |
| text smaller than 18 pixels on a 1080p TV | 15 |
| the desktop's mouse pointer sits over the game | 10 |

**Controls** and **Features** are checklists: the gamepad works in play without setup, the menus
work with it, a gamepad turned on later is found, extra gamepads join as players, nothing needs the
keyboard, keyboard and mouse work; every menu and mode works, saves load, online play works. The
score is the share of the tested items that pass. A game whose gamepad doesn't work without setup
scores 30 at most on Controls. A game made for keyboard and mouse is scored on those alone; how it
does with a gamepad shows as a badge.

A few more things worth knowing:

- **Resolution doesn't count against a game.** A pixel-art game drawing a small picture that is
  scaled up to the TV is fine, and black bars around the right shape cost nothing.
- **A game capped by design** (a classic shooter locked at 35 frames a second, a game that runs at
  30) gets full marks for holding its cap.
- **Sound is two halves.** Half is whether the right sounds play: the player says whether they
  heard the effects and the music. The other half is how clean they are. The recorder measures
  every moment the sound output runs dry: how long it lasted and how loud the sound was around it.
  Only the ones you could hear count: a gap of 20 ms or more with sound before or after it.
- **A stutter** is a frame that takes far longer than the others (over 50 ms, and three times the
  usual frame). The score adds up the time those frames lost each minute.
- **A clean quit** means the game exits normally and the desktop is back within 10 seconds. Having to
  force a game closed, or a desktop that doesn't come back, counts like a crash.
- **Stability needs hours.** A game that never crashed starts at 90, and reaches 100 after three
  hours of play without a crash. With crashes, the score follows how often they happen. The page
  shows the hours behind it ("0 crashes in 1.7 h").
- **No medal without the frame rate and smoothness.** If those couldn't be measured (a software
  renderer, for one), the game is shown as played but not rated.
- **Sessions measured the same way are combined**, the middle value of each score, so one lucky or
  unlucky run doesn't decide a medal. The first 30 seconds of play are a warm-up and aren't measured.
- **The rating is for the newest version.** Each test notes the Walnut release and the game's
  version it ran on. The medal comes from the newest of them, and a game's History table shows how
  it rated on every version before, so you can see a game get better (or worse) between releases.

## The rules {#rules}

The rules change as we learn, and a rating names the rules that made it. A change of weights or
thresholds re-scores every test at once. A new kind of check needs data older tests don't have, so
those tests keep the rules they were measured under until the game is played again.

### Rules 2.2, 2026-10-26 {#rules-2}

The current rules: everything on this page. Rules 2.2 is a small fix: a game whose only played
session crashed late is Rust, not Scrap, until a second session shows whether the crash comes back. A crash
at startup or in the first few minutes is still Scrap.

### Rules 2.1, 2026-10-26

Rules 2.1 changed what the medals mean and nothing else:
the scores are the same as in 2.0, and every test was re-scored. A game with something core broken
used to be Scrap; now it is Rust, because you can still play it. Scrap is kept for games you can't
play at all, and Tin is gone.

### Rules 1.0, 2026-10-24 {#rules-1}

Older ratings use the 2026-10-24 rules: seven scores (frame rate 25, smoothness 20, sound 15, picture
15, stability 15, display 5, load 5), no Controls or Features, the picture judged right or wrong as a
whole, the display from the window's size alone, the tester's own settings, and a weakest-link cap
(a medal at most one level above the weakest score) in place of the broken rule. Their medals go one
step further down: {{< medal "Tin" >}} (playable, but rough) for an average of 25 to 35, Rust (barely
playable) for 15 to 25, and Scrap under 15, or for a game that doesn't start, crashes in most
sessions, or runs under 15 frames a second.

## The medals

The top four medals come from the weighted average of the scores.

| Medal | What it means | When |
|---|---|---|
| {{< medal "Platinum" >}} | Awesome | an average of 90 or more |
| {{< medal "Gold" >}} | Playable, but not quite ideal | 75 or more |
| {{< medal "Silver" >}} | Playable, with some rough edges, but not bad | 55 or more |
| {{< medal "Bronze" >}} | At the limit of playable | 35 or more |
| {{< medal "Rust" >}} | You can play it, but part of it is broken: no sound or bad sound, a very low frame rate, broken textures, big stutters, the odd crash | something core is broken, or an average under 35 |
| {{< medal "Scrap" >}} | You can't play it | it won't start, it keeps crashing, or it runs under 5 frames a second |
| {{< medal "Untested" >}} | Nobody has played it on Walnut yet | |

**An average, unless something is broken.** The average gives leeway: a game that is perfect except
for running at 50 frames a second loses a few points, not a medal, and a weak spot costs only its
share. But a game where a core part is broken is Rust however fast it runs, with its average shown
next to it ("Rust · 82") and what's broken: garbled textures or geometry, no sound effects, under 20
frames a second, freezes over 5 seconds or 5 seconds of stutter a minute, or a crash within an hour
of play. Controls, features, display and load never make a game Rust or Scrap: they only count in
the average.

**Scrap means you can't play it.** A game is Scrap when it doesn't start, crashes at startup or
before you get to play, crashes in more than half of its sessions, crashes within the first 5
minutes of play in half of its sessions or more, or runs under 5 frames a second. A crash now and
then, say once 12 minutes into one of two sessions, is Rust, and the crash is on the game's page.

**Games with more than one launcher.** Some games have two entries in the menu, like OpenArena
with its GL 1.2 and GL 2.0 renderers. Each is played and rated on its own, and the game's medal is
the best one's, with its name ("Gold, with OpenArena (GL 1.2)"). The page lists them all.

**Badges** sit next to the medal and don't change it: how well a controller works (from the Controls checklist), whether the game
needs your own game files (we never ship those), no sound and low memory. A game we changed to run
better on the PS3 lists those changes on its page, with the packages to download.

## Where the numbers come from

Everything here is built from the
[Walnut repository](https://github.com/nanobyte-dev/walnut-linux): each game is a folder with its
test reports and screenshots, and the scoring is a small program anyone can read
([tools/gamedb](https://github.com/nanobyte-dev/walnut-linux/tree/main/tools/gamedb)). The full
rules, with every threshold, are in
[docs/GAMEDB.md](https://github.com/nanobyte-dev/walnut-linux/blob/main/docs/GAMEDB.md).
