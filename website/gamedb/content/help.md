+++
title = "How to help"
summary = "Play a game on your Walnut PS3 with walnut-rate and send us the session: how it works, what is sent, and how you are credited."
# a draft until walnut-rate's receiver is live on walnut.nanobyte.dev (PLAN 5.12): Hugo leaves drafts out of the
# site, and no menu links here yet. The transport, the credit and the weight below are provisional.
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

```
walnut-rate submit             # shows what will be sent and its size, then asks
```

It asks for a nickname (or nothing, to stay anonymous), a way to reach you (optional, never published),
and whether we may publish the session. Then it lists every file and its size, and sends only when you
say yes. The answer is a submission id; `walnut-rate status` shows it again later. Without a network,
`walnut-rate export last /media/usb` writes the same file to a USB stick.

## What is sent

The frame times, sound dropouts, memory and temperatures the recorder measured, the screenshots, the
game's log, which controllers the game opened and when buttons were pressed (never which keys, never
anything you typed), your form, and the list of installed package versions. Before the file is written,
your user name, your full name, the console's host name and every network address of the console are
replaced in every text file, and the tool stops if anything of them is left.

## What happens next

Every session is looked at by us before it counts: the screenshots next to the same game on a PC, the
numbers, your answers. A session we accept is published on the game's Test runs tab, credited to your
nickname (or "a tester"), and counts in the game's rating like one of our own. A disturbed session (the
check saw another program start while you played) can't be rated, but it can still show us a problem.
