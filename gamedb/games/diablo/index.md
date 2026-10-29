+++
id = "diablo"
title = "Diablo (DevilutionX)"
summary = "A dark fantasy action RPG: descend through the dungeon beneath Tristram, kill monsters and collect loot (needs your own game files)."
tags = ["action", "fantasy", "single-player", "online-multiplayer"]
homepage = "https://github.com/diasurgical/devilutionX"
license = "Sustainable-Use-1.0 (non-free: non-commercial use only)"
controller = "full"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "DevilutionX is only the engine: the game's data is yours (Blizzard's), Walnut does not ship it. In Walnut Games, open Diablo and choose Add game files...: pick the disc in the drive, an ISO, GOG's installer or a folder that holds your copy.",
  "Or from a terminal: `walnut-data import diablo /path/to/your/copy`. What it needs: DIABDAT.MPQ from Diablo (the CD, GOG, or an installed copy). Hellfire's hf*.mpq files are optional (Also add).",
  "The data is installed as diablo-data; DevilutionX reads DIABDAT.MPQ from its data folder, and Diablo then appears in Play.",
  "The free shareware version's spawn.mpq also works: put it in `~/.local/share/diasurgical/devilution/` by hand.",
]
known_issues = [
  "The mouse cursor draws semitransparent instead of solid.",
  "One pause of about 2 s in a 20-minute session, and a few short sound dropouts.",
]
+++

Diablo runs through DevilutionX 1.5.5, a source-available engine rebuilt from the original game, with the player's own DIABDAT.MPQ (the Guides tab has the ways to add it). A played session held about 59 frames a second (1% low 24) at 1920x1080, with the DualShock through the menus and the dungeon. DevilutionX's licence allows free non-commercial use and sharing, but it isn't an open-source licence.
