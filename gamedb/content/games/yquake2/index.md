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
controller = "keyboard"
+++

Yamagi Quake II 8.70 runs on the gl1 renderer at about 70 frames a second on the Quake II demo
level. The engine is free but the game data is not: you copy the `baseq2` folder from your own copy of
Quake II. Walnut's two patches byte-swap the 16-bit WAV sounds for the big-endian CPU and shrink the
engine's random number table from 32 MB to 64 KB, which cut the stalls from 22 s to 0.05 s in the
benchmark and saved about 70 MB of memory.
