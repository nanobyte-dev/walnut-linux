+++
title = "About the ratings"
summary = "How Walnut Games tests a game on the PS3 and turns the measurements into a medal."
+++

Every game here was installed on a PlayStation 3 running Walnut and played. What you see on a
game's page is what we measured, not an impression: the frame rate, the stutters, the sound, the
picture next to the same game on a PC, crashes, and how long it takes to load. The medal sums that
up; the numbers behind it are always on the page.

## How a game is tested

**A played session.** Someone plays the game for about ten minutes, the way you would: started from
the Walnut desktop, with the settings Walnut ships, on a 1080p TV. Players hit menus, saves, level
changes and odd inputs that a scripted test never reaches. While they play, a recorder notes every
frame's time, the sound faults, the memory, how the game ended, and a screenshot about once a
minute. At the end the player says whether they heard the sound and music, how the controls worked,
and writes down anything odd; those notes become the game's known issues.

**A side-by-side picture check.** A script drives the game to a few fixed moments (a menu, a map, a
camera position) on the PS3 and on a PC, and the two are put next to each other and looked at:
missing textures, wrong colours, text that isn't drawn.

Every session adds to a game's play time, so its rating firms up as more people play it.

## The seven scores

Each part of the experience gets a score from 0 to 100. The same scale applies to every game.

| What | Weight | 0 means | 100 means |
|---|---|---|---|
| Frame rate | 25 | under 20 frames a second (1% low under 10) | 55 or more (1% low 45 or more), or the game holds its own cap |
| Smoothness | 20 | 5 s of stutter a minute, or a freeze over 5 s | barely any stutter (0.1 s a minute), no frame over a quarter second |
| Sound | 15 | silent, or the sound stops working | sound and music clean, no glitches |
| Picture | 15 | wrong, can't be played | the same as on a PC |
| Stability | 15 | crashes in most sessions | no crashes, quits cleanly |
| Display | 5 | cropped or off-screen | full screen, right shape, cleanly scaled |
| Load | 5 | two minutes to get playing | 15 seconds |

Between those ends the scores rise in steps. A few things worth knowing:

- **Resolution doesn't count against a game.** A pixel-art game drawing a small picture that is
  scaled up to the TV is fine, and black bars around the right shape cost nothing.
- **A game capped by design** (a classic shooter locked at 35 frames a second, a game that runs at
  30) gets full marks for holding its cap.
- **A stutter** is a frame that takes far longer than the others (over 50 ms, and three times the
  usual frame). The score adds up the time those frames lost each minute.
- **Stability needs hours.** A game that never crashed starts at 90, and reaches 100 after three
  hours of play without a crash. With crashes, the score follows how often they happen. The page
  shows the hours behind it ("0 crashes in 1.7 h").
- **What can't be measured is left out.** If a game gives us no frame times or no clear moment when
  play starts, that score is left out and its weight goes to the others; the page says so.
- **Scores are the middle of the sessions**, so one unlucky or lucky run doesn't move a medal.
- **The rating is for the newest version.** Each test notes the Walnut release and the game's
  version it ran on. The medal comes from the newest of them, and a game's History table shows how
  it rated on every version before, so you can see a game get better (or worse) between releases.

## The medals

The medal comes from the weighted average of the seven scores.

| Medal | What it means | Average |
|---|---|---|
| {{< medal "Platinum" >}} | Plays like on a PC | 90 or more |
| {{< medal "Gold" >}} | Plays well | 75 or more |
| {{< medal "Silver" >}} | Good, with compromises | 55 or more |
| {{< medal "Bronze" >}} | Works, with clear problems | 35 or more |
| {{< medal "Tin" >}} | Playable, but rough | 25 or more |
| {{< medal "Rust" >}} | Barely playable | 15 or more |
| {{< medal "Scrap" >}} | Doesn't work: won't start, crashes in most sessions, or far too slow | under 15 |
| {{< medal "Untested" >}} | Nobody has played it on Walnut yet | |

**An average, with a weakest link.** The average gives some leeway: a game that is perfect except
for running at 50 frames a second loses a few points, not a medal. But a game can be at most one
medal above its weakest score, so one real problem isn't averaged away. A game that plays
perfectly but is silent is Bronze, with a "no sound" badge. And some failures make a game Scrap
whatever its average: it doesn't start, crashes in most sessions, runs under 15 frames a second, or
stutters for more than 10 seconds a minute. The page then says which.

**Badges** sit next to the medal and don't change it: how well a controller works, whether the game
needs your own game files (we never ship those), no sound, low memory, and **patched by Walnut**
for games we changed to run better on the PS3; their pages list those changes, with the packages
to download.

## Where the numbers come from

Everything here is built from the
[Walnut repository](https://github.com/nanobyte-dev/walnut-linux): each game is a folder with its
test reports and screenshots, and the scoring is a small program anyone can read
([tools/gamedb](https://github.com/nanobyte-dev/walnut-linux/tree/main/tools/gamedb)). The full
rules, with every threshold, are in
[docs/GAMEDB.md](https://github.com/nanobyte-dev/walnut-linux/blob/main/docs/GAMEDB.md).
