# Setting up the game data

Sauerbraten's maps, textures, models, sounds and music are not part of the package: their licences are mixed
and much of it is Creative Commons non-commercial, so Walnut does not distribute them. They are free to
download from the game's own site, and the steps unpack them where the launcher looks.

## Which edition works

The official 2020 Edition release for Linux, `sauerbraten_2020_12_29_linux.tar.bz2`, from sauerbraten.org (the
download is hosted on SourceForge; the link in the steps on the Overview is the file's address there). Its SHA-256 sum is in
step 3 of those steps. Debian's `cube2-data` is not this game: it holds the data of BananaBread, a different game on the same
engine.

## What you get

Only two folders of the release are used, `data` and `packages`; the release's own binaries and source stay
in the archive. They go to `/usr/share/games/sauerbraten`, and the launcher (`/usr/games/sauerbraten`, the
menu entry) points the engine there. The game starts fullscreen at the desktop's resolution and keeps its
settings in `~/.sauerbraten`. `SAUERBRATEN_WINDOWED=1` starts it in a window. The unpacked data takes 1.1 GB.

## Common problems

- The launcher prints `the game data is missing`: `/usr/share/games/sauerbraten` has to hold the folders
  `data` and `packages` directly. A `sauerbraten` folder inside it means `--strip-components=1` was left out.
- Not enough space on the disk: the archive and the unpacked data are about 2 GB together; delete the archive
  once it is unpacked.
