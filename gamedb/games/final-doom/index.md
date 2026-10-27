+++
id = "final-doom"
title = "Final Doom"
summary = "Two 32-level Doom II episodes from 1996, TNT: Evilution and The Plutonia Experiment, played with DSDA-Doom from your own copy."
categories = ["shooter"]
tags = ["fps", "classic", "doom", "single player"]
license = "GPL-2.0-or-later engine, commercial game data"
homepage = "https://doomwiki.org/wiki/Final_Doom"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "Turn on Debian's contrib section once, where the tool that packs game data lives: `printf 'Types: deb\\nURIs: http://deb.debian.org/debian-ports\\nSuites: unstable unreleased\\nComponents: contrib\\n' | sudo tee /etc/apt/sources.list.d/debian-contrib.sources`.",
  "Install the tool: `sudo apt update && sudo apt install game-data-packager` (and `innoextract` too when your copy is a GOG installer, `setup_*.exe`).",
  "Make a package from your own copy, the folder (or the CD's mount point) that holds `PLUTONIA.WAD` and `TNT.WAD`: `mkdir -p ~/game-data && /usr/games/game-data-packager -d ~/game-data final-doom --package plutonia-wad --package tnt-wad /path/to/FinalDoom`.",
  "Install it: `sudo apt install ~/game-data/plutonia-wad_*_all.deb ~/game-data/tnt-wad_*_all.deb`. The WADs land in `/usr/share/games/doom/plutonia.wad` and `/usr/share/games/doom/tnt.wad`.",
  "Start it from a terminal: `ps3-game dsda-doom -iwad /usr/share/games/doom/plutonia.wad -vidmode gl -width 1920 -height 1080 -fullscreen`; `ps3-game dsda-doom -iwad /usr/share/games/doom/tnt.wad -file /usr/share/games/doom/tnt31.wad -vidmode gl -width 1920 -height 1080 -fullscreen`. The menu entries Final Doom: The Plutonia Experiment (DSDA-Doom) and Final Doom: TNT: Evilution (DSDA-Doom) stay hidden until Walnut's menu learns to look for the WAD (it comes with the data import).",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Final Doom runs on DSDA-Doom, the Doom port Walnut packages and patches for the PS3 (big-endian sound
lumps, textures in formats the RSX has, batched GL draws). The engine is free; the game's data is not, so
Walnut doesn't offer it: you make a package of it from your own copy (below). Nobody has played it on
Walnut yet. DSDA-Doom with Freedoom runs at about 56 frames a second on its OpenGL renderer; with this
game's WADs it has not been tried.

## Setting up the game data

The tool is Debian's `game-data-packager`, from Debian's contrib section, which Walnut's package sources
don't list: step 1 adds it as a source of its own, so Walnut's updates leave it alone. It checks the files
against the checksums of the editions it knows and makes an ordinary `.deb`, which apt installs and can
remove again. On a Debian or Ubuntu PC the same commands work, and you can copy the `.deb` to the PS3 and
install it with `sudo apt install ./file.deb`.

Which copies work: Final Doom (either WAD, or both). A Steam copy is the `common/Final Doom` folder of its library; a GOG copy is the installed folder or its installer. For TNT the tool also downloads Team TNT's fixed MAP31 (`tnt31fix.zip`, about 120 KB) from the idgames archive, unless the folder already has it.

The tool searches your home folder and the usual Steam and GOG places by itself; add `--no-search` to keep
it to the folder you name. Coming: Walnut Games will do the steps for you (a button on this page, and
`walnut-data import final-doom /path/to/FinalDoom` in a terminal).
