+++
id = "luanti"
title = "Luanti"
summary = "A voxel sandbox engine for building and exploring, with Minetest Game and thousands of mods."
tags = ["sandbox", "adventure", "single-player", "online-multiplayer", "3d", "first-person", "mods", "voxel", "building"]
license = "LGPL-2.1-or-later"
homepage = "https://www.luanti.org/"
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "Starts in a window instead of fullscreen.",
  "The options menu is broken.",
]
+++

Luanti 5.15.2 runs on Walnut's own build, since Debian ships none for this CPU, with LuaJIT and the
Minetest Game. It flies the benchmark path at about 46 to 50 frames a second, with 1.5 to 3 s of stalls
as new map blocks are generated and meshed. The patches cut the OpenGL calls per draw, fix the Ogg
Vorbis decoding for big-endian (the sounds were noise) and a LuaJIT allocation bug that made the
game fail to start about 4 times in 10.
