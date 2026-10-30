+++
title = "How to help"
summary = "Play a game on your Walnut PS3 with walnut-rate and send us the session: how it works, what is sent, and how you are credited."
# a draft until the user decides to show it (PLAN 5.12): Hugo leaves drafts out of the site, and no menu links here
# yet. Sessions arrive as GitHub pull requests (the user, 2026-10-30).
draft = true
+++

**Every rating on this site comes from someone playing the game on a PS3.** If you have a PS3 running
Walnut, you can be that someone. Ten minutes of play gives a game one more session, and a game with
several sessions from different consoles has a rating that rests on more than our one console.

## What you need

- A PS3 with Walnut on it, the desktop running, and a TV at 1080p.
- The rating tool: `sudo apt install walnut-rate` in a terminal.
- A game from the list below, installed (Walnut Games installs it).
- About fifteen minutes in which you don't use the console for anything else.

## Playing a session

Open a terminal on the Walnut desktop and type:

```
walnut-rate list               # the games with a test, and how many sessions each has
walnut-rate play GAME          # checks the console, then starts the game
```

`walnut-rate` first checks that nothing else is running: no browser, no second game, no SSH login. If
something is, it names it and stops (`--close` asks those programs to close). Then the game starts the
way it would for a new player: your own settings for it are put aside and come back afterwards. Play
it normally, for ten minutes or more, and quit it from its own menu. Alt+F12, or Select+Start on the
gamepad, takes a screenshot of anything odd you want us to see.

When the game is over, a short form on the terminal asks what only you can tell: did you hear the sound
and music, did anything look wrong, how did the gamepad work. It takes under a minute.

## Sending it

A session reaches us as a pull request on GitHub, to
[nanobyte-dev/walnut-linux](https://github.com/nanobyte-dev/walnut-linux). You need a GitHub account; the
pull request adds one folder, `submissions/<game>/<id>/`, and nothing else. Two ways to make it:

**From the console, with gh** (GitHub's command line, if your console has it; Debian doesn't build it for the
PS3 yet). Log in once with `gh auth login`, then:

```
walnut-rate submit             # shows what will be sent and its size, then asks
```

It asks for a nickname (or nothing: the site then says "a tester") and whether we may publish the session.
It lists every file and its size, and only on your yes it makes a fork of the repository under your account,
commits the folder there and opens the pull request. `walnut-rate status` shows its link later.

**From a PC, with a USB stick.** Without gh, `walnut-rate submit` prints these same steps:

```
walnut-rate export last /media/$USER/STICK
```

writes the folder to the stick, as `submissions/<game>/<id>/`. On a PC, sign in to GitHub, open
nanobyte-dev/walnut-linux and press **Fork**. In your fork, choose **Add file**, then **Upload files**, and drag
the stick's `submissions` folder onto the page: GitHub keeps the folders. Pick "Create a new branch", then
**Propose changes** and **Create pull request**. If you use gh or git on the PC instead, copy the `submissions`
folder into a clone of your fork, commit it on a new branch, push, and run `gh pr create`.

A check runs on the pull request within a few minutes (the first time, after we approve it to run). It
looks only at the folder: the file names and sizes, that the screenshots are pictures, that your consent is
there and that no user name, host name or address was left in. If it fails, its log says why.

**A pull request is public.** Anyone can read its files on GitHub (the logs, your answers, the screenshots),
your GitHub account name shows on it, and GitHub keeps the files even after we close it. That is why there is
no contact field: we answer on the pull request.

## What is sent

The frame times, sound dropouts, memory and temperatures the recorder measured, the screenshots, the
game's log, which controllers the game opened and when buttons were pressed (never which keys, never
anything you typed), your form, and the list of installed package versions. Before the file is written,
your user name, your full name, the console's host name and every network address of the console are
replaced in every text file, and the tool stops if anything of them is left.

## What happens next

Every session is looked at by us before it counts: the screenshots next to the same game on a PC, the
numbers, your answers. A session we accept is published on the game's Test runs tab, credited to your
nickname (or "a tester"), and counts in the game's rating like one of our own. We say so on your pull
request and close it; we never merge it, so the raw files don't end up in the repository. A session we can't
use gets a comment with the reason, and the pull request is closed too. A disturbed session (the
check saw another program start while you played) can't be rated, but it can still show us a problem.
