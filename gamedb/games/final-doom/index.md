+++
id = "final-doom"
title = "Final Doom"
summary = "Two 32-level Doom II episodes from 1996, TNT: Evilution and The Plutonia Experiment, played with DSDA-Doom from your own copy."
tags = ["fps", "sci-fi", "horror", "single-player", "first-person", "doom-engine"]
license = "GPL-2.0-or-later engine, commercial game data"
homepage = "https://doomwiki.org/wiki/Final_Doom"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "In Walnut Games, open Final Doom and choose Add game files...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal, with the same sources: `walnut-data import final-doom /path/to/your/copy`.",
  "What it needs: PLUTONIA.WAD and TNT.WAD (either, or both), from a CD, an installed copy (Steam: `common/Final Doom`), or the GOG installer. For TNT the import also downloads Team TNT's fixed MAP31 (`tnt31fix.zip`, about 120 KB).",
  "The data is installed as plutonia-wad and tnt-wad, in `/usr/share/games/doom`; Final Doom then appears in Play. Remove data in the game's page takes it away again.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo final-doom`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Final Doom runs on DSDA-Doom, the Doom port Walnut packages and patches for the PS3 (big-endian sound
lumps, textures in formats the RSX has, batched GL draws). The engine is free; the game's data is not, so
Walnut doesn't offer it: Walnut Games makes a package of it from your own copy (the Overview's Installation, and the Guides). Nobody has played it on
Walnut yet. DSDA-Doom with Freedoom runs at about 56 frames a second on its OpenGL renderer; with this
game's WADs it has not been tried.
