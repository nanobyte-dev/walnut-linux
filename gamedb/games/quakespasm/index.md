+++
id = "quakespasm"
title = "QuakeSpasm"
summary = "A modern source port of id Software's Quake, with the original campaign and mods."
categories = ["shooter"]
tags = ["fps", "classic", "3d", "single player", "mods"]
license = "GPL-2.0-or-later"
homepage = "https://quakespasm.sourceforge.net/"
packages = ["quakespasm"]
recipes = ["quakespasm:base"]
needs_data = true
data_guide = "https://quakespasm.sourceforge.net/"
data_steps = [
  "Turn on Debian's contrib section once, where the tool that packs game data lives: `printf 'Types: deb\\nURIs: http://deb.debian.org/debian-ports\\nSuites: unstable unreleased\\nComponents: contrib\\n' | sudo tee /etc/apt/sources.list.d/debian-contrib.sources`.",
  "Install the tool: `sudo apt update && sudo apt install game-data-packager lhasa`.",
  "Download the free Quake shareware (9 MB, the first episode) and make a package from it: `mkdir -p ~/game-data && /usr/games/game-data-packager -d ~/game-data quake --package quake-shareware`.",
  "Install it: `sudo apt install ~/game-data/quake-shareware_*_all.deb`. The data lands in `/usr/share/games/quake/id1`, where the menu entries look.",
  "For the full game from your own copy (CD, GOG or Steam), use this instead of the last two steps, with the folder that holds `id1/pak0.pak` and `id1/pak1.pak`: `/usr/games/game-data-packager -d ~/game-data quake --package quake-registered /path/to/Quake`, then `sudo apt install ~/game-data/quake-registered_*_all.deb`.",
  "Start QuakeSpasm (GLSL) from the menu.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

QuakeSpasm 0.96.3 is Debian's package, unpatched, and plays the shareware demo1 timedemo at about 91
frames a second. It is started with `-nopackedpixels`, because 0.96.3's packed lightmaps show wrong
colours on a big-endian CPU. It needs Quake's `id1/pak0.pak` (the shareware one is enough for the
first episode); the data is not part of the package. A few frames take up to 400 ms, 2.5 s in
total over a two minute window.

## Setting up the game data

Quake's data is not free, so Walnut does not offer it. You get it on the PS3 in one of two ways: the free
shareware, which the tool downloads for you, or the full game from your own copy.

The tool that packs the data is Debian's `game-data-packager`. It is free software, but it sits in Debian's
contrib section (the data it handles is not free), and Walnut's package sources only list main. Step 1 adds
contrib as a source of its own (`/etc/apt/sources.list.d/debian-contrib.sources`), so Walnut's updates leave it
alone. Debian publishes the tool for the PS3's architecture (it is an `all` package). It makes an ordinary
`.deb` of the data in the folder you give with `-d`, and apt installs it and can remove it again. On a
Debian or Ubuntu PC the same two commands work, and you can copy the `.deb` to the PS3 and install it there
with `sudo apt install ./file.deb`.

### Which editions work

- **Shareware** (free): version 1.06, the first episode. Package `quake-shareware`;
  nothing to supply.
- **The full game**: package `quake-registered`. It needs `id1/pak0.pak` and `id1/pak1.pak` of your copy. Pass
  the tool the folder that holds `id1`, or the CD-ROM's mount point. A Steam copy is the `common/Quake` folder
  of its library, copied from the PC that has Steam. A GOG copy (Quake: The Offering) is either the installed
  folder, or the folder holding the installer `setup_quake_the_offering_*.exe`, which the tool unpacks if
  `innoextract` is installed (`sudo apt install innoextract`). The tool fetches the 1.06 patch itself, from the
  shareware download.
- The mission packs (`quake-armagon`, `quake-dissolution`) and other add-ons are packages of the same tool
  (`game-data-packager quake --help` lists them). They have not been tried on the PS3.

### What you get

The data ends up in `/usr/share/games/quake/id1`. The menu entries QuakeSpasm (GLSL) and QuakeSpasm (no GLSL)
read `/usr/share/games/quake`, so they play whichever edition is installed. `quake-registered` replaces
`quake-shareware` (they cannot be installed together), so the full game takes over the same folder.

### Common problems

- `Unable to complete any packages` with `apt-get install innoextract lhasa` in its message: `lhasa` is
  missing. Quake's shareware archive is LHA-packed. Install it and run the command again.
- `argument ... to --destination does not exist`: the `-d` folder has to exist first (`mkdir -p ~/game-data`).
- `Unable to locate package game-data-packager`: step 1 was skipped, or `sudo apt update` has not run since.
- The tool searches your home folder and the usual Steam and GOG places for Quake files by itself; to keep it
  to the folder you name, add `--no-search`.
