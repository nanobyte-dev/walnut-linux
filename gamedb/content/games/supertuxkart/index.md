+++
id = "supertuxkart"
title = "SuperTuxKart"
summary = "A 3D kart racer with items, many tracks and a family-friendly cast."
categories = ["racing"]
tags = ["kart", "3d", "multiplayer", "family"]
license = "GPL-3.0-or-later"
homepage = "https://supertuxkart.net/"
packages = ["supertuxkart", "supertuxkart-data"]
patched_by = ["supertuxkart"]
recipes = ["supertuxkart:lean"]
needs_data = false
controller = "partial"
known_issues = [
  "On heavy tracks (Ravenbridge Mansion) the GPU's memory runs out: objects pop in and out, the game can freeze for many seconds and crash (a Mesa fix is in progress).",
  "About 30 frames a second at the lean settings, with long stalls on heavy tracks.",
]
+++

SuperTuxKart 1.5 runs on its legacy OpenGL 2.1 renderer with Walnut's big-endian fixes: without them
the game quits while loading its track models. The tracks and karts draw correctly. It is the
heaviest game on the list: at the lean settings it races at 19 to 22 frames a second.
