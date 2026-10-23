+++
id = "sauerbraten"
title = "Sauerbraten"
summary = "A fast multiplayer first person shooter on the Cube 2 engine, with an in-game map editor."
categories = ["shooter"]
tags = ["fps", "multiplayer", "bots", "3d", "map editor"]
license = "Zlib"
homepage = "http://sauerbraten.org/"
packages = ["cube2", "cube2-data"]
patched_by = ["cube2"]
recipes = ["sauerbraten:base"]
needs_data = false
controller = "keyboard"
+++

Sauerbraten (the Cube 2 engine, package `cube2`) plays a recorded round with two bots at about 39
frames a second. It is the heaviest shader game of the set. Walnut's patches compress textures when they
are loaded, skin the models on the CPU (the RSX has no indirect constant addressing, so GPU skinning ran
at 11 frames a second against 28), and send fewer GL calls. Sending the world normals as 16-bit values
raises the rate to about 45 but is not the default yet.
