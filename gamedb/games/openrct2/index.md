+++
id = "openrct2"
title = "OpenRCT2"
summary = "An open source re-implementation of RollerCoaster Tycoon 2: build a theme park, its rides and coasters, and keep the guests happy."
categories = ["simulation", "strategy"]
tags = ["management", "tycoon", "theme park", "single player"]
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
Walnut doesn't offer it: Walnut Games makes a package of it from your own copy (below). Nobody has played it on Walnut
yet. In the first benchmark (Electric Fields at 960x540, scaled 2x) it drew 21 to 24 frames a second, with
the console's memory nearly all in use.

## Setting up the game data

Walnut Games makes the data for you. Open RollerCoaster Tycoon 2 in the app and choose Add game files...; the chooser lists the disc in the drive, any mounted disc or USB stick, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder, or you pick a file or folder yourself. In a terminal, the same thing is `walnut-data import openrct2 /path/to/your/copy` (an ISO is mounted read-only by the desktop for the import). It runs Walnut's patched `game-data-packager`, which checks the files against the checksums of the editions it knows, and installs the result as an ordinary package (roller-coaster-tycoon2-data) that Remove data takes away again. OpenRCT2 appears in Play once the data is there.

What it needs: RollerCoaster Tycoon 2: its CD, an installed copy (a Steam or GOG folder) or the GOG installer. The CD works in the drive or as an ISO.

The Wacky Worlds and Time Twister expansions are optional. They come with the Triple Thrill Pack (its CD, a Steam or GOG folder, or either GOG installer, `setup_rollercoaster_tycoon2_2.0.0.6.exe` or `setup_rollercoaster_tycoon_2_triple_thrill_pack_2.01.043_(17987).exe`), and the same import installs them as roller-coaster-tycoon2-wacky-worlds-time-twister-data. Their scenarios then get their own tabs in OpenRCT2's scenario list. The original 2002 CD has only the base game.

If you have Walnut's licensed suite as an apt source, the page offers Install from Walnut licensed instead (`walnut-data install-repo openrct2`). Debian's `game-data-packager` makes the same package on a PC, and `sudo apt install ./file.deb` installs it.
