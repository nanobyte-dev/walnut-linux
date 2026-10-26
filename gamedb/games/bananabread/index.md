+++
id = "bananabread"
title = "BananaBread"
summary = "A first person shooter on the Cube 2 engine, with bots and an in-game map editor: the BananaBread content in Debian's cube2-data."
categories = ["shooter"]
tags = ["fps", "multiplayer", "bots", "3d", "map editor"]
license = "Zlib"
homepage = "https://github.com/kripken/BananaBread"
packages = ["cube2", "cube2-data"]
patched_by = ["cube2"]
recipes = ["bananabread:base"]
needs_data = false
controller = "keyboard"
designed_for = "keyboard-mouse"
+++

BananaBread is Mozilla's Cube 2 engine demo (Debian's `cube2`, with the BananaBread maps of `cube2-data`),
the engine Sauerbraten runs on. It is the heaviest shader game of the set. Walnut's patches compress textures when they
are loaded, skin the models on the CPU (the RSX has no indirect constant addressing, so GPU skinning ran
at 11 frames a second against 28), and send fewer GL calls. Sending the world normals as 16-bit values
raises the rate to about 45 but is not the default yet.
