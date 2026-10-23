+++
id = "xmoto"
title = "X-Moto"
summary = "A 2D motorbike trials game: ride through physics-based levels without falling off, collecting strawberries."
categories = ["platformer", "racing"]
tags = ["2d", "physics", "bike", "levels"]
license = "GPL-2.0-or-later"
homepage = "https://xmoto.tuxfamily.org/"
packages = ["xmoto", "xmoto-data"]
patched_by = ["xmoto"]
recipes = ["xmoto:base"]
needs_data = false
controller = "keyboard"
known_issues = ["Controllers need setting up by hand: \"Enable joysticks\" is off by default, pads are not given to players 1 to 4 by themselves, and a pad and the keyboard don't work together, so switching between them means changing the options again. Play with the keyboard."]
+++

X-Moto 0.6.3 runs at 1920x1080 with the right picture and sound. Walnut's package batches the game's
OpenGL drawing (the first version made about 18,500 GL calls a frame, now 165), which took the
benchmark replay from 44 to about 160 frames a second. The test replays the game's built-in credits
replay twice and measures the second pass.
