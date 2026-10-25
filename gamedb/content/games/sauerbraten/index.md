+++
id = "sauerbraten"
title = "Sauerbraten"
summary = "Cube 2: Sauerbraten, the 2020 Edition: a fast first person shooter with bots, over 200 maps and an in-game map editor."
categories = ["shooter"]
tags = ["fps", "multiplayer", "bots", "3d", "map editor"]
license = "Zlib"
homepage = "http://sauerbraten.org/"
packages = ["sauerbraten", "cube2"]
desktop = ["sauerbraten.desktop"]
patched_by = ["cube2"]
recipes = ["sauerbraten-edition:base"]
needs_data = true
data_guide = "http://sauerbraten.org/"
controller = "keyboard"
known_issues = [
  "Video memory runs full on the big maps, and Mesa then crashed: after 3.5 minutes online (its occlusion queries, fixed in Mesa ps3.819) and after 6 minutes with bots (a buffer allocation that failed, fixed in Mesa ps3.827 with patches 0047 and 0048, not yet played again). New installs compress more textures (texcompress 512); an existing config keeps its own setting.",
]
+++

Sauerbraten is the original game of the Cube 2 engine: the 2020 Edition release with its maps, models, sounds and
music. The engine is free (zlib licence) and is what Walnut's `cube2` package builds, with the same patches as
BananaBread, which runs on the same engine with different content: textures are compressed when they load, models
are skinned on the CPU (the RSX has no indirect constant addressing) and fewer GL calls are sent. The game's data
has mixed licences, much of it Creative Commons non-commercial, so Walnut offers it only in its licensed repository
(`sauerbraten-data`); otherwise you copy the `data` and `packages` folders of the release from sauerbraten.org to
`/usr/share/games/sauerbraten`. The menu entry starts it fullscreen at the desktop's resolution and keeps its settings
in `~/.sauerbraten`.
