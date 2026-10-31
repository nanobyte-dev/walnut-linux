+++
id = "sauerbraten"
title = "Sauerbraten"
summary = "Cube 2: Sauerbraten, the 2020 Edition: a fast first person shooter with bots, over 200 maps and an in-game map editor."
tags = ["fps", "arena", "single-player", "online-multiplayer", "bots", "3d", "first-person", "map-editor"]
license = "Zlib"
license_source = "our debian/copyright of sauerbraten-walnut (Zlib, sauerbraten.org) and Debian's of cube2 0.0.20201229+dfsg-1"
homepage = "http://sauerbraten.org/"
data_guide = "http://sauerbraten.org/"
data_steps = [
  "Install the game with the Install button, or `sudo apt install sauerbraten`.",
  "Download the official release, Sauerbraten 2020 Edition for Linux (about 940 MB): `wget -O ~/sauerbraten_2020_12_29_linux.tar.bz2 https://sourceforge.net/projects/sauerbraten/files/sauerbraten/2020_11_29/sauerbraten_2020_12_29_linux.tar.bz2/download`.",
  "Check it: `sha256sum ~/sauerbraten_2020_12_29_linux.tar.bz2` prints cdba7c4a47cefd30d0afdd6a912199a1384319cf1619923cb7189e72e468be70.",
  "Unpack the game's `data` and `packages` folders (1.1 GB, a few minutes) into place: `sudo mkdir -p /usr/share/games/sauerbraten && sudo tar -xjf ~/sauerbraten_2020_12_29_linux.tar.bz2 --strip-components=1 --no-same-owner -C /usr/share/games/sauerbraten sauerbraten/data sauerbraten/packages`.",
  "Free the space again: `rm ~/sauerbraten_2020_12_29_linux.tar.bz2`. Then start Sauerbraten from the menu.",
]
controller = "keyboard"
designed_for = "keyboard-mouse"
known_issues = [
  "Video memory runs full on the big maps, and Mesa then crashed: after 3.5 minutes online (its occlusion queries, fixed in Mesa ps3.819) and after 6 minutes with bots (a buffer allocation that failed, fixed in Mesa ps3.827 with patches 0047 and 0048, not yet played again). New installs compress more textures (texcompress 512); an existing config keeps its own setting.",
]
+++

Sauerbraten is the original game of the Cube 2 engine: the 2020 Edition release with its maps, models, sounds and
music. The engine is free (zlib licence) and is what Walnut's `cube2` package builds, with the same patches as
BananaBread, which runs on the same engine with different content: textures are compressed when they load, models
are skinned on the CPU (the RSX has no indirect constant addressing) and fewer GL calls are sent. The game's data
has mixed licences, much of it Creative Commons non-commercial, so Walnut offers it only in its licensed repository
(`sauerbraten-data`); otherwise you unpack the `data` and `packages` folders of the
release from sauerbraten.org into `/usr/share/games/sauerbraten` (the Guides). The menu entry starts it fullscreen at the desktop's resolution and keeps its settings
in `~/.sauerbraten`.
