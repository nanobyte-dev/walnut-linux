+++
id = "the-secret-of-monkey-island"
title = "The Secret of Monkey Island"
summary = "Lucasfilm Games' 1990 pirate comedy: Guybrush Threepwood wants to be a pirate and ends up chasing the ghost LeChuck. Played with ScummVM from your own copy."
tags = ["point-and-click", "fantasy", "single-player", "2d"]
license = "GPL-3.0-or-later engine (ScummVM), commercial game data"
homepage = "https://wiki.scummvm.org/index.php?title=The_Secret_of_Monkey_Island"
data_guide = "https://wiki.scummvm.org/index.php?title=The_Secret_of_Monkey_Island"
data_steps = [
  "In Walnut Games, open The Secret of Monkey Island and choose Add game files...: pick the disc in the drive, an ISO or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import the-secret-of-monkey-island /path/to/your/copy`.",
  "What it needs: the DOS release: the CD's MONKEY.000 and MONKEY.001 (English, German, French or Italian), or the VGA floppies' 000.LFL, 901.LFL to 904.LFL and DISK*.LEC (English, German or Spanish). The Special Edition sold on Steam and GOG is not supported.",
  "Each edition and language is a package of its own (the Spanish floppy release becomes secret-of-monkey-island-floppy-es-data, in `/usr/share/games/secret-of-monkey-island-floppy-es`); The Secret of Monkey Island then appears in the menu and in Play, and starts the game directly. Remove data in the game's page takes it away again.",
]
controller = "full"
designed_for = "keyboard-mouse"
+++

The Secret of Monkey Island runs on ScummVM, Debian's package, from your own copy: Walnut Games makes
a package of its files (the Overview's Installation, and the Guides), and the menu entry starts the game
directly, fullscreen with the picture between bars. Walnut's game-data-packager knows the Spanish VGA
floppy release, which Debian's doesn't. A smoke run on the PS3 with that release reached Guybrush on
the dock at night, colours right. Nobody has played a session of it on Walnut yet.
