+++
id = "supertuxkart"
title = "SuperTuxKart"
summary = "A 3D kart racer with items, many tracks and a family-friendly cast."
tags = ["racing", "family", "single-player", "local-multiplayer", "online-multiplayer", "3d", "kart"]
license = "GPL-3.0-or-later"
homepage = "https://supertuxkart.net/"
controller = "partial"
known_issues = [
  "About 24 frames a second in races at the lean settings: light tracks play well, heavy ones (Ravenbridge Mansion) are slow.",
  "Loading a track takes about 20 seconds.",
]
+++

SuperTuxKart 1.5 runs on its legacy OpenGL 2.1 renderer with Walnut's big-endian fixes: without them
the game quits while loading its track models. The tracks and karts draw correctly. It is the
heaviest game on the list: at the lean settings it races at 19 to 22 frames a second.
