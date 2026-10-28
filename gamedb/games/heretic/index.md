+++
id = "heretic"
title = "Heretic"
summary = "Raven Software's 1994 dark fantasy shooter on the Doom engine, Shadow of the Serpent Riders' five episodes, played with DSDA-Doom from your own copy."
tags = ["fps", "fantasy", "single-player", "first-person", "doom-engine"]
license = "GPL-2.0-or-later engine, commercial game data"
homepage = "https://doomwiki.org/wiki/Heretic"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "In Walnut Games, open Heretic and choose Add game files...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import heretic /path/to/your/copy`.",
  "What it needs: HERETIC.WAD from Heretic: Shadow of the Serpent Riders (1.3) or the original 1.0 release (three episodes), from a CD, an installed copy (Steam: `common/Heretic Shadow of the Serpent Riders`), or the GOG installer.",
  "The data is installed as heretic-wad, in `/usr/share/games/doom/heretic.wad`; Heretic then appears in Play. Remove data in the game's page takes it away again.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo heretic`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Heretic runs on DSDA-Doom, the Doom port Walnut packages and patches for the PS3 (big-endian sound
lumps, textures in formats the RSX has, batched GL draws). The engine is free; the game's data is not, so
Walnut doesn't offer it: Walnut Games makes a package of it from your own copy (the Overview's Installation, and the Guides). Nobody has played it on
Walnut yet. DSDA-Doom with Freedoom runs at about 56 frames a second on its OpenGL renderer; with this
game's WAD it has not been tried.
