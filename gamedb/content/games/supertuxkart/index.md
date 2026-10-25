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
  "The textures are corrupted on the PS3 (the geometry is right): being investigated.",
  "Starts in a window; the controller is only found if it is on before the game starts, its default scheme is nonstandard, and the d-pad doesn't work in the menus (only the left stick).",
  "About 20 frames a second even at the lean settings: the PS3's PowerPC core and its memory limit what the game can draw.",
  "OpenAL logs buffer warnings in about half the runs; the sound has not been judged by ear yet.",
]
+++

SuperTuxKart 1.5 runs on its legacy OpenGL 2.1 renderer with Walnut's big-endian fixes: without them
the game quits while loading its track models. The tracks and karts draw correctly. It is the
heaviest game on the list: at the lean settings it races at 19 to 22 frames a second.
