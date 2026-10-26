+++
id = "yquake2"
title = "Yamagi Quake II"
summary = "A modernised engine for id Software's Quake II, with the original single player and multiplayer game."
categories = ["shooter"]
tags = ["fps", "classic", "3d", "single player"]
license = "GPL-2.0-or-later"
homepage = "https://www.yamagi.org/quake2/"
packages = ["yamagi-quake2", "yamagi-quake2-core"]
patched_by = ["yquake2"]
recipes = ["yquake2:kiss4k"]
needs_data = true
data_guide = "https://github.com/yquake2/yquake2#readme"
data_steps = [
  "Turn on Debian's contrib section once, where the tool that packs game data lives: `printf 'Types: deb\\nURIs: http://deb.debian.org/debian-ports\\nSuites: unstable unreleased\\nComponents: contrib\\n' | sudo tee /etc/apt/sources.list.d/debian-contrib.sources`.",
  "Install the tool: `sudo apt update && sudo apt install game-data-packager`.",
  "Download the free Quake II demo (39 MB) and make a package from it: `mkdir -p ~/game-data && /usr/games/game-data-packager -d ~/game-data quake2 --package quake2-demo-data`.",
  "Install it: `sudo apt install ~/game-data/quake2-demo-data_*_all.deb`. The data lands in `/usr/share/games/quake2-demo/baseq2`, where the Quake II (demo) menu entry looks.",
  "For the full game from your own copy (CD, GOG or Steam), use this instead of the last two steps, with the folder that holds `baseq2/pak0.pak` and the `baseq2/video` folder: `/usr/games/game-data-packager -d ~/game-data quake2 --package quake2-full-data /path/to/Quake2`, then `sudo apt install ~/game-data/quake2-full-data_*_all.deb`. The data lands in `/usr/share/games/quake2/baseq2`, which the demo's menu entry does not use: start the full game from a terminal with `ps3-game /usr/lib/yamagi-quake2/quake2 +set vid_renderer gl1 +set vid_fullscreen 1 +set r_mode -2`.",
  "Start Quake II (demo) from the menu.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

Yamagi Quake II 8.70 runs on the gl1 renderer at about 70 frames a second on the Quake II demo
level. The engine is free but the game data is not: the demo's data is a free download and the full game's
comes from your own copy of Quake II (see below). Walnut's two patches byte-swap the 16-bit WAV sounds for the big-endian CPU and shrink the
engine's random number table from 32 MB to 64 KB, which cut the stalls from 22 s to 0.05 s in the
benchmark and saved about 70 MB of memory.

## Setting up the game data

Quake II's data is not free, so Walnut does not offer it. You get it on the PS3 in one of two ways: the free
demo, which the tool downloads for you, or the full game from your own copy.

The tool that packs the data is Debian's `game-data-packager`. It is free software, but it sits in Debian's
contrib section (the data it handles is not free), and Walnut's package sources only list main. Step 1 adds
contrib as a source of its own (`/etc/apt/sources.list.d/debian-contrib.sources`), so Walnut's updates leave it
alone. Debian publishes the tool for the PS3's architecture (it is an `all` package). It makes an ordinary
`.deb` of the data in the folder you give with `-d`, and apt installs it and can remove it again. On a
Debian or Ubuntu PC the same two commands work, and you can copy the `.deb` to the PS3 and install it there
with `sudo apt install ./file.deb`.

### Which editions work

- **The demo** (free): version 3.14. Package `quake2-demo-data`; nothing to supply. This is the
  data the Quake II (demo) menu entry starts.
- **The full game**: package `quake2-full-data`. It needs `baseq2/pak0.pak` and the `baseq2/video` folder of
  your copy. Pass the tool the folder that holds `baseq2`, or the CD-ROM's mount point. A Steam copy is the
  `common/Quake 2` folder of its library, copied from the PC that has Steam. A GOG copy (Quake II Quad Damage)
  is either the installed folder, or the folder holding the installer `setup_quake2_quad_damage_*.exe`, which
  the tool unpacks if `innoextract` is installed (`sudo apt install innoextract`). The tool downloads the
  3.20 patch itself unless your copy already has it.
- The expansions (`quake2-reckoning-data`, `quake2-groundzero-data`, `quake2-zaero-data`) are packages of the
  same tool (`game-data-packager quake2 --help` lists them). They have not been tried on the PS3.

### What you get

The demo is installed in `/usr/share/games/quake2-demo/baseq2` and the full game in
`/usr/share/games/quake2/baseq2`. The two packages use different folders. The Quake II (demo) menu entry
passes `-datadir /usr/share/games/quake2-demo`, so it always plays the demo. Yamagi Quake II looks in
`/usr/share/games/quake2` when it is given no `-datadir` (checked in Debian's binary), so the full game starts
from a terminal with the same command as the menu entry minus that option:

    ps3-game /usr/lib/yamagi-quake2/quake2 +set vid_renderer gl1 +set vid_fullscreen 1 +set r_mode -2

The full game has no menu entry of its own yet.

### Common problems

- `argument ... to --destination does not exist`: the `-d` folder has to exist first (`mkdir -p ~/game-data`).
- `Unable to locate package game-data-packager`: step 1 was skipped, or `sudo apt update` has not run since.
- The tool searches your home folder and the usual Steam and GOG places for Quake II files by itself; to keep
  it to the folder you name, add `--no-search`.
