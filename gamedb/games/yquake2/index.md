+++
id = "yquake2"
title = "Yamagi Quake II"
summary = "A modernised engine for id Software's Quake II, with the original single player and multiplayer game."
categories = ["shooter"]
tags = ["fps", "classic", "3d", "single player"]
license = "GPL-2.0-or-later"
homepage = "https://www.yamagi.org/quake2/"
data_guide = "https://github.com/yquake2/yquake2#readme"
data_steps = [
  "In Walnut Games, open Yamagi Quake II and choose Add game files...: pick the disc in the drive, an ISO, an installer or a folder that holds your copy.",
  "Or from a terminal: `walnut-data import yquake2 /path/to/your/copy`. The free demo needs no copy: choose Download the free demo (needs the network) in the chooser, or run `walnut-data import -p quake2-demo-data yquake2`.",
  "What it needs: Quake II's CD, or an installed copy (Steam's `common/Quake 2`, GOG's Quake II Quad Damage folder or installer): `baseq2/pak0.pak` and the `baseq2/video` folder. The 3.20 patch (pak1, pak2 and more) is downloaded unless your copy has it.",
  "The full game is installed as quake2-full-data in `/usr/share/games/quake2/baseq2` and appears in Play as Quake II; the demo (quake2-demo-data, `/usr/share/games/quake2-demo/baseq2`) as Quake II (demo).",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo yquake2`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "Particles (blood, sparks, bullet trails) draw as big discs of one size at every distance (squares before Mesa 0050): fixed in the next Mesa and RSX driver builds.",
  "Movement is bound to E/S/D/F out of the box, not W/A/S/D (Yamagi's defaults): rebind it in Options, Customize controls.",
  "The menu and HUD text is small at 1080p.",
]
+++

Yamagi Quake II 8.70 runs on the gl1 renderer at about 70 frames a second on the Quake II demo
level. The engine is free but the game data is not: the demo's data is a free download that Walnut Games fetches for you and the full game's
comes from your own copy of Quake II (see below). Walnut's two patches byte-swap the 16-bit WAV sounds for the big-endian CPU and shrink the
engine's random number table from 32 MB to 64 KB, which cut the stalls from 22 s to 0.05 s in the
benchmark and saved about 70 MB of memory.

## Setting up the game data

Walnut Games makes the data for you. Open Yamagi Quake II in the app and choose Add game files...; the chooser lists the disc in the drive, mounted discs and sticks, GOG and Steam folders, and the ISOs, installers and archives in Downloads and your home folder. For the free demo choose Download the free demo (needs the network): nothing to find. In a terminal it is `walnut-data import yquake2 /path/to/your/copy` for the full game and `walnut-data import -p quake2-demo-data yquake2` for the demo. It runs Walnut's patched `game-data-packager` and installs an ordinary package that Remove data takes away again.

### Which editions work

- **The demo** (free): version 3.14, downloaded by game-data-packager itself (no copy or installer needed). Package `quake2-demo-data`; the Quake II (demo) menu entry starts it.
- **The full game**: package `quake2-full-data`. It needs `baseq2/pak0.pak` and the `baseq2/video` folder of your copy: a CD, a Steam `common/Quake 2` folder, or GOG's Quake II Quad Damage (the installed folder or `setup_quake2_quad_damage_*.exe`). The import downloads the 3.20 patch (the official patch files) unless the copy already has it.
- The expansions (`quake2-reckoning-data`, `quake2-groundzero-data`, `quake2-zaero-data`) are packages of the same tool; they have not been tried on the PS3.

### What you get

The demo is installed in `/usr/share/games/quake2-demo/baseq2` and the full game in `/usr/share/games/quake2/baseq2`; the two use different folders. The Quake II (demo) entry passes `-datadir /usr/share/games/quake2-demo`, and the Quake II entry (shown once `baseq2/pak0.pak` is there) passes `-datadir /usr/share/games/quake2`. With Walnut's licensed suite as an apt source the page offers Install from Walnut licensed instead.
