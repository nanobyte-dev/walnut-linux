+++
id = "monkey-island-2"
title = "Monkey Island 2: LeChuck's Revenge"
summary = "The 1991 sequel: Guybrush hunts for the treasure of Big Whoop while the zombie pirate LeChuck hunts for him. Played with ScummVM from your own copy."
tags = ["point-and-click", "fantasy", "single-player", "2d"]
license = "GPL-3.0-or-later engine (ScummVM), commercial game data"
license_source = "ScummVM's COPYING (GPL version 3, github.com/scummvm/scummvm; Debian's debian/copyright of scummvm 2026.4.0+dfsg-1 still says GPL-2+); the game data is the player's own copy"
homepage = "https://wiki.scummvm.org/index.php?title=Monkey_Island_2:_LeChuck%27s_Revenge"
data_guide = "https://wiki.scummvm.org/index.php?title=Monkey_Island_2:_LeChuck%27s_Revenge"
data_steps = [
  "In Walnut Games, open Monkey Island 2: LeChuck's Revenge and choose Add game files...: pick the disc in the drive, an ISO or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import monkey-island-2 /path/to/your/copy`.",
  "What it needs: MONKEY2.000 and MONKEY2.001 from the DOS floppy release, in English, German, Spanish, French or Italian (installed from the floppies). The Special Edition sold on Steam and GOG is not supported.",
  "Each edition and language is a package of its own (the Spanish release becomes monkey-island2-es-data, in `/usr/share/games/monkey-island2-es`); Monkey Island 2: LeChuck's Revenge then appears in the menu and in Play, and starts the game directly. Remove data in the game's page takes it away again.",
]
controller = "full"
designed_for = "keyboard-mouse"
known_issues = [
  "The Spanish floppy release starts with its copy-protection screen, the voodoo recipe wheel: ScummVM doesn't skip it for this release, so the answer comes from the wheel in the box.",
]
+++

Monkey Island 2 runs on ScummVM, Debian's package, from your own copy: Walnut Games makes a package
of its files (the Overview's Installation, and the Guides), and the menu entry starts the game directly,
fullscreen with the picture between bars. A smoke run on the PS3 with the Spanish floppy release
stopped at the game's copy-protection screen, drawn right. Nobody has played a session of it on Walnut
yet.
