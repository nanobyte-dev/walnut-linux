+++
id = "openrct2"
title = "OpenRCT2"
summary = "An open source re-implementation of RollerCoaster Tycoon 2: build a theme park, its rides and coasters, and keep the guests happy."
tags = ["simulation", "management", "single-player", "2d", "remake", "theme-park", "tycoon"]
license = "GPL-3.0-or-later engine, commercial game data"
homepage = "https://openrct2.io/"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "In Walnut Games, open RollerCoaster Tycoon 2 and choose Add game files...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import openrct2 /path/to/your/copy`.",
  "What it needs: RollerCoaster Tycoon 2: its CD, an installed copy (a Steam or GOG folder) or the GOG installer. The CD works in the drive or as an ISO.",
  "The data is installed as roller-coaster-tycoon2-data, in `/usr/share/games/roller-coaster-tycoon2`; RollerCoaster Tycoon 2 then appears in Play. Remove data in the game's page takes it away again.",
  "Optional: the Wacky Worlds and Time Twister expansions, which come with the Triple Thrill Pack (its CD, a Steam or GOG folder, or either GOG installer). The same import adds them as roller-coaster-tycoon2-wacky-worlds-time-twister-data, and their scenarios show up under their own tabs in the scenario list. A 2002 CD without the expansions gives only the base game.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo openrct2`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

OpenRCT2 0.5.6 with Walnut's big-endian port (upstream builds only for little-endian CPUs), on its software
renderer: its OpenGL renderer needs OpenGL 3.3. The engine is free; RollerCoaster Tycoon 2's data is not, so
Walnut doesn't offer it: Walnut Games makes a package of it from your own copy (the Overview's Installation, and the Guides). Nobody has played it on Walnut
yet. In the first benchmark (Electric Fields at 960x540, scaled 2x) it drew 21 to 24 frames a second, with
the console's memory nearly all in use.
