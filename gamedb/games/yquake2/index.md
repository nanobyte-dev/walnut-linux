+++
id = "yquake2"
title = "Yamagi Quake II"
summary = "A modernised engine for id Software's Quake II, with the original single player and multiplayer game."
tags = ["fps", "sci-fi", "single-player", "online-multiplayer", "3d", "first-person"]
license = "GPL-2.0-or-later"
homepage = "https://www.yamagi.org/quake2/"
data_guide = "https://github.com/yquake2/yquake2#readme"
data_steps = [
  "In Walnut Games, open Yamagi Quake II and choose Add game files...: pick the disc in the drive, an ISO or BIN/CUE image, an installer or a folder that holds your copy.",
  "Or from a terminal: `walnut-data import yquake2 /path/to/your/copy`. The free demo needs no copy: choose Download the free demo (needs the network) in the chooser, or run `walnut-data import -p quake2-demo-data yquake2`.",
  "What it needs: Quake II's CD, or an installed copy (Steam's `common/Quake 2`, GOG's Quake II Quad Damage folder or installer): `baseq2/pak0.pak` and the `baseq2/video` folder. The 3.20 patch (pak1, pak2 and more) is downloaded unless your copy has it.",
  "The full game is installed as quake2-full-data in `/usr/share/games/quake2/baseq2` and appears in Play as Quake II; the demo (quake2-demo-data, `/usr/share/games/quake2-demo/baseq2`) as Quake II (demo).",
  "The CD's music (optional, quake2-music): from a BIN/CUE image its audio tracks are encoded to Ogg Vorbis; from the disc in the drive game-data-packager rips them. `walnut-data import -p quake2-music yquake2 /path/to/Quake2.cue` adds it alone; Quake II plays `baseq2/music/02.ogg`..`11.ogg` in place of the CD.",
  "If you have Walnut's licensed suite configured as an apt source, choose Install from Walnut licensed (or `walnut-data install-repo yquake2`) instead.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "Particles (blood, sparks, bullet trails) draw as big discs of one size at every distance (squares before Mesa 0050): fixed in the next Mesa and RSX driver builds.",
  "Movement is bound to E/S/D/F out of the box, not W/A/S/D (Yamagi's defaults): rebind it in Options, Customize controls. Fixed for new players in our next yquake2 (W/A/S/D and the pad's buttons by default, checked on the PS3).",
  "The menu and HUD text is small at 1080p.",
]
+++

Yamagi Quake II 8.70 runs on the gl1 renderer at about 70 frames a second on the Quake II demo
level. The engine is free but the game data is not: the demo's data is a free download that Walnut Games fetches for you and the full game's
comes from your own copy of Quake II (the Overview's Installation, and the Guides). Walnut's two patches byte-swap the 16-bit WAV sounds for the big-endian CPU and shrink the
engine's random number table from 32 MB to 64 KB, which cut the stalls from 22 s to 0.05 s in the
benchmark and saved about 70 MB of memory.
