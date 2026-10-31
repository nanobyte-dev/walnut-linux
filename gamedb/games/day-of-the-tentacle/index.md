+++
id = "day-of-the-tentacle"
title = "Day of the Tentacle"
summary = "LucasArts' 1993 time-travel comedy: Bernard, Hoagie and Laverne pass things to each other across three centuries to stop Purple Tentacle. Played with ScummVM from your own copy."
tags = ["point-and-click", "sci-fi", "single-player", "2d"]
license = "GPL-3.0-or-later engine (ScummVM), commercial game data"
license_source = "ScummVM's COPYING (GPL version 3, github.com/scummvm/scummvm; Debian's debian/copyright of scummvm 2026.4.0+dfsg-1 still says GPL-2+); the game data is the player's own copy"
homepage = "https://wiki.scummvm.org/index.php?title=Day_of_the_Tentacle"
data_guide = "https://wiki.scummvm.org/index.php?title=Day_of_the_Tentacle"
data_steps = [
  "In Walnut Games, open Day of the Tentacle and choose Add game files...: pick the disc in the drive, an ISO or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import day-of-the-tentacle /path/to/your/copy`.",
  "What it needs: TENTACLE.000, TENTACLE.001 and MONSTER.SOU from the CD (the talkie) or the floppy release, in English, German, Spanish, French or Italian. Day of the Tentacle Remastered's files don't work.",
  "Each edition and language is a package of its own (the Spanish floppy release becomes dayofthetentacle-floppy-es-data, in `/usr/share/games/dayofthetentacle-floppy-es`); Day of the Tentacle then appears in the menu and in Play, and starts the game directly. Remove data in the game's page takes it away again.",
]
controller = "full"
designed_for = "keyboard-mouse"
+++

Day of the Tentacle runs on ScummVM, Debian's package, from your own copy: Walnut Games makes a
package of its files (the Overview's Installation, and the Guides), and the menu entry starts the game
directly, fullscreen at 1920x1080 with the 4:3 picture between bars. A smoke run on the PS3 with the
Spanish floppy release went past the intro into the hotel lobby with the colours right and the music
playing. Nobody has played a session of it on Walnut yet.
