+++
id = "sauerbraten"
title = "Sauerbraten"
summary = "Cube 2: Sauerbraten, the 2020 Edition: a fast first person shooter with bots, over 200 maps and an in-game map editor."
categories = ["shooter"]
tags = ["fps", "multiplayer", "bots", "3d", "map editor"]
license = "Zlib"
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
release from sauerbraten.org into `/usr/share/games/sauerbraten` (see below). The menu entry starts it fullscreen at the desktop's resolution and keeps its settings
in `~/.sauerbraten`.

## Setting up the game data

Sauerbraten's maps, textures, models, sounds and music are not part of the package: their licences are mixed
and much of it is Creative Commons non-commercial, so Walnut does not distribute them. They are free to
download from the game's own site, and the steps unpack them where the launcher looks.

### Which edition works

The official 2020 Edition release for Linux, `sauerbraten_2020_12_29_linux.tar.bz2`, from sauerbraten.org (the
download is hosted on SourceForge; the link in the steps is the file's address there). Its SHA-256 sum is in
step 3. Debian's `cube2-data` is not this game: it holds the data of BananaBread, a different game on the same
engine.

### What you get

Only two folders of the release are used, `data` and `packages`; the release's own binaries and source stay
in the archive. They go to `/usr/share/games/sauerbraten`, and the launcher (`/usr/games/sauerbraten`, the
menu entry) points the engine there. The game starts fullscreen at the desktop's resolution and keeps its
settings in `~/.sauerbraten`. `SAUERBRATEN_WINDOWED=1` starts it in a window. The unpacked data takes 1.1 GB.

### Common problems

- The launcher prints `the game data is missing`: `/usr/share/games/sauerbraten` has to hold the folders
  `data` and `packages` directly. A `sauerbraten` folder inside it means `--strip-components=1` was left out.
- Not enough space on the disk: the archive and the unpacked data are about 2 GB together; delete the archive
  once it is unpacked.
