+++
id = "loom"
title = "Loom"
summary = "Lucasfilm Games' 1990 fantasy adventure: Bobbin Threadbare weaves spells as four-note drafts on his distaff. Played with ScummVM from your own copy."
tags = ["point-and-click", "fantasy", "single-player", "2d"]
license = "GPL-3.0-or-later engine (ScummVM), commercial game data"
license_source = "ScummVM's COPYING (GPL version 3, github.com/scummvm/scummvm; Debian's debian/copyright of scummvm 2026.4.0+dfsg-1 still says GPL-2+); the game data is the player's own copy"
homepage = "https://wiki.scummvm.org/index.php?title=Loom"
data_guide = "https://wiki.scummvm.org/index.php?title=Loom"
data_steps = [
  "In Walnut Games, open Loom and choose Add game files...: pick the disc in the drive, an ISO or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import loom /path/to/your/copy`.",
  "What it needs: the EGA floppy release's 00.LFL to 99.LFL (English, German, French or Spanish). The VGA talkie (the CD, Steam, GOG) can't be added yet: its music is a CD audio track.",
  "Each edition and language is a package of its own (the Spanish EGA release becomes loom-ega-es-data, in `/usr/share/games/loom-ega-es`); Loom then appears in the menu and in Play, and starts the game directly. Remove data in the game's page takes it away again.",
]
controller = "full"
designed_for = "keyboard-mouse"
known_issues = [
  "The VGA talkie (CD, Steam, GOG) can't be imported yet because its music is a CD audio track; only the EGA floppy releases work.",
  "Nobody has played a session of it on Walnut yet, only smoke-tested. The Spanish EGA release reached the difficulty choice.",
]
+++

Loom runs on ScummVM, Debian's package, from your own copy: Walnut Games makes a package of its files
(the Overview's Installation, and the Guides), and the menu entry starts the game directly, fullscreen
with the picture between bars. Walnut's game-data-packager knows the Spanish EGA release, which Debian's
doesn't. A smoke run on the PS3 with that release reached the difficulty choice, drawn right. Nobody has
played a session of it on Walnut yet.
