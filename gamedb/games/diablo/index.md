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
status = "tried"
status_note = "Played session 2026-10-29 on our package (ps3.931~dev, 21.5 min of play, quit): Gold 80 (provisional, under an hour), 59 fps median, 1% low 24, one 2.3 s freeze, fullscreen 1920x1080, sound and music heard, 3 audible dropouts, DualShock and mouse worked, no glitches but a semitransparent mouse cursor (the player's note); the smoke runs of 2026-10-28 gave 58 fps in town (technical.md)."
+++
