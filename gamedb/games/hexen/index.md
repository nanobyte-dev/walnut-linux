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
  "Turn on Debian's contrib section once, where the tool that packs game data lives: `printf 'Types: deb\\nURIs: http://deb.debian.org/debian-ports\\nSuites: unstable unreleased\\nComponents: contrib\\n' | sudo tee /etc/apt/sources.list.d/debian-contrib.sources`.",
  "Install the tool: `sudo apt update && sudo apt install game-data-packager` (and `innoextract` too when your copy is a GOG installer, `setup_*.exe`).",
  "Make a package from your own copy, the folder (or the CD's mount point) that holds `HEXEN.WAD`: `mkdir -p ~/game-data && /usr/games/game-data-packager -d ~/game-data hexen --package hexen-wad /path/to/Hexen`.",
  "Install it: `sudo apt install ~/game-data/hexen-wad_*_all.deb`. The WAD lands in `/usr/share/games/doom/hexen.wad`.",
  "Start it from a terminal: `ps3-game dsda-hexen -iwad /usr/share/games/doom/hexen.wad -vidmode gl -width 1920 -height 1080 -fullscreen`. The menu entry Hexen (DSDA-Doom) stays hidden until Walnut's menu learns to look for the WAD (it comes with the data import).",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Hexen: Beyond Heretic runs on DSDA-Doom, the Doom port Walnut packages and patches for the PS3 (big-endian sound
lumps, textures in formats the RSX has, batched GL draws). The engine is free; the game's data is not, so
Walnut doesn't offer it: you make a package of it from your own copy (below). Nobody has played it on
Walnut yet. DSDA-Doom with Freedoom runs at about 56 frames a second on its OpenGL renderer; with this
game's WAD it has not been tried.

## Setting up the game data

The tool is Debian's `game-data-packager`, from Debian's contrib section, which Walnut's package sources
don't list: step 1 adds it as a source of its own, so Walnut's updates leave it alone. It checks the files
against the checksums of the editions it knows and makes an ordinary `.deb`, which apt installs and can
remove again. On a Debian or Ubuntu PC the same commands work, and you can copy the `.deb` to the PS3 and
install it with `sudo apt install ./file.deb`.

Which copies work: Hexen: Beyond Heretic. A Steam copy is the `common/Hexen` folder of its library.

The tool searches your home folder and the usual Steam and GOG places by itself; add `--no-search` to keep
it to the folder you name. Coming: Walnut Games will do the steps for you (a button on this page, and
`walnut-data import hexen /path/to/Hexen` in a terminal).
