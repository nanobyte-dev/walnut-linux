+++
id = "hexen"
title = "Hexen: Beyond Heretic"
summary = "Raven Software's 1995 sequel to Heretic, with three classes and hub levels, played with DSDA-Doom from your own copy."
categories = ["shooter"]
tags = ["fps", "classic", "fantasy", "single player"]
license = "GPL-2.0-or-later engine, commercial game data"
homepage = "https://doomwiki.org/wiki/Hexen"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "In Walnut Games, open Hexen and choose Add game files...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import hexen /path/to/your/copy`.",
  "What it needs: HEXEN.WAD from Hexen: Beyond Heretic 1.1: a CD, an installed copy (Steam: `common/Hexen`) or GOG's installer (`setup_hexen_beyond_heretic_1.1_(42801).exe`). Version 1.0 does not run and is refused; the Macintosh CD's WAD is not taken either.",
  "Deathkings of the Dark Citadel, if you have it: HEXDD.WAD from its CD or an installed copy (Steam: `common/Hexen Deathkings of the Dark Citadel`), in the same import or later.",
  "The data is installed as hexen-wad, in `/usr/share/games/doom/hexen.wad` (Deathkings as hexen-deathkings-wad, `hexdd.wad`); Hexen, and Deathkings when its WAD is there, then appear in Play. Remove data in the game's page takes it away again.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo hexen`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Hexen: Beyond Heretic runs on DSDA-Doom, the Doom port Walnut packages and patches for the PS3 (big-endian sound
lumps, textures in formats the RSX has, batched GL draws). The engine is free; the game's data is not, so
Walnut doesn't offer it: Walnut Games makes a package of it from your own copy (below). Imported from GOG's
installer on the PS3, it reaches the title screen and plays its demo; nobody has played it on Walnut yet,
and its speed hasn't been measured (DSDA-Doom with Freedoom runs at about 56 frames a second on its OpenGL
renderer).

## Setting up the game data

Walnut Games makes the data for you. Open Hexen in the app and choose Add game files...; the chooser lists the disc in the drive, any mounted disc or USB stick, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder, or you pick a file or folder yourself. In a terminal, the same thing is `walnut-data import hexen /path/to/your/copy` (an ISO is mounted read-only by the desktop for the import). It runs Walnut's patched `game-data-packager`, which checks the files against the checksums of the editions it knows, and installs the result as an ordinary package (hexen-wad) that Remove data takes away again. Hexen (DSDA-Doom) appears in Play once the data is there.

What it needs: HEXEN.WAD from Hexen: Beyond Heretic 1.1: a CD, an installed copy (Steam: `common/Hexen`) or GOG's installer (`setup_hexen_beyond_heretic_1.1_(42801).exe`, which has no Deathkings). Version 1.0 (the floppies and the first CD) does not run and is refused; Raven's update to 1.1 isn't applied for you yet. The Macintosh CD's Hexen.WAD isn't taken either: it lacks the music instrument lumps DSDA-Doom wants at start.

Deathkings of the Dark Citadel, the expansion, is optional: HEXDD.WAD from its CD or an installed copy (Steam: `common/Hexen Deathkings of the Dark Citadel`). It gets its own entry in Play.

If you have Walnut's licensed suite as an apt source, the page offers Install from Walnut licensed instead (`walnut-data install-repo hexen`). Debian's `game-data-packager` makes the same package on a PC, and `sudo apt install ./file.deb` installs it.
