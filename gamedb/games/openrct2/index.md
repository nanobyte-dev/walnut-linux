+++
id = "openrct2"
title = "OpenRCT2"
summary = "An open source re-implementation of RollerCoaster Tycoon 2: build a theme park, its rides and coasters, and keep the guests happy."
categories = ["simulation", "strategy"]
tags = ["management", "tycoon", "theme park", "single player"]
license = "GPL-3.0-or-later engine, commercial game data"
homepage = "https://openrct2.io/"
data_guide = "https://wiki.debian.org/Games/GameDataPackager"
data_steps = [
  "Turn on Debian's contrib section once, where the tool that packs game data lives: `printf 'Types: deb\\nURIs: http://deb.debian.org/debian-ports\\nSuites: unstable unreleased\\nComponents: contrib\\n' | sudo tee /etc/apt/sources.list.d/debian-contrib.sources`.",
  "Install the tool and the unpackers for RCT2's CD and the GOG installer: `sudo apt update && sudo apt install game-data-packager unshield innoextract`.",
  "Make a package from your own copy of RollerCoaster Tycoon 2, the CD's mount point or the folder of an installed copy (a Steam or GOG copy's folder, or GOG's installer): `mkdir -p ~/game-data && /usr/games/game-data-packager -d ~/game-data roller-coaster-tycoon2 --package roller-coaster-tycoon2-data /path/to/RCT2`.",
  "Install it: `sudo apt install ~/game-data/roller-coaster-tycoon2-data_*_all.deb`. The data lands in `/usr/share/games/roller-coaster-tycoon2`, one of the places OpenRCT2 looks for it.",
  "Start OpenRCT2 from the menu.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

OpenRCT2 0.5.6 with Walnut's big-endian port (upstream builds only for little-endian CPUs), on its software
renderer: its OpenGL renderer needs OpenGL 3.3. The engine is free; RollerCoaster Tycoon 2's data is not, so
Walnut doesn't offer it: you make a package of it from your own copy (below). Nobody has played it on Walnut
yet. In the first benchmark (Electric Fields at 960x540, scaled 2x) it drew 21 to 24 frames a second, with
the console's memory nearly all in use.

## Setting up the game data

The tool is Debian's `game-data-packager`, from Debian's contrib section, which Walnut's package sources
don't list: step 1 adds it as a source of its own, so Walnut's updates leave it alone. It checks the files
against the checksums of the editions it knows and makes an ordinary `.deb`, which apt installs and can
remove again. On a Debian or Ubuntu PC the same commands work, and you can copy the `.deb` to the PS3 and
install it with `sudo apt install ./file.deb`.

The tool searches your home folder and the usual Steam and GOG places by itself; add `--no-search` to keep
it to the folder you name. Coming: Walnut Games will do the steps for you (a button on this page, and
`walnut-data import openrct2 /path/to/RCT2` in a terminal).
