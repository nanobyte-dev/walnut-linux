+++
id = "hexen"
title = "Hexen: Beyond Heretic"
summary = "Raven Software's 1995 sequel to Heretic, with three classes and hub levels, played with DSDA-Doom from your own copy."
tags = ["fps", "fantasy", "single-player", "first-person"]
license = "GPL-2.0-or-later engine, commercial game data"
license_source = "debian/copyright of Debian's dsda-doom 0.30.1+dfsg-1; the game data is the player's own copy"
homepage = "https://doomwiki.org/wiki/Hexen"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "In Walnut Games, open Hexen and choose Add game files...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import hexen /path/to/your/copy`.",
  "What it needs: HEXEN.WAD from Hexen: Beyond Heretic 1.1: a CD, an installed copy (Steam: `common/Hexen`) or GOG's installer (`setup_hexen_beyond_heretic_1.1_(42801).exe`). Version 1.0 does not run and is refused. The Macintosh CD's Hexen.WAD works too, without OPL music (our default player, fluidsynth, plays its music).",
  "Deathkings of the Dark Citadel, if you have it: HEXDD.WAD from its CD or an installed copy (Steam: `common/Hexen Deathkings of the Dark Citadel`), in the same import or later.",
  "The data is installed as hexen-wad, in `/usr/share/games/doom/hexen.wad` (Deathkings as hexen-deathkings-wad, `hexdd.wad`); Hexen, and Deathkings when its WAD is there, then appear in Play. Remove data in the game's page takes it away again.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo hexen`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Hexen: Beyond Heretic runs on DSDA-Doom, the Doom port Walnut packages and patches for the PS3 (big-endian sound
lumps, textures in formats the RSX has, batched GL draws). The engine is free; the game's data is not, so
Walnut doesn't offer it: Walnut Games makes a package of it from your own copy (the Overview's Installation, and the Guides). Imported from GOG's
installer on the PS3, it reaches the title screen and plays its demo; nobody has played it on Walnut yet,
and its speed hasn't been measured (DSDA-Doom with Freedoom runs at about 56 frames a second on its OpenGL
renderer).
