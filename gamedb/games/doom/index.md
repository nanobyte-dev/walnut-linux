+++
id = "doom"
title = "Doom"
summary = "id Software's 1993 first person shooter, The Ultimate Doom's four episodes, played with DSDA-Doom from your own copy."
tags = ["fps", "sci-fi", "horror", "single-player", "first-person"]
license = "GPL-2.0-or-later engine, commercial game data"
homepage = "https://doomwiki.org/wiki/Doom"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "In Walnut Games, open Doom and choose Add game files...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import doom /path/to/your/copy`.",
  "What it needs: DOOM.WAD from Doom 1.9 or The Ultimate Doom, from a CD, an installed copy (Steam: the `common/Ultimate Doom` folder), or the GOG installer.",
  "The data is installed as doom-wad, in `/usr/share/games/doom/doom.wad`; Doom then appears in Play. Remove data in the game's page takes it away again.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo doom`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Doom runs on DSDA-Doom, the Doom port Walnut packages and patches for the PS3 (big-endian sound
lumps, textures in formats the RSX has, batched GL draws). The engine is free; the game's data is not, so
Walnut doesn't offer it: Walnut Games makes a package of it from your own copy (the Overview's Installation, and the Guides). Nobody has played it on
Walnut yet. DSDA-Doom with Freedoom runs at about 56 frames a second on its OpenGL renderer; with this
game's WAD it has not been tried.
