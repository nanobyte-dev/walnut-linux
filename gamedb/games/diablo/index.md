+++
id = "diablo"
title = "Diablo (DevilutionX)"
homepage = "https://github.com/diasurgical/devilutionX"
license = "Sustainable-Use-1.0 (non-free: non-commercial use only)"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "DevilutionX is only the engine: the game's data is yours (Blizzard's), Walnut does not ship it. In Walnut Games, open Diablo and choose Add game files...: pick the disc in the drive, an ISO, GOG's installer or a folder that holds your copy.",
  "Or from a terminal: `walnut-data import diablo /path/to/your/copy`. What it needs: DIABDAT.MPQ from Diablo (the CD, GOG, or an installed copy). Hellfire's hf*.mpq files are optional (Also add).",
  "The data is installed as diablo-data; DevilutionX reads DIABDAT.MPQ from its data folder, and Diablo then appears in Play.",
  "The free shareware version's spawn.mpq also works: put it in `~/.local/share/diasurgical/devilution/` by hand.",
]
draft = true
status = "tried"
status_note = "Smoke runs on the PS3 on 2026-10-28 with a DevilutionX 1.5.5 build of ours (licence decided on 2026-10-29: the Sustainable Use License 1.0, non-commercial use, is accepted; published in the testing suite as non-free) and the player's own Diablo 1.09 DIABDAT.MPQ: a new Warrior in Tristram, fullscreen 1920x1080, 58 fps steady (1% low 25), the colours right, the hero saved (technical.md); no played session yet."
+++
